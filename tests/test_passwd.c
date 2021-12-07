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

#include <assert.h>
#include <dlfcn.h>
#include <setjmp.h>
#include <stdarg.h>
#include <unistd.h>

#include "tconfig.h"

#include <cmocka.h>
#include <libyang/libyang.h>
#include <sysrepo/plugins_datastore.h>

/* augeas SR DS plugin */
extern const struct srplg_ds_s srpds__;

struct state {
    struct ly_ctx *ctx;
    const struct lys_module *mod;
    const struct srplg_ds_s *ds_plg;
};

static int
setup_f(void **state)
{
    struct state *st;

    st = calloc(1, sizeof *st);
    *state = st;

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
    ly_ctx_destroy(st->ctx);
    free(st);

    return ret;
}

static void
test_load(void **state)
{
    struct state *st = (struct state *)*state;
    struct lyd_node *data;
    char *str;

    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &data));
    lyd_print_mem(&str, data, LYD_XML, LYD_PRINT_WITHSIBLINGS);
    lyd_free_siblings(data);

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
            "  </entry>\n""  <entry>\n"
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

int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_load, setup_f, teardown_f),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
