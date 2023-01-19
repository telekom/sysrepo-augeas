/**
 * @file test_simplevars.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief simplevars SR DS plugin test
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
#define AUG_TEST_INPUT_FILES AUG_CONFIG_FILES_DIR "/simplevars;" AUG_CONFIG_FILES_DIR "/simplevars2"
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

#define AUG_TEST_MODULE "simplevars"

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
            "  <entry-list>\n"
            "    <_id>1</_id>\n"
            "    <entry>\n"
            "      <word>mykey</word>\n"
            "      <to-comment>myvalue</to-comment>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>2</_id>\n"
            "    <entry>\n"
            "      <word>anotherkey</word>\n"
            "      <to-comment>another value</to-comment>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>3</_id>\n"
            "    <entry>\n"
            "      <word>UserParameter</word>\n"
            "      <to-comment>custom.vfs.dev.read.ops[*],cat /proc/diskstats | grep $1 | head -1 | awk '{print $$4}'</to-comment>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>4</_id>\n"
            "    <entry>\n"
            "      <word>foo</word>\n"
            "      <to-comment/>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "</simplevars>\n"
            "<simplevars xmlns=\"aug:simplevars\">\n"
            "  <config-file>" AUG_CONFIG_FILES_DIR "/simplevars2</config-file>\n"
            "  <entry-list>\n"
            "    <_id>1</_id>\n"
            "    <entry>\n"
            "      <word>key1</word>\n"
            "      <to-comment>value1</to-comment>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>2</_id>\n"
            "    <entry>\n"
            "      <word>key2</word>\n"
            "      <to-comment>value2</to-comment>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>3</_id>\n"
            "    <entry>\n"
            "      <word>key3</word>\n"
            "      <to-comment>value3</to-comment>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "</" AUG_TEST_MODULE ">\n");
    free(str);
}

static void
test_store_add(void **state)
{
    struct tstate *st = (struct tstate *)*state;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* add some variable to both files */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_id='5']/entry/word", "newvar", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_id='5']/entry/to-comment", "value", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data->next, NULL, "entry-list[_id='4']/entry/word", "newvar2", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data->next, NULL, "entry-list[_id='4']/entry/to-comment",
            "value", 0, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "6a7\n"
            "> newvar = value\n",
            "7a8\n"
            "> newvar2 = value\n"));
}

static void
test_store_modify(void **state)
{
    struct tstate *st = (struct tstate *)*state;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* modify a variable in the second file */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data->next, NULL, "entry-list[_id='2']/entry/to-comment",
            "changed value", LYD_NEW_PATH_UPDATE, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "",
            "4c4\n"
            "< key2 = value2\n"
            "---\n"
            "> key2 = changed value\n"));
}

static void
test_store_remove(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *node;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* remove 2 variables from the first file */
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "entry-list[_id='3']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "entry-list[_id='2']", 0, &node));
    lyd_free_tree(node);

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "4,5d3\n"
            "< anotherkey = another value\n"
            "< UserParameter=custom.vfs.dev.read.ops[*],cat /proc/diskstats | grep $1 | head -1 | awk '{print $$4}'\n",
            ""));
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
