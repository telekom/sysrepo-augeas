/**
 * @file test_backuppchosts.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief ethers SR DS plugin test
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
#define AUG_TEST_INPUT_FILES AUG_CONFIG_FILES_DIR "/backuppchosts"
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

#define AUG_TEST_MODULE "backuppchosts"

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
            "      <id>1</id>\n"
            "      <host>host</host>\n"
            "      <dhcp>dhcp</dhcp>\n"
            "      <user>user</user>\n"
            "      <moreusers>moreUsers</moreusers>\n"
            "    </record>\n"
            "  </record-list>\n"
            "  <record-list>\n"
            "    <_id>2</_id>\n"
            "    <record>\n"
            "      <id>2</id>\n"
            "      <host>hostname1</host>\n"
            "      <dhcp>0</dhcp>\n"
            "      <user>user1</user>\n"
            "      <moreusers>anotheruser</moreusers>\n"
            "      <moreusers>athirduser</moreusers>\n"
            "    </record>\n"
            "  </record-list>\n"
            "  <record-list>\n"
            "    <_id>3</_id>\n"
            "    <record>\n"
            "      <id>3</id>\n"
            "      <host>hostname2</host>\n"
            "      <dhcp>1</dhcp>\n"
            "      <user>user2</user>\n"
            "      <moreusers>stillanotheruser</moreusers>\n"
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
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='4']/record/id", "4", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='4']/record/host", "hostname3", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='4']/record/dhcp", "maybe", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='4']/record/user", "nobody", 0, NULL));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='2']/record/moreusers", "forthuser", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "record-list[_id='2']/record/moreusers[.='anotheruser']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_before(node, entries));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "2c2\n"
            "< hostname1     0     user1     anotheruser,athirduser\n"
            "---\n"
            "> hostname1     0     user1     forthuser,anotheruser,athirduser\n"
            "3a4\n"
            "> hostname3\tmaybe\tnobody\n"));
}

static void
test_store_modify(void **state)
{
    struct tstate *st = (struct tstate *)*state;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* modify some values */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='1']/record/host", "myhost",
            LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='3']/record/dhcp", "no",
            LYD_NEW_PATH_UPDATE, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "1c1\n"
            "< host        dhcp    user      moreUsers\n"
            "---\n"
            "> myhost        dhcp    user      moreUsers\n"
            "3c3\n"
            "< hostname2     1     user2     stillanotheruser\n"
            "---\n"
            "> hostname2     no     user2     stillanotheruser\n"));
}

static void
test_store_remove(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *node;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* remove list values */
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "record-list[_id='2']/record/moreusers[.='athirduser']", 0, &node));
    lyd_free_tree(node);

    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "record-list[_id='3']/record/moreusers[.='stillanotheruser']", 0, &node));
    lyd_free_tree(node);

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "2,3c2,3\n"
            "< hostname1     0     user1     anotheruser,athirduser\n"
            "< hostname2     1     user2     stillanotheruser\n"
            "---\n"
            "> hostname1     0     user1     anotheruser\n"
            "> hostname2     1     user2\n"));
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
