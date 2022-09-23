/**
 * @file augyang.h
 * @author Adam Piecek <piecek@cesnet.cz>
 * @brief The augyang interface.
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

#include <inttypes.h>

/* verbose flags */
#define AYV_LTREE               0x01
#define AYV_YTREE               0x02
#define AYV_YTREE_AFTER_TRANS   0x04
#define AYV_YNODE_ID_IN_YANG    0x08
#define AYV_PTREE               0x10

/* error codes */
#define AYE_MEMORY 1
#define AYE_LENSE_NOT_FOUND 2
#define AYE_L_REC 3
#define AYE_DEBUG_FAILED 4
#define AYE_IDENT_NOT_FOUND 5
#define AYE_IDENT_LIMIT 6
#define AYE_LTREE_NO_ROOT 7
#define AYE_IDENT_BAD_CHAR 8
#define AYE_PARSE_FAILED 9

struct module;
struct augeas;

/**
 * @brief Parse augeas module @p filename and print terms.
 *
 * @param[in,out] aug Augeas context.
 * @param[in] filename Path with name of augeas module.
 * @param[out] str Dynamically allocated output string containing printed terms.
 * @return 0 on success. The augyang_get_error_message() is used for the error message.
 */
int augyang_print_input_terms(struct augeas *aug, const char *filename, char **str);

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

void augyang_print_stats(struct augeas *aug);
