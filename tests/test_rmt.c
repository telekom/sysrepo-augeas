/**
 * @file test_rmt.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief rmt SR DS plugin test
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
#define AUG_TEST_INPUT_FILES AUG_CONFIG_FILES_DIR "/rmt"
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

#define AUG_TEST_MODULE "rmt"

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
            "  <config-entries>\n"
            "    <_id>1</_id>\n"
            "    <debug>/tmp/RMT</debug>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>2</_id>\n"
            "    <user>*</user>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>3</_id>\n"
            "    <access>\n"
            "      <name>rtape</name>\n"
            "      <host>sparky</host>\n"
            "      <path>/dev/rmt/*</path>\n"
            "    </access>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>4</_id>\n"
            "    <access>\n"
            "      <name>*</name>\n"
            "      <host>*</host>\n"
            "      <path>/dev/rmt/*</path>\n"
            "    </access>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>5</_id>\n"
            "    <access>\n"
            "      <name>*</name>\n"
            "      <host>*</host>\n"
            "      <path>*</path>\n"
            "    </access>\n"
            "  </config-entries>\n"
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
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='6']/debug", "/tmp/dbg", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='1']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_before(node, entries));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='7']/user", "nobody", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='2']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='8']/access/name", "me", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='8']/access/host", "localhost", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='8']/access/path", "/", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='4']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "4a5\n"
            "> DEBUG=/tmp/dbg\n"
            "6a8\n"
            "> USER=nobody\n"
            "9a12\n"
            "> ACCESS=me\tlocalhost\t/\n"));
}

static void
test_store_modify(void **state)
{
    struct tstate *st = (struct tstate *)*state;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* modify some values */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='1']/debug", "/tmp/debug",
            LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='2']/user", "rmt",
            LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='3']/access/name", "ftape",
            LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='4']/access/host", "*.com",
            LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='5']/access/path", "/home",
            LYD_NEW_PATH_UPDATE, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "5,6c5,6\n"
            "< DEBUG=/tmp/RMT\n"
            "< USER=*\n"
            "---\n"
            "> DEBUG=/tmp/debug\n"
            "> USER=rmt\n"
            "8,9c8,9\n"
            "< ACCESS=rtape\tsparky\t/dev/rmt/*\n"
            "< ACCESS=*\t*\t/dev/rmt/*\n"
            "---\n"
            "> ACCESS=ftape\tsparky\t/dev/rmt/*\n"
            "> ACCESS=*\t*.com\t/dev/rmt/*\n"
            "12c12\n"
            "< ACCESS=*\t*\t*\n"
            "---\n"
            "> ACCESS=*\t*\t/home\n"));
}

static void
test_store_remove(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *node;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* remove list values */
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='2']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='4']", 0, &node));
    lyd_free_tree(node);

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "6d5\n"
            "< USER=*\n"
            "9d7\n"
            "< ACCESS=*\t*\t/dev/rmt/*\n"));
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
