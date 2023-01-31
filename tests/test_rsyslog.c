/**
 * @file test_rsyslog.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief rsyslog SR DS plugin test
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
#define AUG_TEST_INPUT_FILES AUG_CONFIG_FILES_DIR "/rsyslog"
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

#define AUG_TEST_MODULE "rsyslog"

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
            "  <entries>\n"
            "    <_id>1</_id>\n"
            "    <macro>\n"
            "      <label>$ModLoad</label>\n"
            "      <macro-rx>imuxsock</macro-rx>\n"
            "    </macro>\n"
            "  </entries>\n"
            "  <entries>\n"
            "    <_id>2</_id>\n"
            "    <macro>\n"
            "      <label>$ModLoad</label>\n"
            "      <macro-rx>imklog</macro-rx>\n"
            "    </macro>\n"
            "  </entries>\n"
            "  <entries>\n"
            "    <_id>3</_id>\n"
            "    <module>\n"
            "      <config-object-param-list>\n"
            "        <_id>1</_id>\n"
            "        <config-object-param>\n"
            "          <label>load</label>\n"
            "          <value>immark</value>\n"
            "        </config-object-param>\n"
            "      </config-object-param-list>\n"
            "      <config-object-param-list>\n"
            "        <_id>2</_id>\n"
            "        <config-object-param>\n"
            "          <label>markmessageperiod</label>\n"
            "          <value>60</value>\n"
            "        </config-object-param>\n"
            "      </config-object-param-list>\n"
            "      <config-object-param-list>\n"
            "        <_id>3</_id>\n"
            "        <config-object-param>\n"
            "          <label>fakeoption</label>\n"
            "          <value>bar</value>\n"
            "        </config-object-param>\n"
            "      </config-object-param-list>\n"
            "    </module>\n"
            "  </entries>\n"
            "  <entries>\n"
            "    <_id>4</_id>\n"
            "    <timezone>\n"
            "      <config-object-param-list>\n"
            "        <_id>1</_id>\n"
            "        <config-object-param>\n"
            "          <label>id</label>\n"
            "          <value>CET</value>\n"
            "        </config-object-param>\n"
            "      </config-object-param-list>\n"
            "      <config-object-param-list>\n"
            "        <_id>2</_id>\n"
            "        <config-object-param>\n"
            "          <label>offset</label>\n"
            "          <value>+01:00</value>\n"
            "        </config-object-param>\n"
            "      </config-object-param-list>\n"
            "    </timezone>\n"
            "  </entries>\n"
            "  <entries>\n"
            "    <_id>5</_id>\n"
            "    <macro>\n"
            "      <label>$UDPServerRun</label>\n"
            "      <macro-rx>514</macro-rx>\n"
            "    </macro>\n"
            "  </entries>\n"
            "  <entries>\n"
            "    <_id>6</_id>\n"
            "    <macro>\n"
            "      <label>$InputTCPServerRun</label>\n"
            "      <macro-rx>514</macro-rx>\n"
            "    </macro>\n"
            "  </entries>\n"
            "  <entries>\n"
            "    <_id>7</_id>\n"
            "    <macro>\n"
            "      <label>$ActionFileDefaultTemplate</label>\n"
            "      <macro-rx>RSYSLOG_TraditionalFileFormat</macro-rx>\n"
            "    </macro>\n"
            "  </entries>\n"
            "  <entries>\n"
            "    <_id>8</_id>\n"
            "    <macro>\n"
            "      <label>$ActionFileEnableSync</label>\n"
            "      <macro-rx>on</macro-rx>\n"
            "    </macro>\n"
            "  </entries>\n"
            "  <entries>\n"
            "    <_id>9</_id>\n"
            "    <macro>\n"
            "      <label>$IncludeConfig</label>\n"
            "      <macro-rx>/etc/rsyslog.d/*.conf</macro-rx>\n"
            "    </macro>\n"
            "  </entries>\n"
            "  <entries>\n"
            "    <_id>10</_id>\n"
            "    <entry>\n"
            "      <selector-list>\n"
            "        <_id>1</_id>\n"
            "        <selector>\n"
            "          <facility>*</facility>\n"
            "          <level>info</level>\n"
            "        </selector>\n"
            "      </selector-list>\n"
            "      <selector-list>\n"
            "        <_id>2</_id>\n"
            "        <selector>\n"
            "          <facility>mail</facility>\n"
            "          <level>none</level>\n"
            "        </selector>\n"
            "      </selector-list>\n"
            "      <selector-list>\n"
            "        <_id>3</_id>\n"
            "        <selector>\n"
            "          <facility>authpriv</facility>\n"
            "          <level>none</level>\n"
            "        </selector>\n"
            "      </selector-list>\n"
            "      <selector-list>\n"
            "        <_id>4</_id>\n"
            "        <selector>\n"
            "          <facility>cron</facility>\n"
            "          <level>none</level>\n"
            "        </selector>\n"
            "      </selector-list>\n"
            "      <action-list>\n"
            "        <_id>1</_id>\n"
            "        <action>\n"
            "          <file>/var/log/messages</file>\n"
            "        </action>\n"
            "      </action-list>\n"
            "    </entry>\n"
            "  </entries>\n"
            "  <entries>\n"
            "    <_id>11</_id>\n"
            "    <entry>\n"
            "      <selector-list>\n"
            "        <_id>1</_id>\n"
            "        <selector>\n"
            "          <facility>authpriv</facility>\n"
            "          <level>*</level>\n"
            "        </selector>\n"
            "      </selector-list>\n"
            "      <action-list>\n"
            "        <_id>1</_id>\n"
            "        <action>\n"
            "          <file>/var/log/secure</file>\n"
            "        </action>\n"
            "      </action-list>\n"
            "    </entry>\n"
            "  </entries>\n"
            "  <entries>\n"
            "    <_id>12</_id>\n"
            "    <entry>\n"
            "      <selector-list>\n"
            "        <_id>1</_id>\n"
            "        <selector>\n"
            "          <facility>*</facility>\n"
            "          <level>emerg</level>\n"
            "        </selector>\n"
            "      </selector-list>\n"
            "      <action-list>\n"
            "        <_id>1</_id>\n"
            "        <action>\n"
            "          <user>*</user>\n"
            "        </action>\n"
            "      </action-list>\n"
            "    </entry>\n"
            "  </entries>\n"
            "  <entries>\n"
            "    <_id>13</_id>\n"
            "    <entry>\n"
            "      <selector-list>\n"
            "        <_id>1</_id>\n"
            "        <selector>\n"
            "          <facility>*</facility>\n"
            "          <level>*</level>\n"
            "        </selector>\n"
            "      </selector-list>\n"
            "      <action-list>\n"
            "        <_id>1</_id>\n"
            "        <action>\n"
            "          <protocol>@</protocol>\n"
            "          <hostname>2.7.4.1</hostname>\n"
            "        </action>\n"
            "      </action-list>\n"
            "    </entry>\n"
            "  </entries>\n"
            "  <entries>\n"
            "    <_id>14</_id>\n"
            "    <entry>\n"
            "      <selector-list>\n"
            "        <_id>1</_id>\n"
            "        <selector>\n"
            "          <facility>*</facility>\n"
            "          <level>*</level>\n"
            "        </selector>\n"
            "      </selector-list>\n"
            "      <action-list>\n"
            "        <_id>1</_id>\n"
            "        <action>\n"
            "          <protocol>@@</protocol>\n"
            "          <hostname>2.7.4.1</hostname>\n"
            "        </action>\n"
            "      </action-list>\n"
            "    </entry>\n"
            "  </entries>\n"
            "  <entries>\n"
            "    <_id>15</_id>\n"
            "    <entry>\n"
            "      <selector-list>\n"
            "        <_id>1</_id>\n"
            "        <selector>\n"
            "          <facility>*</facility>\n"
            "          <level>emerg</level>\n"
            "        </selector>\n"
            "      </selector-list>\n"
            "      <action-list>\n"
            "        <_id>1</_id>\n"
            "        <action>\n"
            "          <omusrmsg>*</omusrmsg>\n"
            "        </action>\n"
            "      </action-list>\n"
            "    </entry>\n"
            "  </entries>\n"
            "  <entries>\n"
            "    <_id>16</_id>\n"
            "    <entry>\n"
            "      <selector-list>\n"
            "        <_id>1</_id>\n"
            "        <selector>\n"
            "          <facility>*</facility>\n"
            "          <level>emerg</level>\n"
            "        </selector>\n"
            "      </selector-list>\n"
            "      <action-list>\n"
            "        <_id>1</_id>\n"
            "        <action>\n"
            "          <omusrmsg>foo</omusrmsg>\n"
            "          <omusrmsg>bar</omusrmsg>\n"
            "        </action>\n"
            "      </action-list>\n"
            "    </entry>\n"
            "  </entries>\n"
            "  <entries>\n"
            "    <_id>17</_id>\n"
            "    <entry>\n"
            "      <selector-list>\n"
            "        <_id>1</_id>\n"
            "        <selector>\n"
            "          <facility>*</facility>\n"
            "          <level>emerg</level>\n"
            "        </selector>\n"
            "      </selector-list>\n"
            "      <action-list>\n"
            "        <_id>1</_id>\n"
            "        <action>\n"
            "          <pipe>/dev/xconsole</pipe>\n"
            "        </action>\n"
            "      </action-list>\n"
            "    </entry>\n"
            "  </entries>\n"
            "  <entries>\n"
            "    <_id>18</_id>\n"
            "    <if>\n"
            "      <condition> \\\n"
            "\t    /* kernel up to warning except of firewall  */ \\\n"
            "\t    ($syslogfacility-text == 'kern')      and      \\\n"
            "\t    ($syslogseverity &lt;= 4 /* warning */ ) and not  \\\n"
            "\t    ($msg contains 'IN=' and $msg contains 'OUT=') \\\n"
            "\t</condition>\n"
            "      <config-entries>\n"
            "        <_id>1</_id>\n"
            "        <node>\n"
            "          <label>or</label>\n"
            "          <condition-expr> \\\n"
            "\t    /* up to errors except of facility authpriv */ \\\n"
            "\t    ($syslogseverity &lt;= 3 /* errors  */ ) and not  \\\n"
            "\t    ($syslogfacility-text == 'authpriv')           \\\n"
            "\t</condition-expr>\n"
            "        </node>\n"
            "      </config-entries>\n"
            "      <then>\n"
            "        <cmd>/dev/tty10</cmd>\n"
            "        <cmd>|/dev/xconsole</cmd>\n"
            "      </then>\n"
            "    </if>\n"
            "  </entries>\n"
            "  <entries>\n"
            "    <_id>19</_id>\n"
            "    <macro>\n"
            "      <label>$IncludeConfig</label>\n"
            "      <macro-rx>/etc/rsyslog.d/*.frule</macro-rx>\n"
            "    </macro>\n"
            "  </entries>\n"
            "  <entries>\n"
            "    <_id>20</_id>\n"
            "    <entry>\n"
            "      <selector-list>\n"
            "        <_id>1</_id>\n"
            "        <selector>\n"
            "          <facility>mail</facility>\n"
            "          <level>*</level>\n"
            "        </selector>\n"
            "      </selector-list>\n"
            "      <action-list>\n"
            "        <_id>1</_id>\n"
            "        <action>\n"
            "          <no-sync/>\n"
            "          <file>/var/log/mail</file>\n"
            "        </action>\n"
            "      </action-list>\n"
            "    </entry>\n"
            "  </entries>\n"
            "  <entries>\n"
            "    <_id>21</_id>\n"
            "    <entry>\n"
            "      <selector-list>\n"
            "        <_id>1</_id>\n"
            "        <selector>\n"
            "          <facility>mail</facility>\n"
            "          <level>info</level>\n"
            "        </selector>\n"
            "      </selector-list>\n"
            "      <action-list>\n"
            "        <_id>1</_id>\n"
            "        <action>\n"
            "          <no-sync/>\n"
            "          <file>/var/log/mail.info</file>\n"
            "        </action>\n"
            "      </action-list>\n"
            "    </entry>\n"
            "  </entries>\n"
            "  <entries>\n"
            "    <_id>22</_id>\n"
            "    <entry>\n"
            "      <selector-list>\n"
            "        <_id>1</_id>\n"
            "        <selector>\n"
            "          <facility>mail</facility>\n"
            "          <level>warning</level>\n"
            "        </selector>\n"
            "      </selector-list>\n"
            "      <action-list>\n"
            "        <_id>1</_id>\n"
            "        <action>\n"
            "          <no-sync/>\n"
            "          <file>/var/log/mail.warn</file>\n"
            "        </action>\n"
            "      </action-list>\n"
            "    </entry>\n"
            "  </entries>\n"
            "  <entries>\n"
            "    <_id>23</_id>\n"
            "    <entry>\n"
            "      <selector-list>\n"
            "        <_id>1</_id>\n"
            "        <selector>\n"
            "          <facility>mail</facility>\n"
            "          <level>err</level>\n"
            "        </selector>\n"
            "      </selector-list>\n"
            "      <action-list>\n"
            "        <_id>1</_id>\n"
            "        <action>\n"
            "          <file>/var/log/mail.err</file>\n"
            "        </action>\n"
            "      </action-list>\n"
            "    </entry>\n"
            "  </entries>\n"
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
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entries[_id='16']/entry/action-list[_id='2']/action/program",
            "shutdown", 0, &entries));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entries[_id='24']/parser/config-object-param-list[_id='1']/"
            "config-object-param/label", "yang", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entries[_id='24']/parser/config-object-param-list[_id='1']/"
            "config-object-param/value", "parse", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "entries[_id='9']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='1']/program/reverse", NULL, 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='1']/program/program",
            "ay_start", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='1']/program/entries[_id='1']/"
            "entry/selector-list[_id='1']/selector/facility", "*", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='1']/program/entries[_id='1']/"
            "entry/selector-list[_id='1']/selector/level", "*", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='1']/program/entries[_id='1']/"
            "entry/action/file", "/root_file", 0, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "13a14\n"
            "> parser(yang=\"parse\")\n"
            "21a23\n"
            "> & |shutdown\n"
            "59a62,63\n"
            "> !-ay_start\n"
            "> *.*\t/root_file\n"));
}

static void
test_store_modify(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *node;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* modify some values */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entries[_id='13']/entry/action-list[_id='1']/action/hostname",
            "10.10.100.1", LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "entries[_id='10']/entry/selector-list[_id='4']/selector/"
            "facility[.='cron']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entries[_id='10']/entry/selector-list[_id='4']/selector/facility",
            "apache2", LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entries[_id='3']/module/config-object-param-list[_id='2']/"
            "config-object-param/value", "30", LYD_NEW_PATH_UPDATE, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "5c5\n"
            "< module(load=\"immark\" markmessageperiod=\"60\" fakeoption=\"bar\") #provides --MARK-- message capability\n"
            "---\n"
            "> module(load=\"immark\" markmessageperiod=\"30\" fakeoption=\"bar\") #provides --MARK-- message capability\n"
            "15c15\n"
            "< *.info;mail.none;authpriv.none;cron.none                /var/log/messages\n"
            "---\n"
            "> *.info;mail.none;authpriv.none;apache2.none                /var/log/messages\n"
            "18c18\n"
            "< *.*    @2.7.4.1\n"
            "---\n"
            "> *.*    @10.10.100.1\n"));
}

static void
test_store_remove(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *node;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* remove list values */
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "entries[_id='16']/entry/action-list[_id='1']/action/"
            "omusrmsg[.='foo']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "entries[_id='10']/entry/selector-list[_id='2']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "entries[_id='6']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "entries[_id='13']", 0, &node));
    lyd_free_tree(node);

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "10d9\n"
            "< $InputTCPServerRun 514\n"
            "15c14\n"
            "< *.info;mail.none;authpriv.none;cron.none                /var/log/messages\n"
            "---\n"
            "> *.info;authpriv.none;cron.none                /var/log/messages\n"
            "18d16\n"
            "< *.*    @2.7.4.1\n"
            "20,21c18,19\n"
            "< *.emerg :omusrmsg:*\n"
            "< *.emerg :omusrmsg:foo,bar\n"
            "---\n"
            "> *.emerg    :omusrmsg:*\n"
            "> *.emerg :omusrmsg:bar\n"));
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
