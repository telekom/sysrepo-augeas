/**
 * @file main.c
 * @author Adam Piecek <piecek@cesnet.cz>
 * @brief Main code for augyang executable file.
 *
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

#include <argz.h>
#include <dirent.h>
#include <getopt.h>

#include <locale.h>
#include "augeas.h"
#include "augyang.h"
#include "errcode.h"
#include "list.h"
#include "syntax.h"

/**
 * @brief Buffer for filename including the path.
 */
#define AYM_MAX_FILENAME_LEN 256

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
__attribute__((noreturn))
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
            "  -I, --include DIR  search DIR for augeas modules; can be given multiple times\n"
            "                     default value: %s\n", AUGEAS_LENSES_DIR);
    fprintf(stderr,
            "  -O, --outdir DIR   directory in which the generated yang file is written;\n"
            "                     default value: ./\n");
    fprintf(stderr,
            "  -s, --show         print the generated yang only to stdout and not to the file\n");
    fprintf(stderr,
            "  -v, --verbose HEX  bitmask for various debug outputs\n");
    fprintf(stderr, "\nExample:\n"
            AYM_PROGNAME " passwd backuppchosts\n"
            AYM_PROGNAME " -e -I ./mylenses -O ./genyang someAugfile\n");

    exit(EXIT_FAILURE);
}

/**
 * @brief Convert verbose code in string format to unsigned integer.
 *
 * @param[in] optarg String from command line.
 * @return Verbose code aka vercode.
 */
static uint64_t
aym_get_vercode(char *optarg)
{
    uint64_t ret;
    char *endptr;

    if (optarg[0] == '-') {
        fprintf(stderr, "ERROR: Verbose code cannot be negative number\n");
        aym_usage();
    }

    errno = 0;
    ret = strtoull(optarg, &endptr, 16);
    if (errno || (&optarg[strlen(optarg)] != endptr)) {
        fprintf(stderr, "ERROR: Verbose code conversion error\n");
        aym_usage();
    }

    return ret;
}

/**
 * @brief Wrapper of ::argz_create_sep().
 *
 * Convenient call this function after argz_stringify().
 *
 * @param[in] argz The argz vector.
 * @param[in] argz_len Length of the @p argz.
 */
static void
aym_argz_create_sep_(char **argz, size_t *argz_len)
{
    char *str;

    assert(*argz);
    str = strdup(*argz);
    free(*argz);
    argz_create_sep(str, PATH_SEP_CHAR, argz, argz_len);
    free(str);
}

/**
 * @brief Find the size of the longest string in the argz vector.
 *
 * @param[in] argz The argz vector.
 * @param[in] argz_len Length of the @p argz.
 * @return The number of characters in the longest string.
 */
static size_t
aym_argz_max_string_len(const char *argz, size_t argz_len)
{
    size_t ret = 0;
    const char *iter = NULL;

    while ((iter = argz_next(argz, argz_len, iter))) {
        ret = strlen(iter) > ret ? strlen(iter) : ret;
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
    filename[strlen(dirpath) + AYM_SLASH_LEN + strlen(filename)] = '\0';
    memmove(filename + strlen(dirpath) + AYM_SLASH_LEN, filename, strlen(filename));
    memcpy(filename, dirpath, strlen(dirpath));
    filename[strlen(dirpath)] = '/';
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
    size_t new_end;

    assert(!strncmp(filename, dirpath, strlen(dirpath)));
    new_end = strlen(filename) - (strlen(dirpath) + AYM_SLASH_LEN);
    memmove(filename, filename + strlen(dirpath) + AYM_SLASH_LEN, strlen(filename));
    filename[new_end] = '\0';
}

/**
 * @brief Find out in which directory path the module @p filename is located.
 *
 * @param[in] argz The argz vector.
 * @param[in] argz_len Length of the @p argz.
 * @param[in] filename Buffer containing file name. It is not modified.
 * @return Directory path from @p argz.
 */
static const char *
aym_find_aug_module(const char *argz, size_t argz_len, char *filename)
{
    bool succ = 0;
    const char *iter = NULL, *next = NULL;

    while (!succ && (iter = argz_next(argz, argz_len, next))) {
        aym_insert_dirpath(iter, filename);
        succ = aym_file_exists(filename);
        aym_remove_dirpath(iter, filename);
        next = iter;
    }

    return iter;
}

/**
 * @brief Create filename by name and suffix.
 *
 * @param[in] name Name of the file.
 * @param[in] suffix Name of the suffix to add.
 * @param[in,out] filename Sufficiently large buffer which will be overwritten.
 * The output form will be "<name><suffix>".
 */
static void
aym_insert_filename(const char *name, const char *suffix, char *filename)
{
    strcpy(filename, name);
    strcpy(&filename[strlen(filename)], suffix);
}

int
main(int argc, char **argv)
{
    int opt, ret = 0, ret2 = 0, vercode = 0, explicit = 0, show = 0;
    struct augeas *aug = NULL;
    char *loadpath = NULL, *str = NULL, *modname, *outdir = NULL;
    const char *dirpath;
    size_t loadpathlen = 0, maxpathlen = 0;
    struct module *mod;
    char filename[AYM_MAX_FILENAME_LEN];
    FILE *file = NULL;

    enum {
        VAL_NO_STDINC = CHAR_MAX + 1,
        VAL_NO_TYPECHECK = VAL_NO_STDINC + 1,
        VAL_VERSION = VAL_NO_TYPECHECK + 1
    };
    struct option options[] = {
        {"help",      0, 0, 'h'},
        {"explicit",  0, 0, 'e'},
        {"include",   1, 0, 'I'},
        {"outdir",    1, 0, 'O'},
        {"show",      0, 0, 's'},
        {"verbose",   1, 0, 'v'},
        {0, 0, 0, 0}
    };
    int idx;
    unsigned int flags = AUG_TYPE_CHECK | AUG_NO_MODL_AUTOLOAD;

    setlocale(LC_ALL, "");
    while ((opt = getopt_long(argc, argv, "heI:O:sv:", options, &idx)) != -1) {
        switch (opt) {
        case 'e':
            explicit = 1;
            break;
        case 'I':
            argz_add(&loadpath, &loadpathlen, optarg);
            break;
        case 'O':
            outdir = optarg;
            break;
        case 's':
            show = 1;
            break;
        case 'v':
            vercode = aym_get_vercode(optarg);
            break;
        case 'h':
            aym_usage();
            break;
        default:
            aym_usage();
            break;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "ERROR: expected .aug file\n");
        aym_usage();
    }

    if (!explicit) {
        /* add default lense directory */
        argz_add(&loadpath, &loadpathlen, AUGEAS_LENSES_DIR);
    }

    if (show && outdir) {
        fprintf(stderr, "ERROR: options \'-O\' and \'-s\' should not be entered at the same time.");
        aym_usage();
    }

    if (outdir && !aym_dir_exists(outdir)) {
        fprintf(stderr, "ERROR: cannot open output directory %s\n", outdir);
        ret = 1;
        goto end;
    } else if (!outdir) {
        /* add default output directory - CWD */
        outdir = ".";
    }

    /* aug_init() */
    argz_stringify(loadpath, loadpathlen, PATH_SEP_CHAR);
    aug = aug_init(NULL, loadpath, flags);
    if (aug == NULL) {
        fprintf(stderr, "ERROR: memory exhausted\n");
        ret = 2;
        goto end;
    }
    aym_argz_create_sep_(&loadpath, &loadpathlen);
    maxpathlen = aym_argz_max_string_len(loadpath, loadpathlen);

    /* for every entered augeas module generate yang file */
    for (int i = 0; i < argc - optind; i++) {
        modname = argv[optind + i];
        /* check buffer length */
        if ((strlen(outdir) + AYM_SLASH_LEN + strlen(modname) + AYM_SUFF_YANG_LEN >= AYM_MAX_FILENAME_LEN) ||
                (maxpathlen + AYM_SLASH_LEN + strlen(modname) + AYM_SUFF_AUG_LEN >= AYM_MAX_FILENAME_LEN)) {
            fprintf(stderr, "ERROR: constant AYM_MAX_FILENAME_LEN exceeded\n");
            ret = 1;
            goto end;
        }

        /* parse and compile augeas module */
        aym_insert_filename(modname, ".aug", filename);
        dirpath = aym_find_aug_module(loadpath, loadpathlen, filename);
        if (!dirpath) {
            fprintf(stderr, "ERROR: file %s not found in any directory\n", filename);
            ret2 = 1;
            continue;
        }
        aym_insert_dirpath(dirpath, filename);
        if (__aug_load_module_file(aug, filename) == -1) {
            fprintf(stderr, "ERROR: %s\n", aug_error_message(aug));
            const char *s = aug_error_details(aug);
            if (s != NULL) {
                fprintf(stderr, "ERROR: %s\n", s);
            }
            ret = 1;
            goto end;
        }

        /* get last compiled (current) module */
        list_for_each(mod_iter, aug->modules) {
            mod = mod_iter;
        }

        /* generate yang module as string */
        ret = augyang_print_yang(mod, vercode, &str);
        if (ret) {
            fprintf(stderr, "%s", augyang_get_error_message(ret));
            goto end;
        }

        if (show) {
            /* write result to stdout */
            printf("%s\n", str);
        } else {
            /* write result to the yang file */
            aym_insert_filename(modname, ".yang", filename);
            aym_insert_dirpath(outdir, filename);
            file = fopen(filename, "w");
            if (!file) {
                fprintf(stderr, "ERROR: failed to open %s\n", filename);
                goto end;
            }
            fprintf(file, "%s", str);
            fclose(file);
        }
        free(str);
        str = NULL;
    }

end:
    free(str);
    aug_close(aug);
    free(loadpath);

    return ret | ret2;
}
