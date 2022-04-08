/**
 * @file test_pam.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief pam SR DS plugin test
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
#define AUG_TEST_INPUT_FILES AUG_CONFIG_FILES_DIR "/pam"
#include "srds_augeas.c"

#include <assert.h>
#include <dlfcn.h>
#include <setjmp.h>
#include <stdarg.h>
#include <unistd.h>

#include <cmocka.h>
#include <libyang/libyang.h>
#include <sysrepo/plugins_datastore.h>

#define AUG_TEST_MODULE "pam"

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
            "    <record-svc>\n"
            "      <record>1</record>\n"
            "      <type>session</type>\n"
            "      <control>required</control>\n"
            "      <module>pam_limits.so</module>\n"
            "    </record-svc>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>2</_id>\n"
            "    <record-svc>\n"
            "      <record>2</record>\n"
            "      <type>auth</type>\n"
            "      <control>required</control>\n"
            "      <module>pam_unix.so</module>\n"
            "      <argument>try_first_pass</argument>\n"
            "      <argument>quiet</argument>\n"
            "    </record-svc>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>3</_id>\n"
            "    <record-svc>\n"
            "      <record>3</record>\n"
            "      <type>session</type>\n"
            "      <control>optional</control>\n"
            "      <module>common-auth</module>\n"
            "    </record-svc>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>4</_id>\n"
            "    <record-svc>\n"
            "      <record>4</record>\n"
            "      <type>account</type>\n"
            "      <control>optional</control>\n"
            "      <module>pam_env.so</module>\n"
            "      <argument>revoke</argument>\n"
            "      <argument>force</argument>\n"
            "    </record-svc>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>5</_id>\n"
            "    <record-svc>\n"
            "      <record>5</record>\n"
            "      <type>session</type>\n"
            "      <control>include</control>\n"
            "      <module>pam_systemd.so</module>\n"
            "      <argument>onerr=succeed</argument>\n"
            "      <argument>sense=allow</argument>\n"
            "    </record-svc>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>6</_id>\n"
            "    <record-svc>\n"
            "      <record>6</record>\n"
            "      <type>password</type>\n"
            "      <control>include</control>\n"
            "      <module>common-password</module>\n"
            "    </record-svc>\n"
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
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='7']/record-svc/record",
            "7", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='7']/record-svc/type",
            "auth", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='7']/record-svc/control",
            "optional", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='7']/record-svc/module",
            "my_module.so", 0, NULL));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='4']/record-svc/argument",
            "quiet", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='4']/record-svc/argument[.='revoke']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='3']/record-svc/optional", NULL, 0, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "3,4c3,4\n"
            "< session  optional        common-auth\n"
            "< account  optional        pam_env.so      revoke force\n"
            "---\n"
            "> -session  optional        common-auth\n"
            "> account  optional        pam_env.so      revoke quiet force\n"
            "6a7\n"
            "> auth optional my_module.so"));
}

static void
test_store_modify(void **state)
{
    struct tstate *st = (struct tstate *)*state;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* modify some values */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='2']/record-svc/control",
            "optional", LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='5']/record-svc/type",
            "password", LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='4']/record-svc/module",
            "pam_acc.so", LYD_NEW_PATH_UPDATE, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "2c2\n"
            "< auth     required        pam_unix.so     try_first_pass quiet\n"
            "---\n"
            "> auth     optional        pam_unix.so     try_first_pass quiet\n"
            "4,5c4,5\n"
            "< account  optional        pam_env.so      revoke force\n"
            "< session  include         pam_systemd.so  onerr=succeed sense=allow\n"
            "---\n"
            "> account  optional        pam_acc.so      revoke force\n"
            "> password  include         pam_systemd.so  onerr=succeed sense=allow"));
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
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='4']/record-svc/argument[.='revoke']", 0, &node));
    lyd_free_tree(node);

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "2d1\n"
            "< auth     required        pam_unix.so     try_first_pass quiet\n"
            "4c3\n"
            "< account  optional        pam_env.so      revoke force\n"
            "---\n"
            "> account  optional        pam_env.so      force"));
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
