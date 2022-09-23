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
 * @brief Modules for which YANG will not be generated.
 */
const char * const ignored_modules[] = {
    "build.aug",
    "erlang.aug",
    "quote.aug",
    "rx.aug",
    "sep.aug",
    "util.aug",
};

/**
 * @brief Print help to stderr.
 */
static void
aym_usage(void)
{
    fprintf(stderr, "Usage: "AYM_PROGNAME " [OPTIONS] MODULE...\n");
    fprintf(stderr, "       "AYM_PROGNAME " -a [OPTIONS]\n");
    fprintf(stderr, "Generate YANG module (.yang) from Augeas MODULE (.aug).\n");
    fprintf(stderr, "Information about the YANG format is in the RFC 7950.\n");
    fprintf(stderr, "\nOptions:\n\n");
    fprintf(stderr,
            "  -a, --all          process all augeas modules in Search DIR;\n"
            "                     if the root lense is not found, then the module is ignored;\n"
            "                     (for example rx.aug, build.aug, ...)\n");
    fprintf(stderr,
            "  -e, --explicit     default value of the -I parameter is not used;\n"
            "                     only the directories specified by the -I parameter are used\n");
    fprintf(stderr,
            "  -I, --include DIR  Search DIR for augeas modules; can be given multiple times;\n"
            "                     default value: %s\n", AUGEAS_LENSES_DIR);
    fprintf(stderr,
            "  -n, --name         print the name of the currently processed module\n");
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
            AYM_PROGNAME " -e -I ./mylenses -O ./genyang someAugfile\n"
            AYM_PROGNAME " -a -I ./mylenses\n");
}

/**
 * @brief Check if @p filename should be ignored.
 *
 * @param[in] filename Name of augeas module with suffix .aug.
 * @return 1 if module should be ignored.
 */
static ly_bool
aym_ignore_module(char *filename)
{
    uint64_t i;
    size_t stop;

    stop = sizeof(ignored_modules) / sizeof(ignored_modules[0]);
    for (i = 0; i < stop; i++) {
        if (!strcmp(filename, ignored_modules[i])) {
            return 1;
        }
    }

    return 0;
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
 * @brief Iterator over command lines arguments.
 */
struct aym_iter_argv{
    char **argv;        /**< The argv from main function. */
    int index;          /**< Current index. */
    int argc;           /**< The argc from main function. */
};

/**
 * @brief Initialize iterator over command lines arguments.
 *
 * @param[in] argv The argv from main function.
 * @param[in] argc The argc from main function.
 * @param[in] optind The optind variable from getopt function.
 * @param[out] it Iterator to initialize.
 */
static void
aym_module_iter_argv_init(char **argv, int argc, int optind, struct aym_iter_argv *it)
{
    assert(argv && it);

    it->argv = argv;
    it->index = optind - 1;
    it->argc = argc;
}

/**
 * @brief Reset argv iterator.
 *
 * @param[in] it Iterator to reset.
 */
static void
aym_module_iter_argv_reset(struct aym_iter_argv *it)
{
    assert(it);

    it->index = optind - 1;
}

/**
 * @brief Close argv iterator.
 *
 * @param[in] it Iterator to close.
 */
static void
aym_module_iter_argv_close(struct aym_iter_argv *it)
{
    (void)it;
}

/**
 * @brief Move the iterator to the next element.
 *
 * @param[in] it Iterator to move.
 * @return Filename of augeas module (without suffix .aug) or NULL.
 */
static char *
aym_module_iter_argv(struct aym_iter_argv *it)
{
    assert(it);

    it->index++;
    if (it->index < it->argc) {
        return it->argv[it->index];
    } else {
        return NULL;
    }
}

/**
 * @brief Iterate over all files in multiple directories.
 */
struct aym_iter_dir{
    DIR *dir;                   /**< Pointer to the directory stream. */
    struct dirent *file;        /**< Directory entry in directory stream. */
    char *loadpath;             /**< Storage of paths to directories. */
    char *loadpath_iter;        /**< Current path to directory. */
};

/**
 * @brief Initialize iterator over all files in multiple directories.
 *
 * @param[in] loadpath Storage of paths to directories.
 * @param[out] it Iterator to initialize.
 */
static void
aym_module_iter_dir_init(char *loadpath, struct aym_iter_dir *it)
{
    assert(loadpath && it);

    it->dir = NULL;
    it->file = NULL;
    it->loadpath = loadpath;
    it->loadpath_iter = NULL;
}

/**
 * @brief Reset dir iterator.
 *
 * @param[in] it Iterator to reset.
 */
static void
aym_module_iter_dir_reset(struct aym_iter_dir *it)
{
    assert(it);

    it->dir = NULL;
    it->file = NULL;
    it->loadpath_iter = NULL;
}

/**
 * @brief Move the iterator to the next element.
 *
 * @param[in] it Iterator to move.
 * @param[out] err Set to 1 if an error occurs.
 * @return Filename of augeas module (with suffix .aug) or NULL.
 */
static char *
aym_module_iter_dir(struct aym_iter_dir *it, int *err)
{
    char *name, *path;
    size_t len;

    assert(it && err);

    /* Iterate over directories. */
    for (it->loadpath_iter = !it->dir ? it->loadpath : it->loadpath_iter;
            it->loadpath_iter;
            it->loadpath_iter = aym_loadpath_next(it->loadpath_iter)) {

        /* Open a new directory if not set. */
        if (!it->dir) {
            /* Temporarily allocate memory space for the directory path. */
            len = aym_loadpath_pathlen(it->loadpath_iter);
            path = strndup(it->loadpath_iter, len);
            if (!path) {
                fprintf(stderr, "ERROR: Allocation of memory failed\n");
                *err = 1;
                return NULL;
            }
            /* Open directory stream. */
            it->dir = opendir(path);
            /* Release temporary variable. */
            free(path);
            if (!it->dir) {
                assert(it->loadpath_iter);
                fprintf(stderr, "ERROR: cannot open Search DIR %s\n", it->loadpath_iter);
                *err = 1;
                return NULL;
            }
        }

        /* Iterate over files. */
        while ((it->file = readdir(it->dir))) {
            name = it->file->d_name;
            if ((strlen(name) > 4) && !strcmp(name + strlen(name) - 4, ".aug")) {
                /* Return filename of augeas module. */
                return name;
            }
        }
        closedir(it->dir);
        it->dir = NULL;
    }

    return NULL;
}

/**
 * @brief Close dir iterator.
 *
 * @param[in] it Iterator to close.
 */
static void
aym_module_iter_dir_close(struct aym_iter_dir *it)
{
    if (it->dir) {
        closedir(it->dir);
        it->dir = NULL;
    }
}

/**
 * @brief Type if iterator over augeas modules files.
 */
enum aym_iter_type {
    AYI_ARGV,       /**< Iterator over command lines arguments. */
    AYI_DIR         /**< Iterate over all files in multiple directories. */
};

/**
 * @brief Iterator over augeas modules files.
 */
struct aym_iter {
    enum aym_iter_type type;                    /**< Type of iterator. */

    union {
        struct aym_iter_argv iter_argv;         /**< Iterator of type AYI_ARGV. */
        struct aym_iter_dir iter_dir;           /**< Iterator of type AYI_DIR. */
    };
    int err;                                    /**< Error value. */
};

/**
 * @brief Reset the iterator to point to the first element.
 *
 * @param[in] it Iterator to reset.
 */
static void
aym_module_iter_reset(struct aym_iter *it)
{
    assert(it);

    switch (it->type) {
    case AYI_ARGV:
        aym_module_iter_argv_reset(&it->iter_argv);
        break;
    case AYI_DIR:
        aym_module_iter_dir_reset(&it->iter_dir);
        break;
    }
}

/**
 * @brief Move the iterator to the next element.
 *
 * @param[in] it Iterator to move.
 * @return Filename of augeas module (with or without suffix .aug) or NULL.
 */
static char *
aym_module_iter(struct aym_iter *it)
{
    assert(it);

    switch (it->type) {
    case AYI_ARGV:
        return aym_module_iter_argv(&it->iter_argv);
    case AYI_DIR:
        return aym_module_iter_dir(&it->iter_dir, &it->err);
    }

    return NULL;
}

/**
 * @brief Close iterator.
 *
 * @param[in] it Iterator to close.
 */
static void
aym_module_iter_close(struct aym_iter *it)
{
    assert(it);

    switch (it->type) {
    case AYI_ARGV:
        aym_module_iter_argv_close(&it->iter_argv);
        break;
    case AYI_DIR:
        aym_module_iter_dir_close(&it->iter_dir);
        break;
    }
    it->err = 0;
}

/**
 * @brief A 'for' loop through augeas modules.
 *
 * @param[in,out] ITER Pointer to iterator struct aym_iter.
 * @param[out] NAME Name of augeas module (with or without suffix .aug).
 */
#define AYM_MODULE_ITER_FOR(ITER, NAME) \
    aym_module_iter_reset(ITER); \
    for (NAME = aym_module_iter(ITER); NAME && !ITER->err; NAME = aym_module_iter(ITER))

/**
 * @brief Allocate filename buffer.
 *
 * The function guarantees that it allocates enough space for operations with filename buffer in this source file.
 *
 * @param[in] moditer Iterator over augeas modules.
 * @param[in] outdir Option --outdir DIR from command line.
 * @param[in] loadpath Storage of paths.
 * @return Pointer to new allocated buffer or NULL.
 */
static char *
aym_allocate_filename_buffer(struct aym_iter *moditer, char *outdir, char *loadpath)
{
    char *buffer;
    size_t maxpathlen, maxmodname = 0, maxyang, maxaug, buffer_size;
    char *modname;

    maxpathlen = aym_loadpath_maxpath(loadpath);
    AYM_MODULE_ITER_FOR(moditer, modname) {
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
    int opt, ret = 0, rv, explicit = 0, show = 0, quiet = 0, yanglint = 0, all = 0, print_name = 0;
    struct augeas *aug = NULL;
    char *loadpath = NULL, *str = NULL, *modname, *outdir = NULL;
    const char *dirpath;
    size_t loadpathlen = 0;
    struct module *mod, *mod_iter;
    char *filename = NULL;
    FILE *file = NULL;
    uint64_t vercode = 0;
    struct ly_ctx *ctx = NULL;
    struct aym_iter module_name_iter = {0};
    struct aym_iter *modname_iter = &module_name_iter;
    LY_ERR err;

    struct option options[] = {
        {"help",      0, 0, 'h'},
        {"all",       0, 0, 'a'},
        {"explicit",  0, 0, 'e'},
        {"include",   1, 0, 'I'},
        {"name",      0, 0, 'n'},
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

    while ((opt = getopt_long(argc, argv, "haeI:nO:qstv:y", options, &idx)) != -1) {
        switch (opt) {
        case 'a':
            all = 1;
            break;
        case 'e':
            explicit = 1;
            break;
        case 'I':
            ret |= aym_loadpath_add(&loadpath, &loadpathlen, optarg);
            break;
        case 'n':
            print_name = 1;
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

    if ((optind >= argc) && !all) {
        fprintf(stderr, "ERROR: expected .aug file\n");
        aym_usage();
        goto cleanup;
    } else if ((optind < argc) && all) {
        fprintf(stderr, "ERROR: specifying MODULE and option '-a' is not allowed\n");
        aym_usage();
        goto cleanup;
    } else if (show && outdir) {
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

    if (!explicit) {
        /* add default lense directory */
        ret = aym_loadpath_add(&loadpath, &loadpathlen, AUGEAS_LENSES_DIR);
        if (ret) {
            goto cleanup;
        }
    }

    if (outdir && !aym_dir_exists(outdir)) {
        fprintf(stderr, "ERROR: cannot open output directory %s\n", outdir);
        ret = 1;
        goto cleanup;
    } else if (!outdir) {
        /* add default output directory - CWD */
        outdir = ".";
    }

    if (all) {
        /* Iterator over all augeas modules in directories specified in 'loadpath'. */
        modname_iter->type = AYI_DIR;
        aym_module_iter_dir_init(loadpath, &modname_iter->iter_dir);
    } else {
        /* Iterator over augeas modules which must be found somewhere in 'loadpath'. */
        modname_iter->type = AYI_ARGV;
        aym_module_iter_argv_init(argv, argc, optind, &modname_iter->iter_argv);
    }

    /* Allocate a buffer large enough to store the path and name of the augeas module. */
    filename = aym_allocate_filename_buffer(modname_iter, outdir, loadpath);
    if (!filename) {
        fprintf(stderr, "ERROR: Allocation of memory failed\n");
        ret = 1;
        goto cleanup;
    }

    /* For every augeas module generate yang file. */
    AYM_MODULE_ITER_FOR(modname_iter, modname) {
        aug_close(aug);
        ly_ctx_destroy(ctx);
        free(str);
        aug = NULL;
        str = NULL;
        ctx = NULL;

        if (modname_iter->type == AYI_ARGV) {
            /* Find entered augeas module. */
            aym_insert_filename(modname, ".aug", 0, filename);
            dirpath = aym_find_aug_module(loadpath, filename);
            if (!dirpath) {
                fprintf(stderr, "ERROR: file %s not found in any directory\n", filename);
                ret = 1;
                continue;
            }
        } else if ((modname_iter->type == AYI_DIR) && aym_ignore_module(modname)) {
            /* Augeas module is ignored. */
            assert(all);
            continue;
        } else {
            /* Some module from directory. */
            assert(modname_iter->type == AYI_DIR);
            strcpy(filename, modname);
            dirpath = modname_iter->iter_dir.loadpath_iter;
        }
        if (print_name) {
            /* Printing the current name is useful when the program terminates unexpectedly. */
            fprintf(stdout, "%s\n", modname);
        }
        /* Concatenate directory path with filename. */
        aym_insert_dirpath(dirpath, filename);

        /* Initialize augeas context. */
        aug = aug_init(NULL, loadpath, flags);
        if (aug == NULL) {
            fprintf(stderr, "ERROR: aug_init memory exhausted\n");
            ret = 1;
            goto cleanup;
        }

        /* Parse and compile augeas module. */
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
        /* Get last compiled (current) module form augeas context. */
        for (mod_iter = aug->modules; mod_iter; mod_iter = mod_iter->next) {
            mod = mod_iter;
        }

        /* Generate yang module as string. */
        rv = augyang_print_yang(mod, vercode, &str);
        if (all && rv && (rv == AYE_LENSE_NOT_FOUND)) {
            /* Ignore module that can be auxiliary, eg rx.aug, build.aug... */
            continue;
        } else if (rv) {
            /* Error when generating YANG. */
            fprintf(stderr, "%s", augyang_get_error_message(rv));
            ret = 1;
            continue;
        }

        if (show) {
            /* Write YANG to stdout. */
            printf("%s", str);
        } else if (!quiet) {
            /* Write YANG to the yang file. */
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
            /* Validate the YANG module. */
            err = ly_ctx_new(NULL, 0, &ctx);
            if (err != LY_SUCCESS) {
                fprintf(stderr, "ERROR: Failed to create libyang context\n");
                ret = 1;
                ctx = NULL;
                goto cleanup;
            }

            /* Parse augeas extension. */
            err = lys_parse_mem(ctx, (const char *)augeas_extension_yang, LYS_IN_YANG, NULL);
            if (err != LY_SUCCESS) {
                fprintf(stderr, "ERROR: Failed to parse augeas_extension_yang.\n");
                ret = 1;
            }

            /* Parse generated YANG module. */
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
    aym_module_iter_close(modname_iter);

    return ret;
}
