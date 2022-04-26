/**
 * @file test_cpanel.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief cmdline SR DS plugin test
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
#define AUG_TEST_INPUT_FILES AUG_CONFIG_FILES_DIR "/cpanel"
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

#define AUG_TEST_MODULE "cpanel"

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
            "      <label>skipantirelayd</label>\n"
            "      <value>1</value>\n"
            "    </kv>\n"
            "  </kv-list>\n"
            "  <kv-list>\n"
            "    <_id>2</_id>\n"
            "    <kv>\n"
            "      <label>ionice_optimizefs</label>\n"
            "      <value>6</value>\n"
            "    </kv>\n"
            "  </kv-list>\n"
            "  <kv-list>\n"
            "    <_id>3</_id>\n"
            "    <kv>\n"
            "      <label>account_login_access</label>\n"
            "      <value>owner_root</value>\n"
            "    </kv>\n"
            "  </kv-list>\n"
            "  <kv-list>\n"
            "    <_id>4</_id>\n"
            "    <kv>\n"
            "      <label>enginepl</label>\n"
            "      <value>cpanel.pl</value>\n"
            "    </kv>\n"
            "  </kv-list>\n"
            "  <kv-list>\n"
            "    <_id>5</_id>\n"
            "    <kv>\n"
            "      <label>stats_log</label>\n"
            "      <value>/usr/local/cpanel/logs/stats_log</value>\n"
            "    </kv>\n"
            "  </kv-list>\n"
            "  <kv-list>\n"
            "    <_id>6</_id>\n"
            "    <kv>\n"
            "      <label>cpaddons_notify_users</label>\n"
            "      <value>Allow users to choose</value>\n"
            "    </kv>\n"
            "  </kv-list>\n"
            "  <kv-list>\n"
            "    <_id>7</_id>\n"
            "    <kv>\n"
            "      <label>apache_port</label>\n"
            "      <value>0.0.0.0:80</value>\n"
            "    </kv>\n"
            "  </kv-list>\n"
            "  <kv-list>\n"
            "    <_id>8</_id>\n"
            "    <kv>\n"
            "      <label>allow_server_info_status_from</label>\n"
            "      <value/>\n"
            "    </kv>\n"
            "  </kv-list>\n"
            "  <kv-list>\n"
            "    <_id>9</_id>\n"
            "    <kv>\n"
            "      <label>system_diskusage_warn_percent</label>\n"
            "      <value>82.5500</value>\n"
            "    </kv>\n"
            "  </kv-list>\n"
            "  <kv-list>\n"
            "    <_id>10</_id>\n"
            "    <kv>\n"
            "      <label>maxemailsperhour</label>\n"
            "    </kv>\n"
            "  </kv-list>\n"
            "  <kv-list>\n"
            "    <_id>11</_id>\n"
            "    <kv>\n"
            "      <label>email_send_limits_max_defer_fail_percentage</label>\n"
            "    </kv>\n"
            "  </kv-list>\n"
            "  <kv-list>\n"
            "    <_id>12</_id>\n"
            "    <kv>\n"
            "      <label>default_archive-logs</label>\n"
            "      <value>1</value>\n"
            "    </kv>\n"
            "  </kv-list>\n"
            "  <kv-list>\n"
            "    <_id>13</_id>\n"
            "    <kv>\n"
            "      <label>SecurityPolicy::xml-api</label>\n"
            "      <value>1</value>\n"
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
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "kv-list[_id='14']/kv/label", "nolog", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "kv-list[_id='10']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "kv-list[_id='15']/kv/label", "custom-value", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "kv-list[_id='15']/kv/value", "myvalue", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "kv-list[_id='6']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "11a12\n"
            "> custom-value=myvalue\n"
            "15a17\n"
            "> nolog\n"));
}

static void
test_store_modify(void **state)
{
    struct tstate *st = (struct tstate *)*state;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* modify some values */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "kv-list[_id='8']/kv/value", "no", LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "kv-list[_id='10']/kv/label", "maxemailsperday",
            LYD_NEW_PATH_UPDATE, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "13c13\n"
            "< allow_server_info_status_from=\n"
            "---\n"
            "> allow_server_info_status_from=no\n"
            "15c15\n"
            "< maxemailsperhour\n"
            "---\n"
            "> maxemailsperday\n"));
}

static void
test_store_remove(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *node;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* remove list values */
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "kv-list[_id='8']/kv/value", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "kv-list[_id='12']", 0, &node));
    lyd_free_tree(node);

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "13c13\n"
            "< allow_server_info_status_from=\n"
            "---\n"
            "> allow_server_info_status_from\n"
            "17d16\n"
            "< default_archive-logs=1\n"));
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
