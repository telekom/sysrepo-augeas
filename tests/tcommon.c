/**
 * @file tcommon.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief common routines for tests
 *
 * @copyright
 * Copyright (c) 2022 Deutsche Telekom AG.
 * Copyright (c) 2022 CESNET, z.s.p.o.
 *
 * This source code is licensed under BSD 3-Clause License (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://opensource.org/licenses/BSD-3-Clause
 */

#define _GNU_SOURCE

#include "tconfig.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include <libyang/libyang.h>
#include <sysrepo.h>
#include <sysrepo/plugins_datastore.h>

int
tsetup_glob(void **state, const char *yang_mod, const struct srplg_ds_s *ds_plg, const char *aug_input_files)
{
    struct tstate *st;

    st = calloc(1, sizeof *st);
    *state = st;

    sr_log_stderr(SR_LL_WRN);
    //ly_log_level(LY_LLVRB);

    /* context */
    if (ly_ctx_new(AUG_EXPECTED_YANG_DIR, 0, &st->ctx)) {
        return 1;
    }
    ly_ctx_set_searchdir(st->ctx, AUG_MODULES_DIR);

    /* load module */
    st->mod = ly_ctx_load_module(st->ctx, yang_mod, NULL, NULL);
    if (!st->mod) {
        return 1;
    }

    /* plugin */
    st->ds_plg = ds_plg;

    /* aug intpu files */
    st->aug_input_files = aug_input_files;

    return 0;
}

int
tteardown_glob(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    int ret = 0;

    /* destroy DS */
    st->ds_plg->destroy_cb(st->mod, SR_DS_STARTUP);

    /* free */
    lyd_free_siblings(st->data);
    ly_ctx_destroy(st->ctx);
    free(st);

    return ret;
}

int
tteardown(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    char *files, *file, *newfile;

    lyd_free_siblings(st->data);
    st->data = NULL;;

    /* remove all created files */
    files = strdup(st->aug_input_files);
    for (file = strtok(files, ";"); file; file = strtok(NULL, ";")) {
        asprintf(&newfile, "%s.augnew", file);
        unlink(newfile);
        free(newfile);
    }
    free(files);

    return 0;
}

static int
diff_file(const char *file1, const char *file2, char **output)
{
    int wstatus, p[2], r, out_len = 0;
    char buf[1024];

    *output = strdup("");
    assert(*output);

    if (access(file2, F_OK) == -1) {
        /* file does not even exist, empty diff */
        return 0;
    }

    /* create pipe */
    pipe(p);

    if (!fork()) {
        /* child */

        /* output redirect */
        dup2(p[1], STDOUT_FILENO);
        close(p[0]);
        close(p[1]);

        /* exec */
        execl(AUG_DIFF_EXECUTABLE, AUG_DIFF_EXECUTABLE, file1, file2, (char *)NULL);
        exit(2);
    }
    close(p[1]);

    /* get all output */
    while ((r = read(p[0], buf, 1023)) > 0) {
        buf[r] = '\0';

        *output = realloc(*output, out_len + r + 1);
        snprintf((*output) + out_len, r, "%s", buf);
        out_len += r;
    }
    (*output)[out_len] = '\0';
    assert(!r);

    /* wait for child */
    assert(wait(&wstatus) != -1);
    if (!WIFEXITED(wstatus)) {
        printf("[ DIFF ERR ] diff process terminated abnormally.\n");
        return 1;
    }
    if (WEXITSTATUS(wstatus) != 1) {
        printf("[ DIFF ERR ] diff returned unexpected value %d.\n", WEXITSTATUS(wstatus));
        return 1;
    }

    return 0;
}

int
tdiff_files(void **state, ...)
{
    struct tstate *st = (struct tstate *)*state;
    char *files, *file, *newfile = NULL, *str = NULL;
    const char *expected;
    va_list ap;
    int ret = 0;

    va_start(ap, state);
    files = strdup(st->aug_input_files);

    for (file = strtok(files, ";"); file; file = strtok(NULL, ";")) {
        /* get next arg and newfile */
        expected = va_arg(ap, const char *);
        if (asprintf(&newfile, "%s.augnew", file) == -1) {
            ret = -1;
            goto cleanup;
        }

        /* get diff */
        if (diff_file(file, newfile, &str)) {
            ret = -1;
            goto cleanup;
        }

        /* compare */
        if (strcmp(str, expected)) {
            printf("[ DIFF FAIL ] PRINTED:\n%s\nEXPECTED:\n%s\n", str, expected);
            ret = 1;
            goto cleanup;
        }

        /* next iter */
        free(newfile);
        newfile = NULL;
        free(str);
        str = NULL;
    }

cleanup:
    va_end(ap);
    free(newfile);
    free(str);
    free(files);
    return ret;
}
