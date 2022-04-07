/**
 * @file test_sshd.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief sshd SR DS plugin test
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
#define AUG_TEST_INPUT_FILES AUG_CONFIG_FILES_DIR "/sshd"
#include "srds_augeas.c"

#include <assert.h>
#include <dlfcn.h>
#include <setjmp.h>
#include <stdarg.h>
#include <unistd.h>

#include <cmocka.h>
#include <libyang/libyang.h>
#include <sysrepo/plugins_datastore.h>

#define AUG_TEST_MODULE "sshd"

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
            "  <ch-accept_env-list>\n"
            "    <_id>1</_id>\n"
            "    <other_entry>\n"
            "      <key_re>Port</key_re>\n"
            "      <value>22</value>\n"
            "    </other_entry>\n"
            "  </ch-accept_env-list>\n"
            "  <ch-accept_env-list>\n"
            "    <_id>2</_id>\n"
            "    <other_entry>\n"
            "      <key_re>AddressFamily</key_re>\n"
            "      <value>any</value>\n"
            "    </other_entry>\n"
            "  </ch-accept_env-list>\n"
            "  <ch-accept_env-list>\n"
            "    <_id>3</_id>\n"
            "    <other_entry>\n"
            "      <key_re>ListenAddress</key_re>\n"
            "      <value>0.0.0.0</value>\n"
            "    </other_entry>\n"
            "  </ch-accept_env-list>\n"
            "  <ch-accept_env-list>\n"
            "    <_id>4</_id>\n"
            "    <other_entry>\n"
            "      <key_re>ListenAddress</key_re>\n"
            "      <value>::</value>\n"
            "    </other_entry>\n"
            "  </ch-accept_env-list>\n"
            "  <ch-accept_env-list>\n"
            "    <_id>5</_id>\n"
            "    <other_entry>\n"
            "      <key_re>HostKey</key_re>\n"
            "      <value>/etc/ssh/ssh_host_rsa_key</value>\n"
            "    </other_entry>\n"
            "  </ch-accept_env-list>\n"
            "  <ch-accept_env-list>\n"
            "    <_id>6</_id>\n"
            "    <other_entry>\n"
            "      <key_re>HostKey</key_re>\n"
            "      <value>/etc/ssh/ssh_host_ecdsa_key</value>\n"
            "    </other_entry>\n"
            "  </ch-accept_env-list>\n"
            "  <ch-accept_env-list>\n"
            "    <_id>7</_id>\n"
            "    <other_entry>\n"
            "      <key_re>HostKey</key_re>\n"
            "      <value>/etc/ssh/ssh_host_ed25519_key</value>\n"
            "    </other_entry>\n"
            "  </ch-accept_env-list>\n"
            "  <ch-accept_env-list>\n"
            "    <_id>8</_id>\n"
            "    <other_entry>\n"
            "      <key_re>RekeyLimit</key_re>\n"
            "      <value>default none</value>\n"
            "    </other_entry>\n"
            "  </ch-accept_env-list>\n"
            "  <ch-accept_env-list>\n"
            "    <_id>9</_id>\n"
            "    <other_entry>\n"
            "      <key_re>SyslogFacility</key_re>\n"
            "      <value>AUTH</value>\n"
            "    </other_entry>\n"
            "  </ch-accept_env-list>\n"
            "  <ch-accept_env-list>\n"
            "    <_id>10</_id>\n"
            "    <other_entry>\n"
            "      <key_re>LogLevel</key_re>\n"
            "      <value>INFO</value>\n"
            "    </other_entry>\n"
            "  </ch-accept_env-list>\n"
            "  <ch-accept_env-list>\n"
            "    <_id>11</_id>\n"
            "    <other_entry>\n"
            "      <key_re>LoginGraceTime</key_re>\n"
            "      <value>2m</value>\n"
            "    </other_entry>\n"
            "  </ch-accept_env-list>\n"
            "  <ch-accept_env-list>\n"
            "    <_id>12</_id>\n"
            "    <other_entry>\n"
            "      <key_re>PermitRootLogin</key_re>\n"
            "      <value>yes</value>\n"
            "    </other_entry>\n"
            "  </ch-accept_env-list>\n"
            "  <ch-accept_env-list>\n"
            "    <_id>13</_id>\n"
            "    <other_entry>\n"
            "      <key_re>StrictModes</key_re>\n"
            "      <value>yes</value>\n"
            "    </other_entry>\n"
            "  </ch-accept_env-list>\n"
            "  <ch-accept_env-list>\n"
            "    <_id>14</_id>\n"
            "    <other_entry>\n"
            "      <key_re>MaxAuthTries</key_re>\n"
            "      <value>6</value>\n"
            "    </other_entry>\n"
            "  </ch-accept_env-list>\n"
            "  <ch-accept_env-list>\n"
            "    <_id>15</_id>\n"
            "    <other_entry>\n"
            "      <key_re>MaxSessions</key_re>\n"
            "      <value>10</value>\n"
            "    </other_entry>\n"
            "  </ch-accept_env-list>\n"
            "  <ch-accept_env-list>\n"
            "    <_id>16</_id>\n"
            "    <other_entry>\n"
            "      <key_re>PubkeyAuthentication</key_re>\n"
            "      <value>yes</value>\n"
            "    </other_entry>\n"
            "  </ch-accept_env-list>\n"
            "  <ch-accept_env-list>\n"
            "    <_id>17</_id>\n"
            "    <other_entry>\n"
            "      <key_re>AuthorizedKeysFile</key_re>\n"
            "      <value>.ssh/authorized_keys</value>\n"
            "    </other_entry>\n"
            "  </ch-accept_env-list>\n"
            "  <ch-accept_env-list>\n"
            "    <_id>18</_id>\n"
            "    <other_entry>\n"
            "      <key_re>AuthorizedPrincipalsFile</key_re>\n"
            "      <value>none</value>\n"
            "    </other_entry>\n"
            "  </ch-accept_env-list>\n"
            "  <ch-accept_env-list>\n"
            "    <_id>19</_id>\n"
            "    <other_entry>\n"
            "      <key_re>AuthorizedKeysCommand</key_re>\n"
            "      <value>none</value>\n"
            "    </other_entry>\n"
            "  </ch-accept_env-list>\n"
            "  <ch-accept_env-list>\n"
            "    <_id>20</_id>\n"
            "    <other_entry>\n"
            "      <key_re>AuthorizedKeysCommandUser</key_re>\n"
            "      <value>nobody</value>\n"
            "    </other_entry>\n"
            "  </ch-accept_env-list>\n"
            "  <ch-accept_env-list>\n"
            "    <_id>21</_id>\n"
            "    <other_entry>\n"
            "      <key_re>HostbasedAuthentication</key_re>\n"
            "      <value>no</value>\n"
            "    </other_entry>\n"
            "  </ch-accept_env-list>\n"
            "  <ch-accept_env-list>\n"
            "    <_id>22</_id>\n"
            "    <other_entry>\n"
            "      <key_re>IgnoreUserKnownHosts</key_re>\n"
            "      <value>no</value>\n"
            "    </other_entry>\n"
            "  </ch-accept_env-list>\n"
            "  <ch-accept_env-list>\n"
            "    <_id>23</_id>\n"
            "    <other_entry>\n"
            "      <key_re>IgnoreRhosts</key_re>\n"
            "      <value>yes</value>\n"
            "    </other_entry>\n"
            "  </ch-accept_env-list>\n"
            "  <ch-accept_env-list>\n"
            "    <_id>24</_id>\n"
            "    <other_entry>\n"
            "      <key_re>PasswordAuthentication</key_re>\n"
            "      <value>no</value>\n"
            "    </other_entry>\n"
            "  </ch-accept_env-list>\n"
            "  <ch-accept_env-list>\n"
            "    <_id>25</_id>\n"
            "    <other_entry>\n"
            "      <key_re>PermitEmptyPasswords</key_re>\n"
            "      <value>no</value>\n"
            "    </other_entry>\n"
            "  </ch-accept_env-list>\n"
            "  <ch-accept_env-list>\n"
            "    <_id>26</_id>\n"
            "    <other_entry>\n"
            "      <key_re>ChallengeResponseAuthentication</key_re>\n"
            "      <value>no</value>\n"
            "    </other_entry>\n"
            "  </ch-accept_env-list>\n"
            "  <ch-accept_env-list>\n"
            "    <_id>27</_id>\n"
            "    <other_entry>\n"
            "      <key_re>Banner</key_re>\n"
            "      <value>none</value>\n"
            "    </other_entry>\n"
            "  </ch-accept_env-list>\n"
            "  <ch-accept_env-list>\n"
            "    <_id>28</_id>\n"
            "    <subsystem>\n"
            "      <subsystemvalue>\n"
            "        <label>sftp</label>\n"
            "        <value>internal-sftp</value>\n"
            "      </subsystemvalue>\n"
            "    </subsystem>\n"
            "  </ch-accept_env-list>\n"
            "  <ch-accept_env-list>\n"
            "    <_id>29</_id>\n"
            "    <accept_env>\n"
            "      <AcceptEnv-list>\n"
            "        <_id>1</_id>\n"
            "        <AcceptEnv>\n"
            "          <AcceptEnv>1</AcceptEnv>\n"
            "          <value>LANG</value>\n"
            "        </AcceptEnv>\n"
            "      </AcceptEnv-list>\n"
            "      <AcceptEnv-list>\n"
            "        <_id>2</_id>\n"
            "        <AcceptEnv>\n"
            "          <AcceptEnv>2</AcceptEnv>\n"
            "          <value>LC_CTYPE</value>\n"
            "        </AcceptEnv>\n"
            "      </AcceptEnv-list>\n"
            "      <AcceptEnv-list>\n"
            "        <_id>3</_id>\n"
            "        <AcceptEnv>\n"
            "          <AcceptEnv>3</AcceptEnv>\n"
            "          <value>LC_NUMERIC</value>\n"
            "        </AcceptEnv>\n"
            "      </AcceptEnv-list>\n"
            "      <AcceptEnv-list>\n"
            "        <_id>4</_id>\n"
            "        <AcceptEnv>\n"
            "          <AcceptEnv>4</AcceptEnv>\n"
            "          <value>LC_TIME</value>\n"
            "        </AcceptEnv>\n"
            "      </AcceptEnv-list>\n"
            "      <AcceptEnv-list>\n"
            "        <_id>5</_id>\n"
            "        <AcceptEnv>\n"
            "          <AcceptEnv>5</AcceptEnv>\n"
            "          <value>LC_COLLATE</value>\n"
            "        </AcceptEnv>\n"
            "      </AcceptEnv-list>\n"
            "      <AcceptEnv-list>\n"
            "        <_id>6</_id>\n"
            "        <AcceptEnv>\n"
            "          <AcceptEnv>6</AcceptEnv>\n"
            "          <value>LC_MONETARY</value>\n"
            "        </AcceptEnv>\n"
            "      </AcceptEnv-list>\n"
            "      <AcceptEnv-list>\n"
            "        <_id>7</_id>\n"
            "        <AcceptEnv>\n"
            "          <AcceptEnv>7</AcceptEnv>\n"
            "          <value>LC_MESSAGES</value>\n"
            "        </AcceptEnv>\n"
            "      </AcceptEnv-list>\n"
            "    </accept_env>\n"
            "  </ch-accept_env-list>\n"
            "  <ch-accept_env-list>\n"
            "    <_id>30</_id>\n"
            "    <accept_env>\n"
            "      <AcceptEnv-list>\n"
            "        <_id>1</_id>\n"
            "        <AcceptEnv>\n"
            "          <AcceptEnv>8</AcceptEnv>\n"
            "          <value>LC_PAPER</value>\n"
            "        </AcceptEnv>\n"
            "      </AcceptEnv-list>\n"
            "      <AcceptEnv-list>\n"
            "        <_id>2</_id>\n"
            "        <AcceptEnv>\n"
            "          <AcceptEnv>9</AcceptEnv>\n"
            "          <value>LC_NAME</value>\n"
            "        </AcceptEnv>\n"
            "      </AcceptEnv-list>\n"
            "      <AcceptEnv-list>\n"
            "        <_id>3</_id>\n"
            "        <AcceptEnv>\n"
            "          <AcceptEnv>10</AcceptEnv>\n"
            "          <value>LC_ADDRESS</value>\n"
            "        </AcceptEnv>\n"
            "      </AcceptEnv-list>\n"
            "      <AcceptEnv-list>\n"
            "        <_id>4</_id>\n"
            "        <AcceptEnv>\n"
            "          <AcceptEnv>11</AcceptEnv>\n"
            "          <value>LC_TELEPHONE</value>\n"
            "        </AcceptEnv>\n"
            "      </AcceptEnv-list>\n"
            "      <AcceptEnv-list>\n"
            "        <_id>5</_id>\n"
            "        <AcceptEnv>\n"
            "          <AcceptEnv>12</AcceptEnv>\n"
            "          <value>LC_MEASUREMENT</value>\n"
            "        </AcceptEnv>\n"
            "      </AcceptEnv-list>\n"
            "    </accept_env>\n"
            "  </ch-accept_env-list>\n"
            "  <ch-accept_env-list>\n"
            "    <_id>31</_id>\n"
            "    <accept_env>\n"
            "      <AcceptEnv-list>\n"
            "        <_id>1</_id>\n"
            "        <AcceptEnv>\n"
            "          <AcceptEnv>13</AcceptEnv>\n"
            "          <value>LC_IDENTIFICATION</value>\n"
            "        </AcceptEnv>\n"
            "      </AcceptEnv-list>\n"
            "      <AcceptEnv-list>\n"
            "        <_id>2</_id>\n"
            "        <AcceptEnv>\n"
            "          <AcceptEnv>14</AcceptEnv>\n"
            "          <value>LC_ALL</value>\n"
            "        </AcceptEnv>\n"
            "      </AcceptEnv-list>\n"
            "    </accept_env>\n"
            "  </ch-accept_env-list>\n"
            "  <match-list>\n"
            "    <_id>1</_id>\n"
            "    <match>\n"
            "      <match_cond>\n"
            "        <config-entries>\n"
            "          <_id>1</_id>\n"
            "          <node>\n"
            "            <label>User</label>\n"
            "            <value>anoncvs</value>\n"
            "          </node>\n"
            "        </config-entries>\n"
            "      </match_cond>\n"
            "      <Settings>\n"
            "        <ch-accept_env-list>\n"
            "          <_id>1</_id>\n"
            "          <other_entry>\n"
            "            <key_re>X11Forwarding</key_re>\n"
            "            <value>no</value>\n"
            "          </other_entry>\n"
            "        </ch-accept_env-list>\n"
            "        <ch-accept_env-list>\n"
            "          <_id>2</_id>\n"
            "          <other_entry>\n"
            "            <key_re>AllowTcpForwarding</key_re>\n"
            "            <value>no</value>\n"
            "          </other_entry>\n"
            "        </ch-accept_env-list>\n"
            "        <ch-accept_env-list>\n"
            "          <_id>3</_id>\n"
            "          <other_entry>\n"
            "            <key_re>PermitTTY</key_re>\n"
            "            <value>no</value>\n"
            "          </other_entry>\n"
            "        </ch-accept_env-list>\n"
            "        <ch-accept_env-list>\n"
            "          <_id>4</_id>\n"
            "          <other_entry>\n"
            "            <key_re>ForceCommand</key_re>\n"
            "            <value>cvs server</value>\n"
            "          </other_entry>\n"
            "        </ch-accept_env-list>\n"
            "      </Settings>\n"
            "    </match>\n"
            "  </match-list>\n"
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
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "match-list[_id='1']/match/Settings/"
            "ch-accept_env-list[_id='5']/other_entry/key_re", "ExposeAuthInfo", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "match-list[_id='1']/match/Settings/"
            "ch-accept_env-list[_id='5']/other_entry/value", "no", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "match-list[_id='1']/match/Settings/"
            "ch-accept_env-list[_id='1']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "ch-accept_env-list[_id='31']/accept_env/"
            "AcceptEnv-list[_id='3']/AcceptEnv/AcceptEnv", "15", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "ch-accept_env-list[_id='31']/accept_env/"
            "AcceptEnv-list[_id='3']/AcceptEnv/value", "LC_CURRENCY", 0, NULL));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "ch-accept_env-list[_id='32']/subsystem/subsystemvalue/"
            "label", "netconf", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "ch-accept_env-list[_id='32']/subsystem/subsystemvalue/"
            "value", "internal-nc", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "ch-accept_env-list[_id='28']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "56a57\n"
            "> Subsystem netconf internal-nc\n"
            "61c62\n"
            "< AcceptEnv LC_IDENTIFICATION LC_ALL\n"
            "---\n"
            "> AcceptEnv LC_IDENTIFICATION LC_ALL LC_CURRENCY\n"
            "65a67\n"
            ">   ExposeAuthInfo no"));
}

static void
test_store_modify(void **state)
{
    struct tstate *st = (struct tstate *)*state;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* modify some values */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "match-list[_id='1']/match/match_cond/"
            "config-entries[_id='1']/node/value", "nobody", LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "match-list[_id='1']/match/Settings/"
            "ch-accept_env-list[_id='2']/other_entry/value", "yes", LYD_NEW_PATH_UPDATE, NULL));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "ch-accept_env-list[_id='3']/other_entry/value",
            "127.0.0.1", LYD_NEW_PATH_UPDATE, NULL));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "ch-accept_env-list[_id='30']/accept_env/"
            "AcceptEnv-list[_id='4']/AcceptEnv/value", "LC_MOBILE", LYD_NEW_PATH_UPDATE, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "4c4\n"
            "< ListenAddress 0.0.0.0\n"
            "---\n"
            "> ListenAddress 127.0.0.1\n"
            "60c60\n"
            "< AcceptEnv LC_PAPER LC_NAME LC_ADDRESS LC_TELEPHONE LC_MEASUREMENT\n"
            "---\n"
            "> AcceptEnv LC_PAPER LC_NAME LC_ADDRESS LC_MOBILE LC_MEASUREMENT\n"
            "64c64\n"
            "< Match User anoncvs\n"
            "---\n"
            "> Match User nobody\n"
            "66c66\n"
            "<   AllowTcpForwarding no\n"
            "---\n"
            ">   AllowTcpForwarding yes"));
}

static void
test_store_remove(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *node;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* remove list values */
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "match-list[_id='1']/match/Settings/"
            "ch-accept_env-list[_id='3']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "ch-accept_env-list[_id='30']/accept_env/"
            "AcceptEnv-list[_id='1']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "ch-accept_env-list[_id='22']", 0, &node));
    lyd_free_tree(node);

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "41d40\n"
            "< IgnoreUserKnownHosts no\n"
            "60c59\n"
            "< AcceptEnv LC_PAPER LC_NAME LC_ADDRESS LC_TELEPHONE LC_MEASUREMENT\n"
            "---\n"
            "> AcceptEnv LC_NAME LC_ADDRESS LC_TELEPHONE LC_MEASUREMENT\n"
            "67d65\n"
            "<   PermitTTY no"));
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
