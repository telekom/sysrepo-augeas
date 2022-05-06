/**
 * @file test_inittab.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief inittab SR DS plugin test
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
#define AUG_TEST_INPUT_FILES AUG_CONFIG_FILES_DIR "/inittab"
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

#define AUG_TEST_MODULE "inittab"

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
            "      <id>ap</id>\n"
            "      <runlevels/>\n"
            "      <action>sysinit</action>\n"
            "      <process>/sbin/autopush -f /etc/iu.ap</process>\n"
            "    </record>\n"
            "  </record-list>\n"
            "  <record-list>\n"
            "    <_id>2</_id>\n"
            "    <record>\n"
            "      <id>ap</id>\n"
            "      <runlevels/>\n"
            "      <action>sysinit</action>\n"
            "      <process>/sbin/soconfig -f /etc/sock2path</process>\n"
            "    </record>\n"
            "  </record-list>\n"
            "  <record-list>\n"
            "    <_id>3</_id>\n"
            "    <record>\n"
            "      <id>fs</id>\n"
            "      <runlevels/>\n"
            "      <action>sysinit</action>\n"
            "      <process>/sbin/rcS sysinit   &gt;/dev/msglog 2&lt;&gt;/dev/msglog &lt;/dev/console</process>\n"
            "    </record>\n"
            "  </record-list>\n"
            "  <record-list>\n"
            "    <_id>4</_id>\n"
            "    <record>\n"
            "      <id>is</id>\n"
            "      <runlevels>3</runlevels>\n"
            "      <action>initdefault</action>\n"
            "      <process/>\n"
            "    </record>\n"
            "  </record-list>\n"
            "  <record-list>\n"
            "    <_id>5</_id>\n"
            "    <record>\n"
            "      <id>p3</id>\n"
            "      <runlevels>s1234</runlevels>\n"
            "      <action>powerfail</action>\n"
            "      <process>/usr/sbin/shutdown -y -i5 -g0 &gt;/dev/msglog 2&lt;&gt;/dev/...</process>\n"
            "    </record>\n"
            "  </record-list>\n"
            "  <record-list>\n"
            "    <_id>6</_id>\n"
            "    <record>\n"
            "      <id>sS</id>\n"
            "      <runlevels>s</runlevels>\n"
            "      <action>wait</action>\n"
            "      <process>/sbin/rcS              &gt;/dev/msglog 2&lt;&gt;/dev/msglog &lt;/dev/console</process>\n"
            "    </record>\n"
            "  </record-list>\n"
            "  <record-list>\n"
            "    <_id>7</_id>\n"
            "    <record>\n"
            "      <id>s0</id>\n"
            "      <runlevels>0</runlevels>\n"
            "      <action>wait</action>\n"
            "      <process>/sbin/rc0              &gt;/dev/msglog 2&lt;&gt;/dev/msglog &lt;/dev/console</process>\n"
            "    </record>\n"
            "  </record-list>\n"
            "  <record-list>\n"
            "    <_id>8</_id>\n"
            "    <record>\n"
            "      <id>s1</id>\n"
            "      <runlevels>1</runlevels>\n"
            "      <action>respawn</action>\n"
            "      <process>/sbin/rc1           &gt;/dev/msglog 2&lt;&gt;/dev/msglog &lt;/dev/console</process>\n"
            "    </record>\n"
            "  </record-list>\n"
            "  <record-list>\n"
            "    <_id>9</_id>\n"
            "    <record>\n"
            "      <id>s2</id>\n"
            "      <runlevels>23</runlevels>\n"
            "      <action>wait</action>\n"
            "      <process>/sbin/rc2             &gt;/dev/msglog 2&lt;&gt;/dev/msglog &lt;/dev/console</process>\n"
            "    </record>\n"
            "  </record-list>\n"
            "  <record-list>\n"
            "    <_id>10</_id>\n"
            "    <record>\n"
            "      <id>s3</id>\n"
            "      <runlevels>3</runlevels>\n"
            "      <action>wait</action>\n"
            "      <process>/sbin/rc3             &gt;/dev/msglog 2&lt;&gt;/dev/msglog &lt;/dev/console</process>\n"
            "    </record>\n"
            "  </record-list>\n"
            "  <record-list>\n"
            "    <_id>11</_id>\n"
            "    <record>\n"
            "      <id>s5</id>\n"
            "      <runlevels>5</runlevels>\n"
            "      <action>wait</action>\n"
            "      <process>/sbin/rc5             &gt;/dev/msglog 2&lt;&gt;/dev/msglog &lt;/dev/console</process>\n"
            "    </record>\n"
            "  </record-list>\n"
            "  <record-list>\n"
            "    <_id>12</_id>\n"
            "    <record>\n"
            "      <id>s6</id>\n"
            "      <runlevels>6</runlevels>\n"
            "      <action>wait</action>\n"
            "      <process>/sbin/rc6             &gt;/dev/msglog 2&lt;&gt;/dev/msglog &lt;/dev/console</process>\n"
            "    </record>\n"
            "  </record-list>\n"
            "  <record-list>\n"
            "    <_id>13</_id>\n"
            "    <record>\n"
            "      <id>fw</id>\n"
            "      <runlevels>0</runlevels>\n"
            "      <action>wait</action>\n"
            "      <process>/sbin/uadmin 2 0      &gt;/dev/msglog 2&lt;&gt;/dev/msglog &lt;/dev/console</process>\n"
            "    </record>\n"
            "  </record-list>\n"
            "  <record-list>\n"
            "    <_id>14</_id>\n"
            "    <record>\n"
            "      <id>of</id>\n"
            "      <runlevels>5</runlevels>\n"
            "      <action>wait</action>\n"
            "      <process>/sbin/uadmin 2 6      &gt;/dev/msglog 2&lt;&gt;/dev/msglog &lt;/dev/console</process>\n"
            "    </record>\n"
            "  </record-list>\n"
            "  <record-list>\n"
            "    <_id>15</_id>\n"
            "    <record>\n"
            "      <id>rb</id>\n"
            "      <runlevels>6</runlevels>\n"
            "      <action>wait</action>\n"
            "      <process>/sbin/uadmin 2 1      &gt;/dev/msglog 2&lt;&gt;/dev/msglog &lt;/dev/console</process>\n"
            "    </record>\n"
            "  </record-list>\n"
            "  <record-list>\n"
            "    <_id>16</_id>\n"
            "    <record>\n"
            "      <id>sc</id>\n"
            "      <runlevels>234</runlevels>\n"
            "      <action>respawn</action>\n"
            "      <process>/usr/lib/saf/sac -t 300</process>\n"
            "    </record>\n"
            "  </record-list>\n"
            "  <record-list>\n"
            "    <_id>17</_id>\n"
            "    <record>\n"
            "      <id>co</id>\n"
            "      <runlevels>234</runlevels>\n"
            "      <action>respawn</action>\n"
            "      <process>/usr/lib/saf/ttymon -g -h -p \"`uname -n` console login: \" -T terminal-type -d /dev/console -l console -m ldterm,ttcompat</process>\n"
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
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='18']/record/id", "my", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='18']/record/runlevels", NULL, 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='18']/record/action", "ignore", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='18']/record/process", NULL, 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "record-list[_id='16']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='19']/record/id", "ap", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='19']/record/runlevels", NULL, 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='19']/record/action", "sysinit", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='19']/record/process",
            "/usr/sbin/shutdown now", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "record-list[_id='1']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_before(node, entries));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "0a1\n"
            "> ap::sysinit:/usr/sbin/shutdown now\n"
            "16a18\n"
            "> my::ignore:\n"));
}

static void
test_store_modify(void **state)
{
    struct tstate *st = (struct tstate *)*state;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* modify some values */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='1']/record/runlevels", "7",
            LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='13']/record/action", "kill",
            LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "record-list[_id='16']/record/process", NULL,
            LYD_NEW_PATH_UPDATE, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "1c1\n"
            "< ap::sysinit:/sbin/autopush -f /etc/iu.ap\n"
            "---\n"
            "> ap:7:sysinit:/sbin/autopush -f /etc/iu.ap\n"
            "13c13\n"
            "< fw:0:wait:/sbin/uadmin 2 0      >/dev/msglog 2<>/dev/msglog </dev/console\n"
            "---\n"
            "> fw:0:kill:/sbin/uadmin 2 0      >/dev/msglog 2<>/dev/msglog </dev/console\n"
            "16c16\n"
            "< sc:234:respawn:/usr/lib/saf/sac -t 300\n"
            "---\n"
            "> sc:234:respawn:\n"));
}

static void
test_store_remove(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *node;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* remove list values */
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "record-list[_id='14']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "record-list[_id='1']", 0, &node));
    lyd_free_tree(node);

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "1d0\n"
            "< ap::sysinit:/sbin/autopush -f /etc/iu.ap\n"
            "14d12\n"
            "< of:5:wait:/sbin/uadmin 2 6      >/dev/msglog 2<>/dev/msglog </dev/console\n"));
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
