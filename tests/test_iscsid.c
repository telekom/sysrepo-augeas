/**
 * @file test_iscsid.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief iscsid SR DS plugin test
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
#define AUG_TEST_INPUT_FILES AUG_CONFIG_FILES_DIR "/iscsid"
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

#define AUG_TEST_MODULE "iscsid"

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
            "  <kv-list>\n"
            "    <_id>1</_id>\n"
            "    <kv>\n"
            "      <key>isns.address</key>\n"
            "      <value>127.0.0.1</value>\n"
            "    </kv>\n"
            "  </kv-list>\n"
            "  <kv-list>\n"
            "    <_id>2</_id>\n"
            "    <kv>\n"
            "      <key>isns.port</key>\n"
            "      <value>3260</value>\n"
            "    </kv>\n"
            "  </kv-list>\n"
            "  <kv-list>\n"
            "    <_id>3</_id>\n"
            "    <kv>\n"
            "      <key>node.session.auth.authmethod</key>\n"
            "      <value>CHAP</value>\n"
            "    </kv>\n"
            "  </kv-list>\n"
            "  <kv-list>\n"
            "    <_id>4</_id>\n"
            "    <kv>\n"
            "      <key>node.session.auth.username</key>\n"
            "      <value>someuser1</value>\n"
            "    </kv>\n"
            "  </kv-list>\n"
            "  <kv-list>\n"
            "    <_id>5</_id>\n"
            "    <kv>\n"
            "      <key>node.session.auth.password</key>\n"
            "      <value>somep$31#$^&amp;7!</value>\n"
            "    </kv>\n"
            "  </kv-list>\n"
            "  <kv-list>\n"
            "    <_id>6</_id>\n"
            "    <kv>\n"
            "      <key>discovery.sendtargets.auth.authmethod</key>\n"
            "      <value>CHAP</value>\n"
            "    </kv>\n"
            "  </kv-list>\n"
            "  <kv-list>\n"
            "    <_id>7</_id>\n"
            "    <kv>\n"
            "      <key>discovery.sendtargets.auth.username</key>\n"
            "      <value>someuser3</value>\n"
            "    </kv>\n"
            "  </kv-list>\n"
            "  <kv-list>\n"
            "    <_id>8</_id>\n"
            "    <kv>\n"
            "      <key>discovery.sendtargets.auth.password</key>\n"
            "      <value>_09+7)(,./?;'p[]</value>\n"
            "    </kv>\n"
            "  </kv-list>\n"
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
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "kv-list[_id='9']/kv/key", "my.var", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "kv-list[_id='9']/kv/value", "val", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "kv-list[_id='6']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "16a17\n"
            "> my.var = val\n"));
}

static void
test_store_modify(void **state)
{
    struct tstate *st = (struct tstate *)*state;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* modify some values */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "kv-list[_id='3']/kv/key", "node.session.auth",
            LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "kv-list[_id='7']/kv/value", "nobody",
            LYD_NEW_PATH_UPDATE, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "11c11\n"
            "< node.session.auth.authmethod = CHAP\n"
            "---\n"
            "> node.session.auth = CHAP\n"
            "20c20\n"
            "< discovery.sendtargets.auth.username = someuser3\n"
            "---\n"
            "> discovery.sendtargets.auth.username = nobody\n"));
}

static void
test_store_remove(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *node;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* remove list values */
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "kv-list[_id='2']", 0, &node));
    lyd_free_tree(node);

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "3d2\n"
            "< isns.port = 3260\n"));
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
