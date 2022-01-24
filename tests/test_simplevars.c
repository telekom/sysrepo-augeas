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
    return tsetup_glob(state, "simplevars", &srpds__, AUG_TEST_INPUT_FILES);
}

static void
test_load(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    char *str;

    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));
    lyd_print_mem(&str, st->data, LYD_XML, LYD_PRINT_WITHSIBLINGS);

    assert_string_equal(str,
            "<simplevars xmlns=\"aug:simplevars\">\n"
            "  <config-file>" AUG_CONFIG_FILES_DIR "/simplevars</config-file>\n"
            "  <entry>\n"
            "    <_id>mykey</_id>\n"
            "    <to_comment_re>myvalue</to_comment_re>\n"
            "  </entry>\n"
            "  <entry>\n"
            "    <_id>anotherkey</_id>\n"
            "    <to_comment_re>another value</to_comment_re>\n"
            "  </entry>\n"
            "  <entry>\n"
            "    <_id>UserParameter</_id>\n"
            "    <to_comment_re>custom.vfs.dev.read.ops[*],cat /proc/diskstats | grep $1 | head -1 | awk '{print $$4}'</to_comment_re>\n"
            "  </entry>\n"
            "  <entry>\n"
            "    <_id>foo</_id>\n"
            "    <to_comment_re/>\n"
            "  </entry>\n"
            "</simplevars>\n"
            "<simplevars xmlns=\"aug:simplevars\">\n"
            "  <config-file>" AUG_CONFIG_FILES_DIR "/simplevars2</config-file>\n"
            "  <entry>\n"
            "    <_id>key1</_id>\n"
            "    <to_comment_re>value1</to_comment_re>\n"
            "  </entry>\n"
            "  <entry>\n"
            "    <_id>key2</_id>\n"
            "    <to_comment_re>value2</to_comment_re>\n"
            "  </entry>\n"
            "  <entry>\n"
            "    <_id>key3</_id>\n"
            "    <to_comment_re>value3</to_comment_re>\n"
            "  </entry>\n"
            "</simplevars>\n");
    free(str);
}

static void
test_store_add(void **state)
{
    struct tstate *st = (struct tstate *)*state;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* add some variable to both files */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry[_id='newvar']/to_comment_re", "value", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data->next, NULL, "entry[_id='newvar2']/to_comment_re", "value", 0, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "6a7\n"
            "> newvar = value",
            "7a8\n"
            "> newvar2 = value"));
}

static void
test_store_modify(void **state)
{
    struct tstate *st = (struct tstate *)*state;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* modify a variable in the second file */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data->next, NULL, "entry[_id='mykey']/to_comment_re", "changed value",
            LYD_NEW_PATH_UPDATE, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "",
            "7a8\n"
            "> mykey = changed value"));
}

static void
test_store_remove(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *node;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* remove 2 variables from the first file */
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "entry[_id='UserParameter']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "entry[_id='anotherkey']", 0, &node));
    lyd_free_tree(node);

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "4,5d3\n"
            "< anotherkey = another value\n"
            "< UserParameter=custom.vfs.dev.read.ops[*],cat /proc/diskstats | grep $1 | head -1 | awk '{print $$4}'",
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