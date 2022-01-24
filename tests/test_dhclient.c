/**
 * @file test_dhclient.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief dhclient SR DS plugin test
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
#define AUG_TEST_INPUT_FILES AUG_CONFIG_FILES_DIR "/dhclient"
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
    return tsetup_glob(state, "dhclient", &srpds__, AUG_TEST_INPUT_FILES);
}

static void
test_load(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    char *str;

    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));
    lyd_print_mem(&str, st->data, LYD_XML, LYD_PRINT_WITHSIBLINGS);

    assert_string_equal(str,
            "<dhclient xmlns=\"aug:dhclient\">\n"
            "  <config-file>" AUG_CONFIG_FILES_DIR "/dhclient</config-file>\n"
            "  <stmt_simple>\n"
            "    <stmt_simple_re>timeout</stmt_simple_re>\n"
            "    <sto_to_spc>3</sto_to_spc>\n"
            "  </stmt_simple>\n"
            "  <stmt_simple>\n"
            "    <stmt_simple_re>retry</stmt_simple_re>\n"
            "    <sto_to_spc>10</sto_to_spc>\n"
            "  </stmt_simple>\n"
            "  <stmt_opt_mod>\n"
            "    <stmt_opt_mod_re>append</stmt_opt_mod_re>\n"
            "    <word>\n"
            "      <word>domain-name-servers</word>\n"
            "      <sto_to_spc_noeval>127.0.0.1</sto_to_spc_noeval>\n"
            "    </word>\n"
            "  </stmt_opt_mod>\n"
            "  <stmt_array>\n"
            "    <stmt_array_re>request</stmt_array_re>\n"
            "    <stmt_array>\n"
            "      <_id>1</_id>\n"
            "      <sto_to_spc>subnet-mask</sto_to_spc>\n"
            "    </stmt_array>\n"
            "    <stmt_array>\n"
            "      <_id>2</_id>\n"
            "      <sto_to_spc>broadcast-address</sto_to_spc>\n"
            "    </stmt_array>\n"
            "    <stmt_array>\n"
            "      <_id>3</_id>\n"
            "      <sto_to_spc>ntp-servers</sto_to_spc>\n"
            "    </stmt_array>\n"
            "  </stmt_array>\n"
            "  <stmt_hash>\n"
            "    <stmt_hash_re>send</stmt_hash_re>\n"
            "    <word>\n"
            "      <word>fqdn.fqdn</word>\n"
            "      <sto_to_spc_noeval>\"grosse.fugue.com.\"</sto_to_spc_noeval>\n"
            "    </word>\n"
            "  </stmt_hash>\n"
            "  <stmt_hash>\n"
            "    <stmt_hash_re>option</stmt_hash_re>\n"
            "    <word>\n"
            "      <word>rfc3442-classless-static-routes</word>\n"
            "      <value>\n"
            "        <value>\n"
            "          <code>121</code>\n"
            "          <value>array of unsigned integer 8</value>\n"
            "        </value>\n"
            "      </value>\n"
            "    </word>\n"
            "  </stmt_hash>\n"
            "  <stmt_hash>\n"
            "    <stmt_hash_re>send</stmt_hash_re>\n"
            "    <word>\n"
            "      <word>dhcp-client-identifier</word>\n"
            "      <value>\n"
            "        <eval>hardware</eval>\n"
            "      </value>\n"
            "    </word>\n"
            "  </stmt_hash>\n"
            "  <stmt_block>\n"
            "    <stmt_block_re>interface</stmt_block_re>\n"
            "    <sto_to_spc>ep0</sto_to_spc>\n"
            "    <stmt_array>\n"
            "      <stmt_array_re>request</stmt_array_re>\n"
            "      <stmt_array>\n"
            "        <_id>1</_id>\n"
            "        <sto_to_spc>subnet-mask</sto_to_spc>\n"
            "      </stmt_array>\n"
            "      <stmt_array>\n"
            "        <_id>2</_id>\n"
            "        <sto_to_spc>broadcast-address</sto_to_spc>\n"
            "      </stmt_array>\n"
            "      <stmt_array>\n"
            "        <_id>3</_id>\n"
            "        <sto_to_spc>time-offset</sto_to_spc>\n"
            "      </stmt_array>\n"
            "      <stmt_array>\n"
            "        <_id>4</_id>\n"
            "        <sto_to_spc>routers</sto_to_spc>\n"
            "      </stmt_array>\n"
            "      <stmt_array>\n"
            "        <_id>5</_id>\n"
            "        <sto_to_spc>domain-name</sto_to_spc>\n"
            "      </stmt_array>\n"
            "      <stmt_array>\n"
            "        <_id>6</_id>\n"
            "        <sto_to_spc>domain-name-servers</sto_to_spc>\n"
            "      </stmt_array>\n"
            "      <stmt_array>\n"
            "        <_id>7</_id>\n"
            "        <sto_to_spc>host-name</sto_to_spc>\n"
            "      </stmt_array>\n"
            "    </stmt_array>\n"
            "    <stmt_array>\n"
            "      <stmt_array_re>media</stmt_array_re>\n"
            "      <stmt_array>\n"
            "        <_id>1</_id>\n"
            "        <sto_to_spc>media10baseT/UTP</sto_to_spc>\n"
            "      </stmt_array>\n"
            "      <stmt_array>\n"
            "        <_id>2</_id>\n"
            "        <sto_to_spc>\"media10base2/BNC\"</sto_to_spc>\n"
            "      </stmt_array>\n"
            "    </stmt_array>\n"
            "    <stmt_hash>\n"
            "      <stmt_hash_re>send</stmt_hash_re>\n"
            "      <word>\n"
            "        <word>dhcp-client-identifier</word>\n"
            "        <sto_to_spc_noeval>1:0:a0:24:ab:fb:9c</sto_to_spc_noeval>\n"
            "      </word>\n"
            "    </stmt_hash>\n"
            "    <stmt_hash>\n"
            "      <stmt_hash_re>send</stmt_hash_re>\n"
            "      <word>\n"
            "        <word>dhcp-lease-time</word>\n"
            "        <sto_to_spc_noeval>3600</sto_to_spc_noeval>\n"
            "      </word>\n"
            "    </stmt_hash>\n"
            "    <stmt_block_opt>\n"
            "      <stmt_block_opt_re>script</stmt_block_opt_re>\n"
            "      <sto_to_spc>/sbin/dhclient-script</sto_to_spc>\n"
            "    </stmt_block_opt>\n"
            "  </stmt_block>\n"
            "  <stmt_block>\n"
            "    <stmt_block_re>alias</stmt_block_re>\n"
            "    <stmt_hash>\n"
            "      <stmt_hash_re>option</stmt_hash_re>\n"
            "      <word>\n"
            "        <word>subnet-mask</word>\n"
            "        <sto_to_spc_noeval>255.255.255.255</sto_to_spc_noeval>\n"
            "      </word>\n"
            "    </stmt_hash>\n"
            "    <stmt_block_opt>\n"
            "      <stmt_block_opt_re>interface</stmt_block_opt_re>\n"
            "      <sto_to_spc>\"ep0\"</sto_to_spc>\n"
            "    </stmt_block_opt>\n"
            "    <stmt_block_opt>\n"
            "      <stmt_block_opt_re>fixed-address</stmt_block_opt_re>\n"
            "      <sto_to_spc>192.5.5.213</sto_to_spc>\n"
            "    </stmt_block_opt>\n"
            "  </stmt_block>\n"
            "  <stmt_block>\n"
            "    <stmt_block_re>lease</stmt_block_re>\n"
            "    <stmt_hash>\n"
            "      <stmt_hash_re>option</stmt_hash_re>\n"
            "      <word>\n"
            "        <word>host-name</word>\n"
            "        <sto_to_spc_noeval>\"andare.swiftmedia.com\"</sto_to_spc_noeval>\n"
            "      </word>\n"
            "    </stmt_hash>\n"
            "    <stmt_hash>\n"
            "      <stmt_hash_re>option</stmt_hash_re>\n"
            "      <word>\n"
            "        <word>subnet-mask</word>\n"
            "        <sto_to_spc_noeval>255.255.255.0</sto_to_spc_noeval>\n"
            "      </word>\n"
            "    </stmt_hash>\n"
            "    <stmt_hash>\n"
            "      <stmt_hash_re>option</stmt_hash_re>\n"
            "      <word>\n"
            "        <word>broadcast-address</word>\n"
            "        <sto_to_spc_noeval>192.33.137.255</sto_to_spc_noeval>\n"
            "      </word>\n"
            "    </stmt_hash>\n"
            "    <stmt_hash>\n"
            "      <stmt_hash_re>option</stmt_hash_re>\n"
            "      <word>\n"
            "        <word>routers</word>\n"
            "        <sto_to_spc_noeval>192.33.137.250</sto_to_spc_noeval>\n"
            "      </word>\n"
            "    </stmt_hash>\n"
            "    <stmt_hash>\n"
            "      <stmt_hash_re>option</stmt_hash_re>\n"
            "      <word>\n"
            "        <word>domain-name-servers</word>\n"
            "        <sto_to_spc_noeval>127.0.0.1</sto_to_spc_noeval>\n"
            "      </word>\n"
            "    </stmt_hash>\n"
            "    <stmt_block_opt>\n"
            "      <stmt_block_opt_re>interface</stmt_block_opt_re>\n"
            "      <sto_to_spc>\"eth0\"</sto_to_spc>\n"
            "    </stmt_block_opt>\n"
            "    <stmt_block_opt>\n"
            "      <stmt_block_opt_re>fixed-address</stmt_block_opt_re>\n"
            "      <sto_to_spc>192.33.137.200</sto_to_spc>\n"
            "    </stmt_block_opt>\n"
            "    <stmt_block_opt>\n"
            "      <stmt_block_opt_re>medium</stmt_block_opt_re>\n"
            "      <sto_to_spc>\"link0 link1\"</sto_to_spc>\n"
            "    </stmt_block_opt>\n"
            "    <stmt_block_opt>\n"
            "      <stmt_block_opt_re>vendor option space</stmt_block_opt_re>\n"
            "      <sto_to_spc>\"name\"</sto_to_spc>\n"
            "    </stmt_block_opt>\n"
            "    <stmt_block_date>\n"
            "      <stmt_block_date_re>renew</stmt_block_date_re>\n"
            "      <weekday>2</weekday>\n"
            "      <year>2000</year>\n"
            "      <month>1</month>\n"
            "      <day>12</day>\n"
            "      <hour>00</hour>\n"
            "      <minute>00</minute>\n"
            "      <second>01</second>\n"
            "    </stmt_block_date>\n"
            "    <stmt_block_date>\n"
            "      <stmt_block_date_re>rebind</stmt_block_date_re>\n"
            "      <weekday>2</weekday>\n"
            "      <year>2000</year>\n"
            "      <month>1</month>\n"
            "      <day>12</day>\n"
            "      <hour>00</hour>\n"
            "      <minute>00</minute>\n"
            "      <second>01</second>\n"
            "    </stmt_block_date>\n"
            "    <stmt_block_date>\n"
            "      <stmt_block_date_re>expire</stmt_block_date_re>\n"
            "      <weekday>2</weekday>\n"
            "      <year>2000</year>\n"
            "      <month>1</month>\n"
            "      <day>12</day>\n"
            "      <hour>00</hour>\n"
            "      <minute>00</minute>\n"
            "      <second>01</second>\n"
            "    </stmt_block_date>\n"
            "  </stmt_block>\n"
            "</dhclient>\n");
    free(str);
}

static void
test_store_add(void **state)
{
    struct tstate *st = (struct tstate *)*state;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* add some new list instances */
    /*assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "stmt_opt_mod[stmt_opt_mod_re='supersede']/word/word",
            "whatever", 0, NULL));*/
    /*assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "stmt_opt_mod[stmt_opt_mod_re='supersede']/word/sto_to_spc_noeval",
            "whatever-val", 0, NULL));*/
    /*assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "stmt_block[stmt_block_re='alias']/stmt_block_opt[stmt_block_opt_re='server-name']/sto_to_spc", "my-server", 0, NULL));*/

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
