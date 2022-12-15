/**
 * @file test_iptables.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief iptables SR DS plugin test
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
#define AUG_TEST_INPUT_FILES AUG_CONFIG_FILES_DIR "/iptables"
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

#define AUG_TEST_MODULE "iptables"

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
            "  <table-list>\n"
            "    <_id>1</_id>\n"
            "    <table>\n"
            "      <value>filter</value>\n"
            "      <config-entries>\n"
            "        <_id>1</_id>\n"
            "        <chain>\n"
            "          <chain-name>INPUT</chain-name>\n"
            "          <policy>DROP</policy>\n"
            "        </chain>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>2</_id>\n"
            "        <chain>\n"
            "          <chain-name>FORWARD</chain-name>\n"
            "          <policy>DROP</policy>\n"
            "        </chain>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>3</_id>\n"
            "        <chain>\n"
            "          <chain-name>OUTPUT</chain-name>\n"
            "          <policy>DROP</policy>\n"
            "        </chain>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>4</_id>\n"
            "        <append>\n"
            "          <chain-name>INPUT</chain-name>\n"
            "          <ipt-match>\n"
            "            <_id>1</_id>\n"
            "            <match>state</match>\n"
            "          </ipt-match>\n"
            "          <ipt-match>\n"
            "            <_id>2</_id>\n"
            "            <node>\n"
            "              <label>state</label>\n"
            "              <value>RELATED,ESTABLISHED</value>\n"
            "            </node>\n"
            "          </ipt-match>\n"
            "          <ipt-match>\n"
            "            <_id>3</_id>\n"
            "            <jump>ACCEPT</jump>\n"
            "          </ipt-match>\n"
            "        </append>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>5</_id>\n"
            "        <insert>\n"
            "          <chain-name>FORWARD</chain-name>\n"
            "          <ipt-match>\n"
            "            <_id>1</_id>\n"
            "            <in-interface>\n"
            "              <value>eth0</value>\n"
            "            </in-interface>\n"
            "          </ipt-match>\n"
            "          <ipt-match>\n"
            "            <_id>2</_id>\n"
            "            <match>state</match>\n"
            "          </ipt-match>\n"
            "          <ipt-match>\n"
            "            <_id>3</_id>\n"
            "            <node>\n"
            "              <label>state</label>\n"
            "              <value>RELATED,ESTABLISHED</value>\n"
            "            </node>\n"
            "          </ipt-match>\n"
            "          <ipt-match>\n"
            "            <_id>4</_id>\n"
            "            <jump>ACCEPT</jump>\n"
            "          </ipt-match>\n"
            "        </insert>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>6</_id>\n"
            "        <append>\n"
            "          <chain-name>FORWARD</chain-name>\n"
            "          <ipt-match>\n"
            "            <_id>1</_id>\n"
            "            <in-interface>\n"
            "              <value>eth1</value>\n"
            "            </in-interface>\n"
            "          </ipt-match>\n"
            "          <ipt-match>\n"
            "            <_id>2</_id>\n"
            "            <match>state</match>\n"
            "          </ipt-match>\n"
            "          <ipt-match>\n"
            "            <_id>3</_id>\n"
            "            <node>\n"
            "              <label>state</label>\n"
            "              <value>NEW,RELATED,ESTABLISHED</value>\n"
            "            </node>\n"
            "          </ipt-match>\n"
            "          <ipt-match>\n"
            "            <_id>4</_id>\n"
            "            <jump>ACCEPT</jump>\n"
            "          </ipt-match>\n"
            "        </append>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>7</_id>\n"
            "        <append>\n"
            "          <chain-name>OUTPUT</chain-name>\n"
            "          <ipt-match>\n"
            "            <_id>1</_id>\n"
            "            <match>state</match>\n"
            "          </ipt-match>\n"
            "          <ipt-match>\n"
            "            <_id>2</_id>\n"
            "            <node>\n"
            "              <label>state</label>\n"
            "              <value>NEW,RELATED,ESTABLISHED</value>\n"
            "            </node>\n"
            "          </ipt-match>\n"
            "          <ipt-match>\n"
            "            <_id>3</_id>\n"
            "            <jump>ACCEPT</jump>\n"
            "          </ipt-match>\n"
            "        </append>\n"
            "      </config-entries>\n"
            "    </table>\n"
            "  </table-list>\n"
            "  <table-list>\n"
            "    <_id>2</_id>\n"
            "    <table>\n"
            "      <value>mangle</value>\n"
            "      <config-entries>\n"
            "        <_id>1</_id>\n"
            "        <chain>\n"
            "          <chain-name>PREROUTING</chain-name>\n"
            "          <policy>ACCEPT</policy>\n"
            "        </chain>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>2</_id>\n"
            "        <chain>\n"
            "          <chain-name>INPUT</chain-name>\n"
            "          <policy>ACCEPT</policy>\n"
            "        </chain>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>3</_id>\n"
            "        <chain>\n"
            "          <chain-name>FORWARD</chain-name>\n"
            "          <policy>ACCEPT</policy>\n"
            "        </chain>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>4</_id>\n"
            "        <chain>\n"
            "          <chain-name>OUTPUT</chain-name>\n"
            "          <policy>ACCEPT</policy>\n"
            "        </chain>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>5</_id>\n"
            "        <chain>\n"
            "          <chain-name>POSTROUTING</chain-name>\n"
            "          <policy>ACCEPT</policy>\n"
            "        </chain>\n"
            "      </config-entries>\n"
            "    </table>\n"
            "  </table-list>\n"
            "  <table-list>\n"
            "    <_id>3</_id>\n"
            "    <table>\n"
            "      <value>nat</value>\n"
            "      <config-entries>\n"
            "        <_id>1</_id>\n"
            "        <chain>\n"
            "          <chain-name>PREROUTING</chain-name>\n"
            "          <policy>ACCEPT</policy>\n"
            "        </chain>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>2</_id>\n"
            "        <chain>\n"
            "          <chain-name>POSTROUTING</chain-name>\n"
            "          <policy>ACCEPT</policy>\n"
            "        </chain>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>3</_id>\n"
            "        <chain>\n"
            "          <chain-name>OUTPUT</chain-name>\n"
            "          <policy>ACCEPT</policy>\n"
            "        </chain>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>4</_id>\n"
            "        <insert>\n"
            "          <chain-name>POSTROUTING</chain-name>\n"
            "          <ipt-match>\n"
            "            <_id>1</_id>\n"
            "            <out-interface>\n"
            "              <value>eth0</value>\n"
            "            </out-interface>\n"
            "          </ipt-match>\n"
            "          <ipt-match>\n"
            "            <_id>2</_id>\n"
            "            <jump>SNAT</jump>\n"
            "          </ipt-match>\n"
            "          <ipt-match>\n"
            "            <_id>3</_id>\n"
            "            <node>\n"
            "              <label>to-source</label>\n"
            "              <value>195.233.192.1</value>\n"
            "            </node>\n"
            "          </ipt-match>\n"
            "        </insert>\n"
            "      </config-entries>\n"
            "    </table>\n"
            "  </table-list>\n"
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
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "table-list[_id='1']/table/config-entries[_id='4']/"
            "append/ipt-match[_id='4']/tcp-flags/mask", "ALL", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "table-list[_id='1']/table/config-entries[_id='4']/"
            "append/ipt-match[_id='4']/tcp-flags/set", "FIN", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "table-list[_id='1']/table/config-entries[_id='4']/"
            "append/ipt-match[_id='4']/tcp-flags/set", "PSH", 0, NULL));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "table-list[_id='4']/table/value", "mytable", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "table-list[_id='4']/table/config-entries[_id='1']/"
            "chain/chain-name", "chain1", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "table-list[_id='4']/table/config-entries[_id='1']/"
            "chain/policy", "REJECT", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "table-list[_id='1']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "table-list[_id='3']/table/config-entries[_id='4']/"
            "insert/ipt-match[_id='4']/out-interface/value", "eth25", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "table-list[_id='3']/table/config-entries[_id='4']/"
            "insert/ipt-match[_id='4']/out-interface/not", NULL, 0, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "6c6\n"
            "< -A INPUT -m state --state RELATED,ESTABLISHED -j ACCEPT\n"
            "---\n"
            "> -A INPUT -m state --state RELATED,ESTABLISHED -j ACCEPT --tcp-flags ALL FIN,PSH\n"
            "14a15,17\n"
            "> *mytable\n"
            "> :chain1 REJECT [658:32445]\n"
            "> COMMIT\n"
            "18c21\n"
            "< :PREROUTING ACCEPT [658:32445]\n"
            "---\n"
            "> :PREROUTING ACCEPT [1:229]\n"
            "20,23c23,26\n"
            "< :INPUT ACCEPT [658:32445]\n"
            "< :FORWARD ACCEPT [0:0]\n"
            "< :OUTPUT ACCEPT [891:68234]\n"
            "< :POSTROUTING ACCEPT [891:68234]\n"
            "---\n"
            "> :INPUT ACCEPT [3:450]\n"
            "> :FORWARD ACCEPT [3:450]\n"
            "> :OUTPUT ACCEPT\n"
            "> :POSTROUTING ACCEPT\n"
            "28,29c31,32\n"
            "< :PREROUTING ACCEPT [1:229]\n"
            "< :POSTROUTING ACCEPT [3:450]\n"
            "---\n"
            "> :PREROUTING ACCEPT\n"
            "> :POSTROUTING ACCEPT\n"
            "31c34\n"
            "< :OUTPUT ACCEPT [3:450]\n"
            "---\n"
            "> :OUTPUT ACCEPT\n"
            "33c36\n"
            "< --insert POSTROUTING -o eth0 -j SNAT --to-source 195.233.192.1\n"
            "---\n"
            "> -I POSTROUTING -o eth0 -j SNAT --to-source 195.233.192.1 ! -o eth25\n"));
}

static void
test_store_modify(void **state)
{
    struct tstate *st = (struct tstate *)*state;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* modify some values */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "table-list[_id='1']/table/config-entries[_id='4']/"
            "append/ipt-match[_id='2']/node/value", "ESTABLISHED", LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "table-list[_id='3']/table/config-entries[_id='4']/"
            "insert/ipt-match[_id='1']/out-interface/not", NULL, 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "table-list[_id='3']/table/config-entries[_id='1']/"
            "chain/policy", "-", LYD_NEW_PATH_UPDATE, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "6c6\n"
            "< -A INPUT -m state --state RELATED,ESTABLISHED -j ACCEPT\n"
            "---\n"
            "> -A INPUT -m state --state ESTABLISHED -j ACCEPT\n"
            "28c28\n"
            "< :PREROUTING ACCEPT [1:229]\n"
            "---\n"
            "> :PREROUTING - [1:229]\n"
            "33c33\n"
            "< --insert POSTROUTING -o eth0 -j SNAT --to-source 195.233.192.1\n"
            "---\n"
            "> --insert POSTROUTING ! -o eth0 -j SNAT --to-source 195.233.192.1\n"));
}

static void
test_store_remove(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *node;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* remove list values */
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "table-list[_id='1']/table/config-entries[_id='4']/"
            "append/ipt-match[_id='2']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "table-list[_id='2']/table/config-entries[_id='3']", 0, &node));
    lyd_free_tree(node);

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "6c6\n"
            "< -A INPUT -m state --state RELATED,ESTABLISHED -j ACCEPT\n"
            "---\n"
            "> -A INPUT -m state -j ACCEPT\n"
            "21,22c21\n"
            "< :FORWARD ACCEPT [0:0]\n"
            "< :OUTPUT ACCEPT [891:68234]\n"
            "---\n"
            "> :OUTPUT ACCEPT [0:0]\n"));
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
