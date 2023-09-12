/**
 * @file test_rtadvd.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief rtadvd SR DS plugin test
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
#define AUG_TEST_INPUT_FILES AUG_CONFIG_FILES_DIR "/rtadvd"
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

#define AUG_TEST_MODULE "rtadvd"

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
            "      <name-list>\n"
            "        <_id>1</_id>\n"
            "        <name>default</name>\n"
            "      </name-list>\n"
            "      <capability-list>\n"
            "        <_id>1</_id>\n"
            "        <capability>chlim#64</capability>\n"
            "      </capability-list>\n"
            "      <capability-list>\n"
            "        <_id>2</_id>\n"
            "        <capability>raflags#0</capability>\n"
            "      </capability-list>\n"
            "      <capability-list>\n"
            "        <_id>3</_id>\n"
            "        <capability>rltime#1800</capability>\n"
            "      </capability-list>\n"
            "      <capability-list>\n"
            "        <_id>4</_id>\n"
            "        <capability>rtime#0</capability>\n"
            "      </capability-list>\n"
            "      <capability-list>\n"
            "        <_id>5</_id>\n"
            "        <capability>retrans#0</capability>\n"
            "      </capability-list>\n"
            "      <capability-list>\n"
            "        <_id>6</_id>\n"
            "        <capability>pinfoflags=\"la\"</capability>\n"
            "      </capability-list>\n"
            "      <capability-list>\n"
            "        <_id>7</_id>\n"
            "        <capability>vltime#2592000</capability>\n"
            "      </capability-list>\n"
            "      <capability-list>\n"
            "        <_id>8</_id>\n"
            "        <capability>pltime#604800</capability>\n"
            "      </capability-list>\n"
            "      <capability-list>\n"
            "        <_id>9</_id>\n"
            "        <capability>mtu#0</capability>\n"
            "      </capability-list>\n"
            "    </record>\n"
            "  </record-list>\n"
            "  <record-list>\n"
            "    <_id>2</_id>\n"
            "    <record>\n"
            "      <name-list>\n"
            "        <_id>1</_id>\n"
            "        <name>ef0</name>\n"
            "      </name-list>\n"
            "      <capability-list>\n"
            "        <_id>1</_id>\n"
            "        <capability>addr=\"2001:db8:ffff:1000::\"</capability>\n"
            "      </capability-list>\n"
            "      <capability-list>\n"
            "        <_id>2</_id>\n"
            "        <capability>prefixlen#64</capability>\n"
            "      </capability-list>\n"
            "      <capability-list>\n"
            "        <_id>3</_id>\n"
            "        <capability>tc=default</capability>\n"
            "      </capability-list>\n"
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
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='1']/record/name-list[_id='2']/name",
            "loopback", 0, NULL));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='1']/record/capability-list[_id='10']/"
            "capability", "ttl#128", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "record-list[_id='1']/record/capability-list[_id='4']",
            0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='3']/record/name-list[_id='1']/name",
            "eth0", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='3']/record/capability-list[_id='1']/"
            "capability", "katimeout#20", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "record-list[_id='1']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "1,3c1,6\n"
            "< default:\\\n"
            "<         :chlim#64:raflags#0:rltime#1800:rtime#0:retrans#0:\\\n"
            "<         :pinfoflags=\"la\":vltime#2592000:pltime#604800:mtu#0:\n"
            "---\n"
            "> default|loopback:\\\n"
            ">         :chlim#64:raflags#0:rltime#1800:rtime#0:ttl#128:\\\n"
            ">         :retrans#0:pinfoflags=\"la\":vltime#2592000:pltime#604800:\\\n"
            "> \t:mtu#0:\n"
            "> eth0:\\\n"
            "> \t:katimeout#20:\n"));
}

static void
test_store_modify(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *entries, *node;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* modify some values */
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "record-list[_id='1']/record/name-list[_id='1']/name",
            0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='1']/record/name-list[_id='1']/name",
            "eth25", 0, NULL));

    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "record-list[_id='1']/record/capability-list[_id='5']/"
            "capability", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='1']/record/capability-list[_id='10']/"
            "capability", "retrans#5", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "record-list[_id='1']/record/capability-list[_id='4']",
            0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "1,2c1,2\n"
            "< default:\\\n"
            "<         :chlim#64:raflags#0:rltime#1800:rtime#0:retrans#0:\\\n"
            "---\n"
            "> eth25:\\\n"
            ">         :chlim#64:raflags#0:rltime#1800:rtime#0:retrans#5:\\\n"));
}

static void
test_store_remove(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *node;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* remove list values */
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "record-list[_id='1']/record/capability-list[_id='3']/"
            "capability", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "record-list[_id='1']/record/capability-list[_id='7']/"
            "capability", 0, &node));
    lyd_free_tree(node);

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "2,3c2,3\n"
            "<         :chlim#64:raflags#0:rltime#1800:rtime#0:retrans#0:\\\n"
            "<         :pinfoflags=\"la\":vltime#2592000:pltime#604800:mtu#0:\n"
            "---\n"
            ">         :chlim#64:raflags#0:rtime#0:retrans#0:pinfoflags=\"la\":\\\n"
            ">         :pltime#604800:mtu#0:\n"));
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
