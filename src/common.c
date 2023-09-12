/**
 * @file common.c
 * @author Adam Piecek <piecek@cesnet.cz>
 * @brief Common functions for augyang.
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

#include <assert.h>

#include <libyang/libyang.h>

#include "augyang.h"
#include "common.h"
#include "errcode.h"
#include "lens.h"
#include "transform.h"

struct augeas *
ay_get_augeas_ctx1(struct module *mod)
{
    assert(mod);
    return (struct augeas *)mod->bindings->value->info->error->aug;
}

struct augeas *
ay_get_augeas_ctx2(struct lens *lens)
{
    assert(lens);
    return (struct augeas *)lens->info->error->aug;
}

struct module *
ay_get_module(struct augeas *aug, const char *modname, size_t modname_len)
{
    struct module *mod = NULL, *mod_iter;
    size_t len;

    len = modname_len ? modname_len : strlen(modname);
    LY_LIST_FOR(aug->modules, mod_iter) {
        assert(mod_iter->name);
        if (!strncmp(mod_iter->name, modname, len)) {
            mod = mod_iter;
            break;
        }
    }

    return mod;
}

/**
 * @brief Get module by the filename/path.
 *
 * @param[in] aug Augeas context.
 * @param[in] filename Path to module. Get from lens.info.filename.str.
 * @return Pointer to module or NULL.
 */
static struct module *
ay_get_module2(struct augeas *aug, const char *filename)
{
    struct module *mod = NULL, *mod_iter;

    assert(filename);

    LY_LIST_FOR(aug->modules, mod_iter) {
        if (!mod_iter->bindings) {
            continue;
        }
        if (!strcmp(mod_iter->bindings->value->info->filename->str, filename)) {
            mod = mod_iter;
            break;
        }
    }

    return mod;
}

struct module *
ay_get_module_by_lens(struct lens *lens)
{
    struct augeas *aug;

    aug = ay_get_augeas_ctx2(lens);
    assert(aug);
    return ay_get_module2(aug, lens->info->filename->str);
}

void
ay_get_filename(const char *path, const char **name, uint64_t *len)
{
    const char *iter;

    if ((*name = strrchr(path, '/'))) {
        *name = *name + 1;
    } else {
        *name = path;
    }

    *len = 0;
    for (iter = *name; iter && (*iter != '.'); iter++) {
        (*len)++;
    }
}

/**
 * @brief Check if character is valid as part of identifier.
 *
 * @param[in] ch Character to check.
 * @param[out] shift Number is set to 1 if the next character should not be read in the next iteration
 * because it is preceded by a backslash. Otherwise is set to 0.
 * @return 1 for valid character, otherwise 0.
 */
static ly_bool
ay_ident_character_is_valid(const char *ch, uint32_t *shift)
{
    assert(ch && shift);

    *shift = 0;

    if (((*ch >= 65) && (*ch <= 90)) || /* A-Z */
            ((*ch >= 97) && (*ch <= 122)) || /* a-z */
            ((*ch >= 48) && (*ch <= 57))) { /* 0-9 */
        return 1;
    } else if (((*ch == '\\') && (*(ch + 1) == '.')) ||
            ((*ch == '\\') && (*(ch + 1) == '-')) ||
            ((*ch == '\\') && (*(ch + 1) == '+'))) {
        *shift = 1;
        return 1;
    } else {
        switch (*ch) {
        case ' ':
        case '-':
        case '_':
            return 1;
        default:
            return 0;
        }
    }
}

struct lens *
ay_lense_get_root(struct module *mod)
{
    struct binding *bnd;
    enum value_tag tag;

    /* Print lense. */
    if (mod->autoload) {
        /* root lense is prepared */
        return mod->autoload->lens;
    } else {
        bnd = mod->bindings;
        if (!bnd || !bnd->value) {
            return NULL;
        }

        LY_LIST_FOR(mod->bindings, bnd) {
            tag = bnd->value->tag;
            if ((tag == V_TRANSFORM) || (tag == V_FILTER)) {
                continue;
            } else if (tag == V_LENS) {
                return bnd->value->lens;
            } else {
                return NULL;
            }
        }
    }

    return NULL;
}

const struct ay_lnode *
ay_lnode_next_lv(const struct ay_lnode *lv, uint8_t lv_type)
{
    const struct ay_lnode *iter, *stop;
    enum lens_tag tag;

    if (!lv) {
        return NULL;
    }

    for (iter = lv->parent; iter && (iter->lens->tag != L_SUBTREE); iter = iter->parent) {}
    if (!iter || (iter->lens->tag != L_SUBTREE)) {
        return NULL;
    }

    stop = iter + iter->descendants + 1;
    for (iter = lv + 1; iter < stop; iter++) {
        tag = iter->lens->tag;
        if (tag == L_SUBTREE) {
            iter += iter->descendants;
        } else if (((lv_type == AY_LV_TYPE_LABEL) && AY_TAG_IS_LABEL(tag)) ||
                ((lv_type == AY_LV_TYPE_VALUE) && AY_TAG_IS_VALUE(tag)) ||
                ((lv_type == AY_LV_TYPE_ANY) && (AY_TAG_IS_VALUE(tag)))) {
            return iter;
        }
    }

    return NULL;
}

ly_bool
ay_lense_pattern_is_label(struct lens *lens)
{
    char *ch;
    uint32_t shift;

    if (!lens || ((lens->tag != L_STORE) && (lens->tag != L_KEY)) || lens->regexp->nocase) {
        return 0;
    }

    for (ch = lens->regexp->pattern->str; *ch != '\0'; ch++) {
        if (!ay_ident_character_is_valid(ch, &shift)) {
            break;
        }
        ch = shift ? ch + shift : ch;
    }

    return *ch == '\0';
}

ly_bool
ay_yang_type_is_empty(const struct ay_lnode *lnode)
{
    struct ay_lnode *iter;

    for (iter = lnode->parent; iter; iter = iter->parent) {
        if (iter->lens->tag == L_MAYBE) {
            return 1;
        } else if (iter->lens->tag == L_SUBTREE) {
            return 0;
        }
    }

    return 0;
}

int
ay_dnode_insert(struct ay_dnode *dict, const void *key, const void *value, int (*equal)(const void *, const void *))
{
    int ret = 0;
    struct ay_dnode *dkey, *dval, *gap;

    dkey = ay_dnode_find(dict, key);
    dval = ay_dnode_find(dict, value);
    if ((dkey && (AY_DNODE_IS_VAL(dkey) || !ay_dnode_value_is_unique(dkey, value, equal))) ||
            (equal && !dkey && !dval && equal(key, value))) {
        return ret;
    } else if (dval && AY_DNODE_IS_KEY(dval)) {
        /* The dval will no longer be dictionary key. It will be value of dkey. */
        ret = ay_dnode_merge_keys(dict, dkey, dval);
        return ret;
    }

    if (dkey) {
        /* insert value */
        gap = dkey + dkey->values_count + 1;
        memmove(gap + 1, gap, LY_ARRAY_COUNT(dict) - (gap - dict));
        gap->kvd = value;
        gap->values_count = 0;
        dkey->values_count++;
        LY_ARRAY_INCREMENT(dict);
    } else if (!dkey) {
        /* insert new pair */
        dkey = dict + LY_ARRAY_COUNT(dict);
        dkey[0].kvd = key;
        dkey[0].values_count = 1;
        dkey[1].kvd = value;
        dkey[1].values_count = 0;
        LY_ARRAY_INCREMENT(dict);
        LY_ARRAY_INCREMENT(dict);
    }

    return ret;
}

struct ay_dnode *
ay_dnode_find(struct ay_dnode *dict, const void *kvd)
{
    LY_ARRAY_COUNT_TYPE i;

    LY_ARRAY_FOR(dict, i) {
        if (dict[i].kvd == kvd) {
            return &dict[i];
        }
    }

    return NULL;
}

int
ay_dnode_merge_keys(struct ay_dnode *dict, struct ay_dnode *key1, struct ay_dnode *key2)
{
    LY_ARRAY_COUNT_TYPE i, j, k;
    struct ay_dnode *buff = NULL;

    /* Create buffer. */
    LY_ARRAY_CREATE_RET(NULL, buff, LY_ARRAY_COUNT(dict), AYE_MEMORY);
    j = 0;

    /* Insert key1 and its nodes. */
    AY_DNODE_KEYVAL_FOR(key1, i) {
        buff[j++] = key1[i];
    }

    /* Insert key2 as value of key1. */
    buff[j] = *key2;
    buff[j].values_count = 0;
    j++;

    /* Insert key2's values as key1 values. */
    AY_DNODE_VAL_FOR(key2, i) {
        AY_DNODE_VAL_FOR(key1, k) {
            /* Every item in dictionary should be unique. */
            assert(key1[k].kvd != key2[i].kvd);
        }
        buff[j++] = key2[i];
    }

    /* Set correct 'values_count' for key1. */
    buff[0].values_count += key2->values_count + 1;

    /* Copy all other keys and values. */
    LY_ARRAY_FOR(dict, i) {
        if ((&dict[i] == key1) || (&dict[i] == key2)) {
            /* Skip nodes. */
            i += dict[i].values_count;
        } else {
            buff[j++] = dict[i];
        }
    }
    assert(i == j);

    /* Store merge result into dict. */
    LY_ARRAY_FOR(dict, i) {
        dict[i] = buff[i];
    }

    LY_ARRAY_FREE(buff);
    return 0;
}

ly_bool
ay_dnode_value_is_unique(struct ay_dnode *key, const void *value,  int (*equal)(const void *, const void *))
{
    uint64_t i;

    if (!equal) {
        return 1;
    }

    AY_DNODE_KEYVAL_FOR(key, i) {
        if (equal(key[i].kvd, value)) {
            return 0;
        }
    }

    return 1;
}

struct ay_transl *
ay_transl_find(struct ay_transl *table, const char *origin)
{
    LY_ARRAY_COUNT_TYPE i;
    struct ay_transl *ret;

    ret = NULL;
    LY_ARRAY_FOR(table, i) {
        if (table[i].origin == origin) {
            return &table[i];
        }
    }

    return ret;
}

struct ay_ynode *
ay_ynode_get_first_in_choice(const struct ay_ynode *parent, const struct ay_lnode *choice)
{
    struct ay_ynode *iter;

    if (!choice || !parent) {
        return NULL;
    }

    for (iter = parent->child; iter; iter = iter->next) {
        if (iter->choice == choice) {
            return iter;
        }
    }

    return NULL;
}

ly_bool
ay_ynode_alone_in_choice(struct ay_ynode *node)
{
    if ((!node->choice) || (node != ay_ynode_get_first_in_choice(node->parent, node->choice))) {
        return 0;
    }
    if (node->next && (node->next->choice == node->choice)) {
        return 0;
    } else {
        return 1;
    }
}

ly_bool
ay_ynode_when_value_is_valid(const struct ay_ynode *node)
{
    const char *str;
    struct lens *ln;

    assert(node->when_val);
    ln = node->when_val->lens;

    assert((ln->tag == L_VALUE) || (ln->tag == L_STORE));
    str = ln->tag == L_VALUE ? ln->string->str : ln->regexp->pattern->str;

    if (strchr(str, '\'')) {
        /* The 'when' is not valid from the point of view of the XPATH 1.0 standard.
         * One solution is to use a variable such as SINGLE_QUOTE that contains the ' value.
         * But such generated yang would then only be valid for libyang.
         */
        return 0;
    }

    return 1;
}

struct ay_ynode *
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

struct ay_ynode *
ay_ynode_when_target(struct ay_ynode *tree, struct ay_ynode *node, uint64_t *path_cnt)
{
    struct ay_ynode *refnode, *parent, *child, *target;
    uint64_t i, path;

    /* Get referenced node. */
    refnode = NULL;
    path = 0;
    for (parent = node->parent; parent; parent = parent->parent) {
        if (parent->type != YN_CASE) {
            ++path;
        }
        if (parent->id == node->when_ref) {
            refnode = parent;
            break;
        }
        /* The entire subtree is searched, but the 'parent' child should actually be found. Additionally, it can be
         * wrapped in a YN_LIST, complicating a simple search using a 'for' loop.
         */
        for (i = 0; i < parent->descendants; i++) {
            child = &parent[i + 1];
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
        path++;
    }
    if ((node->type == YN_CASE) && (path > 0)) {
        /* In YANG, the case-stmt is not counted in the path. */
        assert(path);
        path--;
    }

    if (path_cnt) {
        *path_cnt = path;
    }

    if ((refnode->type != YN_VALUE) && (refnode->type != YN_LEAF)) {
        target = ay_ynode_get_value_node(tree, refnode, refnode->label, refnode->value);
    } else {
        target = refnode;
    }

    return target;
}

struct ay_ynode *
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

uint64_t
ay_ynode_splitted_seq_index(const struct ay_ynode *node)
{
    struct ay_ynode *iter;
    struct lens *nodelab, *iterlab;
    uint64_t node_idx = 0;

    nodelab = AY_LABEL_LENS(node);
    for (iter = node->parent->child; iter; iter = iter->next) {
        iterlab = AY_LABEL_LENS(iter);
        if (iter == node) {
            break;
        } else if (iterlab && (iterlab->regexp == nodelab->regexp)) {
            ++node_idx;
        }
    }
    assert(iter);

    return node_idx;
}
