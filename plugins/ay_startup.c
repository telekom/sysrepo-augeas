/**
 * @file ay_startup.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief augyang startup config printer
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

/* augeas SR DS plugin */
#include "srds_augeas.c"
#include "srdsa_init.c"
#include "srdsa_load.c"
#include "srdsa_store.c"
#include "srdsa_common.c"

#include "plg_config.h"

#include <stdlib.h>

#include <libyang/libyang.h>
#include <sysrepo/plugins_datastore.h>

int
main(int argc, const char **argv)
{
    int ret = 0;
    struct ly_ctx *ctx = NULL;
    const struct lys_module *mod;
    struct lyd_node *data = NULL;
    const struct srplg_ds_s *ds_plg;

    if (argc != 2) {
        printf("Usage: ay_startup lens-name\n\n");
        ret = 1;
        goto cleanup;
    }

    /* logging */
    sr_log_stderr(SR_LL_WRN);
    ly_log_options(LY_LOLOG | LY_LOSTORE_LAST);
    ly_log_level(LY_LLWRN);

    /* context */
    if (ly_ctx_new(AUG_EXPECTED_YANG_DIR, 0, &ctx)) {
        ret = 1;
        goto cleanup;
    }
    ly_ctx_set_searchdir(ctx, AUG_MODULES_DIR);

    /* load module */
    mod = ly_ctx_load_module(ctx, argv[1], NULL, NULL);
    if (!mod) {
        ret = 1;
        goto cleanup;
    }

    /* plugin */
    ds_plg = &srpds__;

    /* load calback */
    if (ds_plg->load_cb(mod, SR_DS_STARTUP, NULL, 0, &data)) {
        ret = 1;
        goto cleanup;
    }

    /* print data */
    if (lyd_print_file(stdout, data, LYD_XML, LYD_PRINT_WITHSIBLINGS)) {
        ret = 1;
        goto cleanup;
    }

cleanup:
    lyd_free_siblings(data);
    ly_ctx_destroy(ctx);
    return ret;
}
