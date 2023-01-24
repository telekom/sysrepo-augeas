/**
 * @file test_ntpd.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief ntpd SR DS plugin test
 *
 * @copyright
 * Copyright (c) 2021 - 2022 Deutsche Telekom AG.
 * Copyright (c) 2021 - 2022 CESNET, z.s.p.o.
 *
 * This source code is licensed under BSD 3-Clause License (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://opensource.org/licenses/BSD-3-Clause
 */

#include "tconfig.h"

/* augeas SR DS plugin */
#define AUG_TEST_INPUT_FILES AUG_CONFIG_FILES_DIR "/ntpd"
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

#define AUG_TEST_MODULE "ntpd"

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
            "    <listen-on>\n"
            "      <address>*</address>\n"
            "      <rtable>5</rtable>\n"
            "    </listen-on>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>2</_id>\n"
            "    <server>\n"
            "      <address>ntp.example.org</address>\n"
            "    </server>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>3</_id>\n"
            "    <servers>\n"
            "      <address>pool.ntp.org</address>\n"
            "    </servers>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>4</_id>\n"
            "    <sensor>\n"
            "      <device>nmea0</device>\n"
            "      <correction>5</correction>\n"
            "      <stratum>2</stratum>\n"
            "    </sensor>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>5</_id>\n"
            "    <sensor>\n"
            "      <device>*</device>\n"
            "      <refid>GPS</refid>\n"
            "    </sensor>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>6</_id>\n"
            "    <servers>\n"
            "      <address>0.gentoo.pool.ntp.org</address>\n"
            "      <weight>2</weight>\n"
            "    </servers>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>7</_id>\n"
            "    <servers>\n"
            "      <address>1.gentoo.pool.ntp.org</address>\n"
            "    </servers>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>8</_id>\n"
            "    <servers>\n"
            "      <address>2.gentoo.pool.ntp.org</address>\n"
            "      <weight>5</weight>\n"
            "    </servers>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>9</_id>\n"
            "    <servers>\n"
            "      <address>3.gentoo.pool.ntp.org</address>\n"
            "    </servers>\n"
            "  </config-entries>\n"
            "</" AUG_TEST_MODULE ">\n");
    free(str);
}

static void
test_store_add(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *node, *new;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* add some lists */
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='1']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='10']/listen-on/address",
            "2001::fe25:1", 0, &new));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, new));

    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='5']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='11']/sensor/device", "nmea1", 0, &new));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, new));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "2a3\n"
            "> listen on 2001::fe25:1\n"
            "15a17\n"
            "> sensor nmea1\n"));
}

static void
test_store_modify(void **state)
{
    struct tstate *st = (struct tstate *)*state;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* modify some data */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='6']/servers/address",
            "0.local.localhost.com", LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='4']/sensor/correction", "5000",
            LYD_NEW_PATH_UPDATE, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "12c12\n"
            "< sensor nmea0 correction 5 stratum 2\n"
            "---\n"
            "> sensor nmea0 correction 5000 stratum 2\n"
            "18c18\n"
            "< servers 0.gentoo.pool.ntp.org weight 2\n"
            "---\n"
            "> servers 0.local.localhost.com weight 2\n"));
}

static void
test_store_remove(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *node;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* remove data */
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='4']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='7']", 0, &node));
    lyd_free_tree(node);

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "12d11\n"
            "< sensor nmea0 correction 5 stratum 2\n"
            "19d17\n"
            "< servers 1.gentoo.pool.ntp.org\n"));
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
