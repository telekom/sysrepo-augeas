/**
 * @file test_resolv.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief resolv SR DS plugin test
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
#define AUG_TEST_INPUT_FILES AUG_CONFIG_FILES_DIR "/resolv"
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

#define AUG_TEST_MODULE "resolv"

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
            "    <nameserver>192.168.0.3</nameserver>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>2</_id>\n"
            "    <nameserver>ff02::1</nameserver>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>3</_id>\n"
            "    <domain>mynet.com</domain>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>4</_id>\n"
            "    <search>\n"
            "      <domain>mynet.com</domain>\n"
            "      <domain>anotherorg.net</domain>\n"
            "    </search>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>5</_id>\n"
            "    <sortlist>\n"
            "      <ipaddr-list>\n"
            "        <_id>1</_id>\n"
            "        <ipaddr>\n"
            "          <ip>130.155.160.0</ip>\n"
            "          <netmask>255.255.240.0</netmask>\n"
            "        </ipaddr>\n"
            "      </ipaddr-list>\n"
            "      <ipaddr-list>\n"
            "        <_id>2</_id>\n"
            "        <ipaddr>\n"
            "          <ip>130.155.0.0</ip>\n"
            "        </ipaddr>\n"
            "      </ipaddr-list>\n"
            "    </sortlist>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>6</_id>\n"
            "    <options>\n"
            "      <config-entries>\n"
            "        <_id>1</_id>\n"
            "        <ndots>3</ndots>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>2</_id>\n"
            "        <debug/>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>3</_id>\n"
            "        <timeout>2</timeout>\n"
            "      </config-entries>\n"
            "    </options>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>7</_id>\n"
            "    <options>\n"
            "      <config-entries>\n"
            "        <_id>1</_id>\n"
            "        <ip6-dotint>\n"
            "          <negate/>\n"
            "        </ip6-dotint>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>2</_id>\n"
            "        <single-request-reopen/>\n"
            "      </config-entries>\n"
            "    </options>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>8</_id>\n"
            "    <options>\n"
            "      <config-entries>\n"
            "        <_id>1</_id>\n"
            "        <edns0/>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>2</_id>\n"
            "        <trust-ad/>\n"
            "      </config-entries>\n"
            "    </options>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>9</_id>\n"
            "    <lookup>\n"
            "      <config-entries>\n"
            "        <_id>1</_id>\n"
            "        <file/>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>2</_id>\n"
            "        <bind/>\n"
            "      </config-entries>\n"
            "    </lookup>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>10</_id>\n"
            "    <family>\n"
            "      <config-entries>\n"
            "        <_id>1</_id>\n"
            "        <inet6/>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>2</_id>\n"
            "        <inet4/>\n"
            "      </config-entries>\n"
            "    </family>\n"
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
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='9']/lookup/config-entries[_id='3']/yp",
            NULL, 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='9']/lookup/config-entries[_id='1']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_before(node, entries));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='11']/options/config-entries[_id='1']/use-vc",
            NULL, 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='11']/options/config-entries[_id='2']/attempts",
            "255", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='8']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='5']/sortlist/ipaddr-list[_id='3']/"
            "ipaddr/ip", "127.0.0.1", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='5']/sortlist/ipaddr-list[_id='3']/"
            "ipaddr/netmask", "255.255.255.255", 0, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "9c9\n"
            "< sortlist 130.155.160.0/255.255.240.0 130.155.0.0\n"
            "---\n"
            "> sortlist 130.155.160.0/255.255.240.0 130.155.0.0 127.0.0.1/255.255.255.255\n"
            "13a14\n"
            "> options use-vc attempts:255\n"
            "15c16\n"
            "< lookup file bind\n"
            "---\n"
            "> lookup yp file bind\n"));
}

static void
test_store_modify(void **state)
{
    struct tstate *st = (struct tstate *)*state;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* modify some values */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='3']/domain", "yournet.com",
            LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='5']/sortlist/ipaddr-list[_id='2']/"
            "ipaddr/ip", "130.155.100.0", LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='6']/options/config-entries[_id='3']/"
            "timeout", "5", LYD_NEW_PATH_UPDATE, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "5c5\n"
            "< domain mynet.com  # and EOL comments\n"
            "---\n"
            "> domain yournet.com  # and EOL comments\n"
            "9c9\n"
            "< sortlist 130.155.160.0/255.255.240.0 130.155.0.0\n"
            "---\n"
            "> sortlist 130.155.160.0/255.255.240.0 130.155.100.0\n"
            "11c11\n"
            "< options ndots:3 debug timeout:2\n"
            "---\n"
            "> options ndots:3 debug timeout:5\n"));
}

static void
test_store_remove(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *node;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* remove list values */
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='4']/search/domain[.='mynet.com']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='8']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='7']/options/config-entries[_id='1']/"
            "ip6-dotint/negate", 0, &node));
    lyd_free_tree(node);

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "6c6\n"
            "< search mynet.com anotherorg.net\n"
            "---\n"
            "> search anotherorg.net\n"
            "12,13c12\n"
            "< options no-ip6-dotint single-request-reopen # and EOL comments\n"
            "< options edns0 trust-ad\n"
            "---\n"
            "> options ip6-dotint single-request-reopen # and EOL comments\n"));
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
