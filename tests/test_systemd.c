/**
 * @file test_systemd.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief systemd SR DS plugin test
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
#define AUG_TEST_INPUT_FILES AUG_CONFIG_FILES_DIR "/systemd"
#include "srds_augeas.c"

#include <assert.h>
#include <dlfcn.h>
#include <setjmp.h>
#include <stdarg.h>
#include <unistd.h>

#include <cmocka.h>
#include <libyang/libyang.h>
#include <sysrepo/plugins_datastore.h>

#define AUG_TEST_MODULE "systemd"

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
            "  <record-list>\n"
            "    <_id>1</_id>\n"
            "    <record>\n"
            "      <label>Unit</label>\n"
            "      <config-entries>\n"
            "        <_id>1</_id>\n"
            "        <entry-single>\n"
            "          <value>The Apache HTTP Server</value>\n"
            "        </entry-single>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>2</_id>\n"
            "        <entry-multi>\n"
            "          <entry-multi-kw>After</entry-multi-kw>\n"
            "          <value>network.target</value>\n"
            "          <value>remote-fs.target</value>\n"
            "          <value>nss-lookup.target</value>\n"
            "        </entry-multi>\n"
            "      </config-entries>\n"
            "    </record>\n"
            "  </record-list>\n"
            "  <record-list>\n"
            "    <_id>2</_id>\n"
            "    <record>\n"
            "      <label>Service</label>\n"
            "      <config-entries>\n"
            "        <_id>1</_id>\n"
            "        <entry-multi>\n"
            "          <entry-multi-kw>Type</entry-multi-kw>\n"
            "          <value>notify</value>\n"
            "        </entry-multi>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>2</_id>\n"
            "        <entry-multi>\n"
            "          <entry-multi-kw>EnvironmentFile</entry-multi-kw>\n"
            "          <value>/etc/sysconfig/httpd</value>\n"
            "        </entry-multi>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>3</_id>\n"
            "        <entry-env>\n"
            "          <env-key-list>\n"
            "            <_id>1</_id>\n"
            "            <env-key>\n"
            "              <env-key>MYVAR</env-key>\n"
            "              <value>value</value>\n"
            "            </env-key>\n"
            "          </env-key-list>\n"
            "          <env-key-list>\n"
            "            <_id>2</_id>\n"
            "            <env-key>\n"
            "              <env-key>ANOTHERVAR</env-key>\n"
            "              <value>\"\"</value>\n"
            "            </env-key>\n"
            "          </env-key-list>\n"
            "        </entry-env>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>4</_id>\n"
            "        <entry-command>\n"
            "          <entry-command-kw>ExecStart</entry-command-kw>\n"
            "          <command>/usr/sbin/httpd</command>\n"
            "          <arguments>\n"
            "            <args-list>\n"
            "              <_id>1</_id>\n"
            "              <args>\n"
            "                <args>1</args>\n"
            "                <sto-value>$OPTIONS</sto-value>\n"
            "              </args>\n"
            "            </args-list>\n"
            "            <args-list>\n"
            "              <_id>2</_id>\n"
            "              <args>\n"
            "                <args>2</args>\n"
            "                <sto-value>-DFOREGROUND</sto-value>\n"
            "              </args>\n"
            "            </args-list>\n"
            "          </arguments>\n"
            "        </entry-command>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>5</_id>\n"
            "        <entry-command>\n"
            "          <entry-command-kw>ExecReload</entry-command-kw>\n"
            "          <command>/usr/sbin/httpd</command>\n"
            "          <arguments>\n"
            "            <args-list>\n"
            "              <_id>1</_id>\n"
            "              <args>\n"
            "                <args>1</args>\n"
            "                <sto-value>$OPTIONS</sto-value>\n"
            "              </args>\n"
            "            </args-list>\n"
            "            <args-list>\n"
            "              <_id>2</_id>\n"
            "              <args>\n"
            "                <args>2</args>\n"
            "                <sto-value>-k</sto-value>\n"
            "              </args>\n"
            "            </args-list>\n"
            "            <args-list>\n"
            "              <_id>3</_id>\n"
            "              <args>\n"
            "                <args>3</args>\n"
            "                <sto-value>graceful</sto-value>\n"
            "              </args>\n"
            "            </args-list>\n"
            "          </arguments>\n"
            "        </entry-command>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>6</_id>\n"
            "        <entry-command>\n"
            "          <entry-command-kw>ExecStop</entry-command-kw>\n"
            "          <command>/bin/kill</command>\n"
            "          <arguments>\n"
            "            <args-list>\n"
            "              <_id>1</_id>\n"
            "              <args>\n"
            "                <args>1</args>\n"
            "                <sto-value>-WINCH</sto-value>\n"
            "              </args>\n"
            "            </args-list>\n"
            "            <args-list>\n"
            "              <_id>2</_id>\n"
            "              <args>\n"
            "                <args>2</args>\n"
            "                <sto-value>${MAINPID}</sto-value>\n"
            "              </args>\n"
            "            </args-list>\n"
            "          </arguments>\n"
            "        </entry-command>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>7</_id>\n"
            "        <entry-multi>\n"
            "          <entry-multi-kw>KillSignal</entry-multi-kw>\n"
            "          <value>SIGCONT</value>\n"
            "        </entry-multi>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>8</_id>\n"
            "        <entry-multi>\n"
            "          <entry-multi-kw>PrivateTmp</entry-multi-kw>\n"
            "          <value>true</value>\n"
            "        </entry-multi>\n"
            "      </config-entries>\n"
            "    </record>\n"
            "  </record-list>\n"
            "  <record-list>\n"
            "    <_id>3</_id>\n"
            "    <record>\n"
            "      <label>Install</label>\n"
            "      <config-entries>\n"
            "        <_id>1</_id>\n"
            "        <entry-multi>\n"
            "          <entry-multi-kw>WantedBy</entry-multi-kw>\n"
            "          <value>multi-user.target</value>\n"
            "        </entry-multi>\n"
            "      </config-entries>\n"
            "    </record>\n"
            "  </record-list>\n"
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
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='1']/record/config-entries[_id='3']"
            "/entry-multi/entry-multi-kw", "Documentation", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='1']/record/config-entries[_id='3']"
            "/entry-multi/value", "man:apache(8)", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='1']/record/config-entries[_id='3']"
            "/entry-multi/value", "man:httpd(8)", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "record-list[_id='1']/record/config-entries[_id='1']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='2']/record/config-entries[_id='2']"
            "/entry-multi/value", "/etc/sysconfig/apache", 0, &entries));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='4']/record/label", "Socket", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='4']/record/config-entries[_id='1']"
            "/entry-multi/entry-multi-kw", "ListenStream", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='4']/record/config-entries[_id='1']"
            "/entry-multi/value", "/run/www/apache.socket", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "record-list[_id='1']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "2a3\n"
            "> Documentation=man:apache(8) man:httpd(8)\n"
            "4a6,7\n"
            "> [Socket]\n"
            "> ListenStream=/run/www/apache.socket\n"
            "7c10\n"
            "< EnvironmentFile = /etc/sysconfig/httpd\n"
            "---\n"
            "> EnvironmentFile = /etc/sysconfig/httpd /etc/sysconfig/apache\n"));
}

static void
test_store_modify(void **state)
{
    struct tstate *st = (struct tstate *)*state;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* modify some values */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='1']/record/config-entries[_id='3']"
            "/entry-single/value", "Apache", LYD_NEW_PATH_UPDATE, NULL));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='2']/record/config-entries[_id='2']"
            "/entry-multi/entry-multi-kw", "ReadWritePaths", LYD_NEW_PATH_UPDATE, NULL));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='2']/record/config-entries[_id='6']"
            "/entry-command/arguments/args-list[_id='2']/args/sto-value", "${CHILDPID}", LYD_NEW_PATH_UPDATE, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "3a4\n"
            "> Description=Apache\n"
            "7c8\n"
            "< EnvironmentFile = /etc/sysconfig/httpd\n"
            "---\n"
            "> ReadWritePaths=/etc/sysconfig/httpd\n"
            "11c12\n"
            "< ExecStop = /bin/kill -WINCH ${MAINPID}\n"
            "---\n"
            "> ExecStop = /bin/kill -WINCH ${CHILDPID}\n"));
}

static void
test_store_remove(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *node;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* remove list values */
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "record-list[_id='2']/record/config-entries[_id='5']"
            "/entry-command/arguments/args-list[_id='2']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "record-list[_id='2']/record/config-entries[_id='7']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "record-list[_id='1']/record/config-entries[_id='2']"
            "/entry-multi/value[.='network.target']", 0, &node));
    lyd_free_tree(node);

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "3c3\n"
            "< After = network.target remote-fs.target nss-lookup.target\n"
            "---\n"
            "> After = remote-fs.target nss-lookup.target\n"
            "10c10\n"
            "< ExecReload = /usr/sbin/httpd $OPTIONS -k graceful\n"
            "---\n"
            "> ExecReload = /usr/sbin/httpd $OPTIONS graceful\n"
            "12d11\n"
            "< KillSignal = SIGCONT\n"));
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
