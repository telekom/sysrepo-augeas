/**
 * @file main.c
 * @author Adam Piecek <piecek@cesnet.cz>
 * @brief Main code for augyang executable file.
 *
 * @copyright
 * Copyright (c) 2021 Deutsche Telekom AG.
 * Copyright (c) 2021 CESNET, z.s.p.o.
 *
 * This source code is licensed under BSD 3-Clause License (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://opensource.org/licenses/BSD-3-Clause
 *
 *
 * @section AY_OVERVIEW Overview
 *
 * The Augyang was created for generating YANG module (.yang) from Augeas module (.aug) which is described by the
 * Augeas language. This language is quite complicated, so development of Augyang has been facilitated by the use of
 * internal Augeas header files. The source code in the Augeas project did not have to be modified in any way, only the
 * internal functions are used. By default, static libraries are created when compiling the Augeas project, which only
 * needs to be linked with Augyang.
 *
 * Augyang is therefore dependent on Augeas' output which is an internal tree of basic building blocks called Lenses
 * that Augeas uses to process some configuration file. Augyang is able to gather the necessary information from the
 * Tree of Lenses and uses it to create its own internal tree, which it transforms in various ways until it is finally
 * ready to print the YANG module.
 * (So that's how augyang works. )
 *
 * The development of Augyang is iterative, because Augeas modules can be very different and there is no guarantee that
 * the generated YANG file will be correct. The goal is to make Augyang ultimately usable for ordinary purposes.
 *
 * Another important topic is special data-paths that are written to generated YANG files. The context is that Augeas
 * parses some configuration file and creates a data tree from it, which can be modified using the augtool tool and
 * therefore changing the original configuration file. So these special data paths in the YANG file are used for
 * mapping with the Augeas data tree. Thanks to this mapping, the Sysrepo plugin can manipulate data.
 *
 * The current situation is such that Augyang is further improved on the basis of Augeas modules. It is not known how
 * much is left, because the tasks appear on the fly. However, in the future, reliable regular expression conversion
 * must be added because Augeas and Yang use different formats.
 */

#define _GNU_SOURCE

#include "ayg_config.h"

#include <dirent.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libyang/libyang.h>

#include "augeas.h"
#include "augyang.h"
#include "errcode.h"
#include "list.h"
#include "syntax.h"

#include "../modules/augeas-extension.h"

/**
 * @brief Result of strlen("/").
 */
#define AYM_SLASH_LEN 1

/**
 * @brief Result of strlen(".yang").
 */
#define AYM_SUFF_YANG_LEN 5

/**
 * @brief Result of strlen(".aug").
 */
#define AYM_SUFF_AUG_LEN 4

/**
 * @brief Name of the binary file.
 */
#define AYM_PROGNAME "augyang"

/**
 * @brief Print help to stderr.
 */
static void
aym_usage(void)
{
    fprintf(stderr, "Usage: "AYM_PROGNAME " [OPTIONS] MODULE...\n");
    fprintf(stderr, "Generate YANG module (.yang) from Augeas MODULE (.aug).\n");
    fprintf(stderr, "Information about the YANG format is in the RFC 7950.\n");
    fprintf(stderr, "\nOptions:\n\n");
    fprintf(stderr,
            "  -e, --explicit     default value of the -I parameter is not used;\n"
            "                     only the directories specified by the -I parameter are used\n");
    fprintf(stderr,
            "  -I, --include DIR  search DIR for augeas modules; can be given multiple times;\n"
            "                     default value: %s\n", AUGEAS_LENSES_DIR);
    fprintf(stderr,
            "  -O, --outdir DIR   directory in which the generated yang file is written;\n"
            "                     default value: ./\n");
    fprintf(stderr,
            "  -q, --quiet        generated yang is not printed or written to the file\n");
    fprintf(stderr,
            "  -s, --show         print the generated yang only to stdout and not to the file\n");
    fprintf(stderr,
            "  -t, --typecheck    typecheck lenses. Recommended to use during lense development.\n");
    fprintf(stderr,
            "  -v, --verbose HEX  bitmask for various debug outputs\n");
    fprintf(stderr,
            "  -y, --yanglint     validates the YANG module\n");
    fprintf(stderr, "\nExample:\n"
            AYM_PROGNAME " passwd backuppchosts\n"
            AYM_PROGNAME " -e -I ./mylenses -O ./genyang someAugfile\n");
}

/**
 * @brief Convert verbose code in string format to unsigned integer.
 *
 * @param[in] optarg String from command line.
 * @param[out] vercode Verbose code.
 * @return 0 on success.
 */
static int
aym_get_vercode(char *optarg, uint64_t *vercode)
{
    int ret = 0;
    char *endptr;

    if (optarg[0] == '-') {
        fprintf(stderr, "ERROR: Verbose code cannot be negative number\n");
        aym_usage();
        return -1;
    }

    errno = 0;
    *vercode = strtoull(optarg, &endptr, 16);
    if (errno || (&optarg[strlen(optarg)] != endptr)) {
        fprintf(stderr, "ERROR: Verbose code conversion error\n");
        ret = -1;
    }

    return ret;
}

/**
 * @brief Add path (string item) to the loadpath variable.
 *
 * @param[in,out] loadpath Storage of paths (strings) separated by PATH_SEP_CHAR.
 * @param[in,out] loadpathlen Length of whole loadpath string, excluding the terminating null byte.
 * @param[in] item Path that will be added to @p loadpath.
 * @return 0 if operation success.
 */
static int
aym_loadpath_add(char **loadpath, uint64_t *loadpathlen, char *item)
{
    int ret = 0;
    uint64_t new_loadpathlen, itemlen;
    char *new_loadpath;

    if (!*loadpath) {
        *loadpath = strdup(item);
        *loadpathlen = strlen(item);
        ret = *loadpath ? 0 : 1;
    } else if (item) {
        /* allocate enough memory space */
        itemlen = strlen(item);
        /* loadpathlen + 'null byte' + 'PATH_SEP_CHAR' + itemlen + 'null byte' */
        new_loadpathlen = *loadpathlen + itemlen + 3;
        new_loadpath = realloc(*loadpath, new_loadpathlen);
        if (!new_loadpath) {
            free(*loadpath);
            *loadpath = NULL;
            return 1;
        }
        *loadpath = new_loadpath;

        /* copy item to loadpath */
        (*loadpath)[*loadpathlen] = PATH_SEP_CHAR;
        strcpy(*loadpath + *loadpathlen + 1, item);
        *loadpathlen = new_loadpathlen;
    }

    return ret;
}

/**
 * @brief Get next path from the loadpath variable.
 *
 * @param[in] loadpath_iter Pointer to some item (path/string) in the loadpath.
 * @return Next item or NULL.
 */
static char *
aym_loadpath_next(const char *loadpath_iter)
{
    char *next;

    if (!loadpath_iter) {
        return NULL;
    } else if ((next = strchr(loadpath_iter, PATH_SEP_CHAR))) {
        return next + 1;
    } else {
        return NULL;
    }
}

/**
 * @brief Get length of @p loadpath_item (path/string).
 *
 * @param[in] loadpath_item Pointer to some item (path/string) in the loadpath.
 * @return Length of item.
 */
static size_t
aym_loadpath_pathlen(const char *loadpath_item)
{
    char *retval;

    if (!loadpath_item) {
        return 0;
    } else if ((retval = strchr(loadpath_item, PATH_SEP_CHAR))) {
        return retval - loadpath_item;
    } else {
        return strlen(loadpath_item);
    }
}

/**
 * @brief Find the size of the longest path (string) in the loadpath
 *
 * @param[in] loadpath Array of strings.
 * @return The number of characters in the longest string.
 */
static size_t
aym_loadpath_maxpath(char *loadpath)
{
    size_t ret = 0, len;
    char *iter;

    for (iter = loadpath; iter; iter = aym_loadpath_next(iter)) {
        len = aym_loadpath_pathlen(iter);
        ret = len > ret ? len : ret;
    }

    return ret;
}

/**
 * @brief Check if the path points to a directory.
 *
 * @param[in] path Path to check.
 * @return 1 if path points to existing directory, otherwise 0.
 */
static bool
aym_dir_exists(const char *path)
{
    DIR *dir;

    dir = opendir(path);
    if (dir) {
        closedir(dir);
        return 1;
    } else {
        return 0;
    }
}

/**
 * @brief Check if the path points to a file.
 *
 * @param[in] path Path to check.
 * @return 1 if path points to existing file, otherwise 0.
 */
static bool
aym_file_exists(const char *path)
{
    FILE *file;

    file = fopen(path, "r");
    if (file) {
        fclose(file);
        return 1;
    } else {
        return 0;
    }
}

/**
 * @brief Insert directory path to the file name.
 *
 * @param[in] dirpath String to insert.
 * @param[in,out] filename Sufficiently large buffer already containing file name.
 * The output form will be "dirpath<slash>filename".
 */
static void
aym_insert_dirpath(const char *dirpath, char *filename)
{
    uint64_t dirpathlen;

    dirpathlen = aym_loadpath_pathlen(dirpath),
    filename[dirpathlen + AYM_SLASH_LEN + strlen(filename)] = '\0';
    memmove(filename + dirpathlen + AYM_SLASH_LEN, filename, strlen(filename));
    memcpy(filename, dirpath, dirpathlen);
    filename[dirpathlen] = '/';
}

/**
 * @brief Remove directory path and leave only the file name.
 *
 * This function is opposite of insert_dirpath().
 *
 * @param[in] dirpath string to remove from @p filename.
 * @param[in,out] filename Buffer which begins with the string @p dirpath.
 * The output form will be "filename".
 */
static void
aym_remove_dirpath(const char *dirpath, char *filename)
{
    uint64_t new_end, dirpathlen;

    dirpathlen = aym_loadpath_pathlen(dirpath),
    assert(!strncmp(filename, dirpath, dirpathlen));
    new_end = strlen(filename) - (dirpathlen + AYM_SLASH_LEN);
    memmove(filename, filename + dirpathlen + AYM_SLASH_LEN, new_end);
    filename[new_end] = '\0';
}

/**
 * @brief Find out in which directory path the module @p filename is located.
 *
 * @param[in] loadpath Array of paths separated by PATH_SEP_CHAR.
 * @param[in] filename Buffer containing file name. It is not modified.
 * @return Directory path from @p loadpath.
 */
static const char *
aym_find_aug_module(char *loadpath, char *filename)
{
    bool succ = 0;
    char *iter, *loadpath_item;

    for (iter = loadpath; !succ && iter; iter = aym_loadpath_next(iter)) {
        loadpath_item = iter;
        aym_insert_dirpath(iter,  filename);
        succ = aym_file_exists(filename);
        aym_remove_dirpath(iter, filename);
    }

    return succ ? loadpath_item : NULL;
}

/**
 * @brief Create filename by name and suffix.
 *
 * @param[in] name Name of the file.
 * @param[in] suffix Name of the suffix to add.
 * @param[in] dash Setting the flag replaces '_' character with the '-'.
 * @param[in,out] filename Sufficiently large buffer which will be overwritten.
 * The output form will be "<name><suffix>".
 */
static void
aym_insert_filename(const char *name, const char *suffix, int dash, char *filename)
{
    uint64_t i, len;

    len = strlen(name);
    if (dash) {
        for (i = 0; i < len; i++) {
            filename[i] = name[i] == '_' ? '-' : name[i];
        }
    } else {
        strcpy(filename, name);
    }
    strcpy(&filename[len], suffix);
}

/**
 * @brief Allocate filename buffer.
 *
 * The function guarantees that it allocates enough space for operations with filename buffer in this source file.
 *
 * @param[in] argc Variable from main.
 * @param[in] argv Variable from main.
 * @param[in] optind Global variable from ::getopt_long().
 * @param[in] outdir Option --outdir DIR from command line.
 * @param[in] loadpath Storage of paths.
 * @return Pointer to new allocated buffer or NULL.
 */
static char *
aym_allocate_filename_buffer(int argc, char **argv, int optind, char *outdir, char *loadpath)
{
    char *buffer;
    size_t maxpathlen, maxmodname = 0, maxyang, maxaug, buffer_size;
    char *modname;

    maxpathlen = aym_loadpath_maxpath(loadpath);
    for (int i = 0; i < argc - optind; i++) {
        modname = argv[optind + i];
        maxmodname = strlen(modname) > maxmodname ? strlen(modname) : maxmodname;
    }
    maxyang = strlen(outdir) + AYM_SLASH_LEN + maxmodname + AYM_SUFF_YANG_LEN;
    maxaug = maxpathlen + AYM_SLASH_LEN + maxmodname + AYM_SUFF_AUG_LEN;

    buffer_size = maxyang > maxaug ? maxyang : maxaug;
    buffer = malloc(buffer_size + 1);

    return buffer;
}

int
main(int argc, char **argv)
{
    int opt, ret = 0, rv, explicit = 0, show = 0, quiet = 0, yanglint = 0;
    struct augeas *aug = NULL;
    char *loadpath = NULL, *str = NULL, *modname, *outdir = NULL;
    const char *dirpath;
    size_t loadpathlen = 0;
    struct module *mod;
    char *filename = NULL;
    FILE *file = NULL;
    uint64_t vercode = 0;
    struct module *mod_iter;
    struct ly_ctx *ctx = NULL;
    LY_ERR err;

    struct option options[] = {
        {"help",      0, 0, 'h'},
        {"explicit",  0, 0, 'e'},
        {"include",   1, 0, 'I'},
        {"outdir",    1, 0, 'O'},
        {"quiet",     0, 0, 'q'},
        {"show",      0, 0, 's'},
        {"typecheck", 0, 0, 't'},
        {"verbose",   1, 0, 'v'},
        {"yanglint",  0, 0, 'y'},
        {0, 0, 0, 0}
    };
    int idx;
    unsigned int flags = AUG_NO_MODL_AUTOLOAD | AUG_NO_LOAD;

    while ((opt = getopt_long(argc, argv, "heI:O:qstv:y", options, &idx)) != -1) {
        switch (opt) {
        case 'e':
            explicit = 1;
            break;
        case 'I':
            ret |= aym_loadpath_add(&loadpath, &loadpathlen, optarg);
            break;
        case 'O':
            outdir = optarg;
            break;
        case 'q':
            quiet = 1;
            break;
        case 's':
            show = 1;
            break;
        case 't':
            flags |= AUG_TYPE_CHECK;
            break;
        case 'v':
            ret |= aym_get_vercode(optarg, &vercode);
            break;
        case 'y':
            yanglint = 1;
            break;
        case 'h':
            aym_usage();
            goto cleanup;
        default:
            aym_usage();
            goto cleanup;
        }
    }

    if (ret) {
        goto cleanup;
    }

    if (optind >= argc) {
        fprintf(stderr, "ERROR: expected .aug file\n");
        aym_usage();
        goto cleanup;
    }

    if (!explicit) {
        /* add default lense directory */
        ret |= aym_loadpath_add(&loadpath, &loadpathlen, AUGEAS_LENSES_DIR);
        if (ret) {
            goto cleanup;
        }
    }

    if (show && outdir) {
        fprintf(stderr, "\nERROR: options \'-O\' and \'-s\' should not be entered at the same time.\n\n");
        aym_usage();
        goto cleanup;
    } else if (show && quiet) {
        fprintf(stderr, "\nERROR: options \'-q\' and \'-s\' should not be entered at the same time.\n\n");
        aym_usage();
        goto cleanup;
    } else if (outdir && quiet) {
        fprintf(stderr, "\nERROR: options \'-O\' and \'-q\' should not be entered at the same time.\n\n");
        aym_usage();
        goto cleanup;
    }

    if (outdir && !aym_dir_exists(outdir)) {
        fprintf(stderr, "ERROR: cannot open output directory %s\n", outdir);
        ret = 1;
        goto cleanup;
    } else if (!outdir) {
        /* add default output directory - CWD */
        outdir = ".";
    }

    filename = aym_allocate_filename_buffer(argc, argv, optind, outdir, loadpath);
    if (!filename) {
        fprintf(stderr, "ERROR: Allocation of memory failed\n");
        ret = 1;
        goto cleanup;
    }

    /* for every entered augeas module generate yang file */
    for (int i = 0; i < argc - optind; i++) {
        modname = argv[optind + i];
        aug_close(aug);
        ly_ctx_destroy(ctx);
        free(str);
        aug = NULL;
        str = NULL;
        ctx = NULL;

        /* parse and compile augeas module */
        aym_insert_filename(modname, ".aug", 0, filename);
        // printf("%s\n", filename);

        dirpath = aym_find_aug_module(loadpath, filename);
        if (!dirpath) {
            fprintf(stderr, "ERROR: file %s not found in any directory\n", filename);
            ret = 1;
            continue;
        }
        aym_insert_dirpath(dirpath, filename);

        /* initialize augeas */
        aug = aug_init(NULL, loadpath, flags);
        if (aug == NULL) {
            fprintf(stderr, "ERROR: aug_init memory exhausted\n");
            ret = 1;
            goto cleanup;
        }

        if (__aug_load_module_file(aug, filename) == -1) {
            fprintf(stderr, "ERROR: %s\n", aug_error_message(aug));
            const char *s = aug_error_details(aug);

            if (s != NULL) {
                fprintf(stderr, "ERROR: %s\n", s);
            }
            ret = 1;
            continue;
        }

        assert(aug->modules);
        /* get last compiled (current) module */
        for (mod_iter = aug->modules; mod_iter; mod_iter = mod_iter->next) {
            mod = mod_iter;
        }

        /* generate yang module as string */
        rv = augyang_print_yang(mod, vercode, &str);
        if (rv) {
            fprintf(stderr, "%s", augyang_get_error_message(ret));
            ret = 1;
            continue;
        }

        if (show) {
            /* write result to stdout */
            printf("%s", str);
        } else if (!quiet) {
            /* write result to the yang file */
            aym_insert_filename(modname, ".yang", 1, filename);
            aym_insert_dirpath(outdir, filename);
            file = fopen(filename, "w");
            if (!file) {
                fprintf(stderr, "ERROR: failed to open %s\n", filename);
                continue;
            }
            fprintf(file, "%s", str);
            fclose(file);
        }

        if (yanglint) {
            err = ly_ctx_new(NULL, 0, &ctx);
            if (err != LY_SUCCESS) {
                fprintf(stderr, "ERROR: Failed to create libyang context\n");
                ret = 1;
                ctx = NULL;
                goto cleanup;
            }

            err = lys_parse_mem(ctx, (const char *)augeas_extension_yang, LYS_IN_YANG, NULL);
            if (err != LY_SUCCESS) {
                fprintf(stderr, "ERROR: Failed to parse augeas_extension_yang.\n");
                ret = 1;
            }

            if (lys_parse_mem(ctx, str, LYS_IN_YANG, NULL)) {
                ret = 1;
            }
        }
    }

cleanup:
    free(str);
    aug_close(aug);
    free(loadpath);
    free(filename);
    ly_ctx_destroy(ctx);

    return ret;
}
