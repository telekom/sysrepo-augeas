/**
 * @file debug.c
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

#define _GNU_SOURCE

#include <libyang/libyang.h>

#include "augyang.h"
#include "common.h"
#include "debug.h"
#include "errcode.h"
#include "lens.h"
#include "terms.h"

/**
 * @brief Defined in augeas project in the file parser.y.
 */
int augl_parse_file(struct augeas *aug, const char *name, struct term **term);

/**
 * @brief Compare two strings and print them if they differ.
 *
 * @param[in] subject For readability, what is actually compared.
 * @param[in] str1 First string to comparison.
 * @param[in] str2 Second string to comparison.
 * @return 0 on success, otherwise 1.
 */
static int
ay_test_compare(const char *subject, const char *str1, const char *str2)
{
    if (strcmp(str1, str2)) {
        printf(AY_NAME " DEBUG: %s difference\n", subject);
        printf("%s\n", str1);
        printf("----------------------\n");
        printf("%s\n", str2);
        return 1;
    }

    return 0;
}

/**
 * @brief Print basic debug information about lense.
 *
 * @param[in] out Output where the data are printed.
 * @param[in] lens Lense to print.
 * @param[in] space Current indent.
 * @param[in] lens_tag String representation of the @p lens tag.
 */
static void
ay_print_lens_node_header(struct ly_out *out, struct lens *lens, int space, const char *lens_tag)
{
    const char *filename;
    uint64_t len;
    uint16_t first_line, first_column;

    ay_get_filename(lens->info->filename->str, &filename, &len);
    first_line = lens->info->first_line;
    first_column = lens->info->first_column;

    ly_print(out, "%*s lens_tag: %s\n", space, "", lens_tag);
    ly_print(out, "%*s location: %.*s, %" PRIu16 ", %" PRIu16 "\n",
            space, "",
            (int)len + 4, filename,
            first_line, first_column);
}

/**
 * @brief Print debug information about lense and go to the next lense.
 *
 * @param[in,out] ctx Context for printing.
 * @param[in] lens Lense to print.
 */
static void
ay_print_lens_node(struct lprinter_ctx *ctx, struct lens *lens)
{
    char *regex = NULL;
    int sp;
    struct ly_out *out;

    if (ctx->func.filter && ctx->func.filter(ctx)) {
        ctx->func.transition(ctx);
        return;
    }

    out = ctx->out;
    sp = ctx->space;

    ly_print(out, "%*s {\n", sp, "");
    ctx->space = sp = ctx->space + SPACE_INDENT;

    if (ctx->func.extension) {
        ctx->func.extension(ctx);
    }

    if (lens) {
        switch (lens->tag) {
        case L_DEL:
            ay_print_lens_node_header(out, lens, sp, "L_DEL");
            regex = regexp_escape(lens->regexp);
            ly_print(out, "%*s lens_del_regex: %s\n", sp, "", regex);
            free(regex);
            break;
        case L_STORE:
            ay_print_lens_node_header(out, lens, sp, "L_STORE");
            regex = regexp_escape(lens->regexp);
            ly_print(out, "%*s lens_store_regex: %s\n", sp, "", regex);
            free(regex);
            break;
        case L_VALUE:
            ay_print_lens_node_header(out, lens, sp, "L_VALUE");
            ly_print(out, "%*s lens_value_string: %s\n", sp, "", lens->string->str);
            break;
        case L_KEY:
            ay_print_lens_node_header(out, lens, sp, "L_KEY");
            regex = regexp_escape(lens->regexp);
            ly_print(out, "%*s lens_key_regex: %s\n", sp, "", regex);
            free(regex);
            break;
        case L_LABEL:
            ay_print_lens_node_header(out, lens, sp, "L_LABEL");
            ly_print(out, "%*s lens_label_string: %s\n", sp, "", lens->string->str);
            break;
        case L_SEQ:
            ay_print_lens_node_header(out, lens, sp, "L_SEQ");
            ly_print(out, "%*s lens_seq_string: %s\n", sp, "", lens->string->str);
            break;
        case L_COUNTER:
            ay_print_lens_node_header(out, lens, sp, "L_COUNTER");
            ly_print(out, "%*s lens_counter_string: %s\n", sp, "", lens->string->str);
            break;
        case L_CONCAT:
            ay_print_lens_node_header(out, lens, sp, "L_CONCAT");
            break;
        case L_UNION:
            ay_print_lens_node_header(out, lens, sp, "L_UNION");
            break;
        case L_SUBTREE:
            ay_print_lens_node_header(out, lens, sp, "L_SUBTREE");
            break;
        case L_STAR:
            ay_print_lens_node_header(out, lens, sp, "L_STAR");
            break;
        case L_MAYBE:
            ay_print_lens_node_header(out, lens, sp, "L_MAYBE");
            break;
        case L_REC:
            ay_print_lens_node_header(out, lens, sp, "L_REC");
            ly_print(out, "%*s lens_rec_id: %p\n", sp, "", lens->body);
            break;
        case L_SQUARE:
            ay_print_lens_node_header(out, lens, sp, "L_SQUARE");
            break;
        default:
            ly_print(out, "ay_print_lens_node error\n");
            return;
        }
        ctx->func.transition(ctx);
    } else {
        ctx->func.transition(ctx);
    }

    sp -= SPACE_INDENT;
    ly_print(out, "%*s }\n", sp, "");
    ctx->space = sp;
}

int
ay_print_lens(void *data, struct lprinter_ctx_f *func, struct lens *root_lense, char **str_out)
{
    struct lprinter_ctx ctx = {0};
    struct ly_out *out;
    char *str;

    if (ly_out_new_memory(&str, 0, &out)) {
        return AYE_MEMORY;
    }

    ctx.data = data;
    ctx.func = *func;
    ctx.out = out;
    if (ctx.func.main) {
        ctx.func.main(&ctx);
    } else {
        ay_print_lens_node(&ctx, root_lense);
    }

    ly_out_free(out, NULL, 0);
    *str_out = str;

    return 0;
}

/**
 * @brief Does nothing.
 *
 * @param[in] ctx Context for printing.
 */
static void
ay_print_void(struct lprinter_ctx *ctx)
{
    (void)(ctx);
    return;
}

/**
 * @brief Print labels and values of @p node.
 *
 * @param[in] ctx Context for printing. The lprinter_ctx.data is type of ynode.
 * @param[in] node Node whose labels and values is to be printed.
 */
static void
ay_print_ynode_label_value(struct lprinter_ctx *ctx, struct ay_ynode *node)
{
    const struct ay_lnode *iter;

    void (*transition)(struct lprinter_ctx *);
    void (*extension)(struct lprinter_ctx *);

    if (!node || (!node->label && !node->value) || (node->type == YN_ROOT)) {
        return;
    }

    transition = ctx->func.transition;
    extension = ctx->func.extension;

    ctx->func.transition = ay_print_void;
    ctx->func.extension = NULL;

    for (iter = node->label; iter; iter = ay_lnode_next_lv(iter, AY_LV_TYPE_LABEL)) {
        ay_print_lens_node(ctx, iter->lens);
    }
    for (iter = node->value; iter; iter = ay_lnode_next_lv(iter, AY_LV_TYPE_VALUE)) {
        ay_print_lens_node(ctx, iter->lens);
    }

    ctx->func.transition = transition;
    ctx->func.extension = extension;
}

/**
 * @brief Transition from one ynode node to another.
 *
 * @param[in,out] ctx Context for printing. The lprinter_ctx.data is type of ynode.
 */
static void
ay_print_ynode_transition(struct lprinter_ctx *ctx)
{
    struct ay_ynode *node, *iter;

    node = ctx->data;

    for (iter = node->child; iter; iter = iter->next) {
        assert(iter->parent == node);
        ctx->data = iter;
        if (iter->snode) {
            ay_print_lens_node(ctx, iter->snode->lens);
        } else {
            ay_print_lens_node(ctx, NULL);
        }
    }
}

/**
 * @brief Print node's labels and values then make transition from one ynode to another.
 *
 * @param[in,out] ctx Context for printing. The lprinter_ctx.data is type of ynode.
 */
static void
ay_print_ynode_transition_lv(struct lprinter_ctx *ctx)
{
    ay_print_ynode_label_value(ctx, ctx->data);
    ay_print_ynode_transition(ctx);
}

/**
 * @brief Print additional information about ynode.
 *
 * @param[in] ctx Context for printing. The lprinter_ctx.data is type of ynode.
 */
static void
ay_print_ynode_extension(struct lprinter_ctx *ctx)
{
    struct ay_ynode *node;
    const struct lens *lens;

    node = ctx->data;

    switch (node->type) {
    case YN_UNKNOWN:
        ly_print(ctx->out, "%*s ynode_type: YN_UNKNOWN", ctx->space, "");
        break;
    case YN_LEAF:
        ly_print(ctx->out, "%*s ynode_type: YN_LEAF", ctx->space, "");
        break;
    case YN_LEAFREF:
        ly_print(ctx->out, "%*s ynode_type: YN_LEAFREF", ctx->space, "");
        break;
    case YN_LEAFLIST:
        ly_print(ctx->out, "%*s ynode_type: YN_LEAFLIST", ctx->space, "");
        break;
    case YN_LIST:
        ly_print(ctx->out, "%*s ynode_type: YN_LIST", ctx->space, "");
        break;
    case YN_CONTAINER:
        ly_print(ctx->out, "%*s ynode_type: YN_CONTAINER", ctx->space, "");
        break;
    case YN_CASE:
        ly_print(ctx->out, "%*s ynode_type: YN_CASE", ctx->space, "");
        break;
    case YN_KEY:
        ly_print(ctx->out, "%*s ynode_type: YN_KEY", ctx->space, "");
        break;
    case YN_VALUE:
        ly_print(ctx->out, "%*s ynode_type: YN_VALUE", ctx->space, "");
        break;
    case YN_GROUPING:
        ly_print(ctx->out, "%*s ynode_type: YN_GROUPING", ctx->space, "");
        break;
    case YN_USES:
        ly_print(ctx->out, "%*s ynode_type: YN_USES", ctx->space, "");
        break;
    case YN_REC:
        ly_print(ctx->out, "%*s ynode_type: YN_REC", ctx->space, "");
        break;
    case YN_ROOT:
        ly_print(ctx->out, "%*s ynode_type: YN_ROOT", ctx->space, "");
        break;
    }

    if (node->type == YN_ROOT) {
        ly_print(ctx->out, "\n");
        return;
    }

    if (node->parent->type == YN_ROOT) {
        ly_print(ctx->out, " (id: %" PRIu32 ", par: R00T)\n", node->id);
    } else {
        ly_print(ctx->out, " (id: %" PRIu32 ", par: %" PRIu32 ")\n", node->id, node->parent->id);
    }

    if (node->choice) {
        ly_print(ctx->out, "%*s choice_id: %p\n", ctx->space, "", node->choice);
    }

    if (node->type == YN_REC) {
        ly_print(ctx->out, "%*s snode_id: %p\n", ctx->space, "", node->snode);
    }

    if (node->ident) {
        ly_print(ctx->out, "%*s yang_ident: %s\n", ctx->space, "", node->ident);
    }

    if (node->ref) {
        ly_print(ctx->out, "%*s ref_id: %" PRIu32 "\n", ctx->space, "", node->ref);
    }

    if (node->flags) {
        ly_print(ctx->out, "%*s flags:", ctx->space, "");
        if (node->flags & AY_YNODE_MAND_TRUE) {
            ly_print(ctx->out, " mand_true");
        }
        if (node->flags & AY_YNODE_MAND_FALSE) {
            ly_print(ctx->out, " mand_false");
        }
        if (node->flags & AY_CHILDREN_MAND_FALSE) {
            ly_print(ctx->out, " children_mand_false");
        }
        if (node->flags & AY_VALUE_MAND_FALSE) {
            ly_print(ctx->out, " value_mand_false");
        }
        if (node->flags & AY_CHOICE_MAND_FALSE) {
            ly_print(ctx->out, " choice_mand_false");
        }
        if (node->flags & AY_VALUE_IN_CHOICE) {
            ly_print(ctx->out, " value_in_choice");
        }
        if (node->flags & AY_GROUPING_CHILDREN) {
            ly_print(ctx->out, " gr_children");
        }
        if (node->flags & AY_GROUPING_REDUCTION) {
            ly_print(ctx->out, " gr_reduction");
        }
        if (node->flags & AY_HINT_MAND_TRUE) {
            ly_print(ctx->out, " hint_mand_true");
        }
        if (node->flags & AY_HINT_MAND_FALSE) {
            ly_print(ctx->out, " hint_mand_false");
        }
        if (node->flags & AY_CHOICE_CREATED) {
            ly_print(ctx->out, " choice_created");
        }
        if (node->flags & AY_WHEN_TARGET) {
            ly_print(ctx->out, " when_target");
        }
        if (node->flags & AY_GROUPING_CHOICE) {
            ly_print(ctx->out, " gr_choice");
        }
    }
    if (AY_YNODE_IS_IMPLICIT_LIST(node)) {
        if (!node->flags) {
            ly_print(ctx->out, "%*s flags:", ctx->space, "");
        }
        ly_print(ctx->out, " implicit_list");
    }
    ly_print(ctx->out, "\n");

    if (node->min_elems) {
        ly_print(ctx->out, "%*s min_elems: %" PRIu16 "\n", ctx->space, "", node->min_elems);
    }
    if (node->when_ref) {
        ly_print(ctx->out, "%*s when_ref: %" PRIu32 "\n", ctx->space, "", node->when_ref);
    }
    if (node->when_val) {
        lens = node->when_val->lens;
        if (lens->tag == L_STORE) {
            ly_print(ctx->out, "%*s when_val: %s\n", ctx->space, "", lens->regexp->pattern->str);
        } else {
            assert(lens->tag == L_VALUE);
            ly_print(ctx->out, "%*s when_val: %s\n", ctx->space, "", lens->string->str);
        }
    }
}

char *
ay_gdb_lptree(struct ay_ynode *tree)
{
    char *str1 = NULL;
    struct lprinter_ctx_f print_func = {0};

    print_func.transition = ay_print_ynode_transition_lv;
    print_func.extension = ay_print_ynode_extension;
    ay_print_lens(tree, &print_func, NULL, &str1);

    return str1;
}

int
ay_debug_ynode_tree(uint64_t vercode, uint64_t vermask, struct ay_ynode *tree)
{
    int ret = 0;
    char *str1;
    struct lprinter_ctx_f print_func = {0};

    if (!vercode) {
        return 0;
    }

    print_func.transition = ay_print_ynode_transition_lv;
    print_func.extension = ay_print_ynode_extension;
    ret = ay_print_lens(tree, &print_func, NULL, &str1);
    AY_CHECK_RET(ret);

    if (vercode & vermask) {
        printf("%s\n", str1);
    }
    free(str1);

    return ret;
}

/**
 * @brief Check if lense node is not ynode.
 *
 * @param[in] ctx Context for printing. The lprinter_ctx.data is type of lense.
 * @return 1 if lense should be filtered because it is not ynode, otherwise 0.
 */
static ly_bool
ay_print_lens_filter_ynode(struct lprinter_ctx *ctx)
{
    struct lens *lens;

    lens = ctx->data;
    return !((lens->tag == L_SUBTREE) || (lens->tag == L_REC));
}

/**
 * @brief Starting function for printing ynode forest.
 *
 * @param[in,out] ctx Context for printing. The lprinter_ctx.data is type of ynode.
 */
static void
ay_print_ynode_main(struct lprinter_ctx *ctx)
{
    LY_ARRAY_COUNT_TYPE i;
    struct ay_ynode *forest;

    forest = ctx->data;
    LY_ARRAY_FOR(forest, i) {
        if (forest[i].type == YN_ROOT) {
            continue;
        }
        ctx->data = &forest[i];
        if (forest[i].snode) {
            ay_print_lens_node(ctx, forest[i].snode->lens);
        } else {
            ay_print_lens_node(ctx, NULL);
        }
        i += forest[i].descendants;
    }
}

/**
 * @brief Transition from one lense node to another.
 *
 * @param[in,out] ctx Context for printing. The lprinter_ctx.data is type of lense.
 */
static void
ay_print_lens_transition(struct lprinter_ctx *ctx)
{
    struct lens *lens = ctx->data;

    if (AY_LENSE_HAS_ONE_CHILD(lens->tag)) {
        ctx->data = lens->child;
        ay_print_lens_node(ctx, ctx->data);
    } else if (AY_LENSE_HAS_CHILDREN(lens->tag)) {
        for (uint64_t i = 0; i < lens->nchildren; i++) {
            ctx->data = lens->children[i];
            ay_print_lens_node(ctx, ctx->data);
        }
    } else if ((lens->tag == L_REC) && !lens->rec_internal) {
        ctx->data = lens->body;
        ay_print_lens_node(ctx, ctx->data);
    }
}

int
ay_test_ynode_forest(uint64_t vercode, struct module *mod, struct ay_ynode *yforest)
{
    int ret = 0;
    char *str1, *str2;
    struct lens *lens;
    struct lprinter_ctx_f print_func = {0};

    if (!vercode) {
        return ret;
    }

    lens = ay_lense_get_root(mod);
    AY_CHECK_COND(!lens, AYE_LENSE_NOT_FOUND);
    print_func.transition = ay_print_lens_transition;
    print_func.filter = ay_print_lens_filter_ynode;
    ret = ay_print_lens(lens, &print_func, lens, &str1);
    AY_CHECK_RET(ret);

    print_func.main = ay_print_ynode_main;
    print_func.transition = ay_print_ynode_transition;
    print_func.filter = NULL;
    ret = ay_print_lens(yforest, &print_func, yforest->snode->lens, &str2);
    AY_CHECK_RET(ret);

    ret = ay_test_compare("ynode forest", str1, str2);

    free(str1);
    free(str2);

    return ret;
}

/**
 * @brief Transition from one lnode to another.
 *
 * @param[in,out] ctx Context for printing. The lprinter_ctx.data is type of lnode.
 */
static void
ay_print_lnode_transition(struct lprinter_ctx *ctx)
{
    struct ay_lnode *node, *iter;

    node = ctx->data;
    for (iter = node->child; iter; iter = iter->next) {
        assert(iter->parent == node);
        ctx->data = iter;
        ay_print_lens_node(ctx, iter->lens);
    }
}

int
ay_test_lnode_tree(uint64_t vercode, struct module *mod, struct ay_lnode *tree)
{
    int ret = 0;
    char *str1, *str2;
    struct lprinter_ctx_f print_func = {0};

    if (!vercode) {
        return ret;
    }

    ret = augyang_print_input_lenses(mod, &str1);
    AY_CHECK_RET(ret);

    print_func.transition = ay_print_lnode_transition;
    ret = ay_print_lens(tree, &print_func, tree->lens, &str2);
    AY_CHECK_RET(ret);

    ret = ay_test_compare("lnode tree", str1, str2);
    if (!ret && (vercode & AYV_LTREE)) {
        printf("%s\n", str2);
    }

    free(str1);
    free(str2);

    return ret;
}

int
ay_print_input_lenses(struct module *mod, char **str)
{
    int ret = 0;
    struct lens *lens;
    struct lprinter_ctx_f print_func = {0};

    lens = ay_lense_get_root(mod);
    AY_CHECK_COND(!lens, AYE_LENSE_NOT_FOUND);
    print_func.transition = ay_print_lens_transition;
    ret = ay_print_lens(lens, &print_func, lens, str);

    return ret;
}

/**
 * @brief Recursively print all terms in @p exp.
 *
 * @param[in,out] out Printed output string.
 * @param[in] exp Subtree to print.
 * @param[in] space Space alignment.
 */
static void
ay_term_print(struct ly_out *out, struct term *exp, int space)
{
    if (!exp) {
        return;
    }

    space += 3;

    switch (exp->tag) {
    case A_MODULE:
        printf("MOD %s\n", exp->mname);
        list_for_each(dcl, exp->decls) {
            ay_term_print(out, dcl, 0);
            printf("\n");
        }
        break;
    case A_BIND:
        printf("- %s\n", exp->bname);
        ay_term_print(out, exp->exp, 0);
        break;
    case A_LET:
        printf("LET");
        printf("\n%*s", space, "");
        ay_term_print(out, exp->left, space);
        printf("\n%*s", space, "");
        ay_term_print(out, exp->right, space);
        break;
    case A_COMPOSE:
        printf("COM");
        printf("\n%*s", space, "");
        ay_term_print(out, exp->left, space);
        printf("\n%*s", space, "");
        ay_term_print(out, exp->right, space);
        break;
    case A_UNION:
        printf("UNI");
        printf("\n%*s", space, "");
        ay_term_print(out, exp->left, space);
        printf("\n%*s", space, "");
        ay_term_print(out, exp->right, space);
        break;
    case A_MINUS:
        printf("MIN");
        printf("\n%*s", space, "");
        ay_term_print(out, exp->left, space);
        printf("\n%*s", space, "");
        ay_term_print(out, exp->right, space);
        break;
    case A_CONCAT:
        printf("CON");
        printf("\n%*s", space, "");
        ay_term_print(out, exp->left, space);
        printf("\n%*s", space, "");
        ay_term_print(out, exp->right, space);
        break;
    case A_APP:
        printf("APP");
        printf("\n%*s", space, "");
        ay_term_print(out, exp->left, space);
        printf("\n%*s", space, "");
        ay_term_print(out, exp->right, space);
        break;
    case A_VALUE:
        /* V_NATIVE? */
        printf("VAL");
        if (exp->value->tag == V_REGEXP) {
            char *str;

            str = regexp_escape(exp->value->regexp);
            printf(" \"%s\"", str);
            free(str);
        } else if (exp->value->tag == V_STRING) {
            printf(" \"%s\"", exp->value->string->str);
        } else {
            printf("---");
        }
        break;
    case A_IDENT:
        printf("IDE %s", exp->ident->str);
        break;
    case A_BRACKET:
        printf("BRA");
        printf("\n%*s", space, "");
        ay_term_print(out, exp->brexp, space);
        break;
    case A_FUNC:
        printf("FUNC(%s)", exp->param ? exp->param->name->str : "");
        printf("\n%*s", space, "");
        ay_term_print(out, exp->body, space);
        break;
    case A_REP:
        printf("REP");
        printf("\n%*s", space, "");
        ay_term_print(out, exp->rexp, space);
        break;
    case A_TEST:
    default:
        printf(" .");
        break;
    }
}

/**
 * @brief Get augeas term from @p node.
 *
 * @param[in] node from which the term is obtained.
 * @return augeas term or NULL.
 */
static struct term *
ay_pnode_get_term(const struct ay_pnode *node)
{
    AY_CHECK_COND(!node, NULL);
    return node->term;
}

/**
 * @copydoc ay_pnode_get_term()
 */
static struct term *
ay_lnode_get_term(const struct ay_lnode *node)
{
    AY_CHECK_COND(!node, NULL);
    return ay_pnode_get_term(node->pnode);
}

/**
 * @copydoc ay_pnode_get_term()
 */
static struct term *
ay_ynode_get_term(const struct ay_ynode *node)
{
    AY_CHECK_COND(!node, NULL);
    AY_CHECK_COND(node->type != YN_ROOT, NULL);
    return ay_lnode_get_term(AY_YNODE_ROOT_LTREE(node));
}

char *
ay_print_terms(void *tree, enum ay_term_print_type tpt)
{
    char *str;
    struct ly_out *out;

    if (ly_out_new_memory(&str, 0, &out)) {
        return NULL;
    }

    switch (tpt) {
    case TPT_YNODE:
        ay_term_print(out, ay_ynode_get_term(tree), 0);
        break;
    case TPT_LNODE:
        ay_term_print(out, ay_lnode_get_term(tree), 0);
        break;
    case TPT_PNODE:
        ay_term_print(out, ay_pnode_get_term(tree), 0);
        break;
    case TPT_TERM:
        ay_term_print(out, tree, 0);
        break;
    }

    ly_out_free(out, NULL, 0);

    return str;
}

int
ay_print_input_terms(struct augeas *aug, const char *filename, char **str)
{
    int ret;
    struct term *tree = NULL;

    ret = augl_parse_file(aug, filename, &tree);
    if (ret || (aug->error->code != AUG_NOERROR)) {
        return AYE_PARSE_FAILED;
    }

    *str = ay_print_terms(tree, TPT_TERM);
    ret = *str ? 0 : AYE_MEMORY;

    unref(tree, term);

    return 0;
}
