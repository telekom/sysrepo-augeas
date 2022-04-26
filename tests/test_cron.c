/**
 * @file test_cron.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief cron SR DS plugin test
 *
 * @copyright
 * Copyright (c) 2021 - 2022 Deutsche Telekom AG.
 * Copyright (c) 2021 - 2022 CESNET, z.s.p.o.
 *
 * This source code is licensed under BSD 3-Clause License (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://opensource.org/licenses/BSD-3-Clause
 */

#include "tconfig.h"

/* augeas SR DS plugin */
#define AUG_TEST_INPUT_FILES AUG_CONFIG_FILES_DIR "/cron"
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

#define AUG_TEST_MODULE "cron"

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
            "    <shellvar>\n"
            "      <label>SHELL</label>\n"
            "      <value>/bin/sh</value>\n"
            "    </shellvar>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>2</_id>\n"
            "    <shellvar>\n"
            "      <label>PATH</label>\n"
            "      <value>/usr/local/sbin:/usr/local/bin:/sbin:/bin:/usr/sbin:/usr/bin</value>\n"
            "    </shellvar>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>3</_id>\n"
            "    <shellvar>\n"
            "      <label>CRON_TZ</label>\n"
            "      <value>America/Los_Angeles</value>\n"
            "    </shellvar>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>4</_id>\n"
            "    <shellvar>\n"
            "      <label>MAILTO</label>\n"
            "      <value>user1@tld1,user2@tld2;user3@tld3</value>\n"
            "    </shellvar>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>5</_id>\n"
            "    <entry>\n"
            "      <space-in>test -x /etc/init.d/anacron &amp;&amp; /usr/sbin/invoke-rc.d anacron start &gt;/dev/null</space-in>\n"
            "      <time>\n"
            "        <minute>30</minute>\n"
            "        <hour>7</hour>\n"
            "        <dayofmonth>*</dayofmonth>\n"
            "        <month>*</month>\n"
            "        <dayofweek>*</dayofweek>\n"
            "      </time>\n"
            "      <user>root</user>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>6</_id>\n"
            "    <entry>\n"
            "      <space-in>somecommand</space-in>\n"
            "      <time>\n"
            "        <minute>00</minute>\n"
            "        <hour>*/3</hour>\n"
            "        <dayofmonth>15-25/2</dayofmonth>\n"
            "        <month>May</month>\n"
            "        <dayofweek>1-5</dayofweek>\n"
            "      </time>\n"
            "      <user>user</user>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>7</_id>\n"
            "    <entry>\n"
            "      <space-in>somecommand</space-in>\n"
            "      <time>\n"
            "        <minute>00</minute>\n"
            "        <hour>*/3</hour>\n"
            "        <dayofmonth>15-25/2</dayofmonth>\n"
            "        <month>May</month>\n"
            "        <dayofweek>mon-tue</dayofweek>\n"
            "      </time>\n"
            "      <user>user</user>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>8</_id>\n"
            "    <entry>\n"
            "      <space-in>a command</space-in>\n"
            "      <schedule>yearly</schedule>\n"
            "      <user>foo</user>\n"
            "    </entry>\n"
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

    /* add some lists */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='9']/shellvar/label", "MYVAR", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='9']/shellvar/value", "myvalue", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='2']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='10']/entry/space-in", "rm -rf /", 0,
            &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='10']/entry/schedule", "reboot", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='10']/entry/user", "nobody", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='7']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='11']/entry/space-in", "echo \"hello\"",
            0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='11']/entry/time/minute", "00", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='11']/entry/time/hour", "*/6", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='11']/entry/time/dayofmonth", "*", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='11']/entry/time/month", "7-8", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='11']/entry/time/dayofweek", "*", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='11']/entry/user", "greeter", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='5']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_before(node, entries));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "4a5\n"
            "> MYVAR=myvalue\n"
            "6a8\n"
            "> 00 */6 * 7-8 * greeter echo \"hello\"\n"
            "10a13\n"
            "> @reboot nobody rm -rf /\n"));
}

static void
test_store_modify(void **state)
{
    struct tstate *st = (struct tstate *)*state;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* modify some data */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='2']/shellvar/value",
            "/usr/local/bin:/bin:/usr/bin", LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='6']/entry/space-in", "shutdown now",
            LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='8']/entry/schedule", "annually",
            LYD_NEW_PATH_UPDATE, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "4c4\n"
            "< PATH=/usr/local/sbin:/usr/local/bin:/sbin:/bin:/usr/sbin:/usr/bin\n"
            "---\n"
            "> PATH=/usr/local/bin:/bin:/usr/bin\n"
            "9c9\n"
            "< 00 */3 15-25/2 May 1-5 user somecommand\n"
            "---\n"
            "> 00 */3 15-25/2 May 1-5 user shutdown now\n"
            "12c12\n"
            "< @yearly foo a command\n"
            "---\n"
            "> @annually foo a command\n"));
}

static void
test_store_remove(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *node;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* remove data */
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='4']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='5']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='8']", 0, &node));
    lyd_free_tree(node);

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "6d5\n"
            "< MAILTO=user1@tld1,user2@tld2;user3@tld3\n"
            "8d6\n"
            "< 30 7 * * * root test -x /etc/init.d/anacron && /usr/sbin/invoke-rc.d anacron start >/dev/null\n"
            "12d9\n"
            "< @yearly foo a command\n"));
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
