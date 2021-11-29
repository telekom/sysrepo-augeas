/**
 * @file test_passwd.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief passwd SR DS plugin test
 *
 * @copyright
 * Copyright (c) 2021 Deutsche Telekom AG.
 * Copyright (c) 2021 CESNET, z.s.p.o.
 *
 * This source code is licensed under BSD 3-Clause License (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://opensource.org/licenses/BSD-3-Clause
 */

#include <assert.h>
#include <dlfcn.h>
#include <setjmp.h>
#include <stdarg.h>
#include <unistd.h>

#include "tconfig.h"

#include <cmocka.h>
#include <libyang/libyang.h>
#include <sysrepo/plugins_datastore.h>

/* augeas SR DS plugin */
extern const struct srplg_ds_s srpds__;

struct state {
    struct ly_ctx *ctx;
    const struct lys_module *mod;
    const struct srplg_ds_s *ds_plg;
};

static int
setup_f(void **state)
{
    struct state *st;

    st = calloc(1, sizeof *st);
    *state = st;

    //ly_log_level(LY_LLVRB);

    /* context */
    assert_int_equal(LY_SUCCESS, ly_ctx_new(AUG_EXPECTED_YANG_DIR, 0, &st->ctx));
    ly_ctx_set_searchdir(st->ctx, AUG_MODULES_DIR);

    /* load module */
    st->mod = ly_ctx_load_module(st->ctx, "passwd", NULL, NULL);
    assert_non_null(st->mod);

    /* plugin, init */
    st->ds_plg = &srpds__;
    assert_int_equal(SR_ERR_OK, st->ds_plg->init_cb(st->mod, SR_DS_STARTUP, "root", "root", 00644));

    return 0;
}

static int
teardown_f(void **state)
{
    struct state *st = (struct state *)*state;
    int ret = 0;

    /* destroy */
    ret = st->ds_plg->destroy_cb(st->mod, SR_DS_STARTUP);

    /* free */
    ly_ctx_destroy(st->ctx);
    free(st);

    return ret;
}

static void
test_load(void **state)
{
    struct state *st = (struct state *)*state;
    struct lyd_node *data;

    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &data));
    lyd_free_siblings(data);
}

int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_load, setup_f, teardown_f),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
