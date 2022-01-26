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
    return tsetup_glob(state, "ntpd", &srpds__, AUG_TEST_INPUT_FILES);
}

static void
test_load(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    char *str;

    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));
    lyd_print_mem(&str, st->data, LYD_XML, LYD_PRINT_WITHSIBLINGS);

    assert_string_equal(str,
            "<ntpd xmlns=\"aug:ntpd\">\n"
            "  <config-file>" AUG_CONFIG_FILES_DIR "/ntpd</config-file>\n"
            "  <listen_on>\n"
            "    <_id>1</_id>\n"
            "    <address>*</address>\n"
            "    <rtable>5</rtable>\n"
            "  </listen_on>\n"
            "  <server>\n"
            "    <_id>1</_id>\n"
            "    <address>ntp.example.org</address>\n"
            "  </server>\n"
            "  <servers>\n"
            "    <_id>1</_id>\n"
            "    <address>pool.ntp.org</address>\n"
            "  </servers>\n"
            "  <servers>\n"
            "    <_id>2</_id>\n"
            "    <address>0.gentoo.pool.ntp.org</address>\n"
            "    <weight>2</weight>\n"
            "  </servers>\n"
            "  <servers>\n"
            "    <_id>3</_id>\n"
            "    <address>1.gentoo.pool.ntp.org</address>\n"
            "  </servers>\n"
            "  <servers>\n"
            "    <_id>4</_id>\n"
            "    <address>2.gentoo.pool.ntp.org</address>\n"
            "    <weight>5</weight>\n"
            "  </servers>\n"
            "  <servers>\n"
            "    <_id>5</_id>\n"
            "    <address>3.gentoo.pool.ntp.org</address>\n"
            "  </servers>\n"
            "  <sensor>\n"
            "    <_id>1</_id>\n"
            "    <device>nmea0</device>\n"
            "    <correction>5</correction>\n"
            "    <stratum>2</stratum>\n"
            "  </sensor>\n"
            "  <sensor>\n"
            "    <_id>2</_id>\n"
            "    <device>*</device>\n"
            "    <refid>GPS</refid>\n"
            "  </sensor>\n"
            "</ntpd>\n");
    free(str);
}

static void
test_store_add(void **state)
{
    struct tstate *st = (struct tstate *)*state;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* add some lists */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "listen_on[_id='2']/address", "2001::fe25:1", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "sensor[_id='3']/device", "nmea1", 0, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "21a22,23\n"
            "> listen on 2001::fe25:1\n"
            "> sensor nmea1"));
}

static void
test_store_modify(void **state)
{
    struct tstate *st = (struct tstate *)*state;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* modify some data */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "servers[_id='2']/address", "0.local.localhost.com",
            LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "sensor[_id='1']/correction", "5000",
            LYD_NEW_PATH_UPDATE, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "12c12\n"
            "< sensor nmea0 correction 5 stratum 2\n"
            "---\n"
            "> sensor nmea0 correction 5000 stratum 2\n"
            "18c18\n"
            "< servers 0.gentoo.pool.ntp.org weight 2\n"
            "---\n"
            "> servers 0.local.localhost.com weight 2"));
}

static void
test_store_remove(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *node;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* remove data */
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "servers[_id='3']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "sensor[_id='1']", 0, &node));
    lyd_free_tree(node);

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "12d11\n"
            "< sensor nmea0 correction 5 stratum 2\n"
            "19d17\n"
            "< servers 1.gentoo.pool.ntp.org"));
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
