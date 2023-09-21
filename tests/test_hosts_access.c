/**
 * @file test_hosts_access.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief hosts-access SR DS plugin test
 *
 * @copyright
 * Copyright (c) 2023 Deutsche Telekom AG.
 * Copyright (c) 2023 CESNET, z.s.p.o.
 *
 * This source code is licensed under BSD 3-Clause License (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://opensource.org/licenses/BSD-3-Clause
 */

#include "tconfig.h"

/* augeas SR DS plugin */
#define AUG_TEST_INPUT_FILES AUG_CONFIG_FILES_DIR "/hosts-access"
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

#define AUG_TEST_MODULE "hosts-access"

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
            "  <line-list>\n"
            "    <_seq>1</_seq>\n"
            "    <daemon-list>\n"
            "      <_id>1</_id>\n"
            "      <process>\n"
            "        <list-item>http-rman</list-item>\n"
            "      </process>\n"
            "    </daemon-list>\n"
            "    <entry-list>\n"
            "      <_id>1</_id>\n"
            "      <client>\n"
            "        <value>ALL</value>\n"
            "      </client>\n"
            "    </entry-list>\n"
            "    <except2>\n"
            "      <entry-list>\n"
            "        <_id>1</_id>\n"
            "        <client>\n"
            "          <value>LOCAL</value>\n"
            "        </client>\n"
            "      </entry-list>\n"
            "    </except2>\n"
            "  </line-list>\n"
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
