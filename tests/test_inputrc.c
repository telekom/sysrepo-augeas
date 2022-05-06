/**
 * @file test_inputrc.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief inputrc SR DS plugin test
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
#define AUG_TEST_INPUT_FILES AUG_CONFIG_FILES_DIR "/inputrc"
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

#define AUG_TEST_MODULE "inputrc"

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
            "    <variable>\n"
            "      <label>input-meta</label>\n"
            "      <word>on</word>\n"
            "    </variable>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>2</_id>\n"
            "    <variable>\n"
            "      <label>output-meta</label>\n"
            "      <word>on</word>\n"
            "    </variable>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>3</_id>\n"
            "    <variable>\n"
            "      <label>convert-meta</label>\n"
            "      <word>off</word>\n"
            "    </variable>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>4</_id>\n"
            "    <variable>\n"
            "      <label>bell-style</label>\n"
            "      <word>none</word>\n"
            "    </variable>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>5</_id>\n"
            "    <if-list>\n"
            "      <_r-id>1</_r-id>\n"
            "      <if>\n"
            "        <space-in>mode=emacs</space-in>\n"
            "        <config-entries>\n"
            "          <_id>1</_id>\n"
            "          <entry>\n"
            "            <value>\\e[1~</value>\n"
            "            <mapping>beginning-of-line</mapping>\n"
            "          </entry>\n"
            "        </config-entries>\n"
            "        <config-entries>\n"
            "          <_id>2</_id>\n"
            "          <entry>\n"
            "            <value>\\e[4~</value>\n"
            "            <mapping>end-of-line</mapping>\n"
            "          </entry>\n"
            "        </config-entries>\n"
            "        <config-entries>\n"
            "          <_id>3</_id>\n"
            "          <entry>\n"
            "            <value>\\e[1;5C</value>\n"
            "            <mapping>forward-word</mapping>\n"
            "          </entry>\n"
            "        </config-entries>\n"
            "        <config-entries>\n"
            "          <_id>4</_id>\n"
            "          <entry>\n"
            "            <value>\\e[1;5D</value>\n"
            "            <mapping>backward-word</mapping>\n"
            "          </entry>\n"
            "        </config-entries>\n"
            "        <config-entries>\n"
            "          <_id>5</_id>\n"
            "          <entry>\n"
            "            <value>\\e[5C</value>\n"
            "            <mapping>forward-word</mapping>\n"
            "          </entry>\n"
            "        </config-entries>\n"
            "        <config-entries>\n"
            "          <_id>6</_id>\n"
            "          <entry>\n"
            "            <value>\\e[5D</value>\n"
            "            <mapping>backward-word</mapping>\n"
            "          </entry>\n"
            "        </config-entries>\n"
            "        <config-entries>\n"
            "          <_id>7</_id>\n"
            "          <entry>\n"
            "            <value>\\e\\e[C</value>\n"
            "            <mapping>forward-word</mapping>\n"
            "          </entry>\n"
            "        </config-entries>\n"
            "        <config-entries>\n"
            "          <_id>8</_id>\n"
            "          <entry>\n"
            "            <value>\\e\\e[D</value>\n"
            "            <mapping>backward-word</mapping>\n"
            "          </entry>\n"
            "        </config-entries>\n"
            "        <config-entries>\n"
            "          <_id>9</_id>\n"
            "          <_if-ref>2</_if-ref>\n"
            "        </config-entries>\n"
            "        <config-entries>\n"
            "          <_id>10</_id>\n"
            "          <entry>\n"
            "            <value>\\eOH</value>\n"
            "            <mapping>beginning-of-line</mapping>\n"
            "          </entry>\n"
            "        </config-entries>\n"
            "        <config-entries>\n"
            "          <_id>11</_id>\n"
            "          <entry>\n"
            "            <value>\\eOF</value>\n"
            "            <mapping>end-of-line</mapping>\n"
            "          </entry>\n"
            "        </config-entries>\n"
            "        <config-entries>\n"
            "          <_id>12</_id>\n"
            "          <entry>\n"
            "            <value>\\e[H</value>\n"
            "            <mapping>beginning-of-line</mapping>\n"
            "          </entry>\n"
            "        </config-entries>\n"
            "        <config-entries>\n"
            "          <_id>13</_id>\n"
            "          <entry>\n"
            "            <value>\\e[F</value>\n"
            "            <mapping>end-of-line</mapping>\n"
            "          </entry>\n"
            "        </config-entries>\n"
            "      </if>\n"
            "    </if-list>\n"
            "    <if-list>\n"
            "      <_r-id>2</_r-id>\n"
            "      <if>\n"
            "        <space-in>term=rxvt</space-in>\n"
            "        <config-entries>\n"
            "          <_id>1</_id>\n"
            "          <entry>\n"
            "            <value>\\e[8~</value>\n"
            "            <mapping>end-of-line</mapping>\n"
            "          </entry>\n"
            "        </config-entries>\n"
            "        <config-entries>\n"
            "          <_id>2</_id>\n"
            "          <entry>\n"
            "            <value>\\eOc</value>\n"
            "            <mapping>forward-word</mapping>\n"
            "          </entry>\n"
            "        </config-entries>\n"
            "        <config-entries>\n"
            "          <_id>3</_id>\n"
            "          <entry>\n"
            "            <value>\\eOd</value>\n"
            "            <mapping>backward-word</mapping>\n"
            "          </entry>\n"
            "        </config-entries>\n"
            "        <else>\n"
            "          <config-entries>\n"
            "            <_id>1</_id>\n"
            "            <entry>\n"
            "              <value>\\e[G</value>\n"
            "              <mapping>\",\"</mapping>\n"
            "            </entry>\n"
            "          </config-entries>\n"
            "        </else>\n"
            "      </if>\n"
            "    </if-list>\n"
            "  </config-entries>\n"
            "</" AUG_TEST_MODULE ">\n");
    free(str);
}

int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_teardown(test_load, tteardown),
    };

    return cmocka_run_group_tests(tests, setup_f, tteardown_glob);
}
