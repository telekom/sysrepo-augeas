/**
 * @file augyang.c
 * @author Adam Piecek <piecek@cesnet.cz>
 * @brief The augyang core implementation.
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

#define _GNU_SOURCE

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libyang/libyang.h>
#include <libyang/tree_edit.h>

#include "augyang.h"
#include "common.h"
#include "debug.h"
#include "lens.h"
#include "parse_regex.h"
#include "print_yang.h"
#include "terms.h"

/**
 * @brief Check if the lense cannot have children.
 *
 * @param[in] TAG Tag of the lense.
 */
#define AY_LENSE_HAS_NO_CHILD(TAG) (TAG <= L_COUNTER)

/**
 * @brief Get first child from @p LENSE.
 *
 * @param[in] LENSE Lense to examine. Its type allows to have one or more children.
 * @return Child or NULL.
 */
#define AY_GET_FIRST_LENSE_CHILD(LENSE) \
    ((LENSE->tag == L_REC) && !LENSE->rec_internal ? \
        lens->body : \
    LENSE->tag == L_REC ? \
        NULL : \
    AY_LENSE_HAS_ONE_CHILD(LENSE->tag) ? \
        lens->child : \
    lens->nchildren ? \
        lens->children[0] : NULL)

/**
 * @brief Get address of @p ITEM as if it was in the @p ARRAY1.
 *
 * Remap address @p ITEM (from @p ARRAY2) to the address in @p ARRAY1.
 * The address is calculated based on the index.
 *
 * @param[in] ARRAY1 Array to which the address of @p ITEM is converted.
 * @param[in] ARRAY2 Array where @p ITEM is located.
 * @param[in] ITEM Pointer to item in @p ARRAY2.
 * @return Address in the @p ARRAY1 or NULL.
 */
#define AY_MAP_ADDRESS(ARRAY1, ARRAY2, ITEM) \
    (ITEM ? &ARRAY1[AY_INDEX(ARRAY2, ITEM)] : NULL)

/**
 * @brief Calculate the index value based on the pointer.
 *
 * @param[in] ARRAY array of items.
 * @param[in] ITEM_PTR Pointer to item in @p ARRAY.
 */
#define AY_INDEX(ARRAY, ITEM_PTR) \
    ((ITEM_PTR) - (ARRAY))

const char *
augyang_get_error_message(int err_code)
{
    switch (err_code) {
    case AYE_MEMORY:
        return AY_NAME " ERROR: memory allocation failed.\n";
    case AYE_LENSE_NOT_FOUND:
        return AY_NAME " ERROR: Augyang does not know which lense is the root.\n";
    case AYE_L_REC:
        return AY_NAME " ERROR: lense with tag \'L_REC\' is not supported.\n";
    case AYE_DEBUG_FAILED:
        return AY_NAME " ERROR: debug test failed.\n";
    case AYE_IDENT_NOT_FOUND:
        return AY_NAME " ERROR: identifier not found. Output YANG is not valid.\n";
    case AYE_IDENT_LIMIT:
        return AY_NAME " ERROR: identifier is too long. Output YANG is not valid.\n";
    case AYE_LTREE_NO_ROOT:
        return AY_NAME " ERROR: Augyang does not know which lense is the root.\n";
    case AYE_IDENT_BAD_CHAR:
        return AY_NAME " ERROR: Invalid character in identifier.\n";
    case AYE_PARSE_FAILED:
        return AY_NAME " ERROR: Augeas failed to parse.\n";
    case AYE_INTERNAL_ERROR:
        return AY_NAME " ERROR: Augyang got into an unexpected state.\n";
    default:
        return AY_NAME " INTERNAL ERROR: error message not defined.\n";
    }
}

/**
 * @brief Go through all the lenses and set various counters.
 *
 * @param[in] lens Main lense where to start.
 * @param[out] ltree_size Number of lenses.
 * @param[out] yforest_size Number of lenses with L_SUBTREE tag.
 * @param[out] tpatt_size Maximum number of records in translation table of lens patterns.
 */
static void
ay_lense_summary(struct lens *lens, uint64_t *ltree_size, uint64_t *yforest_size, uint64_t *tpatt_size)
{
    (*ltree_size)++;
    if ((lens->tag == L_SUBTREE) || (lens->tag == L_REC)) {
        (*yforest_size)++;
    }
    if (lens->tag == L_KEY) {
        (*tpatt_size)++;
    }

    if (AY_LENSE_HAS_NO_CHILD(lens->tag)) {
        return;
    }

    if (AY_LENSE_HAS_ONE_CHILD(lens->tag)) {
        ay_lense_summary(lens->child, ltree_size, yforest_size, tpatt_size);
    } else if (AY_LENSE_HAS_CHILDREN(lens->tag)) {
        for (uint64_t i = 0; i < lens->nchildren; i++) {
            ay_lense_summary(lens->children[i], ltree_size, yforest_size, tpatt_size);
        }
    } else if ((lens->tag == L_REC) && !lens->rec_internal) {
        ay_lense_summary(lens->body, ltree_size, yforest_size, tpatt_size);
    }
}

/**
 * @brief Release translation table.
 *
 * @param[in] table Array of translation records.
 */
static void
ay_transl_table_free(struct ay_transl *table)
{
    LY_ARRAY_COUNT_TYPE i;

    LY_ARRAY_FOR(table, i) {
        ay_transl_table_substr_free(&table[i]);
    }

    LY_ARRAY_FREE(table);
}

/**
 * @brief Determine if a pnode is used for just one snode.
 *
 * If it belonged to more snode, then it should not be used to get a name,
 * because the name is probably misleading.
 *
 * TODO: this function is possibly unnecessary.
 *
 * @param[in,out] tree Tree of ynodes.
 */
static void
ay_ynode_snode_unique_pnode(struct ay_ynode *tree)
{
    struct ay_ynode *iter;
    struct ay_pnode *pnode;
    LY_ARRAY_COUNT_TYPE i;

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        iter = &tree[i];
        if (!iter->snode || !iter->snode->pnode) {
            continue;
        }
        pnode = iter->snode->pnode;
        if (pnode->flags & AY_PNODE_FOR_SNODE) {
            pnode->flags |= AY_PNODE_FOR_SNODES;
        } else {
            pnode->flags |= AY_PNODE_FOR_SNODE;
        }
    }
}

/**
 * @brief Release ynode tree.
 *
 * @param[in] tree Tree of ynodes.
 */
static void
ay_ynode_tree_free(struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i;
    struct ay_ynode_root *root;

    if (!tree) {
        return;
    }

    assert(tree->type == YN_ROOT);

    root = (struct ay_ynode_root *)tree;
    LY_ARRAY_FREE(root->ltree);
    root->ltree = NULL;
    LY_ARRAY_FREE(root->labels);
    root->labels = NULL;
    LY_ARRAY_FREE(root->values);
    root->values = NULL;
    ay_transl_table_free(root->patt_table);
    root->patt_table = NULL;

    LY_ARRAY_FOR(tree, i) {
        free(tree[i].ident);
    }

    LY_ARRAY_FREE(tree);
}

/**
 * @brief Go through all the ynode nodes and sum rule results.
 *
 * @param[in] tree Tree of ynodes.
 * @param[in] rule Callback function returns number of nodes that can be inserted.
 * @return Sum of @p rule return values.
 */
static uint32_t
ay_ynode_summary(const struct ay_ynode *tree, uint32_t (*rule)(const struct ay_ynode *))
{
    uint32_t ret;
    LY_ARRAY_COUNT_TYPE i;

    ret = 0;
    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        ret += rule(&tree[i]);
    }

    return ret;
}

/**
 * @copydoc ay_ynode_summary()
 */
static uint32_t
ay_ynode_summary2(const struct ay_ynode *tree, uint32_t (*rule)(const struct ay_ynode *, const struct ay_ynode *))
{
    uint32_t ret;
    LY_ARRAY_COUNT_TYPE i;

    ret = 0;
    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        ret += rule(tree, &tree[i]);
    }

    return ret;
}

/**
 * @brief Copy ynode data to another ynode.
 *
 * @param[out] dst Destination node.
 * @param[in] src Source for copying.
 */
static void
ay_ynode_copy_data(struct ay_ynode *dst, struct ay_ynode *src)
{
    assert((dst && src) && (dst->type != YN_ROOT) && (src->type != YN_ROOT));
    dst->type = src->type;
    dst->snode = src->snode;
    dst->label = src->label;
    dst->value = src->value;
    dst->choice = src->choice;
    dst->ref = src->ref;
    dst->flags = src->flags;
    dst->min_elems = src->min_elems;
    dst->when_ref = src->when_ref;
    dst->when_val = src->when_val;
}

/**
 * @brief Find node with @p id.
 *
 * @param[in] tree Tree of ynodes.
 * @param[in] start_index Index in the array of ynodes where to start searching.
 * @param[in] id Number ay_ynode.id by which the node is searched.
 * @return Node with @p id or NULL.
 */
static struct ay_ynode *
ay_ynode_get_node(struct ay_ynode *tree, LY_ARRAY_COUNT_TYPE start_index, uint32_t id)
{
    LY_ARRAY_COUNT_TYPE i;

    for (i = start_index; i < LY_ARRAY_COUNT(tree); i++) {
        if (tree[i].id == id) {
            return &tree[i];
        }
    }

    return NULL;
}

/**
 * @brief Get last sibling.
 *
 * @param[in] node Node from which it iterates to the last node.
 * @return Last sibling node.
 */
static struct ay_ynode *
ay_ynode_get_last(struct ay_ynode *node)
{
    struct ay_ynode *last;

    if (!node) {
        return NULL;
    }

    for (last = node; last->next; last = last->next) {}
    return last;
}

/**
 * @brief Find node of type @p type in the @p subtree.
 *
 * @param[in] subtree Subtree of ynodes in which the @p type will be searched.
 * @param[in] type Type to search.
 * @return Node of type @p type or NULL.
 */
static const struct ay_ynode *
ay_ynode_subtree_contains_type(const struct ay_ynode *subtree, enum yang_type type)
{
    const struct ay_ynode *iter;
    LY_ARRAY_COUNT_TYPE i;

    for (i = 0; i < subtree->descendants; i++) {
        iter = &subtree[i + 1];
        if (iter->type == type) {
            return iter;
        }
    }

    return NULL;
}

/**
 * @brief Check if @p parent has child (not descendant) of type @p type.
 *
 * @param[in] parent Node whose children will be examined.
 * @param[in] type Type to search.
 * @return Node of type @p type whose parent is @p parent or NULL.
 */
static struct ay_ynode *
ay_ynode_parent_has_child(const struct ay_ynode *parent, enum yang_type type)
{
    struct ay_ynode *iter;

    for (iter = parent->child; iter; iter = iter->next) {
        if (iter->type == type) {
            return iter;
        }
    }

    return NULL;
}

/**
 * @brief Get common 'choice' lnode of @p node1 and @p node2.
 *
 * @param[in] node1 First node.
 * @param[in] node2 Second node.
 * @param[in] stop Parent node is used as a search stop.
 * @return Common choice or NULL. If @p stop is 'L_UNION' and no other was found then @p stop is returned.
 */
static const struct ay_lnode *
ay_ynode_common_choice(const struct ay_lnode *node1, const struct ay_lnode *node2, const struct ay_lnode *stop)
{
    const struct ay_lnode *it1, *it2;

    if (!node1 || !node2) {
        return NULL;
    }

    for (it1 = node1; it1 != stop; it1 = it1->parent) {
        if (!it1) {
            return NULL;
        }
        if (it1->lens->tag != L_UNION) {
            continue;
        }
        for (it2 = node2; it2 != stop; it2 = it2->parent) {
            if (it1 == it2) {
                return it1;
            }
        }
    }

    if (stop && (stop->lens->tag == L_UNION)) {
        return stop;
    } else {
        return NULL;
    }
}

/**
 * @brief Reset choice to original value or to NULL.
 *
 * Use if the unified choice value (from ::ay_ynode_unite_choice()) is no longer needed.
 *
 * @param[in,out] node Node whose choice will be reset.
 * @param[in] stop Upper search limit.
 */
static void
ay_ynode_reset_choice(struct ay_ynode *node, const struct ay_lnode *stop)
{
    const struct ay_lnode *iter, *choice;

    if (!node->snode || !node->choice) {
        return;
    }

    choice = NULL;
    for (iter = node->snode; iter && (iter != stop); iter = iter->parent) {
        if (iter->lens->tag == L_UNION) {
            choice = iter;
        }
    }

    node->choice = choice;
}

/**
 * @brief Get the previous ynode.
 *
 * @param[in] node Input node.
 * @return Previous node or NULL.
 */
static struct ay_ynode *
ay_ynode_get_prev(struct ay_ynode *node)
{
    struct ay_ynode *prev;

    assert(node->parent);
    for (prev = node->parent->child; (prev != node) && (prev->next != node); prev = prev->next) {}
    return prev == node ? NULL : prev;
}

/**
 * @brief Get pointer to inner nodes (first node behind YN_KEY and YN_VALUE).
 *
 * @param[in] root Node that may contain inner nodes.
 * @return First inner node or NULL.
 */
static struct ay_ynode *
ay_ynode_inner_nodes(const struct ay_ynode *root)
{
    struct ay_ynode *iter;

    iter = root->child;
    if (!iter) {
        return NULL;
    }

    if (iter->type == YN_KEY) {
        assert(iter->label == root->label);
        iter = iter->next;
    }
    if (iter && (iter->type == YN_VALUE)) {
        iter = iter->next;
    }

    if (iter == root->child) {
        return root->child;
    } else {
        return iter;
    }
}

/**
 * @brief Get total number of inner nodes.
 *
 * Number of descendants without YN_KEY and YN_VALUE node belonging to @p root.
 * @param[in] root Node that may contain inner nodes.
 * @return Total number of inner descendants or 0.
 */
static uint64_t
ay_ynode_inner_nodes_descendants(const struct ay_ynode *root)
{
    const struct ay_ynode *inner_nodes;

    inner_nodes = ay_ynode_inner_nodes(root);
    if (!inner_nodes) {
        return 0;
    }

    return root->descendants - ((inner_nodes - 1) - root);
}

/**
 * @brief Check if inner node is alone (therefore, is not there any inner node sibling).
 *
 * @param[in] node Node to check.
 * @return 1 if @p node is alone.
 */
static ly_bool
ay_ynode_inner_node_alone(const struct ay_ynode *node)
{
    struct ay_ynode *inner_nodes;

    assert(node && node->parent);

    inner_nodes = ay_ynode_inner_nodes(node->parent);
    if ((inner_nodes == node) && !node->next) {
        return 1;
    } else {
        return 0;
    }
}

/**
 * @brief Check if all siblings starting at node @p ns are under choice.
 *
 * @param[in] ns Group of ynodes.
 * @return 1 if @p ns are under choice.
 */
static ly_bool
ay_ynode_nodes_in_choice(const struct ay_ynode *ns)
{
    ly_bool in_choice;
    const struct ay_ynode *iter;
    const struct ay_lnode *choice;

    assert(ns);
    in_choice = 1;
    choice = ns->choice;
    for (iter = ns; iter; iter = iter->next) {
        if (!iter->choice || (choice != iter->choice)) {
            in_choice = 0;
            break;
        }
    }

    return in_choice;
}

/**
 * @brief Get the last L_CONCAT from @p start to @p stop.
 *
 * @param[in] start The node whose parents will be searched.
 * @param[in] stop The node at which the search should stop.
 * @return The lnode of type L_CONCAT or NULL.
 */
static const struct ay_lnode *
ay_lnode_get_last_concat(const struct ay_lnode *start, const struct ay_lnode *stop)
{
    const struct ay_lnode *iter;
    const struct ay_lnode *concat = NULL;

    if (!start || !stop) {
        return NULL;
    }

    for (iter = start->parent; iter && (iter != stop); iter = iter->parent) {
        if (iter->lens->tag == L_CONCAT) {
            concat = iter;
        }
    }

    return concat;
}

/**
 * @brief Get common 'concat' lnode of @p node1 and @p node2.
 *
 * @param[in] node1 First node.
 * @param[in] node2 Second node.
 * @param[in] stop Parent node is used as a search stop.
 * @return Common last lnode with tag L_CONCAT or NULL.
 */
static const struct ay_lnode *
ay_ynode_common_concat(const struct ay_ynode *node1, const struct ay_ynode *node2, const struct ay_lnode *stop)
{
    const struct ay_lnode *con1, *con2;

    assert(node1 && node2);

    con1 = ay_lnode_get_last_concat(node1->snode, stop);
    con2 = ay_lnode_get_last_concat(node2->snode, stop);
    if (con1 && con2 && (con1 == con2)) {
        return con1;
    } else {
        return 0;
    }
}

/**
 * @brief Check if @p subtree contains some recursive node.
 *
 * @param[in] subtree Subtree of ynodes in which the recursive node will be searched.
 * @param[in] only_one Stop processing when the first recursive node is found.
 * @return Number of internal recursive node.
 */
static uint64_t
ay_ynode_subtree_contains_rec(const struct ay_ynode *subtree, ly_bool only_one)
{
    uint64_t i, ret;
    const struct ay_ynode *iter;

    if (!subtree) {
        return 0;
    }

    ret = 0;
    for (i = 0; i < subtree->descendants; i++) {
        iter = &subtree[i + 1];
        if ((iter->type == YN_LEAFREF) ||
                (iter->snode && (iter->snode->lens->tag == L_REC) && iter->snode->lens->rec_internal)) {
            ret++;
            if (only_one) {
                break;
            }
        }
    }

    return ret;
}

/**
 * @brief Check if 'when' path in all nodes refer to a node in the subtree.
 *
 * It is not checked if the root has the 'when'.
 *
 * @param[in] subtree Subtree in which nodes with 'when' will be searched.
 * @param[in] path_to_root Flag set to 1 if 'when' can refer to the root.
 * @return 1 if all 'when' paths are valid.
 */
static int
ay_ynode_when_paths_are_valid(const struct ay_ynode *subtree, ly_bool path_to_root)
{
    uint64_t i;
    const struct ay_ynode *node, *iter, *stop, *sibl;
    ly_bool found, when_present, target_present;

    when_present = 0;
    target_present = subtree->flags & AY_WHEN_TARGET ? 1 : 0;
    for (i = 0; i < subtree->descendants; i++) {
        node = &subtree[i + 1];

        if (node->flags & AY_WHEN_TARGET) {
            target_present = 1;
        }
        if (!node->when_ref) {
            continue;
        }
        /* Found node with 'when'. */
        when_present = 1;

        /* Check if 'when' refers to a parental node in the subtree. */
        found = 0;
        stop = path_to_root ? subtree->parent : subtree;
        for (iter = node->parent; (iter != stop) && !found; iter = iter->parent) {
            if (iter->id == node->when_ref) {
                found = 1;
                break;
            }
            /* Check if 'when' refers to sibling. */
            for (sibl = iter->child; sibl; sibl = sibl->next) {
                if (sibl->id == node->when_ref) {
                    found = 1;
                    break;
                }
            }
        }
        if (!found) {
            return 0;
        }
    }

    if (!when_present && target_present) {
        /* Some node is the target of 'when', but no node in the subtree has a 'when' statement. */
        return 0;
    }

    return 1;
}

/**
 * @brief Check if lenses are equal.
 *
 * @param[in] l1 First lense.
 * @param[in] l2 Second lense.
 * @return 1 for equal.
 */
static ly_bool
ay_lnode_lense_equal(const struct lens *l1, const struct lens *l2)
{
    char *str1, *str2;

    if (!l1 || !l2) {
        return 0;
    }

    str1 = NULL;
    switch (l1->tag) {
    case L_STORE:
    case L_KEY:
        str1 = l1->regexp->pattern->str;
        break;
    case L_VALUE:
    case L_LABEL:
    case L_SEQ:
        str1 = l1->string->str;
        break;
    default:
        return l1->tag == l2->tag;
    }

    str2 = NULL;
    switch (l2->tag) {
    case L_STORE:
    case L_KEY:
        str2 = l2->regexp->pattern->str;
        break;
    case L_VALUE:
    case L_LABEL:
    case L_SEQ:
        str2 = l2->string->str;
        break;
    default:
        return l1->tag == l2->tag;
    }

    if ((str1 == str2) || !strcmp(str1, str2)) {
        return 1;
    } else {
        return 0;
    }
}

/**
 * @brief Check if ynodes are equal.
 *
 * @param[in] n1 First lense.
 * @param[in] n2 Second lense.
 * @param[in] ignore_choice Flag will cause the 'choice' to be ignored for comparison.
 * @param[in] ignore_when Flag will cause the 'when' tobe ignored for comparison.
 * @return 1 for equal.
 */
static ly_bool
ay_ynode_equal(const struct ay_ynode *n1, const struct ay_ynode *n2, ly_bool ignore_choice, ly_bool ignore_when)
{
    ly_bool alone1, alone2;
    uint16_t cmp_mask;

    assert((n1->type != YN_ROOT) && (n2->type != YN_ROOT));

    alone1 = !n1->next && (n1->parent->child == n1);
    alone2 = !n2->next && (n2->parent->child == n2);
    cmp_mask = ignore_choice ? AY_YNODE_FLAGS_CMP_MASK & ~AY_CHOICE_MAND_FALSE : AY_YNODE_FLAGS_CMP_MASK;

    if ((n1->descendants != n2->descendants) ||
            (n1->type != n2->type) ||
            (!n1->label && n2->label) ||
            (n1->label && !n2->label) ||
            (n1->label && !ay_lnode_lense_equal(n1->label->lens, n2->label->lens)) ||
            (!n1->value && n2->value) ||
            (n1->value && !n2->value) ||
            (n1->value && !ay_lnode_lense_equal(n1->value->lens, n2->value->lens)) ||
            (!n1->snode && n2->snode) ||
            (n1->snode && !n2->snode) ||
            (!ignore_choice && !alone1 && !alone2 && ((!n1->choice && n2->choice) || (n1->choice && !n2->choice))) ||
            ((n1->type != YN_LEAFREF) && (n1->ref != n2->ref)) ||
            ((n1->flags & cmp_mask) != (n2->flags & cmp_mask)) ||
            ((n1->type == YN_LIST) && (n1->min_elems != n2->min_elems)) ||
            (!ignore_when && (n1->when_ref != n2->when_ref))) {
        return 0;
    } else {
        return 1;
    }

    return 1;
}

/**
 * @brief Check if the subtrees are the same.
 *
 * @param[in] tree1 First subtree to check.
 * @param[in] tree2 Second subtree to check.
 * @param[in] compare_roots Set flag whether the roots of @p tree1 and @p tree2 will be compared.
 * @param[in] ignore_when Set flag if 'when' should be ignored.
 * @return 1 if subtrees are equal.
 */
static ly_bool
ay_ynode_subtree_equal(const struct ay_ynode *tree1, const struct ay_ynode *tree2, ly_bool compare_roots,
        ly_bool ignore_when)
{
    LY_ARRAY_COUNT_TYPE i;
    const struct ay_ynode *node1, *node2, *inner1, *inner2;
    uint64_t inner_cnt;

    if (tree1 == tree2) {
        return 1;
    }

    if (compare_roots) {
        if (!ay_ynode_equal(tree1, tree2, 1, ignore_when)) {
            return 0;
        }
        if (tree1->descendants != tree2->descendants) {
            return 0;
        }
        for (i = 0; i < tree1->descendants; i++) {
            node1 = &tree1[i + 1];
            node2 = &tree2[i + 1];
            if (!ay_ynode_equal(node1, node2, 0, ignore_when)) {
                return 0;
            }
        }
    } else {
        inner_cnt = ay_ynode_inner_nodes_descendants(tree1);
        if (!inner_cnt || (inner_cnt != ay_ynode_inner_nodes_descendants(tree2))) {
            return 0;
        }
        inner1 = ay_ynode_inner_nodes(tree1);
        inner2 = ay_ynode_inner_nodes(tree2);
        for (i = 0; i < inner_cnt; i++) {
            node1 = &inner1[i];
            node2 = &inner2[i];
            if (!ay_ynode_equal(node1, node2, 0, ignore_when)) {
                return 0;
            }
        }
    }

    return 1;
}

/**
 * @brief Check if @p subtree contains @p lnode.
 *
 * @param[in] subtree Subtree to check.
 * @param[in] lnode Searched node.
 * @return 1 if @p subtree contains @p lnode.
 */
static ly_bool
ay_ynode_subtree_contains_lnode(const struct ay_ynode *subtree, const struct ay_lnode *lnode)
{
    LY_ARRAY_COUNT_TYPE i;
    const struct ay_ynode *node;

    if (!lnode) {
        return 0;
    }

    for (i = 0; i <= subtree->descendants; i++) {
        node = &subtree[i];

        if ((node->snode && ay_lnode_lense_equal(node->snode->lens, lnode->lens)) ||
                (node->label && ay_lnode_lense_equal(node->label->lens, lnode->lens)) ||
                (node->value && ay_lnode_lense_equal(node->value->lens, lnode->lens))) {
            return 1;
        }
    }

    return 0;
}

/**
 * @brief Check if any choice branch contains @p lnode.
 *
 * @param[in] chnode First branch which will be searched.
 * @param[in] choice Choice according to which it is iterated over the branches.
 * @param[in] lnode node to find.
 * @return 1 if choice contains lnode.
 */
static ly_bool
ay_ynode_choice_contains_lnode(const struct ay_ynode *chnode, const struct ay_lnode *choice,
        const struct ay_lnode *lnode)
{
    const struct ay_ynode *branch;

    for (branch = chnode; branch; branch = branch->next) {
        if (ay_ynode_subtree_contains_lnode(branch, lnode)) {
            return 1;
        }
        if (branch->choice != choice) {
            break;
        }
    }

    return 0;
}

/**
 * @brief Check if @p lnode1 and @p lnode2 from ay_dnode dictionary are equal.
 *
 * @param[in] lnode1 First ay_lnode to check.
 * @param[in] lnode2 Second ay_lnode to check.
 * @return 1 for equal.
 */
static int
ay_dnode_lnode_equal(const void *lnode1, const void *lnode2)
{
    struct lens *ln1, *ln2;

    ln1 = ((const struct ay_lnode *)lnode1)->lens;
    ln2 = ((const struct ay_lnode *)lnode2)->lens;

    return ay_lnode_lense_equal(ln1, ln2);
}

/**
 * @brief Check if the 'maybe' operator (?) is bound to the @p node.
 *
 * @param[in] node Node to check.
 * @param[in] choice_stop Stop searching if @p node is under choice.
 * @param[in] star_stop Stop searching if @p node is under star.
 * @return 1 if maybe operator affects the node otherwise 0.
 */
static ly_bool
ay_lnode_has_maybe(const struct ay_lnode *node, ly_bool choice_stop, ly_bool star_stop)
{
    struct ay_lnode *iter;

    if (!node) {
        return 0;
    }

    for (iter = node->parent; iter && (iter->lens->tag != L_SUBTREE); iter = iter->parent) {
        if ((choice_stop && (iter->lens->tag == L_UNION)) ||
                (star_stop && (iter->lens->tag == L_STAR))) {
            return 0;
        } else if (iter->lens->tag == L_MAYBE) {
            return 1;
        }
    }

    return 0;
}

/**
 * @brief Check if the attribute (etc. choice, asterisk) is bound to the @p node.
 *
 * @param[in] node Node to check.
 * @param[in] attribute Searched parent lnode.
 * @return Pointer to attribute or NULL.
 */
static const struct ay_lnode *
ay_lnode_has_attribute(const struct ay_lnode *node, enum lens_tag attribute)
{
    struct ay_lnode *iter;

    if (!node) {
        return NULL;
    }

    for (iter = node->parent; iter && (iter->lens->tag != L_SUBTREE); iter = iter->parent) {
        if (iter->lens->tag == attribute) {
            return iter;
        }
    }

    return NULL;
}

/**
 * @brief Count number of identifiers in the lense pattern.
 *
 * @param[in] tree Tree of ynodes.
 * @param[in] lens Lense to check his pattern.
 * @return Number of identifiers.
 */
static uint64_t
ay_lense_pattern_idents_count(const struct ay_ynode *tree, const struct lens *lens)
{
    struct ay_transl *tran;

    if ((tran = ay_lense_pattern_has_idents(tree, lens))) {
        return LY_ARRAY_COUNT(tran->substr);
    } else if (lens->tag == L_KEY) {
        return 1;
    } else {
        return 0;
    }
}

int
augyang_print_input_lenses(struct module *mod, char **str)
{
    return ay_print_input_lenses(mod, str);
}

/**
 * @brief Print augeas terms based on verbose settings.
 *
 * @param[in] vercode Verbose that decides the execution of a function.
 * @param[in] ptree Tree of pnodes.
 */
static void
ay_pnode_print_verbose(uint64_t vercode, struct ay_pnode *ptree)
{
    char *str;

    if (!(vercode & AYV_PTREE)) {
        return;
    }

    str = ay_print_terms(ptree, TPT_PNODE);
    if (str) {
        printf("%s\n", str);
        free(str);
    }
}

int
augyang_print_input_terms(struct augeas *aug, const char *filename, char **str)
{
    return ay_print_input_terms(aug, filename, str);
}

/**
 * @brief Fill @p table with records and then the table will be ready to use.
 *
 * @param[in] tree Tree of lnodes. Flag AY_LNODE_KEY_HAS_IDENTS can be set.
 * @param[out] table Translation table of lens patterns. The LY_ARRAY must have enough allocated space.
 * @return 1 on success.
 */
static int
ay_transl_create_pattern_table(struct ay_lnode *tree, struct ay_transl *table)
{
    int ret;
    LY_ARRAY_COUNT_TYPE i;
    struct ay_transl *dst;
    char *origin;

    /* Fill ay_transl.origin. */
    LY_ARRAY_FOR(tree, i) {
        if ((tree[i].lens->tag != L_KEY) ||
                (tree[i].flags & AY_LNODE_KEY_IS_LABEL) ||
                !ay_lense_pattern_has_idents(NULL, tree[i].lens)) {
            continue;
        }

        origin = tree[i].lens->regexp->pattern->str;
        if (ay_transl_find(table, origin)) {
            /* Pattern is already in table. */
            tree[i].flags |= AY_LNODE_KEY_HAS_IDENTS;
            continue;
        }

        dst = &table[LY_ARRAY_COUNT(table)];
        dst->origin = origin;

        /* Fill ay_transl.substr. */
        ret = ay_transl_create_substr(dst);
        if (ret < 0) {
            /* Pattern is complex and augyang cannot split the pattern into identifiers. */
            dst->origin = NULL;
            continue;
        } else if (ret > 0) {
            /* Error */
            return ret;
        }

        /* Successfully deriving identifiers. */
        LY_ARRAY_INCREMENT(table);
        tree[i].flags |= AY_LNODE_KEY_HAS_IDENTS;
    }

    return 0;
}

/**
 * @brief Create lnode tree from lense tree.
 *
 * @param[in] root Root lense of the tree of lenses.
 * @param[in] lens Iterator over lense nodes.
 * @param[out] node Node will contain @p lens.
 */
static void
ay_lnode_create_tree(struct ay_lnode *root, struct lens *lens, struct ay_lnode *node)
{
    struct ay_lnode *child, *prev_child;

    assert(lens);

    LY_ARRAY_INCREMENT(root);
    node->lens = lens;
    node->mod = ay_get_module_by_lens(lens);
    if (ay_lense_pattern_is_label(lens)) {
        node->flags |= AY_LNODE_KEY_IS_LABEL;
    }
    if (ay_lense_pattern_in_datapath(lens)) {
        node->flags |= AY_LNODE_KEY_IN_DP;
    }

    if (AY_LENSE_HAS_NO_CHILD(lens->tag) || ((lens->tag == L_REC) && (lens->rec_internal))) {
        /* values are set by the parent */
        return;
    }

    child = node + 1;
    node->child = child;
    child->parent = node;
    ay_lnode_create_tree(root, AY_GET_FIRST_LENSE_CHILD(lens), child);
    node->descendants = 1 + child->descendants;

    if (AY_LENSE_HAS_ONE_CHILD(lens->tag) || (lens->tag == L_REC)) {
        return;
    }

    prev_child = child;
    for (uint64_t i = 1; i < lens->nchildren; i++) {
        child = &root[LY_ARRAY_COUNT(root)];
        child->parent = node;
        prev_child->next = child;
        ay_lnode_create_tree(root, node->lens->children[i], child);
        node->descendants += 1 + child->descendants;
        prev_child = child;
    }
}

/**
 * @brief Check if lnode tree is usable for generating yang.
 *
 * The goal is to detect auxiliary augeas modules such as build.aug, rx.aug etc.
 *
 * @param[in] ltree Tree of lnodes to check.
 * @param[in] mod Augeas module from which @p ltree was derived.
 */
static int
ay_lnode_tree_check(const struct ay_lnode *ltree, const struct module *mod)
{
    uint64_t bcnt;
    struct binding *bind_iter;

    if (mod->autoload) {
        return 0;
    }

    /* Count number of bindings in module. */
    bcnt = 0;
    LY_LIST_FOR(mod->bindings, bind_iter) {
        bcnt++;
    }

    if (LY_ARRAY_COUNT(ltree) < bcnt) {
        return AYE_LTREE_NO_ROOT;
    } else {
        return 0;
    }
}

/**
 * @brief Check if label indicates comment.
 *
 * @param[in] lns lens to check.
 * @return 1 if label is comment.
 */
static ly_bool
ay_lense_is_comment(struct lens *lns)
{
    if (lns && (lns->tag == L_LABEL) &&
            (!strcmp("#comment", lns->string->str) ||
            !strcmp("!comment", lns->string->str) ||
            !strcmp("#mcomment", lns->string->str) ||
            !strcmp("#scomment", lns->string->str))) {
        return 1;
    } else {
        return 0;
    }
}

/**
 * @brief Check if ynode should be ignored.
 *
 * @param[in] snode The lnode with tag L_SUBTREE.
 * @param[in] label The lnode which belongs to @p snode.
 * @return 1 if should be ignored, 0 otherwise.
 */
static ly_bool
ay_ynode_is_ignored(const struct ay_lnode *snode, const struct ay_lnode *label)
{

    return (snode->lens->tag == L_SUBTREE) && label && ay_lense_is_comment(label->lens);
}

/**
 * @brief Find label for ynode.
 *
 * @param[in] snode The lnode with tag L_SUBTREE.
 * @return label or NULL.
 */
static struct ay_lnode *
ay_ynode_find_label(struct ay_lnode *snode)
{
    uint32_t i;
    struct ay_lnode *lnode;
    enum lens_tag tag;

    for (i = 0; i < snode->descendants; i++) {
        lnode = &snode[i + 1];
        tag = lnode->lens->tag;
        if (tag == L_SUBTREE) {
            i += lnode->descendants;
        } else if (AY_TAG_IS_LABEL(tag)) {
            return lnode;
        }
    }

    return NULL;
}

/**
 * @brief Find value for ynode.
 *
 * @param[in] snode The lnode with tag L_SUBTREE.
 * @return value or NULL.
 */
static struct ay_lnode *
ay_ynode_find_value(struct ay_lnode *snode)
{
    uint32_t i;
    struct ay_lnode *lnode;
    enum lens_tag tag;

    for (i = 0; i < snode->descendants; i++) {
        lnode = &snode[i + 1];
        tag = lnode->lens->tag;
        if (tag == L_SUBTREE) {
            i += lnode->descendants;
        } else if (AY_TAG_IS_VALUE(tag)) {
            return lnode;
        }
    }

    return NULL;
}

/**
 * @brief Create basic ynode forest from lnode tree.
 *
 * Only ay_ynode.snode and ay_ynode.descendants are set.
 *
 * @param[out] ytree Tree of ynodes as destination.
 * @param[in] ltree Tree of lnodes as source.
 */
static void
ay_ynode_create_forest_(struct ay_ynode *ytree, struct ay_lnode *ltree)
{
    uint32_t id = 1;
    struct ay_lnode *child, *label;

    for (uint32_t i = 0, j = 0; i < ltree->descendants; i++) {
        if ((ltree[i].lens->tag == L_SUBTREE) || (ltree[i].lens->tag == L_REC)) {
            /* Set label and value. */
            label = ay_ynode_find_label(&ltree[i]);
            if (ay_ynode_is_ignored(&ltree[i], label)) {
                i += ltree[i].descendants;
                continue;
            }
            ytree[j].label = label;
            ytree[j].value = ay_ynode_find_value(&ltree[i]);

            LY_ARRAY_INCREMENT(ytree);
            ytree[j].type = ltree[i].lens->tag == L_REC ? YN_REC : YN_UNKNOWN;
            ytree[j].snode = &ltree[i];
            ytree[j].descendants = 0;
            ytree[j].id = id++;

            /* Set descendants. */
            for (uint32_t k = 0; k < ltree[i].descendants; k++) {
                child = &ltree[i + 1 + k];
                if ((child->lens->tag == L_SUBTREE) || (child->lens->tag == L_REC)) {
                    label = ay_ynode_find_label(child);
                    if (ay_ynode_is_ignored(child, label)) {
                        k += child->descendants;
                        continue;
                    }
                    ytree[j].descendants++;
                }
            }
            j++;
        }
    }
}

/**
 * @brief Connect ynode top-nodes (set ay_ynode.next).
 *
 * @param[in,out] forest Forest of ynodes.
 */
static void
ay_ynode_forest_connect_topnodes(struct ay_ynode *forest)
{
    LY_ARRAY_COUNT_TYPE i;
    struct ay_ynode *last;

    if (!LY_ARRAY_COUNT(forest)) {
        return;
    }

    last = NULL;
    LY_ARRAY_FOR(forest, i) {
        if (!forest[i].parent) {
            last = &forest[i];
            forest[i].next = last->descendants ? last + last->descendants + 1 : last + 1;
        }
    }
    assert(last);
    last->next = NULL;
}

/**
 * @brief Set choice to all ynodes in the forest.
 *
 * @param[in,out] forest Forest of ynodes.
 */
static void
ay_ynode_add_choice(struct ay_ynode *forest)
{
    LY_ARRAY_COUNT_TYPE i;
    const struct ay_lnode *iter;

    LY_ARRAY_FOR(forest, i) {
        for (iter = forest[i].snode->parent;
                iter && (iter->lens->tag != L_SUBTREE) && (iter->lens->tag != L_REC);
                iter = iter->parent) {

            if (iter->lens->tag == L_UNION) {
                forest[i].choice = iter;
                break;
            }
        }
    }
}

/**
 * @brief Move ynodes in an array one element to the right.
 *
 * @param[in,out] tree Tree of ynodes.
 */
static void
ay_ynode_shift_right(struct ay_ynode *tree)
{
    memmove(&tree[1], tree, LY_ARRAY_COUNT(tree) * sizeof *tree);
    LY_ARRAY_INCREMENT(tree);
    memset(tree, 0, sizeof *tree);

    for (uint32_t i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        tree[i].parent = tree[i].parent ? tree[i].parent + 1 : NULL;
        tree[i].next = tree[i].next ? tree[i].next + 1 : NULL;
        tree[i].child = tree[i].child ? tree[i].child + 1 : NULL;
    }
}

/**
 * @brief Set root node for @p tree.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in] tpatt_size Required memory space for ay_ynode_root.patt_table LY_ARRAY.
 * @param[in] ltree Tree of lnodes (Sized array). If function succeeds then the ownership of memory is moved to @p tree,
 * so the memory of ltree should be therefore released by ::ay_ynode_tree_free().
 * @return 0 on success.
 */
static int
ay_ynode_set_root(struct ay_ynode *tree, uint32_t tpatt_size, struct ay_lnode *ltree)
{
    int ret;
    LY_ARRAY_COUNT_TYPE i;
    struct ay_ynode *iter;
    uint64_t labcount, valcount;
    enum lens_tag tag;

    ay_ynode_shift_right(tree);

    tree->type = YN_ROOT;
    if (LY_ARRAY_COUNT(tree) != 1) {
        tree->child = tree + 1;
        for (iter = tree + 1; iter; iter = iter->next) {
            iter->parent = tree;
            tree->descendants += iter->descendants + 1;
        }
    }

    AY_YNODE_ROOT_ARRSIZE(tree) = LY_ARRAY_COUNT(tree);
    assert(AY_YNODE_ROOT_ARRSIZE(tree) == (tree->descendants + 1));

    labcount = 0;
    valcount = 0;
    LY_ARRAY_FOR(ltree, i) {
        tag = ltree[i].lens->tag;
        if (AY_TAG_IS_LABEL(tag)) {
            labcount++;
        } else if (AY_TAG_IS_VALUE(tag)) {
            valcount++;
        }
    }
    /* Set labels. */
    if (labcount) {
        LY_ARRAY_CREATE(NULL, AY_YNODE_ROOT_LABELS(tree), labcount, return AYE_MEMORY);
    }
    /* Set values. */
    if (valcount) {
        LY_ARRAY_CREATE(NULL, AY_YNODE_ROOT_VALUES(tree), valcount, return AYE_MEMORY);
    }

    /* Create translation table for lens.regexp.pattern. */
    LY_ARRAY_CREATE(NULL, AY_YNODE_ROOT_PATT_TABLE(tree), tpatt_size, return AYE_MEMORY);
    ret = ay_transl_create_pattern_table(ltree, AY_YNODE_ROOT_PATT_TABLE(tree));
    AY_CHECK_RET(ret);

    /* Set idcnt. */
    AY_YNODE_ROOT_IDCNT(tree) = (tree + tree->descendants)->id + 1;
    /* Set ltree. Note that this set must be the last operation before return. */
    AY_YNODE_ROOT_LTREE(tree) = ltree;

    return 0;
}

/**
 * @brief Set correct parent, next and child pointers to all ynodes.
 *
 * @param[in,out] tree Tree of ynodes which have bad pointers. It can also take the form of a forest.
 */
static void
ay_ynode_tree_correction(struct ay_ynode *tree)
{
    struct ay_ynode *parent, *iter, *next;
    uint32_t sum;

    LY_ARRAY_FOR(tree, struct ay_ynode, parent) {
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
 * @brief Create tree of ynodes from lnode tree.
 *
 * @param[in] ltree Tree of lnodes.
 * @param[in] tpatt_size Required memory space for ay_ynode_root.patt_table LY_ARRAY.
 * @param[out] ytree Tree of ynodes.
 * @return 0 on success.
 */
static int
ay_ynode_create_tree(struct ay_lnode *ltree, uint32_t tpatt_size, struct ay_ynode *ytree)
{
    int ret;

    ay_ynode_create_forest_(ytree, ltree);
    ay_ynode_tree_correction(ytree);
    ay_ynode_forest_connect_topnodes(ytree);
    ay_ynode_add_choice(ytree);
    ret = ay_ynode_set_root(ytree, tpatt_size, ltree);

    return ret;
}

/**
 * @brief Get repetition lense (* or +) bound to the @p node.
 *
 * @param[in] node Node to search.
 * @return Pointer to lnode or NULL.
 */
static const struct ay_lnode *
ay_ynode_get_repetition(const struct ay_ynode *node)
{
    const struct ay_lnode *ret = NULL, *liter, *lstart, *lstop;
    const struct ay_ynode *yiter;
    ly_bool impl_list;

    if (!node) {
        return ret;
    }

    for (yiter = node; (yiter->type != YN_ROOT) && !yiter->snode; yiter = yiter->parent) {}
    if (yiter->type == YN_ROOT) {
        return ret;
    }
    lstart = yiter->snode;

    for (yiter = node->parent; (yiter->type != YN_ROOT) && !yiter->snode; yiter = yiter->parent) {}
    lstop = (yiter->type == YN_ROOT) ? NULL : yiter->snode;

    impl_list = AY_YNODE_IS_IMPLICIT_LIST(node->parent);

    for (liter = lstart; liter != lstop; liter = liter->parent) {
        if ((liter->lens->tag == L_STAR) && (!impl_list || (liter != node->parent->label))) {
            ret = liter;
            break;
        }
    }

    return ret;
}

/**
 * @brief YN_LIST detection rule.
 *
 * @param[in] node Node to check.
 * @return 1 if ynode is of type YN_LIST.
 */
static ly_bool
ay_ynode_rule_list(const struct ay_ynode *node)
{
    ly_bool has_value, has_idents, impl_list;
    const struct ay_lnode *star;
    struct lens *label;

    label = AY_LABEL_LENS(node);
    star = ay_ynode_get_repetition(node);
    impl_list = AY_YNODE_IS_IMPLICIT_LIST(node->parent) && (node->label == star);
    has_value = label && ((label->tag == L_KEY) || (label->tag == L_SEQ)) && node->value;
    has_idents = label && (node->label->flags & AY_LNODE_KEY_NOREGEX);
    return (node->child || has_value || has_idents) && label && star && !impl_list;
}

/**
 * @brief YN_CONTAINER detection rule.
 *
 * @param[in] node Node to check.
 * @return 1 if ynode is of type YN_CONTAINER.
 */
static ly_bool
ay_ynode_rule_container(const struct ay_ynode *node)
{
    ly_bool has_value;
    struct lens *label;

    label = AY_LABEL_LENS(node);
    has_value = label && ((label->tag == L_KEY) || (label->tag == L_SEQ)) && node->value;
    return (node->child || has_value) && label && !ay_ynode_get_repetition(node);
}

/**
 * @brief A leaf-list detection rule.
 *
 * @param[in] node Node to check.
 * @return 1 if ynode is of type leaf-list.
 */
static ly_bool
ay_ynode_rule_leaflist(const struct ay_ynode *node)
{
    ly_bool impl_list;
    const struct ay_lnode *star;

    star = ay_ynode_get_repetition(node);
    impl_list = AY_YNODE_IS_IMPLICIT_LIST(node->parent) && (node->label == star);
    return !node->child && node->label && star && !impl_list;
}

/**
 * @brief YN_LEAF detection rule.
 *
 * @param[in] node Node to check.
 * @return 1 if ynode is of type YN_LEAF.
 */
static ly_bool
ay_ynode_rule_leaf(const struct ay_ynode *node)
{
    return !node->child && node->label;
}

/**
 * @brief Rule to insert key node.
 *
 * @param[in] tree Tree of ynodes.
 * @param[in] node Node to check.
 * @return Number of nodes that should be inserted as a key.
 */
static uint32_t
ay_ynode_rule_node_key_and_value(const struct ay_ynode *tree, const struct ay_ynode *node)
{
    struct lens *label, *value;

    (void) tree;

    label = AY_LABEL_LENS(node);
    value = AY_VALUE_LENS(node);
    if (!label ||
            AY_YNODE_IS_IMPLICIT_LIST(node) ||
            ((node->type != YN_CONTAINER) && !AY_YNODE_IS_SEQ_LIST(node))) {
        return 0;
    } else if (AY_LABEL_LENS_IS_IDENT(node)) {
        return value ? 1 : 0;
    } else if ((node->descendants == 0) && !value) {
        return 0;
    } else {
        assert((label->tag == L_KEY) || (label->tag == L_SEQ));
        return value ? 2 : 1;
    }
}

/**
 * @brief Basic checks for ay_ynode_insert_case().
 *
 * @param[in] node1 First node to check.
 * @param[in] node2 Second node to check.
 * @return 1 to meet the basic prerequisites for inserting YN_CASE.
 */
static ly_bool
ay_ynode_insert_case_prerequisite(const struct ay_ynode *node1, const struct ay_ynode *node2)
{
    if (!node1 || !node2 || !node1->choice || !node2->choice || !node1->snode || !node2->snode ||
            (node1->choice != node2->choice)) {
        return 0;
    } else {
        return 1;
    }
}

/**
 * @brief Rule for inserting the implicit list.
 *
 * @param[in] tree Tree of ynodes.
 * @return The maximum number of nodes that can be added.
 */
static uint32_t
ay_ynode_rule_insert_implicit_list(const struct ay_ynode *tree)
{
    uint32_t stars;
    LY_ARRAY_COUNT_TYPE i;
    const struct ay_lnode *ltree, *star1, *star2;

    ltree = AY_YNODE_ROOT_LTREE(tree);
    stars = 0;
    for (i = 0; i < LY_ARRAY_COUNT(ltree); i++) {
        star1 = &ltree[i];
        if (star1->lens->tag != L_STAR) {
            continue;
        }
        for (star2 = star1->parent; star2 && (star2->lens->tag != L_SUBTREE); star2 = star2->parent) {
            if (star2->lens->tag == L_STAR) {
                ++stars;
                break;
            }
        }
    }

    return stars;
}

/**
 * @brief Rule for inserting YN_CASE node which must wrap some nodes due to the choice statement.
 *
 * @param[in] node Node to check.
 * @return 1 if container should be inserted.
 */
static uint32_t
ay_ynode_rule_insert_case(const struct ay_ynode *node)
{
    const struct ay_ynode *first, *iter;
    uint64_t cnt, rank;

    if (!node->choice) {
        return 0;
    }

    /* Every even node can theoretically have a case. */
    first = ay_ynode_get_first_in_choice(node->parent, node->choice);
    cnt = 1;
    rank = 0;
    for (iter = first; iter->next && (iter->choice == iter->next->choice); iter = iter->next) {
        if (iter == node) {
            rank = cnt;
            break;
        }
        cnt++;
    }

    return rank % 2;
}

/**
 * @brief Rule for copy node to YN_CASE.
 *
 * @param[in] tree Tree of ynodes.
 * @return The number of nodes that will be added.
 */
static uint32_t
ay_ynode_rule_copy_case_nodes(const struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i;
    const struct ay_ynode *iter, *cas, *child;
    uint32_t cnt;

    if (!tree->ref) {
        return 0;
    }

    cnt = 0;
    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        iter = &tree[i];
        if (!iter->ref) {
            continue;
        }
        /* Find YN_CASE containing nodes to copy. */
        for (cas = iter->next; cas && (cas->id != iter->ref); cas = cas->next) {}
        assert(cas);

        /* New YN_CASE node. */
        cnt++;
        assert(cas->child->next);

        /* Count nodes to be copied. */
        for (child = cas->child->next; child; child = child->next) {
            cnt += child->descendants + 1;
        }
    }

    return cnt;
}

/**
 * @brief Check if choice branches should be merged.
 *
 * @param[in] br1 First branch to check.
 * @param[in] br2 Second branch to check.
 * @return 1 if should be merged.
 */
static ly_bool
ay_ynode_merge_choice_branches(const struct ay_ynode *br1, const struct ay_ynode *br2)
{
    struct lens *lab1, *lab2;

    lab1 = AY_LABEL_LENS(br1);
    lab2 = AY_LABEL_LENS(br2);

    /* Compare roots. */
    if ((lab1 || lab2) && !ay_lnode_lense_equal(lab1, lab2)) {
        return 0;
    }

    return 1;
}

/**
 * @brief Compare choice branches if should be merged.
 *
 * @param[in] br1 First branch to check.
 * @param[in] br2 Second branch to check.
 * @return 1 if should be merged.
 */
static ly_bool
ay_ynode_cmp_choice_branches(const struct ay_ynode *br1, const struct ay_ynode *br2)
{
    ly_bool match;

    if (br1->when_ref || br2->when_ref) {
        return 0;
    }

    if ((br1->type == YN_CASE) && (br2->type == YN_CASE)) {
        match = ay_ynode_merge_choice_branches(br1->child, br2->child);
    } else if ((br1->type == YN_CASE) && (br2->type != YN_CASE)) {
        match = ay_ynode_merge_choice_branches(br1->child, br2);
    } else if ((br1->type != YN_CASE) && (br2->type == YN_CASE)) {
        match = ay_ynode_merge_choice_branches(br1, br2->child);
    } else {
        assert((br1->type != YN_CASE) && (br2->type != YN_CASE));
        match = ay_ynode_merge_choice_branches(br1, br2);
    }

    return match;
}

/**
 * @brief Give the branch in which the YN_LEAFREF is located.
 *
 * The YN_REC node is the stop.
 *
 * @param[in] leafref Node of type YN_LEAFREF.
 * @return First node in branch.
 */
static struct ay_ynode *
ay_ynode_leafref_branch(const struct ay_ynode *leafref)
{
    struct ay_ynode *iter;

    for (iter = leafref->parent; iter && (iter->parent->type != YN_REC); iter = iter->parent) {}
    assert(iter && (leafref->parent->type == YN_LIST));

    return iter;
}

/**
 * @brief Compare choice branches if should be merged and count how many nodes to add.
 *
 * @param[in] tree Tree of ynodes.
 * @return The maximum number of nodes that can be added.
 */
static uint64_t
ay_ynode_rule_merge_cases(const struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i;
    const struct ay_ynode *chn1, *chn2;
    uint64_t match;

    match = 0;
    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        chn1 = &tree[i];
        if (!chn1->choice) {
            continue;
        }

        for (chn2 = chn1->next; chn2 && (chn2->choice == chn1->choice); chn2 = chn2->next) {
            if (ay_ynode_cmp_choice_branches(chn1, chn2)) {
                match++;
            }
        }
    }

    /* A 2 cases for the children of the first node and 2 for nodes after the first. */
    return match * 4;
}

/**
 * @brief Find out how many nodes must be added if a node splits into multiple nodes.
 *
 * @param[in] tree Tree of ynodes.
 * @param[in] node Node to process.
 * @return Number of nodes needed for split.
 */
static uint64_t
ay_ynode_rule_node_is_splittable(const struct ay_ynode *tree, const struct ay_ynode *node)
{
    struct lens *label;
    uint64_t count;

    assert(node);
    if (node->type == YN_ROOT) {
        return 0;
    }

    label = AY_LABEL_LENS(node);

    if (label && (label->tag == L_KEY) && (node->type != YN_KEY) && (node->type != YN_VALUE) &&
            (count = ay_lense_pattern_idents_count(tree, label)) && (count > 1)) {
        /* +2 for YN_GROUPING and YN_USES node in @p node. */
        return (count - 1) * node->descendants + 2 + (count - 1);
    } else {
        return 0;
    }
}

/**
 * @brief Find out how many nodes must be added in total if all nodes splits into multiple nodes in the @p subtree.
 *
 * @param[in] tree Tree of ynodes.
 * @param[in] subtree to process.
 * @return Number of nodes needed for split.
 */
static uint64_t
ay_ynode_rule_node_split(const struct ay_ynode *tree, const struct ay_ynode *subtree)
{
    const struct ay_ynode *iter;
    uint64_t children_total, count;

    assert(subtree);
    children_total = 0;
    for (iter = subtree->child; iter; iter = iter->next) {
        if (iter->child) {
            children_total += ay_ynode_rule_node_split(tree, iter);
        } else {
            children_total += ay_ynode_rule_node_is_splittable(tree, iter);
        }
    }

    count = ay_ynode_rule_node_is_splittable(tree, subtree);
    if (count && children_total) {
        return children_total * count;
    } else if (count) {
        return count;
    } else {
        return children_total;
    }
}

/**
 * @brief Rule decide how many config-entries lists should be inserted for the whole tree.
 *
 * @param[in] tree Pointer to lnode tree.
 * @return Number of config-entries lists to insert.
 */
static uint64_t
ay_ynode_rule_ordered_entries(const struct ay_lnode *tree)
{
    LY_ARRAY_COUNT_TYPE i;
    uint64_t ret = 0;

    LY_ARRAY_FOR(tree, i) {
        if (tree[i].lens->tag == L_STAR) {
            ret++;
        }
    }

    return ret;
}

/**
 * @brief Rule decide how many nodes must be inserted to create a recursive form.
 *
 * @param[in] node Node to check.
 * @return Number of nodes to insert.
 */
static uint32_t
ay_ynode_rule_recursive_form(const struct ay_ynode *node)
{
    return node->type == YN_REC;
}

/**
 * @brief Rule decide how many nodes must be inserted to create a recursive form by copy.
 *
 * @param[in] tree Tree of ynodes.
 * @return Number of nodes to insert.
 */
static uint64_t
ay_ynode_rule_recursive_form_by_copy(const struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i;
    struct ay_ynode *iter;
    const struct ay_ynode *rec_ext;
    uint64_t ret = 0, rec_int_count, copied, tmp;

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        rec_ext = &tree[i];
        if ((rec_ext->type != YN_REC) || rec_ext->snode->lens->rec_internal) {
            continue;
        }
        rec_int_count = 0;
        copied = 0;
        for (iter = rec_ext->child; iter; iter = iter->next) {
            if ((tmp = ay_ynode_subtree_contains_rec(iter, 0))) {
                rec_int_count += tmp;
            } else {
                copied += iter->descendants + 1;
            }
        }
        ret += copied * (rec_int_count + 1);
    }

    return ret;
}

/**
 * @brief Rule decide how many nodes must be inserted to create a recursive form by groupings.
 *
 * @param[in] tree Tree of ynodes.
 * @return Number of nodes to insert.
 */
static uint64_t
ay_ynode_rule_create_groupings_recursive_form(const struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i;
    const struct ay_ynode *lf;
    uint64_t cnt;

    cnt = 0;
    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        lf = &tree[i];
        if (lf->type != YN_LEAFREF) {
            continue;
        }
        cnt++;
    }

    /* grouping + uses + uses */
    return cnt * 3;
}

/**
 * @brief Rule decide how many nodes will be inserted for node which contains multiple keys (labels).
 *
 * @param[in] tree Tree of ynodes.
 * @return Number of nodes to insert.
 */
static uint32_t
ay_ynode_rule_more_keys_for_node(const struct ay_ynode *tree)
{
    uint32_t ret, i, j;
    struct ay_dnode *labels;
    const struct ay_ynode *node;

    labels = AY_YNODE_ROOT_LABELS(tree);
    if (!LY_ARRAY_COUNT(labels)) {
        return 0;
    }

    ret = 0;
    for (i = 0; i < LY_ARRAY_COUNT(tree); i++) {
        node = &tree[i];
        if (!node->label || !node->snode) {
            continue;
        }
        AY_DNODE_KEY_FOR(labels, j) {
            if (node->label == labels[j].lkey) {
                ret += (node->descendants * labels[j].values_count) + labels[j].values_count;
                break;
            }
        }
    }
    assert(ret);

    return ret;
}

/**
 * @brief Rule decide how many nodes will be inserted for grouping.
 *
 * @param[in] node to check.
 * @return Number of nodes to insert.
 */
static uint32_t
ay_ynode_rule_create_groupings_toplevel(const struct ay_ynode *node)
{
    if (node->id == node->ref) {
        /* YN_GROUPING + YN_USES */
        return 2;
    } else if (node->ref) {
        /* YN_USES */
        return 1;
    } else {
        return 0;
    }
}

/**
 * @brief Rule decide how many containers will be inserted to the choice at most.
 *
 * The rule runs only for the first node in the choice.
 *
 * @param[in] node Node to check.
 * @return Number of nodes that can be insterted for choice.
 */
static uint32_t
ay_ynode_rule_insert_container_in_choice(const struct ay_ynode *node)
{
    uint32_t ret;
    struct ay_ynode *first, *iter;
    ly_bool case_presence;

    if (!node->choice) {
        return 0;
    }
    first = ay_ynode_get_first_in_choice(node->parent, node->choice);
    if (node != first) {
        return 0;
    }

    ret = 0;
    case_presence = 0;
    for (iter = first; iter; iter = iter->next) {
        if (iter->choice != node->choice) {
            break;
        } else if (iter->type == YN_CASE) {
            /* At least one case must be present. */
            case_presence = 1;
        }
        ret++;
    }

    if (!case_presence) {
        return 0;
    } else {
        return ret;
    }
}

/**
 * @brief Swap ynode nodes but keep parent, next, child and choice pointers.
 *
 * @param[in,out] node1 First node to swap.
 * @param[in,out] node2 Second node to swap.
 */
static void
ay_ynode_swap(struct ay_ynode *node1, struct ay_ynode *node2)
{
    struct ay_ynode *parent, *next, *child;
    const struct ay_lnode *choice;
    struct ay_ynode tmp;
    uint32_t descendants;

    /* Temporary store data node1. */
    tmp = *node1;

    /* Temporary store position pointers of node1. */
    parent = node1->parent;
    next = node1->next;
    child = node1->child;
    descendants = node1->descendants;
    choice = node1->choice;
    /* Copy ynode data into node1. */
    *node1 = *node2;
    /* Restore position pointers. */
    node1->parent = parent;
    node1->next = next;
    node1->child = child;
    node1->descendants = descendants;
    node1->choice = choice;

    /* Temporary store position pointers of node2. */
    parent = node2->parent;
    next = node2->next;
    child = node2->child;
    descendants = node2->descendants;
    choice = node2->choice;
    /* Copy ynode data into node2. */
    *node2 = tmp;
    /* Restore position pointers. */
    node2->parent = parent;
    node2->next = next;
    node2->child = child;
    node2->descendants = descendants;
    node2->choice = choice;
}

/**
 * @brief Insert single gap in the array.
 *
 * All pointers to ynodes are invalidated.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in] index Index where there will be the gap.
 */
static void
ay_ynode_insert_gap(struct ay_ynode *tree, uint32_t index)
{
    assert(AY_YNODE_ROOT_ARRSIZE(tree) > LY_ARRAY_COUNT(tree));
    memmove(&tree[index + 1], &tree[index], (LY_ARRAY_COUNT(tree) - index) * sizeof *tree);
    memset(&tree[index], 0, sizeof *tree);
    LY_ARRAY_INCREMENT(tree);
    tree[index].id = AY_YNODE_ROOT_IDCNT(tree);
    AY_YNODE_ROOT_IDCNT_INC(tree);
}

/**
 * @brief Insert wide gap in the array.
 *
 * All pointers to ynodes are invalidated.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in] index Index where the gap starts.
 * @param[in] items Number of items in gap.
 */
static void
ay_ynode_insert_gap_range(struct ay_ynode *tree, uint32_t index, uint32_t items)
{
    assert(AY_YNODE_ROOT_ARRSIZE(tree) > LY_ARRAY_COUNT(tree));
    memmove(&tree[index + items], &tree[index], (LY_ARRAY_COUNT(tree) - index) * sizeof *tree);
    AY_SET_LY_ARRAY_SIZE(tree, LY_ARRAY_COUNT(tree) + items);
    for (uint32_t i = 0; i < items; i++) {
        tree[index + i].id = AY_YNODE_ROOT_IDCNT(tree);
        AY_YNODE_ROOT_IDCNT_INC(tree);
    }
}

/**
 * @brief Delete gap in the array.
 *
 * All pointers to ynodes are invalidated.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in] index Index where the gap will be deleted.
 */
static void
ay_ynode_delete_gap(struct ay_ynode *tree, uint32_t index)
{
    uint32_t tree_count;

    tree_count = LY_ARRAY_COUNT(tree);
    memmove(&tree[index], &tree[index + 1], (tree_count - index - 1) * sizeof *tree);
    memset(tree + tree_count - 1, 0, sizeof *tree);
    LY_ARRAY_DECREMENT(tree);
}

/**
 * @brief Delete wide gap in the array.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in] index Index where the gap will be deleted.
 * @param[in] items Number of items in gap.
 */
static void
ay_ynode_delete_gap_range(struct ay_ynode *tree, uint32_t index, uint32_t items)
{
    uint32_t tree_count;

    tree_count = LY_ARRAY_COUNT(tree);
    memmove(&tree[index], &tree[index + items], (tree_count - index - items) * sizeof *tree);
    memset(tree + tree_count - items, 0, items * sizeof *tree);
    AY_SET_LY_ARRAY_SIZE(tree, tree_count - items);
}

/**
 * @brief Delete node from the tree.
 *
 * Children of deleted node are moved up in the tree level. Member ay_ynode.choice is set on children.
 * Under certain conditions, the node is not deleted, but instead is cast to a YN_CASE.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in] node The ynode to be deleted.
 * @return 0 if node was deleted or 1 if node changed type to YN_CASE.
 */
static ly_bool
ay_ynode_delete_node(struct ay_ynode *tree, struct ay_ynode *node)
{
    struct ay_ynode *iter, *parent;
    uint32_t index;
    ly_bool cast_case;

    if (node->type != YN_CASE) {
        if (node->choice && node->child && node->child->next) {
            /* Choice setting for children. */
            /* Cast @p node to YN_CASE if not all children have choice set. */
            cast_case = 0;
            for (iter = node->child; iter; iter = iter->next) {
                if (!iter->choice) {
                    cast_case = 1;
                }
            }
            if (cast_case) {
                node->type = YN_CASE;
                node->snode = node->label = node->value = NULL;
                node->ref = node->flags = 0;
                return 1;
            } else {
                /* Set children choice. */
                for (iter = node->child; iter; iter = iter->next) {
                    iter->choice = node->choice;
                }
                /* Delete @p node. */
            }
        } else if (node->choice && node->child) {
            node->child->choice = node->choice;
            /* Delete @p node. */
        }
    } else {
        assert(ay_ynode_alone_in_choice(node));
    }

    if (node->flags & AY_CHILDREN_MAND_FALSE) {
        for (iter = node->child; iter; iter = iter->next) {
            iter->flags |= AY_HINT_MAND_FALSE;
        }
    }

    /* Delete @p node. */
    index = AY_INDEX(tree, node);
    for (iter = tree[index].parent; iter; iter = iter->parent) {
        iter->descendants--;
    }
    parent = node->parent;
    ay_ynode_delete_gap(tree, index);
    ay_ynode_tree_correction(tree);

    /* If parent has only one child, then set choice to NULL. */
    if (parent->child && !parent->child->next) {
        parent->child->choice = NULL;
    }

    return 0;
}

/**
 * @brief Delete subtree.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in] subtree Root node of subtree to be deleted.
 */
static void
ay_ynode_delete_subtree(struct ay_ynode *tree, struct ay_ynode *subtree)
{
    struct ay_ynode *iter;
    uint32_t deleted_nodes, index;

    deleted_nodes = subtree->descendants + 1;

    index = AY_INDEX(tree, subtree);
    for (iter = subtree->parent; iter; iter = iter->parent) {
        iter->descendants = iter->descendants - deleted_nodes;
    }

    ay_ynode_delete_gap_range(tree, index, deleted_nodes);
    ay_ynode_tree_correction(tree);
}

/**
 * @brief Delete all children in @p subtree.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in] subtree Subtree to which chilren will be deleted.
 * @param[in] keep_keyval If flags is set then YN_KEY and YN_VALUE nodes belonging to @p subtree will remain.
 */
static void
ay_ynode_delete_children(struct ay_ynode *tree, struct ay_ynode *subtree, ly_bool keep_keyval)
{
    struct ay_ynode *iter, *inner_nodes;
    uint32_t deleted_nodes, index;

    if (keep_keyval) {
        deleted_nodes = ay_ynode_inner_nodes_descendants(subtree);
        inner_nodes = ay_ynode_inner_nodes(subtree);
        subtree = inner_nodes ? inner_nodes : subtree->child;
    } else {
        deleted_nodes = subtree->descendants;
        subtree++;
    }
    if (!deleted_nodes) {
        return;
    }

    index = AY_INDEX(tree, subtree);
    for (iter = subtree->parent; iter; iter = iter->parent) {
        iter->descendants = iter->descendants - deleted_nodes;
    }

    ay_ynode_delete_gap_range(tree, index, deleted_nodes);
    ay_ynode_tree_correction(tree);
}

/**
 * @brief Insert new parent (wrapper) for node.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in] node Node that will be wrapped.
 */
static void
ay_ynode_insert_wrapper(struct ay_ynode *tree, struct ay_ynode *node)
{
    struct ay_ynode *iter, *wrapper;

    assert((1 + LY_ARRAY_COUNT(tree)) <= AY_YNODE_ROOT_ARRSIZE(tree));

    for (iter = node->parent; iter; iter = iter->parent) {
        iter->descendants++;
    }
    ay_ynode_insert_gap(tree, AY_INDEX(tree, node));
    wrapper = node;
    wrapper->descendants = (wrapper + 1)->descendants + 1;
    ay_ynode_tree_correction(tree);
}

/**
 * @brief Insert new parent for all children.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in] child Pointer to the one of children to whom a new parent will be inserted.
 */
static void
ay_ynode_insert_parent(struct ay_ynode *tree, struct ay_ynode *child)
{
    struct ay_ynode *iter, *parent;
    uint32_t index;

    assert((1 + LY_ARRAY_COUNT(tree)) <= AY_YNODE_ROOT_ARRSIZE(tree));
    assert(child && child->parent);

    for (iter = child->parent; iter; iter = iter->parent) {
        iter->descendants++;
    }
    index = AY_INDEX(tree, child->parent->child);
    ay_ynode_insert_gap(tree, index);
    parent = &tree[index];
    parent->descendants = (parent - 1)->descendants - 1;
    ay_ynode_tree_correction(tree);
}

/**
 * @brief Insert new parent for @p child and his siblings behind him.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in] child First child to which the new parent will apply.
 */
static void
ay_ynode_insert_parent_for_rest(struct ay_ynode *tree, struct ay_ynode *child)
{
    struct ay_ynode *iter, *parent;
    uint32_t descendants = 0;

    assert((1 + LY_ARRAY_COUNT(tree)) <= AY_YNODE_ROOT_ARRSIZE(tree));
    assert(child);

    for (iter = child; iter; iter = iter->next) {
        descendants += iter->descendants + 1;
    }
    for (iter = child->parent; iter; iter = iter->parent) {
        iter->descendants++;
    }
    ay_ynode_insert_gap(tree, AY_INDEX(tree, child));
    parent = child;
    parent->descendants = descendants;
    ay_ynode_tree_correction(tree);
}

/**
 * @brief Insert new child for node.
 *
 * New inserted node will be the first child.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in] parent Pointer to parent who will have new child.
 */
static void
ay_ynode_insert_child(struct ay_ynode *tree, struct ay_ynode *parent)
{
    struct ay_ynode *iter;

    assert((1 + LY_ARRAY_COUNT(tree)) <= AY_YNODE_ROOT_ARRSIZE(tree));

    for (iter = parent; iter; iter = iter->parent) {
        iter->descendants++;
    }
    ay_ynode_insert_gap(tree, AY_INDEX(tree, parent + 1));
    ay_ynode_tree_correction(tree);
}

/**
 * @brief Insert new sibling for node.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in] node Node who will have new sibling.
 */
static void
ay_ynode_insert_sibling(struct ay_ynode *tree, struct ay_ynode *node)
{
    struct ay_ynode *iter, *sibling;
    uint32_t index;

    assert((1 + LY_ARRAY_COUNT(tree)) <= AY_YNODE_ROOT_ARRSIZE(tree));

    for (iter = node->parent; iter; iter = iter->parent) {
        iter->descendants++;
    }
    index = AY_INDEX(tree, node) + node->descendants + 1;
    ay_ynode_insert_gap(tree, index);
    sibling = &tree[index];
    sibling->descendants = 0;
    ay_ynode_tree_correction(tree);
}

/**
 * @brief Insert new node as the last sibling for the first @p parent child.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in] parent Node that will have a new child.
 * @return Pointer to the inserted node.
 */
static struct ay_ynode *
ay_ynode_insert_child_last(struct ay_ynode *tree, struct ay_ynode *parent)
{
    struct ay_ynode *last;

    assert((1 + LY_ARRAY_COUNT(tree)) <= AY_YNODE_ROOT_ARRSIZE(tree));

    if (parent->child) {
        last = ay_ynode_get_last(parent->child);
        ay_ynode_insert_sibling(tree, last);
        return last->next;
    } else {
        ay_ynode_insert_child(tree, parent);
        return parent->child;
    }
}

/**
 * @brief Move subtree to another place.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in] dst Index of the place where the subtree is moved. Gaps are inserted on this index.
 * @param[in] src Index to the root of subtree. Gap is deleted after the move.
 */
static void
ay_ynode_move_subtree(struct ay_ynode *tree, uint32_t dst, uint32_t src)
{
    uint32_t subtree_size, i;
    struct ay_ynode node, *buffer;

    if (dst == src) {
        return;
    }

    subtree_size = tree[src].descendants + 1;

    if ((AY_YNODE_ROOT_ARRSIZE(tree) - LY_ARRAY_COUNT(tree)) > subtree_size) {
        buffer = tree + LY_ARRAY_COUNT(tree);
        memcpy(buffer, &tree[src], subtree_size * sizeof *tree);
        ay_ynode_delete_gap_range(tree, src, subtree_size);
        dst = dst > src ? dst - subtree_size : dst;
        ay_ynode_insert_gap_range(tree, dst, subtree_size);
        memcpy(&tree[dst], buffer, subtree_size * sizeof *tree);
    } else {
        for (i = 0; i < subtree_size; i++) {
            node = tree[src];
            ay_ynode_delete_gap(tree, src);
            dst = dst > src ? dst - 1 : dst;
            ay_ynode_insert_gap(tree, dst);
            src = src > dst ? src + 1 : src;
            tree[dst] = node;
            dst++;
        }
    }
}

/**
 * @brief Move subtree to another place as a sibling.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in] dst Node whose sibling will be subtree.
 * @param[in] src Root of subtree that moves.
 */
static void
ay_ynode_move_subtree_as_sibling(struct ay_ynode *tree, struct ay_ynode *dst, struct ay_ynode *src)
{
    struct ay_ynode *iter;
    uint32_t subtree_size, index;

    if (dst->next == src) {
        return;
    }

    subtree_size = src->descendants + 1;
    index = AY_INDEX(tree, dst) + dst->descendants + 1;
    for (iter = src->parent; iter; iter = iter->parent) {
        iter->descendants -= subtree_size;
    }
    for (iter = dst->parent; iter; iter = iter->parent) {
        iter->descendants += subtree_size;
    }
    ay_ynode_move_subtree(tree, index, AY_INDEX(tree, src));
    ay_ynode_tree_correction(tree);
}

/**
 * @brief Move subtree to another place as a child.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in] dst Node whose the first child will be moved subtree.
 * @param[in] src Root of subtree that moves.
 */
static void
ay_ynode_move_subtree_as_child(struct ay_ynode *tree, struct ay_ynode *dst, struct ay_ynode *src)
{
    struct ay_ynode *iter;
    uint32_t subtree_size;

    if (dst->child == src) {
        return;
    }

    subtree_size = src->descendants + 1;
    for (iter = src->parent; iter; iter = iter->parent) {
        iter->descendants -= subtree_size;
    }
    for (iter = dst; iter; iter = iter->parent) {
        iter->descendants += subtree_size;
    }
    ay_ynode_move_subtree(tree, AY_INDEX(tree, dst + 1), AY_INDEX(tree, src));
    ay_ynode_tree_correction(tree);
}

/**
 * @brief Move subtree to another place as a last child.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in] dst Node whose the last child will be moved subtree.
 * @param[in] src Root of subtree that moves.
 */
static void
ay_ynode_move_subtree_as_last_child(struct ay_ynode *tree, struct ay_ynode *dst, struct ay_ynode *src)
{
    struct ay_ynode *last;

    if (dst == src) {
        return;
    }

    for (last = dst->child; last && last->next; last = last->next) {}
    if (last) {
        ay_ynode_move_subtree_as_sibling(tree, last, src);
    } else {
        ay_ynode_move_subtree_as_child(tree, dst, src);
    }
}

/**
 * @brief Copy subtree to another place.
 *
 * Only ay_ynode_copy_subtree_* functions should call this function.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in] dst Index of the place where the subtree is copied. Gaps are inserted on this index.
 * @param[in] src Index to the root of subtree.
 */
static void
ay_ynode_copy_subtree(struct ay_ynode *tree, uint32_t dst, uint32_t src)
{
    uint32_t subtree_size;

    subtree_size = tree[src].descendants + 1;
    ay_ynode_insert_gap_range(tree, dst, subtree_size);
    src = src >= dst ? src + subtree_size : src;
    memcpy(&tree[dst], &tree[src], subtree_size * sizeof *tree);
}

/**
 * @brief Correction of ay_ynode.when_ref after subtree is copied.
 *
 * Because ay_ynode.when_ref must refer to a node in the copied tree and not in the original one.
 *
 * @param[in,out] copied_subtree New subtree created.
 * @param[in] original_subtree Subtree copied from.
 */
static void
ay_ynode_copy_subtree_when_ref_correction(struct ay_ynode *copied_subtree, struct ay_ynode *original_subtree)
{
    LY_ARRAY_COUNT_TYPE i, j;
    struct ay_ynode *node_ref, *src, *dst, *node_target;

    assert(copied_subtree && original_subtree);

    for (i = 0; i < original_subtree->descendants; i++) {
        node_ref = &original_subtree[i + 1];
        if (!node_ref->when_ref) {
            continue;
        }

        /* Find 'when' target. */
        node_target = NULL;
        for (j = 0; j <= original_subtree->descendants; j++) {
            node_target = &original_subtree[j];
            if ((node_ref->when_ref == node_target->id) && (node_target->flags & AY_WHEN_TARGET)) {
                break;
            }
        }
        assert(node_target);

        /* Set 'when' in copied_subtree. */
        dst = &copied_subtree[AY_INDEX(original_subtree, node_ref)];
        src = &copied_subtree[AY_INDEX(original_subtree, node_target)];
        dst->when_ref = src->id;
    }

}

/**
 * @brief Copy subtree @p src and insert it as last child of @p dst.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in] dst Pointer to parent where @p src will be copied as last child.
 * @param[in] src Root of some subtree.
 */
static void
ay_ynode_copy_subtree_as_last_child(struct ay_ynode *tree, struct ay_ynode *dst, struct ay_ynode *src)
{
    struct ay_ynode *iter, *last, *copied_subtree, *original_subtree;
    uint32_t subtree_size, src_id;

    assert((src->descendants + 1 + LY_ARRAY_COUNT(tree)) <= AY_YNODE_ROOT_ARRSIZE(tree));

    for (last = dst->child; last && last->next; last = last->next) {}
    if (last == src) {
        return;
    }
    src_id = src->id;

    subtree_size = src->descendants + 1;
    for (iter = dst; iter; iter = iter->parent) {
        iter->descendants += subtree_size;
    }

    if (last) {
        ay_ynode_copy_subtree(tree, AY_INDEX(tree, last + last->descendants + 1), AY_INDEX(tree, src));
    } else {
        ay_ynode_copy_subtree(tree, AY_INDEX(tree, dst + 1), AY_INDEX(tree, src));
    }
    ay_ynode_tree_correction(tree);
    copied_subtree = ay_ynode_get_last(dst->child);
    original_subtree = ay_ynode_get_node(tree, AY_INDEX(tree, src), src_id);
    ay_ynode_copy_subtree_when_ref_correction(copied_subtree, original_subtree);
}

/**
 * @brief Copy subtree @p src and insert it as sibling of @p dst.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in] dst Node whose sibling will be copied @p src.
 * @param[in] src Root of some subtree.
 */
static void
ay_ynode_copy_subtree_as_sibling(struct ay_ynode *tree, struct ay_ynode *dst, struct ay_ynode *src)
{
    struct ay_ynode *iter, *copied_subtree, *original_subtree;
    uint32_t subtree_size, src_id;

    assert((src->descendants + 1 + LY_ARRAY_COUNT(tree)) <= AY_YNODE_ROOT_ARRSIZE(tree));

    src_id = src->id;
    subtree_size = src->descendants + 1;
    for (iter = dst->parent; iter; iter = iter->parent) {
        iter->descendants += subtree_size;
    }
    ay_ynode_copy_subtree(tree, AY_INDEX(tree, dst + dst->descendants + 1), AY_INDEX(tree, src));
    ay_ynode_tree_correction(tree);
    copied_subtree = dst->next;
    original_subtree = ay_ynode_get_node(tree, AY_INDEX(tree, src), src_id);
    ay_ynode_copy_subtree_when_ref_correction(copied_subtree, original_subtree);
}

/**
 * @brief Unite (reset) choice for ynodes.
 *
 * The lnode tree contains nodes that are not important to the ynode tree. They may even be misleading and lead
 * to bugs. For example, ay_ynode.choice may incorrectly point to a L_UNION consisting of L_DEL nodes. But it should
 * point to L_UNION, just like its ynode sibling. This function resets choice by sibling if certain conditions are met.
 *
 * @param[in,out] tree Tree of ynodes.
 */
static void
ay_ynode_unite_choice(struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i;
    struct ay_ynode *first, *node, *iter;
    const struct ay_lnode *ln, *old_choice;

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        first = &tree[i];
        /* Start at first child. */
        if (!first->parent || (first->parent->child != first)) {
            continue;
        }
        /* Iterate over siblings. */
        for (node = first; node; node = node->next) {
            if (!node->next) {
                break;
            } else if (!node->choice || !node->next->choice) {
                continue;
            }

            /* Reset if one choice is a descendant of another. */
            for (ln = node->choice; ln && node->parent->snode; ln = ln->parent) {
                if (ln->lens == node->next->choice->lens) {
                    /* Reset the choice for all nodes in choice group. */
                    old_choice = node->choice;
                    iter = ay_ynode_get_first_in_choice(node->parent, node->choice);
                    for ( ; iter && (iter->choice == old_choice); iter = iter->next) {
                        iter->choice = node->next->choice;
                    }
                    break;
                }
            }
            /* A similar case. */
            for (ln = node->next->choice; ln && node->parent->snode; ln = ln->parent) {
                if (ln->lens == node->choice->lens) {
                    /* Reset the choice for all nodes in choice group. */
                    old_choice = node->next->choice;
                    for (iter = node->next; iter && (iter->choice == old_choice); iter = iter->next) {
                        iter->choice = node->choice;
                    }
                    break;
                }
            }
        }
    }
}

static const struct ay_lnode *
ay_lnode_get_snode_label(const struct ay_lnode *snode)
{
    LY_ARRAY_COUNT_TYPE i;
    const struct ay_lnode *label;

    label = NULL;
    for (i = 1; i <= snode->descendants; i++) {
        if (snode[i].lens->tag == L_SUBTREE) {
            i += snode[i].descendants;
        } else if (AY_TAG_IS_LABEL(snode[i].lens->tag) || AY_TAG_IS_VALUE(snode[i].lens->tag)) {
            label = &snode[i];
            break;
        }
    }

    return label;
}

/**
 * @brief Check if branch is empty due to deleted node.
 *
 * @param[in] chnode Group of subtrees in which to search for the deleted node.
 * @param[in] choice Choice to be searched.
 * @return 1 if @p choice contains deleted node or L_DEL nodes.
 */
static ly_bool
ay_ynode_mandatory_empty_branch_d2_deleted_node(struct ay_ynode *chnode, const struct ay_lnode *choice)
{
    LY_ARRAY_COUNT_TYPE i;
    const struct ay_lnode *snode, *lnode, *branch, *iter;
    ly_bool found;

    /* For every branch in choice. */
    for (branch = choice->child; branch; branch = branch->next) {
        /* At least one L_SUBTREE must be found in the branch. */
        found = 0;
        for (i = 0; i <= branch->descendants; i++) {
            /* Find first L_SUBTREE. */
            snode = &branch[i];
            if (AY_TAG_IS_VALUE(snode->lens->tag)) {
                for (iter = snode; iter && (iter->lens->tag != L_SUBTREE) && (iter->lens->tag != L_UNION); iter = iter->parent) {}
                if (iter && (iter->lens->tag == L_UNION)) {
                    found = 1;
                    break;
                }
                continue;
            } else if (snode->lens->tag != L_SUBTREE) {
                continue;
            }

            /* Get label for snode. */
            lnode = ay_lnode_get_snode_label(snode);
            if (!lnode) {
                continue;
            } else if (ay_lense_is_comment(lnode->lens)) {
                /* Comment nodes are deleted, but they should not affect mandatory statement. */
                found = 1;
                break;
            }

            /* Check if snode has not been deleted. */
            if (ay_ynode_choice_contains_lnode(chnode, choice, lnode)) {
                found = 1;
                break;
            }
        }
        if (!found) {
            return 1;
        }
    }

    return 0;
}

/**
 * @brief Check if list should be mandatory false because all its children are mandatory false.
 *
 * @param[in] list Node of type YN_LIST to check.
 * @return 1 if list should be mandatory false.
 */
static ly_bool
ay_ynode_mandatory_in_list_children_mandfalse(struct ay_ynode *list)
{
    struct ay_ynode *child, *iter;

    for (child = list->child; child; child = child->next) {
        if (child->choice && (child->flags & AY_CHOICE_MAND_FALSE) && !ay_ynode_alone_in_choice(child)) {
            assert(child->flags & AY_CHOICE_MAND_FALSE);
            for (iter = child; iter && (iter->choice == child->choice); iter = iter->next) {}
            if (!iter) {
                break;
            }
            child = iter;
        } else {
            return 0;
        }
    }

    return 1;
}

/**
 * @brief Set AY_CHOICE_MAND_FALSE flag for choices which contains empty branch.
 *
 * An empty branch in L_UNION is one that contains nodes that was deleted (eg comment nodes
 * or nodes without a label). Or, an empty branch is considered to be one that contains only L_DEL nodes.
 *
 * @param[in,out] tree Tree of ynodes.
 */
static void
ay_ynode_mandatory_empty_branch(struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i;
    struct ay_ynode *chnode;

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        chnode = &tree[i];
        if (!chnode->choice || (chnode->flags & (AY_CHOICE_CREATED | AY_CHOICE_MAND_FALSE)) ||
                (chnode != ay_ynode_get_first_in_choice(chnode->parent, chnode->choice))) {
            continue;
        }

        if (ay_ynode_mandatory_empty_branch_d2_deleted_node(chnode, chnode->choice)) {
            chnode->flags |= AY_CHOICE_MAND_FALSE;
        }
    }
}

/**
 * @brief Check if list should be mandatory false due to some upper choice which is mandatory false.
 *
 * The examined choice statements are probably not in the ynode tree, but in the lnode tree.
 *
 * @param[in] list Node of type YN_LIST to check.
 * @return 1 if list should be mandatory false.
 */
static ly_bool
ay_ynode_mandatory_in_list_upper_choice_mandfalse(struct ay_ynode *list)
{
    const struct ay_lnode *start, *stop, *choice;

    if (!list->parent) {
        return 0;
    }

    /* Starting position to search. */
    if (list->snode && (list->snode->lens->tag == L_SUBTREE)) {
        start = list->snode;
    } else if (list->child && list->child->label) {
        start = list->child->label;
    } else if (list->child && list->child->snode) {
        start = list->child->snode;
    } else {
        return 0;
    }
    assert(start);

    /* End position where the search ends. */
    for (stop = list->parent->snode;
            stop && (stop->lens->tag != L_SUBTREE) && (stop->lens->tag != L_STAR);
            stop = stop->parent) {}
    if (!stop && list->label && (list->label->lens->tag == L_STAR)) {
        stop = list->label;
    } else if (!stop && (list->label->lens->tag == L_SEQ)) {
        stop = ay_lnode_has_attribute(list->snode, L_STAR);
        assert(stop);
    } else if (!stop) {
        return 0;
    }

    /* Search for upper choice. */
    for (choice = start; choice != stop; choice = choice->parent) {
        if (choice->lens->tag != L_UNION) {
            continue;
        }

        if (ay_ynode_mandatory_empty_branch_d2_deleted_node(list->parent, choice)) {
            return 1;
        }
    }

    return 0;
}

/**
 * @brief Set list mandatory to false under certain conditions.
 *
 * @param[in,out] tree Tree of ynodes.
 */
static void
ay_ynode_mandatory_in_list(struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i;
    struct ay_ynode *list;

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        list = &tree[i];
        if ((list->type != YN_LIST) || (list->flags & AY_YNODE_MAND_FALSE) || (list->min_elems == 0)) {
            continue;
        }

        if (ay_ynode_mandatory_in_list_children_mandfalse(list) ||
                ay_ynode_mandatory_in_list_upper_choice_mandfalse(list)) {
            list->min_elems = 0;
            list->flags &= ~AY_YNODE_MAND_MASK;
            list->flags |= AY_YNODE_MAND_FALSE;
        }
    }
}

/**
 * @brief Get last node which belongs to choice.
 *
 * @param[in] first First node in choice.
 * @return Last node in choice or @p first;
 */
static struct ay_ynode *
ay_ynode_get_last_in_choice(struct ay_ynode *first)
{
    struct ay_ynode *iter;
    struct ay_ynode *last;

    last = first;
    for (iter = first; iter && (iter->choice == first->choice); iter = iter->next) {
        last = iter;
    }

    return last;
}

/**
 * @brief Correction of mandatory statements in choice and list.
 *
 * If list has mandatory false and all its children has mandatory false then
 * choice should has mandatory true because the optionality of that data in choice
 * is moved to list.
 *
 * @param[in,out] tree Tree of ynodes.
 */
static void
ay_ynode_mandatory_choice_in_list(struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i;
    struct ay_ynode *list, *chnode;

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        list = &tree[i];
        /* Get list with mandatory false. */
        if ((list->type != YN_LIST) || !list->child ||
                (!list->choice && ((list->flags & AY_YNODE_MAND_TRUE) || list->min_elems)) ||
                /* Check if all children has mandatory false. */
                (!(list->child->choice && (list->child->flags & AY_CHOICE_MAND_FALSE) &&
                (chnode = ay_ynode_get_last_in_choice(list->child)) && !chnode->next))) {
            /* No correction because list contains some node which is not in choice.
             * And if mandatory in choice will be set to true, additional data would be
             * incorrectly required, which are actually optional.
             */
            continue;
        }

        list->child->flags &= ~AY_CHOICE_MAND_FALSE;
    }
}

/**
 * @brief Set ynode.mandatory for all nodes.
 *
 * @param[in,out] tree Tree of ynodes.
 */
static void
ay_ynode_tree_set_mandatory(struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i;
    struct ay_ynode *node, *iter;
    const struct ay_lnode *lnode;

    /* TODO: in YN_CASE node should be at least one node with mandatory true (if choice is mandatory true). */

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        node = &tree[i];

        if (node->flags & AY_CHILDREN_MAND_FALSE) {
            if (node->type == YN_CASE) {
                node->child->flags |= AY_YNODE_MAND_TRUE;
                for (iter = node->child->next; iter; iter = iter->next) {
                    iter->flags |= AY_YNODE_MAND_FALSE;
                }
            } else {
                for (iter = node->child; iter; iter = iter->next) {
                    iter->flags |= AY_YNODE_MAND_FALSE;
                }
            }
        }

        if (node->type == YN_CONTAINER) {
            node->flags &= ~AY_YNODE_MAND_MASK;
            continue;
        } else if (node->type == YN_KEY) {
            node->flags &= ~AY_YNODE_MAND_MASK;
            node->flags |= AY_YNODE_MAND_TRUE;
            continue;
        } else if ((node->parent->type == YN_LIST) && (node->parent->child == node) &&
                (node->parent->descendants == 1) && (node->parent->parent->type != YN_ROOT)) {
            node->flags &= ~AY_YNODE_MAND_MASK;
            node->flags |= AY_YNODE_MAND_TRUE;
            node->min_elems = 1;
            continue;
        } else if ((node->flags & AY_HINT_MAND_TRUE) && !ay_lnode_has_maybe(node->snode, 0, 0)) {
            node->flags |= AY_YNODE_MAND_TRUE;
            node->min_elems = (node->type == YN_LIST) ? 1 : 0;
            continue;
        } else if (node->flags & AY_HINT_MAND_FALSE) {
            node->flags |= AY_YNODE_MAND_FALSE;
            continue;
        } else if (node->flags & AY_YNODE_MAND_TRUE) {
            continue;
        } else if (node->flags & AY_YNODE_MAND_FALSE) {
            node->min_elems = 0;
            continue;
        }

        if (node->type == YN_CONTAINER) {
            if (ay_lnode_has_maybe(node->snode, 0, 1)) {
                node->flags |= AY_YNODE_MAND_FALSE;
            } else {
                node->flags |= AY_YNODE_MAND_TRUE;
            }
        } else if ((node->type == YN_VALUE) &&
                ((node->flags & AY_VALUE_MAND_FALSE) || ay_yang_type_is_empty(node->value))) {
            node->flags |= AY_YNODE_MAND_FALSE;
        } else if (node->type == YN_LIST) {
            lnode = AY_YNODE_IS_SEQ_LIST(node) ? node->snode : node->label;
            if (ay_lnode_has_maybe(lnode, 0, 0)) {
                node->flags |= AY_YNODE_MAND_FALSE;
                node->min_elems = 0;
            } else if (node->min_elems) {
                node->flags |= AY_YNODE_MAND_TRUE;
            } else {
                node->flags |= AY_YNODE_MAND_FALSE;
            }
        } else {
            if (ay_lnode_has_maybe(node->snode, 0, 0)) {
                node->flags |= AY_YNODE_MAND_FALSE;
                node->min_elems = 0;
            } else {
                node->flags |= AY_YNODE_MAND_TRUE;
            }
        }
    }

    /* Setting mandatory for list and choice is not so obvious. The ynode tree does not contain all nodes
     * and also does not contain L_DEL lenses. So the following cases are in ynode tree ignored:
     * 1. Node is comment.
     * 2. Empty node. Node has no label.
     * 3. No nodes. Branch in L_UNION that only contains L_DEL.
     * But cases 2. and 3. have an effect on mandatory-stmt.
     * A branch containing empty nodes or no nodes must have choice set to mandatory false.
     */
    ay_ynode_mandatory_empty_branch(tree);
    ay_ynode_mandatory_in_list(tree);

    /* Setting AY_CHOICE_MAND_FALSE and set mandatory-false for nodes under choice. */
    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        node = &tree[i];
        if ((node != ay_ynode_get_first_in_choice(node->parent, node->choice)) || ay_ynode_alone_in_choice(node)) {
            continue;
        }
        if (!(node->flags & AY_CHOICE_MAND_FALSE) && ay_lnode_has_maybe(node->choice, 0, 0)) {
            /* There is L_MAYBE above L_UNION.*/
            node->flags |= AY_CHOICE_MAND_FALSE;
        } else if (!(node->flags & AY_CHOICE_MAND_FALSE)) {
            for (iter = node; iter && (iter->choice == node->choice); iter = iter->next) {
                if (iter->snode && (iter->snode->lens->tag == L_REC)) {
                    /* Recursive list is exception */
                    break;
                } else if (iter->flags & AY_YNODE_MAND_FALSE) {
                    /* Some node under choice has mandatory false. So choice cannot be mandatory-true. */
                    node->flags |= AY_CHOICE_MAND_FALSE;
                    break;
                }
            }
        }
    }

    /* There is one more rule. If list is mandatory false and its choice is mandatory false,
     * then it is unnecessary to have this information twice. Therefore, its choice is reset to mandatory true.
     */
    ay_ynode_mandatory_choice_in_list(tree);
}

/**
 * @brief Set AY_WHEN_ORNOT flag for 'when-stmt'.
 *
 * @param[in,out] tree Tree of ynodes.
 */
static void
ay_ynode_when_ornot(struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i;
    struct ay_ynode *iter, *target;
    const struct ay_lnode *lnode;

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        iter = &tree[i];
        if (!iter->when_val) {
            continue;
        }
        target = ay_ynode_when_target(tree, iter, NULL, NULL);
        if (target->flags & AY_YNODE_MAND_TRUE) {
            /* 'or not()' is valid only for nodes with mandatory false */
            continue;
        }
        /* Move to the [] */
        for (lnode = iter->when_val; lnode && (lnode->lens->tag != L_SUBTREE); lnode = lnode->parent) {}
        /* Search '?' */
        for (lnode = lnode->parent; lnode && (lnode != target->choice); lnode = lnode->parent) {
            if (lnode->lens->tag == L_MAYBE) {
                iter->flags |= AY_WHEN_ORNOT;
                break;
            }
        }
    }
}

/**
 * @brief Delete nodes with unkown type.
 *
 * @param[in,out] tree Tree of ynodes.
 */
static void
ay_delete_type_unknown(struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i;

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        if (tree[i].type == YN_UNKNOWN) {
            if (tree[i].child && (tree[i].child->type == YN_REC)) {
                ay_ynode_delete_node(tree, &tree[i]);
            } else {
                ay_ynode_delete_subtree(tree, &tree[i]);
            }
            i--;
        }
    }
}

/**
 * @brief Match build list pattern.
 *
 * lns . (sep . lns)*
 *
 * @param[in] node1 First node to compare (first lns).
 * @param[in] node2 Second node to compare (second lns which is under star).
 * @param[in] list_check Set flag will cause that list's condition not to be compared.
 * @return 1 if pattern was found.
 */
static ly_bool
ay_ynode_build_list_match(struct ay_ynode *node1, struct ay_ynode *node2, ly_bool list_check)
{
    if ((node1->type == YN_REC) || (node2->type == YN_REC)) {
        assert(node1->snode && node2->snode);
        if (node1->snode->lens != node2->snode->lens) {
            return 0;
        }
        assert((node1->type == YN_REC) && (node2->type == YN_REC));
    } else if ((node1->choice && (node1->choice == node2->choice) && !ay_ynode_common_concat(node1, node2, node1->choice)) ||
            !node2->label || !node2->snode ||
            (list_check && (node2->type != YN_LIST)) ||
            (list_check && (node1->type == YN_LIST) &&
            ((ay_ynode_alone_in_choice(node1) && !ay_ynode_common_concat(node1, node2, node1->parent->snode)) ||
            (!ay_ynode_alone_in_choice(node1) && !ay_ynode_common_concat(node1, node2, node1->choice)) ||
            (ay_lnode_has_attribute(node1->snode, L_STAR) == ay_lnode_has_attribute(node2->snode, L_STAR)))) ||
            !ay_lnode_lense_equal(node1->label->lens, node2->label->lens) ||
            ((node1->value && !node2->value) || (!node1->value && node2->value)) ||
            (node1->value && node2->value && !ay_lnode_lense_equal(node1->value->lens, node2->value->lens))) {
        return 0;
    }

    return 1;
}

/**
 * @brief The order of the siblings nodes will be reversed.
 *
 * Each 'next' pointer will be set to its predecessor.
 * Each 'child' pointer will be set to its last child.
 * The order of the nodes stored in memory does not change. Only the pointer settings change.
 *
 * @param[in,out] tree Tree of ynodes.
 */
static void
ay_ynode_siblings_reverse(struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i;
    struct ay_ynode *iter, *parent, *last_new, *last_old, *prev;

    for (i = 0; i < LY_ARRAY_COUNT(tree); i++) {
        parent = &tree[i];

        last_old = ay_ynode_get_last(parent->child);
        if (!last_old) {
            continue;
        }
        parent->child = last_old;
        last_new = parent + 1;
        prev = NULL;
        for (iter = last_new; iter != last_old; iter += iter->descendants + 1) {
            iter->next = prev;
            prev = iter;
        }
        last_old->next = prev;
    }
}

/**
 * @brief The order of the siblings nodes will be set to its original form.
 *
 * Each 'next' pointer will be set to its successor.
 * Each 'child' pointer will be set to its first child.
 *
 * @param[in,out] tree Tree of ynodes.
 */
static void
ay_ynode_siblings_reverse_back(struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i;
    struct ay_ynode *parent, *iter, *next_iter;
    uint64_t sum, next_sum;

    for (i = 0; i < LY_ARRAY_COUNT(tree); i++) {
        parent = &tree[i];
        if (parent->child) {
            parent->child = parent + 1;
            for (iter = parent + 1, sum = 0; iter; iter = next_iter, sum = next_sum) {
                next_sum = sum + iter->descendants + 1;
                next_iter = next_sum < parent->descendants ? iter + iter->descendants + 1 : NULL;
                iter->next = next_iter;
            }
        }
    }
}

/**
 * @brief Delete choice for top-nodes.
 *
 * Delete "lns . ( sep . lns )*" pattern. This pattern is located in Build module (build.aug).
 * The first 'lns' is useless.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in] reverse Flag must be set to 1 if siblings pointers (next) are set in reverse order.
 */
static void
ay_ynode_delete_build_list_(struct ay_ynode *tree, ly_bool reverse)
{
    LY_ARRAY_COUNT_TYPE i, j;
    struct ay_ynode *node1, *node2, *it1, *it2, *prev1, *prev2;
    uint64_t cmp_cnt;

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        node1 = &tree[i];
        if (node1->type == YN_REC) {
            assert(node1->snode);
        } else if (!node1->label || !node1->snode) {
            continue;
        }

        /* lns . ( sep . lns )* -> first lns can have multiple nodes, so a for loop is used. */
        for (node2 = node1->next; node2; node2 = node2->next) {
            if (!ay_ynode_build_list_match(node1, node2, 1)) {
                continue;
            }
            /* node1 == node2 */

            /* Remaining nodes must fit. */
            cmp_cnt = 1;
            for (it1 = node1->next, it2 = node2->next; it2 && (it1 != node2); it1 = it1->next, it2 = it2->next) {
                if (ay_ynode_build_list_match(it1, it2, 1)) {
                    cmp_cnt++;
                } else {
                    break;
                }
            }
            if (it1 != node2) {
                /* The node1 and node2 groups are very similar, but not the same.
                 * So this is not an build pattern.
                 */
                continue;
            }

            /* The build pattern detected. The first lns in "lns . ( sep . lns )*" will be deleted. */
            /* set minimal-elements for leader of node2 group */
            for (it1 = node1, it2 = node2, j = 0; j < cmp_cnt; it1 = it1->next, it2 = it2->next, j++) {
                if (ay_ynode_build_list_match(it1, it2, 1) && (it1->type != YN_LIST)) {
                    it2->min_elems++;
                }
            }
            prev1 = ay_ynode_get_prev(node1);
            prev2 = ay_ynode_get_prev(node2);
            if (prev1 && ay_ynode_build_list_match(prev1, prev2, 0)) {
                /* Search again due to 'lns . (sep . lns) . (sep . lns)*' pattern.
                 * First lns should be deleted too.
                 */
                for (j = 1; j < cmp_cnt; j++) {
                    prev1 = ay_ynode_get_prev(prev1);
                }
                i = AY_INDEX(tree, prev1);
            }

            if (reverse) {
                for (j = 0; j < cmp_cnt; j++) {
                    i -= prev2->descendants + 1;
                    ay_ynode_delete_subtree(tree, prev2);
                }
                /* The ay_ynode_delete_subtree() function set 'next' and 'child' pointers in the original form.
                 * Which is not desirable and the 'next' and 'child' pointers must be set back to the reversed order.
                 */
                ay_ynode_siblings_reverse(tree);
            } else {
                for (j = 0; j < cmp_cnt; j++) {
                    ay_ynode_delete_subtree(tree, node1);
                }
                i--;
            }
            break;
        }
    }
}

/**
 * @brief Main function for deleting build list pattern.
 *
 * @param[in,out] tree Tree of ynodes.
 */
static void
ay_ynode_delete_build_list(struct ay_ynode *tree)
{
    ay_ynode_delete_build_list_(tree, 0);

    /* Let's change 'next' and 'child' pointers to browse siblings in reverse order
     * to match pattern (sep . lns)* . lns.
     */
    ay_ynode_siblings_reverse(tree);
    ay_ynode_delete_build_list_(tree, 1);
    /* Now the pointers return to their original form and everything is as usual. */
    ay_ynode_siblings_reverse_back(tree);
}

/**
 * @brief Fill dnode dictionaries for labels and values.
 *
 * Find labels and values which should be in the one union-stmt. See ::ay_lnode_next_lv().
 *
 * @param[in,out] tree Tree of ynodes.
 * @return 0 on success.
 */
static int
ay_ynode_set_lv(struct ay_ynode *tree)
{
    int ret;
    LY_ARRAY_COUNT_TYPE i;
    const struct ay_lnode *label, *value, *next;

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        label = tree[i].label;
        value = tree[i].value;
        next = label;
        while ((next = ay_lnode_next_lv(next, AY_LV_TYPE_LABEL))) {
            ret = ay_dnode_insert(AY_YNODE_ROOT_LABELS(tree), label, next, ay_dnode_lnode_equal);
            AY_CHECK_RET(ret);
        }
        next = value;
        while ((next = ay_lnode_next_lv(next, AY_LV_TYPE_VALUE))) {
            ret = ay_dnode_insert(AY_YNODE_ROOT_VALUES(tree), value, next, ay_dnode_lnode_equal);
            AY_CHECK_RET(ret);
        }
    }

    return 0;
}

/**
 * @brief Insert a list whose key is the path to the configuration file that Augeas parsed.
 *
 * @param[in,out] tree Tree of ynodes.
 * @return 0.
 */
static int
ay_insert_list_files(struct ay_ynode *tree)
{
    struct ay_ynode *list;

    if (tree->descendants) {
        ay_ynode_insert_parent(tree, &tree[1]);
    } else {
        ay_ynode_insert_child(tree, tree);
    }
    list = &tree[1];
    list->type = YN_LIST;

    return 0;
}

/**
 * @brief For @p node iterate to parental @p choice node and return branch.
 *
 * @param[in] node Node to start the search.
 * @param[in] choice Parent node where the search ends.
 * @return Child of @p choice in whose subtree @p node is located.
 */
static const struct ay_lnode *
ay_lnode_choice_branch(const struct ay_lnode *node, const struct ay_lnode *choice)
{
    const struct ay_lnode *iter, *prev;

    for (prev = node, iter = node->parent; iter && (iter != choice); iter = iter->parent) {
        prev = iter;
    }
    assert(iter);

    return prev;
}

/**
 * @brief Insert new nodes and create the right form of ay_ynode_more_keys_for_node() transformation.
 *
 * Note: instead of the augeas 'key' statement, there can of course be a 'label' statement. It doeas not matter.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in] main_key The main key from the dictionary.
 * @param[in] node Node that contains @p main_key as its label.
 * @param[in] choice The common L_UNION node under which is each key.
 * The '|' in ([key lns1 | key lns2 | key lns3 | ...]).
 */
static void
ay_ynode_more_keys_for_node_insert_nodes(struct ay_ynode *tree, struct ay_dnode *main_key, struct ay_ynode *node,
        const struct ay_lnode *choice)
{
    LY_ARRAY_COUNT_TYPE i, j, k;
    struct ay_dnode *key;
    struct ay_ynode *sibl, *child;
    const struct ay_lnode *iter, *branch;

    /* Insert new siblings for @p node and each of them later will have a corresponding key from dictionary. */
    for (i = 0; i < main_key->values_count; i++) {
        ay_ynode_insert_sibling(tree, node);
    }
    /* @p node must have choice because of pattern: [ key lns1 | key lns2 ... ] */
    node->choice = !node->choice ? choice : node->choice;
    /* Set corresponding key from dictionary and set common choice. */
    for (i = 0; i < main_key->values_count; i++) {
        key = main_key + i + 1;
        sibl = (node->next + i);
        sibl->label = key->lval;
        sibl->type = YN_CONTAINER;
        sibl->choice = node->choice;
    }

    /* The key can have a set of nodes. For example: [ key lns1 [node1] [node2] | key lns2 [node3] ... ]
     *                                                  ^__________^_______^       ^_________^
     * Such a set of nodes must be found and assigned to the corresponding sibling node:
     * [ key lns1 [node1] [node2] | key lns2 [node3] ... ] -> [ key lns1 [node1] [node2] ] | [ key lns2 [node3] ] ...
     * ^_________________________________________________^    ^__________________________^   ^__________________^
     *                        ^@p node                                ^first_sibling                ^second
     * All set of nodes are incorrectly located in @p node and must be moved to the corresponding sibling node.
     */
    AY_DNODE_VAL_FOR(main_key, i) {
        key = &main_key[i];
        /* Assume that all keys are under the same choice. */
        assert(choice == ay_ynode_common_choice(main_key->lkey, key->lval, choice));
        branch = ay_lnode_choice_branch(key->lval, choice);
        /* Find set of nodes for 'key'. */
        for (j = 0; j <= branch->descendants; j++) {
            iter = &branch[j];
            if (iter->lens->tag != L_SUBTREE) {
                continue;
            }
            /* The 'key' may not have some subtree because maybe the subtree was deleted (it is some comment node).
             * So the subtree must be found in @p node.
             */
            /* Find set of nodes in @p node. */
            for (child = node->child; child; child = child->next) {
                if (child->snode != iter) {
                    continue;
                }
                /* The 'key' will contain set of nodes. */
                /* Get the corresponding sibling of @p node (where 'key' is assigned). */
                for (k = 1, sibl = node->next; k < i; k++, sibl = sibl->next) {}
                /* Reset choice for child. */
                ay_ynode_reset_choice(child, choice);
                /* Move subtree. */
                ay_ynode_move_subtree_as_last_child(tree, sibl, child);
                break;
            }
        }
    }

    /* One more thing. The @p node could originally have the form:
     * [ (key lns1 | key lns2 ...) . [basic_nodes] ]
     * These basic nodes must be copied because each must contain them, so it must look like:
     * -> [ key lns1 [basic_nodes] ] | [ key lns2 . [basic_nodes] ]
     */
    for (child = node->child; child; child = child->next) {
        if (child->choice == choice) {
            /* Ignore because this is node belonging to @p node. */
            continue;
        }
        /* Copy the 'basic_nodes' to the corresponding siblings of @p node. */
        for (i = 0, sibl = node->next; i < main_key->values_count; i++, sibl = sibl->next) {
            ay_ynode_copy_subtree_as_last_child(tree, sibl, child);
        }
    }
}

/**
 * @brief Split node containing multiple keys.
 *
 * [ key lns1 | key lns2 ... ] -> [ key lns1 ] | [ key lns2 ] ...
 * More details are int the ay_ynode_more_keys_for_node_insert_nodes().
 *
 * @param[in,out] tree Tree of ynodes.
 * @return 0 on success.
 */
static int
ay_ynode_more_keys_for_node(struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i, j;
    struct ay_dnode *labels, *main_key;
    const struct ay_lnode *choice, *iter;
    struct ay_ynode *ynode;

    labels = AY_YNODE_ROOT_LABELS(tree);

    if (!LY_ARRAY_COUNT(labels)) {
        return 0;
    }

    AY_DNODE_KEY_FOR(labels, i) {
        main_key = &labels[i];

        /* Get node which contains 'main_key'. */
        ynode = NULL;
        for (j = 0; j < LY_ARRAY_COUNT(tree); j++) {
            if (tree[j].label == main_key->lkey) {
                ynode = &tree[j];
                break;
            }
        }
        assert(ynode && ynode->snode && (ynode->snode->lens->tag == L_SUBTREE) && (ynode->snode < ynode->label));

        /* Get choice of 'main_key'. */
        choice = NULL;
        for (iter = main_key->lkey->parent; iter && (iter->lens->tag != L_SUBTREE); iter = iter->parent) {
            if (iter->lens->tag == L_UNION) {
                choice = iter;
                break;
            }
        }
        assert(choice);

        /* Apply transformation. */
        ay_ynode_more_keys_for_node_insert_nodes(tree, main_key, ynode, choice);
    }

    return 0;
}

/**
 * @brief Set choice to YN_VALUE node.
 *
 * @param[in] tree Tree of ynodes.
 * @param[in,out] node Node to process.
 */
static void
ay_ynode_set_choice_for_value(const struct ay_ynode *tree, struct ay_ynode *node)
{
    const struct ay_lnode *snode, *choice;
    struct ay_dnode *values;

    assert((node->type == YN_VALUE) && node->value && node->parent);

    values = AY_YNODE_ROOT_VALUES(tree);
    choice = ay_lnode_has_attribute(node->value, L_UNION);

    if (!node->next && AY_YNODE_IS_SEQ_LIST(node->parent)) {
        for (snode = node->value; snode && (snode->lens->tag != L_SUBTREE); snode = snode->parent) {}
        if (snode) {
            node->choice = ay_lnode_has_attribute(snode, L_UNION);
        }
        return;
    } else if (!node->next ||
            (!(node->parent->flags & AY_VALUE_IN_CHOICE) && (!choice || ay_dnode_find(values, node->value)))) {
        return;
    }

    assert(node->next);
    if (node->next->choice && ((node->parent->flags & AY_VALUE_IN_CHOICE) || (node->next->choice == choice))) {
        node->choice = node->next->choice;
    } else if (!node->next->choice && ay_ynode_rule_node_is_splittable(tree, node->next)) {
        node->choice = AY_YNODE_ROOT_LTREE(tree);
        node->flags |= AY_CHOICE_CREATED;
    }
}

/**
 * @brief Place YN_VALUE node close to the parent.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in] node Parent node of YN_VALUE.
 * @return Pointer to YN_VALUE but is empty.
 */
static struct ay_ynode *
ay_ynode_place_value_as_usual(struct ay_ynode *tree, struct ay_ynode *node)
{
    if (node->snode && node->child && (node->child->type == YN_KEY)) {
        /* Insert value behind key. */
        ay_ynode_insert_sibling(tree, node->child);
        return node->child->next;
    } else {
        /* Insert value as first child. Key is label or pattern_has_idents. */
        ay_ynode_insert_child(tree, node);
        return node->child;
    }
}

/**
 * @brief Find a child whose ay_ynode.snode is @p snode.
 *
 * @param[in] parent Node in which children are searched.
 * @param[in] snode Lnode with lens tag L_SUBTREE.
 * @param[in] into_case If flag is set to 1, the search will continue in YN_CASE node.
 * @return Pointer to ynode with equal snode.
 * @return Pointer to YN_CASE (@p into_case must be set) which contains such a node.
 * @return NULL.
 */
static struct ay_ynode *
ay_ynode_get_child_by_snode(struct ay_ynode *parent, const struct ay_lnode *snode, ly_bool into_case)
{
    struct ay_ynode *iter, *ret;

    ret = NULL;
    for (iter = parent->child; iter && !ret; iter = iter->next) {
        if ((into_case && (iter->type == YN_CASE) && (ret = ay_ynode_get_child_by_snode(iter, snode, 1))) ||
                (iter->snode && (snode->lens == iter->snode->lens)) ||
                ((iter->type == YN_LIST) && (iter->child->snode->lens == snode->lens))) {
            ret = iter;
        }
    }

    return ret;
}

/**
 * @brief The YN_VALUE will be placed somewhere as a child of @p node.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in] node Parent node of YN_VALUE.
 * @return Pointer to YN_VALUE node.
 */
static struct ay_ynode *
ay_ynode_place_value(struct ay_ynode *tree, struct ay_ynode *node)
{
    const struct ay_lnode *iterl, *choice, *choice_wanted, *val_parent;
    struct ay_ynode *dst, *value;

    assert(node->value);

    if (!node->snode) {
        return ay_ynode_place_value_as_usual(tree, node);
    }

    /* Find L_SUBTREE before 'value' */
    for (val_parent = node->value; val_parent->lens->tag != L_SUBTREE; val_parent = val_parent->parent) {}
    dst = NULL;
    for (iterl = node->value; (iterl != val_parent) && !dst; iterl--) {
        if (iterl->lens->tag != L_SUBTREE) {
            continue;
        }
        dst = ay_ynode_get_child_by_snode(node, iterl, 1);
    }
    if (!dst) {
        return ay_ynode_place_value_as_usual(tree, node);
    }
    /* An unusual place for value is found. */

    ay_ynode_insert_sibling(tree, dst);
    value = dst->next;

    /* Set the correct choice if any. */

    if (dst->choice) {
        choice_wanted = dst->choice;
    } else if (value->next && value->next->choice) {
        choice_wanted = value->next->choice;
    } else {
        /* The choice is NULL. */
        return value;
    }

    /* Check if 'value' is under 'choice_wanted'. */
    choice = NULL;
    for (iterl = node->value; iterl != node->snode; iterl = iterl->parent) {
        if (choice_wanted == iterl) {
            /* Yes, it is under choice. */
            choice = choice_wanted;
            break;
        }
    }
    value->choice = choice;

    return value;
}

/**
 * @brief Insert node key (YN_KEY).
 *
 * Also node with 'store' pattern can be generated too.
 *
 * @param[in,out] tree Tree of ynodes.
 * @return 0.
 */
static int
ay_insert_node_key_and_value(struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i;
    struct ay_ynode *node, *key, *value;
    uint64_t count;

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        node = &tree[i];
        if ((node->type != YN_CONTAINER) && !AY_YNODE_IS_SEQ_LIST(node)) {
            continue;
        }
        count = ay_ynode_rule_node_key_and_value(tree, &tree[i]);
        if (AY_LABEL_LENS_IS_IDENT(node)) {
            if (node->descendants == 0) {
                node->type = YN_LEAF;
            } else if (node->value) {
                value = ay_ynode_place_value(tree, node);
                value->type = YN_VALUE;
                value->label = node->label;
                value->value = node->value;
                value->flags |= (node->flags & AY_VALUE_MAND_FALSE);
                ay_ynode_set_choice_for_value(tree, value);
            }
        } else if (count == 0) {
            node->type = YN_LEAF;
        } else {
            assert(node->label);
            if (count == 1) {
                ay_ynode_insert_child(tree, node);
                key = node->child;
                key->type = YN_KEY;
                key->label = node->label;
                key->value = node->value;
            } else {
                assert(count == 2);
                ay_ynode_insert_child(tree, node);
                key = node->child;
                key->type = YN_KEY;
                key->label = node->label;
                key->value = node->value;

                value = ay_ynode_place_value(tree, node);
                value->type = YN_VALUE;
                value->label = node->label;
                value->value = node->value;
                value->flags |= (node->flags & AY_VALUE_MAND_FALSE);
                ay_ynode_set_choice_for_value(tree, value);
            }
        }
    }

    return 0;
}

/**
 * @brief Insert YN_CASE node which groups nodes under one case-stmt.
 *
 * Function set ay_ynode.ref for function ay_ynode_copy_case_nodes().
 *
 * @param[in,out] tree Tree of ynodes.
 * @return 0.
 */
static int
ay_ynode_insert_case(struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i;
    struct ay_ynode *first, *cas, *iter;
    const struct ay_lnode *common_choice;
    uint64_t j, cnt;

    assert(!tree->ref);
    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        first = &tree[i];
        assert(!first->ref);
        cnt = 0;
        /* Count how many subtrees will be in case. */
        for (iter = first->next; iter; iter = iter->next) {
            if (!ay_ynode_insert_case_prerequisite(first, iter)) {
                break;
            }
            /* Get first common choice. */
            common_choice = ay_ynode_common_choice(first->snode, iter->snode, first->choice);
            /* Get last common concatenation. */
            if (!ay_ynode_common_concat(first, iter, common_choice)) {
                break;
            }
            cnt++;
        }
        if (!cnt) {
            /* No case will be generated. */
            continue;
        }

        /* Insert case. */
        ay_ynode_insert_wrapper(tree, first);
        cas = first;
        first = cas->child;
        cas->type = YN_CASE;
        cas->choice = first->choice;
        first->choice = NULL;

        /* Move subtrees to case. */
        for (j = 0; j < cnt; j++) {
            ay_ynode_move_subtree_as_last_child(tree, cas, cas->next);
        }
        /* Reset choice in children. */
        for (iter = cas->child->next; iter; iter = iter->next) {
            ay_ynode_reset_choice(iter, cas->choice);
        }
        /* Set choice to NULL if some child is alone in choice. */
        for (iter = cas->child->next; iter; iter = iter->next) {
            if (ay_ynode_alone_in_choice(iter)) {
                iter->choice = NULL;
            }
        }
        /* In the end, the case is alone, so it will be deleted. */
        if (ay_ynode_alone_in_choice(cas) && (ay_lnode_has_attribute(cas->parent->value, L_UNION) != cas->choice)) {
            ay_ynode_delete_node(tree, cas);
            continue;
        }
        /* Find the branch to which the nodes should also be copied. Only references are set.
         * The copying itself happens in the ay_ynode_copy_case_nodes(). */
        for (iter = ay_ynode_get_prev(cas); iter; iter = ay_ynode_get_prev(iter)) {
            common_choice = ay_ynode_common_choice(cas->child->snode, iter->snode, cas->choice);
            if ((first->choice == common_choice) || !ay_ynode_common_concat(cas->child->next, iter, cas->choice)) {
                break;
            }
            iter->ref = cas->id;
            tree->ref = 1;
        }
        i++;
    }

    return 0;
}

/**
 * @brief Insert YN_CASE node and copy nodes which belong to it.
 *
 * Function uses ay_ynode.ref, which will be reset.
 *
 * @param[in,out] tree Tree of ynodes.
 * @return 0.
 */
static int
ay_ynode_copy_case_nodes(struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i;
    struct ay_ynode *first, *iter, *cas_src, *cas_dst;
    uint32_t cnt;

    assert(tree->ref);
    tree->ref = 0;

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        first = &tree[i];
        if (!first->ref) {
            continue;
        }

        /* Insert case. */
        ay_ynode_insert_wrapper(tree, first);
        cas_dst = first;
        first = cas_dst->child;
        cas_dst->type = YN_CASE;
        cas_dst->choice = first->choice;
        first->choice = NULL;

        /* Find nodes to copy. */
        for (cas_src = cas_dst->next; cas_src && (cas_src->id != first->ref); cas_src = cas_src->next) {}
        assert(cas_src);
        first->ref = 0;

        /* Copy nodes. */
        assert(cas_src->child->next);
        for (iter = cas_src->child->next; iter; iter = iter->next) {
            cnt = iter->descendants + 1;
            ay_ynode_copy_subtree_as_last_child(tree, cas_dst, iter);
            iter += cnt;
        }
    }

    return 0;
}

/**
 * @brief Insert YN_CASE node only if @p ns has no successor.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in] ns Group of ynodes that can be in YN_CASE.
 * @param[in] choice What 'choice' will be set in any case.
 * @return 1 if YN_CASE was inserted.
 */
static ly_bool
ay_ynode_case_insert(struct ay_ynode *tree, struct ay_ynode *ns, const struct ay_lnode *choice)
{
    struct ay_ynode *cas;

    if (ns->type == YN_CASE) {
        return 0;
    }

    if (!choice) {
        choice = AY_YNODE_ROOT_LTREE(tree);
        ns->flags |= AY_CHOICE_CREATED;
    }
    if (!ns->choice) {
        ns->flags |= AY_CHOICE_CREATED;
    }

    if (ns->next) {
        /* Create YN_CASE for ns. */
        ay_ynode_insert_parent_for_rest(tree, ns);
        cas = ns;
        cas->choice = choice;
        cas->flags |= AY_CHOICE_CREATED;
        cas->type = YN_CASE;
        cas->when_ref = cas->child->when_ref;
        cas->when_val = cas->child->when_val;
        cas->child->when_ref = 0;
        cas->child->when_val = NULL;
        return 1;
    } else {
        ns->choice = choice;
        ns->flags |= AY_CHOICE_CREATED;
        return 0;
    }
}

/**
 * @brief Move the 'when' data forward.
 *
 * @param[in,out] br Branch for merging.
 */
static void
ay_ynode_merge_cases_move_when(struct ay_ynode *br)
{
    struct ay_ynode *first;

    first = br->type == YN_CASE ? br->child : br;

    if (br->when_ref && first->child) {
        /* Moved to child. */
        first->child->when_ref = br->when_ref;
        first->child->when_val = br->when_val;
    }
    if (br->when_ref && (br->type == YN_CASE)) {
        /* Moved to sibling. */
        first->next->when_ref = br->when_ref;
        first->next->when_val = br->when_val;
    }
    /* Complete the move operation. */
    br->when_ref = 0;
    br->when_val = NULL;
}

/**
 * @brief Set 'when' data while merging cases.
 *
 * @param[in,out] br1 First branch to merge. The 'when' data are set or moved.
 * @param[in,out] br2 Second branch to merge. The 'when' data are set or moved.
 */
static void
ay_ynode_merge_cases_set_when(struct ay_ynode *br1, struct ay_ynode *br2)
{
    struct ay_ynode *first1, *first2;
    ly_bool first1_val_in_choice, first2_val_in_choice;

    first1 = br1->type == YN_CASE ? br1->child : br1;
    first2 = br2->type == YN_CASE ? br2->child : br2;

    if (br1->when_ref || br2->when_ref) {
        /* The 'when' data are just moved. */
        ay_ynode_merge_cases_move_when(br1);
        ay_ynode_merge_cases_move_when(br2);
        return;
    } else if (first1->value && first2->value && ay_lnode_lense_equal(first1->value->lens, first2->value->lens)) {
        /* Values are identical, so it cannot be distinguished. */
        return;
    }
    first1_val_in_choice = first1->flags & AY_VALUE_IN_CHOICE;
    first2_val_in_choice = first2->flags & AY_VALUE_IN_CHOICE;

    /* Set 'when' for child. */
    if (first1->child && !first2->child && first1->value && !first1_val_in_choice) {
        first1->child->when_ref = first1->id;
        first1->child->when_val = first1->value;
        first1->flags |= AY_WHEN_TARGET;
    } else if (!first1->child && first2->child && first2->value && !first2_val_in_choice) {
        first2->child->when_ref = first1->id;
        first2->child->when_val = first2->value;
        first1->flags |= AY_WHEN_TARGET;
    } else if (first1->child && first2->child) {
        if (first1->value && !first1_val_in_choice) {
            first1->child->when_ref = first1->id;
            first1->child->when_val = first1->value;
            first1->flags |= AY_WHEN_TARGET;
        }
        if (first2->value && !first2_val_in_choice) {
            first2->child->when_ref = first1->id;
            first2->child->when_val = first2->value;
            first1->flags |= AY_WHEN_TARGET;
        }
    }

    /* Set 'when' for sibling. */
    if ((br1->type == YN_CASE) && (br2->type == YN_CASE)) {
        if (first1->value && !first1_val_in_choice) {
            first1->next->when_ref = first1->id;
            first1->next->when_val = first1->value;
            first1->flags |= AY_WHEN_TARGET;
        }
        if (first2->value && !first2_val_in_choice) {
            first2->next->when_ref = first1->id;
            first2->next->when_val = first2->value;
            first1->flags |= AY_WHEN_TARGET;
        }
    } else if ((br1->type != YN_CASE) && (br2->type == YN_CASE)) {
        if (first2->value && !first2_val_in_choice) {
            first2->next->when_ref = first1->id;
            first2->next->when_val = first2->value;
            first1->flags |= AY_WHEN_TARGET;
        }
    } else if ((br1->type == YN_CASE) && (br2->type != YN_CASE)) {
        if (first1->value && !first1_val_in_choice) {
            first1->next->when_ref = first1->id;
            first1->next->when_val = first1->value;
            first1->flags |= AY_WHEN_TARGET;
        }
    }
}

/**
 * @brief Merge two group of nodes into one group.
 *
 * If @p merge_as_child is set then just move @p ns2 group to @p ns1 node as child.
 * Else move @p ns2 group to ns1 group as sibling choice branch.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in] ns1 First group of ynodes or parent where @p ns2 group will be moved (specify by @p merget_as_child).
 * @param[in] ns2 Second group of ynodes which will be moved to @p ns1.
 * @param[in] merge_as_child If set then @p ns1 is parent node.
 */
static void
ay_ynode_merge_nodes(struct ay_ynode *tree, struct ay_ynode *ns1, struct ay_ynode *ns2, ly_bool merge_as_child)
{
    ly_bool ns1_in_choice, ns2_in_choice;
    struct ay_ynode *iter, *last;

    if (!ns2) {
        return;
    }

    if (merge_as_child && ns2->next) {
        /* Temporarily wrap first2 children. */
        ay_ynode_insert_parent_for_rest(tree, ns2);
        /* Set first1 new children. */
        ay_ynode_move_subtree_as_last_child(tree, ns1, ns2);
        /* Delete temporary wrapping node. */
        last = ay_ynode_get_last(ns1->child);
        ay_ynode_delete_node(tree, last);
        last->flags |= last->choice ? AY_CHOICE_MAND_FALSE : 0;
    } else if (merge_as_child && !ns2->next) {
        ay_ynode_move_subtree_as_last_child(tree, ns1, ns2);
        ns1->flags |= AY_CHILDREN_MAND_FALSE;
        last = ay_ynode_get_last(ns1->child);
        last->choice = NULL;
    } else {
        assert(!merge_as_child);
        ns1_in_choice = ay_ynode_nodes_in_choice(ns1);
        ns2_in_choice = ay_ynode_nodes_in_choice(ns2);
        last = ay_ynode_get_last(ns1);
        if (ns1_in_choice && ns2_in_choice) {
            /* Temporarily wrap ns2 children. */
            ay_ynode_insert_parent_for_rest(tree, ns2);
            /* Set new sibling. */
            ay_ynode_move_subtree_as_sibling(tree, last, ns2);
            /* Delete temporary wrapping node. */
            ay_ynode_delete_node(tree, last->next);
            /* Unify choice. */
            for (iter = last->next; iter; iter = iter->next) {
                iter->choice = ns1->choice;
                iter->flags |= AY_CHOICE_CREATED;
            }
        } else if (ns1_in_choice && !ns2_in_choice) {
            ay_ynode_case_insert(tree, ns2, ns1->choice);
            /* Set new sibling. */
            ay_ynode_move_subtree_as_sibling(tree, last, ns2);
        } else if (!ns1_in_choice && ns2_in_choice) {
            if (ay_ynode_case_insert(tree, ns1, ns2->choice)) {
                ns2++;
            }
            /* Temporarily wrap ns2 children. */
            ay_ynode_insert_parent_for_rest(tree, ns2);
            /* Set new sibling. */
            ay_ynode_move_subtree_as_sibling(tree, ns1, ns2);
            /* Delete temporary wrapping node. */
            ay_ynode_delete_node(tree, ns1->next);
        } else {
            assert(!ns1_in_choice && !ns2_in_choice);
            if (ay_ynode_case_insert(tree, ns1, NULL)) {
                ns2++;
            }
            ay_ynode_case_insert(tree, ns2, NULL);
            /* Set new sibling. */
            ay_ynode_move_subtree_as_last_child(tree, ns1->parent, ns2);
        }
    }
}

/**
 * @brief Merging two branches.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in,out] br1 First branch.
 * @param[in,out] br2 Second branch.
 * @return 0 on success.
 */
static int
ay_ynode_merge_cases_(struct ay_ynode *tree, struct ay_ynode *br1, struct ay_ynode *br2)
{
    int ret = 0;
    uint32_t br2_id;
    struct ay_ynode *iter, *first1, *first2;

    br2_id = br2->id;
    first1 = br1->type == YN_CASE ? br1->child : br1;
    first2 = br2->type == YN_CASE ? br2->child : br2;

    first1->flags |= first2->flags;
    first1->flags |= AY_HINT_MAND_TRUE;
    first1->min_elems = first1->min_elems < first2->min_elems ? first1->min_elems : first2->min_elems;
    ay_ynode_merge_cases_set_when(br1, br2);

    /* Merge nodes inside first node. */
    if (first1->child && !first2->child) {
        /* Merge values. */
        if (first1->value && first2->value && !ay_lnode_lense_equal(first1->value->lens, first2->value->lens)) {
            ret = ay_dnode_insert(AY_YNODE_ROOT_VALUES(tree), first1->value, first2->value, ay_dnode_lnode_equal);
            AY_CHECK_RET(ret);
            /* All children in first1 are not mandatory. */
            first1->flags |= AY_CHILDREN_MAND_FALSE;
        } else if (first1->value && !first2->value) {
            first1->flags |= AY_VALUE_MAND_FALSE;
            first1->flags |= AY_CHILDREN_MAND_FALSE;
        } else if (!first1->value && first2->value) {
            first1->value = first2->value;
            first1->flags |= AY_VALUE_IN_CHOICE;
        }
        first1->child->flags |= (first1->flags & AY_VALUE_IN_CHOICE) ? 0 : AY_CHOICE_MAND_FALSE;
    } else if (!first1->child && first2->child) {
        /* Merge values. */
        if (first1->value && first2->value && !ay_lnode_lense_equal(first1->value->lens, first2->value->lens)) {
            ret = ay_dnode_insert(AY_YNODE_ROOT_VALUES(tree), first1->value, first2->value, ay_dnode_lnode_equal);
            AY_CHECK_RET(ret);
            first1->flags |= AY_CHILDREN_MAND_FALSE;
        } else if (first1->value && !first2->value) {
            first1->flags |= AY_VALUE_IN_CHOICE;
        } else if (!first1->value && first2->value) {
            first1->value = first2->value;
            first1->flags |= AY_VALUE_MAND_FALSE;
            first1->flags |= AY_CHILDREN_MAND_FALSE;
        }

        first1->type = (first1->type == YN_LIST) ? YN_LIST : YN_CONTAINER;
        ay_ynode_merge_nodes(tree, first1, first2->child, 1);
        first1->child->flags |= (first1->flags & AY_VALUE_IN_CHOICE) ? 0 : AY_CHOICE_MAND_FALSE;
    } else {
        assert((first1->child && first2->child) || (!first1->child && !first2->child));
        /* TODO: if they both have children, then where to place the values? It must be placed
         * in the right choice branch. The YN_VALUES will probably need to be already generated.
         */

        /* Merge values. */
        if (first1->value && first2->value && !ay_lnode_lense_equal(first1->value->lens, first2->value->lens)) {
            ret = ay_dnode_insert(AY_YNODE_ROOT_VALUES(tree), first1->value, first2->value, ay_dnode_lnode_equal);
            AY_CHECK_RET(ret);
        } else if (first1->value && !first2->value) {
            first1->flags |= AY_VALUE_MAND_FALSE;
        } else if (!first1->value && first2->value) {
            first1->value = first2->value;
            first1->flags |= AY_VALUE_MAND_FALSE;
        }

        if (first1->child && first2->child) {
            ay_ynode_merge_nodes(tree, first1->child, first2->child, 0);
        }
    }

    /* Set the pointers to the correct values. */
    for (iter = br1->next; iter && (iter->id != br2_id); iter = iter->next) {}
    assert(iter);
    br2 = iter;
    first2 = br2->type == YN_CASE ? br2->child : br2;

    /* Merge rest nodes. */
    if ((br1->type == YN_CASE) && (br2->type == YN_CASE)) {
        ay_ynode_merge_nodes(tree, first1->next, first2->next, 0);
    } else if ((br1->type == YN_CASE) && (br2->type != YN_CASE)) {
        /* All children except the first are not mandatory. */
        br1->flags |= AY_CHILDREN_MAND_FALSE;
        first1->next->flags |= AY_CHOICE_MAND_FALSE;
    } else if ((br1->type != YN_CASE) && (br2->type == YN_CASE)) {
        /* br1 must be YN_CASE because has at least two children */
        ay_ynode_insert_wrapper(tree, br1);
        br2++;
        br1->type = YN_CASE;
        /* All children except the first are not mandatory. */
        br1->flags |= AY_CHILDREN_MAND_FALSE;
        br1->choice = br1->child->choice;
        br1->flags |= AY_CHOICE_CREATED;
        br1->child->choice = NULL;
        ay_ynode_merge_nodes(tree, br1, br2->child->next, 1);
        first1->next->flags |= AY_CHOICE_MAND_FALSE;
    }

    /* Delete branch br2. */
    for (iter = br1->next; iter && (iter->id != br2_id); iter = iter->next) {}
    assert(iter);
    br2 = iter;
    ay_ynode_delete_subtree(tree, br2);

    return 0;
}

/**
 * @brief Merging two branches which are the same except for value in first node.
 *
 * If the branches are the same including values in first nodes,
 * the function returns 1 and @p br2 can be deleted.
 *
 * TODO: There is a problem if the first node is CONTAINER and the second is LIST. In that case, L_STAR
 * should be moved to the first node and its type should be changed to LIST. But then they won't match lnode subtree.
 * So the first node (first branch) should actually be deleted and not the second. So far this bug has not occurred.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in,out] br1 First branch.
 * @param[in,out] br2 Second branch.
 * @param[out] err Code is non-zero on error. It does not need to be set if successful.
 * @return 1 for success and the @p br2 subtree can be deleted.
 * @return 0 if the branches are differ, function will not change any data.
 */
static ly_bool
ay_ynode_merge_cases_only_by_value(struct ay_ynode *tree, struct ay_ynode *br1, struct ay_ynode *br2, int *err)
{
    struct ay_ynode *first1, *first2, *st1, *st2;

    assert(br1 && br2 && err);

    /* The branches must have the same form. */
    if (((br1->type != YN_CASE) && (br2->type == YN_CASE)) ||
            ((br1->type == YN_CASE) && (br2->type != YN_CASE))) {
        return 0;
    }

    first1 = br1->type == YN_CASE ? br1->child : br1;
    first2 = br2->type == YN_CASE ? br2->child : br2;

    if (br1->type == YN_CASE) {
        assert(br2->type == YN_CASE);
        /* Check if all sibling subtrees except the first one are equal. */
        for (st1 = first1->next, st2 = first2->next; st1 && st2; st1 = st1->next, st2 = st2->next) {
            if (!ay_ynode_subtree_equal(st1, st2, 1, 0)) {
                return 0;
            }
        }
        if ((!st1 && st2) || (st1 && !st2)) {
            /* The number of siblings differs. */
            return 0;
        }
    }

    /* Check if first node's children are equal. */
    if ((!first1->child && first2->child) || (first1->child && !first2->child) ||
            (first1->child && first2->child && !ay_ynode_subtree_equal(first1, first2, 0, 0))) {
        /* Children of the nodes are different, so the ay_ynode_merge_cases_() function must be called. */
        return 0;
    }

    /* Success, @p br2 can be deleted. */
    *err = 0;

    if (first1->value && first2->value && !ay_lnode_lense_equal(first1->value->lens, first2->value->lens)) {
        /* values are different, update dictionary for values. */
        *err = ay_dnode_insert(AY_YNODE_ROOT_VALUES(tree), first1->value, first2->value, ay_dnode_lnode_equal);
    } else if (first1->value && !first2->value) {
        first1->flags |= AY_VALUE_MAND_FALSE;
    } else if (!first1->value && first2->value) {
        first1->value = first2->value;
        first1->flags |= AY_VALUE_MAND_FALSE;
    }
    /* else br1 and br2 are equal */

    if (first2->type == YN_LIST) {
        /* The min_elems must be reset before @p br2 is deleted. */
        first1->min_elems = first1->min_elems < first2->min_elems ? first1->min_elems : first2->min_elems;
    }

    return 1;
}

/**
 * @brief Merging two nodes which are the same except for repetition.
 *
 * TODO Also check children?
 *
 * @param[in] br1 First branch to check.
 * @param[in] br2 Second branch to check.
 * @return 1 for success and br2 is copied to br1, so br2 must be deleted.
 * @return 0 merge cannot be applied and no data has changed.
 */
static ly_bool
ay_ynode_merge_cases_only_by_repetition(struct ay_ynode *br1, struct ay_ynode *br2)
{
    if ((br1->descendants != 0) || (br2->descendants != 0) || (br1->type == br2->type) ||
            (br1->value && br2->value && !ay_lnode_lense_equal(br1->value->lens, br2->value->lens)) ||
            !(
                ((br1->type == YN_LIST) && (br2->type == YN_LEAF)) ||
                ((br2->type == YN_LIST) && (br1->type == YN_LEAF)) ||
                ((br1->type == YN_LIST) && (br2->type == YN_CONTAINER)) ||
                ((br2->type == YN_LIST) && (br1->type == YN_CONTAINER)))) {
        return 0;
    }

    if (br2->type == YN_LIST) {
        ay_ynode_copy_data(br1, br2);
        br1->id = br2->id;
        br1->min_elems = br1->min_elems ? 1 : 0;
    } else {
        br1->min_elems = br1->min_elems ? 1 : 0;
    }

    return 1;
}

/**
 * @brief Merge branches to one if the leading nodes have the same label.
 *
 * For example:
 *
 * Lonely keys.
 * [key lns1 store lns2] | [key lns1]   -> [key lns1 store lns2]
 *
 * Same nodes with but one has the star.
 * [key lns1 . lns2 ]* | [key lns1 . lns2 ] -> list's min-elements = 1
 *
 * Same roots but different inner nodes.
 * [key lns1 . lns2 ]* | [key lns1 . lns3 ]*   -> [key lns1 (lns2 | lns3)]*
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in,out] subtree Current subtree whose immediate children will be processed.
 * @param[out] merged Flag is set if any merge operation is applied.
 * @return 0 on success.
 */
static int
ay_ynode_merge_cases_r(struct ay_ynode *tree, struct ay_ynode *subtree, ly_bool *merged)
{
    int ret;
    struct ay_ynode *child, *chn1, *chn2;
    ly_bool match;
    int err;

    if (!subtree->child) {
        return 0;
    }

    for (child = subtree->child; child; child = child->next) {
        ret = ay_ynode_merge_cases_r(tree, child, merged);
        AY_CHECK_RET(ret);
    }

    /* Iterate over siblings. */
    chn1 = subtree->child;
    while (chn1) {
        if (!chn1->choice) {
            /* Just continue with the next one. */
            chn1 = chn1->next;
            continue;
        }
        match = 0;
        for (chn2 = chn1->next; chn2 && (chn2->choice == chn1->choice); chn2 = chn2->next) {
            match = ay_ynode_cmp_choice_branches(chn1, chn2);
            if (!match) {
                continue;
            }
            if (ay_ynode_merge_cases_only_by_repetition(chn1, chn2)) {
                ay_ynode_delete_subtree(tree, chn2);
            } else if (ay_ynode_merge_cases_only_by_value(tree, chn1, chn2, &err)) {
                AY_CHECK_RET(err);
                ay_ynode_delete_subtree(tree, chn2);
            } else {
                ret = ay_ynode_merge_cases_(tree, chn1, chn2);
                AY_CHECK_RET(ret);
            }
            break;
        }
        if (match) {
            if (ay_ynode_alone_in_choice(chn1)) {
                if (chn1->type == YN_CASE) {
                    /* YN_CASE is useless. */
                    ay_ynode_delete_node(tree, chn1);
                } else {
                    chn1->choice = NULL;
                }
                if (chn1->when_ref) {
                    /* The when reference is meaningless if the node is not in choice. */
                    chn1->when_ref = 0;
                    chn1->when_val = NULL;
                }
            }
            *merged = 1;
            /* Repeat comparing because chn1 can be modified. */
            continue;
        }
        /* Current chn1 is processed. Continue with the next one. */
        chn1 = chn1->next;
    }

    return 0;
}

/**
 * @brief Merge branches to one if the leading nodes have the same label.
 *
 * Function is applied as many times as a merge can be performed.
 *
 * @param[in,out] tree Tree of ynodes.
 * @return 0 on success.
 */
static int
ay_ynode_merge_cases(struct ay_ynode *tree)
{
    int ret;
    ly_bool merged;

    do {
        merged = 0;
        ret = ay_ynode_merge_cases_r(tree, tree, &merged);
        AY_CHECK_RET(ret);
    } while (merged);

    return 0;
}

/**
 * @brief Delete choice branch if is not unique.
 *
 * @param[in,out] tree Tree of ynodes.
 */
static void
ay_ynode_delete_equal_cases(struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i;
    struct ay_ynode *chnode, *br1, *br2;

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        chnode = &tree[i];
        if (!chnode->choice) {
            continue;
        }
        for (br1 = chnode; br1 && (br1->choice == chnode->choice); br1 = br1->next) {
            for (br2 = br1->next; br2 && (br2->choice == chnode->choice); br2 = br2->next) {
                if (!ay_ynode_subtree_equal(br1, br2, 1, 0)) {
                    continue;
                }
                ay_ynode_delete_subtree(tree, br2);
                if (ay_ynode_alone_in_choice(br1) && (br1->type == YN_CASE)) {
                    /* YN_CASE is useless. */
                    ay_ynode_delete_node(tree, br1);
                }
            }
        }
    }

}

/**
 * @brief Delete useless choice-stmt containing the same branches and 'when' statements with all possible references.
 *
 * @param[in,out] tree Tree of ynodes
 * @return 0 on success.
 */
static int
ay_ynode_delete_useless_choice(struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i, j, k;
    struct ay_ynode *target, *iter, *chnode, *branch;
    struct ay_dnode *values, *key;
    ly_bool delete_branches, match;
    uint64_t union_vals, total_branches;

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        target = &tree[i];
        /* Find target of when-stmt. */
        if (!(target->flags & AY_WHEN_TARGET)) {
            continue;
        }

        /* Get all possible values for target. */
        values = AY_YNODE_ROOT_VALUES(tree);
        key = ay_dnode_find(values, target->value);
        if (!key) {
            continue;
        }
        assert(AY_DNODE_IS_KEY(key));

        /* Count the total number of values. */
        union_vals = 0;
        AY_DNODE_KEYVAL_FOR(key, k) {
            ++union_vals;
        }
        assert(union_vals > 1);

        for (j = 0; j < target->parent->descendants; j++) {
            iter = &target[j + 1];
            /* Find corresponding nodes with when-references. */
            if ((iter->when_ref != target->id) || !iter->choice) {
                continue;
            }

            /* Node must be in choice-stmt. */
            chnode = ay_ynode_get_first_in_choice(iter->parent, iter->choice);
            if ((chnode != iter) || !chnode->next || (chnode->next->choice != chnode->choice)) {
                continue;
            }

            /* Chech choice-stmt. */
            delete_branches = 1;
            total_branches = 0;
            for (branch = chnode; branch && (branch->choice == chnode->choice); branch = branch->next) {
                ++total_branches;
                /* The 'when' reference must be correct and all branches are the same except fot the when-references. */
                if ((branch->when_ref != target->id) || !branch->when_val ||
                        !ay_ynode_subtree_equal(chnode, branch, 1, 1)) {
                    delete_branches = 0;
                    break;
                }

                /* Making sure that 'when' is actually referring to some value. */
                match = 0;
                AY_DNODE_KEYVAL_FOR(key, k) {
                    if (key[k].lval == branch->when_val) {
                        match = 1;
                        break;
                    }
                }
                if (!match) {
                    delete_branches = 0;
                    break;
                }
            }
            /* Check if all when-values was referenced in branches. */
            if (!delete_branches || (union_vals != total_branches)) {
                continue;
            }
            /* TODO What if the when-value is repeated and one of the values is therefore omitted? */

            /* Delete choice-stmt and keep one branch/subtree without when-stmt. */
            for (k = 1; k < total_branches; k++) {
                ay_ynode_delete_subtree(tree, chnode->next);
            }
            chnode->when_ref = 0;
            chnode->when_val = NULL;
        }
    }

    return 0;
}

/**
 * @brief Insert a 'when' statement if the node's existence depends on the YN_VALUE node.
 *
 * @param[in] vnode Node ynode of type YN_VALUE.
 * @param[in] key_value Main key value from AY_YNODE_ROOT_VALUES() dictionary.
 * @param[in] uni First L_UNION above vnode->value.
 * @param[in,out] iter Iterator over @p vnode siblings. Children of YN_CASE node are also considered siblings.
 * @param[in,out] sum Total sum of 'when' settings.
 */
static void
ay_ynode_dependence_on_value_set_when(struct ay_ynode *vnode, struct ay_dnode *key_value, const struct ay_lnode *uni,
        uint32_t *sum, struct ay_ynode *iter)
{
    uint64_t i;
    struct ay_ynode *child;
    const struct ay_lnode *con1, *con2, *when_val;
    ly_bool match;

    if (!iter) {
        return;
    } else if (iter->type == YN_CASE) {
        /* Nodes without choice are skipped because 'when' is not currently written to them. It's probably not that
         * important to consistently write 'when' to all nodes, because it can't be fully relied on yet. So it must be
         * taken more as a hint about how the nodes are interdependent. Finally, it can actually be derived and put
         * together the 'when' conditions using the 'or' operator, but that might be added sometime in the future.
         */
        for (child = iter->child; child && !child->choice; child = child->next) {}

        /* Recursive call for YN_CASE children. */
        ay_ynode_dependence_on_value_set_when(vnode, key_value, uni, sum, child);
        /* Continue to YN_CASE sibling. */
        goto next_sibling;
    }

    /* Get last concatenation below L_UNION. */
    if (iter->type == YN_LIST) {
        con1 = ay_lnode_get_last_concat(iter->child->snode, uni);
    } else {
        con1 = ay_lnode_get_last_concat(iter->snode, uni);
    }
    if (!con1) {
        goto next_sibling;
    }

    /* Check if 'iter' is concatenated with YN_VALUE node. */
    match = 0;
    AY_DNODE_KEYVAL_FOR(key_value, i) {
        when_val = key_value[i].lval;
        con2 = ay_lnode_get_last_concat(when_val, uni);
        if (con1 == con2) {
            match = 1;
            break;
        }
    }
    if (!match) {
        /* Not in concatenation. */
        goto next_sibling;
    }
    assert(!iter->when_ref);

    /* Success, the 'when' will be set. */
    iter->when_ref = vnode->id;
    iter->when_val = when_val;
    vnode->flags |= AY_WHEN_TARGET;
    ++(*sum);

next_sibling:
    /* Move to the next sibling. */
    ay_ynode_dependence_on_value_set_when(vnode, key_value, uni, sum, iter->next);
}

/**
 * @brief Insert a 'when' statement if the node's existence depends on the YN_VALUE node.
 *
 * For example lense:
 * [ label "user" . (store user_re | store Rx.word . Sep.space . [label "host"]) ]
 * The 'host' node can only exist if the node's YN_VALUE value is 'Rx.word'.
 *
 * @param[in] tree Tree of nodes.
 * @return 0 on success.
 */
static int
ay_ynode_dependence_on_value(struct ay_ynode *tree)
{
    struct ay_ynode *iter, *vnode, *chnode;
    const struct ay_lnode *val_union;
    struct ay_dnode *values, *key;
    uint64_t i;
    uint32_t sum;

    if (!AY_YNODE_ROOT_VALUES(tree) || !tree->descendants) {
        return 0;
    }

    /* Algorithm deals only with values in union. */
    values = AY_YNODE_ROOT_VALUES(tree);
    if (LY_ARRAY_COUNT(values) == 0) {
        return 0;
    }

    for (i = 1; i < tree->descendants; i++) {
        vnode = &tree[i];
        if (vnode->type != YN_VALUE) {
            continue;
        }

        key = ay_dnode_find(values, vnode->value);
        if (!key) {
            /* YN_VALUE node with no YANG union-stmt. */
            continue;
        }
        assert(AY_DNODE_IS_KEY(key));
        val_union = ay_lnode_has_attribute(vnode->value, L_UNION);
        if (!val_union) {
            /* No choice-stmt. */
            continue;
        }

        /* Skip nodes without choice. */
        for (iter = vnode->next; iter && !iter->choice; iter = iter->next) {}
        if (!iter) {
            continue;
        }

        sum = 0;
        chnode = iter;
        ay_ynode_dependence_on_value_set_when(vnode, key, val_union, &sum, iter);

        /* If when-stmts does not cover all values, the choice must be without mandatory. */
        assert(sum <= (key->values_count + 1));
        if (sum != (key->values_count + 1)) {
            chnode->flags |= AY_CHOICE_MAND_FALSE;
        }
    }

    return 0;
}

/**
 * @brief Copy nodes from other branches next to leafref node.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in] branch Branch in which the leafref is located.
 * @param[in] listord List which will contain the copied nodes and also contains the leafref.
 */
static void
ay_ynode_recursive_form_by_copy_(struct ay_ynode *tree, struct ay_ynode *branch, struct ay_ynode *listord)
{
    struct ay_ynode *iter, *iter2;
    uint64_t desc;

    /* Copy siblings before branch into listord. */
    for (iter = ay_ynode_get_first_in_choice(branch->parent, branch->choice);
            iter && (iter->choice == branch->choice) && (iter != branch);
            iter = iter->next) {
        if (ay_ynode_subtree_contains_rec(iter, 1)) {
            continue;
        }
        ay_ynode_copy_subtree_as_last_child(tree, listord, iter);
    }

    /* Copy siblings after branch into listord. */
    for (iter = branch->next;
            iter && (iter->choice == branch->choice);
            iter = iter->next) {
        if (ay_ynode_subtree_contains_rec(iter, 1)) {
            continue;
        }
        desc = iter->descendants;
        ay_ynode_copy_subtree_as_last_child(tree, listord, iter);
        iter += desc + 1;
    }

    /* Set some choice id. */
    for (iter = listord->child; iter; iter = iter->next) {
        iter->choice = AY_YNODE_ROOT_LTREE(tree);
        iter->flags |= AY_CHOICE_CREATED;
    }

    /* Remove duplicit YN_LIST node. */
    for (iter = listord->child; iter; iter = iter->next) {
        if ((iter->type == YN_LIST) && iter->choice) {
            for (iter2 = iter->child; iter2; iter2 = iter2->next) {
                iter2->choice = iter->choice;
            }
            ay_ynode_delete_node(tree, iter);
        } else if (iter->type == YN_LIST) {
            ay_ynode_delete_node(tree, iter);
        }
    }
}

/**
 * @brief Under certain conditions copy nodes from other branches next to leafref node.
 *
 * If the nodes are not copied, the AY_GROUPING_CHOICE flag is set.
 *
 * @param[in,out] tree Tree of ynodes.
 */
static int
ay_ynode_recursive_form_by_copy(struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i;
    struct ay_ynode *lf, *iter, *branch, *listord, *first_branch;
    ly_bool copy_nodes;

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        lf = &tree[i];
        if (lf->type != YN_LEAFREF) {
            continue;
        }
        branch = ay_ynode_leafref_branch(lf);
        if (!branch->choice) {
            continue;
        }
        listord = lf->parent;
        first_branch = ay_ynode_get_first_in_choice(branch->parent, branch->choice);

        /* Decide if nodes should be copied. */
        copy_nodes = 1;
        for (iter = first_branch; iter && (iter->choice == branch->choice); iter = iter->next) {
            if (ay_ynode_subtree_contains_rec(iter, 1)) {
                continue;
            } else if ((iter->type == YN_LIST) || iter->when_ref || !ay_ynode_when_paths_are_valid(iter, 1)) {
                copy_nodes = 1;
                break;
            }
            copy_nodes = 0;
        }

        if (copy_nodes) {
            /* Copy nodes. */
            ay_ynode_recursive_form_by_copy_(tree, branch, listord);
        } else {
            /* Grouping is more suitable. */
            first_branch->flags |= AY_GROUPING_CHOICE;
        }
    }

    return 0;
}

/**
 * @brief The leafref path must not go outside groupings.
 *
 * @param[in] subtree Subtree to check.
 * @return 1 if grouping must not be applied.
 */
static ly_bool
ay_ynode_set_ref_leafref_restriction(struct ay_ynode *subtree)
{
    LY_ARRAY_COUNT_TYPE i;
    struct ay_ynode *iti, *lrec_external;
    struct lens *exter;

    lrec_external = NULL;
    for (iti = subtree->parent; iti && !lrec_external; iti = iti->parent) {
        if (iti->type == YN_REC) {
            lrec_external = iti;
        }
    }

    if (!lrec_external) {
        return 0;
    }
    exter = AY_SNODE_LENS(lrec_external);
    assert(exter);

    /* Skip if subtree contains leafref because it can break recursive form. */
    for (i = 0; i < subtree->descendants; i++) {
        iti = &subtree[i + 1];
        if ((iti->type == YN_LEAFREF) && (exter->body == iti->snode->lens->body)) {
            return 1;
        }
    }

    return 0;
}

/**
 * @brief Check if choice groups can be considered the same.
 *
 * @param[in] ch1 First choice group.
 * @param[in] ch2 Second choice group.
 * @param[in] ignore_recursive_branch Flag causes branches that contain a leafref to be ignored when comparing.
 * @return 1 if choice groups are equal.
 */
static ly_bool
ay_ynode_choice_group_equal(struct ay_ynode *ch1, struct ay_ynode *ch2, ly_bool ignore_recursive_branch)
{
    ly_bool lf1_check, lf2_check;
    struct ay_ynode *it1, *it2;

    lf1_check = ignore_recursive_branch && (ch1->flags & AY_GROUPING_CHOICE);
    lf2_check = ignore_recursive_branch && (ch2->flags & AY_GROUPING_CHOICE);
    for (it1 = ch1, it2 = ch2;
            it1 && it2 && (it1->choice == ch1->choice) && (it2->choice == ch2->choice);
            it1 = it1->next, it2 = it2->next) {

        /* Ignore branches with leafref. */
        while (lf1_check && ay_ynode_subtree_contains_rec(it1, 1)) {
            it1 = it1->next;
        }
        while (lf2_check && ay_ynode_subtree_contains_rec(it2, 1)) {
            it2 = it2->next;
        }

        /* Terminating condition of the for cycle. */
        if (!(it1 && it2 && (it1->choice == ch1->choice) && (it2->choice == ch2->choice))) {
            break;
        }

        /* Compare branches. */
        if (!ay_ynode_subtree_equal(it1, it2, 1, 0)) {
            return 0;
        }
    }

    if ((!it1 && !it2) ||
            (it1 && it2 && (it1->choice != ch1->choice) && (it2->choice != ch2->choice))) {
        return 1;
    } else {
        return 0;
    }
}

/**
 * @brief Set grouping reference in ay_ynode.ref.
 *
 * Groupings are created in the ay_ynode_create_groupings_recursive_form().
 *
 * @param[in,out] tree Tree of ynodes.
 */
static void
ay_ynode_set_ref_recursive_form(struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i, j;
    struct ay_ynode *grch, *iter, *chnode;

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        grch = &tree[i];
        if (!(grch->flags & AY_GROUPING_CHOICE)) {
            continue;
        }
        /* Tag grouping. */
        grch->ref = grch->id;

        /* Skip choice nodes. */
        for (chnode = grch; chnode->next && (chnode->next->choice == grch->choice); chnode = chnode->next) {}

        /* Find such a choice group elsewhere. */
        for (j = AY_INDEX(tree, chnode) + chnode->descendants + 1; j < LY_ARRAY_COUNT(tree); j++) {
            iter = &tree[j];
            if (!iter->choice) {
                continue;
            }
            if (ay_ynode_choice_group_equal(grch, iter, 1)) {
                /* YN_USED node will be replaced here. */
                iter->ref = grch->id;
            }
            j += iter->descendants;
        }
    }
}

/**
 * @brief Unset ay_lnode.pnode pointer if it is evaluated that it must not be used.
 *
 * During a grouping search, subtree nodes can be the same, but their names can be different.
 * And it would be confusing if a name were used in an unrelated place.
 *
 * @param[in] subt Subtree which can contain YN_GROUPING nodes.
 * @param[in] del_subt Subtree which will be deleted.
 * @param[in] compare_roots Flag set to 1 if roots of subtrees must also be compared.
 */
static void
ay_ynode_snode_unset_pnode(struct ay_ynode *subt, struct ay_ynode *del_subt, ly_bool compare_roots)
{
    LY_ARRAY_COUNT_TYPE i, j, stop;
    struct ay_ynode *iti, *itj;
    struct ay_lnode *snode;

    if (!compare_roots) {
        /* Skip root nodes. */
        stop = subt->descendants;
        subt++;
        del_subt = ay_ynode_inner_nodes(del_subt);
    } else {
        stop = subt->descendants + 1;
    }
    for (i = 0, j = 0; i < stop; i++, j++) {
        iti = &subt[i];
        itj = &del_subt[j];
        if (iti->type == YN_GROUPING) {
            /* Skip grouping node. */
            j--;
            continue;
        } else if (!iti->snode || !iti->snode->pnode) {
            continue;
        }
        assert(iti->type == itj->type);

        if (iti->snode->pnode != itj->snode->pnode) {
            /* This pnode should not be used. */
            snode = (struct ay_lnode *)iti->snode;
            snode->pnode = NULL;
        }
    }
}

/**
 * @brief Nodes that belong to the same grouping are marked by ay_ynode.ref.
 *
 * This function is preparation before calling ::ay_ynode_create_groupings_toplevel().
 *
 * @param[in,out] tree Tree of ynodes.
 */
static void
ay_ynode_set_ref(struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i, j, start;
    struct ay_ynode *iti, *itj, *inner_nodes;
    ly_bool subtree_eq, alone, splittable;
    uint64_t children_eq;

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        /* Get default subtree. */
        iti = &tree[i];
        if (iti->ref && (iti->parent->type != YN_REC)) {
            i += iti->descendants;
            continue;
        } else if (((iti->type == YN_LIST) && (iti->parent->type == YN_ROOT)) ||
                ((iti->type != YN_CONTAINER) && (iti->type != YN_LIST)) ||
                ay_ynode_set_ref_leafref_restriction(iti) || iti->when_ref || !ay_ynode_when_paths_are_valid(iti, 1)) {
            continue;
        }

        /* Find subtrees which are the same. */
        subtree_eq = 0;
        children_eq = 0;
        splittable = 0;
        alone = ay_ynode_inner_node_alone(iti);
        inner_nodes = ay_ynode_inner_nodes(iti);
        start = i + iti->descendants + 1;
        for (j = start; j < LY_ARRAY_COUNT(tree); j++) {
            itj = &tree[j];
            if (itj->ref) {
                /* Grouping has already been evaluated. */
                j += itj->descendants;
                continue;
            } else if (itj->when_ref || !ay_ynode_when_paths_are_valid(itj, 1)) {
                continue;
            }

            if (((itj->type == YN_LIST) && ay_ynode_subtree_equal(iti, itj, 1, 1)) ||
                    ((itj->type == YN_CONTAINER) &&
                    ((alone && ay_ynode_inner_node_alone(itj)) || !ay_ynode_inner_nodes(itj)) &&
                    ay_ynode_subtree_equal(iti, itj, 1, 1))) {
                subtree_eq = 1;
                itj->ref = iti->id;
                j += itj->descendants;
            } else if (inner_nodes && ay_ynode_subtree_equal(iti, itj, 0, 1)) {
                /* Subtrees without root node are equal. */
                splittable = splittable || ay_ynode_rule_node_is_splittable(tree, itj) ? 1 : 0;
                if (!splittable && !inner_nodes->next) {
                    /* If its inner_node has no siblings, no grouping is applied, because it is too 'poor'.
                     * In other words, grouping is not applied because it is not worth it. But if any of the matched
                     * subtrees was splittable, then it can be 'poor', because the subtree repeats too often. */
                    continue;
                }
                children_eq++;
                itj->ref = iti->id;
                itj->flags |= AY_GROUPING_CHILDREN;
                j += itj->descendants;
            }
        }

        /* Setting 'iti->ref' and flag AY_GROUPING_CHILDREN. If grouping is to be applied to the entire subtree
         * including the root, there must not be a subtree that has a different root.*/
        if ((subtree_eq && children_eq) || (!subtree_eq && children_eq)) {
            iti->ref = iti->id;
            iti->flags |= AY_GROUPING_CHILDREN;
        } else if (subtree_eq) {
            iti->ref = iti->id;
        }
    }
}

/**
 * @brief Insert YN_USES and YN_GROUPING nodes.
 *
 * The function assumes that the ::ay_ynode_set_ref() function was called before it.
 *
 * @param[in] tree Tree of ynodes to process.
 * @return 0 on success.
 */
static int
ay_ynode_create_groupings_toplevel(struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i, j;
    struct ay_ynode *iti, *itj, *grouping, *uses, *inner_nodes, *start;
    uint16_t choice_mand_false;
    ly_bool gr_used;

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        iti = &tree[i];
        if (!iti->ref || (iti->type == YN_USES) || (iti->type == YN_LEAFREF)) {
            continue;
        } else if (iti->type == YN_GROUPING) {
            i += iti->descendants;
            continue;
        }
        assert(iti->id == iti->ref);

        gr_used = 0;
        if (iti->flags & AY_GROUPING_CHILDREN) {
            /* Insert YN_GROUPING. */
            assert(iti->child);
            inner_nodes = ay_ynode_inner_nodes(iti);
            grouping = inner_nodes ? inner_nodes : iti->child;
            ay_ynode_insert_parent_for_rest(tree, grouping);
            grouping->snode = iti->snode;
        } else if ((iti->parent->type == YN_GROUPING) &&
                (iti->parent->parent->flags & AY_GROUPING_CHILDREN) &&
                ((inner_nodes = ay_ynode_inner_nodes(iti->parent))) &&
                (inner_nodes == iti) &&
                !inner_nodes->next) {
            /* Use already defined grouping. */
            gr_used = 1;
            grouping = iti->parent;
        } else {
            /* Insert YN_GROUPING. */
            ay_ynode_insert_wrapper(tree, iti);
            grouping = iti;
            iti++;
            grouping->snode = grouping->parent->snode;
        }
        grouping->type = YN_GROUPING;
        choice_mand_false = grouping->child->flags & AY_CHOICE_MAND_FALSE;

        /* Find, remove duplicate subtree and create YN_USES. */
        start = grouping + grouping->descendants + 1;
        for (j = AY_INDEX(tree, start); j < LY_ARRAY_COUNT(tree); j++) {
            itj = &tree[j];
            if ((itj->ref != iti->ref) || (itj->type == YN_USES)) {
                continue;
            }

            /* Create YN_USES at 'itj' node. */
            if (itj->flags & AY_GROUPING_CHILDREN) {
                /* YN_USES for children. */
                ay_ynode_snode_unset_pnode(grouping, itj, 0);
                ay_ynode_delete_children(tree, itj, 1);
                uses = ay_ynode_insert_child_last(tree, itj);
            } else {
                /* YN_USES for subtree (itj node). */
                ay_ynode_snode_unset_pnode(grouping, itj, 1);
                ay_ynode_delete_children(tree, itj, 0);
                uses = itj;
                uses->snode = uses->label = uses->value = NULL;
                uses->flags = 0;
            }
            /* itj node is processed */
            itj->ref = 0;
            /* Set YN_USES. */
            uses->type = YN_USES;
            uses->ref = grouping->id;
            uses->flags |= choice_mand_false;
        }
        /* iti node is processed */
        iti->ref = 0;

        if (!gr_used) {
            /* Insert YN_USES at 'iti' node. */
            ay_ynode_insert_sibling(tree, grouping);
            uses = grouping->next;
            uses->type = YN_USES;
            uses->ref = grouping->id;
            uses->choice = grouping == iti->parent ? iti->choice : grouping->child->choice;
            uses->flags |= choice_mand_false;

            grouping->child->choice = !grouping->child->next ? NULL : grouping->child->choice;
        }
    }

    return 0;
}

/**
 * @brief Insert YN_USES node next to leafref.
 *
 * For all leafref nodes.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in] first_branch First choice-branch under external YN_REC node.
 * @param[in] grouping_id Identifier for referencing in YN_USES.
 *
 */
static void
ay_ynode_leafref_insert_uses(struct ay_ynode *tree, struct ay_ynode *first_branch, uint32_t grouping_id)
{
    LY_ARRAY_COUNT_TYPE i;
    struct ay_ynode *branch, *lf, *uses;

    for (branch = first_branch; branch && (branch->choice == first_branch->choice); branch = branch->next) {
        for (i = 0; i < branch->descendants; i++) {
            lf = &branch[i + 1];
            if (lf->type != YN_LEAFREF) {
                continue;
            }
            ay_ynode_insert_sibling(tree, lf);
            uses = lf->next;
            uses->type = YN_USES;
            uses->ref = grouping_id;
            uses->choice = lf->choice;
        }
    }
}

/**
 * @brief Count number of nodes which will be in grouping, but branches with leafref are not counted.
 *
 * @param[in] grch First choice node which will be searched.
 * @return Number of nodes in choice.
 */
static uint32_t
ay_ynode_grouping_choice_count(struct ay_ynode *grch)
{
    struct ay_ynode *iter;
    uint32_t cnt;

    cnt = 0;
    for (iter = grch; iter && (iter->choice == grch->choice); iter = iter->next) {
        if (ay_ynode_subtree_contains_rec(iter, 1)) {
            continue;
        }
        cnt++;
    }

    return cnt;
}

/**
 * @brief Create Groupings and Uses nodes for the recursive form.
 *
 * Grouping is applied to the choice under the recursive node YN_REC.
 *
 * @param[in,out] tree Tree of ynodes.
 * @return 0 on success.
 */
static int
ay_ynode_create_groupings_recursive_form(struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i, j, k;
    struct ay_ynode *grch, *branch, *grouping, *uses, *iter, *prev;
    uint32_t cnt;

    /* TODO The exact leafref/grouping_choice should be searched for. */

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        grch = &tree[i];
        if ((grch->ref != grch->id) || !(grch->flags & AY_GROUPING_CHOICE)) {
            continue;
        }

        /* This grouping-choice is processed. */
        grch->ref = 0;

        /* Insert GROUPING node. */
        if (grch->parent->child == grch) {
            ay_ynode_insert_child(tree, grch->parent);
        } else {
            prev = ay_ynode_get_prev(grch);
            ay_ynode_insert_sibling(tree, prev);
        }
        grouping = grch;
        grch = grouping->next;
        grouping->type = YN_GROUPING;
        grouping->choice = grch->choice;
        grouping->snode = grouping->parent->snode;

        /* Move branches to the GROUPING (except those containing YN_LEAFREF). */
        cnt = ay_ynode_grouping_choice_count(grch);
        for (j = 0; j < cnt; j++) {
            for (branch = grouping->next; ay_ynode_subtree_contains_rec(branch, 1); branch = branch->next) {}
            ay_ynode_move_subtree_as_last_child(tree, grouping, branch);
        }

        /* Add YN_USES next to GROUPING. */
        ay_ynode_leafref_insert_uses(tree, grouping->next, grouping->id);
        ay_ynode_insert_sibling(tree, grouping);
        uses = grouping->next;
        uses->type = YN_USES;
        uses->choice = grouping->choice;
        uses->flags |= grouping->child->flags & AY_CHOICE_MAND_FALSE;
        uses->ref = grouping->id;
        if (grouping->child->flags & AY_CHOICE_MAND_FALSE) {
            uses->flags |= AY_CHOICE_MAND_FALSE;
            grouping->child->flags &= ~AY_CHOICE_MAND_FALSE;
        }

        /* Find other nodes to delete and replace them with YN_USES. */
        for (j = AY_INDEX(tree, uses + 1); j < LY_ARRAY_COUNT(tree); j++) {
            iter = &tree[j];
            if (iter->ref != grch->id) {
                continue;
            }

            /* Insert YN_USES node. */
            if (iter->parent->child == iter) {
                ay_ynode_insert_child(tree, iter->parent);
                uses = iter->child;
            } else {
                prev = ay_ynode_get_prev(iter);
                ay_ynode_insert_sibling(tree, prev);
                uses = prev->next;
            }
            iter++;
            uses->type = YN_USES;
            uses->choice = iter->choice;
            uses->ref = grouping->id;

            /* Delete branches except those containing YN_LEAFREF. */
            for (k = 0; k < cnt; k++) {
                for (branch = uses->next; ay_ynode_subtree_contains_rec(branch, 1); branch = branch->next) {}
                ay_ynode_delete_subtree(tree, branch);
            }

            /* Add YN_USES next to YN_LEAFREF if exists. */
            ay_ynode_leafref_insert_uses(tree, iter, grouping->id);
        }
        i += grouping->descendants;
    }

    return 0;
}

/**
 * @brief Split node if his pattern consists of identifiers in sequence.
 *
 * Example: [key "a" | "b"] -> list a {} list b {}
 *
 * @param[in,out] tree Tree of ynodes.
 * @return 0.
 */
static int
ay_ynode_node_split(struct ay_ynode *tree)
{
    uint64_t idents_count, i, j, grouping_id;
    struct ay_ynode *node, *node_new, *grouping, *inner_nodes, *key, *value;
    ly_bool rec_form, valid_when;

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        node = &tree[i];

        if (!ay_ynode_rule_node_is_splittable(tree, node) || (ay_ynode_splitted_seq_index(node) != 0)) {
            continue;
        }

        assert(node->label);
        idents_count = ay_lense_pattern_idents_count(tree, node->label->lens);
        assert(idents_count > 1);

        /* Just set choice to some value if not already set. */
        if (!node->choice) {
            node->choice = AY_YNODE_ROOT_LTREE(tree);
            node->flags |= AY_CHOICE_CREATED;
        }

        grouping_id = 0;
        inner_nodes = ay_ynode_inner_nodes(node);
        rec_form = ay_ynode_subtree_contains_type(node, YN_LEAFREF) ? 1 : 0;
        valid_when = ay_ynode_when_paths_are_valid(node, 0);
        if (inner_nodes && (inner_nodes->type == YN_USES) && !inner_nodes->next) {
            grouping_id = inner_nodes->ref;
        } else if (inner_nodes && (inner_nodes->type == YN_GROUPING)) {
            grouping_id = inner_nodes->id;
        } else if (inner_nodes && !rec_form && valid_when) {
            /* Create grouping. */
            ay_ynode_insert_parent_for_rest(tree, inner_nodes);
            grouping = inner_nodes;
            grouping->type = YN_GROUPING;
            grouping->snode = grouping->parent->snode;
            grouping_id = grouping->id;
            /* Create YN_USES node. */
            ay_ynode_insert_sibling(tree, grouping);
            grouping->next->type = YN_USES;
            grouping->next->ref = grouping_id;
        }

        key = ay_ynode_parent_has_child(node, YN_KEY);
        value = ay_ynode_parent_has_child(node, YN_VALUE);

        /* Split node. */
        for (j = 0; j < (idents_count - 1); j++) {
            if (rec_form || !valid_when) {
                ay_ynode_copy_subtree_as_sibling(tree, node, node);
            } else {
                /* insert new node */
                ay_ynode_insert_sibling(tree, node);
                node_new = node->next;
                ay_ynode_copy_data(node_new, node);
                if (grouping_id) {
                    /* Insert YN_USES node. */
                    ay_ynode_insert_child(tree, node_new);
                    node_new->child->type = YN_USES;
                    node_new->child->ref = grouping_id;
                }
                if (value) {
                    ay_ynode_insert_child(tree, node_new);
                    ay_ynode_copy_data(node_new->child, value);
                }
                if (key) {
                    ay_ynode_insert_child(tree, node_new);
                    ay_ynode_copy_data(node_new->child, key);
                }
            }
        }
    }

    return 0;
}

/**
 * @brief Change list to containers and add config-entries list.
 *
 * This function adjusts the YANG scheme so that the order of the data is maintained.
 *
 * @param[in,out] tree Tree of ynodes.
 * @return 0.
 */
static int
ay_ynode_ordered_entries(struct ay_ynode *tree)
{
    uint64_t i, j, nodes_cnt;
    struct ay_ynode *parent, *child, *list, *iter;
    const struct ay_lnode *choice, *star;

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        parent = &tree[i];

        if (AY_YNODE_IS_IMPLICIT_LIST(parent)) {
            continue;
        }

        for (iter = parent->child; iter; iter = iter->next) {
            if (AY_YNODE_IS_SEQ_LIST(iter) || AY_YNODE_IS_IMPLICIT_LIST(iter) ||
                    ((iter->type != YN_LIST) && (iter->type != YN_REC)) ||
                    ((iter->type == YN_REC) && (parent->type == YN_LIST) && (parent->parent->type != YN_ROOT))) {
                continue;
            }

            star = ay_ynode_get_repetition(iter);
            if (!star) {
                continue;
            }

            choice = iter->choice;

            /* every next LIST or YN_REC move to wrapper */
            nodes_cnt = 0;
            for (list = iter->next; list; list = list->next) {
                if ((choice == list->choice) &&
                        ((list->type == YN_LIST) || (list->type == YN_REC)) &&
                        (iter->min_elems == list->min_elems) &&
                        (star == ay_ynode_get_repetition(list))) {
                    assert(!list->when_ref && !list->when_val);
                    nodes_cnt++;
                } else {
                    break;
                }
            }

            /* wrapper is list to maintain the order of the augeas data */
            ay_ynode_insert_wrapper(tree, iter);
            list = iter;
            list->type = YN_LIST;
            list->min_elems = list->child->min_elems;
            list->choice = choice;
            list->flags |= list->child->flags & (AY_CHOICE_MAND_FALSE | AY_CHOICE_CREATED | AY_HINT_MAND_FALSE);
            list->child->flags &= ~AY_CHOICE_MAND_FALSE;
            list->child->flags &= ~AY_HINT_MAND_FALSE;
            /* Move 'when' data to list. */
            list->when_ref = list->child->when_ref;
            list->when_val = list->child->when_val;
            list->child->when_ref = 0;
            list->child->when_val = NULL;

            for (j = 0; j < nodes_cnt; j++) {
                ay_ynode_move_subtree_as_last_child(tree, list, list->next);
            }

            for (child = list->child; child; child = child->next) {
                if (AY_YNODE_IS_IMPLICIT_LIST(child)) {
                    /* Implicit list is not needed because nodes are now in list. */
                    ay_ynode_delete_node(tree, child);
                } else if ((child->type != YN_REC) && (child->type != YN_CASE)) {
                    /* for every child in wrapper set type to container */
                    child->type = YN_CONTAINER;
                }
            }

            /* set list label to repetition because an identifier may be available */
            list->label = star;
        }
    }

    return 0;
}

/**
 * @brief Insert implicit list roughly at the position where the second L_STAR is located.
 *
 * Such a list is added in case there are two L_STAR nodes above each other.
 * In other words, two L_STAR belong to an ynode, so an additional implicit
 * list containing the top star must be inserted.
 *
 * @param[in,out] tree Tree tree of ynodes.
 * @return 0 on success.
 */
static int
ay_ynode_insert_implicit_list(struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i;
    uint64_t nodes_cnt, j;
    const struct ay_lnode *star, *star2;
    struct ay_ynode *ynode, *iter, *list, *first;

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        ynode = &tree[i];
        if (AY_YNODE_IS_IMPLICIT_LIST(ynode->parent)) {
            continue;
        }

        /* Find the first star. */
        star = ay_lnode_has_attribute(ynode->snode, L_STAR);
        if (!star) {
            continue;
        }
        /* Find the second star. */
        star2 = ay_lnode_has_attribute(star, L_STAR);
        if (!star2) {
            continue;
        }

        /* Count the nodes that will be in the implicit list. */
        star = NULL;
        first = NULL;
        nodes_cnt = 0;
        for (iter = ynode->parent->child; iter; iter = iter->next) {
            for (star = ay_lnode_has_attribute(iter->snode, L_STAR);
                    star && (star != star2);
                    star = ay_lnode_has_attribute(star, L_STAR)) {}
            if (!star) {
                break;
            }
            first = !first ? iter : first;
            nodes_cnt++;
        }
        if (!nodes_cnt) {
            continue;
        }

        /* Insert implicit list. */
        ay_ynode_insert_wrapper(tree, first);
        list = first;
        list->type = YN_LIST;
        list->label = list->snode = star2;

        for (j = 0; j < nodes_cnt; j++) {
            ay_ynode_move_subtree_as_last_child(tree, list, list->next);
        }
        list->choice = list->child->choice;

        i++;
    }

    return 0;
}

/**
 * @brief Get next ynode of type YN_REC.
 *
 * @param[in] lrec_ext Search lrec_internal by this lrec_external.
 * @param[in] lrec_int_iter Iterator to previous lrec_internal. It can be NULL.
 * @return Pointer to lrec_internal or NULL.
 */
static struct ay_ynode *
ay_ynode_lrec_internal(struct ay_ynode *lrec_ext, const struct ay_ynode *lrec_int_iter)
{
    uint64_t i, start;
    const struct lens *snode;
    struct ay_ynode *ret = NULL, *iter;

    assert(lrec_ext && (lrec_ext->type == YN_REC) && lrec_ext->snode && (lrec_ext->snode->lens->tag == L_REC));

    start = lrec_int_iter ? lrec_int_iter - lrec_ext : 0;
    for (i = start; i < lrec_ext->descendants; i++) {
        iter = &lrec_ext[i + 1];
        if (iter->type != YN_REC) {
            continue;
        }
        snode = AY_SNODE_LENS(iter);
        if (snode->rec_internal && (snode->body == lrec_ext->snode->lens->body)) {
            ret = iter;
            break;
        }
    }

    return ret;
}

/**
 * @brief Insert list to maintain the order of the records and recursive reference.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in] branch Branch is ynode whose parent is lrec_external and one of his descendants is @p lrec_internal.
 * @param[in] lrec_internal Inner recursive reference.
 */
static void
ay_ynode_lrec_insert_listord(struct ay_ynode *tree, struct ay_ynode *branch, struct ay_ynode **lrec_internal)
{
    struct ay_ynode *listord, *iter;

    if ((*lrec_internal)->parent->type != YN_LIST) {
        ay_ynode_insert_parent(tree, *lrec_internal);
        (*lrec_internal)++;
        listord = (*lrec_internal)->parent;
        listord->type = YN_LIST;
    } else {
        listord = (*lrec_internal)->parent;
    }

    if (!branch->choice) {
        return;
    }

    /* Set some choice id. */
    for (iter = listord->child; iter; iter = iter->next) {
        iter->choice = AY_YNODE_ROOT_LTREE(tree);
        iter->flags |= AY_CHOICE_CREATED;
    }

    return;
}

/**
 * @brief Create recursive form for every ynode which contains L_REC lense.
 *
 * @param[in,out] tree Tree of ynodes.
 * @return 0 on success.
 */
static int
ay_ynode_recursive_form(struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i;
    struct ay_ynode *lrec_external, *lrec_internal, *iter, *branch, *prev_branch, *listrec;

    for (i = 0; i < tree->descendants; i++) {
        lrec_external = &tree[i + 1];
        if (lrec_external->type != YN_REC) {
            continue;
        }
        listrec = NULL;
        if (lrec_external->label || lrec_external->value) {
            assert((lrec_external->parent->label == lrec_external->label) &&
                    (lrec_external->parent->value == lrec_external->value));
            /* [ let rec lns = label . value ] -> let rec lns = [ label . value ]
             * A node of type YN_REC should not have the attributes (label, value) of a SUBTREE node.
             * Complications are avoided thanks to the swap. */
            ay_ynode_swap(lrec_external, lrec_external->parent);
            lrec_external = lrec_external->parent;
        }
        prev_branch = NULL;
        lrec_internal = ay_ynode_lrec_internal(lrec_external, NULL);
        do {
            /* Change lrec_internal to leafref. */
            lrec_internal->type = YN_LEAFREF;

            /* Get branch where is lrec_internal. */
            for (iter = lrec_internal; iter && (iter->parent != lrec_external); iter = iter->parent) {}
            assert(iter);
            branch = iter;
            ay_ynode_lrec_insert_listord(tree, iter, &lrec_internal);

            if (!listrec && (branch->type == YN_LIST)) {
                /* Some list is present. */
                listrec = branch;
                listrec->snode = lrec_external->snode;
                lrec_internal->ref = listrec->id;
            } else if (!listrec) {
                /* Create listrec. */
                ay_ynode_insert_wrapper(tree, branch);
                lrec_internal++;
                listrec = branch;
                listrec->type = YN_LIST;
                listrec->choice = listrec->child->choice;
                listrec->snode = lrec_external->snode;
                lrec_internal->ref = listrec->id;
            } else if (prev_branch == branch) {
                /* More lrec_internals in one branch. */
                lrec_internal->ref = listrec->id;
            } else {
                /* Move branch into listrec. */
                lrec_internal->ref = listrec->id;
                ay_ynode_move_subtree_as_last_child(tree, listrec, branch);
            }
            prev_branch = branch;
        } while ((lrec_internal = ay_ynode_lrec_internal(lrec_external, lrec_internal)));

        /* Set common choice. */
        for (iter = listrec->child; iter; iter = iter->next) {
            iter->choice = listrec->choice;
        }
    }

    return 0;
}

/**
 * @brief Delete all nodes of type YN_REC.
 *
 * @param[in,out] tree Tree of ynodes.
 * @return 0 on success.
 */
static int
ay_ynode_delete_ynrec(struct ay_ynode *tree)
{
    struct ay_ynode *lrec_ext, *child;
    LY_ARRAY_COUNT_TYPE i;

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        lrec_ext = &tree[i];
        if (lrec_ext->type != YN_REC) {
            continue;
        }

        if (lrec_ext->choice) {
            for (child = lrec_ext->child; child; child = child->next) {
                child->choice = lrec_ext->choice;
            }
        }
        ay_ynode_delete_node(tree, lrec_ext);
        i--;
    }

    return 0;
}

/**
 * @brief Move groupings in front of the config-file list.
 *
 * @param[in,out] tree Tree of ynodes.
 * @return 0 on success.
 */
static int
ay_ynode_groupings_ahead(struct ay_ynode *tree)
{
    int ret = 0;
    LY_ARRAY_COUNT_TYPE i, j, k;
    struct ay_ynode *last, *gr, *us, *iter;
    uint32_t *sort = NULL;
    struct ay_dnode *dict = NULL, *key;
    uint64_t cnt, keys;
    ly_bool inserted, key_resolv, val_resolv;

    /* Find out size of 'dict' and 'sort' */
    cnt = 0;
    keys = 0;
    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        if (tree[i].type == YN_GROUPING) {
            keys++;
        } else if (tree[i].type == YN_USES) {
            cnt++;
        }
    }
    if (!keys) {
        assert(!cnt);
        return ret;
    }

    LY_ARRAY_CREATE_GOTO(NULL, dict, keys * 2 + cnt, ret, cleanup);
    LY_ARRAY_CREATE_GOTO(NULL, sort, keys, ret, cleanup);

    /* Fill 'dict'. Key is YN_GROUPING and values are YN_USES. */
    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        if (tree[i].type == YN_GROUPING) {
            gr = &tree[i];
            inserted = 0;
            for (j = 0; j < gr->descendants; j++) {
                iter = &gr[j + 1];
                if (iter->type == YN_USES) {
                    /* Connect YN_USES to the 'gr'. */
                    inserted = 1;
                    ret = ay_dnode_insert(dict, gr, iter, NULL);
                    AY_CHECK_GOTO(ret, cleanup);
                } else if (iter->type == YN_GROUPING) {
                    /* Skip inner YN_GROUPING. */
                    j += iter->descendants;
                }
            }
            if (!inserted) {
                /* Insert YN_GROUPING which does not have YN_USES nodes. */
                ret = ay_dnode_insert(dict, gr, NULL, NULL);
                AY_CHECK_GOTO(ret, cleanup);
            }
        }
    }

    /* Fill 'sort'. */
    cnt = 0;
    while (cnt < keys) {
        AY_DNODE_KEY_FOR(dict, i) {
            key = &dict[i];
            if (!key->gr) {
                /* This YN_GROUPING is already in 'sort'. */
                continue;
            }
            key_resolv = 1;
            /* Iterate over YN_USES. */
            AY_DNODE_VAL_FOR(key, j) {
                us = key[j].us;
                if (!us) {
                    /* YN_GROUPING does not have YN_USES. */
                    break;
                }
                val_resolv = 0;
                /* YN_USES must point to resolved YN_GROUPING. */
                LY_ARRAY_FOR(sort, k) {
                    if (sort[k] == us->ref) {
                        /* YN_USES refers to resolved YN_GROUPING. */
                        val_resolv = 1;
                        break;
                    }
                }
                if (!val_resolv) {
                    /* YN_GROUPING cannot be resolved yet. */
                    key_resolv = 0;
                    break;
                }
            }
            if (key_resolv) {
                /* YN_GROUPING is resolved. */
                sort[cnt] = key->gr->id;
                cnt++;
                LY_ARRAY_INCREMENT(sort);
                key->gr = NULL;
            }
        }
    }

    /* Move grouping. */
    for (i = 0; i < LY_ARRAY_COUNT(sort); i++) {
        for (j = 1; j < LY_ARRAY_COUNT(tree); j++) {
            if (tree[j].id == sort[i]) {
                ay_ynode_move_subtree_as_last_child(tree, tree, &tree[j]);
                break;
            }
        }
    }

    /* Move main list. */
    assert(tree->child->type == YN_LIST);
    last = ay_ynode_get_last(tree->child);
    if (last->type != YN_LIST) {
        ay_ynode_move_subtree_as_sibling(tree, last, tree->child);
    }

cleanup:
    LY_ARRAY_FREE(dict);
    LY_ARRAY_FREE(sort);

    return ret;
}

/**
 * @brief Detect the need to move a node from grouping and
 * count how many nodes eventually need to be added to the tree.
 *
 * See ay_ynode_grouping_reduction().
 *
 * @param[in,out] tree Tree of ynodes. Only flags can be modified.
 * @return Number of nodes that must be inserted if grouping reduction is applied.
 */
static uint64_t
ay_ynode_grouping_reduction_count(struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i;
    struct ay_ynode *gr, *uses;
    uint64_t new_nodes, dupl_count;

    /* For each top-level grouping, set gr->ref and gr->flags. */
    for (gr = tree->child; gr->type == YN_GROUPING; gr = gr->next) {
        if (gr->child->next) {
            /* Identifier collision can occur only for groupings whose content was created due to subtrees match
             * including root node. If there is only a subtree body in grouping, then collisions have no way
             * of occuring. See ::ay_ynode_set_ref().
             */
            continue;
        }
        assert(!gr->ref);
        gr->ref = 0;
        /* Find YN_USES which refers to this grouping. */
        for (i = AY_INDEX(tree, gr->next) + 1; i < LY_ARRAY_COUNT(tree); i++) {
            uses = &tree[i];
            if ((uses->type != YN_USES) || (uses->ref != gr->id)) {
                continue;
            }
            /* Temporarily store number of YN_USES which references this grouping. */
            gr->ref++;
            if (gr->flags & AY_GROUPING_REDUCTION) {
                /* Flag is already set. Move to next YN_USES. */
                continue;
            }
            /* Explore all siblings of this YN_USES. */
            ay_yang_ident_duplications(tree, uses, gr->child->ident, NULL, &dupl_count);
            if (dupl_count) {
                /* Name collision found. Grouping must be reduced. */
                gr->flags |= AY_GROUPING_REDUCTION;
            }
        }
    }

    /* Calculate how many new nodes must be inserted into the tree due to the reductions in groupings. */
    new_nodes = 0;
    for (gr = tree->child; gr->type == YN_GROUPING; gr = gr->next) {
        if (gr->flags & AY_GROUPING_REDUCTION) {
            new_nodes += gr->ref - 1;
            gr->ref = 0;
        }
    }

    return new_nodes;
}

/**
 * @brief Extract top-level node in grouping and wrap corresponding uses-stmt.
 *
 * It is applied only for groupings which has AY_GROUPING_REDUCTION flag set.
 *
 * @param[in,out] tree Tree of ynodes.
 * @return 0 on success.
 */
static int
ay_ynode_grouping_reduction(struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i;
    struct ay_ynode *gr, *uses, *prev, *new, *parent;
    struct ay_ynode data = {0};
    uint32_t ref;
    ly_bool empty_grouping;

    /* For each top-level grouping. */
    for (gr = tree->child; gr->type == YN_GROUPING; gr = gr->next) {
        if (!(gr->flags & AY_GROUPING_REDUCTION)) {
            continue;
        }
        ay_ynode_copy_data(&data, gr->child);
        /* Delete node from grouping. */
        free(gr->child->ident);
        ay_ynode_delete_node(tree, gr->child);

        if ((gr->descendants == 1) && (gr->child->type == YN_USES)) {
            /* Grouping has only YN_USES node, so corresponding YN_USES node must switch to new grouping. */
            ref = gr->child->ref;
            free(gr->child->ident);
            ay_ynode_delete_node(tree, gr->child);
        } else {
            /* Nothing special. */
            ref = gr->id;
        }

        empty_grouping = gr->descendants == 0;

        /* Find YN_USES which refers to this grouping. */
        for (i = AY_INDEX(tree, gr->next) + 1; i < LY_ARRAY_COUNT(tree); i++) {
            uses = &tree[i];
            if ((uses->type != YN_USES) || (uses->ref != gr->id)) {
                continue;
            }

            /* Insert new node. (The node that was deleted in grouping.) */
            parent = uses->parent;
            prev = ay_ynode_get_prev(uses);
            if (prev) {
                ay_ynode_insert_sibling(tree, prev);
                new = prev->next;
            } else {
                ay_ynode_insert_child(tree, parent);
                new = parent->child;
            }
            ay_ynode_copy_data(new, &data);
            ay_ynode_move_subtree_as_child(tree, new, new->next);
            uses = new->child;
            new->choice = uses->choice;

            if (!ref || empty_grouping) {
                /* YN_USES node is deleted because grouping was deleted too. */
                free(uses->ident);
                ay_ynode_delete_node(tree, uses);
                i = AY_INDEX(tree, new);
            } else {
                /* YN_USES node has reference to new grouping or still the same. */
                uses->ref = ref;
                i = AY_INDEX(tree, uses);
            }
        }
    }

    /* Remove grouping if has no children. */
    for (i = 1; (tree[i].type == YN_GROUPING) && (i < LY_ARRAY_COUNT(tree)); i += tree[i].descendants + 1) {
        gr = &tree[i];
        if (gr->descendants == 0) {
            free(gr->ident);
            ay_ynode_delete_node(tree, gr);
            i--;
        }
    }

    return 0;
}

/**
 * @brief Insert containers to the choice or change type from YN_CASE to container.
 *
 * This prevents duplicate nodes.
 *
 * @param[in,out] tree Tree of ynodes.
 * @return 0 on success.
 */
static int
ay_ynode_insert_container_in_choice(struct ay_ynode *tree)
{
    int ret = 0;
    LY_ARRAY_COUNT_TYPE i;
    struct ay_ynode *cas, *iter, *first;
    const struct ay_lnode *choice;
    uint64_t dupl_count;
    ly_bool insert_cont;

    /* Find YN_CASE node. */
    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        cas = &tree[i];
        if (cas->type != YN_CASE) {
            continue;
        }

        /* Check if some YN_CASE node has name collision. */
        insert_cont = 0;
        for (iter = cas->child; iter; iter = iter->next) {
            ret = ay_yang_ident_duplications(tree, iter, iter->ident, NULL, &dupl_count);
            AY_CHECK_RET(ret);
            if (dupl_count) {
                /* Name collision. */
                insert_cont = 1;
                break;
            }
        }
        if (!insert_cont) {
            continue;
        }

        /* Let's insert containers for better readability. */
        first = ay_ynode_get_first_in_choice(cas->parent, cas->choice);
        choice = cas->choice;
        for (iter = first; iter && (iter->choice == choice); iter = iter->next) {
            if (iter->type == YN_CASE) {
                /* For YN_CASE node just change type. */
                iter->type = YN_CONTAINER;
            } else {
                /* Insert container. */
                ay_ynode_insert_wrapper(tree, iter);
                iter->type = YN_CONTAINER;
                iter->choice = iter->child->choice;

                iter->when_ref = iter->child->when_ref;
                iter->when_val = iter->child->when_val;
                iter->child->when_ref = 0;
                iter->child->when_val = NULL;
            }
        }
    }

    return ret;
}

/**
 * @brief Set ay_ynode.type for all nodes.
 *
 * @param[in,out] tree Tree of ynodes.
 */
static void
ay_ynode_set_type(struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i;
    struct ay_ynode *node;

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        node = &tree[i];
        if (!node->snode) {
            assert(node->type != YN_UNKNOWN);
            continue;
        } else if ((node->type == YN_REC) || (node->type == YN_LIST)) {
            continue;
        }

        if (ay_ynode_rule_list(node) || ay_ynode_rule_leaflist(node)) {
            node->type = YN_LIST;
        } else if (ay_ynode_rule_container(node)) {
            node->type = YN_CONTAINER;
        } else if (ay_ynode_rule_leaf(node)) {
            node->type = YN_LEAF;
        }
    }
}

/**
 * @brief Wrapper for calling some insert function.
 *
 * Warning: The @p tree pointer may or may not change. Therefore, this function should only be called
 * from ay_ynode_transformations() which does not contain pointers to array elements.
 *
 * @param[in,out] tree Tree of ynodes. The memory address of the tree may change.
 * The insertion result will be applied.
 * @param[in] items_count Number of nodes to be inserted.
 * @param[in] insert Callback function which inserts some nodes.
 * @return 0 on success.
 */
static int
ay_ynode_trans_insert(struct ay_ynode **tree, int (*insert)(struct ay_ynode *), uint32_t items_count)
{
    int ret;
    uint64_t free_space, new_items;
    void *old;

    if (items_count == 0) {
        return 0;
    }

    free_space = AY_YNODE_ROOT_ARRSIZE(*tree) - LY_ARRAY_COUNT(*tree);
    if (free_space < items_count) {
        new_items = items_count - free_space;
        old = *tree;
        LY_ARRAY_CREATE_RET(NULL, *tree, items_count, AYE_MEMORY);
        if (*tree != old) {
            ay_ynode_tree_correction(*tree);
        }
        AY_YNODE_ROOT_ARRSIZE(*tree) = AY_YNODE_ROOT_ARRSIZE(*tree) + new_items;
    }
    ret = insert(*tree);

    return ret;
}

/**
 * @brief Wrapper for calling some insert function.
 *
 * @param[in,out] ctx Context containing tree of ynodes and default vaules.
 * @param[in] insert Callback function which inserts some nodes.
 * @param[in] items_count Number of nodes to be inserted.
 * @return 0 on success.
 */
static int
ay_ynode_trans_ident_insert(struct yprinter_ctx *ctx, int (*insert)(struct ay_ynode *), uint32_t items_count)
{
    int ret = 0;

    if (items_count) {
        AY_CHECK_RV(ay_ynode_trans_insert(&ctx->tree, insert, items_count));
        ret = ay_ynode_idents(ctx, 1);
    }

    return ret;
}

/**
 * @brief Transformations based on ynode identifier.
 *
 * @param[in] mod Augeas module.
 * @param[in,out] tree Tree of ynodes.
 * @return 0 on success.
 */
static int
ay_ynode_transformations_ident(struct module *mod, struct ay_ynode **tree)
{
    int ret;
    struct yprinter_ctx ctx;

#define TRANSF(FUNC, REQ_SPACE) \
    AY_CHECK_RV(ay_ynode_trans_ident_insert(&ctx, FUNC, REQ_SPACE))

    ctx.aug = ay_get_augeas_ctx1(mod);
    ctx.mod = mod;

    /* Set identifier for every ynode. */
    ctx.tree = *tree;
    ret = ay_ynode_idents(&ctx, 0);
    AY_CHECK_RET(ret);

    TRANSF(ay_ynode_insert_container_in_choice, ay_ynode_summary(*tree, ay_ynode_rule_insert_container_in_choice));

    TRANSF(ay_ynode_grouping_reduction, ay_ynode_grouping_reduction_count(ctx.tree));

    ret = ay_ynode_idents(&ctx, 1);
    AY_CHECK_RET(ret);

    *tree = ctx.tree;

#undef TRANSF

    return 0;
}

/**
 * @brief Apply various transformations before the tree is ready to print.
 *
 * @param[in] mod Module containing lenses for printing.
 * @param[in,out] tree Tree of ynodes. The memory address of the tree will be changed.
 * @return 0 on success.
 */
static int
ay_ynode_transformations(struct module *mod, struct ay_ynode **tree)
{
    int ret = 0;

#define TRANSF(FUNC, REQ_SPACE) \
    AY_CHECK_RV(ay_ynode_trans_insert(tree, FUNC, REQ_SPACE))

    assert((*tree)->type == YN_ROOT);

    /* Insert list if two L_STAR belongs to ynode node. */
    TRANSF(ay_ynode_insert_implicit_list, ay_ynode_rule_insert_implicit_list(*tree));

    /* set type */
    ay_ynode_set_type(*tree);

    ay_delete_type_unknown(*tree);

    /* lns . (sep . lns)*   -> lns*
     * (sep . lns)* . lns   -> lns*
     */
    ay_ynode_delete_build_list(*tree);

    /* Reset choice for siblings. */
    ay_ynode_unite_choice(*tree);

    /* [ (key lns1 | key lns2) lns3 ]    -> node { type union { pattern lns1; pattern lns2; }}
     * store to YN_ROOT.labels
     * [ key lns1 (store lns2 | store lns3)) ]    -> node { type union { pattern lns2; pattern lns3; }}
     * store to YN_ROOT.values
     */
    ay_ynode_set_lv(*tree);

    /* [ key lns1 | key lns2 ... ] -> [ key lns1 ] | [ key lns2 ] ... */
    TRANSF(ay_ynode_more_keys_for_node, ay_ynode_rule_more_keys_for_node(*tree));

    /* ([key lns1 ...] . [key lns2 ...]) | [key lns3 ...]   ->
     * choice ch { case { node1{pattern lns1} node2{pattern lns2} } node3{pattern lns3} }
     */
    TRANSF(ay_ynode_insert_case, ay_ynode_summary(*tree, ay_ynode_rule_insert_case));

    /*
     * [key lns1] | (([key lns2] | [key lns3]) . [key lns4]) ->
     * [key lns1] | YN_CASE{[key lns2] . [key lns4]} | YN_CASE{[key lns3] . [key lns4]}
     */
    TRANSF(ay_ynode_copy_case_nodes, ay_ynode_rule_copy_case_nodes(*tree));

    /* If some choice branch is repeated, it is useless and is deleted. */
    ay_ynode_delete_equal_cases(*tree);

    /* ... | [key lns1 . lns2] . lns3 | [key lns1 . lns2] . lns4 | ... ->
     * ... | [key lns1 . lns2] . (lns3 | lns4) | ... */
    /* If lns3 or lns4 missing then choice between lns3 and lns4 is not mandatory. */
    TRANSF(ay_ynode_merge_cases, ay_ynode_rule_merge_cases(*tree));

    /* Choice is useless if it has all branches the same except for the different when-stmt,
     * which actually cover all possible values. Choice and when-stmt is deleted, one branch is left.
     */
    ay_ynode_delete_useless_choice(*tree);

    /* insert top-level list for storing configure file */
    TRANSF(ay_insert_list_files, 1);

    /* Make a tree that reflects the order of records.
     * list A {} list B{} -> list C { container A{} container B{}}
     */
    TRANSF(ay_ynode_ordered_entries, ay_ynode_rule_ordered_entries(AY_YNODE_ROOT_LTREE(*tree)));

    /* Apply recursive yang form for recursive lenses. */
    TRANSF(ay_ynode_recursive_form, ay_ynode_summary(*tree, ay_ynode_rule_recursive_form));

    /* [label str store lns]*   -> container { YN_KEY{} } */
    /* [key lns1 store lns2]*   -> container { YN_KEY{} YN_VALUE{} } */
    TRANSF(ay_insert_node_key_and_value, ay_ynode_summary2(*tree, ay_ynode_rule_node_key_and_value));

    /* [label str (store lns | store lns2 . [label str2])] -> [label str2] has 'when' reference to lns2 */
    /* ... */
    ay_ynode_dependence_on_value(*tree);

    ay_ynode_tree_set_mandatory(*tree);

    /* Decide if the 'or not(...)' should be added into when-stmt.
     * Set if the target node is not mandatory for the given 'when'.
     */
    ay_ynode_when_ornot(*tree);

    /* Groupings algorithms. */

    /* It is decided whether the recursive form will be wrapped in groupings, or a nodes will be copied. */
    TRANSF(ay_ynode_recursive_form_by_copy, ay_ynode_rule_recursive_form_by_copy(*tree));

    /* Find groupings for recursive form. */
    ay_ynode_set_ref_recursive_form(*tree);

    /* Groupings are resolved in functions ay_ynode_set_ref() and ay_ynode_create_groupings_toplevel() */
    /* Link nodes that should be in grouping by number. */
    ay_ynode_set_ref(*tree);

    /* Create groupings and uses-stmt based on recursive form.  */
    TRANSF(ay_ynode_create_groupings_recursive_form, ay_ynode_rule_create_groupings_recursive_form(*tree));

    /* Create groupings and uses-stmt for containers and lists. */
    TRANSF(ay_ynode_create_groupings_toplevel, ay_ynode_summary(*tree, ay_ynode_rule_create_groupings_toplevel));

    /* Delete YN_REC nodes. */
    ay_ynode_delete_ynrec(*tree);

    /* [key "a" | "b"] -> list a {} list b {} */
    /* It is for generally nodes, not just a list nodes. */
    TRANSF(ay_ynode_node_split, ay_ynode_rule_node_split(*tree, *tree));

    /* No other groupings will not be added, so move groupings in front of config-file list. */
    AY_CHECK_RV(ay_ynode_groupings_ahead(*tree));

    /* Changes based on identifier */

    ay_ynode_snode_unique_pnode(*tree);

    /* Transformations based on ynode identifier. */
    AY_CHECK_RV(ay_ynode_transformations_ident(mod, tree));

#undef TRANSF

    return ret;
}

int
augyang_print_yang(struct module *mod, uint64_t vercode, char **str)
{
    int ret = 0;
    struct lens *lens;
    struct ay_lnode *ltree = NULL;
    struct ay_ynode *ytree = NULL;
    struct ay_pnode *ptree = NULL;
    uint64_t ltree_size = 0, yforest_size = 0, tpatt_size = 0;

    AY_CHECK_COND(!mod, AYE_LENSE_NOT_FOUND);

    assert(sizeof(struct ay_ynode) == sizeof(struct ay_ynode_root));

    lens = ay_lense_get_root(mod);
    AY_CHECK_COND(!lens, AYE_LENSE_NOT_FOUND);

    ay_lense_summary(lens, &ltree_size, &yforest_size, &tpatt_size);
    AY_CHECK_COND(yforest_size + 1 > UINT32_MAX, AYE_MEMORY);

    /* Create lnode tree. */
    LY_ARRAY_CREATE_GOTO(NULL, ltree, ltree_size, ret, cleanup);
    ay_lnode_create_tree(ltree, lens, ltree);
    ret = ay_lnode_tree_check(ltree, mod);
    AY_CHECK_GOTO(ret, cleanup);
    ay_test_lnode_tree(vercode, mod, ltree);

    /* Create pnode tree. */
    ret = ay_pnode_create(ay_get_augeas_ctx1(mod), lens->info->filename->str, ltree, &ptree);
    AY_CHECK_GOTO(ret, cleanup);
    ay_pnode_print_verbose(vercode, ptree);

    /* Create ynode forest. */
    LY_ARRAY_CREATE_GOTO(NULL, ytree, yforest_size + 1, ret, cleanup);
    ay_ynode_create_tree(ltree, tpatt_size, ytree);
    AY_CHECK_GOTO(ret, cleanup);
    /* The ltree is now owned by ytree, so ytree is responsible for freeing memory of ltree. */
    ltree = NULL;
    /* Print ytree if debugged. */
    ret = ay_debug_ynode_tree(vercode, AYV_YTREE, ytree);
    AY_CHECK_GOTO(ret, cleanup);

    /* Apply transformations. */
    ret = ay_ynode_transformations(mod, &ytree);
    AY_CHECK_GOTO(ret, cleanup);
    ret = ay_debug_ynode_tree(vercode, AYV_YTREE_AFTER_TRANS, ytree);
    AY_CHECK_GOTO(ret, cleanup);

    ret = ay_print_yang(mod, ytree, vercode, str);

cleanup:
    LY_ARRAY_FREE(ltree);
    ay_pnode_free(ptree);
    ay_ynode_tree_free(ytree);

    return ret;
}
