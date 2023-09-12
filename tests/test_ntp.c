/**
 * @file test_ntp.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief ntp SR DS plugin test
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
#define AUG_TEST_INPUT_FILES AUG_CONFIG_FILES_DIR "/ntp"
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

#define AUG_TEST_MODULE "ntp"

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
            "    <server>\n"
            "      <word>dns01.echo-net.net</word>\n"
            "      <config-entries>\n"
            "        <_id>1</_id>\n"
            "        <version>3</version>\n"
            "      </config-entries>\n"
            "    </server>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>2</_id>\n"
            "    <server>\n"
            "      <word>dns02.echo-net.net</word>\n"
            "      <config-entries>\n"
            "        <_id>1</_id>\n"
            "        <version>4</version>\n"
            "      </config-entries>\n"
            "    </server>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>3</_id>\n"
            "    <driftfile>/var/lib/ntp/ntp.drift</driftfile>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>4</_id>\n"
            "    <restrict>\n"
            "      <value>default</value>\n"
            "      <action-list>\n"
            "        <_id>1</_id>\n"
            "        <action>ignore</action>\n"
            "      </action-list>\n"
            "    </restrict>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>5</_id>\n"
            "    <restrict>\n"
            "      <value>192.168.0.150</value>\n"
            "      <action-list>\n"
            "        <_id>1</_id>\n"
            "        <action>nomodify</action>\n"
            "      </action-list>\n"
            "    </restrict>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>6</_id>\n"
            "    <restrict>\n"
            "      <value>127.0.0.1</value>\n"
            "    </restrict>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>7</_id>\n"
            "    <logfile>/var/log/ntpd</logfile>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>8</_id>\n"
            "    <statsdir>/var/log/ntpstats/</statsdir>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>9</_id>\n"
            "    <ntpsigndsocket>/var/lib/samba/ntp_signd</ntpsigndsocket>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>10</_id>\n"
            "    <statistics>\n"
            "      <statistics-opts>\n"
            "        <_id>1</_id>\n"
            "        <loopstats/>\n"
            "      </statistics-opts>\n"
            "      <statistics-opts>\n"
            "        <_id>2</_id>\n"
            "        <peerstats/>\n"
            "      </statistics-opts>\n"
            "      <statistics-opts>\n"
            "        <_id>3</_id>\n"
            "        <clockstats/>\n"
            "      </statistics-opts>\n"
            "    </statistics>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>11</_id>\n"
            "    <filegen>\n"
            "      <word>loopstats</word>\n"
            "      <filegen-opts>\n"
            "        <_id>1</_id>\n"
            "        <file>loopstats</file>\n"
            "      </filegen-opts>\n"
            "      <filegen-opts>\n"
            "        <_id>2</_id>\n"
            "        <type>day</type>\n"
            "      </filegen-opts>\n"
            "      <filegen-opts>\n"
            "        <_id>3</_id>\n"
            "        <enable>enable</enable>\n"
            "      </filegen-opts>\n"
            "      <filegen-opts>\n"
            "        <_id>4</_id>\n"
            "        <link>link</link>\n"
            "      </filegen-opts>\n"
            "    </filegen>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>12</_id>\n"
            "    <filegen>\n"
            "      <word>peerstats</word>\n"
            "      <filegen-opts>\n"
            "        <_id>1</_id>\n"
            "        <file>peerstats</file>\n"
            "      </filegen-opts>\n"
            "      <filegen-opts>\n"
            "        <_id>2</_id>\n"
            "        <type>day</type>\n"
            "      </filegen-opts>\n"
            "      <filegen-opts>\n"
            "        <_id>3</_id>\n"
            "        <enable>disable</enable>\n"
            "      </filegen-opts>\n"
            "    </filegen>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>13</_id>\n"
            "    <filegen>\n"
            "      <word>clockstats</word>\n"
            "      <filegen-opts>\n"
            "        <_id>1</_id>\n"
            "        <file>clockstats</file>\n"
            "      </filegen-opts>\n"
            "      <filegen-opts>\n"
            "        <_id>2</_id>\n"
            "        <type>day</type>\n"
            "      </filegen-opts>\n"
            "      <filegen-opts>\n"
            "        <_id>3</_id>\n"
            "        <enable>enable</enable>\n"
            "      </filegen-opts>\n"
            "      <filegen-opts>\n"
            "        <_id>4</_id>\n"
            "        <link>nolink</link>\n"
            "      </filegen-opts>\n"
            "    </filegen>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>14</_id>\n"
            "    <interface>\n"
            "      <action>ignore</action>\n"
            "      <addresses>wildcard</addresses>\n"
            "    </interface>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>15</_id>\n"
            "    <interface>\n"
            "      <action>listen</action>\n"
            "      <addresses>127.0.0.1</addresses>\n"
            "    </interface>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>16</_id>\n"
            "    <autokey/>\n"      /* no value cause of weird lens with empty label */
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>17</_id>\n"
            "    <requestkey>25</requestkey>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>18</_id>\n"
            "    <revoke/>\n"       /* no value cause of weird lens with empty label */
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
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='19']/pool/word", "my-pool", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='19']/pool/config-entries[_id='1']/true",
            NULL, 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='19']/pool/config-entries[_id='2']/ttl",
            "64", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='1']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_before(node, entries));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='20']/fudge/word",
            "not-sure-what", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='20']/fudge/refid", "5", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='6']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='21']/enable/flag-list[_id='1']/flag",
            "kernel", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='21']/enable/flag-list[_id='2']/flag",
            "stats", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='21']/enable/flag-list[_id='3']/flag",
            "auth", 0, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "3a4\n"
            "> pool my-pool true ttl 64\n"
            "11a13\n"
            "> fudge not-sure-what refid 5\n"
            "23a26\n"
            "> enable kernel stats auth\n"));
}

static void
test_store_modify(void **state)
{
    struct tstate *st = (struct tstate *)*state;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* modify some values */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='11']/filegen/"
            "filegen-opts[_id='4']/link", "nolink", LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='14']/interface/action", "drop",
            LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='17']/requestkey", "50",
            LYD_NEW_PATH_UPDATE, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "16c16\n"
            "< filegen loopstats file loopstats type day enable link\n"
            "---\n"
            "> filegen loopstats file loopstats type day enable nolink\n"
            "19c19\n"
            "< interface ignore wildcard\n"
            "---\n"
            "> interface drop wildcard\n"
            "22c22\n"
            "< requestkey 25\n"
            "---\n"
            "> requestkey 50\n"));
}

static void
test_store_remove(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *node;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* remove 2 list values */
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='2']/server/config-entries[_id='1']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='10']/statistics/statistics-opts[_id='2']",
            0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='16']", 0, &node));
    lyd_free_tree(node);

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "5c5\n"
            "< server dns02.echo-net.net version 4\n"
            "---\n"
            "> server dns02.echo-net.net\n"
            "15c15\n"
            "< statistics loopstats peerstats clockstats\n"
            "---\n"
            "> statistics loopstats clockstats\n"
            "21d20\n"
            "< autokey akey\n"));
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
