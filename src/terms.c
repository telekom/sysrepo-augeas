/**
 * @file terms.c
 * @author Adam Piecek <piecek@cesnet.cz>
 * @brief Parsed data of Augeas lenses.
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
#include "errcode.h"
#include "lens.h"
#include "terms.h"

/**
 * @brief The minimum number of characters a regex must contain to be considered long.
 */
#define AY_REGEX_LONG 72

/**
 * @brief Defined in augeas project in the file parser.y.
 */
int augl_parse_file(struct augeas *aug, const char *name, struct term **term);

/**
 * @brief Increment counter @p cnt.
 *
 * @param[in] term Current term.
 * @param[in,out] cnt Pointer to counter of type uint64_t.
 */
static void
ay_term_count(struct term *term, void *cnt)
{
    (void) term;
    ++(*(uint64_t *)cnt);
}

/**
 * @brief Recursively loop through the terms and call the callback function.
 *
 * @param[in] term Current term.
 * @param[in,out] data Data that the callback function can modify.
 * @param[in] func Callback function which take @p data as parameter.
 */
static void
ay_term_visitor(struct term *term, void *data, void (*func)(struct term *, void *data))
{
    func(term, data);

    switch (term->tag) {
    case A_MODULE:
        list_for_each(dcl, term->decls) {
            assert(dcl->tag == A_BIND);
            ay_term_visitor(dcl, data, func);
        }
        break;
    case A_BIND:
        ay_term_visitor(term->exp, data, func);
        break;
    case A_LET:
    case A_COMPOSE:
    case A_UNION:
    case A_MINUS:
    case A_CONCAT:
    case A_APP:
        ay_term_visitor(term->left, data, func);
        ay_term_visitor(term->right, data, func);
        break;
    case A_VALUE:
    case A_IDENT:
        break;
    case A_BRACKET:
        ay_term_visitor(term->brexp, data, func);
        break;
    case A_FUNC:
        ay_term_visitor(term->body, data, func);
        break;
    case A_REP:
        ay_term_visitor(term->rexp, data, func);
        break;
    case A_TEST:
    default:
        break;
    }
}

/**
 * @brief Set pnode ay_pnode.term and ay_pnode.descendants.
 *
 * @param[in] term Current term.
 * @param[in,out] data Pnode iterator.
 */
static void
ay_pnode_set_term(struct term *term, void *data)
{
    struct ay_pnode **iter;
    uint64_t cnt = 0;

    iter = data;
    (*iter)->term = term;
    ay_term_visitor(term, &cnt, ay_term_count);
    (*iter)->descendants = cnt - 1;
    ++(*iter);
}

/**
 * @brief Set pointers parent, child and next in the pnode tree by ay_pnode.descendants.
 *
 * Should be the same as ay_ynode_tree_correction().
 *
 * @param[in,out] tree Tree of pnodes.
 */
static void
ay_pnode_tree_correction(struct ay_pnode *tree)
{
    struct ay_pnode *parent, *iter, *next;
    uint32_t sum;

    /* Copied from ay_ynode_tree_correction(). */
    LY_ARRAY_FOR(tree, struct ay_pnode, parent) {
        iter = parent->descendants ? parent + 1 : NULL;
        parent->child = iter;
        sum = 0;
        while (iter) {
            iter->parent = parent;
            iter->child = iter->descendants ? iter + 1 : NULL;
            sum += iter->descendants + 1;
            next = sum != parent->descendants ? iter + iter->descendants + 1 : NULL;
            iter->next = next;
            iter = next;
        }
    }
}

/**
 * @brief Set ay_pnode.bind for all pnodes.
 *
 * @param[in,out] tree Tree of pnodes.
 */
static void
ay_pnode_set_bind(struct ay_pnode *tree)
{
    struct ay_pnode *bind, *iter;
    LY_ARRAY_COUNT_TYPE i;

    for (bind = tree->child; bind; bind = bind->next) {
        for (i = 0; i < bind->descendants; i++) {
            iter = &bind[i + 1];
            iter->bind = bind;
        }
    }
}

void
ay_pnode_free(struct ay_pnode *tree)
{
    if (tree) {
        unref(tree->term, term);
        LY_ARRAY_FREE(tree);
    }
}

/**
 * @brief Copy pnode data from @p dst to @p src.
 *
 * @param[out] dst Destination node.
 * @param[in] src Source node.
 */
static void
ay_pnode_copy_data(struct ay_pnode *dst, struct ay_pnode *src)
{
    dst->flags = src->flags;
    dst->bind = src->bind;
    dst->ref = src->ref;
    dst->term = src->term;
}

/**
 * @brief Swap pnode data.
 *
 * @param[in,out] first First pnode.
 * @param[in,out] second Second pnode.
 */
static void
ay_pnode_swap_data(struct ay_pnode *first, struct ay_pnode *second)
{
    struct ay_pnode tmp;

    ay_pnode_copy_data(&tmp, first);
    ay_pnode_copy_data(first, second);
    ay_pnode_copy_data(second, &tmp);
}

/**
 * @brief Check if term info are equal.
 *
 * @param[in] inf1 First info to check.
 * @param[in] inf2 Second info to check.
 * @return 1 if info are equal.
 */
static ly_bool
ay_term_info_equal(const struct info *inf1, const struct info *inf2)
{
    if ((inf1->first_line != inf2->first_line) ||
            (inf1->first_column != inf2->first_column) ||
            (inf1->last_line != inf2->last_line) ||
            (inf1->last_column != inf2->last_column) ||
            (strcmp(inf1->filename->str, inf2->filename->str) != 0)) {
        return 0;
    } else {
        return 1;
    }
}

/**
 * @brief Find pnode with the same @p info.
 *
 * @param[in] tree Tree of pnodes.
 * @param[in] info Info by which the pnode will be searched.
 * @return Pnode with equal @p info or NULL.
 */
static struct ay_pnode *
ay_pnode_find_by_info(struct ay_pnode *tree, struct info *info)
{
    uint32_t i;

    LY_ARRAY_FOR(tree, i) {
        if (ay_term_info_equal(tree[i].term->info, info)) {
            return &tree[i];
        }
    }

    return NULL;
}

/**
 * @brief Count the total number of minuses.
 *
 * @param[in] regex Subtree of pnodes related to regular expression.
 * @return Number of minuses.
 */
static uint32_t
ay_pnode_minus_count(struct ay_pnode *regex)
{
    LY_ARRAY_COUNT_TYPE i;
    struct ay_pnode *iter;
    uint32_t ret = 0;

    for (i = 0; i <= regex->descendants; i++) {
        iter = &regex[i];
        if (iter->term->tag == A_MINUS) {
            ++ret;
        } else if (AY_PNODE_REF(iter)) {
            ret += ay_pnode_minus_count(iter->ref);
        }
    }

    return ret;
}

/**
 * @brief For A_IDENT term, find a corresponding A_FUNC term in the current bind.
 *
 * @param[in] ident Pnode with tag A_IDENT.
 * @return Pnode with the same name as @p ident or NULL.
 */
static struct ay_pnode *
ay_pnode_find_func(struct ay_pnode *ident)
{
    struct ay_pnode *iter;

    assert(ident->term->tag == A_IDENT);

    for (iter = ident; iter != ident->bind; iter = iter->parent) {
        if ((iter->term->tag != A_FUNC) || (iter->parent->term->tag != A_LET)) {
            continue;
        } else if (!strcmp(iter->term->param->name->str, ident->term->ident->str)) {
            return iter;
        }
    }

    return NULL;
}

/**
 * @brief For A_IDENT term, find a corresponding A_BIND pnode.
 *
 * @param[in] tree Tree of pnodes.
 * @param[in] ident Pnode with tag A_IDENT.
 * @return Pnode with the same name as @p ident or NULL.
 */
static struct ay_pnode *
ay_pnode_find_bind(struct ay_pnode *tree, struct ay_pnode *ident)
{
    struct ay_pnode *iter;

    for (iter = tree->child; iter; iter = iter->next) {
        assert(iter->term->tag == A_BIND);
        if (!strcmp(iter->term->bname, ident->term->ident->str)) {
            return iter;
        }
    }

    return NULL;
}

/**
 * @brief Check if pnode regex subtree has all references set.
 *
 * Check if the all IDENT pnodes are resolved.
 *
 * @param[in] regex Subtree of pnodes related to regular expression.
 * @return 1 if all IDENT pnodes are resolved.
 */
static ly_bool
ay_pnode_ident_are_evaluated(struct ay_pnode *regex)
{
    int ret;
    uint32_t i;
    struct ay_pnode *iter;

    for (i = 0; i <= regex->descendants; i++) {
        iter = &regex[i];
        if (iter->term->tag != A_IDENT) {
            continue;
        }

        if (!iter->ref) {
            return 0;
        } else if (AY_PNODE_REF(iter)) {
            ret = ay_pnode_ident_are_evaluated(iter->ref);
            AY_CHECK_COND(!ret, 0);
        }
    }

    return 1;
}

/**
 * @brief Check if pnode regex contains simple expression with minus.
 *
 * And it can be simply be expressed in YANG.
 *
 * @param[in] regex Subtree of pnodes related to regex.
 * @return 1 if regular expression is considered as "simple".
 */
static ly_bool
ay_pnode_is_simple_minus_regex(struct ay_pnode *regex)
{
    ly_bool ret;
    uint32_t minus_count;

    /* Check if regex contains simple expression with minus. */
    if (AY_PNODE_REF(regex)) {
        return ay_pnode_is_simple_minus_regex(regex->ref);
    } else if (regex->term->tag == A_REP) {
        return ay_pnode_is_simple_minus_regex(regex->child);
    } else if (regex->term->tag == A_UNION) {
        ret = ay_pnode_is_simple_minus_regex(regex->child);
        ret |= ay_pnode_is_simple_minus_regex(regex->child->next);
        return ret;
    } else if (regex->term->tag != A_MINUS) {
        return 0;
    }
    minus_count = ay_pnode_minus_count(regex);
    AY_CHECK_COND(minus_count != 1, 0);

    if (!ay_pnode_ident_are_evaluated(regex)) {
        return 0;
    }

    return 1;
}

/**
 * @brief Find @p lensname in the @p mod module and return its regexp if it is of type V_REGEXP.
 *
 * @param[in] mod Augeas module in which lense will be searched.
 * @param[in] lensname Name of lense.
 * @return Pointer to regexp or NULL.
 */
static struct regexp *
ay_get_regexp_by_lensname(struct module *mod, char *lensname)
{
    struct binding *bind_iter;

    LY_LIST_FOR(mod->bindings, bind_iter) {
        assert(bind_iter->ident && bind_iter->value);
        if ((strcmp(bind_iter->ident->str, lensname) != 0) || (bind_iter->value->tag != V_REGEXP)) {
            continue;
        }

        return bind_iter->value->regexp;
    }

    return NULL;
}

/**
 * @brief Find a regular expression in some compiled module by @p ident .
 *
 * @param[in] aug Augeas context.
 * @param[in] ident Identifier in "module.lense" format is valid.
 * @return Pointer to regexp or NULL.
 */
static struct regexp *
ay_pnode_regexp_lookup_in_diff_mod(struct augeas *aug, char *ident)
{
    struct module *mod;
    char *lensname, *modname, *dot;
    size_t modname_len;

    dot = strchr(ident, '.');
    if (!dot) {
        return NULL;
    }

    modname = ident;
    modname_len = dot - ident;
    mod = ay_get_module(aug, modname, modname_len);

    lensname = dot + 1;
    assert(lensname[0] != '\0');

    return ay_get_regexp_by_lensname(mod, lensname);
}

/**
 * @brief For every pnode set ay_pnode.ref or ay_pnode.regexp.
 *
 * @param[in] aug Augeas context.
 * @param[in] tree Tree of pnodes.
 * @param[in] regex Subtree of pnodes related to regex. Only pnode with term tag A_IDENT will be affected.
 */
static void
ay_pnode_set_ref(struct augeas *aug, struct ay_pnode *tree, struct ay_pnode *regex)
{
    struct ay_pnode *ident, *func, *bind;
    struct regexp *re;
    uint32_t i;

    for (i = 0; i <= regex->descendants; i++) {
        ident = &regex[i];
        if (ident->term->tag != A_IDENT) {
            continue;
        }

        re = ay_pnode_regexp_lookup_in_diff_mod(aug, ident->term->ident->str);
        if (re) {
            ident->flags |= AY_PNODE_HAS_REGEXP;
            ident->regexp = re;
            continue;
        }

        func = ay_pnode_find_func(ident);
        if (func) {
            ident->ref = func->parent->child->next;
            ay_pnode_set_ref(aug, tree, ident->ref);
            continue;
        }

        bind = ay_pnode_find_bind(tree, ident);
        if (bind) {
            ident->ref = bind->child;
            ay_pnode_set_ref(aug, tree, ident->ref);
        }
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
 * @brief Swap pnode data if parent is A_REP and child is A_MINUS.
 *
 * If the function is applied, then the pnode tree and term tree will be different.
 * But that shouldn't be a problem. This modification made it easier to write the algorithms that follow.
 *
 * @param[in,out] regex Subtree of pnodes related to regex.
 */
static void
ay_pnode_swap_rep_minus(struct ay_pnode *regex)
{
    struct ay_pnode *iter;

    for (iter = regex; AY_PNODE_REF(iter); iter = iter->ref) {}

    if (iter->term->tag == A_UNION) {
        ay_pnode_swap_rep_minus(iter->child);
        ay_pnode_swap_rep_minus(iter->child->next);
    } else if ((iter->term->tag == A_REP) && (iter->child->term->tag == A_MINUS)) {
        ay_pnode_swap_data(iter, iter->child);
    }
}

/**
 * @brief Check if regular expression is long.
 *
 * @param[in] regex Regular expression.
 */
static ly_bool
ay_regex_is_long(const char *regex)
{
    uint64_t i, cnt;

    cnt = 0;
    for (i = 0; regex[i] != '\0'; i++) {
        ++cnt;
        if (cnt >= AY_REGEX_LONG) {
            return 1;
        }
    }

    return 0;
}

/**
 * @brief For snode, find the correct pnode to use as a name.
 *
 * @param[in] pnode whose 'struct info' is the same as for snode.
 * @return pnode from which to derive the name or NULL.
 */
static struct ay_pnode *
ay_lnode_get_pnode_name(struct ay_pnode *pnode)
{
    struct ay_pnode *iter, *prev, *ret;

    if (!pnode) {
        return NULL;
    }

    /* Must come from the correct LET without parameter. */
    ret = NULL;
    prev = pnode;
    for (iter = pnode->parent; iter && (iter->term->tag != A_BIND); iter = iter->parent) {
        if ((iter->term->tag == A_LET) && (iter->child->term->tag == A_FUNC) &&
                (prev != iter->child) && (prev->term->tag != A_FUNC)) {
            ret = iter->child;
            break;
        } else if (iter->term->tag == A_BRACKET) {
            break;
        }
        prev = iter;
    }
    assert(iter);

    if (ret && (ret->bind->child->term->tag == A_LET)) {
        return ret;
    } else if (iter->child->term->tag == A_LET) {
        assert(iter->term->tag == A_BIND);
        return iter;
    } else {
        return NULL;
    }
}

/**
 * @brief Find suitable pnode for regex shorthand.
 *
 * Applied to regex that use minus, which can be shortened with yang 'invert-match' statement.
 *
 * @param[in] aug Augeas context.
 * @param[in] ptree Tree of pnodes.
 * @param[in] pnode the pnode founded by info.
 * @return pnode or NULL.
 */
static struct ay_pnode *
ay_pnode_for_regex(struct augeas *aug, struct ay_pnode *ptree, struct ay_pnode *pnode)
{
    if (!pnode || (pnode->term->tag != A_APP)) {
        return NULL;
    }

    assert(pnode->child && pnode->child->next);
    pnode = pnode->child->next;
    ay_pnode_set_ref(aug, ptree, pnode);
    pnode = ay_pnode_ref_apply(pnode);
    ay_pnode_swap_rep_minus(pnode);

    /* TODO For AY_PNODE_REG_UNMIN form of regex, there can be more minuses. */
    if (!ay_pnode_is_simple_minus_regex(pnode)) {
        return NULL;
    }

    return pnode;
}

/**
 * @brief For every lnode set parsed node.
 *
 * The root of the pnode tree is stored in the root of the lnode node.
 * Only lnodes tagged L_STORE and L_KEY can have pnode set.
 *
 * @param[in,out] tree Tree of lnodes.
 * @param[in] ptree Tree of pnodes.
 */
static void
ay_lnode_set_pnode(struct ay_lnode *tree, struct ay_pnode *ptree)
{
    LY_ARRAY_COUNT_TYPE i;
    struct ay_lnode *iter;
    struct ay_pnode *pnode, *pnode_by_info;
    struct augeas *aug;

    aug = ay_get_augeas_ctx2(tree->lens);
    LY_ARRAY_FOR(tree, i) {
        iter = &tree[i];
        if (((iter->lens->tag == L_STORE) || (iter->lens->tag == L_KEY)) &&
                ay_regex_is_long(iter->lens->regexp->pattern->str)) {
            pnode_by_info = ay_pnode_find_by_info(ptree, iter->lens->info);
            if ((pnode = ay_pnode_for_regex(aug, ptree, pnode_by_info))) {
                pnode->flags |= AY_PNODE_REG_MINUS;
                iter->pnode = pnode;
            } else {
                pnode = ay_lnode_get_pnode_name(pnode_by_info);
                iter->pnode = pnode;
            }
        } else if ((iter->lens->tag == L_KEY) || (iter->lens->tag == L_SUBTREE)) {
            pnode_by_info = ay_pnode_find_by_info(ptree, iter->lens->info);
            pnode = ay_lnode_get_pnode_name(pnode_by_info);
            iter->pnode = pnode;
            /* flag is set in ay_ynode_snode_unique_pnode(). */
        }
    }

    /* Store root of pnode tree in lnode. */
    assert((tree->lens->tag != L_STORE) && (tree->lens->tag != L_KEY));
    tree->pnode = ptree;
}

int
ay_pnode_create(struct augeas *aug, const char *filename, struct ay_lnode *ltree, struct ay_pnode **ptree)
{
    int ret;
    struct ay_pnode *tree, *iter;
    struct term *term;
    uint64_t cnt = 0;

    ret = augl_parse_file(aug, filename, &term);
    if (ret || (aug->error->code != AUG_NOERROR)) {
        return AYE_PARSE_FAILED;
    }

    ay_term_visitor(term, &cnt, ay_term_count);
    tree = NULL;
    LY_ARRAY_CREATE(NULL, tree, cnt, return AYE_MEMORY);
    iter = tree;
    ay_term_visitor(term, &iter, ay_pnode_set_term);
    AY_SET_LY_ARRAY_SIZE(tree, cnt);
    ay_pnode_tree_correction(tree);
    ay_pnode_set_bind(tree);
    ay_lnode_set_pnode(ltree, tree);

    *ptree = tree;

    return 0;
}
