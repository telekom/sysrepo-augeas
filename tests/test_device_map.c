/**
 * @file test_device_map.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief device-map SR DS plugin test
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
#define AUG_TEST_INPUT_FILES AUG_CONFIG_FILES_DIR "/device-map"
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

#define AUG_TEST_MODULE "device-map"

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
            "  <map-list>\n"
            "    <_id>1</_id>\n"
            "    <map>\n"
            "      <label>fd0</label>\n"
            "      <fspath>/dev/fda</fspath>\n"
            "    </map>\n"
            "  </map-list>\n"
            "  <map-list>\n"
            "    <_id>2</_id>\n"
            "    <map>\n"
            "      <label>hd0</label>\n"
            "      <fspath>/dev/sda</fspath>\n"
            "    </map>\n"
            "  </map-list>\n"
            "  <map-list>\n"
            "    <_id>3</_id>\n"
            "    <map>\n"
            "      <label>cd0</label>\n"
            "      <fspath>/dev/cdrom</fspath>\n"
            "    </map>\n"
            "  </map-list>\n"
            "  <map-list>\n"
            "    <_id>4</_id>\n"
            "    <map>\n"
            "      <label>hd1,1</label>\n"
            "      <fspath>/dev/sdb1</fspath>\n"
            "    </map>\n"
            "  </map-list>\n"
            "  <map-list>\n"
            "    <_id>5</_id>\n"
            "    <map>\n"
            "      <label>hd0,a</label>\n"
            "      <fspath>/dev/sda1</fspath>\n"
            "    </map>\n"
            "  </map-list>\n"
            "  <map-list>\n"
            "    <_id>6</_id>\n"
            "    <map>\n"
            "      <label>0x80</label>\n"
            "      <fspath>/dev/sda</fspath>\n"
            "    </map>\n"
            "  </map-list>\n"
            "  <map-list>\n"
            "    <_id>7</_id>\n"
            "    <map>\n"
            "      <label>128</label>\n"
            "      <fspath>/dev/sda</fspath>\n"
            "    </map>\n"
            "  </map-list>\n"
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
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "map-list[_id='8']/map/label", "1", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "map-list[_id='8']/map/fspath", "/dev/floppy", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "map-list[_id='1']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "map-list[_id='9']/map/label", "hd2", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "map-list[_id='9']/map/fspath", "/dev/sdb", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "map-list[_id='3']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "2a3\n"
            "> (1)\t/dev/floppy\n"
            "4a6\n"
            "> (hd2)\t/dev/sdb\n"));
}

static void
test_store_modify(void **state)
{
    struct tstate *st = (struct tstate *)*state;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* modify some values */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "map-list[_id='1']/map/label", "fd1", LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "map-list[_id='5']/map/fspath", "/dev/sda2",
            LYD_NEW_PATH_UPDATE, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "2c2\n"
            "< (fd0)     /dev/fda\n"
            "---\n"
            "> (fd1)\t/dev/fda\n"
            "6c6\n"
            "< (hd0,a)   /dev/sda1\n"
            "---\n"
            "> (hd0,a)   /dev/sda2\n"));
}

static void
test_store_remove(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *node;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* remove list values */
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "map-list[_id='3']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "map-list[_id='6']", 0, &node));
    lyd_free_tree(node);

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "4d3\n"
            "< (cd0)     /dev/cdrom\n"
            "7d5\n"
            "< (0x80)    /dev/sda\n"));
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
