/**
 * @file test_anaconda.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief anaconda SR DS plugin test
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
#define AUG_TEST_INPUT_FILES AUG_CONFIG_FILES_DIR "/anaconda"
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

#define AUG_TEST_MODULE "anaconda"

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
            "  <record-list>\n"
            "    <_id>1</_id>\n"
            "    <record>\n"
            "      <record-re>General</record-re>\n"
            "      <entry-re-list>\n"
            "        <_id>1</_id>\n"
            "        <entry-re>\n"
            "          <entry-re>post_install_tools_disabled</entry-re>\n"
            "          <value>0</value>\n"
            "        </entry-re>\n"
            "      </entry-re-list>\n"
            "    </record>\n"
            "  </record-list>\n"
            "  <record-list>\n"
            "    <_id>2</_id>\n"
            "    <record>\n"
            "      <record-re>DatetimeSpoke</record-re>\n"
            "      <entry-re-list>\n"
            "        <_id>1</_id>\n"
            "        <entry-re>\n"
            "          <entry-re>visited</entry-re>\n"
            "          <value>1</value>\n"
            "        </entry-re>\n"
            "      </entry-re-list>\n"
            "      <entry-re-list>\n"
            "        <_id>2</_id>\n"
            "        <entry-re>\n"
            "          <entry-re>changed_timezone</entry-re>\n"
            "          <value>1</value>\n"
            "        </entry-re>\n"
            "      </entry-re-list>\n"
            "      <entry-re-list>\n"
            "        <_id>3</_id>\n"
            "        <entry-re>\n"
            "          <entry-re>changed_ntp</entry-re>\n"
            "          <value>0</value>\n"
            "        </entry-re>\n"
            "      </entry-re-list>\n"
            "      <entry-re-list>\n"
            "        <_id>4</_id>\n"
            "        <entry-re>\n"
            "          <entry-re>changed_timedate</entry-re>\n"
            "          <value>1</value>\n"
            "        </entry-re>\n"
            "      </entry-re-list>\n"
            "    </record>\n"
            "  </record-list>\n"
            "  <record-list>\n"
            "    <_id>3</_id>\n"
            "    <record>\n"
            "      <record-re>KeyboardSpoke</record-re>\n"
            "      <entry-re-list>\n"
            "        <_id>1</_id>\n"
            "        <entry-re>\n"
            "          <entry-re>visited</entry-re>\n"
            "          <value>0</value>\n"
            "        </entry-re>\n"
            "      </entry-re-list>\n"
            "    </record>\n"
            "  </record-list>\n"
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
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='4']/record/record-re", "MouseSpoke", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='4']/record/entry-re-list[_id='1']/"
            "entry-re/entry-re", "visited", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='4']/record/entry-re-list[_id='1']/"
            "entry-re/value", "1", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "record-list[_id='2']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='3']/record/entry-re-list[_id='2']/"
            "entry-re/entry-re", "doubleclick_delay", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='3']/record/entry-re-list[_id='2']/"
            "entry-re/value", "300ms", 0, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "12a13,14\n"
            "> [MouseSpoke]\n"
            "> visited=1\n"
            "15a18\n"
            "> doubleclick_delay=300ms\n"));
}

static void
test_store_modify(void **state)
{
    struct tstate *st = (struct tstate *)*state;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* modify some values */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='3']/record/entry-re-list[_id='1']/"
            "entry-re/value", "1", LYD_NEW_PATH_UPDATE, NULL));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='2']/record/record-re", "DateAndTimeSpoke",
            LYD_NEW_PATH_UPDATE, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "6c6\n"
            "< [DatetimeSpoke]\n"
            "---\n"
            "> [DateAndTimeSpoke]\n"
            "15c15\n"
            "< visited=0\n"
            "---\n"
            "> visited=1\n"));
}

static void
test_store_remove(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *node;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* remove list values */
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "record-list[_id='2']/record/entry-re-list[_id='3']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "record-list[_id='3']", 0, &node));
    lyd_free_tree(node);

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "10d9\n"
            "< changed_ntp=0\n"
            "13,15d11\n"
            "< [KeyboardSpoke]\n"
            "< # the keyboard spoke has not been visited\n"
            "< visited=0\n"));
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
