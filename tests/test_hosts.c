/**
 * @file test_hosts.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief hosts SR DS plugin test
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

#include "tconfig.h"

/* augeas SR DS plugin */
#define AUG_TEST_INPUT_FILES AUG_CONFIG_FILES_DIR "/hosts"
#include "srds_augeas.c"
#include "srdsa_init.c"
#include "srdsa_load.c"
#include "srdsa_store.c"
#include "srdsa_common.c"

#include <assert.h>
#include <dlfcn.h>
#include <setjmp.h>
#include <stdarg.h>
#include <unistd.h>

#include <cmocka.h>
#include <libyang/libyang.h>
#include <sysrepo/plugins_datastore.h>

#define AUG_TEST_MODULE "hosts"

static int
setup_f(void **state)
{
    return tsetup_glob(state, AUG_TEST_MODULE, &srpds__, AUG_TEST_INPUT_FILES);
}

static void
test_load(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    char *str;

    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));
    lyd_print_mem(&str, st->data, LYD_XML, LYD_PRINT_WITHSIBLINGS);

    assert_string_equal(str,
            "<" AUG_TEST_MODULE " xmlns=\"aug:" AUG_TEST_MODULE "\">\n"
            "  <config-file>" AUG_CONFIG_FILES_DIR "/" AUG_TEST_MODULE "</config-file>\n"
            "  <host-list>\n"
            "    <_seq>1</_seq>\n"
            "    <ipaddr>127.0.0.1</ipaddr>\n"
            "    <canonical>foo</canonical>\n"
            "    <alias>foo.example.com</alias>\n"
            "  </host-list>\n"
            "  <host-list>\n"
            "    <_seq>2</_seq>\n"
            "    <ipaddr>192.168.0.1</ipaddr>\n"
            "    <canonical>pigiron.example.com</canonical>\n"
            "    <alias>pigiron</alias>\n"
            "    <alias>pigiron.example</alias>\n"
            "  </host-list>\n"
            "  <host-list>\n"
            "    <_seq>3</_seq>\n"
            "    <ipaddr>::1</ipaddr>\n"
            "    <canonical>localhost</canonical>\n"
            "    <alias>ipv6-localhost</alias>\n"
            "    <alias>ipv6-loopback</alias>\n"
            "  </host-list>\n"
            "  <host-list>\n"
            "    <_seq>4</_seq>\n"
            "    <ipaddr>fe00::0</ipaddr>\n"
            "    <canonical>ipv6-localnet</canonical>\n"
            "  </host-list>\n"
            "  <host-list>\n"
            "    <_seq>5</_seq>\n"
            "    <ipaddr>ff00::0</ipaddr>\n"
            "    <canonical>ipv6-mcastprefix</canonical>\n"
            "  </host-list>\n"
            "  <host-list>\n"
            "    <_seq>6</_seq>\n"
            "    <ipaddr>ff02::1</ipaddr>\n"
            "    <canonical>ipv6-allnodes</canonical>\n"
            "  </host-list>\n"
            "  <host-list>\n"
            "    <_seq>7</_seq>\n"
            "    <ipaddr>ff02::2</ipaddr>\n"
            "    <canonical>ipv6-allrouters</canonical>\n"
            "  </host-list>\n"
            "  <host-list>\n"
            "    <_seq>8</_seq>\n"
            "    <ipaddr>ff02::3</ipaddr>\n"
            "    <canonical>ipv6-allhosts</canonical>\n"
            "  </host-list>\n"
            "</" AUG_TEST_MODULE ">\n");
    free(str);
}

static void
test_store_add(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *entries, *node;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* add some new list instances */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "host-list[_seq='9']/ipaddr", "10.0.0.1", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "host-list[_seq='9']/canonical", "local-net", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "host-list[_seq='2']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "host-list[_seq='6']/alias", "6all", 0, &entries));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "host-list[_seq='3']/alias", "6loop", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "host-list[_seq='3']/alias[.='ipv6-localhost']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_before(node, entries));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "3a4\n"
            "> 10.0.0.1\tlocal-net\n"
            "6c7\n"
            "< ::1             localhost ipv6-localhost ipv6-loopback\n"
            "---\n"
            "> ::1             localhost 6loop ipv6-localhost ipv6-loopback\n"
            "11c12\n"
            "< ff02::1         ipv6-allnodes\n"
            "---\n"
            "> ff02::1         ipv6-allnodes 6all\n"));
}

static void
test_store_modify(void **state)
{
    struct tstate *st = (struct tstate *)*state;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* modify some values */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "host-list[_seq='1']/canonical", "localhost",
            LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "host-list[_seq='2']/ipaddr", "192.168.1.1",
            LYD_NEW_PATH_UPDATE, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "1c1\n"
            "< 127.0.0.1 foo foo.example.com\n"
            "---\n"
            "> 127.0.0.1 localhost foo.example.com\n"
            "3c3\n"
            "< 192.168.0.1 pigiron.example.com pigiron pigiron.example\n"
            "---\n"
            "> 192.168.1.1 pigiron.example.com pigiron pigiron.example\n"));
}

static void
test_store_remove(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *node;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* remove list values */
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "host-list[_seq='4']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "host-list[_seq='3']/alias[.='ipv6-loopback']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "host-list[_seq='2']/alias[.='pigiron']", 0, &node));
    lyd_free_tree(node);

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "3c3\n"
            "< 192.168.0.1 pigiron.example.com pigiron pigiron.example\n"
            "---\n"
            "> 192.168.0.1 pigiron.example.com pigiron.example\n"
            "6c6\n"
            "< ::1             localhost ipv6-localhost ipv6-loopback\n"
            "---\n"
            "> ::1             localhost ipv6-localhost\n"
            "8d7\n"
            "< fe00::0         ipv6-localnet\n"));
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
