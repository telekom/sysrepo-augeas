/**
 * @file test_xymon.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief xymon SR DS plugin test
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
#define AUG_TEST_INPUT_FILES AUG_CONFIG_FILES_DIR "/xymon"
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

#define AUG_TEST_MODULE "xymon"

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
            "    <title>test title</title>\n"
            "  </config-entries>\n"
            "  <config-entries2>\n"
            "    <_id>1</_id>\n"
            "    <page>\n"
            "      <page-name>page1</page-name>\n"
            "      <pagetitle>'This is a test page'</pagetitle>\n"
            "      <config-entries>\n"
            "        <_id>1</_id>\n"
            "        <host>\n"
            "          <ip>1.1.1.1</ip>\n"
            "          <fqdn>testhost.localdomain</fqdn>\n"
            "          <tag>test1</tag>\n"
            "          <tag>test2</tag>\n"
            "          <tag>http:443</tag>\n"
            "          <tag>ldaps=testhost.localdomain</tag>\n"
            "          <tag>http://testhost.localdomain</tag>\n"
            "        </host>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>2</_id>\n"
            "        <host>\n"
            "          <ip>2.2.2.2</ip>\n"
            "          <fqdn>testhost2.local.domain</fqdn>\n"
            "          <tag>COMMENT:stuff</tag>\n"
            "          <tag>apache=wow</tag>\n"
            "        </host>\n"
            "      </config-entries>\n"
            "    </page>\n"
            "  </config-entries2>\n"
            "  <config-entries2>\n"
            "    <_id>2</_id>\n"
            "    <page>\n"
            "      <page-name>newpage</page-name>\n"
            "      <config-entries>\n"
            "        <_id>1</_id>\n"
            "        <host>\n"
            "          <ip>1.1.1.1</ip>\n"
            "          <fqdn>testhost.localdomain</fqdn>\n"
            "          <tag>test1</tag>\n"
            "          <tag>test2</tag>\n"
            "          <tag>http:443</tag>\n"
            "          <tag>ldaps=testhost.localdomain</tag>\n"
            "          <tag>http://testhost.localdomain</tag>\n"
            "        </host>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>2</_id>\n"
            "        <host>\n"
            "          <ip>2.2.2.2</ip>\n"
            "          <fqdn>testhost2.local.domain</fqdn>\n"
            "          <tag>COMMENT:stuff</tag>\n"
            "          <tag>apache=wow</tag>\n"
            "        </host>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>3</_id>\n"
            "        <title>test title</title>\n"
            "      </config-entries>\n"
            "      <ch-group-list>\n"
            "        <_id>1</_id>\n"
            "        <group>\n"
            "          <value-to-eol>group1</value-to-eol>\n"
            "          <config-entries>\n"
            "            <_id>1</_id>\n"
            "            <host>\n"
            "              <ip>3.3.3.3</ip>\n"
            "              <fqdn>host1</fqdn>\n"
            "            </host>\n"
            "          </config-entries>\n"
            "          <config-entries>\n"
            "            <_id>2</_id>\n"
            "            <host>\n"
            "              <ip>4.4.4.4</ip>\n"
            "              <fqdn>host2</fqdn>\n"
            "            </host>\n"
            "          </config-entries>\n"
            "        </group>\n"
            "      </ch-group-list>\n"
            "      <ch-group-list>\n"
            "        <_id>2</_id>\n"
            "        <group-sorted>\n"
            "          <value-to-eol>group2</value-to-eol>\n"
            "          <config-entries>\n"
            "            <_id>1</_id>\n"
            "            <host>\n"
            "              <ip>5.5.5.5</ip>\n"
            "              <fqdn>host3</fqdn>\n"
            "              <tag>conn</tag>\n"
            "            </host>\n"
            "          </config-entries>\n"
            "          <config-entries>\n"
            "            <_id>2</_id>\n"
            "            <host>\n"
            "              <ip>6.6.6.6</ip>\n"
            "              <fqdn>host4</fqdn>\n"
            "              <tag>ssh</tag>\n"
            "            </host>\n"
            "          </config-entries>\n"
            "        </group-sorted>\n"
            "      </ch-group-list>\n"
            "    </page>\n"
            "  </config-entries2>\n"
            "  <config-entries2>\n"
            "    <_id>3</_id>\n"
            "    <subparent>\n"
            "      <parent>page1</parent>\n"
            "      <page-name>page2</page-name>\n"
            "      <pagetitle>This is after page 1</pagetitle>\n"
            "      <config-entries>\n"
            "        <_id>1</_id>\n"
            "        <host>\n"
            "          <ip>10.0.0.1</ip>\n"
            "          <fqdn>router1.loni.org</fqdn>\n"
            "        </host>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>2</_id>\n"
            "        <host>\n"
            "          <ip>10.0.0.2</ip>\n"
            "          <fqdn>sw1.localdomain</fqdn>\n"
            "        </host>\n"
            "      </config-entries>\n"
            "    </subparent>\n"
            "  </config-entries2>\n"
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
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='2']/netinclude",
            "scp://localhost/config", 0, NULL));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "ch-group-list[_id='1']/group-only/value-to-eol", "grp", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "ch-group-list[_id='1']/group-only/col", "col1", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "ch-group-list[_id='1']/group-only/col", "col2", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "ch-group-list[_id='1']/group-only/config-entries[_id='1']/"
            "host/ip", "10.0.0.1", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "ch-group-list[_id='1']/group-only/config-entries[_id='1']/"
            "host/fqdn", "hhost", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "ch-group-list[_id='1']/group-only/config-entries[_id='1']/"
            "host/tag", "no-tag", 0, NULL));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries2[_id='4']/subpage/page-name", "my-spage",
            0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries2[_id='4']/subpage/pagetitle", "spage-title",
            0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries2[_id='4']/subpage/config-entries[_id='1']/"
            "directory", "mydir", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries2[_id='1']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries2[_id='2']/page/ch-group-list[_id='1']/"
            "group/config-entries[_id='3']/host/ip", "1.1.2.2", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries2[_id='2']/page/ch-group-list[_id='1']/"
            "group/config-entries[_id='3']/host/fqdn", "host12", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries2[_id='2']/page/ch-group-list[_id='1']/"
            "group/config-entries[_id='1']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "3a4,6\n"
            "> netinclude scp://localhost/config\n"
            "> group-only col1|col2 grp\n"
            "> 10.0.0.1 hhost # no-tag\n"
            "8a12,13\n"
            "> subpage my-spage spage-title\n"
            "> directory mydir\n"
            "15a21\n"
            "> 1.1.2.2 host12 #\n"));
}

static void
test_store_modify(void **state)
{
    struct tstate *st = (struct tstate *)*state;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* modify some values */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries2[_id='2']/page/ch-group-list[_id='2']/"
            "group-sorted/value-to-eol", "group22", LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries2[_id='3']/subparent/pagetitle",
            "This is still after page 1", LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries2[_id='2']/page/config-entries[_id='2']/"
            "host/ip", "2.2.2.3", LYD_NEW_PATH_UPDATE, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "11c11\n"
            "< 2.2.2.2     testhost2.local.domain # COMMENT:stuff apache=wow\n"
            "---\n"
            "> 2.2.2.3     testhost2.local.domain # COMMENT:stuff apache=wow\n"
            "18c18\n"
            "< group-sorted group2\n"
            "---\n"
            "> group-sorted group22\n"
            "22c22\n"
            "< subparent page1 page2 This is after page 1\n"
            "---\n"
            "> subparent page1 page2 This is still after page 1\n"));
}

static void
test_store_remove(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *node;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* remove list values */
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries2[_id='2']/page/config-entries[_id='1']/"
            "host/tag[.='test2']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries2[_id='2']/page/ch-group-list[_id='1']/"
            "group/config-entries[_id='1']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries2[_id='3']/subparent/config-entries[_id='1']",
            0, &node));
    lyd_free_tree(node);

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "10c10\n"
            "< 1.1.1.1  testhost.localdomain # test1 test2 http:443 ldaps=testhost.localdomain http://testhost.localdomain\n"
            "---\n"
            "> 1.1.1.1  testhost.localdomain # test1 http:443 ldaps=testhost.localdomain http://testhost.localdomain\n"
            "15d14\n"
            "< 3.3.3.3 host1 #\n"
            "23d21\n"
            "< 10.0.0.1 router1.loni.org #\n"));
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
