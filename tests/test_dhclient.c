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

#define AUG_TEST_MODULE "dhclient"

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
            "    <timeout>3</timeout>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>2</_id>\n"
            "    <retry>10</retry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>3</_id>\n"
            "    <request>\n"
            "      <config-entries>\n"
            "        <_id>1</_id>\n"
            "        <stmt_array>\n"
            "          <stmt_array>1</stmt_array>\n"
            "          <sto_to_spc>subnet-mask</sto_to_spc>\n"
            "        </stmt_array>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>2</_id>\n"
            "        <stmt_array>\n"
            "          <stmt_array>2</stmt_array>\n"
            "          <sto_to_spc>broadcast-address</sto_to_spc>\n"
            "        </stmt_array>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>3</_id>\n"
            "        <stmt_array>\n"
            "          <stmt_array>3</stmt_array>\n"
            "          <sto_to_spc>ntp-servers</sto_to_spc>\n"
            "        </stmt_array>\n"
            "      </config-entries>\n"
            "    </request>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>4</_id>\n"
            "    <send>\n"
            "      <word>\n"
            "        <word>fqdn.fqdn</word>\n"
            "        <sto_to_spc_noeval>\"grosse.fugue.com.\"</sto_to_spc_noeval>\n"
            "      </word>\n"
            "    </send>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>5</_id>\n"
            "    <option>\n"
            "      <word>\n"
            "        <word>rfc3442-classless-static-routes</word>\n"
            "        <code>121</code>\n"
            "        <value>array of unsigned integer 8</value>\n"
            "      </word>\n"
            "    </option>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>6</_id>\n"
            "    <append>\n"
            "      <word>\n"
            "        <word>domain-name-servers</word>\n"
            "        <sto_to_spc_noeval>127.0.0.1</sto_to_spc_noeval>\n"
            "      </word>\n"
            "    </append>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>7</_id>\n"
            "    <send>\n"
            "      <word>\n"
            "        <word>dhcp-client-identifier</word>\n"
            "        <eval>hardware</eval>\n"
            "      </word>\n"
            "    </send>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>8</_id>\n"
            "    <interface>\n"
            "      <sto_to_spc>ep0</sto_to_spc>\n"
            "      <config-entries>\n"
            "        <_id>1</_id>\n"
            "        <script>/sbin/dhclient-script</script>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>2</_id>\n"
            "        <send>\n"
            "          <word>\n"
            "            <word>dhcp-client-identifier</word>\n"
            "            <sto_to_spc_noeval>1:0:a0:24:ab:fb:9c</sto_to_spc_noeval>\n"
            "          </word>\n"
            "        </send>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>3</_id>\n"
            "        <send>\n"
            "          <word>\n"
            "            <word>dhcp-lease-time</word>\n"
            "            <sto_to_spc_noeval>3600</sto_to_spc_noeval>\n"
            "          </word>\n"
            "        </send>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>4</_id>\n"
            "        <request>\n"
            "          <config-entries>\n"
            "            <_id>1</_id>\n"
            "            <stmt_array>\n"
            "              <stmt_array>1</stmt_array>\n"
            "              <sto_to_spc>subnet-mask</sto_to_spc>\n"
            "            </stmt_array>\n"
            "          </config-entries>\n"
            "          <config-entries>\n"
            "            <_id>2</_id>\n"
            "            <stmt_array>\n"
            "              <stmt_array>2</stmt_array>\n"
            "              <sto_to_spc>broadcast-address</sto_to_spc>\n"
            "            </stmt_array>\n"
            "          </config-entries>\n"
            "          <config-entries>\n"
            "            <_id>3</_id>\n"
            "            <stmt_array>\n"
            "              <stmt_array>3</stmt_array>\n"
            "              <sto_to_spc>time-offset</sto_to_spc>\n"
            "            </stmt_array>\n"
            "          </config-entries>\n"
            "          <config-entries>\n"
            "            <_id>4</_id>\n"
            "            <stmt_array>\n"
            "              <stmt_array>4</stmt_array>\n"
            "              <sto_to_spc>routers</sto_to_spc>\n"
            "            </stmt_array>\n"
            "          </config-entries>\n"
            "          <config-entries>\n"
            "            <_id>5</_id>\n"
            "            <stmt_array>\n"
            "              <stmt_array>5</stmt_array>\n"
            "              <sto_to_spc>domain-name</sto_to_spc>\n"
            "            </stmt_array>\n"
            "          </config-entries>\n"
            "          <config-entries>\n"
            "            <_id>6</_id>\n"
            "            <stmt_array>\n"
            "              <stmt_array>6</stmt_array>\n"
            "              <sto_to_spc>domain-name-servers</sto_to_spc>\n"
            "            </stmt_array>\n"
            "          </config-entries>\n"
            "          <config-entries>\n"
            "            <_id>7</_id>\n"
            "            <stmt_array>\n"
            "              <stmt_array>7</stmt_array>\n"
            "              <sto_to_spc>host-name</sto_to_spc>\n"
            "            </stmt_array>\n"
            "          </config-entries>\n"
            "        </request>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>5</_id>\n"
            "        <media>\n"
            "          <config-entries>\n"
            "            <_id>1</_id>\n"
            "            <stmt_array>\n"
            "              <stmt_array>1</stmt_array>\n"
            "              <sto_to_spc>media10baseT/UTP</sto_to_spc>\n"
            "            </stmt_array>\n"
            "          </config-entries>\n"
            "          <config-entries>\n"
            "            <_id>2</_id>\n"
            "            <stmt_array>\n"
            "              <stmt_array>2</stmt_array>\n"
            "              <sto_to_spc>\"media10base2/BNC\"</sto_to_spc>\n"
            "            </stmt_array>\n"
            "          </config-entries>\n"
            "        </media>\n"
            "      </config-entries>\n"
            "    </interface>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>9</_id>\n"
            "    <alias>\n"
            "      <config-entries>\n"
            "        <_id>1</_id>\n"
            "        <interface>\"ep0\"</interface>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>2</_id>\n"
            "        <fixed-address>192.5.5.213</fixed-address>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>3</_id>\n"
            "        <option>\n"
            "          <word>\n"
            "            <word>subnet-mask</word>\n"
            "            <sto_to_spc_noeval>255.255.255.255</sto_to_spc_noeval>\n"
            "          </word>\n"
            "        </option>\n"
            "      </config-entries>\n"
            "    </alias>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>10</_id>\n"
            "    <lease>\n"
            "      <config-entries>\n"
            "        <_id>1</_id>\n"
            "        <interface>\"eth0\"</interface>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>2</_id>\n"
            "        <fixed-address>192.33.137.200</fixed-address>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>3</_id>\n"
            "        <medium>\"link0 link1\"</medium>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>4</_id>\n"
            "        <vendor_option_space>\"name\"</vendor_option_space>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>5</_id>\n"
            "        <option>\n"
            "          <word>\n"
            "            <word>host-name</word>\n"
            "            <sto_to_spc_noeval>\"andare.swiftmedia.com\"</sto_to_spc_noeval>\n"
            "          </word>\n"
            "        </option>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>6</_id>\n"
            "        <option>\n"
            "          <word>\n"
            "            <word>subnet-mask</word>\n"
            "            <sto_to_spc_noeval>255.255.255.0</sto_to_spc_noeval>\n"
            "          </word>\n"
            "        </option>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>7</_id>\n"
            "        <option>\n"
            "          <word>\n"
            "            <word>broadcast-address</word>\n"
            "            <sto_to_spc_noeval>192.33.137.255</sto_to_spc_noeval>\n"
            "          </word>\n"
            "        </option>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>8</_id>\n"
            "        <option>\n"
            "          <word>\n"
            "            <word>routers</word>\n"
            "            <sto_to_spc_noeval>192.33.137.250</sto_to_spc_noeval>\n"
            "          </word>\n"
            "        </option>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>9</_id>\n"
            "        <option>\n"
            "          <word>\n"
            "            <word>domain-name-servers</word>\n"
            "            <sto_to_spc_noeval>127.0.0.1</sto_to_spc_noeval>\n"
            "          </word>\n"
            "        </option>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>10</_id>\n"
            "        <renew>\n"
            "          <weekday>2</weekday>\n"
            "          <year>2000</year>\n"
            "          <month>1</month>\n"
            "          <day>12</day>\n"
            "          <hour>00</hour>\n"
            "          <minute>00</minute>\n"
            "          <second>01</second>\n"
            "        </renew>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>11</_id>\n"
            "        <rebind>\n"
            "          <weekday>2</weekday>\n"
            "          <year>2000</year>\n"
            "          <month>1</month>\n"
            "          <day>12</day>\n"
            "          <hour>00</hour>\n"
            "          <minute>00</minute>\n"
            "          <second>01</second>\n"
            "        </rebind>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>12</_id>\n"
            "        <expire>\n"
            "          <weekday>2</weekday>\n"
            "          <year>2000</year>\n"
            "          <month>1</month>\n"
            "          <day>12</day>\n"
            "          <hour>00</hour>\n"
            "          <minute>00</minute>\n"
            "          <second>01</second>\n"
            "        </expire>\n"
            "      </config-entries>\n"
            "    </lease>\n"
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
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='11']/send/word/word",
            "dhcp-lease-time", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='11']/send/word/sto_to_spc_noeval",
            "1800", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='7']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='12']/supersede/word/word",
            "something", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='12']/supersede/word/sto_to_spc_noeval",
            "extra", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='6']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='9']/alias/config-entries[_id='4']/filename",
            "my_file", 0, &entries));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "18a19\n"
            "> supersede something extra;\n"
            "19a21\n"
            "> send dhcp-lease-time 1800;\n"
            "33a36\n"
            ">  filename my_file;"));
}

static void
test_store_modify(void **state)
{
    struct tstate *st = (struct tstate *)*state;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* modify some values */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='3']/request/config-entries[_id='1']/"
            "stmt_array/sto_to_spc", "subnet", LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='8']/interface/config-entries[_id='4']/"
            "request/config-entries[_id='1']/stmt_array/sto_to_spc", "subnet", LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='10']/lease/config-entries[_id='12']/"
            "expire/month", "6", LYD_NEW_PATH_UPDATE, NULL));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='4']/send/word/word", "fqdn.qdn",
            LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='4']/send/word/sto_to_spc_noeval",
            "\"grosse.fuge.com.\"", LYD_NEW_PATH_UPDATE, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "8c8\n"
            "< \tsubnet-mask,\n"
            "---\n"
            "> \tsubnet,\n"
            "13,14c13\n"
            "< \tfqdn.fqdn\n"
            "< \t  \"grosse.fugue.com.\";\n"
            "---\n"
            "> \tfqdn.qdn \"grosse.fuge.com.\";\n"
            "25c24\n"
            "<    request subnet-mask, broadcast-address, time-offset, routers,\n"
            "---\n"
            ">    request subnet, broadcast-address, time-offset, routers,\n"
            "48c47\n"
            "<   expire 2 2000/1/12 00:00:01;\n"
            "---\n"
            ">   expire 2 2000/6/12 00:00:01;"));
}

static void
test_store_remove(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *node;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* remove list values */
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='10']/lease/config-entries[_id='11']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='8']/interface/config-entries[_id='4']/"
            "request/config-entries[_id='4']", 0, &node));
    lyd_free_tree(node);

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "25c25\n"
            "<    request subnet-mask, broadcast-address, time-offset, routers,\n"
            "---\n"
            ">    request subnet-mask, broadcast-address, time-offset,\n"
            "47d46\n"
            "<   rebind 2 2000/1/12 00:00:01;"));
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
