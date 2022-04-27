/**
 * @file test_devfsrules.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief devfsrules SR DS plugin test
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
#define AUG_TEST_INPUT_FILES AUG_CONFIG_FILES_DIR "/devfsrules"
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

#define AUG_TEST_MODULE "devfsrules"

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
            "      <word>devfsrules_jail_unhide_usb_printer_and_scanner</word>\n"
            "      <id>30</id>\n"
            "      <entry-list>\n"
            "        <_id>1</_id>\n"
            "        <entry>\n"
            "          <entry>1</entry>\n"
            "          <line-re>add include $devfsrules_hide_all</line-re>\n"
            "        </entry>\n"
            "      </entry-list>\n"
            "      <entry-list>\n"
            "        <_id>2</_id>\n"
            "        <entry>\n"
            "          <entry>2</entry>\n"
            "          <line-re>add include $devfsrules_unhide_basic</line-re>\n"
            "        </entry>\n"
            "      </entry-list>\n"
            "      <entry-list>\n"
            "        <_id>3</_id>\n"
            "        <entry>\n"
            "          <entry>3</entry>\n"
            "          <line-re>add include $devfsrules_unhide_login</line-re>\n"
            "        </entry>\n"
            "      </entry-list>\n"
            "      <entry-list>\n"
            "        <_id>4</_id>\n"
            "        <entry>\n"
            "          <entry>4</entry>\n"
            "          <line-re>add path 'ulpt*' mode 0660 group printscan unhide</line-re>\n"
            "        </entry>\n"
            "      </entry-list>\n"
            "      <entry-list>\n"
            "        <_id>5</_id>\n"
            "        <entry>\n"
            "          <entry>5</entry>\n"
            "          <line-re>add path 'unlpt*' mode 0660 group printscan unhide</line-re>\n"
            "        </entry>\n"
            "      </entry-list>\n"
            "      <entry-list>\n"
            "        <_id>6</_id>\n"
            "        <entry>\n"
            "          <entry>6</entry>\n"
            "          <line-re>add path 'ugen2.8' mode 0660 group printscan unhide</line-re>\n"
            "        </entry>\n"
            "      </entry-list>\n"
            "      <entry-list>\n"
            "        <_id>7</_id>\n"
            "        <entry>\n"
            "          <entry>7</entry>\n"
            "          <line-re>add path usb unhide</line-re>\n"
            "        </entry>\n"
            "      </entry-list>\n"
            "      <entry-list>\n"
            "        <_id>8</_id>\n"
            "        <entry>\n"
            "          <entry>8</entry>\n"
            "          <line-re>add path usbctl unhide</line-re>\n"
            "        </entry>\n"
            "      </entry-list>\n"
            "      <entry-list>\n"
            "        <_id>9</_id>\n"
            "        <entry>\n"
            "          <entry>9</entry>\n"
            "          <line-re>add path 'usb/2.8.0' mode 0660 group printscan unhide</line-re>\n"
            "        </entry>\n"
            "      </entry-list>\n"
            "    </record>\n"
            "  </record-list>\n"
            "  <record-list>\n"
            "    <_id>2</_id>\n"
            "    <record>\n"
            "      <word>devfsrules_jail_unhide_usb_scanner_only</word>\n"
            "      <id>30</id>\n"
            "      <entry-list>\n"
            "        <_id>1</_id>\n"
            "        <entry>\n"
            "          <entry>1</entry>\n"
            "          <line-re>add include $devfsrules_hide_all</line-re>\n"
            "        </entry>\n"
            "      </entry-list>\n"
            "      <entry-list>\n"
            "        <_id>2</_id>\n"
            "        <entry>\n"
            "          <entry>2</entry>\n"
            "          <line-re>add include $devfsrules_unhide_basic</line-re>\n"
            "        </entry>\n"
            "      </entry-list>\n"
            "      <entry-list>\n"
            "        <_id>3</_id>\n"
            "        <entry>\n"
            "          <entry>3</entry>\n"
            "          <line-re>add include $devfsrules_unhide_login</line-re>\n"
            "        </entry>\n"
            "      </entry-list>\n"
            "      <entry-list>\n"
            "        <_id>4</_id>\n"
            "        <entry>\n"
            "          <entry>4</entry>\n"
            "          <line-re>add path 'ugen2.8' mode 0660 group scan unhide</line-re>\n"
            "        </entry>\n"
            "      </entry-list>\n"
            "      <entry-list>\n"
            "        <_id>5</_id>\n"
            "        <entry>\n"
            "          <entry>5</entry>\n"
            "          <line-re>add path usb unhide</line-re>\n"
            "        </entry>\n"
            "      </entry-list>\n"
            "      <entry-list>\n"
            "        <_id>6</_id>\n"
            "        <entry>\n"
            "          <entry>6</entry>\n"
            "          <line-re>add path usbctl unhide</line-re>\n"
            "        </entry>\n"
            "      </entry-list>\n"
            "      <entry-list>\n"
            "        <_id>7</_id>\n"
            "        <entry>\n"
            "          <entry>7</entry>\n"
            "          <line-re>add path 'usb/2.8.0' mode 0660 group scan unhide</line-re>\n"
            "        </entry>\n"
            "      </entry-list>\n"
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
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='3']/record/word", "devfsrules_my_jail",
            0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='3']/record/id", "20", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='3']/record/entry-list[_id='1']/"
            "entry/entry", "1", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='3']/record/entry-list[_id='1']/"
            "entry/line-re", "add path mydev unhide", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "record-list[_id='1']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='2']/record/entry-list[_id='8']/"
            "entry/entry", "8", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='2']/record/entry-list[_id='8']/"
            "entry/line-re", "add include $var", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "record-list[_id='2']/record/entry-list[_id='3']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "11a12,13\n"
            "> [devfsrules_my_jail=20]\n"
            "> add path mydev unhide\n"
            "15a18\n"
            "> add include $var\n"));
}

static void
test_store_modify(void **state)
{
    struct tstate *st = (struct tstate *)*state;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* modify some values */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='1']/record/word",
            "devfsrules_jail_unhide_usb_printer", LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='2']/record/id", "25",
            LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='2']/record/entry-list[_id='4']/entry/line-re",
            "add path 'ugen2.8' mode 0600 unhide", LYD_NEW_PATH_UPDATE, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "1c1\n"
            "< [devfsrules_jail_unhide_usb_printer_and_scanner=30]\n"
            "---\n"
            "> [devfsrules_jail_unhide_usb_printer=30]\n"
            "12c12\n"
            "< [devfsrules_jail_unhide_usb_scanner_only=30]\n"
            "---\n"
            "> [devfsrules_jail_unhide_usb_scanner_only=25]\n"
            "16c16\n"
            "< add path 'ugen2.8' mode 0660 group scan unhide  # Scanner\n"
            "---\n"
            "> add path 'ugen2.8' mode 0600 unhide  # Scanner\n"));
}

static void
test_store_remove(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *node;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* remove list values */
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "record-list[_id='1']/record/entry-list[_id='7']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "record-list[_id='2']", 0, &node));
    lyd_free_tree(node);

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "8d7\n"
            "< add path usb unhide\n"
            "12,19d10\n"
            "< [devfsrules_jail_unhide_usb_scanner_only=30]\n"
            "< add include $devfsrules_hide_all\n"
            "< add include $devfsrules_unhide_basic\n"
            "< add include $devfsrules_unhide_login\n"
            "< add path 'ugen2.8' mode 0660 group scan unhide  # Scanner\n"
            "< add path usb unhide\n"
            "< add path usbctl unhide\n"
            "< add path 'usb/2.8.0' mode 0660 group scan unhide\n"));
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
