/**
 * @file augyang.h
 * @author Adam Piecek <piecek@cesnet.cz>
 * @brief The augyang interface.
 *
 * Copyright (c) 2021 CESNET, z.s.p.o.
 *
 * This source code is licensed under BSD 3-Clause License (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://opensource.org/licenses/BSD-3-Clause
 */

#include <inttypes.h>

#define AYV_LTREE               0x1
#define AYV_YTREE               0x2
#define AYV_YTREE_AFTER_TRANS   0x4
#define AYV_TRANS_REMOVE        0x8
#define AYV_TRANS_INSERT1       0x10

struct augeas;
struct module;

int augyang_print_input_lenses(struct module *mod, char **str);
int augyang_print_yang(struct augeas *aug, struct module *mod, uint64_t vercode, char **str);

const char *augyang_get_error_message(int err_code);
