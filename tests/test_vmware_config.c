/**
 * @file test_vmware_config.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief vmware-config SR DS plugin test
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
#define AUG_TEST_INPUT_FILES AUG_CONFIG_FILES_DIR "/vmware-config"
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

#define AUG_TEST_MODULE "vmware-config"

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
            "      <word>libdir</word>\n"
            "      <value>/usr/lib/vmware</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>2</_id>\n"
            "    <entry>\n"
            "      <word>dhcpd.fullpath</word>\n"
            "      <value>/usr/bin/vmnet-dhcpd</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>3</_id>\n"
            "    <entry>\n"
            "      <word>authd.fullpath</word>\n"
            "      <value>/usr/sbin/vmware-authd</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>4</_id>\n"
            "    <entry>\n"
            "      <word>authd.client.port</word>\n"
            "      <value>902</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>5</_id>\n"
            "    <entry>\n"
            "      <word>loop.fullpath</word>\n"
            "      <value>/usr/bin/vmware-loop</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>6</_id>\n"
            "    <entry>\n"
            "      <word>vmware.fullpath</word>\n"
            "      <value>/usr/bin/vmware</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>7</_id>\n"
            "    <entry>\n"
            "      <word>control.fullpath</word>\n"
            "      <value>/usr/bin/vmware-cmd</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>8</_id>\n"
            "    <entry>\n"
            "      <word>serverd.fullpath</word>\n"
            "      <value>/usr/sbin/vmware-serverd</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>9</_id>\n"
            "    <entry>\n"
            "      <word>wizard.fullpath</word>\n"
            "      <value>/usr/bin/vmware-wizard</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>10</_id>\n"
            "    <entry>\n"
            "      <word>serverd.init.fullpath</word>\n"
            "      <value>/usr/lib/vmware/serverd/init.pl</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>11</_id>\n"
            "    <entry>\n"
            "      <word>serverd.vpxuser</word>\n"
            "      <value>vpxuser</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>12</_id>\n"
            "    <entry>\n"
            "      <word>serverd.snmpdconf.subagentenabled</word>\n"
            "      <value>TRUE</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>13</_id>\n"
            "    <entry>\n"
            "      <word>template.useFlatDisks</word>\n"
            "      <value>TRUE</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>14</_id>\n"
            "    <entry>\n"
            "      <word>autoStart.defaultStartDelay</word>\n"
            "      <value>60</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>15</_id>\n"
            "    <entry>\n"
            "      <word>autoStart.enabled</word>\n"
            "      <value>True</value>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>16</_id>\n"
            "    <entry>\n"
            "      <word>autoStart.defaultStopDelay</word>\n"
            "      <value>60</value>\n"
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
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_id='17']/entry/word", "group.option", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_id='17']/entry/value", "off", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "entry-list[_id='12']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "12a13\n"
            "> group.option = \"off\"\n"));
}

static void
test_store_modify(void **state)
{
    struct tstate *st = (struct tstate *)*state;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* modify some values */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_id='1']/entry/word", "librarydir",
            LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_id='3']/entry/value", "/usr/sbin/vmw-authd",
            LYD_NEW_PATH_UPDATE, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "1c1\n"
            "< libdir = \"/usr/lib/vmware\"\n"
            "---\n"
            "> librarydir = \"/usr/lib/vmware\"\n"
            "3c3\n"
            "< authd.fullpath = \"/usr/sbin/vmware-authd\"\n"
            "---\n"
            "> authd.fullpath = \"/usr/sbin/vmw-authd\"\n"));
}

static void
test_store_remove(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *node;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* remove list values */
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "entry-list[_id='2']", 0, &node));
    lyd_free_tree(node);

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "2d1\n"
            "< dhcpd.fullpath = \"/usr/bin/vmnet-dhcpd\"\n"));
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
