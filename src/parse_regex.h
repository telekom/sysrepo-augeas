/**
 * @file parse_regex.h
 * @author Adam Piecek <piecek@cesnet.cz>
 * @brief Functions for parsing regexes.
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

#include <stdint.h>

struct ay_transl;
struct ay_ynode;
struct lens;

/**
 * @brief Check if lense pattern does not have a fairly regular expression,
 * but rather a sequence of identifiers separated by '|'.
 *
 * @param[in] tree Tree of ynodes. If set to NULL then the whole pattern in @p lens will be checked.
 * Otherwise ay_ynode_root.patt_table will be used which should be faster.
 * @param[in] lens Lense to check.
 * @return non-zero/(ay_transl *) if lense contains identifiers in his pattern. Otherwise NULL.
 */
struct ay_transl *ay_lense_pattern_has_idents(const struct ay_ynode *tree, const struct lens *lens);

/**
 * @brief Create and fill ay_transl.substr LY_ARRAY based on ay_transl.origin.
 *
 * @param[in,out] tran Translation record.
 * @return 0 on success.
 * @return -1 if pattern cannot be divided into identifiers.
 * @return Positive number if an error occurs.
 */
int ay_transl_create_substr(struct ay_transl *tran);

/**
 * @brief Release ay_transl.substr in translation record.
 *
 * @param[in] entry Record from translation table.
 */
void ay_transl_table_substr_free(struct ay_transl *entry);
