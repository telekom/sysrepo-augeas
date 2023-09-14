/**
 * @file test_aptconf.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief aptconf SR DS plugin test
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
#define AUG_TEST_INPUT_FILES AUG_CONFIG_FILES_DIR "/aptconf"
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

#define AUG_TEST_MODULE "aptconf"

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
            "    <name-list>\n"
            "      <_r-id>1</_r-id>\n"
            "      <name>\n"
            "        <name>APT</name>\n"
            "        <entry-noeol>\n"
            "          <_id>1</_id>\n"
            "          <_name-ref>2</_name-ref>\n"
            "        </entry-noeol>\n"
            "      </name>\n"
            "    </name-list>\n"
            "    <name-list>\n"
            "      <_r-id>2</_r-id>\n"
            "      <name>\n"
            "        <name>Update</name>\n"
            "        <entry-noeol>\n"
            "          <_id>1</_id>\n"
            "          <_name-ref>3</_name-ref>\n"
            "        </entry-noeol>\n"
            "      </name>\n"
            "    </name-list>\n"
            "    <name-list>\n"
            "      <_r-id>3</_r-id>\n"
            "      <name>\n"
            "        <name>Pre-Invoke</name>\n"
            "        <entry-noeol>\n"
            "          <_id>1</_id>\n"
            "          <elem>[ ! -e /run/systemd/system ] || [ $(id -u) -ne 0 ] || systemctl start --no-block apt-news.service esm-cache.service || true</elem>\n"
            "        </entry-noeol>\n"
            "      </name>\n"
            "    </name-list>\n"
            "  </entry>\n"
            "  <entry>\n"
            "    <_id>2</_id>\n"
            "    <name-list>\n"
            "      <_r-id>1</_r-id>\n"
            "      <name>\n"
            "        <name>binary</name>\n"
            "        <entry-noeol>\n"
            "          <_id>1</_id>\n"
            "          <_name-ref>2</_name-ref>\n"
            "        </entry-noeol>\n"
            "      </name>\n"
            "    </name-list>\n"
            "    <name-list>\n"
            "      <_r-id>2</_r-id>\n"
            "      <name>\n"
            "        <name>apt</name>\n"
            "        <entry-noeol>\n"
            "          <_id>1</_id>\n"
            "          <_name-ref>3</_name-ref>\n"
            "        </entry-noeol>\n"
            "      </name>\n"
            "    </name-list>\n"
            "    <name-list>\n"
            "      <_r-id>3</_r-id>\n"
            "      <name>\n"
            "        <name>AptCli</name>\n"
            "        <entry-noeol>\n"
            "          <_id>1</_id>\n"
            "          <_name-ref>4</_name-ref>\n"
            "        </entry-noeol>\n"
            "      </name>\n"
            "    </name-list>\n"
            "    <name-list>\n"
            "      <_r-id>4</_r-id>\n"
            "      <name>\n"
            "        <name>Hooks</name>\n"
            "        <entry-noeol>\n"
            "          <_id>1</_id>\n"
            "          <_name-ref>5</_name-ref>\n"
            "        </entry-noeol>\n"
            "      </name>\n"
            "    </name-list>\n"
            "    <name-list>\n"
            "      <_r-id>5</_r-id>\n"
            "      <name>\n"
            "        <name>Upgrade</name>\n"
            "        <entry-noeol>\n"
            "          <_id>1</_id>\n"
            "          <elem>[ ! -f /usr/lib/ubuntu-advantage/apt-esm-json-hook ] || /usr/lib/ubuntu-advantage/apt-esm-json-hook || true</elem>\n"
            "        </entry-noeol>\n"
            "      </name>\n"
            "    </name-list>\n"
            "  </entry>\n"
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
