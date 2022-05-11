/**
 * @file test_up2date.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief up2date SR DS plugin test
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
#define AUG_TEST_INPUT_FILES AUG_CONFIG_FILES_DIR "/up2date"
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

#define AUG_TEST_MODULE "up2date"

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
            "  <entry-list>\n"
            "    <_id>1</_id>\n"
            "    <entry>\n"
            "      <entry>1</entry>\n"
            "      <key-re>debug[comment]</key-re>\n"
            "      <value>Whether or not debugging is enabled</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>2</_id>\n"
            "    <entry>\n"
            "      <entry>2</entry>\n"
            "      <key-re>debug</key-re>\n"
            "      <value>0</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>3</_id>\n"
            "    <entry>\n"
            "      <entry>3</entry>\n"
            "      <key-re>systemIdPath[comment]</key-re>\n"
            "      <value>Location of system id</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>4</_id>\n"
            "    <entry>\n"
            "      <entry>4</entry>\n"
            "      <key-re>systemIdPath</key-re>\n"
            "      <value>/etc/sysconfig/rhn/systemid</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>5</_id>\n"
            "    <entry>\n"
            "      <entry>5</entry>\n"
            "      <key-re>serverURL[comment]</key-re>\n"
            "      <value>Remote server URL (use FQDN)</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>6</_id>\n"
            "    <entry>\n"
            "      <entry>6</entry>\n"
            "      <key-re>serverURL</key-re>\n"
            "      <value>https://enter.your.server.url.here/XMLRPC</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>7</_id>\n"
            "    <entry>\n"
            "      <entry>7</entry>\n"
            "      <key-re>hostedWhitelist[comment]</key-re>\n"
            "      <value>RHN Hosted URL's</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>8</_id>\n"
            "    <entry>\n"
            "      <entry>8</entry>\n"
            "      <key-re>hostedWhitelist</key-re>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>9</_id>\n"
            "    <entry>\n"
            "      <entry>9</entry>\n"
            "      <key-re>enableProxy[comment]</key-re>\n"
            "      <value>Use a HTTP Proxy</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>10</_id>\n"
            "    <entry>\n"
            "      <entry>10</entry>\n"
            "      <key-re>enableProxy</key-re>\n"
            "      <value>0</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>11</_id>\n"
            "    <entry>\n"
            "      <entry>11</entry>\n"
            "      <key-re>versionOverride[comment]</key-re>\n"
            "      <value>Override the automatically determined system version</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>12</_id>\n"
            "    <entry>\n"
            "      <entry>12</entry>\n"
            "      <key-re>versionOverride</key-re>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>13</_id>\n"
            "    <entry>\n"
            "      <entry>13</entry>\n"
            "      <key-re>httpProxy[comment]</key-re>\n"
            "      <value>HTTP proxy in host:port format, e.g. squid.redhat.com:3128</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>14</_id>\n"
            "    <entry>\n"
            "      <entry>14</entry>\n"
            "      <key-re>httpProxy</key-re>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>15</_id>\n"
            "    <entry>\n"
            "      <entry>15</entry>\n"
            "      <key-re>noReboot[comment]</key-re>\n"
            "      <value>Disable the reboot actions</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>16</_id>\n"
            "    <entry>\n"
            "      <entry>16</entry>\n"
            "      <key-re>noReboot</key-re>\n"
            "      <value>0</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>17</_id>\n"
            "    <entry>\n"
            "      <entry>17</entry>\n"
            "      <key-re>networkRetries[comment]</key-re>\n"
            "      <value>Number of attempts to make at network connections before giving up</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>18</_id>\n"
            "    <entry>\n"
            "      <entry>18</entry>\n"
            "      <key-re>networkRetries</key-re>\n"
            "      <value>1</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>19</_id>\n"
            "    <entry>\n"
            "      <entry>19</entry>\n"
            "      <key-re>disallowConfChanges[comment]</key-re>\n"
            "      <value>Config options that can not be overwritten by a config update action</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>20</_id>\n"
            "    <entry>\n"
            "      <entry>20</entry>\n"
            "      <key-re>disallowConfChanges</key-re>\n"
            "      <multi-entry>\n"
            "        <multi-value-list>\n"
            "          <_id>1</_id>\n"
            "          <multi-value>\n"
            "            <multi>1</multi>\n"
            "            <value-re>noReboot</value-re>\n"
            "          </multi-value>\n"
            "        </multi-value-list>\n"
            "        <multi-value-list>\n"
            "          <_id>2</_id>\n"
            "          <multi-value>\n"
            "            <multi>2</multi>\n"
            "            <value-re>sslCACert</value-re>\n"
            "          </multi-value>\n"
            "        </multi-value-list>\n"
            "        <multi-value-list>\n"
            "          <_id>3</_id>\n"
            "          <multi-value>\n"
            "            <multi>3</multi>\n"
            "            <value-re>useNoSSLForPackages</value-re>\n"
            "          </multi-value>\n"
            "        </multi-value-list>\n"
            "        <multi-value-list>\n"
            "          <_id>4</_id>\n"
            "          <multi-value>\n"
            "            <multi>4</multi>\n"
            "            <value-re>noSSLServerURL</value-re>\n"
            "          </multi-value>\n"
            "        </multi-value-list>\n"
            "        <multi-value-list>\n"
            "          <_id>5</_id>\n"
            "          <multi-value>\n"
            "            <multi>5</multi>\n"
            "            <value-re>serverURL</value-re>\n"
            "          </multi-value>\n"
            "        </multi-value-list>\n"
            "        <multi-value-list>\n"
            "          <_id>6</_id>\n"
            "          <multi-value>\n"
            "            <multi>6</multi>\n"
            "            <value-re>disallowConfChanges</value-re>\n"
            "          </multi-value>\n"
            "        </multi-value-list>\n"
            "      </multi-entry>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>21</_id>\n"
            "    <entry>\n"
            "      <entry>21</entry>\n"
            "      <key-re>sslCACert[comment]</key-re>\n"
            "      <value>The CA cert used to verify the ssl server</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>22</_id>\n"
            "    <entry>\n"
            "      <entry>22</entry>\n"
            "      <key-re>sslCACert</key-re>\n"
            "      <value>/usr/share/rhn/RHNS-CA-CERT</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>23</_id>\n"
            "    <entry>\n"
            "      <entry>23</entry>\n"
            "      <key-re>useNoSSLForPackages[comment]</key-re>\n"
            "      <value>Use the noSSLServerURL for package, package list, and header fetching (disable Akamai)</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>24</_id>\n"
            "    <entry>\n"
            "      <entry>24</entry>\n"
            "      <key-re>useNoSSLForPackages</key-re>\n"
            "      <value>0</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>25</_id>\n"
            "    <entry>\n"
            "      <entry>25</entry>\n"
            "      <key-re>retrieveOnly[comment]</key-re>\n"
            "      <value>Retrieve packages only</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>26</_id>\n"
            "    <entry>\n"
            "      <entry>26</entry>\n"
            "      <key-re>retrieveOnly</key-re>\n"
            "      <value>0</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>27</_id>\n"
            "    <entry>\n"
            "      <entry>27</entry>\n"
            "      <key-re>skipNetwork[comment]</key-re>\n"
            "      <value>Skips network information in hardware profile sync during registration.</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>28</_id>\n"
            "    <entry>\n"
            "      <entry>28</entry>\n"
            "      <key-re>skipNetwork</key-re>\n"
            "      <value>0</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>29</_id>\n"
            "    <entry>\n"
            "      <entry>29</entry>\n"
            "      <key-re>tmpDir[comment]</key-re>\n"
            "      <value>Use this Directory to place the temporary transport files</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>30</_id>\n"
            "    <entry>\n"
            "      <entry>30</entry>\n"
            "      <key-re>tmpDir</key-re>\n"
            "      <value>/tmp</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>31</_id>\n"
            "    <entry>\n"
            "      <entry>31</entry>\n"
            "      <key-re>writeChangesToLog[comment]</key-re>\n"
            "      <value>Log to /var/log/up2date which packages has been added and removed</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>32</_id>\n"
            "    <entry>\n"
            "      <entry>32</entry>\n"
            "      <key-re>writeChangesToLog</key-re>\n"
            "      <value>0</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>33</_id>\n"
            "    <entry>\n"
            "      <entry>33</entry>\n"
            "      <key-re>stagingContent[comment]</key-re>\n"
            "      <value>Retrieve content of future actions in advance</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>34</_id>\n"
            "    <entry>\n"
            "      <entry>34</entry>\n"
            "      <key-re>stagingContent</key-re>\n"
            "      <value>1</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>35</_id>\n"
            "    <entry>\n"
            "      <entry>35</entry>\n"
            "      <key-re>stagingContentWindow[comment]</key-re>\n"
            "      <value>How much forward we should look for future actions. In hours.</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>36</_id>\n"
            "    <entry>\n"
            "      <entry>36</entry>\n"
            "      <key-re>stagingContentWindow</key-re>\n"
            "      <value>24</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
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
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_id='37']/entry/entry", "37", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_id='37']/entry/key-re", "myVariable", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_id='37']/entry/value", "55", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "entry-list[_id='32']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_id='38']/entry/entry", "38", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_id='38']/entry/key-re", "myMultiVariable", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_id='38']/entry/multi-entry/multi-value-list[_id='1']/"
            "multi-value/multi", "1", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_id='38']/entry/multi-entry/multi-value-list[_id='1']/"
            "multi-value/value-re", "value-a", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_id='38']/entry/multi-entry/multi-value-list[_id='2']/"
            "multi-value/multi", "2", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_id='38']/entry/multi-entry/multi-value-list[_id='2']/"
            "multi-value/value-re", "value-b", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_id='38']/entry/multi-entry/multi-value-list[_id='3']/"
            "multi-value/multi", "3", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_id='38']/entry/multi-entry/multi-value-list[_id='3']/"
            "multi-value/value-re", "value-c", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "entry-list[_id='34']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "52a53\n"
            "> myVariable=55\n"
            "55a57\n"
            "> myMultiVariable=value-a;value-b;value-c;\n"));
}

static void
test_store_modify(void **state)
{
    struct tstate *st = (struct tstate *)*state;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* modify some values */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_id='20']/entry/multi-entry/multi-value-list[_id='2']/"
            "multi-value/value-re", "sslClientCert", LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_id='34']/entry/key-re", "staging",
            LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_id='30']/entry/value", "/tmp/ud",
            LYD_NEW_PATH_UPDATE, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "33c33\n"
            "< disallowConfChanges=noReboot;sslCACert;useNoSSLForPackages;noSSLServerURL;serverURL;disallowConfChanges;\n"
            "---\n"
            "> disallowConfChanges=noReboot;sslClientCert;useNoSSLForPackages;noSSLServerURL;serverURL;disallowConfChanges;\n"
            "49c49\n"
            "< tmpDir=/tmp\n"
            "---\n"
            "> tmpDir=/tmp/ud\n"
            "55c55\n"
            "< stagingContent=1\n"
            "---\n"
            "> staging=1\n"));
}

static void
test_store_remove(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *node;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* remove list values */
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "entry-list[_id='30']/entry/value", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "entry-list[_id='20']/entry/multi-entry/multi-value-list[_id='5']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "entry-list[_id='7']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "entry-list[_id='8']", 0, &node));
    lyd_free_tree(node);

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "14,15d13\n"
            "< hostedWhitelist[comment]=RHN Hosted URL's\n"
            "< hostedWhitelist=\n"
            "33c31\n"
            "< disallowConfChanges=noReboot;sslCACert;useNoSSLForPackages;noSSLServerURL;serverURL;disallowConfChanges;\n"
            "---\n"
            "> disallowConfChanges=noReboot;sslCACert;useNoSSLForPackages;noSSLServerURL;disallowConfChanges;\n"
            "49c47\n"
            "< tmpDir=/tmp\n"
            "---\n"
            "> tmpDir=\n"));
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
