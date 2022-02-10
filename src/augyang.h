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
#define AYV_TRANS1              0x8
#define AYV_TRANS2              0x10
#define AYV_TRANS3              0x20

struct module;

/**
 * @brief Print lenses of the module.
 *
 * @param[in] mod Module containing lenses for printing.
 * @param[out] str Dynamically allocated output string containing printed lenses.
 * @return 0 on success. The augyang_get_error_message() is used for the error message.
 */
int augyang_print_input_lenses(struct module *mod, char **str);

/**
 * @brief Convert and print YANG module from Augeas module.
 *
 * @param[in] mod Augeas module.
 * @param[in] vercode Verbose code for various debug outputs. See AYV_* constants.
 * @param[out] str Dynamically allocated output string containing printed yang module.
 * @return 0 on success. The augyang_get_error_message() is used for the error message.
 */
int augyang_print_yang(struct module *mod, uint64_t vercode, char **str);

/**
 * @brief Print error message.
 *
 * It can be applied to the output values of the augyang_print_input_lenses() and augyang_print_yang() functions.
 *
 * @param[in] err_code Error code for which the message is to be printed.
 * @return Message string.
 */
const char *augyang_get_error_message(int err_code);
