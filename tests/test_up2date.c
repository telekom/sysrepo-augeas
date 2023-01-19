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
            "    <_seq>1</_seq>\n"
            "    <key>debug[comment]</key>\n"
            "    <value>Whether or not debugging is enabled</value>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_seq>2</_seq>\n"
            "    <key>debug</key>\n"
            "    <value>0</value>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_seq>3</_seq>\n"
            "    <key>systemIdPath[comment]</key>\n"
            "    <value>Location of system id</value>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_seq>4</_seq>\n"
            "    <key>systemIdPath</key>\n"
            "    <value>/etc/sysconfig/rhn/systemid</value>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_seq>5</_seq>\n"
            "    <key>serverURL[comment]</key>\n"
            "    <value>Remote server URL (use FQDN)</value>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_seq>6</_seq>\n"
            "    <key>serverURL</key>\n"
            "    <value>https://enter.your.server.url.here/XMLRPC</value>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_seq>7</_seq>\n"
            "    <key>hostedWhitelist[comment]</key>\n"
            "    <value>RHN Hosted URL's</value>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_seq>8</_seq>\n"
            "    <key>hostedWhitelist</key>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_seq>9</_seq>\n"
            "    <key>enableProxy[comment]</key>\n"
            "    <value>Use a HTTP Proxy</value>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_seq>10</_seq>\n"
            "    <key>enableProxy</key>\n"
            "    <value>0</value>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_seq>11</_seq>\n"
            "    <key>versionOverride[comment]</key>\n"
            "    <value>Override the automatically determined system version</value>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_seq>12</_seq>\n"
            "    <key>versionOverride</key>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_seq>13</_seq>\n"
            "    <key>httpProxy[comment]</key>\n"
            "    <value>HTTP proxy in host:port format, e.g. squid.redhat.com:3128</value>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_seq>14</_seq>\n"
            "    <key>httpProxy</key>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_seq>15</_seq>\n"
            "    <key>noReboot[comment]</key>\n"
            "    <value>Disable the reboot actions</value>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_seq>16</_seq>\n"
            "    <key>noReboot</key>\n"
            "    <value>0</value>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_seq>17</_seq>\n"
            "    <key>networkRetries[comment]</key>\n"
            "    <value>Number of attempts to make at network connections before giving up</value>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_seq>18</_seq>\n"
            "    <key>networkRetries</key>\n"
            "    <value>1</value>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_seq>19</_seq>\n"
            "    <key>disallowConfChanges[comment]</key>\n"
            "    <value>Config options that can not be overwritten by a config update action</value>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_seq>20</_seq>\n"
            "    <key>disallowConfChanges</key>\n"
            "    <multi-entry>\n"
            "      <multi-list>\n"
            "        <_seq>1</_seq>\n"
            "        <value>noReboot</value>\n"
            "      </multi-list>\n"
            "      <multi-list>\n"
            "        <_seq>2</_seq>\n"
            "        <value>sslCACert</value>\n"
            "      </multi-list>\n"
            "      <multi-list>\n"
            "        <_seq>3</_seq>\n"
            "        <value>useNoSSLForPackages</value>\n"
            "      </multi-list>\n"
            "      <multi-list>\n"
            "        <_seq>4</_seq>\n"
            "        <value>noSSLServerURL</value>\n"
            "      </multi-list>\n"
            "      <multi-list>\n"
            "        <_seq>5</_seq>\n"
            "        <value>serverURL</value>\n"
            "      </multi-list>\n"
            "      <multi-list>\n"
            "        <_seq>6</_seq>\n"
            "        <value>disallowConfChanges</value>\n"
            "      </multi-list>\n"
            "    </multi-entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_seq>21</_seq>\n"
            "    <key>sslCACert[comment]</key>\n"
            "    <value>The CA cert used to verify the ssl server</value>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_seq>22</_seq>\n"
            "    <key>sslCACert</key>\n"
            "    <value>/usr/share/rhn/RHNS-CA-CERT</value>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_seq>23</_seq>\n"
            "    <key>useNoSSLForPackages[comment]</key>\n"
            "    <value>Use the noSSLServerURL for package, package list, and header fetching (disable Akamai)</value>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_seq>24</_seq>\n"
            "    <key>useNoSSLForPackages</key>\n"
            "    <value>0</value>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_seq>25</_seq>\n"
            "    <key>retrieveOnly[comment]</key>\n"
            "    <value>Retrieve packages only</value>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_seq>26</_seq>\n"
            "    <key>retrieveOnly</key>\n"
            "    <value>0</value>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_seq>27</_seq>\n"
            "    <key>skipNetwork[comment]</key>\n"
            "    <value>Skips network information in hardware profile sync during registration.</value>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_seq>28</_seq>\n"
            "    <key>skipNetwork</key>\n"
            "    <value>0</value>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_seq>29</_seq>\n"
            "    <key>tmpDir[comment]</key>\n"
            "    <value>Use this Directory to place the temporary transport files</value>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_seq>30</_seq>\n"
            "    <key>tmpDir</key>\n"
            "    <value>/tmp</value>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_seq>31</_seq>\n"
            "    <key>writeChangesToLog[comment]</key>\n"
            "    <value>Log to /var/log/up2date which packages has been added and removed</value>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_seq>32</_seq>\n"
            "    <key>writeChangesToLog</key>\n"
            "    <value>0</value>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_seq>33</_seq>\n"
            "    <key>stagingContent[comment]</key>\n"
            "    <value>Retrieve content of future actions in advance</value>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_seq>34</_seq>\n"
            "    <key>stagingContent</key>\n"
            "    <value>1</value>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_seq>35</_seq>\n"
            "    <key>stagingContentWindow[comment]</key>\n"
            "    <value>How much forward we should look for future actions. In hours.</value>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_seq>36</_seq>\n"
            "    <key>stagingContentWindow</key>\n"
            "    <value>24</value>\n"
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
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_seq='37']/key", "myVariable", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_seq='37']/value", "55", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "entry-list[_seq='32']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_seq='38']/key", "myMultiVariable", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_seq='38']/multi-entry/multi-list[_seq='1']/"
            "value", "value-a", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_seq='38']/multi-entry/multi-list[_seq='2']/"
            "value", "value-b", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_seq='38']/multi-entry/multi-list[_seq='3']/"
            "value", "value-c", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "entry-list[_seq='34']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

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
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_seq='20']/multi-entry/multi-list[_seq='2']/"
            "value", "sslClientCert", LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_seq='34']/key", "staging",
            LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_seq='30']/value", "/tmp/ud",
            LYD_NEW_PATH_UPDATE, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

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
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "entry-list[_seq='30']/value", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "entry-list[_seq='20']/multi-entry/multi-list[_seq='5']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "entry-list[_seq='7']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "entry-list[_seq='8']", 0, &node));
    lyd_free_tree(node);

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

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
