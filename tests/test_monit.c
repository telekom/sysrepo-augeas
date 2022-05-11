/**
 * @file test_monit.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief monit SR DS plugin test
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
#define AUG_TEST_INPUT_FILES AUG_CONFIG_FILES_DIR "/monit"
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

#define AUG_TEST_MODULE "monit"

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
            "    <set>\n"
            "      <value>\n"
            "        <word>alert</word>\n"
            "        <sto-to-spc>root@localhost</sto-to-spc>\n"
            "      </value>\n"
            "    </set>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>2</_id>\n"
            "    <include>/my/monit/conf</include>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>3</_id>\n"
            "    <service>\n"
            "      <value-list>\n"
            "        <_id>1</_id>\n"
            "        <value>\n"
            "          <word>process</word>\n"
            "          <sto-to-spc>sshd</sto-to-spc>\n"
            "        </value>\n"
            "      </value-list>\n"
            "      <value-list>\n"
            "        <_id>2</_id>\n"
            "        <value>\n"
            "          <word>start</word>\n"
            "          <sto-to-spc>program \"/etc/init.d/ssh start\"</sto-to-spc>\n"
            "        </value>\n"
            "      </value-list>\n"
            "      <value-list>\n"
            "        <_id>3</_id>\n"
            "        <value>\n"
            "          <word>if</word>\n"
            "          <sto-to-spc>failed port 22 protocol ssh then restart</sto-to-spc>\n"
            "        </value>\n"
            "      </value-list>\n"
            "    </service>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>4</_id>\n"
            "    <service>\n"
            "      <value-list>\n"
            "        <_id>1</_id>\n"
            "        <value>\n"
            "          <word>process</word>\n"
            "          <sto-to-spc>httpd with pidfile /usr/local/apache2/logs/httpd.pid</sto-to-spc>\n"
            "        </value>\n"
            "      </value-list>\n"
            "      <value-list>\n"
            "        <_id>2</_id>\n"
            "        <value>\n"
            "          <word>group</word>\n"
            "          <sto-to-spc>www-data</sto-to-spc>\n"
            "        </value>\n"
            "      </value-list>\n"
            "      <value-list>\n"
            "        <_id>3</_id>\n"
            "        <value>\n"
            "          <word>start</word>\n"
            "          <sto-to-spc>program \"/usr/local/apache2/bin/apachectl start\"</sto-to-spc>\n"
            "        </value>\n"
            "      </value-list>\n"
            "      <value-list>\n"
            "        <_id>4</_id>\n"
            "        <value>\n"
            "          <word>stop</word>\n"
            "          <sto-to-spc>program \"/usr/local/apache2/bin/apachectl stop\"</sto-to-spc>\n"
            "        </value>\n"
            "      </value-list>\n"
            "    </service>\n"
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
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='5']/service/value-list[_id='1']/"
            "value/word", "process", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='5']/service/value-list[_id='1']/"
            "value/sto-to-spc", "flask", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='5']/service/value-list[_id='2']/"
            "value/word", "if", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='5']/service/value-list[_id='2']/"
            "value/sto-to-spc", "flask needed", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='1']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='6']/include", "/no/path", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='2']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='7']/set/value/word", "alarm", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='7']/set/value/sto-to-spc", "PID 256", 0, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "3a4,5\n"
            "> check process flask\n"
            ">  if flask needed\n"
            "4a7\n"
            "> include /no/path\n"
            "13a17\n"
            "> set alarm PID 256\n"));
}

static void
test_store_modify(void **state)
{
    struct tstate *st = (struct tstate *)*state;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* modify some values */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='1']/set/value/word", "signal",
            LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='3']/service/value-list[_id='3']/value/sto-to-spc",
            "failed port 22 protocol ssh then stop", LYD_NEW_PATH_UPDATE, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "3c3\n"
            "< set alert root@localhost\n"
            "---\n"
            "> set signal root@localhost\n"
            "8c8\n"
            "<  if failed port 22 protocol ssh then restart\n"
            "---\n"
            ">  if failed port 22 protocol ssh then stop\n"));
}

static void
test_store_remove(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *node;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* remove list values */
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='4']/service/value-list[_id='2']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='2']", 0, &node));
    lyd_free_tree(node);

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "4d3\n"
            "< include /my/monit/conf\n"
            "11d9\n"
            "<  group www-data\n"));
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
