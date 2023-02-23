/**
 * @file print_yang.c
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

#define _GNU_SOURCE

#include <ctype.h>
#include <string.h>

#include <libyang/libyang.h>

#include "augyang.h"
#include "common.h"
#include "lens.h"
#include "print_yang.h"
#include "terms.h"

/**
 * @brief Prefix of imported yang module which contains extensions for generated yang module.
 */
#define AY_EXT_PREFIX "augex"

/**
 * @brief Extension name for showing the path in the augeas data tree.
 */
#define AY_EXT_PATH "data-path"

/**
 * @brief Extension name for showing the value-yang-path..
 */
#define AY_EXT_VALPATH "value-yang-path"

/**
 * @brief Specification where the identifier should be placed.
 */
enum ay_ident_dst {
    AY_IDENT_NODE_NAME,     /**< Identifier to be placed as name for some YANG node. */
    AY_IDENT_DATA_PATH,     /**< Identifier to be placed in the data-path. */
    AY_IDENT_VALUE_YPATH    /**< Identifier to be placed in the value-yang-path. */
};

/**
 * @brief Print module name.
 *
 * @param[in] mod Module whose name is to be printed.
 * @param[out] namelen Length of module name.
 * @return Module name.
 */
static const char *
ay_get_yang_module_name(struct module *mod, size_t *namelen)
{
    const char *name, *path;

    path = mod->bindings->value->info->filename->str;
    ay_get_filename(path, &name, namelen);

    return name;
}

/**
 * @brief Get a name of the lense from @p mod.
 *
 * @param[in] mod Module where to find lense name.
 * @param[in] lens Lense for which the name is to be found.
 * @return Name of the lense or NULL.
 */
static char *
ay_get_lense_name_by_mod(struct module *mod, struct lens *lens)
{
    struct binding *bind_iter;

    if (!lens) {
        return NULL;
    }

    LY_LIST_FOR(mod->bindings, bind_iter) {
        if (bind_iter->value->lens == lens) {
            return bind_iter->ident->str;
        }
    }

    if ((lens->tag == L_STORE) || (lens->tag == L_KEY)) {
        LY_LIST_FOR(mod->bindings, bind_iter) {
            if (bind_iter->value->tag != V_REGEXP) {
                continue;
            }
            if (bind_iter->value->regexp == lens->regexp) {
                return bind_iter->ident->str;
            }
        }
    }

    return NULL;
}

/**
 * @brief Get lense name from specific module.
 *
 * @param[in] modname Name of the module where to look for @p lens.
 * @param[in] lens Lense for which to find the name.
 * @return Lense name or NULL.
 */
static char *
ay_get_lense_name_by_modname(const char *modname, struct lens *lens)
{
    char *ret;
    struct module *mod;

    mod = ay_get_module(ay_get_augeas_ctx2(lens), modname, 0);
    ret = mod ? ay_get_lense_name_by_mod(mod, lens) : NULL;

    return ret;
}

/**
 * @brief Get lense name.
 *
 * @param[in] mod Module that augyang is currently processing. Takes precedence over lnode.mod when searching.
 * @param[in] lens Lense for which to find the name.
 * @return Lense name or NULL.
 */
static char *
ay_get_lense_name(struct module *mod, const struct ay_lnode *lnode)
{
    static char *ret;

    if (!lnode) {
        return NULL;
    }

    /* First search in @p mod. */
    ret = ay_get_lense_name_by_mod(mod, lnode->lens);
    AY_CHECK_RET(ret);
    ret = ay_get_lense_name_by_modname("Rx", lnode->lens);
    AY_CHECK_RET(ret);

    if (mod == lnode->mod) {
        return ret;
    }

    /* Try searching in lnode.mod. */
    ret = ay_get_lense_name_by_mod(lnode->mod, lnode->lens);
    AY_CHECK_RET(ret);

    return ret;
}

/**
 * @brief Get lense name which is not directly related to @p node.
 *
 * This function is a bit experimental. The point is that, for example, list nodes often have the identifier
 * 'config-entries', which often causes name collisions. But there may be unused lense identifiers in the augeas module
 * and it would be a pity not to use them. So even though the identifier isn't quite directly related to the @p node,
 * it's still better than the default name ('config-entries').
 *
 * @param[in] mod Module that augyang is currently processing. Takes precedence over lnode.mod when searching.
 * @param[in] node Node for which the identifier is to be found.
 * @return Identifier or NULL.
 */
static char *
ay_get_spare_lense_name(struct module *mod, const struct ay_ynode *node)
{
    const struct ay_ynode *ynter;
    const struct ay_lnode *liter, *start, *end;
    struct binding *bind_iter;

    /* Find the node that terminates the search. */
    end = NULL;
    for (ynter = node->parent; ynter; ynter = ynter->parent) {
        if (ynter->snode) {
            end = ynter->snode;
            break;
        }
    }
    if (!end) {
        return NULL;
    }

    /* Find the node that starts the search. */
    start = NULL;
    for (ynter = node->child; ynter; ynter = ynter->child) {
        if (ynter->snode) {
            start = ynter->snode;
            break;
        } else if (ynter->label) {
            start = ynter->label;
            break;
        }
    }
    if (!start) {
        return NULL;
    }

    /* Find a free unused identifier in the module. */
    for (liter = start->parent; liter && (liter != end); liter = liter->parent) {
        LY_LIST_FOR(mod->bindings, bind_iter) {
            if ((bind_iter->value->lens == liter->lens) && strcmp("lns", bind_iter->ident->str)) {
                return bind_iter->ident->str;
            }
        }
        /* Try search in ay_lnode.mod */
        LY_LIST_FOR(liter->mod->bindings, bind_iter) {
            if ((bind_iter->value->lens == liter->lens) && strcmp("lns", bind_iter->ident->str)) {
                return bind_iter->ident->str;
            }
        }
    }

    return NULL;
}

/**
 * @brief Get lense name from specific module and search using a regular expression.
 *
 * @param[in] aug Augeas context.
 * @param[in] modname Name of the module where to look for @p pattern.
 * @param[in] pattern Regular expression used to find lense.
 * @param[in] ignore_maybe Flag set to 1 to ignore the "{0,1}" substring in @p pattern.
 * Augeas inserts this substring into the @p pattern if it is affected by the '?' (maybe) operator.
 * @return Lense name which has @p pattern in the @p modname module otherwise NULL.
 */
static char *
ay_get_lense_name_by_regex(struct augeas *aug, const char *modname, const char *pattern, ly_bool ignore_maybe)
{
    char *ret = NULL;
    struct module *mod;
    size_t pattern_len, maybe_len = strlen("{0,1}");
    char *found;
    uint64_t cnt_found = 0;
    struct binding *bind_iter;
    char *str;

    if (!pattern) {
        return NULL;
    }

    mod = ay_get_module(aug, modname, 0);
    if (!mod) {
        return ret;
    }

    pattern_len = strlen(pattern);
    if (ignore_maybe && (pattern_len > maybe_len) && !strcmp(pattern + (pattern_len - maybe_len), "{0,1}")) {
        pattern_len = pattern_len - maybe_len;
        /* pattern without parentheses */
        pattern++;
        pattern_len -= 2;
    }

    mod = ay_get_module(aug, modname, 0);
    LY_LIST_FOR(mod->bindings, bind_iter) {
        if (bind_iter->value->tag != V_REGEXP) {
            continue;
        }
        str = bind_iter->value->regexp->pattern->str;
        if (strlen(str) != pattern_len) {
            continue;
        }
        if (!strncmp(str, pattern, pattern_len)) {
            found = bind_iter->ident->str;
            cnt_found++;
        }
    }

    if (cnt_found == 1) {
        ret = found;
    }

    return ret;
}

/**
 * @brief Get ay_transl.substr from ay_ynode_root.patt_table based on @p node label and his rank as a sibling.
 *
 * @param[in] tree Tree of ynodes.
 * @param[in] node Node that is indirectly bound to ay_transl.substr.
 * @return Corresponding ay_transl.substr.
 */
static const char *
ay_ynode_get_substr_from_transl_table(const struct ay_ynode *tree, const struct ay_ynode *node)
{
    uint64_t node_idx;
    const char *pattern, *substr;
    struct lens *label;
    struct ay_transl *table, *tran;

    assert(node->label->flags & AY_LNODE_KEY_HAS_IDENTS);

    table = AY_YNODE_ROOT_PATT_TABLE(tree);
    label = AY_LABEL_LENS(node);
    assert(label && (label->tag == L_KEY) && node->parent);
    pattern = label->regexp->pattern->str;
    /* find out which identifier index to look for in the pattern */
    node_idx = ay_ynode_splitted_seq_index(node);

    tran = ay_transl_find(table, pattern);
    assert(tran && (node_idx < LY_ARRAY_COUNT(tran->substr)));
    substr = tran->substr[node_idx];

    return substr;
}

/**
 * @brief Get identifier from lense->regexp->pattern in a suitable form for YANG.
 *
 * @param[in] ident Pointer to the identifier.
 * @param[in] opt Where the identifier will be placed.
 * @param[out] buffer Buffer in which a valid identifier will be written.
 * @return 0 on success.
 */
static int
ay_get_ident_from_pattern_standardized(const char *ident, enum ay_ident_dst opt, char *buffer)
{
    int64_t i, j;

    for (i = 0, j = 0; ident[i] != '\0'; i++, j++) {
        switch (ident[i]) {
        case '\n':
            j--;
            break;
        case ' ':
            if ((j == 0) || (j && (buffer[j - 1] == '-'))) {
                j--;
            } else {
                AY_CHECK_COND(j >= AY_MAX_IDENT_SIZE, AYE_IDENT_LIMIT);
                buffer[j] = opt == AY_IDENT_NODE_NAME ? '-' : ' ';
            }
            break;
        case '(':
        case ')':
        case '?':
            j--;
            break;
        case '\\':
            if ((j == 0) && (ident[i + 1] == '.')) {
                /* remove '\' and also '.' */
                j--;
                i++;
            } else if ((ident[i + 1] == '.') || (ident[i + 1] == '-')) {
                /* remove '\' but keep '.' */
                j--;
            } else {
                return AYE_IDENT_BAD_CHAR;
            }
            break;
        case '_':
            if (j == 0) {
                j--;
            } else {
                AY_CHECK_COND(j >= AY_MAX_IDENT_SIZE, AYE_IDENT_LIMIT);
                buffer[j] = opt == AY_IDENT_NODE_NAME ? '-' : '_';
            }
            break;
        default:
            AY_CHECK_COND(j >= AY_MAX_IDENT_SIZE, AYE_IDENT_LIMIT);
            buffer[j] = ident[i];
        }
    }

    AY_CHECK_COND(j >= AY_MAX_IDENT_SIZE, AYE_IDENT_LIMIT);
    buffer[j] = '\0';

    return 0;
}

/**
 * @brief Get identifier stored in translation table.
 *
 * @param[in] tree Tree of ynodes.
 * @param[in] node Node for which the identifier is to be found.
 * @param[in] opt Where the identifier will be placed.
 * @param[out] buffer Array with sufficient memory space in which the identifier will be written.
 * @return 1 if identifier is successfully found and standardized.
 */
static int
ay_ynode_get_ident_from_transl_table(const struct ay_ynode *tree, const struct ay_ynode *node, enum ay_ident_dst opt,
        char *buffer)
{
    int ret;
    const char *ident;

    ident = ay_ynode_get_substr_from_transl_table(tree, node);
    ret = ay_get_ident_from_pattern_standardized(ident, opt, buffer);

    return ret;
}

/**
 * @brief Remove all @p rem characters from the @p str and write result to @p buffer.
 *
 * @param[in] str Input string.
 * @param[in] rem Character that should not appear in @p buffer.
 * @param[out] buffer Output string without @p rem.
 */
static void
ay_string_remove_characters(const char *str, char rem, char *buffer)
{
    uint64_t i, j, len;

    len = strlen(str);
    assert(len < AY_MAX_IDENT_SIZE);
    for (i = 0, j = 0; i < len; i++, j++) {
        if (str[i] != rem) {
            buffer[j] = str[i];
        } else {
            j--;
        }
    }
    buffer[j] = '\0';
}

/**
 * @brief Modify string so that uppercase letters are not present and possibly separate the words with dash.
 *
 * @param[in,out] buffer Buffer containing identifier of node.
 * @return 0 on success.
 */
static int
ay_ident_lowercase_dash(char *buffer)
{
    uint64_t i;

    for (i = 0; i < strlen(buffer); i++) {
        if (!isupper(buffer[i]) && (buffer[i] != '-') && isupper(buffer[i + 1])) {
            AY_CHECK_MAX_IDENT_SIZE(buffer, "_");
            memmove(buffer + i + 2, buffer + i + 1, strlen(buffer + i + 1) + 1);
            buffer[i + 1] = '-';
            i++;
        } else if (isupper(buffer[i])) {
            buffer[i] = tolower(buffer[i]);
        }
    }

    return 0;
}

/**
 * @brief Modify the identifier to conform to the constraints of the yang identifier.
 *
 * TODO: complete for all input characters.
 *
 * @param[in] ident Identifier for standardization.
 * @param[in] opt Where the identifier will be placed.
 * @param[in] internal If set then add '_' to the beginning of the buffer.
 * @param[out] buffer Buffer in which a valid identifier will be written.
 * @return 0 on success.
 */
static int
ay_get_ident_standardized(const char *ident, enum ay_ident_dst opt, ly_bool internal, char *buffer)
{
    int ret;
    int64_t i, j, len, stop;

    assert((opt == AY_IDENT_NODE_NAME) || (opt == AY_IDENT_VALUE_YPATH));

    stop = strlen(ident);
    for (i = 0, j = 0; i < stop; i++, j++) {
        switch (ident[i]) {
        case ' ':
            AY_CHECK_COND(j >= AY_MAX_IDENT_SIZE, AYE_IDENT_LIMIT);
            buffer[j] = opt == AY_IDENT_NODE_NAME ? '-' : ' ';
            break;
        case '#':
            j--;
            break;
        case '+':
            len = strlen("plus-");
            AY_CHECK_COND(j + len >= AY_MAX_IDENT_SIZE, AYE_IDENT_LIMIT);
            strcpy(&buffer[j], "plus-");
            j += len - 1;
            break;
        case '-':
            if (j == 0) {
                len = strlen("minus-");
                AY_CHECK_COND(j + len >= AY_MAX_IDENT_SIZE, AYE_IDENT_LIMIT);
                strcpy(&buffer[j], "minus-");
                j += len - 1;
            } else {
                AY_CHECK_COND(j >= AY_MAX_IDENT_SIZE, AYE_IDENT_LIMIT);
                buffer[j] = '-';
            }
            break;
        case '@':
            j--;
            break;
        case '\\':
            if ((j == 0) && (ident[i + 1] == '.')) {
                /* remove '\' and also '.' */
                j--;
                i++;
            } else if (ident[i + 1] == '.') {
                /* remove '\' but keep '.' */
                j--;
            } else if (ident[i + 1] == '+') {
                len = strlen("plus-");
                AY_CHECK_COND(j + len >= AY_MAX_IDENT_SIZE, AYE_IDENT_LIMIT);
                strcpy(&buffer[j], "plus-");
                j += len - 1;
                i++;
            } else {
                return AYE_IDENT_BAD_CHAR;
            }
            break;
        case '_':
            if (j == 0) {
                j--;
            } else {
                AY_CHECK_COND(j >= AY_MAX_IDENT_SIZE, AYE_IDENT_LIMIT);
                buffer[j] = '-';
            }
            break;
        default:
            if ((j == 0) && isalpha(ident[i])) {
                buffer[j] = ident[i];
            } else if (j > 0) {
                AY_CHECK_COND(j >= AY_MAX_IDENT_SIZE, AYE_IDENT_LIMIT);
                buffer[j] = ident[i];
            } else {
                j--;
            }
        }
    }

    if ((j > 0) && (buffer[j - 1] == '-')) {
        /* Dash as the last character will be removed. */
        j--;
    }
    if ((j > 3) && !strncmp("-re", &buffer[j - 3], 3)) {
        /* The abbreviation is "-re" probably means regular expression. The substring is redundant. */
        j -= 3;
    }
    AY_CHECK_COND(j >= AY_MAX_IDENT_SIZE, AYE_IDENT_LIMIT);
    buffer[j] = '\0';

    ret = ay_ident_lowercase_dash(buffer);
    AY_CHECK_RET(ret);

    if (internal) {
        AY_CHECK_MAX_IDENT_SIZE(buffer, "_");
        memmove(buffer + 1, buffer, strlen(buffer) + 1);
        buffer[0] = '_';
    }

    return 0;
}

/**
 * @brief Get identifier of the ynode from the label lense.
 *
 * @param[in] tree Tree of ynodes.
 * @param[in] node Node to process.
 * @param[in] opt Where the identifier will be placed.
 * @param[out] buffer Identifier can be written to the @p buffer and in this case return value points to @p buffer.
 * @param[out] standardized Flag is set to 1 if string in @p buffer is standardized. In other words the function
 * ay_get_ident_standardized() was called.
 * @param[out] erc Error code is 0 on success.
 * @return Exact identifier, pointer to @p buffer or NULL.
 */
static const char *
ay_get_yang_ident_from_label(const struct ay_ynode *tree, struct ay_ynode *node, enum ay_ident_dst opt, char *buffer,
        ly_bool *standardized, int *erc)
{
    char *str;
    struct lens *label;

    if (*erc) {
        return NULL;
    }
    *erc = 0;

    label = AY_LABEL_LENS(node);
    if (!label) {
        return NULL;
    }

    str = NULL;
    if ((label->tag == L_LABEL) || (label->tag == L_SEQ)) {
        str = label->string->str;
    } else if (node->label->flags & AY_LNODE_KEY_IS_LABEL) {
        if ((opt == AY_IDENT_DATA_PATH) || (opt == AY_IDENT_VALUE_YPATH)) {
            /* remove backslashes */
            ay_string_remove_characters(label->regexp->pattern->str, '\\', buffer);
            return buffer;
        } else {
            str = label->regexp->pattern->str;
        }
    } else if (node->label->flags & AY_LNODE_KEY_HAS_IDENTS) {
        *erc = ay_ynode_get_ident_from_transl_table(tree, node, opt, buffer);
        return buffer;
    } else {
        return NULL;
    }

    assert(str);
    if ((opt == AY_IDENT_NODE_NAME) || (opt == AY_IDENT_VALUE_YPATH)) {
        ay_get_ident_standardized(str, opt, 0, buffer);
        if (buffer[0]) {
            /* Name is valid and standardized. */
            *standardized = 1;
            return buffer;
        } else {
            /* String is not suitable. Contains only special characters, e.g. @. */
            return NULL;
        }
    } else {
        return str;
    }
}

/**
 * @brief Get top-level grouping with @p id.
 *
 * @param[in] tree Tree of ynodes.
 * @param[in] id Unique ynode number (ay_ynode.id).
 * @return Grouping or NULL.
 */
static struct ay_ynode *
ay_ynode_get_grouping(const struct ay_ynode *tree, uint32_t id)
{
    struct ay_ynode *iter;

    for (iter = tree->child; iter; iter = iter->next) {
        if ((iter->type == YN_GROUPING) && (iter->id == id)) {
            return iter;
        }
    }

    return NULL;
}

/**
 * @brief Find YN_VALUE node of @p node.
 *
 * @param[in] tree Tree of ynodes.
 * @param[in] node Parent node in which is YN_VALUE node.
 * @param[in] label Label by which the YN_VALUE node is to be found.
 * @param[in] value Value by which the YN_VALUE node is to be found.
 * @return The YN_VALUE node placed as a child or NULL.
 */
static struct ay_ynode *
ay_ynode_get_value_node(const struct ay_ynode *tree, struct ay_ynode *node, const struct ay_lnode *label,
        const struct ay_lnode *value)
{
    struct ay_ynode *iter, *gr, *valnode;
    uint64_t i;

    valnode = NULL;
    for (i = 0; i < node->descendants; i++) {
        iter = &node[i + 1];
        if ((iter->type == YN_VALUE) && (iter->label->lens == label->lens) && (iter->value->lens == value->lens)) {
            valnode = iter;
            break;
        } else if (iter->type == YN_USES) {
            gr = ay_ynode_get_grouping(tree, iter->ref);
            assert(gr);
            valnode = ay_ynode_get_value_node(tree, gr, label, value);
            if (valnode) {
                break;
            }
        }
    }

    return valnode;
}

/**
 * @brief Try to find a name by pnode.
 *
 * @param[in] pnode Node for processing.
 * @return pointer to name or NULL.
 */
static char *
ay_ynode_name_by_pnode(struct ay_pnode *pnode)
{
    if (!pnode) {
        return NULL;
    }
    if (pnode->term->tag == A_FUNC) {
        return pnode->term->param->name->str;
    } else if (pnode->term->tag == A_BIND) {
        return pnode->term->bname;
    } else {
        return NULL;
    }
}

/**
 * @brief Try to find snode name from pnode.
 *
 * @param[in] node Node for which a name is sought.
 * @return pointer to name or NULL.
 */
static char *
ay_ynode_snode_name(struct ay_ynode *node)
{
    struct ay_pnode *pnode;
    char *name;

    if (!node->snode || !node->snode->pnode) {
        return NULL;
    }

    pnode = node->snode->pnode;
    if (!(pnode->flags & AY_PNODE_FOR_SNODE) || (pnode->flags & AY_PNODE_FOR_SNODES)) {
        return NULL;
    }

    name = ay_ynode_name_by_pnode(pnode);

    return name;
}

static int ay_get_yang_ident(struct yprinter_ctx *ctx, struct ay_ynode *node, enum ay_ident_dst opt, char *buffer);

/**
 * @brief Try to find identifier in first children.
 *
 * @param[in] ctx Current printing context.
 * @param[in] node Node whose children will be examined.
 * @param[in] opt Where the identifier will be placed.
 * @param[out] buffer Buffer in which the obtained identifier is written.
 */
static int
ay_get_yang_ident_first_descendants(struct yprinter_ctx *ctx, struct ay_ynode *node, enum ay_ident_dst opt,
        char *buffer)
{
    int ret;
    char *str;
    struct ay_ynode *iter;

    buffer[0] = '\0';
    for (iter = node->child; iter; iter = iter->child) {
        if (iter->next || (iter->type == YN_LEAFREF)) {
            break;
        } else if (iter->type == YN_CASE) {
            continue;
        }
        if (iter->snode && (str = ay_get_lense_name(ctx->mod, iter->snode))) {
            strcpy(buffer, str);
            break;
        }
        ret = ay_get_yang_ident(ctx, iter, opt, buffer);
        AY_CHECK_RET(ret);
        if (!strcmp(buffer, "config-entries") || !strcmp(buffer, "node")) {
            buffer[0] = '\0';
            continue;
        } else {
            break;
        }
    }

    return 0;
}

/**
 * @brief Print opening curly brace and set new indent.
 *
 * @param[in,out] ctx Context for printing.
 */
static void
ay_print_yang_nesting_begin(struct yprinter_ctx *ctx)
{
    ly_print(ctx->out, " {\n");
    ctx->space += SPACE_INDENT;
}

/**
 * @brief Print opening curly brace and set new indent.
 *
 * Conditionaly print debugging ID as comment.
 *
 * @param[in,out] ctx Context for printing.
 */
static void
ay_print_yang_nesting_begin2(struct yprinter_ctx *ctx, uint32_t id)
{
    if (ctx->vercode & AYV_YNODE_ID_IN_YANG) {
        ly_print(ctx->out, " { // %" PRIu64 "\n", id);
    } else {
        ly_print(ctx->out, " {\n");
    }
    ctx->space += SPACE_INDENT;
}

/**
 * @brief Print closing curly brace and set new indent.
 *
 * @param[in,out] ctx Context for printing.
 */
static void
ay_print_yang_nesting_end(struct yprinter_ctx *ctx)
{
    ctx->space -= SPACE_INDENT;
    ly_print(ctx->out, "%*s}\n", ctx->space, "");
}

/**
 * @brief Replace all occurences of @p target with @p replace.
 *
 * @param[in,out] str String in which to search @p target.
 * @param[in] target Substring being searched.
 * @param[in] replace Substituent.
 */
static void
ay_replace_substr(char *str, const char *target, const char *replace)
{
    char *hit, *remain;
    size_t trlen, rplen, rmlen;

    assert(str && target && (*target != '\0') && replace);
    trlen = strlen(target);
    rplen = strlen(replace);
    assert(trlen > rplen);

    hit = str;
    while ((hit = strstr(hit, target))) {
        if (rplen) {
            /* insert @p replace */
            memcpy(hit, replace, rplen);
            rmlen = trlen - rplen;
            if (rmlen) {
                remain = hit + rplen;
                memmove(remain, remain + rmlen, strlen(remain + rmlen) + 1);
            }
        } else {
            /* remove target in str */
            memmove(hit, hit + trlen, strlen(hit + trlen) + 1);
        }
    }
}

/**
 * @brief Remove parentheses around the entire regex pattern.
 *
 * TODO optimization: move code logic to the caller.
 *
 * @param[in,out] src Regex pattern.
 */
static void
ay_regex_remove_parentheses(char **src)
{
    int level;
    uint64_t i;
    size_t len;
    char *str;

    str = *src;
    len = strlen(str);
    if ((*str != '(') && (*(str + len - 1) != ')')) {
        return;
    }

    level = 1;
    for (i = 1; i < (len - 1); i++) {
        if (str[i] == '(') {
            level += 1;
        } else if (str[i] == ')') {
            level -= 1;
        }
        if (level == 0) {
            break;
        }
    }

    if (level == 1) {
        *(str + len - 1) = '\0';
        *src = str + 1;
    }
}

/**
 * @brief Greedy search for a substring to skip (and finally delete).
 *
 * Searched substrings are for example: \$?, \r ...
 *
 * @param[in] curr Current position in pattern to search substring.
 * @return Pointer to character that must not be skiped (deleted).
 */
static const char *
ay_regex_try_skip(const char *curr)
{
    const char *skip, *old;
    int64_t parcnt;

    /* Cannot be skiped, this substring is important. */
    if ((curr[1] && curr[2] && !strncmp(curr, "|()", 3)) ||
            (curr[1] && !strncmp(curr, "()", 2))) {
        return curr;
    }

    skip = curr;
    parcnt = 0;
    /* Let's skip these symbols and watch the number of parentheses. */
    do {
        old = skip;
        switch (skip[0]) {
        case '(':
            parcnt++;
            skip++;
            break;
        case ')':
            parcnt--;
            skip++;
            break;
        case '\r':
            skip++;
            break;
        }

        if (parcnt < 0) {
            /* There is more ')' than '('. The ')' must be printed. */
            return skip - 1;
        }
    } while (old != skip);

    if (parcnt != 0) {
        /* There is some '(' in the substring that should be printed. */
        return curr;
    }

    /* If some characters are skipped then skip repeat operator too. */
    if (skip != curr) {
        switch (*skip) {
        case '?':
        case '*':
        case '+':
            skip++;
            break;
        default:
            /* But OR operator cannot be skipped. */
            if (*(skip - 1) == '|') {
                skip--;
            }
            break;
        }
    }

    return skip;
}

/**
 * @brief Print string and clean up regex-related characters.
 *
 * For example, functions is used when printing when-stmt. In that case, the backslashes are deleted.
 *
 * @param[in,out] out Output handler for printing.
 * @param[in] str Input string to print.
 */
static void
ay_print_string_standardized(struct ly_out *out, const char *str)
{
    const char *chr;

    assert(str);

    for (chr = str; *chr != '\0'; chr++) {
        if ((chr[0] == '\\') && (chr[1] == '\\')) {
            ly_print(out, "\\\\");
        } else if (chr[0] == '\\') {
            continue;
        } else {
            ly_print(out, "%c", chr[0]);
        }
    }
}

/**
 * @brief Print lense regex pattern to be valid for libyang.
 *
 * This function converts an augeas regular expression to a double-quoted yang pattern. Backslash cases are a bit
 * complicated. Examples of backslash conversions are in the following table, where augeas regular expression is on
 * the left and yang double-quoted pattern is on the right:
 *
 * [\]      |   [\\\\]      - match one backslash character
 * [\\]     |   [\\\\]      - match one backslash character
 * \\\\     |   \\\\        - match one backslash character
 * \[       |   \\[         - match character '['
 * []]      |   [\\]]       - match character ']'
 * [\\\\]   |   [\\\\\\\\]  - match one backslash character
 *
 * Note:
 * The lense tests in augeas/src/lenses/tests require escaping of backslash.
 * Conversion probably doesn't work in all cases.
 *
 * @param[in,out] out Output handler for printing.
 * @param[in] patt Regex pattern to print.
 * @return 0 on success.
 */
static int
ay_print_regex_standardized(struct ly_out *out, const char *patt)
{
    const char *ch, *skip;
    char *mem, *src;
    ly_bool charClassExpr, charClassEmpty;

    if (!patt || (*patt == '\0')) {
        return 0;
    }

    /* substitution of erroneous strings in lenses */
    mem = strdup(patt);
    AY_CHECK_COND(!mem, AYE_MEMORY);
    src = mem;
    ay_replace_substr(src, "\n                  ", ""); /* Rx.hostname looks wrong */
    ay_replace_substr(src, "    minclock", "minclock"); /* ntp.aug looks wrong */

    /* remove () around pattern  */
    ay_regex_remove_parentheses(&src);

    charClassExpr = 0;
    charClassEmpty = 0;

    for (ch = src; *ch; ch++) {

        if ((skip = ay_regex_try_skip(ch)) != ch) {
            ch = skip - 1;
            continue;
        }

        switch (*ch) {
        case '[':
            if (charClassExpr) {
                /* Character [ is escaped. */
                ly_print(out, "\\\\[");
            } else {
                /* Start of character class expression []. */
                charClassExpr = 1;
                charClassEmpty = 1;
                ly_print(out, "[");
            }
            continue;
        case ']':
            if (charClassExpr && charClassEmpty) {
                /* Character ] is escaped. */
                ly_print(out, "\\\\]");
            } else {
                /* End of character class expression []. */
                charClassExpr = 0;
                ly_print(out, "]");
            }
            continue;
        case '^':
            ly_print(out, "^");
            continue;
        case '\n':
            ly_print(out, "\\n");
            break;
        case '\t':
            ly_print(out, "\\t");
            break;
        case '\"':
            ly_print(out, "\\\"");
            break;
        case '\\':
            switch (ch[1]) {
            case '$':
                /* Print only dolar character. */
                ly_print(out, "$");
                ch++;
                break;
            case '[':
            case ']':
                if (charClassExpr) {
                    /* Write backslash character inside of []. */
                    ly_print(out, "\\\\");
                    ly_print(out, "\\\\");
                } else {
                    /* Escape character ] or [. */
                    ly_print(out, "\\\\");
                    ly_print(out, "%c", ch[1]);
                    ch++;
                    continue;
                }
                break;
            case '\\':
                if (charClassExpr) {
                    /* Write backslash character twice. */
                    ly_print(out, "\\\\");
                    ly_print(out, "\\\\");
                    ly_print(out, "\\\\");
                    ly_print(out, "\\\\");
                } else {
                    /* Write backslash character outside of []. */
                    ly_print(out, "\\\\");
                    ly_print(out, "\\\\");
                }
                ch++;
                break;
            default:
                if (charClassExpr) {
                    /* Write backslash character inside of []. */
                    ly_print(out, "\\\\");
                    ly_print(out, "\\\\");
                } else {
                    /* Some character will be escaped outside of []. */
                    ly_print(out, "\\\\");
                }
                break;
            }
            break;
        default:
            ly_print(out, "%c", *ch);
            break;
        }

        charClassEmpty = 0;
    }

    free(mem);

    return 0;
}

/**
 * @brief Evaluate the identifier for the node.
 *
 * @param[in] ctx Current printing context.
 * @param[in] node Node for which the identifier is to be derived.
 * @param[in] opt Where the identifier will be placed.
 * @param[out] buffer Buffer in which the obtained identifier is written.
 * @return 0 on success.
 */
static int
ay_get_yang_ident(struct yprinter_ctx *ctx, struct ay_ynode *node, enum ay_ident_dst opt, char *buffer)
{
    int ret = 0;
    const char *str, *tmp, *ident_from_label;
    struct lens *label, *value, *snode;
    struct ay_ynode *tree, *iter;
    uint64_t len = 0;
    ly_bool internal = 0;
    ly_bool ch_tag = 0;
    ly_bool stand = 0;

    tree = ctx->tree;
    snode = AY_SNODE_LENS(node);
    label = AY_LABEL_LENS(node);
    value = AY_VALUE_LENS(node);

    /* Identifier priorities should work as follows:
     *
     * YN_CONTAINER yang-ident:
     * has_idents, "label not in YN_KEY", lense_name(snode), lense_name(label), LABEL, SEQ, is_label, "cont"
     * data-path:
     * LABEL, SEQ, is_label, has_idents, "$$"
     * value-yang-path:
     * get_yang_ident(YN_VALUE)
     *
     * YN_KEY yang-ident:
     * if label is (LABEL, is_label, has_idents) AND value is (L_STORE) then lense_name(value),
     * LABEL, SEQ, is_label, has_idents, lense_name(label), "_id"
     * data-path, value-yang-path:
     * empty
     *
     * YN_VALUE yang-ident:
     * lense_name(value), "value"
     * data-path, value-yang-path:
     * empty
     *
     * YN_LEAF yang-ident:
     * LABEL, SEQ, is_label, has_idents, lense_name(snode), lense_name(label), "node"
     * data-path:
     * LABEL, SEQ, is_label, has_idents, "$$"
     * value-yang-path:
     * get_yang_ident(YN_VALUE)
     */

    if (node->type == YN_GROUPING) {
        assert(node->child);
        ret = ay_get_yang_ident_first_descendants(ctx, node, opt, buffer);
        AY_CHECK_RET(ret);

        if (!buffer[0]) {
            /* Try snode from ex-parent. */
            str = ay_get_lense_name(ctx->mod, node->snode);
            if (!str) {
                ret = ay_get_yang_ident(ctx, node->child, opt, buffer);
                AY_CHECK_RET(ret);
                if (!strcmp(buffer, "node") || !strcmp(buffer, "config-entries")) {
                    str = "gr";
                } else {
                    ch_tag = 1;
                    str = buffer;
                }
            }
        } else {
            ch_tag = 1;
            str = buffer;
        }
        assert(str);
    } else if (node->type == YN_LEAFREF) {
        assert(snode);
        for (iter = node->parent; iter; iter = iter->parent) {
            if ((iter->type == YN_LIST) && iter->snode && (iter->snode->lens->tag == L_REC) &&
                    (iter->snode->lens->body == snode->body)) {
                break;
            }
        }
        assert(iter);
        ret = ay_get_yang_ident(ctx, iter->child, opt, buffer);
        AY_CHECK_RET(ret);
        internal = 1;
        AY_CHECK_MAX_IDENT_SIZE(buffer, "-ref");
        strcat(buffer, "-ref");
        str = buffer;
    } else if (node->type == YN_USES) {
        if (node->ident) {
            str = node->ident;
        } else {
            /* Resolve identifier later. */
            str = "node";
        }
    } else if (node->type == YN_LIST) {
        if (node->parent->type == YN_ROOT) {
            tmp = ay_get_yang_module_name(ctx->mod, &len);
            AY_CHECK_COND(len + 1 > AY_MAX_IDENT_SIZE, AYE_IDENT_LIMIT);
            strncpy(buffer, tmp, len);
            buffer[len] = '\0';
            len = 0;
            str = buffer;
        } else if (node->snode && (node->snode->lens->tag == L_REC)) {
            /* get identifier of node behind key */
            ret = ay_get_yang_ident(ctx, node->child, AY_IDENT_NODE_NAME, buffer);
            AY_CHECK_RET(ret);
            AY_CHECK_MAX_IDENT_SIZE(buffer, "-list");
            strcat(buffer, "-list");
            str = buffer;
        } else if (AY_YNODE_IS_SEQ_LIST(node)) {
            AY_CHECK_MAX_IDENT_SIZE(buffer, label->string->str);
            strcpy(buffer, label->string->str);
            AY_CHECK_MAX_IDENT_SIZE(buffer, "-list");
            strcat(buffer, "-list");
            str = buffer;
        } else if ((tmp = ay_get_lense_name(ctx->mod, node->label)) && strcmp(tmp, "lns")) {
            /* label can points to L_STAR lense */
            str = tmp;
        } else if (!ay_get_yang_ident_first_descendants(ctx, node, opt, buffer) && buffer[0]) {
            ch_tag = 1;
            AY_CHECK_MAX_IDENT_SIZE(buffer, "-list");
            strcat(buffer, "-list");
            str = buffer;
        } else if ((tmp = ay_get_spare_lense_name(ctx->mod, node))) {
            str = tmp;
        } else {
            str = "config-entries";
        }
    } else if ((node->type == YN_CONTAINER) && (opt == AY_IDENT_NODE_NAME) && !node->label) {
        ret = ay_get_yang_ident(ctx, node->child, opt, buffer);
        AY_CHECK_RET(ret);
        str = buffer;
    } else if ((node->type == YN_CONTAINER) && (opt == AY_IDENT_NODE_NAME)) {
        if (label && (node->label->flags & AY_LNODE_KEY_HAS_IDENTS)) {
            ret = ay_ynode_get_ident_from_transl_table(tree, node, opt, buffer);
            AY_CHECK_RET(ret);
            str = buffer;
        } else if (((node->child->type != YN_KEY) && (tmp = ay_get_yang_ident_from_label(tree, node, opt, buffer, &stand, &ret))) ||
                (tmp = ay_get_lense_name(ctx->mod, node->snode)) ||
                (tmp = ay_ynode_snode_name(node)) ||
                (tmp = ay_get_lense_name(ctx->mod, node->label)) ||
                (tmp = ay_get_yang_ident_from_label(tree, node, opt, buffer, &stand, &ret)) ||
                (node->label && (tmp = ay_ynode_name_by_pnode(node->label->pnode)))) {
            AY_CHECK_RET(ret);
            str = tmp;
        } else if (!node->label) {
            ret = ay_get_yang_ident(ctx, node->child, opt, buffer);
            AY_CHECK_RET(ret);
            str = buffer;
        } else {
            str = "node";
        }
    } else if ((node->type == YN_CONTAINER) && (opt == AY_IDENT_VALUE_YPATH)) {
        assert(node->child && node->child->next && (node->child->next->type == YN_VALUE));
        ret = ay_get_yang_ident(ctx, node->child->next, AY_IDENT_NODE_NAME, buffer);
        return ret;
    } else if (node->type == YN_KEY) {
        ident_from_label = ay_get_yang_ident_from_label(tree, node, opt, buffer, &stand, &ret);
        if (ident_from_label && (label->tag != L_SEQ) && value && (tmp = ay_get_lense_name(ctx->mod, node->value))) {
            AY_CHECK_RET(ret);
            str = tmp;
        } else if (ident_from_label) {
            AY_CHECK_RET(ret);
            str = ident_from_label;
        } else if ((tmp = ay_get_lense_name(ctx->mod, node->label)) ||
                (tmp = ay_ynode_name_by_pnode(node->label->pnode))) {
            str = tmp;
        } else {
            str = "label";
        }
    } else if (node->type == YN_CASE) {
        ay_get_yang_ident(ctx, node->child, opt, buffer);
        str = buffer;
    } else if (node->type == YN_VALUE) {
        if (!ay_dnode_find(AY_YNODE_ROOT_VALUES(ctx->tree), node->value) &&
                (tmp = ay_get_lense_name(ctx->mod, node->value))) {
            str = tmp;
        } else {
            str = "value";
        }
    } else if ((opt == AY_IDENT_NODE_NAME) && ((node->type == YN_LEAF) || (node->type == YN_LEAFLIST))) {
        if (((tmp = ay_get_yang_ident_from_label(tree, node, opt, buffer, &stand, &ret))) ||
                (tmp = ay_get_lense_name(ctx->mod, node->snode)) ||
                (tmp = ay_get_lense_name(ctx->mod, node->label)) ||
                (tmp = ay_ynode_name_by_pnode(node->label->pnode))) {
            AY_CHECK_RET(ret);
            str = tmp;
        } else {
            str = "node";
        }
    } else if ((opt == AY_IDENT_DATA_PATH) &&
            ((node->type == YN_CONTAINER) || (node->type == YN_LEAF) || (node->type == YN_LEAFLIST))) {
        if ((tmp = ay_get_yang_ident_from_label(tree, node, opt, buffer, &stand, &ret))) {
            AY_CHECK_RET(ret);
            str = tmp;
        } else {
            str = "$$";
        }
    } else if ((node->type == YN_LEAF) && (opt == AY_IDENT_VALUE_YPATH)) {
        ret = ay_get_yang_ident(ctx, node, AY_IDENT_NODE_NAME, buffer);
        return ret;
    } else {
        return AYE_IDENT_NOT_FOUND;
    }

    if ((opt == AY_IDENT_NODE_NAME) || (opt == AY_IDENT_VALUE_YPATH)) {
        if (!stand) {
            ret = ay_get_ident_standardized(str, opt, internal, buffer);
        }
        assert(buffer[0]);
    } else if (buffer != str) {
        assert((opt == AY_IDENT_DATA_PATH) || (opt == AY_IDENT_VALUE_YPATH));
        strcpy(buffer, str);
    }

    if (ch_tag) {
        if (((node->type == YN_GROUPING) || (node->type == YN_LIST)) && node->child && node->child->next &&
                node->child->choice && (node->child->next->choice == node->child->choice) &&
                ((strlen(buffer) >= 3) && strncmp(buffer, "ch-", 3))) {
            AY_CHECK_MAX_IDENT_SIZE(buffer, "ch-");
            memmove(buffer + 3, buffer, strlen(buffer) + 1);
            memcpy(buffer, "ch-", 3);
        }
    }

    return ret;
}

/**
 * @brief Print node identifier according to the yang language.
 *
 * @param[in] ctx Current printing context.
 * @param[in] node Node for which the identifier will be printed.
 * @param[in] opt Where the identifier will be placed.
 * @return 0 on success.
 */
static int
ay_print_yang_ident(struct yprinter_ctx *ctx, struct ay_ynode *node, enum ay_ident_dst opt)
{
    int ret = 0;
    char ident[AY_MAX_IDENT_SIZE];
    struct ay_ynode *grouping;

    if ((opt == AY_IDENT_NODE_NAME) && (node->type == YN_USES)) {
        grouping = ay_ynode_get_grouping(ctx->tree, node->ref);
        ly_print(ctx->out, "%s", grouping->ident);
    } else if (opt == AY_IDENT_NODE_NAME) {
        ly_print(ctx->out, "%s", node->ident);
    } else {
        ret = ay_get_yang_ident(ctx, node, opt, ident);
        AY_CHECK_RET(ret);
        ly_print(ctx->out, "%s", ident);
    }

    return ret;
}

/**
 * @brief Iterating 'sibling' nodes in such a way as to detect duplicate identifiers.
 *
 * @param[in] root Parent node whose direct descendants will iterate.
 * If set to NULL, then the root is initialized from @p iter.
 * @param[in] iter Current position of iterator.
 * If set to NULL, then the iterator is initialized to the @p root child.
 * @return Initialized root, initialized iterator, next position of iterator or NULL.
 */
static struct ay_ynode *
ay_yang_ident_iter(struct ay_ynode *root, struct ay_ynode *iter)
{
    struct ay_ynode *ret;

    if (!iter) {
        ret = root->child;
    } else if (!iter->next) {
        for (iter = iter->parent; (iter != root) && !iter->next; iter = iter->parent) {}
        ret = iter != root ? iter->next : NULL;
    } else if (iter->type == YN_CASE) {
        ret = iter;
    } else {
        ret = iter->next;
    }

    if (ret && (ret->type == YN_CASE)) {
        for (iter = ret->child; iter && (iter->type == YN_CASE); iter = iter->child) {}
        ret = iter;
    }

    return ret;
}

int
ay_yang_ident_duplications(struct ay_ynode *tree, struct ay_ynode *node, char *node_ident, int64_t *dupl_rank,
        uint64_t *dupl_count)
{
    int ret = 0;
    struct ay_ynode *iter, *root, *gr;
    int64_t rnk, tmp_rnk, tmp, prev;
    uint64_t cnt, tmp_cnt;
    const char *ch1, *ch2;
    char *end;

    assert(dupl_count);

    rnk = -1;
    cnt = 0;
    prev = -1;

    if (node->type == YN_CASE) {
        rnk = 0;
        goto end;
    }

    for (iter = node->parent; iter && (iter->type == YN_CASE); iter = iter->parent) {}
    assert(iter);
    root = iter;

    for (iter = ay_yang_ident_iter(root, NULL); iter; iter = ay_yang_ident_iter(root, iter)) {
        if ((iter->type == YN_KEY) || (iter->type == YN_LEAFREF) || !iter->ident) {
            continue;
        } else if (iter == node) {
            rnk = cnt;
            continue;
        } else if (iter->type == YN_USES) {
            gr = ay_ynode_get_grouping(tree, iter->ref);
            ret = ay_yang_ident_duplications(tree, gr->child, node_ident, &tmp_rnk, &tmp_cnt);
            AY_CHECK_RET(ret);
            rnk = rnk == -1 ? tmp_rnk : rnk;
            cnt += tmp_cnt;
        }

        /* Compare until non-numeric character. */
        for (ch1 = iter->ident, ch2 = node_ident; *ch1 && *ch2; ch1++, ch2++) {
            if (isdigit(*ch1) || isdigit(*ch2) || (*ch1 != *ch2)) {
                break;
            }
        }
        if (isdigit(*ch1) && !*ch2) {
            errno = 0;
            tmp = strtol(ch1, &end, 10);
            if (!errno && (*end == '\0')) {
                prev = rnk >= 0 ? prev : tmp;
                cnt++;
            }
        } else if (!*ch1 && !*ch2) {
            cnt++;
        }
    }

end:

    if (dupl_rank) {
        *dupl_rank = prev >= 0 ? prev : rnk;
    }
    *dupl_count = cnt;

    return ret;
}

/**
 * @brief Write a new identifier to dynamic memory.
 *
 * @param[in,out] old Array of identifiers.
 * @param[in] new New identifier. Old one can be freed.
 * @retrun 0 on success.
 */
static int
ay_ynode_ident_write(char **old, char *new)
{
    assert(new && new[0]);
    if (*old && new && (strlen(*old) >= strlen(new))) {
        strcpy(*old, new);
        return 0;
    } else {
        free(*old);
        *old = strdup(new);
        return *old ? 0 : AYE_MEMORY;
    }
}

int
ay_ynode_idents(struct yprinter_ctx *ctx, ly_bool solve_duplicates)
{
    int ret = 0;
    LY_ARRAY_COUNT_TYPE i;
    struct ay_ynode *tree, *iter, *uses, *gre, *parent;
    char buffer[AY_MAX_IDENT_SIZE];
    int64_t dupl_rank;
    uint64_t dupl_count;

    /* Resolve most of identifiers. */
    tree = ctx->tree;
    LY_ARRAY_FOR(tree, i) {
        iter = &tree[i];
        assert(iter->type != YN_REC);
        if ((iter->type == YN_USES) || (iter->type == YN_ROOT)) {
            continue;
        }

        if ((iter->type == YN_CONTAINER) && !iter->label) {
            strcpy(buffer, "case");
        } else {
            ret = ay_get_yang_ident(ctx, iter, AY_IDENT_NODE_NAME, buffer);
            AY_CHECK_RET(ret);
        }
        ay_ynode_ident_write(&iter->ident, buffer);
        AY_CHECK_RET(ret);
    }

    /* Resolve identifiers for YN_USES nodes.
     * It is assumed that the referenced grouping has the identifier evaluated.
     */
    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        uses = &tree[i];
        if (uses->type != YN_USES) {
            continue;
        }

        /* Find grouping. */
        gre = ay_ynode_get_grouping(tree, uses->ref);
        assert(gre);
        /* Set new identifier for YN_USES node. */
        ay_ynode_ident_write(&(uses->ident), gre->ident);

        /* Update parental identifiers. */
        for (iter = uses; iter; iter = iter->parent) {
            parent = iter->parent;
            if ((parent->child != iter) || ((parent->type != YN_LIST) && (parent->type != YN_GROUPING))) {
                break;
            } else if ((parent->type == YN_CONTAINER) && !parent->label) {
                continue;
            }
            ret = ay_get_yang_ident(ctx, parent, AY_IDENT_NODE_NAME, buffer);
            AY_CHECK_RET(ret);
            ay_ynode_ident_write(&parent->ident, buffer);
            AY_CHECK_RET(ret);
        }
    }

    if (!solve_duplicates) {
        return 0;
    }

    /* Number the duplicate identifiers. */
    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        iter = &tree[i];
        ret = ay_yang_ident_duplications(tree, iter, iter->ident, &dupl_rank, &dupl_count);
        AY_CHECK_RET(ret);
        if (!dupl_count) {
            /* No duplicates found. */
            continue;
        }

        /* Make duplicate identifiers unique. */
        if (iter->type == YN_KEY) {
            strcpy(buffer, "id");
        } else if (dupl_rank) {
            assert(dupl_rank > 0);
            strcpy(buffer, iter->ident);
            if (dupl_rank < 10) {
                AY_CHECK_MAX_IDENT_SIZE(buffer, "X");
            } else {
                assert(dupl_rank < 100);
                AY_CHECK_MAX_IDENT_SIZE(buffer, "XX");
            }
            sprintf(buffer + strlen(buffer),  "%" PRId64, dupl_rank + 1);
        } else {
            strcpy(buffer, iter->ident);
        }
        ay_ynode_ident_write(&iter->ident, buffer);
        AY_CHECK_RET(ret);
    }

    return ret;
}

/**
 * @brief Print type enumeration for lense with tag L_VALUE.
 *
 * @param[in] ctx Context for printing.
 * @param[in] lens Lense used to print enum value.
 * @return 0 on success.
 */
static int
ay_print_yang_enumeration(struct yprinter_ctx *ctx, struct lens *lens)
{
    assert(lens->tag == L_VALUE);

    ly_print(ctx->out, "%*stype enumeration", ctx->space, "");
    ay_print_yang_nesting_begin(ctx);
    ly_print(ctx->out, "%*senum \"%s\";\n", ctx->space, "", lens->string->str);
    ay_print_yang_nesting_end(ctx);

    return 0;
}

/**
 * @brief Print caseless flag in the pattern.
 *
 * @param[in] ctx Context for printing.
 * @param[in] re Regexp to check.
 */
static void
ay_print_yang_pattern_nocase(struct yprinter_ctx *ctx, const struct regexp *re)
{
    if (re->nocase) {
        ly_print(ctx->out, "(?i)");
    }
}

/**
 * @brief Check if caseless flag should be printed.
 *
 * @param[in] node Subtree of pnodes related to regexp.
 * @return 1 if regexp is nocase.
 */
static int
ay_pnode_regexp_has_nocase(struct ay_pnode *node)
{
    int ret;

    if (AY_PNODE_REF(node)) {
        ret = ay_pnode_regexp_has_nocase(node->ref);
    } else if ((node->term->tag == A_VALUE) && (node->term->value->tag == V_REGEXP)) {
        ret = node->term->value->regexp->nocase;
    } else if (node->term->tag == A_UNION) {
        ret = ay_pnode_regexp_has_nocase(node->child);
        ret &= ay_pnode_regexp_has_nocase(node->child->next);
    } else {
        ret = 0;
    }

    return ret;
}

/**
 * @brief Check if pnode subtree contains term with tag @p tag.
 *
 * Goes through the ay_pnode.ref.
 *
 * @param[in] node Subtree to check.
 * @param[in] tag Augeas term tag to search.
 * @return 1 if @p tag was found.
 */
static ly_bool
ay_pnode_peek(struct ay_pnode *node, enum term_tag tag)
{
    uint32_t i;
    ly_bool ret;

    for (i = 0; i <= node->descendants; i++) {
        if (node->term->tag == tag) {
            return 1;
        } else if (AY_PNODE_REF(node)) {
            ret = ay_pnode_peek(node->ref, tag);
            AY_CHECK_RET(ret);
        }
    }

    return 0;
}

/**
 * @brief Print regular expression from pnodes to buffer.
 *
 * @param[in] buffer Large enough buffer to hold the printed string.
 * @param[in,out] idx Current index in which a character can be printed.
 * @param[in] regex Subtree of pnodes related to the regex.
 * @return 0 on success.
 */
static int
ay_pnode_print_regex_to_buffer(char *buffer, uint64_t *idx, struct ay_pnode *regex)
{
    int ret = 0;
    LY_ARRAY_COUNT_TYPE i;
    struct ay_pnode *iter;
    struct regexp *re;
    char *str;

    for (i = 0; i <= regex->descendants; i++) {
        iter = &regex[i];
        switch (iter->term->tag) {
        case A_UNION:
            ay_pnode_print_regex_to_buffer(buffer, idx, iter->child);
            buffer[(*idx)++] = '|';
            ay_pnode_print_regex_to_buffer(buffer, idx, iter->child->next);
            i += iter->descendants;
            break;
        case A_CONCAT:
            if (ay_pnode_peek(iter->child, A_UNION)) {
                buffer[(*idx)++] = '(';
                ay_pnode_print_regex_to_buffer(buffer, idx, iter->child);
                buffer[(*idx)++] = ')';
            } else {
                ay_pnode_print_regex_to_buffer(buffer, idx, iter->child);
            }
            if (ay_pnode_peek(iter->child->next, A_UNION)) {
                buffer[(*idx)++] = '(';
                ay_pnode_print_regex_to_buffer(buffer, idx, iter->child->next);
                buffer[(*idx)++] = ')';
            } else {
                ay_pnode_print_regex_to_buffer(buffer, idx, iter->child->next);
            }
            i += iter->descendants;
            break;
        case A_VALUE:
            if (iter->term->value->tag == V_STRING) {
                /* Convert string to regexp. */
                re = make_regexp_literal(iter->term->value->info, iter->term->value->string->str);
                AY_CHECK_COND((re == NULL), AYE_MEMORY);
                str = re->pattern->str;
                strcpy(&buffer[*idx], str);
                *idx += strlen(str);
                unref(re, regexp);
            } else {
                assert(iter->term->value->tag == V_REGEXP);
                str = iter->term->value->regexp->pattern->str;
                strcpy(&buffer[*idx], str);
                *idx += strlen(str);
            }
            break;
        case A_IDENT:
            assert(iter->ref);
            if (AY_PNODE_REF(iter)) {
                ret = ay_pnode_print_regex_to_buffer(buffer, idx, iter->ref);
                AY_CHECK_RET(ret);
            } else {
                assert(iter->regexp && (iter->flags & AY_PNODE_HAS_REGEXP));
                str = iter->regexp->pattern->str;
                strcpy(&buffer[*idx], str);
                *idx += strlen(str);
            }
            break;
        case A_REP:
            buffer[(*idx)++] = '(';
            ret = ay_pnode_print_regex_to_buffer(buffer, idx, iter->child);
            AY_CHECK_RET(ret);
            buffer[(*idx)++] = ')';
            switch (iter->term->quant) {
            case Q_STAR:
                buffer[(*idx)++] = '*';
                break;
            case Q_PLUS:
                buffer[(*idx)++] = '+';
                break;
            case Q_MAYBE:
                buffer[(*idx)++] = '?';
                break;
            }
            i += iter->descendants;
            break;
        default:
            break;
        }
    }

    return ret;
}

static int ay_print_regex_standardized(struct ly_out *out, const char *patt);

/**
 * @brief Calculate the length of the string for the regular expression.
 *
 * @param[in] regex Subtree of pnodes representing the regular expression to be converted to a string.
 * @return Length of string.
 */
static uint64_t
ay_pnode_regex_buffer_size(struct ay_pnode *regex)
{
    LY_ARRAY_COUNT_TYPE i;
    uint64_t ret = 0;
    struct ay_pnode *iter;

    for (i = 0; i <= regex->descendants; i++) {
        iter = &regex[i];
        switch (iter->term->tag) {
        case A_UNION:
            /* | */
            ret += 1;
            break;
        case A_CONCAT:
            /* ()() */
            ret += 4;
            break;
        case A_VALUE:
            assert((iter->term->value->tag == V_STRING) || (iter->term->value->tag == V_REGEXP));
            if (iter->term->value->tag == V_STRING) {
                /* Assume that every character can be escaped. */
                ret += 2 * strlen(iter->term->value->string->str);
            } else {
                ret += strlen(iter->term->value->regexp->pattern->str);
            }
            break;
        case A_IDENT:
            if (AY_PNODE_REF(iter)) {
                ret += ay_pnode_regex_buffer_size(iter->ref);
            } else {
                assert(iter->regexp && (iter->flags & AY_PNODE_HAS_REGEXP));
                ret += strlen(iter->regexp->pattern->str);
            }
            break;
        case A_REP:
            /* ()* */
            ret += 3;
            break;
        default:
            break;
        }
    }

    return ret;
}

/**
 * @brief Print regular expression in @p regex subtree.
 *
 * @param[out] out Output where the regex is printed.
 * @param[in] regex Subtree of pnodes related to the regex.
 * @return 0 on success.
 */
static int
ay_pnode_print_regex(struct ly_out *out, struct ay_pnode *regex)
{
    int ret;
    uint64_t size, idx = 0;
    char *buffer;

    size = ay_pnode_regex_buffer_size(regex);
    buffer = malloc(size + 1);
    if (!buffer) {
        return AYE_MEMORY;
    }

    ret = ay_pnode_print_regex_to_buffer(buffer, &idx, regex);
    AY_CHECK_GOTO(ret, free);

    buffer[idx] = '\0';
    ay_print_regex_standardized(out, buffer);

free:
    free(buffer);
    return ret;
}

/**
 * @brief Print caseless flag in the pattern.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Subtree of pnodes related to regexp.
 */
static void
ay_pnode_print_yang_pattern_nocase(struct yprinter_ctx *ctx, struct ay_pnode *node)
{
    if (ay_pnode_regexp_has_nocase(node)) {
        ly_print(ctx->out, "(?i)");
    }
}

/**
 * @brief If possible, iterate over the ay_pnode.ref.
 *
 * @param[in] regex Subtree of pnodes related to regex.
 * @return Some pnode or @p regex.
 */
static struct ay_pnode *
ay_pnode_ref_apply(struct ay_pnode *regex)
{
    if (AY_PNODE_REF(regex)) {
        return ay_pnode_ref_apply(regex->ref);
    } else {
        return regex;
    }
}

/**
 * @brief Print yang pattern by pnode regex.
 *
 * @param[in] ctx Context for printing.
 * @param[in] regex Subtree of pnodes related to regexp.
 */
static int
ay_print_yang_pattern_by_pnode_regex(struct yprinter_ctx *ctx, struct ay_pnode *regex)
{
    int ret;

    ly_print(ctx->out, "%*spattern \"", ctx->space, "");
    ay_pnode_print_yang_pattern_nocase(ctx, regex);
    ret = ay_pnode_print_regex(ctx->out, regex);
    ly_print(ctx->out, "\"");

    return ret;
}

/**
 * @brief Check or set flag AY_PNODE_REG_UNMIN.
 *
 * @param[in] node Corresponding ynode to check flags.
 * @param[in] pnode Pnode to which a flag can be set.
 * @return 1 if AY_PNODE_REG_UNMIN flag is set.
 */
static ly_bool
ay_yang_type_is_regex_unmin(const struct ay_ynode *node, struct ay_pnode *pnode)
{
    if (pnode && (pnode->flags & AY_PNODE_REG_UNMIN)) {
        return 1;
    } else if (pnode && (pnode->flags & AY_PNODE_REG_MINUS) && (pnode->term->tag == A_UNION) &&
            !(node->flags & AY_WHEN_TARGET)) {
        pnode->flags |= AY_PNODE_REG_UNMIN;
        return 1;
    } else {
        return 0;
    }
}

/**
 * @brief Print yang patterns with modifier invert-match.
 *
 * @param[in] ctx Context for printing.
 * @param[in] regex Subtree of pnodes related to regexp.
 * @return 0 on success.
 */
static int
ay_print_yang_pattern_minus(struct yprinter_ctx *ctx, const struct ay_pnode *regex)
{
    int ret;

    ret = ay_print_yang_pattern_by_pnode_regex(ctx, regex->child);
    AY_CHECK_RET(ret);
    ly_print(ctx->out, ";\n");
    /* Print pattern with invert-match. */
    ret = ay_print_yang_pattern_by_pnode_regex(ctx, regex->child->next);
    AY_CHECK_RET(ret);
    ay_print_yang_nesting_begin(ctx);
    ly_print(ctx->out, "%*smodifier invert-match;\n", ctx->space, "");
    ay_print_yang_nesting_end(ctx);

    return ret;
}

/**
 * @brief Print yang pattern.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node to print.
 * @param[in] lnode Node of type lnode containing string/regex for printing.
 * @return 0 on success.
 */
static int
ay_print_yang_pattern(struct yprinter_ctx *ctx, const struct ay_ynode *node, const struct ay_lnode *lnode)
{
    int ret = 0;
    const char *subpatt;

    if (!(node->flags & AY_WHEN_TARGET) && lnode->pnode && (lnode->pnode->term->tag == A_MINUS)) {
        ay_print_yang_pattern_minus(ctx, lnode->pnode);
        return ret;
    }
    assert(lnode->lens);
    if (lnode->lens->tag == L_VALUE) {
        ly_print(ctx->out, "%*spattern \"%s\";\n", ctx->space, "", lnode->lens->string->str);
        return ret;
    }

    assert((lnode->lens->tag == L_KEY) || (lnode->lens->tag == L_STORE));
    ly_print(ctx->out, "%*spattern \"", ctx->space, "");
    ay_print_yang_pattern_nocase(ctx, lnode->lens->regexp);

    if ((lnode->flags & AY_LNODE_KEY_HAS_IDENTS) && (node->type == YN_KEY)) {
        subpatt = ay_ynode_get_substr_from_transl_table(ctx->tree, node->parent);
        ly_print(ctx->out, "%s\";\n", subpatt);
    } else if (lnode->flags & AY_LNODE_KEY_HAS_IDENTS) {
        subpatt = ay_ynode_get_substr_from_transl_table(ctx->tree, node);
        ly_print(ctx->out, "%s\";\n", subpatt);
    } else {
        ay_print_regex_standardized(ctx->out, lnode->lens->regexp->pattern->str);
        ly_print(ctx->out, "\";\n");
    }

    return ret;
}

/**
 * @brief Print type-stmt string and also pattern-stmt if necessary.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node of type ynode to which the string type is to be printed.
 * @param[in] lnode Node of type lnode containing string/regex for printing. It can be NULL.
 * @return 0 on success.
 */
static int
ay_print_yang_type_string(struct yprinter_ctx *ctx, const struct ay_ynode *node, const struct ay_lnode *lnode)
{
    int ret = 0;

    if (!lnode) {
        ly_print(ctx->out, "%*stype string;\n", ctx->space, "");
        return ret;
    }

    ly_print(ctx->out, "%*stype string", ctx->space, "");
    ay_print_yang_nesting_begin(ctx);

    ay_print_yang_pattern(ctx, node, lnode);

    ay_print_yang_nesting_end(ctx);

    return ret;
}

/**
 * @brief Print yang type union item whose regexp will be printed from parsed node.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node of type ynode to which the union is to be printed.
 * @param[in] regex Subtree of pnodes related to regexp.
 * @return 0 on success.
 */
static int
ay_print_yang_type_union_item_from_regex(struct yprinter_ctx *ctx, const struct ay_ynode *node, struct ay_pnode *regex)
{
    int ret;
    struct ay_lnode wrapper = {0};
    struct ay_pnode *pnode;

    if (ay_pnode_peek(regex, A_MINUS) && (pnode = ay_pnode_ref_apply(regex)) && (pnode->term->tag == A_MINUS)) {
        wrapper.pnode = pnode;
        ret = ay_print_yang_type_string(ctx, node, &wrapper);
    } else {
        ly_print(ctx->out, "%*stype string", ctx->space, "");
        ay_print_yang_nesting_begin(ctx);
        ret = ay_print_yang_pattern_by_pnode_regex(ctx, regex);
        ly_print(ctx->out, ";\n");
        ay_print_yang_nesting_end(ctx);
    }

    return ret;
}

/**
 * @brief Print yang type union items whose regexp will be printed from parsed node.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node of type ynode to which the union is to be printed.
 * @param[in] regex Subtree of pnodes related to regexp.
 * @return 0 on success.
 */
static int
ay_print_yang_type_union_items_from_regex(struct yprinter_ctx *ctx, const struct ay_ynode *node,
        const struct ay_lnode *lnode)
{
    int ret;
    struct ay_pnode *uni;

    assert(lnode->pnode->term->tag == A_UNION);

    /* Get first A_UNION item. */
    for (uni = lnode->pnode; uni->term->tag != A_UNION; uni = uni->child) {}

    for ( ; uni != lnode->pnode->parent; uni = uni->parent) {
        ret = ay_print_yang_type_union_item_from_regex(ctx, node, uni->child);
        AY_CHECK_RET(ret);

        ret = ay_print_yang_type_union_item_from_regex(ctx, node, uni->child->next);
        AY_CHECK_RET(ret);
    }

    return 0;
}

/**
 * @brief Assign a yang type to a specific lense in the module.
 *
 * @param[in] modname Name of the module to which the @p ident belongs.
 * @param[in] ident Name of the lense.
 * @return Yang built-In Type name or NULL.
 */
static const char *
ay_get_yang_type_by_lense_name(const char *modname, const char *ident)
{
    const char *ret = NULL;

    if (!ident) {
        return ret;
    }

    if (!strcmp(modname, "Rx")) {
        if (!strcmp("integer", ident)) {
            ret = "uint64";
        } else if (!strcmp("relinteger", ident) || !strcmp("relinteger_noplus", ident)) {
            ret = "int64";
        } else if (!strcmp("ip", ident)) {
            ret = "inet:ip-address-no-zone";
        } else if (!strcmp("ipv4", ident)) {
            ret = "inet:ipv4-address-no-zone";
        } else if (!strcmp("ipv6", ident)) {
            ret = "inet:ipv6-address-no-zone";
        }
        /* !strcmp("reldecimal", ident) || !strcmp("decimal", ident) -> decimal64 but what fraction-digits stmt? */
    }

    return ret;
}

/**
 * @brief Print type built-in yang type.
 *
 * @param[in] ctx Context for printing.
 * @param[in] reg Lense containing regular expression to print.
 * @return 0 if type was printed successfully.
 */
static int
ay_print_yang_type_builtin(struct yprinter_ctx *ctx, struct lens *reg)
{
    int ret = 0;
    const char *ident = NULL, *type, *pattern;
    const char *filename = NULL;
    size_t len = 0;

    assert(reg);

    if ((reg->tag != L_STORE) && (reg->tag != L_KEY)) {
        return 1;
    }

    ay_get_filename(reg->regexp->info->filename->str, &filename, &len);

    if (!strncmp(filename, "rx", len)) {
        ident = ay_get_lense_name_by_modname("Rx", reg);
    } else {
        ident = ay_get_lense_name_by_regex(ctx->aug, "Rx", reg->regexp->pattern->str, 1);
    }

    type = ay_get_yang_type_by_lense_name("Rx", ident);

    if (!type) {
        pattern = reg->regexp->pattern->str;
        if (!strcmp("[0-9]+", pattern)) {
            type = "uint64";
        } else if (!strcmp("[-+]?[0-9]+", pattern) || !strcmp("[-]?[0-9]+", pattern)) {
            type = "int64";
        } else if (!strcmp("true|false", pattern) || !strcmp("(true|false)", pattern) ||
                !strcmp("false|true", pattern) || !strcmp("(false|true)", pattern)) {
            type = "boolean";
        } else {
            type = NULL;
        }
    }

    if (type) {
        ly_print(ctx->out, "%*stype %s;\n", ctx->space, "", type);
    } else {
        ret = 1;
    }

    return ret;
}

/**
 * @brief Print type-stmt statement.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node of type ynode to which the type is to be printed.
 * @param[in] lnode Node of type lnode containing string/regex for printing.
 * @return 0 if print was successful otherwise nothing is printed.
 */
static int
ay_print_yang_type_item(struct yprinter_ctx *ctx, const struct ay_ynode *node, const struct ay_lnode *lnode)
{
    int ret;
    char *valstr;

    valstr = (lnode->lens->tag == L_VALUE) ? lnode->lens->string->str : NULL;
    ret = ay_print_yang_type_builtin(ctx, lnode->lens);
    if (!ret ||
            /* If this condition evaluates to true, then it is assumed that the empty string has already been printed. */
            (valstr && (valstr[0] == '\0'))) {
        return 0;
    }

    /* The builtin print failed, so print just string pattern. */
    if (valstr && !isspace(valstr[0]) && !isspace(valstr[strlen(valstr) - 1])) {
        ret = ay_print_yang_enumeration(ctx, lnode->lens);
    } else {
        ret = ay_print_yang_type_string(ctx, node, lnode);
    }

    return ret;
}

/**
 * @brief Print yang union-stmt types.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node of type ynode to which the string type is to be printed.
 * @param[in] key Key fo type lnode from the dnode dictionary. The key and its values as union types will be printed.
 * @return 0 on success.
 */
static int
ay_print_yang_type_union_items(struct yprinter_ctx *ctx, const struct ay_ynode *node, struct ay_dnode *key)
{
    int ret = 0;
    uint64_t i;
    const struct ay_lnode *item;

    assert(AY_DNODE_IS_KEY(key));

    /* Print dnode KEY'S VALUES. */
    AY_DNODE_KEYVAL_FOR(key, i) {
        item = key[i].lnode;
        if (ay_yang_type_is_regex_unmin(node, item->pnode)) {
            ret = ay_print_yang_type_union_items_from_regex(ctx, node, item);
        } else {
            assert((item->lens->tag == L_STORE) || (item->lens->tag == L_KEY) || (item->lens->tag == L_VALUE));
            ret = ay_print_yang_type_item(ctx, node, item);
        }
        AY_CHECK_RET(ret);
    }

    return ret;
}

/**
 * @brief Check if empty string should be printed.
 *
 * @param[in] lens Lense in which the regular expression is check.
 * @return 1 if empty string is in @p lens.
 */
static ly_bool
ay_yang_type_is_empty_string(const struct lens *lens)
{
    const char *rpstr;
    size_t rplen;
    const char *patstr = "{0,1}";
    const size_t patlen = 5; /* strlen("{0,1}") */

    if ((lens->tag == L_LABEL) || (lens->tag == L_VALUE)) {
        return lens->string->str[0] == '\0';
    }

    assert((lens->tag == L_KEY) || (lens->tag == L_STORE));
    rpstr = lens->regexp->pattern->str;
    rplen = strlen(rpstr);
    if (rplen < patlen) {
        return 0;
    }
    return strncmp(rpstr + rplen - patlen, patstr, patlen) == 0;
}

/**
 * @brief Resolve printing of yang node type.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node for which the type-stmt is to be printed.
 * @return 0 on success.
 */
static int
ay_print_yang_type(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    int ret = 0;
    struct lens *label, *value;
    const struct ay_lnode *lnode;
    struct ay_dnode *key;
    uint8_t lv_type;
    ly_bool empty_string = 0, empty_type = 0, reg_unmin = 0;
    uint64_t i;

    if (!node->label && !node->value) {
        return ret;
    }

    label = AY_LABEL_LENS(node);
    value = AY_VALUE_LENS(node);
    if (!value && label &&
            (((node->type == YN_LEAF) && (node->label->flags & AY_LNODE_KEY_NOREGEX)) ||
            (label->tag == L_LABEL))) {
        ly_print(ctx->out, "%*stype empty;\n", ctx->space, "");
        return ret;
    } else if ((node->type == YN_VALUE) || (value &&
            (AY_LABEL_LENS_IS_IDENT(node) ||
            ((value->tag == L_STORE) && (!label || (label->tag != L_KEY)))))) {
        lnode = node->value;
        lv_type = AY_LV_TYPE_VALUE;
    } else if (label && (label->tag == L_KEY)) {
        lnode = node->label;
        lv_type = AY_LV_TYPE_LABEL;
    } else {
        ret = ay_print_yang_type_string(ctx, node, NULL);
        return ret;
    }
    assert(lnode && lnode->lens);

    /* Set dnode key if exists. */
    if (lv_type == AY_LV_TYPE_LABEL) {
        key = ay_dnode_find(AY_YNODE_ROOT_LABELS(ctx->tree), lnode);
    } else {
        assert(lv_type == AY_LV_TYPE_VALUE);
        key = ay_dnode_find(AY_YNODE_ROOT_VALUES(ctx->tree), lnode);
    }

    /* Set empty_string and empty_type. */
    if (key) {
        /* Iterate over key and its values .*/
        AY_DNODE_KEYVAL_FOR(key, i) {
            if (empty_string && empty_type && reg_unmin) {
                /* Both are set. */
                break;
            }
            if (!empty_string) {
                empty_string = ay_yang_type_is_empty_string(key[i].lnode->lens);
            }
            if (!empty_type) {
                empty_type = ay_yang_type_is_empty(key[i].lnode);
            }
        }
    } else {
        empty_string = ay_yang_type_is_empty_string(lnode->lens);
        empty_type = ay_yang_type_is_empty(lnode);
        reg_unmin = ay_yang_type_is_regex_unmin(node, lnode->pnode);
    }

    if (empty_type && (node->type == YN_VALUE) && (node->flags & AY_YNODE_MAND_FALSE)) {
        empty_type = 0;
    }

    /* Print union */
    if (empty_string || empty_type || reg_unmin || key) {
        ly_print(ctx->out, "%*stype union", ctx->space, "");
        ay_print_yang_nesting_begin(ctx);
    }

    if (empty_string) {
        /* print empty string */
        ly_print(ctx->out, "%*stype string", ctx->space, "");
        ay_print_yang_nesting_begin(ctx);
        ly_print(ctx->out, "%*slength 0;\n", ctx->space, "");
        ay_print_yang_nesting_end(ctx);
    }
    if (empty_type) {
        /* print empty type */
        ly_print(ctx->out, "%*stype empty;\n", ctx->space, "");
    }

    if (key) {
        /* Print other types in union. */
        ay_print_yang_type_union_items(ctx, node, key);
    } else if (reg_unmin) {
        ay_print_yang_type_union_items_from_regex(ctx, node, lnode);
    } else {
        /* Print lnode type. */
        ret = ay_print_yang_type_item(ctx, node, lnode);
    }

    /* End of union. */
    if (empty_string || empty_type || reg_unmin || key) {
        ay_print_yang_nesting_end(ctx);
    }

    return ret;
}

static int ay_print_yang_node(struct yprinter_ctx *ctx, struct ay_ynode *node);

/**
 * @brief Iterate over to all node's children and call print function.
 *
 * @param[in] ctx Context for printing.
 * @param[node] node Current node which may have children.
 * @return 0 on success.
 */
static int
ay_print_yang_children(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    int ret = 0;
    struct ay_ynode *iter;

    for (iter = node->child; iter; iter = iter->next) {
        ret = ay_print_yang_node(ctx, iter);
        AY_CHECK_RET(ret);
    }

    return ret;
}

/**
 * @brief Print yang when-stmt.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node to process.
 */
static void
ay_print_yang_when(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    struct ay_ynode *child, *parent, *refnode, *valnode;
    struct lens *value;
    ly_bool is_simple;
    const char *str;
    uint64_t i, j, path_cnt;

    if (!node->when_ref) {
        return;
    }

    /* Get referenced node. */
    refnode = NULL;
    path_cnt = 0;
    for (parent = node->parent; parent; parent = parent->parent) {
        if (parent->type != YN_CASE) {
            ++path_cnt;
        }
        if (parent->id == node->when_ref) {
            refnode = parent;
            break;
        }
        /* The entire subtree is searched, but the 'parent' child should actually be found. Additionally, it can be
         * wrapped in a YN_LIST, complicating a simple search using a 'for' loop.
         */
        for (j = 0; j < parent->descendants; j++) {
            child = &parent[j + 1];
            if (child->id == node->when_ref) {
                refnode = child;
                break;
            }
        }
        if (refnode) {
            break;
        }
    }
    assert(parent);

    if (parent->type == YN_CASE) {
        path_cnt++;
    }
    if ((node->type == YN_CASE) && (path_cnt > 0)) {
        /* In YANG, the case-stmt is not counted in the path. */
        assert(path_cnt);
        path_cnt--;
    }
    if (!refnode) {
        /* Warning: when is ignored. */
        fprintf(stderr, "augyang warn: 'when' has invalid path and therefore will not be generated "
                "(id = %" PRIu32 ", when_ref = %" PRIu32 ").\n", node->id, node->when_ref);
        return;
    }

    /* Print 'when' statement. */
    if (!ay_ynode_when_value_is_valid(node)) {
        /* The 'when' is not valid from the point of view of the XPATH 1.0 standard,
         * so at least the 'when' restriction is printed as a comment.
         */
        ly_print(ctx->out, "%*s//when \"", ctx->space, "");
    } else {
        ly_print(ctx->out, "%*swhen \"", ctx->space, "");
    }
    value = node->when_val->lens;
    assert((value->tag == L_VALUE) || (value->tag == L_STORE));
    if (ay_lense_pattern_is_label(value)) {
        /* The L_STORE pattern is just simple name. */
        is_simple = 1;
    } else {
        /* The 'when' expression is more complex. */
        ly_print(ctx->out, "re-match(");
        is_simple = 0;
    }

    /* Print path to referenced node. */
    for (i = 0; i < path_cnt; i++) {
        ly_print(ctx->out, "../");
    }
    if ((refnode->parent->type == YN_LIST) && (refnode->parent->parent == parent)) {
        /* Print list name. */
        ay_print_yang_ident(ctx, refnode->parent, AY_IDENT_NODE_NAME);
        ly_print(ctx->out, "/");
    }

    /* Print name of referenced node. */
    valnode = ay_ynode_get_value_node(ctx->tree, refnode, refnode->label, refnode->value);
    if ((refnode != parent) && valnode) {
        /* Print name of referenced node. */
        ay_print_yang_ident(ctx, refnode, AY_IDENT_NODE_NAME);
        ly_print(ctx->out, "/");
        /* Print name of referenced node's value. */
        ay_print_yang_ident(ctx, valnode, AY_IDENT_NODE_NAME);
    } else if (valnode) {
        /* Print name of referenced node's child (value). */
        ay_print_yang_ident(ctx, valnode, AY_IDENT_NODE_NAME);
    } else {
        /* Print name of referenced node. */
        ay_print_yang_ident(ctx, refnode, AY_IDENT_NODE_NAME);
    }
    /* Print value/regex for comparison. */
    str = (value->tag == L_VALUE) ? value->string->str : value->regexp->pattern->str;
    if (is_simple && !value->regexp->nocase) {
        /* String is just simple name. */
        ly_print(ctx->out, "=\'", str);
        ay_print_string_standardized(ctx->out, str);
        ly_print(ctx->out, "\'\";\n", str);
    } else {
        /* The 'when' expression is more complex, continue with printing of re-match function. */
        ly_print(ctx->out, ", \'");
        ay_print_regex_standardized(ctx->out, str);
        ly_print(ctx->out, "\')\";\n");
    }
}

/**
 * @brief Print yang description.
 *
 * @param[in] ctx Context for printing.
 * @param[in] msg Message in yang description.
 */
static void
ay_print_yang_description(struct yprinter_ctx *ctx, char *msg)
{
    ly_print(ctx->out, "%*sdescription\n", ctx->space, "");
    ly_print(ctx->out, "%*s\"%s\";\n", ctx->space + SPACE_INDENT, "", msg);
}

/**
 * @brief Print dat-path for @p node.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node for which the data-path is to be printed.
 * @return 0 on success.
 */
static int
ay_print_yang_data_path(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    int ret = 0;
    struct lens *label;

    label = AY_LABEL_LENS(node);
    if (!label || (node->type == YN_VALUE) || (node->type == YN_KEY)) {
        return ret;
    }

    ly_print(ctx->out, "%*s"AY_EXT_PREFIX ":"AY_EXT_PATH " \"", ctx->space, "");

    if (AY_LABEL_LENS_IS_IDENT(node)) {
        ret = ay_print_yang_ident(ctx, node, AY_IDENT_DATA_PATH);
    } else {
        ly_print(ctx->out, "$$");
    }

    ly_print(ctx->out, "\";\n");

    return ret;
}

/**
 * @brief Print value-yang-path.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node for which the value-yang-path is to be printed.
 * @return 0 on success.
 */
static int
ay_print_yang_value_path(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    int ret = 0;
    struct ay_ynode *valnode;
    struct lens *value;

    value = AY_VALUE_LENS(node);

    if (!value || (node->type == YN_CASE) || (node->type == YN_KEY) || (node->type == YN_VALUE) ||
            ((node->type == YN_LEAF) && AY_LABEL_LENS_IS_IDENT(node))) {
        return ret;
    }

    ly_print(ctx->out, "%*s"AY_EXT_PREFIX ":"AY_EXT_VALPATH " \"", ctx->space, "");

    valnode = ay_ynode_get_value_node(ctx->tree, node, node->label, node->value);
    assert(valnode);
    ret = ay_print_yang_ident(ctx, valnode, AY_IDENT_VALUE_YPATH);
    ly_print(ctx->out, "\";\n");

    return ret;
}

/**
 * @brief Print YANG min-elements statement.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node for which the min-elements is to be printed.
 */
static void
ay_print_yang_minelements(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    if (((node->type == YN_LIST) && node->choice && !ay_ynode_alone_in_choice(node) && (node->min_elems < 2)) ||
            (ay_ynode_alone_in_choice(node) && (node->flags & AY_CHOICE_MAND_FALSE))) {
        return;
    } else if (node->min_elems) {
        ly_print(ctx->out, "%*smin-elements %" PRIu16 ";\n", ctx->space, "", node->min_elems);
    } else if (node->flags & AY_YNODE_MAND_TRUE) {
        ly_print(ctx->out, "%*smin-elements 1;\n", ctx->space, "");
    }
}

/**
 * @brief Print yang leaf-list-stmt.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node of type YN_LEAFLIST.
 * @return 0 on success.
 */
static int
ay_print_yang_leaflist(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    int ret = 0;

    ly_print(ctx->out, "%*sleaf-list ", ctx->space, "");
    ret = ay_print_yang_ident(ctx, node, AY_IDENT_NODE_NAME);
    AY_CHECK_RET(ret);
    ay_print_yang_nesting_begin2(ctx, node->id);

    ay_print_yang_minelements(ctx, node);
    ret = ay_print_yang_type(ctx, node);
    AY_CHECK_RET(ret);
    ay_print_yang_when(ctx, node);
    ly_print(ctx->out, "%*sordered-by user;\n", ctx->space, "");
    ret = ay_print_yang_data_path(ctx, node);
    AY_CHECK_RET(ret);

    ay_print_yang_nesting_end(ctx);

    return ret;
}

/**
 * @brief Print yang mandatory-stmt.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node for which the mandatory-stmt is to be printed.
 */
static void
ay_print_yang_mandatory(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    if (ay_ynode_alone_in_choice(node) && (node->flags & AY_CHOICE_MAND_FALSE)) {
        return;
    }
    if ((node->flags & AY_YNODE_MAND_TRUE) && !node->choice && !node->when_val) {
        ly_print(ctx->out, "%*smandatory true;\n", ctx->space, "");
    }
}

/**
 * @brief Print yang leaf-stmt.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node printed as leaf-stmt.
 * @return 0 on success.
 */
static int
ay_print_yang_leaf(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    int ret = 0;

    ly_print(ctx->out, "%*sleaf ", ctx->space, "");
    ret = ay_print_yang_ident(ctx, node, AY_IDENT_NODE_NAME);
    AY_CHECK_RET(ret);
    ay_print_yang_nesting_begin2(ctx, node->id);

    ay_print_yang_mandatory(ctx, node);
    ret = ay_print_yang_type(ctx, node);
    AY_CHECK_RET(ret);
    ret = ay_print_yang_data_path(ctx, node);
    AY_CHECK_RET(ret);
    ret = ay_print_yang_value_path(ctx, node);
    ay_print_yang_when(ctx, node);

    ay_print_yang_nesting_end(ctx);

    return ret;
}

/**
 * @brief Print yang leafref-stmt.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node printed as leafref-stmt.
 * @return 0 on success.
 */
static int
ay_print_yang_leafref(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    int ret = 0;
    struct ay_ynode *iter;
    struct lens *snode;

    ly_print(ctx->out, "%*sleaf ", ctx->space, "");
    ret = ay_print_yang_ident(ctx, node, AY_IDENT_NODE_NAME);
    AY_CHECK_RET(ret);
    ay_print_yang_nesting_begin2(ctx, node->id);

    ly_print(ctx->out, "%*stype leafref", ctx->space, "");
    ay_print_yang_nesting_begin(ctx);
    ly_print(ctx->out, "%*spath \"../../", ctx->space, "");
    for (iter = node->parent; iter; iter = iter->parent) {
        snode = AY_SNODE_LENS(iter);
        if (snode && (snode->tag == L_REC) && (snode->body == node->snode->lens->body)) {
            break;
        }
        ly_print(ctx->out, "../");
    }
    assert(iter);
    ay_print_yang_ident(ctx, iter, AY_IDENT_NODE_NAME);
    ly_print(ctx->out, "/_r-id\";\n");
    ay_print_yang_nesting_end(ctx);

    ay_print_yang_description(ctx, "Implicitly generated leaf to maintain recursive augeas data.");
    ay_print_yang_when(ctx, node);
    ay_print_yang_nesting_end(ctx);

    return ret;
}

/**
 * @brief Print yang uses-stmt
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node printed as uses node.
 */
static int
ay_print_yang_uses(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    int ret;

    ly_print(ctx->out, "%*suses ", ctx->space, "");
    ret = ay_print_yang_ident(ctx, node, AY_IDENT_NODE_NAME);
    if (ctx->vercode & AYV_YNODE_ID_IN_YANG) {
        ly_print(ctx->out, "; // %" PRIu64 "\n", node->id);
    } else {
        ly_print(ctx->out, ";\n");
    }

    return ret;
}

/**
 * @brief Print yang leaf-stmt.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node printed as list key of type leaf.
 * @return 0 on success.
 */
static int
ay_print_yang_leaf_key(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    int ret = 0;
    struct lens *label;

    if (AY_YNODE_IS_SEQ_LIST(node->parent)) {
        ly_print(ctx->out, "%*sleaf _seq", ctx->space, "");
    } else {
        ly_print(ctx->out, "%*sleaf ", ctx->space, "");
        ret = ay_print_yang_ident(ctx, node, AY_IDENT_NODE_NAME);
        AY_CHECK_RET(ret);
    }
    ay_print_yang_nesting_begin2(ctx, node->id);
    label = AY_LABEL_LENS(node);

    if (node->parent->type == YN_CONTAINER) {
        /* print mandatory-stmt for container leaf key */
        ay_print_yang_mandatory(ctx, node);
    }

    /* print type */
    if (label && (label->tag == L_SEQ)) {
        ly_print(ctx->out, "%*stype uint64;\n", ctx->space, "");
    } else {
        ret = ay_print_yang_type(ctx, node);
        AY_CHECK_RET(ret);
    }

    if (AY_YNODE_IS_SEQ_LIST(node->parent)) {
        ay_print_yang_description(ctx, "Key contains some unique value. "
                "The order is based on the actual order of list instances.");
    }

    ay_print_yang_nesting_end(ctx);

    return ret;
}

/**
 * @brief Print yang list of files.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node of type list containing config-file as a key.
 * @return 0 on success.
 */
static int
ay_print_yang_list_files(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    int ret = 0;

    ly_print(ctx->out, "%*slist ", ctx->space, "");
    ret = ay_print_yang_ident(ctx, node, AY_IDENT_NODE_NAME);
    AY_CHECK_RET(ret);
    ay_print_yang_nesting_begin(ctx);

    ly_print(ctx->out, "%*skey \"config-file\";\n", ctx->space, "");
    ly_print(ctx->out, "%*sleaf config-file", ctx->space, "");
    ay_print_yang_nesting_begin(ctx);
    ly_print(ctx->out, "%*stype string;\n", ctx->space, "");
    ay_print_yang_nesting_end(ctx);

    ret = ay_print_yang_children(ctx, node);
    AY_CHECK_RET(ret);

    ay_print_yang_nesting_end(ctx);

    return ret;
}

/**
 * @brief Print yang list with '_seq' key.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node 'seq_list' to print. See AY_YNODE_IS_SEQ_LIST.
 * @return 0 on success.
 */
static int
ay_print_yang_seq_list(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    int ret;

    ly_print(ctx->out, "%*slist ", ctx->space, "");
    ret = ay_print_yang_ident(ctx, node, AY_IDENT_NODE_NAME);
    AY_CHECK_RET(ret);
    ay_print_yang_nesting_begin2(ctx, node->id);

    ly_print(ctx->out, "%*skey \"_seq\";\n", ctx->space, "");
    ay_print_yang_minelements(ctx, node);
    ay_print_yang_when(ctx, node);
    ly_print(ctx->out, "%*sordered-by user;\n", ctx->space, "");
    ret = ay_print_yang_data_path(ctx, node);
    AY_CHECK_RET(ret);
    ret = ay_print_yang_value_path(ctx, node);
    AY_CHECK_RET(ret);

    ret = ay_print_yang_children(ctx, node);
    AY_CHECK_RET(ret);

    ay_print_yang_nesting_end(ctx);

    return ret;
}

/**
 * @brief Print yang list-stmt.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node of type YN_LIST.
 * @return 0 on success.
 */
static int
ay_print_yang_list(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    int ret = 0;
    ly_bool is_lrec;

    if (node->parent->type == YN_ROOT) {
        ay_print_yang_list_files(ctx, node);
        return ret;
    } else if (AY_YNODE_IS_SEQ_LIST(node)) {
        ay_print_yang_seq_list(ctx, node);
        return ret;
    }

    ly_print(ctx->out, "%*slist ", ctx->space, "");
    ret = ay_print_yang_ident(ctx, node, AY_IDENT_NODE_NAME);
    AY_CHECK_RET(ret);
    ay_print_yang_nesting_begin2(ctx, node->id);

    is_lrec = node->snode && (node->snode->lens->tag == L_REC);
    if (is_lrec) {
        ly_print(ctx->out, "%*skey \"_r-id\";\n", ctx->space, "");
    } else {
        ly_print(ctx->out, "%*skey \"_id\";\n", ctx->space, "");
    }
    ay_print_yang_minelements(ctx, node);
    ay_print_yang_when(ctx, node);
    if (is_lrec) {
        ly_print(ctx->out, "%*sleaf _r-id", ctx->space, "");
    } else {
        ly_print(ctx->out, "%*sordered-by user;\n", ctx->space, "");
        ly_print(ctx->out, "%*sleaf _id", ctx->space, "");
    }
    ay_print_yang_nesting_begin(ctx);
    ly_print(ctx->out, "%*stype uint64;\n", ctx->space, "");

    if (is_lrec) {
        ay_print_yang_description(ctx, "Implicitly generated list key to maintain the recursive augeas data.");
    } else {
        ay_print_yang_description(ctx, "Implicitly generated list key to maintain the order of the augeas data.");
    }

    ay_print_yang_nesting_end(ctx);

    ret = ay_print_yang_children(ctx, node);
    AY_CHECK_RET(ret);

    ay_print_yang_nesting_end(ctx);

    return ret;
}

/**
 * @brief Print yang presence-stmt.
 *
 * @param[in] ctx Context for printing.
 * @param[in] cont Node of type YN_CONTAINER.
 */
static void
ay_print_yang_presence(struct yprinter_ctx *ctx, struct ay_ynode *cont)
{
    (void)cont;
    ly_print(ctx->out, "%*spresence \"Config entry.\";\n", ctx->space, "");
}

/**
 * @brief Print yang container-stmt.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node of type YN_CONTAINER.
 * @return 0 on success.
 */
static int
ay_print_yang_container(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    int ret = 0;

    ly_print(ctx->out, "%*scontainer ", ctx->space, "");
    ret = ay_print_yang_ident(ctx, node, AY_IDENT_NODE_NAME);
    AY_CHECK_RET(ret);
    ay_print_yang_nesting_begin2(ctx, node->id);
    ret = ay_print_yang_data_path(ctx, node);
    AY_CHECK_RET(ret);
    ret = ay_print_yang_value_path(ctx, node);
    AY_CHECK_RET(ret);
    ay_print_yang_presence(ctx, node);
    ay_print_yang_when(ctx, node);
    ret = ay_print_yang_children(ctx, node);
    AY_CHECK_RET(ret);
    ay_print_yang_nesting_end(ctx);

    return ret;
}

/**
 * @brief Print yang grouping-stmt.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node printed as grouping node.
 */
static int
ay_print_yang_grouping(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    int ret = 0;

    ly_print(ctx->out, "%*sgrouping ", ctx->space, "");
    ret = ay_print_yang_ident(ctx, node, AY_IDENT_NODE_NAME);
    AY_CHECK_RET(ret);
    ay_print_yang_nesting_begin2(ctx, node->id);

    ret = ay_print_yang_children(ctx, node);
    AY_CHECK_RET(ret);

    ay_print_yang_nesting_end(ctx);

    return ret;
}

/**
 * @brief Print node based on type.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node to print.
 * @return 0 on success.
 */
static int
ay_print_yang_node_(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    int ret;

    assert(node->type != YN_UNKNOWN);

    switch (node->type) {
    case YN_UNKNOWN:
        return 1;
    case YN_LEAF:
        ret = ay_print_yang_leaf(ctx, node);
        break;
    case YN_LEAFREF:
        ret = ay_print_yang_leafref(ctx, node);
        break;
    case YN_LEAFLIST:
        ret = ay_print_yang_leaflist(ctx, node);
        break;
    case YN_LIST:
        ret = ay_print_yang_list(ctx, node);
        break;
    case YN_CONTAINER:
        ret = ay_print_yang_container(ctx, node);
        break;
    case YN_CASE:
        /* Handling in ay_print_yang_node_in_choice(). */
        ret = 1;
        break;
    case YN_KEY:
        ret = ay_print_yang_leaf_key(ctx, node);
        break;
    case YN_VALUE:
        ret = ay_print_yang_leaf(ctx, node);
        break;
    case YN_GROUPING:
        ret = ay_print_yang_grouping(ctx, node);
        break;
    case YN_USES:
        ret = ay_print_yang_uses(ctx, node);
        break;
    case YN_REC:
    case YN_ROOT:
        ret = ay_print_yang_children(ctx, node);
        break;
    default:
        ret = 1;
        break;
    }

    return ret;
}

/**
 * @brief Print mandatory-stmt for choice-stmt.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node First ynode in choice-stmt.
 */
static void
ay_print_yang_mandatory_choice(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    if (node->flags & AY_CHOICE_MAND_FALSE) {
        return;
    } else {
        ly_print(ctx->out, "%*smandatory true;\n", ctx->space, "");
    }
}

/**
 * @brief Print yang choice-stmt.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node for case identifier evaluation.
 * @return 0 on success.
 */
static int
ay_print_yang_choice(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    int ret = 0;
    uint32_t choice_cnt;
    struct ay_ynode *iter;
    const struct ay_lnode *last_choice;
    char *ident;

    assert(node->parent);

    /* Taking care of duplicate choice names. */
    choice_cnt = 1;
    last_choice = NULL;
    for (iter = node->parent->child; iter != node; iter = iter->next) {
        if (iter->choice && (iter->choice != node->choice) && (last_choice != iter->choice) &&
                !ay_ynode_alone_in_choice(iter)) {
            choice_cnt++;
            last_choice = iter->choice;
        }
    }

    ident = node->parent->ident;
    if ((strlen(ident) <= 3) || strncmp(ident, "ch-", 3)) {
        ly_print(ctx->out, "%*schoice ch-%s", ctx->space, "", ident);
    } else {
        ly_print(ctx->out, "%*schoice %s", ctx->space, "", ident);
    }

    if (choice_cnt > 1) {
        ly_print(ctx->out, "%" PRIu32 "", choice_cnt);
    }

    return ret;
}

/**
 * @brief Print yang case-stmt.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node under case-stmt.
 * @return 0 on success.
 */
static int
ay_print_yang_case(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    int ret;

    ly_print(ctx->out, "%*scase ", ctx->space, "");
    if (node->child) {
        assert(node->type == YN_CASE);
        ret = ay_print_yang_ident(ctx, node->child, AY_IDENT_NODE_NAME);
    } else {
        assert(node->type == YN_USES);
        ret = ay_print_yang_ident(ctx, node, AY_IDENT_NODE_NAME);
    }
    ay_print_yang_nesting_begin2(ctx, node->id);
    ay_print_yang_when(ctx, node);

    return ret;
}

/**
 * @brief Print some node in the choice-stmt.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node under choice-stmt.
 * @param[in] alone Flag must be set if @p node is alone in choice statement.
 * @return 0 on success.
 */
static int
ay_print_yang_node_in_choice(struct yprinter_ctx *ctx, struct ay_ynode *node, ly_bool alone)
{
    int ret;

    if ((node->type == YN_CASE) || (node->type == YN_USES)) {
        if (!alone) {
            ret = ay_print_yang_case(ctx, node);
            AY_CHECK_RET(ret);
        }

        if (node->type == YN_CASE) {
            /* Ignore container, print only children of container. */
            ret = ay_print_yang_children(ctx, node);
        } else {
            assert(node->type == YN_USES);
            /* Print the node under case-stmt. */
            ret = ay_print_yang_node_(ctx, node);
        }

        if (!alone) {
            ay_print_yang_nesting_end(ctx);
        }
    } else {
        /* Just print the node. */
        ret = ay_print_yang_node_(ctx, node);
    }

    return ret;
}

/**
 * @brief Recursively print subtree and decide about printing choice-stmt.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Subtree to print.
 * @return 0 on success.
 */
static int
ay_print_yang_node(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    int ret = 0;
    ly_bool first = 0, alone, last, next_has_same_choice;
    struct ay_ynode *iter;
    const struct ay_lnode *choice;

    if (!node->choice) {
        return ay_print_yang_node_(ctx, node);
    }

    /* Find out if node is the first in choice-stmt. */
    choice = node->choice;
    assert(node->parent);
    for (iter = node->parent->child; iter; iter = iter->next) {
        if (iter->choice == choice) {
            first = iter == node ? 1 : 0;
            break;
        }
    }

    next_has_same_choice = node->next && (node->next->choice == choice);
    alone = first && !next_has_same_choice;
    last = !first && !next_has_same_choice;

    if (alone || (!first && !last)) {
        /* choice with one 'case' is not printed */
        ret = ay_print_yang_node_in_choice(ctx, node, alone);
    } else if (first && !last) {
        /* print choice */
        ay_print_yang_choice(ctx, node);
        /* start of choice nesting */
        ay_print_yang_nesting_begin(ctx);
        ay_print_yang_mandatory_choice(ctx, node);
        ret = ay_print_yang_node_in_choice(ctx, node, alone);
    } else {
        /* print last case */
        ret = ay_print_yang_node_in_choice(ctx, node, alone);
        /* end of choice nesting */
        ay_print_yang_nesting_end(ctx);
    }

    return ret;
}

/**
 * @brief Check if 'import ietf-inet-types' must be printed.
 *
 * @param[in] reg Lense to check.
 * @return 1 if import must be printed.
 */
static ly_bool
ay_print_yang_import_inet_types(struct lens *reg)
{
    const char *ident, *filename, *path;
    uint64_t len;

    if (reg && ((reg->tag == L_KEY) || (reg->tag == L_STORE))) {
        path = reg->regexp->info->filename->str;
        ay_get_filename(path, &filename, &len);
        if (!strncmp("rx", filename, len)) {
            ident = ay_get_lense_name_by_modname("Rx", reg);
            if (ident &&
                    (!strcmp(ident, "ip") ||
                    !strcmp(ident, "ipv4") ||
                    !strcmp(ident, "ipv6"))) {
                return 1;
            }
        }
    }

    return 0;
}

/**
 * @brief Print yang import statements.
 *
 * @param[in,out] out Output handler for printing.
 * @param[in] tree Tree of ynodes.
 */
static void
ay_print_yang_imports(struct ly_out *out, struct ay_ynode *tree)
{
    struct ay_ynode *iter;
    LY_ARRAY_COUNT_TYPE i;

    ly_print(out, "  import augeas-extension {\n");
    ly_print(out, "    prefix " AY_EXT_PREFIX ";\n");
    ly_print(out, "  }\n");

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        iter = &tree[i];

        /* Find out if ietf-inet-types needs to be imported. */
        if (ay_print_yang_import_inet_types(AY_LABEL_LENS(iter)) ||
                ay_print_yang_import_inet_types(AY_VALUE_LENS(iter))) {
            ly_print(out, "  import ietf-inet-types {\n");
            ly_print(out, "    prefix inet;\n");
            ly_print(out, "    reference\n");
            ly_print(out, "      \"RFC 6991: Common YANG Data Types\";\n");
            ly_print(out, "  }\n");
            break;
        }
    }
    ly_print(out, "\n");
}

/**
 * @brief Print ynode tree in yang format.
 *
 * @param[in] mod Module in which the tree is located.
 * @param[in] tree Ynode tree to print.
 * @param[in] vercode Decide if debugging information should be printed.
 * @param[out] str_out Printed tree in yang format. Call free() after use.
 * @return 0 on success.
 */
int
ay_print_yang(struct module *mod, struct ay_ynode *tree, uint64_t vercode, char **str_out)
{
    int ret = 0;
    struct yprinter_ctx ctx;
    struct ly_out *out = NULL;
    const char *modname;
    char *str;
    size_t i, modname_len;

    if (ly_out_new_memory(&str, 0, &out)) {
        ret = AYE_MEMORY;
        goto free;
    }

    ctx.aug = ay_get_augeas_ctx1(mod);
    ctx.mod = mod;
    ctx.tree = tree;
    ctx.vercode = vercode;
    ctx.out = out;
    ctx.space = SPACE_INDENT;

    modname = ay_get_yang_module_name(ctx.mod, &modname_len);

    ly_print(out, "module ");
    for (i = 0; i < modname_len; i++) {
        ly_print(out, "%c", modname[i] == '_' ? '-' : modname[i]);
    }
    ly_print(out, " {\n");
    ly_print(out, "  yang-version 1.1;\n");

    ly_print(out, "  namespace \"aug:");
    for (i = 0; i < modname_len; i++) {
        ly_print(out, "%c", modname[i] == '_' ? '-' : modname[i]);
    }
    ly_print(out, "\";\n");

    ly_print(out, "  prefix aug;\n\n");
    ay_print_yang_imports(out, tree);
    ly_print(out, "  " AY_EXT_PREFIX ":augeas-mod-name \"%s\";\n", mod->name);
    ly_print(out, "\n");

    ret = ay_print_yang_children(&ctx, tree);

    ly_print(out, "}\n");

    *str_out = str;

free:
    ly_out_free(out, NULL, 0);

    return ret;
}
