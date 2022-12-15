/**
 * @file test_smbusers.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief smbusers SR DS plugin test
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
#define AUG_TEST_INPUT_FILES AUG_CONFIG_FILES_DIR "/smbusers"
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

#define AUG_TEST_MODULE "smbusers"

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
            "      <word>jarwin</word>\n"
            "      <username>JosephArwin</username>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>2</_id>\n"
            "    <entry>\n"
            "      <word>manderso</word>\n"
            "      <username>MarkAnderson</username>\n"
            "      <username>MarkusAndersonus</username>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>3</_id>\n"
            "    <entry>\n"
            "      <word>users</word>\n"
            "      <username>@account</username>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>4</_id>\n"
            "    <entry>\n"
            "      <word>nobody</word>\n"
            "      <username>*</username>\n"
            "    </entry>\n"
            "  </entry-list>\n"
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
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_id='5']/entry/word", "root", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_id='5']/entry/username", "owner", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "entry-list[_id='2']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_id='2']/entry/username", "MA", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "entry-list[_id='2']/entry/username[.='MarkAnderson']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_before(node, entries));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "4c4,5\n"
            "< manderso = MarkAnderson MarkusAndersonus\n"
            "---\n"
            "> manderso = MA MarkAnderson MarkusAndersonus\n"
            "> root = owner\n"));
}

static void
test_store_modify(void **state)
{
    struct tstate *st = (struct tstate *)*state;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* modify some values */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_id='1']/entry/word", "jarvis",
            LYD_NEW_PATH_UPDATE, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "3c3\n"
            "< jarwin = JosephArwin\n"
            "---\n"
            "> jarvis = JosephArwin\n"));
}

static void
test_store_remove(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *node;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* remove list values */
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "entry-list[_id='2']/entry/username[.='MarkusAndersonus']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "entry-list[_id='3']", 0, &node));
    lyd_free_tree(node);

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "4,5c4\n"
            "< manderso = MarkAnderson MarkusAndersonus\n"
            "< users = @account\n"
            "---\n"
            "> manderso = MarkAnderson\n"));
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
