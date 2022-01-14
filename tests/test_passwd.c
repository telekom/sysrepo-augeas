/**
 * @file test_passwd.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief passwd SR DS plugin test
 *
 * @copyright
 * Copyright (c) 2021 - 2022 Deutsche Telekom AG.
 * Copyright (c) 2021 - 2022 CESNET, z.s.p.o.
 *
 * This source code is licensed under BSD 3-Clause License (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://opensource.org/licenses/BSD-3-Clause
 */

#include "tconfig.h"

/* augeas SR DS plugin */
#define AUG_TEST_INPUT_FILES AUG_CONFIG_FILES_DIR "/passwd"
#include "srds_augeas.c"

#include <assert.h>
#include <dlfcn.h>
#include <setjmp.h>
#include <stdarg.h>
#include <unistd.h>

#include <cmocka.h>
#include <libyang/libyang.h>
#include <sysrepo/plugins_datastore.h>

static int
setup_f(void **state)
{
    return tsetup_glob(state, "passwd", &srpds__, AUG_TEST_INPUT_FILES);
}

static void
test_load(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    char *str;

    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));
    lyd_print_mem(&str, st->data, LYD_XML, LYD_PRINT_WITHSIBLINGS);

    assert_string_equal(str,
            "<passwd xmlns=\"aug:passwd\">\n"
            "  <config-file>" AUG_CONFIG_FILES_DIR "/passwd</config-file>\n"
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
            "  <nis>\n"
            "    <username>some-nis-group</username>\n"
            "  </nis>\n"
            "  <nis>\n"
            "    <username>bob</username>\n"
            "    <home>/home/bob</home>\n"
            "    <shell>/bin/bash</shell>\n"
            "  </nis>\n"
            "  <nisdefault>\n"
            "    <_id>1</_id>\n"
            "    <password/>\n"
            "    <uid/>\n"
            "    <gid/>\n"
            "  </nisdefault>\n"
            "  <nisdefault>\n"
            "    <_id>2</_id>\n"
            "    <password/>\n"
            "    <uid/>\n"
            "    <gid/>\n"
            "    <shell>/sbin/nologin</shell>\n"
            "  </nisdefault>\n"
            "  <nisdefault>\n"
            "    <_id>3</_id>\n"
            "  </nisdefault>\n"
            "  <plus_nisuser>\n"
            "    <username>cecil</username>\n"
            "    <name>User Comment</name>\n"
            "    <home>/home/bob</home>\n"
            "    <shell>/bin/bash</shell>\n"
            "  </plus_nisuser>\n"
            "  <minus_nisuser>\n"
            "    <username>alice</username>\n"
            "  </minus_nisuser>\n"
            "</passwd>\n");
    free(str);
}

static void
test_store_add(void **state)
{
    struct tstate *st = (struct tstate *)*state;
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

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "14a15\n"
            "> admin:x:2000:200:The Admin:/home/admin:/bin/bash"));
}

static void
test_store_modify(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *user;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* change shell of nobody */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry[username='nobody']/shell", "/bin/sh",
            LYD_NEW_PATH_UPDATE, &user));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "7c7\n"
            "< nobody:x:65534:65534:nobody:/var/lib/nobody:/bin/bash\n"
            "---\n"
            "> nobody:x:65534:65534:nobody:/var/lib/nobody:/bin/sh"));
}

static void
test_store_remove(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *node;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* remove chrony user */
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "entry[username='chrony']", 0, &node));
    lyd_free_tree(node);

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "3d2\n"
            "< chrony:x:473:475:Chrony Daemon:/var/lib/chrony:/bin/false"));
}

int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_teardown(test_load, tteardown),
        cmocka_unit_test_teardown(test_store_add, tteardown),
        cmocka_unit_test_teardown(test_store_modify, tteardown),
        cmocka_unit_test_teardown(test_store_remove, tteardown),
    };

    return cmocka_run_group_tests(tests, setup_f, tteardown_glob);
}
