/**
 * @file debug.h
 * @author Adam Piecek <piecek@cesnet.cz>
 * @brief Helper functions for debugging.
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

struct augeas;
struct lprinter_ctx;
struct lens;
struct ay_ynode;
struct ay_lnode;
struct module;
typedef uint8_t ly_bool;

/**
 * @brief Callback functions for debug printer.
 */
struct lprinter_ctx_f {
    void (*main)(struct lprinter_ctx *);        /**< Printer can start by this function. */
    ly_bool (*filter)(struct lprinter_ctx *);   /**< To ignore a node so it doesn't print. */
    void (*transition)(struct lprinter_ctx *);  /**< Transition function to the next node. */
    void (*extension)(struct lprinter_ctx *);   /**< To print extended information for the node. */
};

/**
 * @brief Context for the debug printer.
 */
struct lprinter_ctx {
    int space;                      /**< Current indent. */
    void *data;                     /**< General pointer to node. */
    struct lprinter_ctx_f func;     /**< Callbacks to customize the print. */
    struct ly_out *out;             /**< Output to which it is printed. */
};

/**
 * @brief Print debug information about lenses.
 *
 * @param[in] data General pointer to a node (eg lense or ynode).
 * @param[in] func Callback functions for debug printer.
 * @param[in] root_lense Lense where to begin.
 * @param[out] str_out Printed result.
 * @return 0 on success.
 */
int ay_print_lens(void *data, struct lprinter_ctx_f *func, struct lens *root_lense, char **str_out);

/**
 * @brief Print ynode tree in gdb.
 *
 * This function is useful for storing the ynode tree to the GDB Value history.
 *
 * @param[in] tree Tree of ynodes to print.
 * @return Printed ynode tree.
 */
char *ay_gdb_lptree(struct ay_ynode *tree);

/**
 * @brief Print ynode tree.
 *
 * @param[in] vercode Verbose that decides the execution of a function.
 * @param[in] vermask Bitmask for vercode to decide if result should be printed to stdout.
 * @param[in] tree Tree of ynodes to check by print functions.
 * @return 0 on success.
 */
int ay_debug_ynode_tree(uint64_t vercode, uint64_t vermask, struct ay_ynode *tree);

/**
 * @brief Test if ynode forest matches lense tree.
 *
 * @param[in] vercode Verbose that decides the execution of a function.
 * @param[in] mod Module in which the trees are located.
 * @param[in] yforest Forest of ynodes to check by print functions.
 * @return 0 on success.
 */
int ay_test_ynode_forest(uint64_t vercode, struct module *mod, struct ay_ynode *yforest);

/**
 * @brief Test if lnode tree matches lense tree.
 *
 * @param[in] vercode Verbose that decides the execution of a function.
 * @param[in] mod Module in which the trees are located.
 * @param[in] tree Tree of lnodes to check by print functions.
 * @return 0 on success.
 */
int ay_test_lnode_tree(uint64_t vercode, struct module *mod, struct ay_lnode *tree);

/**
 * @brief Print lenses of the module.
 *
 * @param[in] mod Module containing lenses for printing.
 * @param[out] str Dynamically allocated output string containing printed lenses.
 * @return 0 on success. The augyang_get_error_message() is used for the error message.
 */
int ay_print_input_lenses(struct module *mod, char **str);

/**
 * @brief Parse augeas module @p filename and print terms.
 *
 * @param[in,out] aug Augeas context.
 * @param[in] filename Path with name of augeas module.
 * @param[out] str Dynamically allocated output string containing printed terms.
 * @return 0 on success. The augyang_get_error_message() is used for the error message.
 */
int ay_print_input_terms(struct augeas *aug, const char *filename, char **str);

/**
 * @brief Enumeration for ay_print_terms().
 */
enum ay_term_print_type{
    TPT_YNODE,  /* struct ay_ynode */
    TPT_LNODE,  /* struct ay_lnode */
    TPT_PNODE,  /* struct ay_pnode */
    TPT_TERM    /* struct term */
};

/**
 * @brief Print tree in the form of terms.
 *
 * @param[in] tree Tree of type @p tpt.
 * @param[in] tpt See ay_term_print_type.
 * @return Printed terms or NULL.
 */
char *ay_print_terms(void *tree, enum ay_term_print_type tpt);
