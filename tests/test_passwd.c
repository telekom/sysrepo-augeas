/**
 * @file test_passwd.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief passwd SR DS plugin test
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
 */

#include "tconfig.h"

/* augeas SR DS plugin */
#define AUG_TEST_INPUT_FILE AUG_CONFIG_FILES_DIR "/passwd"
#include "srds_augeas.c"

#include <assert.h>
#include <dlfcn.h>
#include <setjmp.h>
#include <stdarg.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cmocka.h>
#include <libyang/libyang.h>
#include <sysrepo/plugins_datastore.h>

struct state {
    struct ly_ctx *ctx;
    const struct lys_module *mod;
    struct lyd_node *data;
    const struct srplg_ds_s *ds_plg;
    char *str;
};

static int
setup_f(void **state)
{
    struct state *st;

    st = calloc(1, sizeof *st);
    *state = st;

    sr_log_stderr(SR_LL_WRN);
    //ly_log_level(LY_LLVRB);

    /* context */
    assert_int_equal(LY_SUCCESS, ly_ctx_new(AUG_EXPECTED_YANG_DIR, 0, &st->ctx));
    ly_ctx_set_searchdir(st->ctx, AUG_MODULES_DIR);

    /* load module */
    st->mod = ly_ctx_load_module(st->ctx, "passwd", NULL, NULL);
    assert_non_null(st->mod);

    /* plugin, init */
    st->ds_plg = &srpds__;
    assert_int_equal(SR_ERR_OK, st->ds_plg->init_cb(st->mod, SR_DS_STARTUP, "root", "root", 00644));

    return 0;
}

static int
teardown_f(void **state)
{
    struct state *st = (struct state *)*state;
    int ret = 0;

    /* destroy */
    ret = st->ds_plg->destroy_cb(st->mod, SR_DS_STARTUP);

    /* free */
    lyd_free_siblings(st->data);
    ly_ctx_destroy(st->ctx);
    free(st->str);
    free(st);

    return ret;
}

static int
teardown(void **state)
{
    struct state *st = (struct state *)*state;

    lyd_free_siblings(st->data);
    st->data = NULL;
    free(st->str);
    st->str = NULL;

    return 0;
}

static int
diff_files(const char *file1, const char *file2, char **output)
{
    int wstatus, p[2], r, out_len = 0;
    char buf[1024];

    *output = strdup("");
    assert_non_null(*output);

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
        snprintf((*output) + out_len, r, buf);
        out_len += r;
    }
    (*output)[out_len] = '\0';
    assert_int_equal(r, 0);

    /* wait for child */
    assert_int_not_equal(-1, wait(&wstatus));
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

static void
test_load(void **state)
{
    struct state *st = (struct state *)*state;
    char *str;

    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));
    lyd_print_mem(&str, st->data, LYD_XML, LYD_PRINT_WITHSIBLINGS);

    assert_string_equal(str,
            "<passwd xmlns=\"aug:passwd\">\n"
            "  <entry>\n"
            "    <username>avahi</username>\n"
            "    <password>x</password>\n"
            "    <uid>466</uid>\n"
            "    <gid>468</gid>\n"
            "    <name>User for Avahi</name>\n"
            "    <home>/run/avahi-daemon</home>\n"
            "    <shell>/bin/false</shell>\n"
            "  </entry>\n"
            "  <entry>\n"
            "    <username>bin</username>\n"
            "    <password>x</password>\n"
            "    <uid>1</uid>\n"
            "    <gid>1</gid>\n"
            "    <name>bin</name>\n"
            "    <home>/bin</home>\n"
            "    <shell>/sbin/nologin</shell>\n"
            "  </entry>\n"
            "  <entry>\n"
            "    <username>chrony</username>\n"
            "    <password>x</password>\n"
            "    <uid>473</uid>\n"
            "    <gid>475</gid>\n"
            "    <name>Chrony Daemon</name>\n"
            "    <home>/var/lib/chrony</home>\n"
            "    <shell>/bin/false</shell>\n"
            "  </entry>\n"
            "  <entry>\n"
            "    <username>man</username>\n"
            "    <password>x</password>\n"
            "    <uid>13</uid>\n"
            "    <gid>62</gid>\n"
            "    <name>Manual pages viewer</name>\n"
            "    <home>/var/lib/empty</home>\n"
            "    <shell>/sbin/nologin</shell>\n"
            "  </entry>\n"
            "  <entry>\n"
            "    <username>nm-openconnect</username>\n"
            "    <password>x</password>\n"
            "    <uid>464</uid>\n"
            "    <gid>465</gid>\n"
            "    <name>NetworkManager user for OpenConnect</name>\n"
            "    <home>/var/lib/nm-openconnect</home>\n"
            "    <shell>/sbin/nologin</shell>\n"
            "  </entry>\n"
            "  <entry>\n"
            "    <username>nm-openvpn</username>\n"
            "    <password>x</password>\n"
            "    <uid>465</uid>\n"
            "    <gid>466</gid>\n"
            "    <name>NetworkManager user for OpenVPN</name>\n"
            "    <home>/var/lib/openvpn</home>\n"
            "    <shell>/sbin/nologin</shell>\n"
            "  </entry>\n"
            "  <entry>\n"
            "    <username>nobody</username>\n"
            "    <password>x</password>\n"
            "    <uid>65534</uid>\n"
            "    <gid>65534</gid>\n"
            "    <name>nobody</name>\n"
            "    <home>/var/lib/nobody</home>\n"
            "    <shell>/bin/bash</shell>\n"
            "  </entry>\n"
            "  <nisentry>\n"
            "    <username>some-nis-group</username>\n"
            "  </nisentry>\n"
            "  <nisentry>\n"
            "    <username>bob</username>\n"
            "    <home>/home/bob</home>\n"
            "    <shell>/bin/bash</shell>\n"
            "  </nisentry>\n"
            "  <nisdefault>\n"
            "    <_id>1</_id>\n"
            "  </nisdefault>\n"
            "  <nisdefault>\n"
            "    <_id>2</_id>\n"
            "    <shell>/sbin/nologin</shell>\n"
            "  </nisdefault>\n"
            "  <nisuserplus>\n"
            "    <username>cecil</username>\n"
            "    <name>User Comment</name>\n"
            "    <home>/home/bob</home>\n"
            "    <shell>/bin/bash</shell>\n"
            "  </nisuserplus>\n"
            "  <nisuserminus>\n"
            "    <username>alice</username>\n"
            "  </nisuserminus>\n"
            "</passwd>\n");
    free(str);
}

static void
test_store_add(void **state)
{
    struct state *st = (struct state *)*state;
    struct lyd_node *user;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* add a user */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry[username='admin']/password", "x", 0, &user));
    assert_int_equal(LY_SUCCESS, lyd_new_path(user, NULL, "uid", "2000", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(user, NULL, "gid", "200", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(user, NULL, "name", "The Admin", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(user, NULL, "home", "/home/admin", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(user, NULL, "shell", "/bin/bash", 0, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* get diff */
    assert_int_equal(0, diff_files(AUG_TEST_INPUT_FILE, AUG_TEST_INPUT_FILE ".augnew", &st->str));
    assert_string_equal(st->str,
            "13a14\n"
            "> admin:x:2000:200:The Admin:/home/admin:/bin/bash");
}

int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_teardown(test_load, teardown),
        cmocka_unit_test_teardown(test_store_add, teardown),
    };

    return cmocka_run_group_tests(tests, setup_f, teardown_f);
}
