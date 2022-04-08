/**
 * @file augyang.c
 * @author Adam Piecek <piecek@cesnet.cz>
 * @brief The augyang core implementation.
 *
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
#include "errcode.h"
#include "lens.h"
#include "transform.h"

/**
 * @brief Alignment size of the output text when nesting.
 */
#define SPACE_INDENT 2

/**
 * @brief Check @p RETVAL and call return with @p RETVAL.
 *
 * @param[in] RETVAL Value to check.
 */
#define AY_CHECK_RET(RETVAL) if (RETVAL != 0) {return RETVAL;}

/**
 * @brief Check return value of function @p FUNC (expected 0).
 *
 * @param[in] FUNC Called function.
 */
#define AY_CHECK_RV(FUNC) {int ret__ = FUNC; if (ret__ != 0) {return ret__;}}

/**
 * @brief Check by @p COND and call return with @p RETVAL.
 *
 * @param[in] COND Boolean expression.
 * @param[in] RETVAL Value to return.
 */
#define AY_CHECK_COND(COND, RETVAL) if (COND) {return RETVAL;}

/**
 * @brief Check by @p COND and call goto.
 *
 * @param[in] COND Boolean expression.
 * @param[in] GOTO Label where to jump.
 */
#define AY_CHECK_GOTO(COND, GOTO) if (COND) {goto GOTO;}

/**
 * @brief Edit size of [sizedarray].
 *
 * @param[in] ARRAY Array in which the size is edited.
 * @param[in] SIZE Value to be written.
 */
#define AY_SET_LY_ARRAY_SIZE(ARRAY, SIZE) \
    *(((LY_ARRAY_COUNT_TYPE *)ARRAY) - 1) = SIZE;

/**
 * @brief Check if the lense cannot have children.
 *
 * @param[in] TAG Tag of the lense.
 */
#define AY_LENSE_HAS_NO_CHILD(TAG) (TAG <= L_COUNTER)

/**
 * @brief Check if the lense can have exactly one child.
 *
 * @param[in] TAG Tag of the lense.
 */
#define AY_LENSE_HAS_ONE_CHILD(TAG) ((TAG >= L_SUBTREE) && (TAG != L_REC))

/**
 * @brief Check if the lense can have more children (not at most one).
 *
 * @param[in] TAG Tag of the lense.
 */
#define AY_LENSE_HAS_CHILDREN(TAG) ((TAG == L_CONCAT) || (TAG == L_UNION))

/**
 * @brief Get first child from @p LENSE.
 *
 * @param[in] LENSE Lense to examine. Its type allows to have one or more children.
 * @return Child or NULL.
 */
#define AY_GET_FIRST_LENSE_CHILD(LENSE) \
    (LENSE->tag == L_REC) && !LENSE->rec_internal ? \
        lens->body : \
    LENSE->tag == L_REC ? \
        NULL : \
    AY_LENSE_HAS_ONE_CHILD(LENSE->tag) ? \
        lens->child : \
    lens->nchildren ? \
        lens->children[0] : NULL

/**
 * @brief Get lense from ynode.label.
 *
 * @param[in] YNODE Node ynode which may not have the label set.
 * @return Lense of ay_ynode.label or NULL.
 */
#define AY_LABEL_LENS(YNODE) \
    YNODE->label ? YNODE->label->lens : NULL

/**
 * @brief Get lense from ynode.value.
 *
 * @param[in] YNODE Node ynode which may not have the value set.
 * @return Lense of ay_ynode.value or NULL.
 */
#define AY_VALUE_LENS(YNODE) \
    YNODE->value ? YNODE->value->lens : NULL

/**
 * @brief Get lense from ynode.snode.
 *
 * @param[in] YNODE Node ynode which may not have the snode set.
 * @return Lense of ay_ynode.snode or NULL.
 */
#define AY_SNODE_LENS(YNODE) \
    YNODE->snode ? YNODE->snode->lens : NULL

/**
 * @brief Calculate the index value based on the pointer.
 *
 * @param[in] ARRAY Sized array.
 * @param[in] ITEM_PTR Pointer to item in @p ARRAY.
 */
#define AY_INDEX(ARRAY, ITEM_PTR) \
    (ITEM_PTR - ARRAY)

/**
 * @brief Get address of @p ITEM as if it was in the @p ARRAY1.
 *
 * Remap address @p ITEM (from @p ARRAY2) to the address in @p ARRAY1.
 * The address is calculated based on the index.
 *
 * @param[in] ARRAY1 Sized array to which the address of @p ITEM is converted.
 * @param[in] ARRAY2 Sized array where @p ITEM is located.
 * @param[in] ITEM Pointer to item in @p ARRAY2.
 * @return Address in the @p ARRAY1 or NULL.
 */
#define AY_MAP_ADDRESS(ARRAY1, ARRAY2, ITEM) \
    (ITEM ? &ARRAY1[AY_INDEX(ARRAY2, ITEM)] : NULL)

/**
 * @brief Tag message of the augyang executable.
 */
#define AY_NAME "[augyang]"

/**
 * @brief Maximum identifier size (yang statement identifier).
 */
#define AY_MAX_IDENT_SIZE 64

/* error codes */

#define AYE_MEMORY 1
#define AYE_LENSE_NOT_FOUND 2
#define AYE_L_REC 3
#define AYE_DEBUG_FAILED 4
#define AYE_IDENT_NOT_FOUND 5
#define AYE_IDENT_LIMIT 6
#define AYE_LTREE_NO_ROOT 7

/**
 * @brief Check if lense tag belongs to ynode.label.
 *
 * @param[in] TAG Tag of the lense.
 */
#define AY_TAG_IS_LABEL(TAG) \
    ((TAG == L_LABEL) || (TAG == L_KEY) || (TAG == L_SEQ))

/**
 * @brief Check if lense tag belongs to ynode.value.
 *
 * @param[in] TAG Tag of the lense.
 */
#define AY_TAG_IS_VALUE(TAG) \
    ((tag == L_STORE) || (tag == L_VALUE))

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
 * @brief Wrapper for lense node.
 *
 * Interconnection of lense structures is not suitable for comfortable browsing. Therefore, an ay_lnode wrapper has
 * been created that contains a better connection between the nodes.
 * The lnode nodes are stored in the Sized array and the number of nodes should not be modified.
 */
struct ay_lnode {
    struct ay_lnode *parent;    /**< Pointer to the parent node. */
    struct ay_lnode *next;      /**< Pointer to the next sibling node. */
    struct ay_lnode *child;     /**< Pointer to the first child node. */
    uint32_t descendants;       /**< Number of descendants in the subtree where current node is the root. */

    struct lens *lens;          /**< Pointer to lense node. Always set. */
};

struct ay_dnode;

/**
 * @brief Type of the ynode.
 */
enum yang_type {
    YN_UNKNOWN = 0,     /**< Unknown or undefined type. */
    YN_LEAF,            /**< Yang statement "leaf". */
    YN_LEAFREF,         /**< Yang statement "leaf" of type leafref. */
    YN_LEAFLIST,        /**< Yang statement "leaf-list". */
    YN_LIST,            /**< Yang statement "list". */
    YN_CONTAINER,       /**< Yang statement "container". */
    YN_KEY,             /**< The node is the key in the yang "list" or first node in special container. */
    YN_VALUE,           /**< Yang statement "leaf". The node was generated to store the augeas node value. */
    YN_USES,            /**< Yang statement "uses". */
    YN_GROUPING,        /**< Yang statement "grouping". */
    YN_REC,             /**< A special type of node that doesn't print in yang. Contains a reference to L_REC lense. */
    YN_ROOT             /**< A special type that is only one in the ynode tree. Indicates the root of the entire tree.
                             It has no printing application only makes writing algorithms easier. */
};

/**
 * @defgroup ynodeflags Ynode flags
 *
 * Various flags for ynode nodes (used as ::ay_ynode.flags).
 *
 * @{
 */
#define AY_YNODE_MAND_TRUE      0x01    /**< Yang mandatory-stmt. The ynode has "mandatory true;". In the case of
                                             type YN_LEAFLIST, the set bit means "min-elements 1;". */

#define AY_YNODE_MAND_FALSE     0x02    /**< No mandatory-stmt is printed. */
#define AY_YNODE_MAND_MASK      0x03    /**< Mask for mandatory-stmt. */
#define AY_CHOICE_MAND_FALSE    0x04    /**< Choice statement must be false. */
#define AY_VALUE_IN_CHOICE      0x08    /**< YN_VALUE of node must be in choice statement. */
#define AY_GROUPING_CHILDREN    0x10    /**< The ay_ynode.ref only affects children. */
#define AY_CONFIG_FALSE         0x20    /**< Print 'config false' for this node. */
/** @} ynodeflags */

/**
 * @brief Node for printing the yang node.
 *
 * The ynode node represents the yang data node. It is generally created from a lense with tag L_SUBTREE, but can also
 * be created later, in which case ay_ynode.snode is set to NULL. Nodes are stored in the Sized array, so if a node is
 * added or removed from the array, then the ay_ynode pointers must be reset to the correct address so that the tree
 * structure made sense. Functions ay_ynode_insert_* and ay_ynode_delete_node() are intended for these modifications.
 * These modifications are applied in transformations and they are gradually applied in the ay_ynode_transformations(),
 * which results in a print-ready yang format. The structure of the ynode tree should be adjusted in these
 * transformations. The ynode tree is created by the ynode forest (it has multiple root nodes), which is made up of a
 * lnode tree. In the ynode tree is always one root node with type YN_ROOT.
 *
 * Node ynode is also a bit like an augeas node. The Augeas manages its tree consisting of nodes that consist of
 * a label, value and children. The same items contains ynode. Unfortunately, the ynode tree cannot be the same as the
 * augeas tree, so an yang extension statement containing a path is added to the printed yang nodes to make the mapping
 * between the two trees clear. This path generation is handled in the ay_print_yang_data_path().
 *
 * One ynode can contain multiple labels/values because they can be separated by the union operator ('|', L_UNION).
 * So, for example, if a node has multiple labels, the ay_lnode_next_lv() returns the next one. These groups labels and
 * values are stored in YN_ROOT node, specifically in ay_ynode_root.labels and ay_ynode_root.values. In the YANG, such
 * a group is then printed as a union stmt.
 *
 * Note that choice-case node from yang laguage is not considered as a ynode. The ynode nodes are indirectly connected
 * via ay_ynode.choice pointer, which serves as an identifier of that choice-case relationship.
 */
struct ay_ynode {
    struct ay_ynode *parent;    /**< Pointer to the parent node. */
    struct ay_ynode *next;      /**< Pointer to the next sibling node. */
    struct ay_ynode *child;     /**< Pointer to the first child node. */
    uint32_t descendants;       /**< Number of descendants in the subtree where current node is the root. */

    enum yang_type type;        /**< Type of the ynode. */

    /* Applies to every yang_type except YN_ROOT. For YN_ROOT type node use conversion to struct ay_ynode_root. */

    const struct ay_lnode *snode;   /**< Pointer to the corresponding lnode with lense tag L_SUBTREE (or L_REC).
                                         Can be NULL if the ynode was inserted by some transformation. */
    const struct ay_lnode *label;   /**< Pointer to the first 'label' which is lense with tag L_KEY, L_LABEL
                                         or L_SEQ. Can be NULL. */
    const struct ay_lnode *value;   /**< Pointer to the first 'value' which is lense with tag L_STORE or L_VALUE.
                                         Can be NULL. */
    const struct ay_lnode *choice;  /**< Pointer to the lnode with lense tag L_UNION.
                                         Set if the node is under the influence of the union operator. */
    uint32_t ref;                   /**< Containes ay_ynode.id of some other ynode. Used as reference. */
    uint32_t flags;                 /**< [ynode flags](@ref ynodeflags) */
    uint32_t id;                    /**< Numeric identifier of ynode node. */
};

/**
 * @brief Specific structure for ynode of type YN_ROOT.
 *
 * The ynode of type YN_ROOT is always the first node in the ynode tree (in the Sized Array) and nowhere else.
 */
struct ay_ynode_root {
    struct ay_ynode *parent;        /**< Always NULL. */
    uint64_t arrsize;               /**< Allocated ynodes in LY_ARRAY. Root is also counted.
                                         NOTE: The LY_ARRAY_COUNT integer is used to store the number of items
                                         (ynode nodes) in the array and therefore is not used for array size. */
    struct ay_ynode *child;         /**< Pointer to the first child node. */
    uint32_t descendants;           /**< Number of descendants in the ynode tree. */

    enum yang_type type;            /**< Always YN_ROOT. */
    const struct ay_lnode *ltree;   /**< Pointer to the root of the tree of lnodes. */
    struct ay_dnode *labels;        /**< Dictionary for labels of type lnode for grouping labels to be printed
                                         in union-stmt. The key in the dictionary is the first label in the
                                         union and values in the dictionary are the remaining labels. */
    struct ay_dnode *values;        /**< Dictionary for values of type lnode. See ynode.labels. */
    const struct ay_lnode *choice;  /**< Not used. */
    uint32_t ref;                   /**< Not used. */
    uint32_t flags;                 /**< Not used. */
    uint32_t idcnt;                 /**< ID counter for uniquely assigning identifiers to ynodes. */
};

/**
 * @brief Get ay_ynode_root.arrsize from ynode tree.
 */
#define AY_YNODE_ROOT_ARRSIZE(TREE) \
    ((struct ay_ynode_root *)(TREE))->arrsize

/**
 * @brief Get ay_ynode_root.ltree from ynode tree.
 *
 * @param[in] TREE Tree of ynodes. First item in the tree must be YN_ROOT.
 */
#define AY_YNODE_ROOT_LTREE(TREE) \
    ((struct ay_ynode_root *)TREE)->ltree

/**
 * @brief Get ay_ynode_root.labels from ynode tree.
 *
 * @param[in] TREE Tree of ynodes. First item in the tree must be YN_ROOT.
 */
#define AY_YNODE_ROOT_LABELS(TREE) \
    ((struct ay_ynode_root *)TREE)->labels

/**
 * @brief Get ay_ynode_root.values from ynode tree.
 *
 * @param[in] TREE Tree of ynodes. First item in the tree must be YN_ROOT.
 */
#define AY_YNODE_ROOT_VALUES(TREE) \
    ((struct ay_ynode_root *)TREE)->values

/**
 * @brief Get ay_ynode_root.idcnt from ynode tree.
 *
 * @param[in] TREE Tree of ynodes. First item in the tree must be YN_ROOT.
 */
#define AY_YNODE_ROOT_IDCNT(TREE) \
    ((struct ay_ynode_root *)TREE)->idcnt

/**
 * @brief Increment ay_ynode_root.idcnt in the ynode tree.
 *
 * @param[in] TREE Tree of ynodes. First item in the tree must be YN_ROOT.
 */
#define AY_YNODE_ROOT_IDCNT_INC(TREE) \
    ((struct ay_ynode_root *)TREE)->idcnt++

/**
 * @brief Node (item) in the dictionary.
 *
 * The dictionary is stored in the Sized Array. One dnode in the dictionary can be of type KEY or it can be a VALUE for
 * corresponding KEY. A set of VALUES is stored under one KEY. At the same time, each dnode (item) in the dictionary
 * is unique. So a VALUE can only occur under one KEY and for this KEY it occurs only once.
 */
struct ay_dnode {
    uint32_t values_count;      /**< Number of VALUES stored for the KEY and it cannot be 0. This means that if it
                                     contains 0, it is a dnode of type VALUE. */
    union {
        const void *kvd;                /**< Generic KEY or VALUE Data. */
        const struct ay_lnode *lnode;   /**< Generic KEY or VALUE of type lnode. */
        const struct ay_lnode *lkey;    /**< KEY of the dictionary. */
        const struct ay_lnode *lval;    /**< VALUE of the KEY. */
    };
};

/**
 * @brief Iterate over KEYS in the dictionary.
 *
 * @param[in] DICT Pointer to the first item in the Sized Array.
 * @param[in] INDEX Index value for iterating.
 */
#define AY_DNODE_KEY_FOR(DICT, INDEX) \
    for (INDEX = 0; INDEX < LY_ARRAY_COUNT(DICT); INDEX += (DICT[INDEX].values_count + 1))

/**
 * @brief Iterate over VALUES for given KEY.
 *
 * @param[in] KEY Pointer to dnode of type KEY.
 * @param[in] INDEX Index value for iterating.
 */
#define AY_DNODE_VAL_FOR(KEY, INDEX) \
    for (INDEX = 1; INDEX <= KEY->values_count; INDEX++)

/**
 * @brief Iterate over given KEY and its VALUES.
 *
 * @param[in] KEY Pointer to dnode of type KEY.
 * @param[in] INDEX Index value for iterating.
 */
#define AY_DNODE_KEYVAL_FOR(KEY, INDEX) \
    for (INDEX = 0; INDEX <= KEY->values_count; INDEX++)

/**
 * @brief Check if @p DNODE is dictionary KEY.
 * @return 1 if yes.
 */
#define AY_DNODE_IS_KEY(DNODE) \
    (DNODE->values_count > 0)

/**
 * @brief Check if @p DNODE is dictionary VALUE.
 * @return 1 if yes.
 */
#define AY_DNODE_IS_VAL(DNODE) \
    (DNODE->values_count == 0)

struct lprinter_ctx;

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
 * @brief Context for the yang printer.
 */
struct yprinter_ctx {
    struct augeas *aug;     /**< Augeas context. */
    struct module *mod;     /**< Current Augeas module. */
    struct ay_ynode *tree;  /**< Pointer to the Sized array. */
    uint64_t vercode;       /**< Verbose options from API to debugging. */
    struct ly_out *out;     /**< Output to which it is printed. */
    int space;              /**< Current indent. */
};

/**
 * @brief Get error message base on the code.
 *
 * @param[in] err_code Error code;
 * @return String with message.
 */
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
    default:
        return AY_NAME " INTERNAL ERROR: error message not defined.\n";
    }
}

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
 * @brief Get main lense which augeas will use for parsing.
 *
 * @param[in] mod Current augeas module.
 * @return Main lense or NULL.
 */
static struct lens *
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

/**
 * @brief Get name of a file (without filename extension) from path.
 *
 * @param[in] path String containing the path to process.
 * @param[out] name Set to part where filename is is.
 * @param[out] len Length of the name.
 */
static void
ay_get_filename(char *path, char **name, uint64_t *len)
{
    char *iter;

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
 * @brief Go through all the lenses and set various counters.
 *
 * @param[in] lens Main lense where to start.
 * @param[out] ltree_size Number of lenses.
 * @param[out] yforest_size Number of lenses with L_SUBTREE tag.
 */
static void
ay_lense_summary(struct lens *lens, uint32_t *ltree_size, uint32_t *yforest_size)
{
    (*ltree_size)++;
    if ((lens->tag == L_SUBTREE) || (lens->tag == L_REC)) {
        (*yforest_size)++;
    }

    if (AY_LENSE_HAS_NO_CHILD(lens->tag)) {
        return;
    }

    if (AY_LENSE_HAS_ONE_CHILD(lens->tag)) {
        ay_lense_summary(lens->child, ltree_size, yforest_size);
    } else if (AY_LENSE_HAS_CHILDREN(lens->tag)) {
        for (uint64_t i = 0; i < lens->nchildren; i++) {
            ay_lense_summary(lens->children[i], ltree_size, yforest_size);
        }
    } else if ((lens->tag == L_REC) && !lens->rec_internal) {
        ay_lense_summary(lens->body, ltree_size, yforest_size);
    }
}

/**
 * @brief Search dnode KEY/VALUE in the dictionary.
 *
 * @param[in] dict Dictionary in which the @p kvd.
 * @param[in] kvd The Key or Value Data to be searched in the dnode.
 * @return The dnode with the same kvd or NULL.
 */
static struct ay_dnode *
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

/**
 * @brief Insert new KEY and VALUE pair or insert new VALUE for @p key to the dictionary.
 *
 * @param[in,out] dict Dictionary into which it is inserted.
 * @param[in] key The KEY to search or KEY to insert.
 * @param[in] value The VALUE to be added under @p key. If it is not unique, then another will NOT be added.
 */
static void
ay_dnode_insert(struct ay_dnode *dict, const void *key, const void *value)
{
    struct ay_dnode *dkey, *dval, *gap;

    dkey = ay_dnode_find(dict, key);
    dval = ay_dnode_find(dict, value);
    if (dkey && AY_DNODE_IS_VAL(dkey)) {
        return;
    } else if (dval && AY_DNODE_IS_KEY(dval)) {
        return;
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
}

/**
 * @brief Release ynode tree.
 *
 * @param[in] tree Tree of ynodes.
 */
static void
ay_ynode_tree_free(struct ay_ynode *tree)
{
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

    LY_ARRAY_FREE(tree);
}

/**
 * @brief Go through all the ynode nodes and sum rule results.
 *
 * @param[in] tree Tree of ynodes.
 * @param[in] rule Callback function returns number of nodes that can be inserted.
 * @param[out] cnt Counter how many times the @p rule returned 1.
 */
static void
ay_ynode_summary(struct ay_ynode *tree, uint64_t (*rule)(struct ay_ynode *), uint64_t *cnt)
{
    LY_ARRAY_COUNT_TYPE i;

    *cnt = 0;
    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        *cnt = *cnt + rule(&tree[i]);
    }
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

    for (last = node; last->next; last = last->next) {}
    return last;
}

/**
 * @brief Get first node which belongs to @p choice.
 *
 * @param[in] parent Node in which some of his immediate children contain @p choice.
 * @param[in] choice Choice id to find.
 * @return First node in choice or NULL.
 */
static struct ay_ynode *
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

/**
 * @brief Check if @p node is the only node in the choice.
 *
 * @param[in] node Node to check.
 * @return 1 if node is alone.
 */
ly_bool
ay_ynode_alone_in_choice(const struct ay_ynode *node)
{
    struct ay_ynode *iter;
    uint32_t sum;
    ly_bool node_hit;

    if (!node->choice) {
        return 1;
    }

    sum = 0;
    node_hit = 0;
    for (iter = node->parent->child; iter; iter = iter->next) {
        if (iter->choice == node->choice) {
            node_hit = iter == node ? 1 : node_hit;
            sum++;
        } else if (node_hit) {
            break;
        } else {
            sum = 0;
            node_hit = 0;
        }
    }

    return (sum == 1) && node_hit;
}

/**
 * @brief Check if lenses are equal.
 *
 * @param[in] l1 First lense.
 * @param[in] l2 Second lense.
 * @return 1 for equal.
 */
static ly_bool
ay_lnode_lense_equal(struct lens *l1, struct lens *l2)
{
    if (!l1 || !l2 || (l1->tag != l2->tag)) {
        return 0;
    }

    switch (l1->tag) {
    case L_STORE:
        return l1->regexp == l2->regexp;
    case L_VALUE:
        return l1->string == l2->string;
    case L_KEY:
        return l1->regexp == l2->regexp;
    case L_LABEL:
        return l1->string == l2->string;
    case L_SEQ:
        return l1->string == l2->string;
    default:
        return 1;
    }
}

/**
 * @brief Check if ynodes are equal.
 *
 * @param[in] n1 First lense.
 * @param[in] n2 Second lense.
 * @return 1 for equal.
 */
static ly_bool
ay_ynode_equal(const struct ay_ynode *n1, const struct ay_ynode *n2)
{
    ly_bool alone1, alone2;

    assert((n1->type != YN_ROOT) && (n2->type != YN_ROOT));

    alone1 = !n1->next && (n1->parent->child == n1);
    alone2 = !n2->next && (n2->parent->child == n2);

    if (n1->descendants != n2->descendants) {
        return 0;
    } else if (n1->type != n2->type) {
        return 0;
    } else if ((!n1->label && n2->label) || (n1->label && !n2->label)) {
        return 0;
    } else if (n1->label && !ay_lnode_lense_equal(n1->label->lens, n2->label->lens)) {
        return 0;
    } else if ((!n1->value && n2->value) || (n1->value && !n2->value)) {
        return 0;
    } else if (n1->value && !ay_lnode_lense_equal(n1->value->lens, n2->value->lens)) {
        return 0;
    } else if ((!n1->snode && n2->snode) || (n1->snode && !n2->snode)) {
        return 0;
    } else if (!alone1 && !alone2 && ((!n1->choice && n2->choice) || (n1->choice && !n2->choice))) {
        return 0;
    } else if (n1->ref != n2->ref) {
        return 0;
    } else if (n1->flags != n2->flags) {
        return 0;
    }

    return 1;
}

/**
 * @brief Check if the subtrees are the same.
 *
 * @param[in] tree1 First subtree to check.
 * @param[in] tree2 Second subtree to check.
 * @param[in] compare_roots Set flag whether the roots of @p tree1 and @p tree2 will be compared.
 * @return 1 if subtrees are equal.
 */
static ly_bool
ay_ynode_subtree_equal(const struct ay_ynode *tree1, const struct ay_ynode *tree2, ly_bool compare_roots)
{
    LY_ARRAY_COUNT_TYPE i;

    if (tree1->descendants != tree2->descendants) {
        return 0;
    }

    if (compare_roots) {
        if (!ay_ynode_equal(tree1, tree2)) {
            return 0;
        }
    }

    for (i = 0; i < tree1->descendants; i++) {
        if (!ay_ynode_equal(&tree1[i + 1], &tree2[i + 1])) {
            return 0;
        }
    }

    return 1;
}

/**
 * @brief Check if the 'maybe' operator (?) is bound to the @p node.
 *
 * @param[in] node Node to check.
 * @param[in] choice_stop Stop searching if @p node is under choice.
 * @return 1 if maybe operator affects the node otherwise 0.
 */
static ly_bool
ay_lnode_has_maybe(const struct ay_lnode *node, ly_bool choice_stop)
{
    struct ay_lnode *iter;

    if (!node) {
        return 0;
    }

    for (iter = node->parent; iter && (iter->lens->tag != L_SUBTREE); iter = iter->parent) {
        if (choice_stop && (iter->lens->tag == L_UNION)) {
            return 0;
        } else if (iter->lens->tag == L_MAYBE) {
            return 1;
        }
    }

    return 0;
}

/**
 * @brief Check if the union/choice operator (|) is bound to the @p node.
 *
 * @param[in] node Node to check.
 * @return 1 if union operator affects the node otherwise 0.
 */
static ly_bool
ay_lnode_has_choice(const struct ay_lnode *node)
{
    struct ay_lnode *iter;

    if (!node) {
        return 0;
    }

    for (iter = node->parent; iter && (iter->lens->tag != L_SUBTREE); iter = iter->parent) {
        if (iter->lens->tag == L_UNION) {
            return 1;
        }
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
    char *filename;
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

/**
 * @brief Print debug information about lenses.
 *
 * @param[in] data General pointer to a node (eg lense or ynode).
 * @param[in] func Callback functions for debug printer.
 * @param[in] root_lense Lense where to begin.
 * @param[out] str_out Printed result.
 * @return 0 on success.
 */
static int
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

static int ay_print_yang_node(struct yprinter_ctx *ctx, struct ay_ynode *node);

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
 * @brief Get Augeas context from module.
 *
 * @param[in] mod Module from which the context is taken.
 * @return Augeas context.
 */
static struct augeas *
ay_get_augeas_ctx1(struct module *mod)
{
    assert(mod);
    return (struct augeas *)mod->bindings->value->info->error->aug;
}

/**
 * @brief Get Augeas context from lense.
 *
 * @param[in] lens Lense from which the context is taken.
 * @return Augeas context.
 */
static struct augeas *
ay_get_augeas_ctx2(struct lens *lens)
{
    assert(lens);
    return (struct augeas *)lens->info->error->aug;
}

/**
 * @brief Get module by the module name.
 *
 * @param[in] aug Augeas context.
 * @param[in] modname Name of the required module.
 * @return Pointer to Augeas module or NULL.
 */
static struct module *
ay_get_module(struct augeas *aug, const char *modname)
{
    struct module *mod = NULL, *mod_iter;

    LY_LIST_FOR(aug->modules, mod_iter) {
        if (!strcmp(mod_iter->name, modname)) {
            mod = mod_iter;
            break;
        }
    }

    return mod;
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

    mod = ay_get_module(ay_get_augeas_ctx2(lens), modname);
    ret = mod ? ay_get_lense_name_by_mod(mod, lens) : NULL;

    return ret;
}

/**
 * @brief Get lense name.
 *
 * It is probably not possible to find a lense name base on @p lens alone because there is no direct mapping between
 * the module path (lens->regexp->info->filename) and module name (mod->name).
 *
 * @param[in] mod Module in which search the @p lens. If it fails then it is then searched in other predefined modules.
 * @param[in] lens Lense for which to find the name.
 * @return Lense name or NULL.
 */
static char *
ay_get_lense_name(struct module *mod, struct lens *lens)
{
    static char *ret;

    if (!lens) {
        return NULL;
    }

    ret = ay_get_lense_name_by_mod(mod, lens);
    if (!ret) {
        ret = ay_get_lense_name_by_modname("Rx", lens);
    }

    return ret;
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

    mod = ay_get_module(aug, modname);
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

    mod = ay_get_module(aug, modname);
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
 * @brief Print module name.
 *
 * @param[in] mod Module whose name is to be printed.
 * @param[out] namelen Length of module name.
 * @return Module name.
 */
static char *
ay_get_yang_module_name(struct module *mod, size_t *namelen)
{
    char *name, *path;

    path = mod->bindings->value->info->filename->str;
    ay_get_filename(path, &name, namelen);

    return name;
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
 * @brief Specification where the identifier should be placed.
 */
enum ay_ident_dst {
    AY_IDENT_NODE_NAME,     /**< Identifier to be placed as name for some YANG node. */
    AY_IDENT_DATA_PATH,     /**< Identifier to be placed in the data-path. */
    AY_IDENT_VALUE_YPATH    /**< Identifier to be placed in the value-yang-path. */
};

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
    int64_t i, j, len, stop;

    stop = strlen(ident);
    for (i = 0, j = 0; i < stop; i++, j++) {
        switch (ident[i]) {
        case '@':
            j--;
            break;
        case '#':
            j--;
            break;
        case ' ':
            buffer[j] = opt == AY_IDENT_NODE_NAME ? '-' : ' ';
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
        case '_':
            if (j == 0) {
                j--;
            } else {
                buffer[j] = '-';
            }
            break;
        default:
            AY_CHECK_COND(j >= AY_MAX_IDENT_SIZE, AYE_IDENT_LIMIT);
            buffer[j] = ident[i];
        }
    }

    buffer[j] = '\0';

    if (internal) {
        memmove(buffer + 1, buffer, strlen(buffer) + 1);
        buffer[0] = '_';
    }

    return 0;
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
            strncpy(hit, replace, rplen);
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
    if (curr[1] && curr[2] && !strncmp(curr, "|()", 3)) {
        return curr;
    } else if (curr[1] && !strncmp(curr, "()", 2)) {
        return curr;
    }

    skip = curr;
    parcnt = 0;
    /* Let's skip these symbols and watch the number of parentheses. */
    do {
        old = skip;
        switch (skip[0]) {
        case '\\':
            switch (skip[1]) {
            case '$':
                skip += 2;
                break;
            }
            break;
        case '(':
            parcnt++;
            skip++;
            break;
        case ')':
            parcnt--;
            skip++;
            break;
        case '|':
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
 * @brief Print lense regex pattern to be valid for libyang.
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
    ly_bool charClassExpr;

    /* substitution of erroneous strings in lenses */
    mem = strdup(patt);
    AY_CHECK_COND(!mem, AYE_MEMORY);
    src = mem;
    ay_replace_substr(src, "\n                  ", ""); /* Rx.hostname looks wrong */
    ay_replace_substr(src, "    minclock", "minclock"); /* ntp.aug looks wrong */

    /* remove () around pattern  */
    ay_regex_remove_parentheses(&src);

    for (ch = src; *ch; ch++) {

        if ((skip = ay_regex_try_skip(ch)) != ch) {
            ch = skip - 1;
            continue;
        } else {
            skip = ch;
        }

        switch (*ch) {
        case '[':
            if ((ch[1] == '^') && (ch[2] == ']') && (ch[3] == '[')) {
                ly_print(out, "[^\\\\]\\\\[");
                skip = &ch[3];
            } else if ((ch[1] == '^') && (ch[2] == '[') && (ch[3] == ']')) {
                ly_print(out, "[^\\\\[\\\\]");
                skip = &ch[3];
            } else if ((ch[1] == '^') && (ch[2] == '[')) {
                ly_print(out, "[^\\\\[");
                skip = &ch[2];
            } else if ((ch[1] == '^') && (ch[2] == ']')) {
                ly_print(out, "[^\\\\]");
                skip = &ch[2];
            } else {
                ly_print(out, "[");
            }
            charClassExpr = 1;
            break;
        case ']':
            ly_print(out, "]");
            charClassExpr = 0;
            break;
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
            case ']':
                if (charClassExpr) {
                    /* TODO weird */
                    ly_print(out, "\\\\\\\\]");
                    skip = &ch[1];
                } else {
                    ly_print(out, "\\\\]");
                    skip = &ch[1];
                }
                break;
            case '\\':
                /* just print \\ */
                ly_print(out, "\\\\\\\\");
                skip = &ch[1];
                break;
            default:
                /* just print first \ */
                ly_print(out, "\\\\");
                break;
            }
            break;
        default:
            ly_print(out, "%c", *ch);
            break;
        }

        ch = skip;
    }

    free(mem);

    return 0;
}

/**
 * @brief Get identifier from lense->regexp->pattern in a suitable form for YANG.
 *
 * @param[in] ident Pointer to the identifier.
 * @param[in] ident_len Number of characters in @p ident.
 * @param[in] opt Where the identifier will be placed.
 * @param[out] buffer Buffer in which a valid identifier will be written.
 * @return 0 on success.
 */
static int
ay_get_ident_from_pattern_standardized(const char *ident, uint64_t ident_len, enum ay_ident_dst opt, char *buffer)
{
    int64_t i, j, stop;

    stop = ident_len;
    for (i = 0, j = 0; i < stop; i++, j++) {
        switch (ident[i]) {
        case ' ':
            if (j && (buffer[j - 1] == '-')) {
                j--;
            } else if (j == 0) {
                j--;
            } else {
                buffer[j] = opt == AY_IDENT_NODE_NAME ? '-' : ' ';
            }
            break;
        case '(':
            j--;
            break;
        case ')':
            j--;
            break;
        case '\n':
            j--;
            break;
        case '_':
            if (j == 0) {
                j--;
            } else {
                buffer[j] = '-';
            }
            break;
        default:
            AY_CHECK_COND(j >= AY_MAX_IDENT_SIZE, AYE_IDENT_LIMIT);
            buffer[j] = ident[i];
        }
    }

    buffer[j] = '\0';

    return 0;
}

/**
 * @brief Check if character is valid as part of identifier.
 *
 * @param[in] ch Character to check.
 * @return 1 for valid character, otherwise 0.
 */
static ly_bool
ay_ident_character_is_valid(const char *ch)
{
    if (((*ch < 65) && (*ch != 45) && (*ch != 32)) || ((*ch > 90) && (*ch != 95) && (*ch < 97)) || (*ch > 122)) {
        return 0;
    } else {
        /* [_- A-Za-z] */
        return 1;
    }
}

/**
 * @brief Check if pattern is so simple that can be interpreted as label.
 *
 * Function check if there is exactly one identifier in the pattern.
 * In case of detection of more identifiers, use ::ay_lense_pattern_has_idents().
 *
 * @param[in] lens Lense to check.
 * @return 1 if pattern is label.
 */
static ly_bool
ay_lense_pattern_is_label(struct lens *lens)
{
    char *ch;

    if (!lens || ((lens->tag != L_STORE) && (lens->tag != L_KEY))) {
        return 0;
    }

    for (ch = lens->regexp->pattern->str; *ch != '\0'; ch++) {
        if (!ay_ident_character_is_valid(ch)) {
            break;
        }
    }

    return *ch == '\0';
}

/**
 * @brief Get next part where should be identifier in the pattern.
 *
 * This function does not validate identifier.
 *
 * @param[in] patt Pointer to some part in the lense pattern.
 * @return Pointer to next identifier.
 */
const char *
ay_lense_pattern_next_ident(const char *patt)
{
    const char *ret;

    ret = strchr(patt, '|');
    return ret ? ret + 1 : NULL;
}

/**
 * @brief Get length of current identifier in the pattern.
 *
 * @param[in] patt Pointer to identifier located in the lense pattern.
 * @return Length of identifier.
 */
static uint64_t
ay_lense_pattern_ident_length(const char *patt)
{
    const char *next;

    if (!patt) {
        return 0;
    }

    next = ay_lense_pattern_next_ident(patt);
    return next ? (uint64_t)((next - 1) - patt) : strlen(patt);
}

/**
 * @brief Check if lense pattern does not have a fairly regular expression,
 * but rather a sequence of identifiers separated by '|'.
 *
 * @param[in] lens Lense to check.
 * @return 1 if lense contains identifiers in his pattern.
 */
static ly_bool
ay_lense_pattern_has_idents(struct lens *lens)
{
    const char *patt;

    if (!lens || (lens->tag != L_KEY)) {
        return 0;
    }

    for (patt = lens->regexp->pattern->str; *patt != '\0'; patt++) {
        if ((*patt == '|') || (*patt == '(') || (*patt == ')')) {
            continue;
        } else if (*patt == '\n') {
            /* TODO pattern is probably written wrong -> bugfix lense? */
            continue;
        } else if (!ay_ident_character_is_valid(patt)) {
            return 0;
        }
    }

    return 1;
}

/**
 * @brief Count number of identifiers in the lense pattern.
 *
 * @param[in] lens Lense to check his pattern.
 * @return Number of identifiers.
 */
static uint64_t
ay_lense_pattern_idents_count(struct lens *lens)
{
    uint64_t ret;
    const char *patt;

    if (!lens || (lens->tag != L_KEY) || !ay_lense_pattern_has_idents(lens)) {
        return 0;
    }

    ret = 1;
    patt = ay_lense_pattern_next_ident(lens->regexp->pattern->str);
    if (!patt) {
        return ret;
    }

    ret = 2;
    for (patt = ay_lense_pattern_next_ident(patt); patt; patt = ay_lense_pattern_next_ident(patt)) {
        ++ret;
    }

    return ret;
}

/**
 * @brief Find the order number of the divided node whose pattern consists of identifiers.
 *
 * It counts how many nodes with the same label precede. From this, it can then be deduced
 * which identifier in the pattern belongs to @p node.
 *
 * @param[in] node One of the node which was created due to pattern with sequence of identifiers.
 * @return Index of splitted node in the sequence of splitted nodes.
 */
static uint64_t
ay_ynode_splitted_seq_index(struct ay_ynode *node)
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

/**
 * @brief For given ynode find his identifier somewhere in the lense pattern (containing sequence of identifiers).
 *
 * @param[in] node Node for which the identifier is being searched.
 * @return Pointer to part in the pattern where identifier starts.
 */
static const char *
ay_ynode_find_ident_in_pattern(struct ay_ynode *node)
{
    uint64_t item_idx, node_idx;
    struct lens *label;
    const char *patt;

    label = AY_LABEL_LENS(node);
    if (!label || (label->tag != L_KEY)) {
        return NULL;
    } else if (*label->regexp->pattern->str == '\0') {
        return NULL;
    } else if (!node->parent) {
        return NULL;
    }

    /* find out which identifier index to look for in the pattern */
    node_idx = ay_ynode_splitted_seq_index(node);

    /* find position of identifier index */
    item_idx = 0;
    for (patt = label->regexp->pattern->str; patt; patt = ay_lense_pattern_next_ident(patt)) {
        if (item_idx != node_idx) {
            ++item_idx;
        } else {
            break;
        }
    }
    assert(item_idx == node_idx);

    return patt;
}

/**
 * @brief Store standardized @p ident located in @p patt to @p buffer.
 *
 * @param[in] patt Lense pattern.
 * @param[in] ident Identifier from @p patt to standardize.
 * @param[in] ident_len Length of @p ident.
 * @param[in] opt Where the identifier will be placed.
 * @param[out] buffer Buffer in which a valid identifier will be written.
 * @return 0 on success.
 */
static int
ay_get_ident_from_pattern(const char *patt, const char *ident, uint64_t ident_len, enum ay_ident_dst opt, char *buffer)
{
    int ret;
    const char *prefix, *suffix, *iter, *brop, *brcl;

    assert(patt && ident);

    /* skip opening bracket */
    for (iter = ident; iter < (ident + ident_len); iter++) {
        if (*ident == '(') {
            ident_len--;
            ident++;
        } else {
            break;
        }
    }
    /* close bracket delimits ident */
    for (iter = ident; iter < (ident + ident_len); iter++) {
        if (*iter == ')') {
            ident_len = iter - ident;
            break;
        }
    }

    /* Find if ident has prefix: prefix_string(...|ident|...) */
    /* find ')' */
    brcl = NULL;
    for (iter = ident; *iter; iter++) {
        if (*iter == '(') {
            /* Pattern is too complex or prefix is not available. */
            break;
        } else if (*iter == ')') {
            brcl = iter;
            break;
        }
    }
    /* find '(' */
    brop = NULL;
    for (iter = ident; iter != (patt - 1); iter--) {
        if (*iter == ')') {
            /* Pattern is too complex or prefix is not available. */
            break;
        } else if (*iter == '(') {
            brop = iter;
            break;
        }
    }
    if (brop && brcl) {
        /* ident is in "( )" */

        /* Find prefix. */
        prefix = NULL;
        for (iter = brop - 1; iter != (patt - 1); iter--) {
            if (ay_ident_character_is_valid(iter)) {
                prefix = iter;
                continue;
            } else if (*iter == '|') {
                prefix = (iter + 1) != brop ? iter + 1 : NULL;
            } else {
                break;
            }
        }

        if (prefix) {
            /* Standardize prefix. */
            ret = ay_get_ident_from_pattern_standardized(prefix, brop - prefix, opt, buffer);
            AY_CHECK_RET(ret);
            buffer = buffer + strlen(buffer);
        }
    }

    /* Standardize ident. */
    ret = ay_get_ident_from_pattern_standardized(ident, ident_len, opt, buffer);
    AY_CHECK_RET(ret);
    buffer = buffer + strlen(buffer);

    /* Find if ident has suffix: (...|ident|...)suffix_string */
    if (brop && brcl) {
        /* Find suffix. */
        suffix = (brcl + 1);
        for (iter = suffix; *iter; iter++) {
            if (!ay_ident_character_is_valid(iter)) {
                break;
            }
        }
        if (ay_ident_character_is_valid(suffix)) {
            /* Standardize suffix. */
            ret = ay_get_ident_from_pattern_standardized(suffix, iter - suffix, opt, buffer);
            AY_CHECK_RET(ret);
        }
    }

    return ret;
}

/**
 * @brief Get identifier of the ynode from the label lense.
 *
 * @param[in] node Node to process.
 * @param[out] len Length of the identifier. It is set only if the identifier is somewhere in the L_KEY pattern.
 * @return Exact identifier, location of identifier in the pattern or NULL. If the function returns location of
 * identifier in the pattern then @p len is set. The identifier should be adjusted in this case because it contains
 * invalid characters.
 */
static const char *
ay_get_yang_ident_from_label(struct ay_ynode *node, uint64_t *len)
{
    struct lens *label;
    const char *ret;

    label = AY_LABEL_LENS(node);
    if (!label) {
        return NULL;
    }

    if ((label->tag == L_LABEL) || (label->tag == L_SEQ)) {
        return label->string->str;
    } else if (ay_lense_pattern_is_label(label)) {
        return label->regexp->pattern->str;
    } else if (ay_lense_pattern_has_idents(label)) {
        ret = ay_ynode_find_ident_in_pattern(node);
        *len = ay_lense_pattern_ident_length(ret);
        return ret;
    } else {
        return NULL;
    }
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
        if (iter->type == YN_LEAFREF) {
            break;
        }
        if (iter->next) {
            break;
        }
        if (iter->snode && (str = ay_get_lense_name(ctx->mod, iter->snode->lens))) {
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
 * @brief Evaluate the identifier for the node.
 *
 * @param[in] ctx Current printing context.
 * @param[in] node Node for which the identifier is to be derived.
 * @param[in] opt Where the identifier will be placed.
 * @param[out] buffer Buffer in which the obtained identifier is written.
 * @return 0 on success.
 */
static int
ay_get_yang_ident_(struct yprinter_ctx *ctx, struct ay_ynode *node, enum ay_ident_dst opt, char *buffer)
{
    int ret = 0;
    const char *str, *tmp;
    struct lens *label, *value, *snode;
    struct ay_ynode *iter;
    uint64_t len = 0;
    ly_bool internal = 0;

    snode = AY_SNODE_LENS(node);
    label = AY_LABEL_LENS(node);
    value = AY_VALUE_LENS(node);

    /* Identifier priorities should work as follows:
     *
     * YN_CONTAINER yang-ident:
     * has_idents, lense_name(snode), lense_name(label), LABEL, SEQ, is_label, "cont"
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
            str = ay_get_lense_name(ctx->mod, snode);
            if (!str) {
                ret = ay_get_yang_ident(ctx, node->child, opt, buffer);
                AY_CHECK_RET(ret);
                if (!strcmp(buffer, "node")) {
                    str = "gr";
                } else if (node->child->choice) {
                    memmove(buffer + 3, buffer, strlen(buffer) + 1);
                    memcpy(buffer, "ch-", 3);
                    str = buffer;
                } else {
                    str = buffer;
                }
            }
        } else {
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
        strcat(buffer, "-ref");
        str = buffer;
    } else if (node->type == YN_USES) {
        for (iter = ctx->tree->child; iter; iter = iter->next) {
            if (iter->id == node->ref) {
                break;
            }
        }
        assert(iter);
        ret = ay_get_yang_ident(ctx, iter, AY_IDENT_NODE_NAME, buffer);
        AY_CHECK_RET(ret);
        str = buffer;
    } else if (node->type == YN_LIST) {
        if (node->parent->type == YN_ROOT) {
            tmp = ay_get_yang_module_name(ctx->mod, &len);
            strncpy(buffer, tmp, len);
            buffer[len] = '\0';
            len = 0;
            str = buffer;
        } else if (node->snode && (node->snode->lens->tag == L_REC)) {
            /* get identifier of node behind key */
            ret = ay_get_yang_ident(ctx, node->child, AY_IDENT_NODE_NAME, buffer);
            AY_CHECK_RET(ret);
            strcpy(buffer + strlen(buffer), "-list");
            str = buffer;
        } else if ((tmp = ay_get_lense_name(ctx->mod, label)) && strcmp(tmp, "lns")) {
            /* label can points to L_STAR lense */
            str = tmp;
        } else if (!ay_get_yang_ident_first_descendants(ctx, node, opt, buffer) && buffer[0]) {
            strcat(buffer, "-list");
            str = buffer;
        } else {
            str = "config-entries";
        }
    } else if ((node->type == YN_CONTAINER) && (opt == AY_IDENT_NODE_NAME)) {
        if (!ay_lense_pattern_is_label(label) && ay_lense_pattern_has_idents(label)) {
            str = ay_ynode_find_ident_in_pattern(node);
            len = ay_lense_pattern_ident_length(str);
        } else if ((tmp = ay_get_lense_name(ctx->mod, snode))) {
            str = tmp;
        } else if ((tmp = ay_get_lense_name(ctx->mod, label))) {
            str = tmp;
        } else if ((tmp = ay_get_yang_ident_from_label(node, &len))) {
            str = tmp;
        } else if (!node->label) {
            ret = ay_get_yang_ident(ctx, node->child, opt, buffer);
            AY_CHECK_RET(ret);
            str = buffer;
        } else {
            str = "node";
        }
    } else if ((node->type == YN_CONTAINER) && (opt == AY_IDENT_DATA_PATH)) {
        if ((tmp = ay_get_yang_ident_from_label(node, &len))) {
            str = tmp;
        } else {
            str = "$$";
        }
    } else if ((node->type == YN_CONTAINER) && (opt == AY_IDENT_VALUE_YPATH)) {
        assert(node->child && node->child->next && (node->child->next->type == YN_VALUE));
        ret = ay_get_yang_ident(ctx, node->child->next, AY_IDENT_NODE_NAME, buffer);
        return ret;
    } else if (node->type == YN_KEY) {
        if ((tmp = ay_get_yang_ident_from_label(node, &len)) && (label->tag != L_SEQ) &&
                value && (tmp = ay_get_lense_name(ctx->mod, value))) {
            str = tmp;
        } else if ((tmp = ay_get_yang_ident_from_label(node, &len))) {
            str = tmp;
        } else if ((tmp = ay_get_lense_name(ctx->mod, label))) {
            str = tmp;
        } else {
            str = "label";
        }
    } else if (node->type == YN_VALUE) {
        if ((tmp = ay_get_lense_name(ctx->mod, value))) {
            str = tmp;
        } else {
            str = "value";
        }
    } else if (((node->type == YN_LEAF) || (node->type == YN_LEAFLIST)) && (opt == AY_IDENT_NODE_NAME)) {
        if ((tmp = ay_get_yang_ident_from_label(node, &len))) {
            str = tmp;
        } else if ((tmp = ay_get_lense_name(ctx->mod, snode))) {
            str = tmp;
        } else if ((tmp = ay_get_lense_name(ctx->mod, label))) {
            str = tmp;
        } else {
            str = "node";
        }
    } else if (((node->type == YN_LEAF) || (node->type == YN_LEAFLIST)) && (opt == AY_IDENT_DATA_PATH)) {
        if ((tmp = ay_get_yang_ident_from_label(node, &len))) {
            str = tmp;
        } else {
            str = "$$";
        }
    } else if ((node->type == YN_LEAF) && (opt == AY_IDENT_VALUE_YPATH)) {
        assert(node->next && (node->next->type == YN_VALUE));
        ret = ay_get_yang_ident(ctx, node->next, AY_IDENT_NODE_NAME, buffer);
        return ret;
    } else {
        return AYE_IDENT_NOT_FOUND;
    }

    if (len) {
        ret = ay_get_ident_from_pattern(label->regexp->pattern->str, str, len, opt, buffer);
    } else if ((opt == AY_IDENT_NODE_NAME) || (opt == AY_IDENT_VALUE_YPATH)) {
        ret = ay_get_ident_standardized(str, opt, internal, buffer);
    } else {
        assert((opt == AY_IDENT_DATA_PATH) || (opt == AY_IDENT_VALUE_YPATH));
        strcpy(buffer, str);
    }

    return ret;
}

/**
 * @brief Detect for duplicates for the identifier.
 *
 * @param[in] ctx Current printing context.
 * @param[in] node Node for which the duplicates will be searched.
 * @param[in] ident Evaluated @p node identifier.
 * @param[out] dupl_rank Duplicate number for @p ident.
 * @param[out] dupl_count Number of all duplicates.
 * @return 0 on success.
 */
static int
ay_yang_ident_duplications(struct yprinter_ctx *ctx, struct ay_ynode *node, char *ident, int64_t *dupl_rank,
        uint64_t *dupl_count)
{
    int ret = 0;
    struct ay_ynode *iter;
    int64_t rnk, tmp_rnk;
    uint64_t cnt, tmp_cnt;
    char second[AY_MAX_IDENT_SIZE];

    rnk = -1;
    cnt = 0;

    for (iter = node->parent->child; iter; iter = iter->next) {
        if ((iter->type == YN_KEY) || (iter->type == YN_LEAFREF) || (iter->type == YN_USES)) {
            continue;
        } else if (iter == node) {
            rnk = cnt;
            continue;
        } else if ((iter->type == YN_CONTAINER) && !iter->label) {
            ret = ay_yang_ident_duplications(ctx, iter->child, ident, &tmp_rnk, &tmp_cnt);
            AY_CHECK_RET(ret);
            rnk = rnk == -1 ? tmp_rnk : rnk;
            cnt += tmp_cnt;
        }

        ret = ay_get_yang_ident_(ctx, iter, AY_IDENT_NODE_NAME, second);
        AY_CHECK_RET(ret);
        if (!strcmp(ident, second)) {
            cnt++;
        }
    }

    *dupl_rank = rnk;
    *dupl_count = cnt;

    return ret;
}

/**
 * @brief Evaluate unique identifier for a yang node.
 *
 * @param[in] ctx Current printing context.
 * @param[in] node Node for which the identifier is to be derived.
 * @param[in] opt Where the identifier will be placed.
 * @param[out] ident Buffer in which the obtained identifier is written.
 * @return 0 on success.
 */
static int
ay_get_yang_ident(struct yprinter_ctx *ctx, struct ay_ynode *node, enum ay_ident_dst opt, char *ident)
{
    int ret = 0;
    int64_t dupl_rank;
    uint64_t dupl_count;

    ret = ay_get_yang_ident_(ctx, node, opt, ident);
    AY_CHECK_RET(ret);

    if (opt == AY_IDENT_DATA_PATH) {
        /* Duplicate identifiers are not resolved in the data-path. */
        return ret;
    } else if (node->type == YN_USES) {
        return ret;
    }
    assert(node->parent);

    ret = ay_yang_ident_duplications(ctx, node, ident, &dupl_rank, &dupl_count);
    AY_CHECK_RET(ret);
    if (!dupl_count) {
        /* No duplicates found. */
        return ret;
    }

    /* Make duplicate identifiers unique. */
    if (node->type == YN_KEY) {
        strcpy(ident, "_id");
    } else if (dupl_rank) {
        assert(dupl_rank > 0);
        sprintf(ident + strlen(ident),  "%" PRId64, dupl_rank + 1);
    }

    return ret;
}

/**
 * @brief Print node identifier according to the yang language.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node for which the identifier will be printed.
 * @param[in] opt Where the identifier will be placed.
 * @return 0 on success.
 */
static int
ay_print_yang_ident(struct yprinter_ctx *ctx, struct ay_ynode *node, enum ay_ident_dst opt)
{
    int ret;
    char ident[AY_MAX_IDENT_SIZE];

    ret = ay_get_yang_ident(ctx, node, opt, ident);
    AY_CHECK_RET(ret);
    ly_print(ctx->out, "%s", ident);

    return ret;
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
    if (!label || (node->type == YN_VALUE) || (node->type == YN_KEY) || (node->type == YN_LIST)) {
        return ret;
    }

    ly_print(ctx->out, "%*s"AY_EXT_PREFIX ":"AY_EXT_PATH " \"", ctx->space, "");

    if ((label->tag == L_LABEL) || ay_lense_pattern_has_idents(label)) {
        ret = ay_print_yang_ident(ctx, node, AY_IDENT_DATA_PATH);
    } else {
        assert((label->tag == L_SEQ) || (label->tag == L_KEY));
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
    struct ay_ynode *valnode = NULL;
    struct lens *label, *value;

    label = AY_LABEL_LENS(node);
    value = AY_VALUE_LENS(node);

    if ((node->type == YN_CONTAINER) && !label) {
        return ret;
    } else if ((node->type == YN_LIST) || (node->type == YN_KEY) || (node->type == YN_VALUE)) {
        return ret;
    } else if (!value) {
        return ret;
    } else if ((node->type == YN_LEAF) && label && ((label->tag == L_LABEL) || ay_lense_pattern_has_idents(label))) {
        return ret;
    }

    ly_print(ctx->out, "%*s"AY_EXT_PREFIX ":"AY_EXT_VALPATH " \"", ctx->space, "");

    if ((node->type == YN_CONTAINER) && ((label->tag == L_LABEL) || ay_lense_pattern_has_idents(label))) {
        valnode = node->child;
    } else if (node->type == YN_CONTAINER) {
        valnode = node->child->next;
    } else {
        assert(node->type == YN_LEAF);
        valnode = node->next;
    }
    assert(valnode);
    ret = ay_print_yang_ident(ctx, valnode, AY_IDENT_VALUE_YPATH);
    ly_print(ctx->out, "\";\n");

    return ret;
}

static void
ay_print_yang_minelements(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    if (node->flags & AY_YNODE_MAND_TRUE) {
        ly_print(ctx->out, "%*smin-elements 1;\n", ctx->space, "");
    }
}

/**
 * @brief For ay_lnode_next_lv(), find next label or value.
 */
#define AY_LV_TYPE_ANY   0

/**
 * @brief For ay_lnode_next_lv(), find next value.
 */
#define AY_LV_TYPE_VALUE 1

/**
 * @brief For ay_lnode_next_lv(), find next label.
 */
#define AY_LV_TYPE_LABEL 2

/**
 * @brief Get next label/value.
 *
 * Next in order may be found if they are separated by the '|' operator (L_UNION).
 * For example: store lns1 | store lns2.
 *
 * @param[in] lv Node whose lense tag is L_LABEL or L_VALUE. The point from which to look further.
 * @param[in] lv_type Flag specifying the type to search for. See AY_LV_TYPE_* constants.
 * @return Follower or NULL.
 */
static const struct ay_lnode *
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
                ((lv_type == AY_LV_TYPE_ANY) && (AY_TAG_IS_VALUE(tag) || AY_TAG_IS_VALUE(tag)))) {
            return iter;
        }
    }

    return NULL;
}

/**
 * @brief Check if "type empty;" should be printed.
 *
 * @param[in] lnode Node to check.
 * @return 1 if type empty is in the @p lnode.
 */
static ly_bool
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

    if ((lens->tag != L_KEY) && (lens->tag != L_STORE)) {
        return 0;
    }

    rpstr = lens->regexp->pattern->str;
    rplen = strlen(rpstr);
    if (rplen < patlen) {
        return 0;
    }
    return strncmp(rpstr + rplen - patlen, patstr, patlen) == 0;
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
 * @brief Print type-stmt string and also pattern-stmt if necessary.
 *
 * @param[in] ctx Context for printing.
 * @param[in] lens Lense used to print type. It can be NULL.
 * @return 0 on success.
 */
static int
ay_print_yang_type_string(struct yprinter_ctx *ctx, struct lens *lens)
{
    int ret = 0;

    if (!lens) {
        ly_print(ctx->out, "%*stype string;\n", ctx->space, "");
        return ret;
    }

    ly_print(ctx->out, "%*stype string", ctx->space, "");
    ay_print_yang_nesting_begin(ctx);

    if (lens->tag == L_VALUE) {
        ly_print(ctx->out, "%*spattern \"%s\";\n", ctx->space, "", lens->string->str);
    } else {
        assert((lens->tag == L_KEY) || (lens->tag == L_STORE));
        ly_print(ctx->out, "%*spattern \"", ctx->space, "");
        ay_print_regex_standardized(ctx->out, lens->regexp->pattern->str);
        ly_print(ctx->out, "\";\n");
    }

    ay_print_yang_nesting_end(ctx);

    return ret;
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
    const char *ident = NULL, *type;
    char *filename = NULL;
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
 * @param[in] item Lense containing the type.
 * @return 0 if print was successful otherwise nothing is printed.
 */
static int
ay_print_yang_type_item(struct yprinter_ctx *ctx, struct lens *item)
{
    int ret;

    ret = ay_print_yang_type_builtin(ctx, item);
    if (ret) {
        /* The builtin print failed, so print just string pattern. */
        if (item->tag == L_VALUE) {
            ret = ay_print_yang_enumeration(ctx, item);
        } else {
            ret = ay_print_yang_type_string(ctx, item);
        }
    }

    return ret;
}

/**
 * @brief Print yang union-stmt types.
 *
 * @param[in] ctx Context for printing.
 * @param[in] key Key fo type lnode from the dnode dictionary. The key and its values as union types will be printed.
 * @return 0 on success.
 */
static int
ay_print_yang_type_union_items(struct yprinter_ctx *ctx, struct ay_dnode *key)
{
    int ret = 0;
    uint64_t i;
    struct lens *item;

    assert(AY_DNODE_IS_KEY(key));

    /* Print dnode KEY. */
    ret = ay_print_yang_type_item(ctx, key->lkey->lens);

    /* Print dnode KEY'S VALUES. */
    AY_DNODE_VAL_FOR(key, i) {
        item = key[i].lval->lens;
        assert((item->tag == L_STORE) || (item->tag == L_KEY) || (item->tag == L_VALUE));
        ret = ay_print_yang_type_item(ctx, item);
        AY_CHECK_RET(ret);
    }

    return ret;
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
    ly_bool empty_string = 0, empty_type = 0;
    uint64_t i;

    if (!node->label && !node->value) {
        return ret;
    }

    label = AY_LABEL_LENS(node);
    value = AY_VALUE_LENS(node);
    if (node->type == YN_VALUE) {
        lnode = node->value;
        lv_type = AY_LV_TYPE_VALUE;
    } else if (ay_lense_pattern_has_idents(label) && value) {
        lnode = node->value;
        lv_type = AY_LV_TYPE_VALUE;
    } else if ((node->type == YN_LEAF) && ay_lense_pattern_has_idents(label) && !value) {
        ly_print(ctx->out, "%*stype empty;\n", ctx->space, "");
        return ret;
    } else if (label && (label->tag == L_KEY)) {
        lnode = node->label;
        lv_type = AY_LV_TYPE_LABEL;
    } else if (value && (value->tag == L_STORE)) {
        lnode = node->value;
        lv_type = AY_LV_TYPE_VALUE;
    } else if (label && (label->tag == L_LABEL) && !value) {
        ly_print(ctx->out, "%*stype empty;\n", ctx->space, "");
        return ret;
    } else {
        ret = ay_print_yang_type_string(ctx, NULL);
        return ret;
    }
    assert(lnode);

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
            if (empty_string && empty_type) {
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
    }

    if (empty_type && (node->type == YN_VALUE) && (node->flags & AY_YNODE_MAND_FALSE)) {
        empty_type = 0;
    }

    /* Print union */
    if (empty_string || empty_type || key) {
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
        ay_print_yang_type_union_items(ctx, key);
    } else {
        /* Print lnode type. */
        ret = ay_print_yang_type_item(ctx, lnode->lens);
    }

    /* End of union. */
    if (empty_string || empty_type || key) {
        ay_print_yang_nesting_end(ctx);
    }

    return ret;
}

/**
 * @brief Print yang config-stmt.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node to process.
 */
static void
ay_print_yang_config(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    if (node->flags & AY_CONFIG_FALSE) {
        ly_print(ctx->out, "%*sconfig false;\n", ctx->space, "");
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
    ay_print_yang_config(ctx, node);
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
    if (node->flags & AY_YNODE_MAND_TRUE) {
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
    ay_print_yang_config(ctx, node);
    ret = ay_print_yang_data_path(ctx, node);
    AY_CHECK_RET(ret);
    ret = ay_print_yang_value_path(ctx, node);

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

    ly_print(ctx->out, "%*sdescription\n", ctx->space, "");
    ly_print(ctx->out, "%*s\"Implicitly generated leaf to maintain recursive augeas data.\";\n",
            ctx->space + SPACE_INDENT, "");
    ay_print_yang_config(ctx, node);
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

    ly_print(ctx->out, "%*sleaf ", ctx->space, "");

    ret = ay_print_yang_ident(ctx, node, AY_IDENT_NODE_NAME);
    AY_CHECK_RET(ret);
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
    ay_print_yang_config(ctx, node);
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
    ay_print_yang_config(ctx, node);
    ay_print_yang_nesting_end(ctx);

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
    ay_print_yang_config(ctx, node);
    if (is_lrec) {
        ly_print(ctx->out, "%*sleaf _r-id", ctx->space, "");
    } else {
        ly_print(ctx->out, "%*sordered-by user;\n", ctx->space, "");
        ly_print(ctx->out, "%*sleaf _id", ctx->space, "");
    }
    ay_print_yang_nesting_begin(ctx);
    ly_print(ctx->out, "%*stype uint64;\n", ctx->space, "");
    ly_print(ctx->out, "%*sdescription\n", ctx->space, "");

    if (is_lrec) {
        ly_print(ctx->out, "%*s\"Implicitly generated list key to maintain the recursive augeas data.\";\n",
                ctx->space + SPACE_INDENT, "");
    } else {
        ly_print(ctx->out, "%*s\"Implicitly generated list key to maintain the order of the augeas data.\";\n",
                ctx->space + SPACE_INDENT, "");
    }
    ay_print_yang_nesting_end(ctx);

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
 * @brief Print yang presence-stmt.
 *
 * @param[in] ctx Context for printing.
 * @param[in] cont Node of type YN_CONTAINER.
 */
static void
ay_print_yang_presence(struct yprinter_ctx *ctx, struct ay_ynode *cont)
{
    (void) cont;
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
    ay_print_yang_config(ctx, node);
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
        ret = ay_print_yang_children(ctx, node);
        break;
    case YN_ROOT:
        ret = ay_print_yang_children(ctx, node);
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

    assert(node->parent);

    /* Taking care of duplicate choice names. */
    choice_cnt = 1;
    last_choice = NULL;
    for (iter = node->parent->child; iter != node; iter = iter->next) {
        if (iter->choice && (iter->choice != node->choice) && (last_choice != iter->choice)) {
            choice_cnt++;
            last_choice = iter->choice;
        }
    }

    if (node->parent->type == YN_GROUPING) {
        ly_print(ctx->out, "%*schoice ", ctx->space, "");
    } else {
        ly_print(ctx->out, "%*schoice ch-", ctx->space, "");
    }
    ret = ay_print_yang_ident(ctx, node->parent, AY_IDENT_NODE_NAME);
    AY_CHECK_RET(ret);

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
        ret = ay_print_yang_ident(ctx, node->child, AY_IDENT_NODE_NAME);
    } else {
        ret = ay_print_yang_ident(ctx, node, AY_IDENT_NODE_NAME);
    }

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

    if (((node->type == YN_CONTAINER) && !node->label) || (node->type == YN_USES)) {
        if (!alone) {
            ret = ay_print_yang_case(ctx, node);
            ay_print_yang_nesting_begin2(ctx, node->id);
            AY_CHECK_RET(ret);
        }

        if ((node->type == YN_CONTAINER) && !node->label) {
            /* Ignore container, print only children of container. */
            ret = ay_print_yang_children(ctx, node);
        } else {
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

    if (alone) {
        /* choice with one 'case' is not printed */
        ret = ay_print_yang_node_in_choice(ctx, node, alone);
    } else if (first && !last) {
        /* print choice */
        ay_print_yang_choice(ctx, node);
        /* start of choice nesting */
        ay_print_yang_nesting_begin(ctx);
        ay_print_yang_mandatory_choice(ctx, node);
        ret = ay_print_yang_node_in_choice(ctx, node, alone);
    } else if (!last) {
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
 * @brief Print ynode tree in yang format.
 *
 * @param[in] mod Module in which the tree is located.
 * @param[in] tree Ynode tree to print.
 * @param[in] vercode Decide if debugging information should be printed.
 * @param[out] str_out Printed tree in yang format. Call free() after use.
 * @return 0 on success.
 */
static int
ay_print_yang(struct module *mod, struct ay_ynode *tree, uint64_t vercode, char **str_out)
{
    int ret;
    struct yprinter_ctx ctx;
    struct ly_out *out;
    char *str, *modname;
    size_t i, modname_len;

    if (ly_out_new_memory(&str, 0, &out)) {
        return AYE_MEMORY;
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

    ly_print(out, "  namespace \"aug:");
    for (i = 0; i < modname_len; i++) {
        ly_print(out, "%c", modname[i] == '_' ? '-' : modname[i]);
    }
    ly_print(out, "\";\n");

    ly_print(out, "  prefix aug;\n\n");
    ly_print(out, "  import augeas-extension {\n");
    ly_print(out, "    prefix " AY_EXT_PREFIX ";\n");
    ly_print(out, "  }\n\n");
    ly_print(out, "  " AY_EXT_PREFIX ":augeas-mod-name \"%s\";\n", mod->name);
    ly_print(out, "\n");

    ret = ay_print_yang_node(&ctx, tree);

    ly_print(out, "}\n");
    ly_out_free(out, NULL, 0);

    *str_out = str;

    return ret;
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
 * @brief Print additional information about ynode.
 *
 * @param[in] ctx Context for printing. The lprinter_ctx.data is type of ynode.
 */
static void
ay_print_ynode_extension(struct lprinter_ctx *ctx)
{
    struct ay_ynode *node;

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

    ly_print(ctx->out, " (id: %" PRIu32 ")\n", node->id);

    if (node->type == YN_REC) {
        ly_print(ctx->out, "%*s snode_id: %p\n", ctx->space, "", node->snode);
    }

    if (node->choice) {
        ly_print(ctx->out, "%*s choice_id: %p\n", ctx->space, "", node->choice);
    }

    if (node->ref) {
        ly_print(ctx->out, "%*s ref_id: %" PRIu32 "\n", ctx->space, "", node->ref);
    }

    if (node->flags & AY_YNODE_MAND_TRUE) {
        if ((node->type == YN_LIST) || (node->type == YN_LEAFLIST)) {
            ly_print(ctx->out, "%*s min-elements: 1\n", ctx->space, "");
        } else {
            ly_print(ctx->out, "%*s mandatory: true\n", ctx->space, "");
        }
    }
    if (node->flags & AY_CHOICE_MAND_FALSE) {
        ly_print(ctx->out, "%*s flag: choice_mand_false\n", ctx->space, "");
    }
    if (node->flags & AY_VALUE_IN_CHOICE) {
        ly_print(ctx->out, "%*s flag: value_in_choice\n", ctx->space, "");
    }
}

/**
 * @brief Test if lnode tree matches lense tree.
 *
 * @param[in] vercode Verbose that decides the execution of a function.
 * @param[in] mod Module in which the trees are located.
 * @param[in] tree Tree of lnodes to check by print functions.
 * @return 0 on success.
 */
static int
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

/**
 * @brief Test if ynode forest matches lense tree.
 *
 * @param[in] vercode Verbose that decides the execution of a function.
 * @param[in] mod Module in which the trees are located.
 * @param[in] yforest Forest of ynodes to check by print functions.
 * @return 0 on success.
 */
static int
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
 * @brief Print ynode tree in gdb.
 *
 * This function is useful for storing the ynode tree to the GDB Value history.
 *
 * @param[in] tree Tree of ynodes to print.
 * @return Printed ynode tree.
 */
__attribute__((unused))
static char *
ay_gdb_lptree(struct ay_ynode *tree)
{
    char *str1;
    struct lprinter_ctx_f print_func = {0};

    print_func.transition = ay_print_ynode_transition_lv;
    print_func.extension = ay_print_ynode_extension;
    ay_print_lens(tree, &print_func, NULL, &str1);

    return str1;
}

/**
 * @brief Print ynode tree.
 *
 * @param[in] vercode Verbose that decides the execution of a function.
 * @param[in] vermask Bitmask for vercode to decide if result should be printed to stdout.
 * @param[in] tree Tree of ynodes to check by print functions.
 * @return 0 on success.
 */
static int
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

int
augyang_print_input_lenses(struct module *mod, char **str)
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

    LY_ARRAY_INCREMENT(root);
    node->lens = lens;

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
 * @brief Create basic ynode forest from lnode tree.
 *
 * Only ay_ynode.snode and ay_ynode.descendants are set.
 *
 * @param[out] ynode Tree of ynodes as destination.
 * @param[in] lnode Tree of lnodes as source.
 */
static void
ay_ynode_create_forest_(struct ay_ynode *ynode, struct ay_lnode *lnode)
{
    uint32_t id = 1;
    struct ay_lnode *child;

    for (uint32_t i = 0, j = 0; i < lnode->descendants; i++) {
        if ((lnode[i].lens->tag == L_SUBTREE) || (lnode[i].lens->tag == L_REC)) {
            LY_ARRAY_INCREMENT(ynode);
            ynode[j].type = lnode[i].lens->tag == L_REC ? YN_REC : YN_UNKNOWN;
            ynode[j].snode = &lnode[i];
            ynode[j].descendants = 0;
            ynode[j].id = id++;
            for (uint32_t k = 0; k < lnode[i].descendants; k++) {
                child = &lnode[i + 1 + k];
                if ((child->lens->tag == L_SUBTREE) || (child->lens->tag == L_REC)) {
                    ynode[j].descendants++;
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

    LY_ARRAY_FOR(forest, i) {
        if (!forest[i].parent) {
            last = &forest[i];
            forest[i].next = last->descendants ? last + last->descendants + 1 : last + 1;
        }
    }
    last->next = NULL;
}

/**
 * @brief Set label and value to all ynodes in the forest.
 *
 * @param[in,out] forest Forest of ynodes.
 */
static void
ay_ynode_add_label_value(struct ay_ynode *forest)
{
    struct ay_ynode *ynode;
    const struct ay_lnode *lnode;
    enum lens_tag tag;

    LY_ARRAY_FOR(forest, struct ay_ynode, ynode) {
        for (uint32_t j = 0; j < ynode->snode->descendants; j++) {
            lnode = &ynode->snode[j + 1];
            tag = lnode->lens->tag;
            if (tag == L_SUBTREE) {
                /* skip - this subtree will be processed later */
                j += lnode->descendants;
            } else if (!ynode->label && AY_TAG_IS_LABEL(tag)) {
                ynode->label = lnode;
            } else if (!ynode->value && AY_TAG_IS_VALUE(value)) {
                ynode->value = lnode;
            }
        }
    }
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
 * @brief Create forest of ynodes from lnode tree.
 *
 * @param[in] ltree Tree of lnodes.
 * @param[out] yforest Forest of ynodes.
 */
static void
ay_ynode_create_forest(struct ay_lnode *ltree, struct ay_ynode *yforest)
{
    ay_ynode_create_forest_(yforest, ltree);
    ay_ynode_tree_correction(yforest);
    ay_ynode_forest_connect_topnodes(yforest);
    ay_ynode_add_label_value(yforest);
    ay_ynode_add_choice(yforest);
}

/**
 * @brief Copy ynodes to new location.
 *
 * @param[out] dst Destination where are ynode copied.
 * @param[in] src Source where the ynodes are copied from.
 */
static void
ay_ynode_copy(struct ay_ynode *dst, struct ay_ynode *src)
{
    LY_ARRAY_COUNT_TYPE i;

    LY_ARRAY_FOR(src, i) {
        dst[i] = src[i];
        dst[i].parent = AY_MAP_ADDRESS(dst, src, src[i].parent);
        dst[i].next = AY_MAP_ADDRESS(dst, src, src[i].next);
        dst[i].child = AY_MAP_ADDRESS(dst, src, src[i].child);
        LY_ARRAY_INCREMENT(dst);
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
 * @brief Create ynode tree from ynode forest.
 *
 * @param[in] forest Forest of ynodes.
 * @param[in] ltree Tree of lnodes (Sized array). If function succeeds then the ownership of memory is moved to @tree,
 * so the memory of ltree should be therefore released by ::ay_ynode_tree_free().
 * @param[out] tree Resulting ynode tree (new dynamic memory is allocated).
 * @return 0 on success.
 */
static int
ay_ynode_create_tree(struct ay_ynode *forest, struct ay_lnode *ltree, struct ay_ynode **tree)
{
    LY_ARRAY_COUNT_TYPE i;
    struct ay_ynode *iter;
    uint32_t forest_count;
    uint64_t labcount, valcount;
    enum lens_tag tag;

    forest_count = LY_ARRAY_COUNT(forest);

    LY_ARRAY_CREATE(NULL, *tree, 1 + forest_count, return AYE_MEMORY);
    if (!forest_count) {
        return 0;
    }

    ay_ynode_copy(*tree, forest);
    ay_ynode_shift_right(*tree);
    (*tree)->child = (*tree) + 1;
    (*tree)->type = YN_ROOT;

    for (iter = *tree + 1; iter; iter = iter->next) {
        iter->parent = *tree;
        (*tree)->descendants += iter->descendants + 1;
    }

    /* Set YN_ROOT members. */
    AY_YNODE_ROOT_ARRSIZE(*tree) = LY_ARRAY_COUNT(*tree);
    assert(AY_YNODE_ROOT_ARRSIZE(*tree) == ((*tree)->descendants + 1));
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
        LY_ARRAY_CREATE(NULL, AY_YNODE_ROOT_LABELS(*tree), labcount, return AYE_MEMORY);
    }
    /* Set values. */
    if (valcount) {
        LY_ARRAY_CREATE(NULL, AY_YNODE_ROOT_VALUES(*tree), valcount, return AYE_MEMORY);
    }
    /* Set idcnt. */
    AY_YNODE_ROOT_IDCNT(*tree) = ((*tree) + (*tree)->descendants)->id + 1;
    /* Set ltree. Note that this set must be the last operation before return. */
    AY_YNODE_ROOT_LTREE(*tree) = ltree;

    return 0;
}

/**
 * @brief Get repetition lense (* or +) bound to the @p node.
 *
 * @param[in] node Node to search.
 * @return Pointer to lnode or NULL.
 */
static const struct ay_lnode *
ay_ynode_get_repetition(struct ay_ynode *node)
{
    const struct ay_lnode *ret = NULL, *liter, *lstart, *lstop;
    struct ay_ynode *yiter;

    if (!node) {
        return ret;
    }

    for (yiter = node; yiter && !yiter->snode; yiter = yiter->parent) {}
    lstart = yiter && (yiter->type != YN_ROOT) ? yiter->snode : NULL;

    if (!lstart) {
        return ret;
    }

    for (yiter = node->parent; yiter && !yiter->snode; yiter = yiter->parent) {}
    lstop = yiter && (yiter->type != YN_ROOT) ? yiter->snode : NULL;

    for (liter = lstart; liter != lstop; liter = liter->parent) {
        if (liter->lens->tag == L_STAR) {
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
ay_ynode_rule_list(struct ay_ynode *node)
{
    ly_bool has_value;
    struct lens *label;

    label = AY_LABEL_LENS(node);
    has_value = label && ((label->tag == L_KEY) || (label->tag == L_SEQ)) && node->value;
    return (node->child || has_value) && label && ay_ynode_get_repetition(node);
}

/**
 * @brief YN_CONTAINER detection rule.
 *
 * @param[in] node Node to check.
 * @return 1 if ynode is of type YN_CONTAINER.
 */
static ly_bool
ay_ynode_rule_container(struct ay_ynode *node)
{
    ly_bool has_value;
    struct lens *label;

    label = AY_LABEL_LENS(node);
    has_value = label && ((label->tag == L_KEY) || (label->tag == L_SEQ)) && node->value;
    return (node->child || has_value) && label && !ay_ynode_get_repetition(node);
}

/**
 * @brief YN_LEAFLIST detection rule.
 *
 * @param[in] node Node to check.
 * @return 1 if ynode is of type YN_LEAFLIST.
 */
static ly_bool
ay_ynode_rule_leaflist(struct ay_ynode *node)
{
    return !node->child && node->label && ay_ynode_get_repetition(node);
}

/**
 * @brief YN_LEAF detection rule.
 *
 * @param[in] node Node to check.
 * @return 1 if ynode is of type YN_LEAF.
 */
static ly_bool
ay_ynode_rule_leaf(struct ay_ynode *node)
{
    return !node->child && node->label;
}

/**
 * @brief Rule to insert key node.
 *
 * @param[in] node Node to check.
 * @return Number of nodes that should be inserted as a key.
 */
static uint64_t
ay_ynode_rule_cont_key(struct ay_ynode *node)
{
    struct lens *label, *value;

    label = AY_LABEL_LENS(node);
    value = AY_VALUE_LENS(node);
    if ((node->type != YN_CONTAINER) || !label) {
        return 0;
    } else if ((label->tag == L_LABEL) || ay_lense_pattern_is_label(label) || ay_lense_pattern_has_idents(label)) {
        return value ? 1 : 0;
    } else if (label->tag == L_SEQ) {
        return value ? 2 : 1;
    } else {
        assert(label->tag == L_KEY);
        return value ? 2 : 1;
    }
}

/**
 * @brief Rule for inserting container which must wrap some nodes due to the choice statement.
 *
 * @param[in] node Node to check.
 * @return 1 if container should be inserted. Also the function returns 1 only for the first node in the wrap.
 */
static uint64_t
ay_ynode_rule_insert_container(struct ay_ynode *node)
{
    const struct ay_lnode *lter, *conc1, *conc2;
    enum lens_tag tag;

    if (!node->choice || !node->snode || !node->next || !node->next->snode || !node->next->choice) {
        return 0;
    } else if (node->choice != node->next->choice) {
        return 0;
    }

    /* L_CONCAT must be between L_SUBTREE and L_UNION */
    conc1 = NULL;
    for (lter = node->snode->parent; lter; lter = lter->parent) {
        tag = lter->lens->tag;
        if (tag == L_SUBTREE) {
            break;
        } else if (node->choice == lter) {
            break;
        } else if (tag == L_CONCAT) {
            conc1 = lter;
        }
    }

    conc2 = NULL;
    for (lter = node->next->snode->parent; lter; lter = lter->parent) {
        tag = lter->lens->tag;
        if (tag == L_SUBTREE) {
            break;
        } else if (node->choice == lter) {
            break;
        } else if (tag == L_CONCAT) {
            conc2 = lter;
        }
    }

    return conc1 && conc2 && (conc1 == conc2);
}

/**
 * @brief Rule for unification of containers or lists which have the same key.
 *
 * @param[in] node Node to check.
 * @return 1 unification should take place and container can be inserted.
 */
static uint64_t
ay_ynode_rule_conlist_with_same_key(struct ay_ynode *node)
{
    struct ay_ynode *iter;
    struct lens *lab1, *lab2;

    if (!node->parent || ((node->type != YN_LIST) && (node->type != YN_CONTAINER)) || (!node->choice) || !node->next) {
        return 0;
    }

    for (iter = node->parent->child; iter; iter = iter->next) {
        if (iter->choice == node->choice) {
            break;
        }
    }
    if (iter != node) {
        return 0;
    }
    /* The node is first list/container in choice statement. */

    lab1 = AY_LABEL_LENS(node);
    lab2 = AY_LABEL_LENS(node->next);
    if (((node->next->type == YN_LIST) || (node->next->type == YN_CONTAINER)) &&
            (node->choice == node->next->choice) && lab1 && lab2 && (lab1->tag == lab2->tag)) {
        if ((lab1->tag == L_LABEL) && (!strcmp(lab1->string->str, lab2->string->str))) {
            return 1;
        } else if ((lab1->tag == L_SEQ) && (!strcmp(lab1->string->str, lab2->string->str))) {
            return 1;
        } else if ((lab1->tag == L_KEY) && (!strcmp(lab1->regexp->pattern->str, lab2->regexp->pattern->str))) {
            return 1;
        } else {
            return 0;
        }
    } else {
        return 0;
    }
}

/**
 * @brief Check whether list should be splitted based on pattern which consists of sequence of identifiers.
 *
 * @param[in] node to check.
 * @return 0 or unsigned integer greater than 1 if node must be splitted.
 */
static uint64_t
ay_ynode_rule_node_split(struct ay_ynode *node)
{
    struct lens *label;
    uint64_t count;

    label = AY_LABEL_LENS(node);

    if (!label || (label->tag != L_KEY)) {
        return 0;
    } else if ((node->type != YN_LIST) && (node->type != YN_LEAFLIST) && (node->type != YN_CONTAINER)) {
        return 0;
    }

    if ((count = ay_lense_pattern_idents_count(label)) && (count > 1)) {
        if (node->descendants) {
            /* For first identifier in pattern (0), for every other identifiers in pattern (+n),
             * One grouping node and YN_USES node for every identifier in pattern.
             */
            return (count - 1) + 1 + count;
        } else {
            /* For first identifier in pattern (0), for every other identifiers in pattern (+n). */
            return count - 1;
        }
    } else {
        return 0;
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
 * @param[in] tree Tree of ynodes.
 * @return Number of nodes to insert.
 */
static uint64_t
ay_ynode_rule_recursive_form(const struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i, j;
    const struct ay_ynode *rec_ext, *iter;
    uint64_t ret = 0, rec_int_count;

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        rec_ext = &tree[i];
        if (rec_ext->type != YN_REC) {
            continue;
        }
        rec_int_count = 0;
        for (j = 0; j < rec_ext->descendants; j++) {
            iter = &rec_ext[j + 1];
            if (iter->type == YN_REC) {
                rec_int_count++;
            }
        }
        ret += rec_ext->descendants * (rec_int_count + 1);
    }

    return ret;
}

/**
 * @brief Rule decide how many nodes will be inserted for grouping.
 *
 * @param[in] node to check.
 * @return Number of nodes to insert.
 */
static uint64_t
ay_ynode_rule_create_groupings_toplevel(struct ay_ynode *node)
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
 * @brief Test ay_ynode_copy().
 *
 * @param[in] vercode Verbose that decides the execution of a function.
 * @param[in] forest Forest of ynodes.
 * @return 0 on success.
 */
static int
ay_test_ynode_copy(uint64_t vercode, struct ay_ynode *forest)
{
    int ret = 0;
    char *str1, *str2;
    struct ay_ynode *dupl = NULL;
    struct lprinter_ctx_f print_func = {0};

    if (!vercode) {
        return ret;
    }

    LY_ARRAY_CREATE(NULL, dupl, LY_ARRAY_COUNT(forest), return AYE_MEMORY);
    ay_ynode_copy(dupl, forest);

    print_func.main = ay_print_ynode_main;
    print_func.transition = ay_print_ynode_transition;
    ret = ay_print_lens(forest, &print_func, forest->snode->lens, &str1);
    AY_CHECK_RET(ret);

    ret = ay_print_lens(dupl, &print_func, dupl->snode->lens, &str2);
    AY_CHECK_RET(ret);

    ret = ay_test_compare("ynode copy", str1, str2);
    free(str1);
    free(str2);
    LY_ARRAY_FREE(dupl);

    return ret;
}

/**
 * @brief Insert gap in the array.
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
    memmove(&tree[index], &tree[index + 1], (LY_ARRAY_COUNT(tree) - index - 1) * sizeof *tree);
    memset(tree + LY_ARRAY_COUNT(tree) - 1, 0, sizeof *tree);
    LY_ARRAY_DECREMENT(tree);
}

/**
 * @brief Delete node from the tree.
 *
 * Children of deleted node are moved up in the tree level. Member ay_ynode.choice is set on children.
 * Under certain conditions, the node is not deleted, but instead is cast to a container,
 * which is later overwritten in case-stmt.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in] node The ynode to be deleted.
 * @return 0 if node was deleted or 1 if node changed type to YN_CONTAINER.
 */
static ly_bool
ay_ynode_delete_node(struct ay_ynode *tree, struct ay_ynode *node)
{
    struct ay_ynode *iter, *parent;
    uint32_t index;
    ly_bool cast_cont;

    if (node->choice && node->child && node->child->next) {
        /* Choice setting for children. */
        /* Cast @p node to container if all children have choice set. */
        cast_cont = 0;
        for (iter = node->child; iter; iter = iter->next) {
            if (!iter->choice) {
                cast_cont = 1;
            }
        }
        if (cast_cont) {
            /* Just cast to container (case-stmt). */
            node->type = YN_CONTAINER;
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
 * @param[in] delete_root Setting the flag deletes the root.
 */
static void
ay_ynode_delete_subtree(struct ay_ynode *tree, struct ay_ynode *subtree, ly_bool delete_root)
{
    struct ay_ynode *iter;
    uint32_t deleted_nodes;
    uint32_t index, i;

    if (delete_root) {
        deleted_nodes = subtree->descendants + 1;
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

    for (i = 0; i < deleted_nodes; i++) {
        ay_ynode_delete_gap(tree, index);
    }

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
 * @brief Move subtree to another place.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in] dst Index of the place where the subtree is moved. Gaps are inserted on this index.
 * @param[in] src Index to the root of subtree. Gap is deleted after the move.
 */
static void
ay_ynode_move_subtree(struct ay_ynode *tree, uint32_t dst, uint32_t src)
{
    uint32_t subtree_size;
    struct ay_ynode node;

    if (dst == src) {
        return;
    }

    subtree_size = tree[src].descendants + 1;
    for (uint32_t i = 0; i < subtree_size; i++) {
        node = tree[src];
        ay_ynode_delete_gap(tree, src);
        dst = dst > src ? dst - 1 : dst;
        ay_ynode_insert_gap(tree, dst);
        src = src > dst ? src + 1 : src;
        tree[dst] = node;
        dst++;
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
 * @param[in] dst Index of the place where the subtree is copied. Gaps are inserted on this index.
 * @param[in] src Index to the root of subtree.
 */
static void
ay_ynode_copy_subtree(struct ay_ynode *tree, uint32_t dst, uint32_t src)
{
    uint32_t subtree_size;
    struct ay_ynode node;

    if (dst == src) {
        return;
    }

    subtree_size = tree[src].descendants + 1;
    for (uint32_t i = 0; i < subtree_size; i++) {
        node = tree[src];
        ay_ynode_insert_gap(tree, dst);
        src = src > dst ? src + 1 : src;
        ay_ynode_copy_data(&tree[dst], &node);
        dst++;
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
    struct ay_ynode *iter, *last;
    uint32_t subtree_size;

    for (last = dst->child; last && last->next; last = last->next) {}
    if (last == src) {
        return;
    }

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
}

/**
 * @brief Set ynode.mandatory for node.
 *
 * @param[in,out] node Node whose mandatory statement will be set.
 */
static void
ay_ynode_set_mandatory(struct ay_ynode *node)
{
    if (ay_lnode_has_maybe(node->snode, 0)) {
        node->flags &= ~AY_YNODE_MAND_MASK;
        node->flags |= AY_YNODE_MAND_FALSE;
    } else {
        node->flags &= ~AY_YNODE_MAND_MASK;
        node->flags = AY_YNODE_MAND_TRUE;
    }
}

/**
 * @brief Set AY_CHOICE_MAND_FALSE to ynode.flag for every node in the tree.
 *
 * @param[in,out] tree Tree of ynodes.
 */
static void
ay_ynode_set_flag(struct ay_ynode *tree)
{
    struct ay_ynode *iter, *first;
    LY_ARRAY_COUNT_TYPE i;

    /* Set AY_CHOICE_MAND_FALSE. */
    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        iter = &tree[i];

        if (iter->label && ay_lense_pattern_has_idents(iter->label->lens)) {
            if (iter->snode && ay_lnode_has_maybe(iter->snode, 0)) {
                iter->flags |= AY_CHOICE_MAND_FALSE;
            }
            continue;
        }

        first = ay_ynode_get_first_in_choice(iter->parent, iter->choice);
        if (first != iter) {
            continue;
        }

        if (first->snode && ay_lnode_has_maybe(first->snode, 0)) {
            first->flags |= AY_CHOICE_MAND_FALSE;
        }
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
    struct ay_ynode *node, *ch_node;

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        node = &tree[i];
        if (!(node->flags & AY_YNODE_MAND_MASK)) {
            if ((node->type == YN_KEY) || (node->type == YN_LEAF)) {
                ay_ynode_set_mandatory(&tree[i]);
            } else if ((node->type == YN_LEAFLIST) && ay_lnode_has_maybe(node->snode, 0)) {
                node->flags &= ~AY_YNODE_MAND_MASK;
                node->flags |= AY_YNODE_MAND_FALSE;
            }
        }

        if (((node->type == YN_LEAF) || (node->type == YN_VALUE)) &&
                (ch_node = ay_ynode_get_first_in_choice(node->parent, node->choice)) &&
                !(ch_node->flags & AY_CHOICE_MAND_FALSE) &&
                !ay_ynode_alone_in_choice(node)) {
            node->flags &= ~AY_YNODE_MAND_MASK;
            node->flags |= AY_YNODE_MAND_FALSE;
        } else if ((node->type == YN_VALUE) && ay_yang_type_is_empty(node->value)) {
            node->flags &= ~AY_YNODE_MAND_MASK;
            node->flags |= AY_YNODE_MAND_FALSE;
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
        if ((tree[i].type == YN_UNKNOWN) && (!tree[i].child)) {
            ay_ynode_delete_subtree(tree, &tree[i], 1);
            i--;
        }
    }
}

/**
 * @brief Delete generally using nodes for comments.
 *
 * @param[in,out] tree Tree of ynodes.
 */
static void
ay_delete_comment(struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i;
    struct ay_ynode *iter;
    struct lens *label;

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        iter = &tree[i];
        label = AY_LABEL_LENS(iter);
        if (label && (label->tag == L_LABEL) &&
                (!strcmp("#comment", label->string->str) ||
                (!strcmp("#scomment", label->string->str)))) {
            ay_ynode_delete_subtree(tree, &tree[i], 1);
            i--;
        }
    }
}

/**
 * @brief Delete choice for top-nodes.
 *
 * Delete "lns . ( sep . lns )*" pattern (TODO bilateral). This pattern is located in Build module (build.aug).
 * The first 'lns' is useless.
 *
 * @param[in,out] tree Tree of ynodes.
 */
static void
ay_ynode_delete_build_list(struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i;
    struct ay_ynode *node1, *node2;
    struct ay_lnode *iter;

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        node1 = &tree[i];
        if (!node1->snode) {
            continue;
        }

        for (node2 = node1->next; node2; node2 = node2->next) {
            if (!node2 || !node2->snode) {
                continue;
            } else if (node1->snode->lens == node2->snode->lens) {
                break;
            }
        }
        if (!node2) {
            continue;
        }

        /* node1 == node2 */

        /* node1 should not have an asterisk */
        for (iter = node1->snode->parent; iter; iter = iter->parent) {
            if ((iter->lens->tag == L_SUBTREE) || (iter->lens->tag == L_STAR)) {
                break;
            }
        }
        if (iter && (iter->lens->tag == L_STAR)) {
            continue;
        }

        /* node2 should have an asterisk */
        for (iter = node2->snode->parent; iter; iter = iter->parent) {
            if ((iter->lens->tag == L_SUBTREE) || (iter->lens->tag == L_STAR)) {
                break;
            }
        }
        if (!iter || (iter->lens->tag != L_STAR)) {
            continue;
        }

        /* set minimal-elements for node2 */
        if (!ay_lnode_has_maybe(node2->snode, 1)) {
            assert(!(node2->flags & AY_YNODE_MAND_MASK));
            node2->flags &= ~AY_YNODE_MAND_MASK;
            node2->flags = AY_YNODE_MAND_TRUE;
        }

        /* delete node1 because it is useless */
        ay_ynode_delete_subtree(tree, node1, 1);
        i--;
    }
}

/**
 * @brief Delete lonely list key which is unnecessary.
 *
 * Delete "[key lns1 store lns2] | [key lns1]" pattern (bilateral).
 *
 * @param[in,out] tree Tree of ynodes.
 */
static void
ay_ynode_delete_lonely_key(struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i;
    struct ay_ynode *iter, *node;
    struct lens *nodeval, *iterval, *nodelab, *iterlab;

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        node = &tree[i];
        nodelab = AY_LABEL_LENS(node);
        nodeval = AY_VALUE_LENS(node);

        if ((node->type == YN_REC) || !node->choice || !nodelab || (nodelab->tag != L_KEY)) {
            continue;
        }

        /* node and node2 must have the same choice_id */
repeat:
        for (iter = node->next; iter; iter = iter->next) {
            iterlab = AY_LABEL_LENS(iter);
            iterval = AY_VALUE_LENS(iter);

            if (!iter->choice || !iterlab || (iterlab->tag != L_KEY)) {
                continue;
            } else if (node->choice != iter->choice) {
                continue;
            } else if (nodelab->regexp != iterlab->regexp) {
                continue;
            }

            if (!nodeval && (iterval->tag == L_STORE) && !node->child) {
                iter->flags &= ~AY_YNODE_MAND_MASK;
                iter->flags |= AY_YNODE_MAND_FALSE;
                ay_ynode_delete_node(tree, node);
                i--;
                break;
            } else if (!iterval && (nodeval->tag == L_STORE) && !iter->child) {
                node->flags &= ~AY_YNODE_MAND_MASK;
                node->flags |= AY_YNODE_MAND_FALSE;
                ay_ynode_delete_node(tree, iter);
                goto repeat;
            }
        }
    }
}

/**
 * @brief Merge two nodes that have the same counter and STORE lense.
 *
 * [seq str1 . store str2] . [seq str1 . store str2]*   -> [seq str1 . store str2]+
 *
 * @param[in,out] tree Tree of ynodes.
 */
static void
ay_ynode_delete_seq_node(struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i;
    struct ay_ynode *node, *sibling;
    struct lens *nodelab, *siblab, *nodeval, *sibval;

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        node = &tree[i];
        nodelab = AY_LABEL_LENS(node);
        nodeval = AY_VALUE_LENS(node);
        sibling = node->next;
        siblab = sibling ? AY_LABEL_LENS(sibling) : NULL;
        sibval = sibling ? AY_VALUE_LENS(sibling) : NULL;

        if (!nodelab || (nodelab->tag != L_SEQ)) {
            continue;
        } else if (!siblab || (siblab->tag != L_SEQ)) {
            continue;
        } else if (!nodeval || (nodeval->tag != L_STORE)) {
            continue;
        } else if (!sibval || (sibval->tag != L_STORE)) {
            continue;
        } else if (nodeval->regexp != sibval->regexp) {
            continue;
        } else if (node->child || sibling->child) {
            continue;
        } else if (ay_ynode_get_repetition(node) || !ay_ynode_get_repetition(sibling)) {
            continue;
        }

        sibling->flags &= ~AY_YNODE_MAND_MASK;
        sibling->flags = AY_YNODE_MAND_TRUE;
        ay_ynode_delete_node(tree, node);
        i--;
    }
}

/**
 * @brief Unify the containers or lists under the choice relationship if they have the same key.
 *
 * If containers or lists are in choice relation and they have same key then merge them and its nodes will have that
 * choice relation.
 *
 * @param[in,out] tree Tree of ynodes.
 * @return 0.
 */
static int
ay_delete_conlist_with_same_key(struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i;
    struct ay_ynode *first, *second, *cont;
    ly_bool cont_inserted;

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        cont_inserted = 0;
        while (ay_ynode_rule_conlist_with_same_key(&tree[i])) {
            first = &tree[i];
            if (!cont_inserted && first->child) {
                /* in first iteration create cont1 */
                ay_ynode_insert_parent(tree, first->child);
                cont = first->child;
                cont->type = YN_CONTAINER;
                /* set cont the same choice */
                cont->choice = first->choice;
                cont_inserted = 1;
            }
            second = first->next;

            /* YN_VALUES nodes are united in yang union-stmt */
            if (second->value && first->value) {
                /* union will be created */
                ay_dnode_insert(AY_YNODE_ROOT_VALUES(tree), first->value, second->value);
            } else if (second->value && !first->value) {
                /* [key lns1 ] | [key lns1 store lns2]   -> [key lns1 store lns2] */
                first->value = second->value;
            }
            if (first->value || second->value) {
                first->flags |= AY_VALUE_IN_CHOICE;
            }
            second->value = NULL;

            if (second->child) {
                /* Move second node to the first. The second node is merged. */
                second->label = NULL;
                second->choice = first->choice;
                ay_ynode_move_subtree_as_last_child(tree, first, second);
                second = ay_ynode_get_last(first->child);
                ay_ynode_delete_node(tree, second);
            } else {
                /* Delete second node. Nothing to merge. */
                if (first->child) {
                    first->child->flags |= AY_CHOICE_MAND_FALSE;
                }
                ay_ynode_delete_node(tree, second);
            }
        }
    }

    return 0;
}

/**
 * @brief Delete or change the containers that has only one child or has no children.
 *
 * @param[in,out] tree Tree of ynodes.
 */
static void
ay_delete_poor_container(struct ay_ynode *tree)
{
    int ret;
    struct ay_ynode *cont;
    struct lens *label;
    uint32_t i;

    assert((tree->descendants > 1) && (tree[0].type == YN_ROOT));

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        cont = &tree[i];
        label = AY_LABEL_LENS(cont);

        if (cont->type != YN_CONTAINER) {
            continue;
        }

        if (label && ((label->tag == L_LABEL) || ay_lense_pattern_is_label(label) || ay_lense_pattern_has_idents(label))) {
            if (cont->descendants == 0) {
                cont->type = YN_LEAF;
                cont->flags &= ~AY_YNODE_MAND_MASK;
            } else if ((cont->descendants == 1) && cont->value && (cont->child->type == YN_VALUE)) {
                cont->type = YN_LEAF;
                cont->flags &= ~AY_YNODE_MAND_MASK;
            }
            continue;
        }

        if (cont->descendants == 0) {
            ay_ynode_delete_node(tree, cont);
            i--;
        } else if ((cont->descendants == 1) && (cont->label == cont->child->label)) {
            cont->child->type = YN_LEAF;
            cont->child->flags &= ~AY_YNODE_MAND_MASK;
            ay_ynode_delete_node(tree, cont);
            i--;
        } else if (cont->child->choice && !cont->label) {
            /* Remove container if all his children have the same choice. Otherwise choice under case appear. */
            /* All children have the same choice. */
            ret = ay_ynode_delete_node(tree, cont);
            i = ret ? i : i - 1;
        }
    }
}

/**
 * @brief Fill dnode dictionaries for labels and values.
 *
 * Find labels and values which should be in the one union-stmt. See ::ay_lnode_next_lv().
 *
 * @param[in,out] tree Tree of ynodes.
 */
static void
ay_ynode_set_lv(struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i;
    const struct ay_lnode *label, *value, *next;

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        label = tree[i].label;
        value = tree[i].value;
        next = label;
        while ((next = ay_lnode_next_lv(next, AY_LV_TYPE_LABEL))) {
            ay_dnode_insert(AY_YNODE_ROOT_LABELS(tree), label, next);
        }
        next = value;
        while ((next = ay_lnode_next_lv(next, AY_LV_TYPE_VALUE))) {
            ay_dnode_insert(AY_YNODE_ROOT_VALUES(tree), value, next);
        }
    }
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

    ay_ynode_insert_parent(tree, &tree[1]);
    list = &tree[1];
    list->type = YN_LIST;

    return 0;
}

/**
 * @brief Set choice to YN_VALUE node.
 *
 * @param[in,out] node Node to process.
 */
static void
ay_ynode_set_choice_for_value(struct ay_ynode *node)
{
    assert((node->type == YN_VALUE) && node->value && node->parent);

    if (node->next && ((node->parent->flags & AY_VALUE_IN_CHOICE) || (ay_lnode_has_choice(node->value)))) {
        if (node->next->type == YN_GROUPING) {
            assert(node->next->next->type == YN_USES);
            node->choice = node->next->next->choice;
        } else if (node->next) {
            node->choice = node->next->choice;
        }
    }
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
ay_insert_cont_key(struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i;
    struct ay_ynode *parent, *key, *value;
    struct lens *label;
    uint64_t count;

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        if (!(count = ay_ynode_rule_cont_key(&tree[i]))) {
            continue;
        }
        parent = &tree[i];
        label = AY_LABEL_LENS(parent);
        if ((label->tag == L_LABEL) || ay_lense_pattern_is_label(label) || ay_lense_pattern_has_idents(label)) {
            ay_ynode_insert_child(tree, parent);
            value = parent->child;
            value->type = YN_VALUE;
            value->label = parent->label;
            value->value = parent->value;
            value->flags |= ay_yang_type_is_empty(parent->value) ? AY_YNODE_MAND_FALSE : AY_YNODE_MAND_TRUE;
            ay_ynode_set_choice_for_value(value);
            i++;
            continue;
        }

        assert((count == 1) || (count == 2));
        ay_ynode_insert_child(tree, parent);
        key = parent->child;
        key->type = YN_KEY;
        key->label = parent->label;
        key->value = parent->value;
        key->snode = parent->snode;
        ay_ynode_set_mandatory(key);

        if (count == 2) {
            ay_ynode_insert_sibling(tree, key);
            value = key->next;
            value->type = YN_VALUE;
            value->label = parent->label;
            value->value = parent->value;
            if (parent->flags & AY_YNODE_MAND_FALSE) {
                value->flags |= AY_YNODE_MAND_FALSE;
            } else {
                value->flags |= AY_YNODE_MAND_TRUE;
            }
            ay_ynode_set_choice_for_value(value);
        }
    }

    return 0;
}

/**
 * @brief Insert container which groups nodes under one case-stmt.
 *
 * @param[in,out] tree Tree of ynodes.
 * @return 0.
 */
static int
ay_ynode_insert_container(struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i;
    struct ay_ynode *first, *cont, *iter;
    uint64_t j, cnt;

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        first = &tree[i];
        if (!ay_ynode_rule_insert_container(first)) {
            continue;
        }

        cnt = 1;
        for (iter = first->next; iter; iter = iter->next) {
            if (ay_ynode_rule_insert_container(iter)) {
                cnt++;
            }
        }

        if ((first->type != YN_CONTAINER) || ((first->type == YN_CONTAINER) && first->label)) {
            ay_ynode_insert_wrapper(tree, first);
            cont = first;
            first = cont->child;
            cont->type = YN_CONTAINER;
            cont->choice = first->choice;
            cont->flags |= first->flags & AY_CHOICE_MAND_FALSE;
            first->choice = NULL;
        } else {
            cont = first;
        }

        for (j = 0; j < cnt; j++) {
            cont->next->choice = NULL;
            ay_ynode_move_subtree_as_last_child(tree, cont, cont->next);
        }
        i++;
    }

    return 0;
}

/**
 * @brief Nodes that belong to the same grouping are marked by ay_ynode.ref.
 *
 * This function is preparation before calling ::ay_ynode_create_groupings_toplevel().
 *
 * @param[in,out] tree Tree of ynodes.
 * @return 0.
 */
static void
ay_ynode_set_ref(struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i, j, start;
    struct ay_ynode *iti, *itj;
    ly_bool skip, children_eq, subtree_eq;

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        /* Get default subtree. */
        iti = &tree[i];
        if ((iti->type != YN_CONTAINER) && (iti->type != YN_LIST)) {
            continue;
        } else if (iti->ref) {
            i += iti->descendants;
            continue;
        }
        start = i + iti->descendants + 1;

        skip = 0;
        /* Skip if subtree contains leafref because it can break recursive form. */
        for (j = 0; j < iti->descendants; j++) {
            if (iti[j + 1].type == YN_LEAFREF) {
                skip = 1;
                break;
            }
        }
        if (skip) {
            continue;
        }

        /* Find subtrees which are the same. */
        subtree_eq = 0;
        children_eq = 0;
        for (j = start; j < LY_ARRAY_COUNT(tree); j++) {
            itj = &tree[j];
            if (itj->ref) {
                j += itj->descendants;
                continue;
            } else if (ay_ynode_subtree_equal(iti, itj, 1)) {
                /* Subtrees including root node are equal. */
                subtree_eq = 1;
                itj->ref = iti->id;
                j += itj->descendants;
            } else if (itj->descendants && ay_ynode_subtree_equal(iti, itj, 0)) {
                /* Subtrees without root node are equal. */
                children_eq = 1;
                itj->ref = iti->id;
                j += itj->descendants;
            }
            if (subtree_eq && children_eq) {
                break;
            }
        }

        /* Setting 'iti->ref' and flag AY_GROUPING_CHILDREN. */
        if ((subtree_eq && children_eq) || (!subtree_eq && children_eq)) {
            /* Flag children_eq has higher priority than subtree_eq. */
            for (j = start; j < LY_ARRAY_COUNT(tree); j++) {
                itj = &tree[j];
                if (itj->ref == iti->id) {
                    itj->flags |= AY_GROUPING_CHILDREN;
                }
            }
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
    struct ay_ynode *iti, *itj, *grouping, *uses;

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        iti = &tree[i];
        if (!iti->ref) {
            continue;
        } else if ((iti->parent->type == YN_GROUPING) && !iti->next && !(iti->flags & AY_GROUPING_CHILDREN)) {
            continue;
        } else if ((iti->type == YN_USES) || (iti->type == YN_LEAFREF)) {
            continue;
        }
        assert(iti->id == iti->ref);

        /* Insert YN_GROUPING. */
        if (iti->flags & AY_GROUPING_CHILDREN) {
            assert(iti->child);
            ay_ynode_insert_parent(tree, iti->child);
            grouping = iti->child;
            grouping->snode = iti->snode;
        } else {
            ay_ynode_insert_wrapper(tree, iti);
            grouping = iti;
            grouping->snode = NULL;
            iti++;
        }
        grouping->type = YN_GROUPING;

        /* Find, remove duplicate subtree and create YN_USES. */
        for (j = AY_INDEX(tree, grouping) + 1; j < LY_ARRAY_COUNT(tree); j++) {
            itj = &tree[j];
            if (itj->ref != iti->ref) {
                continue;
            } else if ((itj->parent->type == YN_GROUPING) && !itj->next) {
                continue;
            } else if (iti->type == YN_USES) {
                continue;
            }

            /* Delete duplicite subtree. */
            ay_ynode_delete_subtree(tree, itj, 0);
            /* Create YN_USES at 'itj' node. */
            if (iti->flags & AY_GROUPING_CHILDREN) {
                /* YN_USES for children. */
                ay_ynode_insert_child(tree, itj);
                uses = itj->child;
            } else {
                /* YN_USES for subtree (itj node). */
                uses = itj;
                uses->snode = uses->label = uses->value = NULL;
                uses->flags = 0;
            }
            /* itj node is processed */
            itj->ref = 0;
            /* Set YN_USES. */
            uses->type = YN_USES;
            uses->ref = grouping->id;
        }
        /* iti node is processed */
        iti->ref = 0;

        /* Insert YN_USES at 'iti' node. */
        ay_ynode_insert_sibling(tree, grouping);
        uses = grouping->next;
        uses->type = YN_USES;
        uses->ref = grouping->id;
        uses->choice = grouping == iti->parent ? iti->choice : grouping->child->choice;

        grouping->child->choice = !grouping->child->next ? NULL : grouping->child->choice;
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
    struct ay_ynode *node, *node_new, *grouping;

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        node = &tree[i];

        if (!ay_ynode_rule_node_split(node)) {
            continue;
        } else if (ay_ynode_splitted_seq_index(node) != 0) {
            continue;
        }

        assert(node->label);
        idents_count = ay_lense_pattern_idents_count(node->label->lens);
        assert(idents_count > 1);

        node->flags &= ~AY_YNODE_MAND_MASK;
        node->flags |= AY_YNODE_MAND_FALSE;
        /* Just set choice to some value if not already set. */
        node->choice = !node->choice ? AY_YNODE_ROOT_LTREE(tree) : node->choice;

        grouping_id = 0;
        if (node->child && (node->child->type == YN_USES)) {
            assert(node->descendants == 1);
            grouping_id = node->child->ref;
        } else if (node->child && (node->child->type == YN_GROUPING)) {
            assert((node->child->next->type == YN_USES) && (node->child->next->ref == node->child->id));
            grouping_id = node->child->id;
        } else if (node->descendants) {
            /* Create grouping. */
            ay_ynode_insert_parent(tree, node->child);
            grouping = node->child;
            grouping->type = YN_GROUPING;
            grouping->snode = grouping->parent->snode;
            grouping_id = grouping->id;
            /* Create YN_USES node. */
            ay_ynode_insert_sibling(tree, grouping);
            grouping->next->type = YN_USES;
            grouping->next->ref = grouping_id;
        }

        /* Split node. */
        for (j = 0; j < (idents_count - 1); j++) {
            /* insert new node node */
            ay_ynode_insert_sibling(tree, node);
            node_new = node->next;
            ay_ynode_copy_data(node_new, node);
            if (grouping_id) {
                /* Insert YN_USES node. */
                ay_ynode_insert_child(tree, node_new);
                node_new->child->type = YN_USES;
                node_new->child->ref = grouping_id;
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
    uint64_t i;
    struct ay_ynode *parent, *child, *list, *iter;
    const struct ay_lnode *choice, *star;

    for (i = 0; i < tree->descendants; i++) {
        parent = &tree[i + 1];

        for (child = parent->child; child; child = child->next) {
            if ((child->type != YN_LIST) && (child->type != YN_LEAFLIST) && (child->type != YN_REC)) {
                continue;
            } else if ((child->type == YN_LEAFLIST) && !child->choice) {
                continue;
            } else if ((child->type == YN_REC) && (child->parent->type == YN_LIST) &&
                    (child->parent->parent->type != YN_ROOT)) {
                continue;
            }

            star = ay_ynode_get_repetition(child);
            if (!star) {
                continue;
            }

            choice = child->choice;

            /* wrapper is list to maintain the order of the augeas data */
            ay_ynode_insert_wrapper(tree, child);
            list = child;
            list->type = YN_LIST;
            list->flags |= (list->child->flags & AY_YNODE_MAND_MASK);
            list->choice = choice;

            /* every next LIST or LEAFLIST move to wrapper */
            while (list->next &&
                    ((list->next->type == YN_LIST) ||
                    ((list->next->type == YN_LEAFLIST) && (choice == list->next->choice))) &&
                    (star == ay_ynode_get_repetition(list->next))) {
                ay_ynode_move_subtree_as_last_child(tree, list, list->next);
            }

            /* for every child in wrapper set type to container */
            for (iter = list->child; iter; iter = iter->next) {
                if (iter->type != YN_REC) {
                    iter->type = YN_CONTAINER;
                    iter->flags &= ~AY_YNODE_MAND_TRUE;
                }
            }

            /* set list label to repetition because an identifier may be available */
            list->label = star;
        }
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
    struct ay_ynode *listord, *iter, *iter2;

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
    /* Copy siblings before branch into listord. */
    for (iter = ay_ynode_get_first_in_choice(branch->parent, branch->choice);
            iter && (iter->choice == branch->choice) && (iter != branch);
            iter = iter->next) {
        ay_ynode_copy_subtree_as_last_child(tree, listord, iter);
    }
    /* Copy siblings after branch into listord. */
    for (iter = branch->next;
            iter && (iter->choice == branch->choice);
            iter = iter->next) {
        ay_ynode_copy_subtree_as_last_child(tree, listord, iter);
    }

    /* Set some choice id. */
    for (iter = listord->child; iter; iter = iter->next) {
        iter->choice = AY_YNODE_ROOT_LTREE(tree);
    }

    /* Remove duplicit YN_LIST node. */
    for (iter = listord->child; iter; iter = iter->next) {
        if (iter->type == YN_LIST) {
            if (iter->choice) {
                for (iter2 = iter->child; iter2; iter2 = iter2->next) {
                    iter2->choice = iter->choice;
                }
            }
            ay_ynode_delete_node(tree, iter);
        }
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
    struct ay_ynode *lrec_external, *lrec_internal, *iter, *branch, *listrec;

    for (i = 0; i < tree->descendants; i++) {
        lrec_external = &tree[i + 1];
        if (lrec_external->type != YN_REC) {
            continue;
        }
        lrec_internal = NULL;
        while ((lrec_internal = ay_ynode_lrec_internal(lrec_external, lrec_internal))) {

            /* Change lrec_internal to leafref. */
            lrec_internal->type = YN_LEAFREF;

            /* Get branch where is lrec_internal. */
            for (iter = lrec_internal; iter && (iter->parent != lrec_external); iter = iter->parent) {}
            assert(iter);
            branch = iter;
            ay_ynode_lrec_insert_listord(tree, iter, &lrec_internal);

            /* Insert listrec. */
            if (branch->type == YN_LIST) {
                listrec = branch;
            } else {
                ay_ynode_insert_wrapper(tree, branch);
                lrec_internal++;
                listrec = branch;
                listrec->type = YN_LIST;
                listrec->choice = listrec->child->choice;
            }
            listrec->snode = lrec_external->snode;
            listrec->flags |= AY_CONFIG_FALSE;
            lrec_internal->ref = listrec->id;
        }
    }

    return 0;
}

/**
 * @brief Delete all nodes of type YN_REC.
 *
 * @param[in,out] Tree of ynodes.
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
 */
static void
ay_ynode_groupings_ahead(struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i;
    struct ay_ynode *last;

    /* Move grouping. */
    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        if (tree[i].type == YN_GROUPING) {
            ay_ynode_move_subtree_as_last_child(tree, tree, &tree[i]);
        }
    }

    assert(tree->child->type == YN_LIST);
    last = ay_ynode_get_last(tree->child);
    if (last->type == YN_LIST) {
        return;
    }
    ay_ynode_move_subtree_as_sibling(tree, last, tree->child);
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
        }

        if (ay_ynode_rule_list(node)) {
            node->type = YN_LIST;
        } else if (ay_ynode_rule_container(node)) {
            node->type = YN_CONTAINER;
        } else if (ay_ynode_rule_leaflist(node)) {
            node->type = YN_LEAFLIST;
        } else if (ay_ynode_rule_leaf(node)) {
            node->type = YN_LEAF;
        }
    }
}

/**
 * @brief Wrapper for calling some insert function.
 *
 * @param[in,out] tree Tree of ynodes. The memory address of the tree will be changed.
 * The insertion result will be applied.
 * @param[in] items_count Number of nodes to be inserted.
 * @param[in] insert Callback function which inserts some nodes.
 * @return 0 on success.
 */
static int
ay_ynode_trans_insert2(struct ay_ynode **tree, uint32_t items_count, int (*insert)(struct ay_ynode *))
{
    int ret;
    struct ay_ynode *new = NULL;
    uint64_t free_space, new_size;

    free_space = AY_YNODE_ROOT_ARRSIZE(*tree) - LY_ARRAY_COUNT(*tree);
    if (free_space < items_count) {
        new_size = AY_YNODE_ROOT_ARRSIZE(*tree) + (items_count - free_space);
        LY_ARRAY_CREATE(NULL, new, new_size, return AYE_MEMORY);
        ay_ynode_copy(new, *tree);
        AY_YNODE_ROOT_ARRSIZE(new) = new_size;
        LY_ARRAY_FREE(*tree);
        *tree = new;
    }
    ret = insert(*tree);

    return ret;
}

/**
 * @brief Wrapper for calling some insert function.
 *
 * @param[in,out] tree Tree of ynodes. The memory address of the tree will be changed.
 * The insertion result will be applied.
 * @param[in] rule Callback function with which to determine the total number of inserted nodes. This callback is
 * called for each node and the number of nodes to be inserted for that node is returned. Finally, all intermediate
 * results are summed.
 * @param[in] insert Callback function which inserts some nodes.
 * @return 0 on success.
 */
static int
ay_ynode_trans_insert1(struct ay_ynode **tree, uint64_t (*rule)(struct ay_ynode *), int (*insert)(struct ay_ynode *))
{
    uint64_t counter;

    ay_ynode_summary(*tree, rule, &counter);
    return ay_ynode_trans_insert2(tree, counter, insert);
}

/**
 * @brief Apply various transformations before the tree is ready to print.
 *
 * @param[in,out] tree Tree of ynodes. The memory address of the tree will be changed.
 * @return 0 on success.
 */
static int
ay_ynode_transformations(struct ay_ynode **tree)
{
    int ret = 0;

    assert((*tree)->type == YN_ROOT);

    /* lns . (sep . lns)*   -> lns*
     * TODO:
     * (sep . lns)* . lns   -> lns*
     */
    ay_ynode_delete_build_list(*tree);

    /* [key lns1 store lns2] | [key lns1]   -> [key lns1 store lns2] */
    /* [key lns1] | [key lns1 store lns2]   -> [key lns1 store lns2] */
    ay_ynode_delete_lonely_key(*tree);

    /* [seq str1 . store str2] . [seq str1 . store str2]*   -> [seq str1 . store str2]+ */
    ay_ynode_delete_seq_node(*tree);

    /* delete unnecessary nodes */
    ay_delete_comment(*tree);

    /* set type */
    ay_ynode_set_type(*tree);

    ay_delete_type_unknown(*tree);

    ay_ynode_set_flag(*tree);

    /* [ (key lns1 | key lns2) lns3 ]    -> node { type union { pattern lns1; pattern lns2; }}
     * store to YN_ROOT.labels
     * [ key lns1 (store lns2 | store lns3)) ]    -> node { type union { pattern lns2; pattern lns3; }}
     * store to YN_ROOT.values
     */
    ay_ynode_set_lv(*tree);

    /* insert top-level list for storing configure file */
    AY_CHECK_RV(ay_ynode_trans_insert2(tree, 1, ay_insert_list_files));

    /* ([key lns1 ...] . [key lns2 ...]) | [key lns3 ...]   ->
     * choice ch { container { node1{pattern lns1} node2{pattern lns2} } node3{pattern lns3} }
     */
    AY_CHECK_RV(ay_ynode_trans_insert1(tree, ay_ynode_rule_insert_container, ay_ynode_insert_container));

    /* [key lns1 . lns2 ]* | [key lns1 . lns3 ]*   -> [key lns1 (lns2 | lns3)]* */
    /* [key lns1 . lns2 ]  | [key lns1 . lns3 ]    -> [key lns1 (lns2 | lns3)]  */
    /* [key lns1 . lns2 ]  | [key lns1 . lns3 ]*   -> [key lns1 (lns2 | lns3)]* */
    AY_CHECK_RV(ay_ynode_trans_insert1(tree, ay_ynode_rule_conlist_with_same_key, ay_delete_conlist_with_same_key));

    /* Make a tree that reflects the order of records.
     * list A {} list B{} -> list C { container A{} container B{}}
     */
    AY_CHECK_RV(ay_ynode_trans_insert2(tree,
            ay_ynode_rule_ordered_entries(AY_YNODE_ROOT_LTREE(*tree)), ay_ynode_ordered_entries));

    /* Apply recursive yang form for recursive lenses. */
    AY_CHECK_RV(ay_ynode_trans_insert2(tree,
            ay_ynode_rule_recursive_form(*tree), ay_ynode_recursive_form));

    /* Groupings are resolved in functions ay_ynode_set_ref() and ay_ynode_create_groupings_toplevel() */
    /* Link nodes that should be in grouping by number. */
    ay_ynode_set_ref(*tree);

    /* Create groupings and uses-stmt. Grouping are moved to the top-level part of the module. */
    AY_CHECK_RV(ay_ynode_trans_insert1(tree,
            ay_ynode_rule_create_groupings_toplevel, ay_ynode_create_groupings_toplevel));

    /* Delete YN_REC nodes. */
    ay_ynode_delete_ynrec(*tree);

    /* [key "a" | "b"] -> list a {} list b {} */
    /* It is for generally nodes, not just a list nodes. */
    AY_CHECK_RV(ay_ynode_trans_insert1(tree, ay_ynode_rule_node_split, ay_ynode_node_split));

    /* [label str store lns]*   -> container { YN_KEY{} } */
    /* [key lns1 store lns2]*   -> container { YN_KEY{} YN_VALUE{} } */
    AY_CHECK_RV(ay_ynode_trans_insert1(tree, ay_ynode_rule_cont_key, ay_insert_cont_key));

    /* delete the containers that has only one child */
    ay_delete_poor_container(*tree);

    ay_ynode_tree_set_mandatory(*tree);

    /* No other groupings will not be added, so move groupings in front of config-file list. */
    ay_ynode_groupings_ahead(*tree);

    return ret;
}

int
augyang_print_yang(struct module *mod, uint64_t vercode, char **str)
{
    int ret = 0;
    struct lens *lens;
    struct ay_lnode *ltree = NULL;
    struct ay_ynode *yforest = NULL, *ytree = NULL;
    uint32_t ltree_size = 0, yforest_size = 0;

    assert(sizeof(struct ay_ynode) == sizeof(struct ay_ynode_root));

    lens = ay_lense_get_root(mod);
    AY_CHECK_COND(!lens, AYE_LENSE_NOT_FOUND);

    ay_lense_summary(lens, &ltree_size, &yforest_size);

    /* Create lnode tree. */
    LY_ARRAY_CREATE_GOTO(NULL, ltree, ltree_size, ret, cleanup);
    ay_lnode_create_tree(ltree, lens, ltree);
    ret = ay_lnode_tree_check(ltree, mod);
    AY_CHECK_GOTO(ret, cleanup);
    ay_test_lnode_tree(vercode, mod, ltree);

    /* Create ynode forest. */
    LY_ARRAY_CREATE_GOTO(NULL, yforest, yforest_size, ret, cleanup);
    ay_ynode_create_forest(ltree, yforest);
    ay_test_ynode_forest(vercode, mod, yforest);

    /* Convert ynode forest to tree. */
    ret = ay_test_ynode_copy(vercode, yforest);
    AY_CHECK_GOTO(ret, cleanup);
    ret = ay_ynode_create_tree(yforest, ltree, &ytree);
    AY_CHECK_GOTO(ret, cleanup);
    /* The ltree is now owned by ytree, so ytree is responsible for freeing memory of ltree. */
    ltree = NULL;
    /* The yforest is no longer needed. */
    LY_ARRAY_FREE(yforest);
    yforest = NULL;
    /* Print ytree if debugged. */
    ret = ay_debug_ynode_tree(vercode, AYV_YTREE, ytree);
    AY_CHECK_GOTO(ret, cleanup);

    /* Apply transformations. */
    ret = ay_ynode_transformations(&ytree);
    AY_CHECK_GOTO(ret, cleanup);
    ret = ay_debug_ynode_tree(vercode, AYV_YTREE_AFTER_TRANS, ytree);
    AY_CHECK_GOTO(ret, cleanup);

    ret = ay_print_yang(mod, ytree, vercode, str);

cleanup:
    LY_ARRAY_FREE(ltree);
    LY_ARRAY_FREE(yforest);
    ay_ynode_tree_free(ytree);

    return ret;
}
