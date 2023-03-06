/**
 * @file print_yang.h
 * @author Adam Piecek <piecek@cesnet.cz>
 * @brief Print YANG format.
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

struct module;
struct ay_ynode;
struct yprinter_ctx;
typedef uint8_t ly_bool;

/**
 * @brief Print ynode tree in yang format.
 *
 * @param[in] mod Module in which the tree is located.
 * @param[in] tree Ynode tree to print.
 * @param[in] vercode Decide if debugging information should be printed.
 * @param[out] str_out Printed tree in yang format. Call free() after use.
 * @return 0 on success.
 */
int ay_print_yang(struct module *mod, struct ay_ynode *tree, uint64_t vercode, char **str_out);

/**
 * @brief Set ay_ynode.ident for every ynode in the tree.
 *
 * @param[in,out] ctx Context for printing.
 * @param[in] solve_duplicates Flag for call ay_yang_ident_duplications().
 * @return 0 on success.
 */
int ay_ynode_idents(struct yprinter_ctx *ctx, ly_bool solve_duplicates);

/**
 * @brief Detect for duplicates for the identifier.
 *
 * @param[in] tree Tree of ynodes.
 * @param[in] node Node for which the duplicates will be searched.
 * @param[in] node_ident name to be verified.
 * @param[out] dupl_rank Duplicate number for @p ident. Rank may be greater than @p dupl_count because it is also
 * derived from the number of the previous duplicate identifier.
 * @param[out] dupl_count Number of all duplicates.
 * @return 0 on success.
 */
int ay_yang_ident_duplications(struct ay_ynode *tree, struct ay_ynode *node, char *node_ident, int64_t *dupl_rank,
        uint64_t *dupl_count);
