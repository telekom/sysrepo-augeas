/**
 * @file test_cmdline.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief cmdline SR DS plugin test
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
#define AUG_TEST_INPUT_FILES AUG_CONFIG_FILES_DIR "/cmdline"
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

#define AUG_TEST_MODULE "cmdline"

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
            "  <word-list>\n"
            "    <_id>1</_id>\n"
            "    <word>\n"
            "      <word>BOOT_IMAGE</word>\n"
            "      <no-spaces>/boot/vmlinuz-5.17.2-1-default</no-spaces>\n"
            "    </word>\n"
            "  </word-list>\n"
            "  <word-list>\n"
            "    <_id>2</_id>\n"
            "    <word>\n"
            "      <word>root</word>\n"
            "      <no-spaces>UUID=49be951e-c3c1-4230-bc1c-6ff82a4d82e8</no-spaces>\n"
            "    </word>\n"
            "  </word-list>\n"
            "  <word-list>\n"
            "    <_id>3</_id>\n"
            "    <word>\n"
            "      <word>splash</word>\n"
            "      <no-spaces>silent</no-spaces>\n"
            "    </word>\n"
            "  </word-list>\n"
            "  <word-list>\n"
            "    <_id>4</_id>\n"
            "    <word>\n"
            "      <word>mitigations</word>\n"
            "      <no-spaces>auto</no-spaces>\n"
            "    </word>\n"
            "  </word-list>\n"
            "  <word-list>\n"
            "    <_id>5</_id>\n"
            "    <word>\n"
            "      <word>quiet</word>\n"
            "    </word>\n"
            "  </word-list>\n"
            "  <word-list>\n"
            "    <_id>6</_id>\n"
            "    <word>\n"
            "      <word>security</word>\n"
            "      <no-spaces>apparmor</no-spaces>\n"
            "    </word>\n"
            "  </word-list>\n"
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
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "word-list[_id='7']/word/word", "fstab", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "word-list[_id='7']/word/no-spaces", "automount=yes", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "word-list[_id='2']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "word-list[_id='8']/word/word", "nolog", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "word-list[_id='5']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "1c1\n"
            "< BOOT_IMAGE=/boot/vmlinuz-5.17.2-1-default root=UUID=49be951e-c3c1-4230-bc1c-6ff82a4d82e8 splash=silent mitigations=auto quiet security=apparmor\n"
            "---\n"
            "> BOOT_IMAGE=/boot/vmlinuz-5.17.2-1-default root=UUID=49be951e-c3c1-4230-bc1c-6ff82a4d82e8 fstab=automount=yes splash=silent mitigations=auto quiet nolog security=apparmor\n"));
}

static void
test_store_modify(void **state)
{
    struct tstate *st = (struct tstate *)*state;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* modify some values */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "word-list[_id='2']/word/no-spaces",
            "UUID=49be951e-c3c1-4230-bc1c-abcdef4d82e8", LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "word-list[_id='5']/word/word", "silent",
            LYD_NEW_PATH_UPDATE, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "1c1\n"
            "< BOOT_IMAGE=/boot/vmlinuz-5.17.2-1-default root=UUID=49be951e-c3c1-4230-bc1c-6ff82a4d82e8 splash=silent mitigations=auto quiet security=apparmor\n"
            "---\n"
            "> BOOT_IMAGE=/boot/vmlinuz-5.17.2-1-default root=UUID=49be951e-c3c1-4230-bc1c-abcdef4d82e8 splash=silent mitigations=auto silent security=apparmor\n"));
}

static void
test_store_remove(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *node;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* remove list values */
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "word-list[_id='3']/word/no-spaces", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "word-list[_id='4']", 0, &node));
    lyd_free_tree(node);

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "1c1\n"
            "< BOOT_IMAGE=/boot/vmlinuz-5.17.2-1-default root=UUID=49be951e-c3c1-4230-bc1c-6ff82a4d82e8 splash=silent mitigations=auto quiet security=apparmor\n"
            "---\n"
            "> BOOT_IMAGE=/boot/vmlinuz-5.17.2-1-default root=UUID=49be951e-c3c1-4230-bc1c-6ff82a4d82e8 splash quiet security=apparmor\n"));
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
