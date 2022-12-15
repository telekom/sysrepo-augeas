/**
 * @file test_squid.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief squid SR DS plugin test
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
#define AUG_TEST_INPUT_FILES AUG_CONFIG_FILES_DIR "/squid"
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

#define AUG_TEST_MODULE "squid"

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
            "  <entry>\n"
            "    <_id>1</_id>\n"
            "    <acl>\n"
            "      <word>\n"
            "        <word>all</word>\n"
            "        <type>src</type>\n"
            "        <setting>all</setting>\n"
            "      </word>\n"
            "    </acl>\n"
            "  </entry>\n"
            "  <entry>\n"
            "    <_id>2</_id>\n"
            "    <acl>\n"
            "      <word>\n"
            "        <word>manager</word>\n"
            "        <type>proto</type>\n"
            "        <setting>cache_object</setting>\n"
            "      </word>\n"
            "    </acl>\n"
            "  </entry>\n"
            "  <entry>\n"
            "    <_id>3</_id>\n"
            "    <acl>\n"
            "      <word>\n"
            "        <word>localhost</word>\n"
            "        <type>src</type>\n"
            "        <setting>127.0.0.1/32</setting>\n"
            "      </word>\n"
            "    </acl>\n"
            "  </entry>\n"
            "  <entry>\n"
            "    <_id>4</_id>\n"
            "    <acl>\n"
            "      <word>\n"
            "        <word>to_localhost</word>\n"
            "        <type>dst</type>\n"
            "        <setting>127.0.0.0/8</setting>\n"
            "      </word>\n"
            "    </acl>\n"
            "  </entry>\n"
            "  <entry>\n"
            "    <_id>5</_id>\n"
            "    <acl>\n"
            "      <word>\n"
            "        <word>purge</word>\n"
            "        <type>method</type>\n"
            "        <setting>PURGE</setting>\n"
            "      </word>\n"
            "    </acl>\n"
            "  </entry>\n"
            "  <entry>\n"
            "    <_id>6</_id>\n"
            "    <acl>\n"
            "      <word>\n"
            "        <word>CONNECT</word>\n"
            "        <type>method</type>\n"
            "        <setting>CONNECT</setting>\n"
            "      </word>\n"
            "    </acl>\n"
            "  </entry>\n"
            "  <entry>\n"
            "    <_id>7</_id>\n"
            "    <http-access3>\n"
            "      <allow>\n"
            "        <sto-to-spc>manager</sto-to-spc>\n"
            "        <parameters>\n"
            "          <parameters-list>\n"
            "            <_seq>1</_seq>\n"
            "            <sto-to-spc>localhost</sto-to-spc>\n"
            "          </parameters-list>\n"
            "        </parameters>\n"
            "      </allow>\n"
            "    </http-access3>\n"
            "  </entry>\n"
            "  <entry>\n"
            "    <_id>8</_id>\n"
            "    <http-access3>\n"
            "      <deny>\n"
            "        <sto-to-spc>manager</sto-to-spc>\n"
            "      </deny>\n"
            "    </http-access3>\n"
            "  </entry>\n"
            "  <entry>\n"
            "    <_id>9</_id>\n"
            "    <http-access3>\n"
            "      <allow>\n"
            "        <sto-to-spc>purge</sto-to-spc>\n"
            "        <parameters>\n"
            "          <parameters-list>\n"
            "            <_seq>1</_seq>\n"
            "            <sto-to-spc>localhost</sto-to-spc>\n"
            "          </parameters-list>\n"
            "        </parameters>\n"
            "      </allow>\n"
            "    </http-access3>\n"
            "  </entry>\n"
            "  <entry>\n"
            "    <_id>10</_id>\n"
            "    <http-access3>\n"
            "      <deny>\n"
            "        <sto-to-spc>purge</sto-to-spc>\n"
            "      </deny>\n"
            "    </http-access3>\n"
            "  </entry>\n"
            "  <entry>\n"
            "    <_id>11</_id>\n"
            "    <http-access3>\n"
            "      <deny>\n"
            "        <sto-to-spc>!Safe_ports</sto-to-spc>\n"
            "      </deny>\n"
            "    </http-access3>\n"
            "  </entry>\n"
            "  <entry>\n"
            "    <_id>12</_id>\n"
            "    <http-access3>\n"
            "      <deny>\n"
            "        <sto-to-spc>CONNECT</sto-to-spc>\n"
            "        <parameters>\n"
            "          <parameters-list>\n"
            "            <_seq>1</_seq>\n"
            "            <sto-to-spc>!SSL_ports</sto-to-spc>\n"
            "          </parameters-list>\n"
            "        </parameters>\n"
            "      </deny>\n"
            "    </http-access3>\n"
            "  </entry>\n"
            "  <entry>\n"
            "    <_id>13</_id>\n"
            "    <http-access3>\n"
            "      <allow>\n"
            "        <sto-to-spc>localhost</sto-to-spc>\n"
            "      </allow>\n"
            "    </http-access3>\n"
            "  </entry>\n"
            "  <entry>\n"
            "    <_id>14</_id>\n"
            "    <http-access3>\n"
            "      <deny>\n"
            "        <sto-to-spc>all</sto-to-spc>\n"
            "      </deny>\n"
            "    </http-access3>\n"
            "  </entry>\n"
            "  <entry>\n"
            "    <_id>15</_id>\n"
            "    <no-cache>deny query_no_cache</no-cache>\n"
            "  </entry>\n"
            "  <entry>\n"
            "    <_id>16</_id>\n"
            "    <icp-access>allow localnet</icp-access>\n"
            "  </entry>\n"
            "  <entry>\n"
            "    <_id>17</_id>\n"
            "    <icp-access>deny all</icp-access>\n"
            "  </entry>\n"
            "  <entry>\n"
            "    <_id>18</_id>\n"
            "    <http-port>3128</http-port>\n"
            "  </entry>\n"
            "  <entry>\n"
            "    <_id>19</_id>\n"
            "    <hierarchy-stoplist>cgi-bin ?</hierarchy-stoplist>\n"
            "  </entry>\n"
            "  <entry>\n"
            "    <_id>20</_id>\n"
            "    <access-log>/var/log/squid/access.log squid</access-log>\n"
            "  </entry>\n"
            "  <entry>\n"
            "    <_id>21</_id>\n"
            "    <refresh-pattern>\n"
            "      <value>^ftp:</value>\n"
            "      <min>1440</min>\n"
            "      <percent>20</percent>\n"
            "      <max>10080</max>\n"
            "    </refresh-pattern>\n"
            "  </entry>\n"
            "  <entry>\n"
            "    <_id>22</_id>\n"
            "    <refresh-pattern>\n"
            "      <value>^gopher:</value>\n"
            "      <min>1440</min>\n"
            "      <percent>0</percent>\n"
            "      <max>1440</max>\n"
            "    </refresh-pattern>\n"
            "  </entry>\n"
            "  <entry>\n"
            "    <_id>23</_id>\n"
            "    <refresh-pattern>\n"
            "      <case-insensitive/>\n"
            "      <value>(/cgi-bin/|\\?)</value>\n"
            "      <min>0</min>\n"
            "      <percent>0</percent>\n"
            "      <max>0</max>\n"
            "    </refresh-pattern>\n"
            "  </entry>\n"
            "  <entry>\n"
            "    <_id>24</_id>\n"
            "    <refresh-pattern>\n"
            "      <value>(Release|Package(.gz)*)$</value>\n"
            "      <min>0</min>\n"
            "      <percent>20</percent>\n"
            "      <max>2880</max>\n"
            "    </refresh-pattern>\n"
            "  </entry>\n"
            "  <entry>\n"
            "    <_id>25</_id>\n"
            "    <refresh-pattern>\n"
            "      <value>.</value>\n"
            "      <min>0</min>\n"
            "      <percent>20</percent>\n"
            "      <max>4320</max>\n"
            "      <option>ignore-reload</option>\n"
            "      <option>ignore-auth</option>\n"
            "    </refresh-pattern>\n"
            "  </entry>\n"
            "  <entry>\n"
            "    <_id>26</_id>\n"
            "    <acl>\n"
            "      <word>\n"
            "        <word>shoutcast</word>\n"
            "        <type>rep_header</type>\n"
            "        <setting>X-HTTP09-First-Line</setting>\n"
            "        <parameters>\n"
            "          <parameters-list>\n"
            "            <_seq>1</_seq>\n"
            "            <sto-to-spc>^ICY\\s[0-9]</sto-to-spc>\n"
            "          </parameters-list>\n"
            "        </parameters>\n"
            "      </word>\n"
            "    </acl>\n"
            "  </entry>\n"
            "  <entry>\n"
            "    <_id>27</_id>\n"
            "    <upgrade-http0.9>\n"
            "      <deny>\n"
            "        <sto-to-spc>shoutcast</sto-to-spc>\n"
            "      </deny>\n"
            "    </upgrade-http0.9>\n"
            "  </entry>\n"
            "  <entry>\n"
            "    <_id>28</_id>\n"
            "    <acl>\n"
            "      <word>\n"
            "        <word>apache</word>\n"
            "        <type>rep_header</type>\n"
            "        <setting>Server</setting>\n"
            "        <parameters>\n"
            "          <parameters-list>\n"
            "            <_seq>1</_seq>\n"
            "            <sto-to-spc>^Apache</sto-to-spc>\n"
            "          </parameters-list>\n"
            "        </parameters>\n"
            "      </word>\n"
            "    </acl>\n"
            "  </entry>\n"
            "  <entry>\n"
            "    <_id>29</_id>\n"
            "    <broken-vary-encoding>\n"
            "      <allow>\n"
            "        <sto-to-spc>apache</sto-to-spc>\n"
            "      </allow>\n"
            "    </broken-vary-encoding>\n"
            "  </entry>\n"
            "  <entry>\n"
            "    <_id>30</_id>\n"
            "    <extension-methods>\n"
            "      <extension-method-list>\n"
            "        <_seq>1</_seq>\n"
            "        <word>REPORT</word>\n"
            "      </extension-method-list>\n"
            "      <extension-method-list>\n"
            "        <_seq>2</_seq>\n"
            "        <word>MERGE</word>\n"
            "      </extension-method-list>\n"
            "      <extension-method-list>\n"
            "        <_seq>3</_seq>\n"
            "        <word>MKACTIVITY</word>\n"
            "      </extension-method-list>\n"
            "      <extension-method-list>\n"
            "        <_seq>4</_seq>\n"
            "        <word>CHECKOUT</word>\n"
            "      </extension-method-list>\n"
            "    </extension-methods>\n"
            "  </entry>\n"
            "  <entry>\n"
            "    <_id>31</_id>\n"
            "    <hosts-file>/etc/hosts</hosts-file>\n"
            "  </entry>\n"
            "  <entry>\n"
            "    <_id>32</_id>\n"
            "    <coredump-dir>/var/spool/squid</coredump-dir>\n"
            "  </entry>\n"
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
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry[_id='33']/zph-mode", "none", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "entry[_id='20']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry[_id='34']/auth-param/scheme", "specified", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry[_id='34']/auth-param/parameter", "username", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry[_id='34']/auth-param/setting", "any", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "entry[_id='29']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry[_id='28']/acl/word/parameters/"
            "parameters-list[_seq='2']/sto-to-spc", "^Flask", 0, &entries));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "20a21\n"
            "> zph_mode none\n"
            "28c29\n"
            "< acl apache rep_header Server ^Apache\n"
            "---\n"
            "> acl apache rep_header Server ^Apache ^Flask\n"
            "29a31\n"
            "> auth_param specified username any\n"));
}

static void
test_store_modify(void **state)
{
    struct tstate *st = (struct tstate *)*state;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* modify some values */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry[_id='25']/refresh-pattern/percent", "40",
            LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry[_id='6']/acl/word/setting", "DISCONNECT",
            LYD_NEW_PATH_UPDATE, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "6c6\n"
            "< acl CONNECT method CONNECT\n"
            "---\n"
            "> acl CONNECT method DISCONNECT\n"
            "25c25\n"
            "< refresh_pattern .               0       20%     4320\tignore-reload ignore-auth # testing options\n"
            "---\n"
            "> refresh_pattern .               0       40%     4320\tignore-reload ignore-auth # testing options\n"));
}

static void
test_store_remove(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *node;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* remove list values */
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "entry[_id='26']/acl/word/parameters", 0, &node));
    lyd_free_tree(node);

    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "entry[_id='25']/refresh-pattern/option[.='ignore-reload']", 0, &node));
    lyd_free_tree(node);

    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "entry[_id='1']", 0, &node));
    lyd_free_tree(node);

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "1d0\n"
            "< acl all src all\n"
            "25,26c24,25\n"
            "< refresh_pattern .               0       20%     4320\tignore-reload ignore-auth # testing options\n"
            "< acl shoutcast rep_header X-HTTP09-First-Line ^ICY\\s[0-9]\n"
            "---\n"
            "> refresh_pattern .               0       20%     4320\tignore-auth # testing options\n"
            "> acl shoutcast rep_header X-HTTP09-First-Line\n"));
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
