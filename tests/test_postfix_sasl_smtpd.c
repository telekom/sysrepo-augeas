/**
 * @file test_postfix_sasl_smtpd.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief postfix_sasl_smtpd SR DS plugin test
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
#define AUG_TEST_INPUT_FILES AUG_CONFIG_FILES_DIR "/postfix_sasl_smtpd"
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
    return tsetup_glob(state, "postfix_sasl_smtpd", &srpds__, AUG_TEST_INPUT_FILES);
}

static void
test_load(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    char *str;

    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));
    lyd_print_mem(&str, st->data, LYD_XML, LYD_PRINT_WITHSIBLINGS);

    assert_string_equal(str,
            "<postfix_sasl_smtpd xmlns=\"aug:postfix_sasl_smtpd\">\n"
            "  <config-file>" AUG_CONFIG_FILES_DIR "/postfix_sasl_smtpd</config-file>\n"
            "  <pwcheck_method>\n"
            "    <_id>1</_id>\n"
            "    <value_to_eol>auxprop saslauthd</value_to_eol>\n"
            "  </pwcheck_method>\n"
            "  <auxprop_plugin>\n"
            "    <_id>1</_id>\n"
            "    <value_to_eol>plesk</value_to_eol>\n"
            "  </auxprop_plugin>\n"
            "  <saslauthd_path>\n"
            "    <_id>1</_id>\n"
            "    <value_to_eol>/private/plesk_saslauthd</value_to_eol>\n"
            "  </saslauthd_path>\n"
            "  <mech_list>\n"
            "    <_id>1</_id>\n"
            "    <value_to_eol>CRAM-MD5 PLAIN LOGIN</value_to_eol>\n"
            "  </mech_list>\n"
            "  <sql_engine>\n"
            "    <_id>1</_id>\n"
            "    <value_to_eol>intentionally disabled</value_to_eol>\n"
            "  </sql_engine>\n"
            "  <log_level>\n"
            "    <_id>1</_id>\n"
            "    <value_to_eol>4</value_to_eol>\n"
            "  </log_level>\n"
            "</postfix_sasl_smtpd>\n");
    free(str);
}

static void
test_store_add(void **state)
{
    struct tstate *st = (struct tstate *)*state;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* add some new list instances */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "auxprop_plugin[_id='2']/value_to_eol", "flask", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "sql_engine[_id='2']/value_to_eol", "old", 0, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "6a7,8\n"
            "> auxprop_plugin: flask\n"
            "> sql_engine: old"));
}

static void
test_store_modify(void **state)
{
    struct tstate *st = (struct tstate *)*state;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* modify some values */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "mech_list[_id='1']/value_to_eol", "CRAM-MD5 PLAIN",
            LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "pwcheck_method[_id='1']/value_to_eol", "auxprop",
            LYD_NEW_PATH_UPDATE, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "1c1\n"
            "< pwcheck_method: auxprop saslauthd\n"
            "---\n"
            "> pwcheck_method: auxprop\n"
            "4c4\n"
            "< mech_list: CRAM-MD5 PLAIN LOGIN\n"
            "---\n"
            "> mech_list: CRAM-MD5 PLAIN"));
}

static void
test_store_remove(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *node;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* remove 2 list values */
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "saslauthd_path[_id='1']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "sql_engine[_id='1']", 0, &node));
    lyd_free_tree(node);

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "3d2\n"
            "< saslauthd_path: /private/plesk_saslauthd\n"
            "5d3\n"
            "< sql_engine: intentionally disabled"));
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
