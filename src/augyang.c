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
#include <ctype.h>
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
 * @brief The minimum number of characters a regex must contain to be considered long.
 */
#define AY_REGEX_LONG 72

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
 * @brief Check if ynode has L_KEY label with nocase set (caseless regular expression).
 *
 * @param[in] YNODE Node ynode to check.
 * @return 1 if nocase is set.
 */
#define AY_LABEL_LENS_NOCASE(YNODE) \
    (YNODE->label && (YNODE->label->lens->tag == L_KEY) ? YNODE->label->lens->regexp->nocase : 0)

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
 * @brief Check if ynode is of type YN_LIST and its lense label is of type L_SEQ.
 *
 * A 'seq_list' is a special type of list that contains the '_seq' node as a key.
 * This key is used as a counter in lenses and typically indicates the order of records.
 *
 * @param[in] YNODE Pointer to ynode.
 * @return 1 if ynode is 'seq_list'.
 */
#define AY_YNODE_IS_SEQ_LIST(YNODE) \
    (YNODE && (YNODE->type == YN_LIST) && YNODE->label && (YNODE->label->lens->tag == L_SEQ))

/**
 * @brief Calculate the index value based on the pointer.
 *
 * @param[in] ARRAY array of items.
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
 * @param[in] ARRAY1 Array to which the address of @p ITEM is converted.
 * @param[in] ARRAY2 Array where @p ITEM is located.
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

/**
 * @brief Check if @p STR can be written to the @p BUFFER.
 *
 * @param[in,out] BUFFER Array of characters.
 * @param[in] STR String to write.
 * @return AYE_IDENT_LIMIT on error.
 */
#define AY_CHECK_MAX_IDENT_SIZE(BUFFER, STR) \
    AY_CHECK_COND(strlen(BUFFER) + strlen(STR) + 1 > AY_MAX_IDENT_SIZE, AYE_IDENT_LIMIT);

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
 * @defgroup pnodeflags Pnode flags.
 *
 * Various flags and additional infromation about pnode structure (used in ::ay_pnode.flags).
 *
 * @{
 */
#define AY_PNODE_HAS_REGEXP     0x1     /**< Pnode does not contain a reference to another pnode,
                                             but a pointer to a regexp. */
#define AY_PNODE_REG_MINUS      0x2     /**< Terms subtree contains a regular expression with a minus operation. */
#define AY_PNODE_REG_UNMIN      0x4     /**< Terms subtree contains a regular expression starting with the A_UNION
                                             operation and there is a minus operation in one of the branches. */
/** @} pnodeflags */

/**
 * @brief Check if ay_pnode.ref is set.
 *
 * @param[in] PNODE Pointer to pnode.
 */
#define AY_PNODE_REF(PNODE) \
    (PNODE->ref && !(PNODE->flags & AY_PNODE_HAS_REGEXP))

/**
 * @brief Wrapper for augeas struct term.
 *
 * This node represents the information obtained from parsing the augeas module. As a wrapper allows more convenient
 * browsing of term nodes. Pnodes are connected in the form of a tree, where the root is the pnode containing the term
 * with the A_MODULE tag. Pnodes are stored in the form of Sized Array. An Augeas term can have two children and they
 * are accessed via the term.left and term.right pointers. For pnode, the left child is accessed via the ay_pnode.child
 * pointer and right child is accessed via ay_pnode.child->next.
 */
struct ay_pnode {
    struct ay_pnode *parent;        /**< Pointer to parent node. */
    struct ay_pnode *next;          /**< Pointer to the next sibling. */
    struct ay_pnode *child;         /**< Pointer to the first child (left term). */
    uint32_t descendants;           /**< Number of descendants in the subtree where current node is the root. */

    uint32_t flags;                 /**< Various additional information about [pnode flags](@ref pnodeflags). */
    struct ay_pnode *bind;          /**< Pointer to the pnode with the A_BIND term under which this node belongs.
                                         In other words it is pointer to a branch from the root of the whole tree. */

    union {
        struct ay_pnode *ref;       /**< This pointer is set at the pnode with the term A_IDENT and refers to the
                                         substituent. Macro AY_PNODE_REF is prepared for checking settings. When
                                         traversing all descendants, it is not enough to use only the
                                         ay_pnode.descendats item, but also this reference. */
        struct regexp *regexp;      /**< Flag AY_PNODE_HAS_REGEXP muset be set. This pointer is set at the pnode with
                                         the term A_IDENT and refers directly to regular expression located in the
                                         different (compiled) agueas module. */
    };
    struct term *term;              /**< Pointer to the corresponding augeas term. */
};

/**
 * @defgroup lenseflags Lense flags.
 *
 * Various flags and additional infromation about lens structures (used in ::ay_lnode.flags).
 *
 * @{
 */
#define AY_LNODE_KEY_IS_LABEL   0x01    /**< A lense has tag L_KEY but should be L_LABEL because it doesn't
                                             actually contain regular expression. For example 'key somename'. */
#define AY_LNODE_KEY_HAS_IDENTS 0x02    /**< A lense has tag L_KEY and has list of identifiers delimited by '|'.
                                             This flag is set even if some identifier is composed of prefixes
                                             or suffixes (like: '(pref1|pref2)name|name2'). */
#define AY_LNODE_KEY_NOREGEX    0x03    /**< A lense has tag L_KEY but it is rather a name than regular expression. */
/** @} lenseflags */

/**
 * @brief Check if ynode label is like identifier.
 *
 * @param[in] YNODE Node of type ynode to check.
 * @return 1 if label is L_LABEL or L_KEY but it is not a regular expression.
 */
#define AY_LABEL_LENS_IS_IDENT(YNODE) \
    (YNODE->label && YNODE->label->lens && ((YNODE->label->lens->tag == L_LABEL) || \
            ((YNODE->label->flags & AY_LNODE_KEY_NOREGEX) && !AY_LABEL_LENS_NOCASE(YNODE))))

/**
 * @brief Wrapper for lense node.
 *
 * This node represents information obtained by compiling augeas terms. The result of the compilation is in the form
 * of augeas lense. Interconnection of lense structures is not suitable for comfortable browsing.
 * Therefore, an ay_lnode wrapper has been created that contains a better connection between the nodes. The lnode nodes
 * are stored in the Sized array and the number of nodes should not be modified.
 */
struct ay_lnode {
    struct ay_lnode *parent;    /**< Pointer to the parent node. */
    struct ay_lnode *next;      /**< Pointer to the next sibling node. */
    struct ay_lnode *child;     /**< Pointer to the first child node. */
    uint32_t descendants;       /**< Number of descendants in the subtree where current node is the root. */

    uint32_t flags;             /**< Various additional information about [lense flags](@ref lenseflags) */
    struct ay_pnode *pnode;     /**< Access to augeas term. Can be NULL. */
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
    YN_CASE,            /**< Yang statement "case" in choice-stmt. */
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
#define AY_YNODE_MAND_FALSE     0x002   /**< No mandatory-stmt is printed. */
#define AY_YNODE_MAND_MASK      0x003   /**< Mask for mandatory-stmt. */
#define AY_CHOICE_MAND_FALSE    0x004   /**< Choice statement must be false. */
#define AY_CHILDREN_MAND_FALSE  0x008   /**< All children must be mandatory false. */
#define AY_VALUE_MAND_FALSE     0x010   /**< The YN_VALUE node must be mandatory false. */
#define AY_VALUE_IN_CHOICE      0x020   /**< YN_VALUE of node must be in choice statement. */
#define AY_GROUPING_CHILDREN    0x040   /**< The ay_ynode.ref only affects children. */
#define AY_CONFIG_FALSE         0x080   /**< Print 'config false' for this node. */
#define AY_GROUPING_REDUCTION   0x100   /**< Grouping is reduced due to node name collisions. */
#define AY_HINT_MAND_TRUE       0x200   /**< Node can be mandatory false only due to the maybe operator. */
#define AY_HINT_MAND_FALSE      0x400   /**< maybe operator > AY_HINT_MAND_TRUE > AY_HINT_MAND_FALSE. */
#define AY_CHOICE_CREATED       0x800   /**< A choice is created by the transform and is not in lense. Or it's in the
                                             lense, but it's moved so it doesn't match the ay_lnode tree. */
#define AY_WHEN_TARGET          0x1000  /**< A node is the target of some when statement. */

#define AY_YNODE_FLAGS_CMP_MASK 0xFF    /**< Bitmask to use when comparing ay_ynode.flags. */
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

    const struct ay_lnode *snode;       /**< Pointer to the corresponding lnode with lense tag L_SUBTREE (or L_REC).
                                             Can be NULL if the ynode was inserted by some transformation. */
    const struct ay_lnode *label;       /**< Pointer to the first 'label' which is lense with tag L_KEY, L_LABEL
                                             or L_SEQ. Can be NULL. */
    const struct ay_lnode *value;       /**< Pointer to the first 'value' which is lense with tag L_STORE or L_VALUE.
                                             Can be NULL. */
    const struct ay_lnode *choice;      /**< Pointer to the lnode with lense tag L_UNION.
                                             Set if the node is under the influence of the union operator. */
    char *ident;                        /**< Yang identifier (yang node name). */
    uint32_t ref;                       /**< Contains ay_ynode.id of some other ynode. Used as reference. */
    uint32_t id;                        /**< Numeric identifier of ynode node. */
    uint16_t flags;                     /**< [ynode flags](@ref ynodeflags) */
    uint16_t min_elems;                 /**< Number of minimal elements in the node of type YN_LIST. */
    uint32_t when_ref;                  /**< Contains ay_ynode.id of ynode that owns the value to which the 'when'
                                             statement will be applied. */
    const struct ay_lnode *when_val;    /**< Value for comparison in the 'when' statement. */
};

/**
 * @brief Specific structure for ynode of type YN_ROOT.
 *
 * The ynode of type YN_ROOT is always the first node in the ynode tree (in the Sized Array) and nowhere else.
 */
struct ay_ynode_root {
    struct ay_ynode *parent;        /**< Always NULL. */
    struct ay_ynode *next;          /**< Always NULL. */
    struct ay_ynode *child;         /**< Pointer to the first child node. */
    uint32_t descendants;           /**< Number of descendants in the ynode tree. */

    enum yang_type type;            /**< Always YN_ROOT. */
    const struct ay_lnode *ltree;   /**< Pointer to the root of the tree of lnodes. */
    struct ay_dnode *labels;        /**< Dictionary for labels of type lnode for grouping labels to be printed
                                         in union-stmt. The key in the dictionary is the first label in the
                                         union and values in the dictionary are the remaining labels. */
    struct ay_dnode *values;        /**< Dictionary for values of type lnode. See ynode.labels. */
    char *choice;                   /**< Not used. */
    struct ay_transl *patt_table;   /**< The ay_transl data in LY_ARRAY. Contains a link to lens.regexp.pattern.str
                                         in the form '(pref1|pref2)name1|name2|name3(suf1|suf2)|...' and its parsed
                                         names (pref1name1, pref2name1, name2, name3suf1, name3suf2, ...). */
    uint32_t ref;                   /**< Not used. */
    uint32_t idcnt;                 /**< ID counter for uniquely assigning identifiers to ynodes. */
    uint16_t flags;                 /**< Not used. */
    uint16_t min_elems;             /**< Not used. */
    uint32_t when_ref;              /**< Not used. */
    uint64_t arrsize;               /**< Allocated ynodes in LY_ARRAY. Root is also counted.
                                         NOTE: The LY_ARRAY_COUNT integer is used to store the number of items
                                         (ynode nodes) in the array and therefore is not used for array size. */
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
 * @brief Get ay_ynode_root.patt_table from ynode tree.
 *
 * @param[in] TREE Tree of ynodes. First item in the tree must be YN_ROOT.
 */
#define AY_YNODE_ROOT_PATT_TABLE(TREE) \
    ((struct ay_ynode_root *)TREE)->patt_table

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
        struct ay_ynode *gr;            /**< Pointer to YN_GROUPING node. Used as KEY. */
        struct ay_ynode *us;            /**< Pointer to YN_USES node. Used as VALUE. */
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

/**
 * @brief Record in translation table.
 *
 * The lens.regexp.pattern.str can be a long string which may consist of a list of possible identifiers rather than
 * a regular expression. It is more efficient to store such identifiers so that the pattern does not have to be parsed
 * repeatedly.
 */
struct ay_transl {
    const char *origin;     /**< Pointer to lens.regexp.pattern.str which is consists of list of identifiers. */
    char **substr;          /**< Parsed identifiers from 'origin' are stored in LY_ARRAY. However, the identifier
                                 is not yet complete, use ay_ynode_get_ident_from_transl_table(). */
};

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
    case AYE_IDENT_BAD_CHAR:
        return AY_NAME " ERROR: Invalid character in identifier.\n";
    case AYE_PARSE_FAILED:
        return AY_NAME " ERROR: Augeas failed to parse.\n";
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
 * @brief Remove character from string.
 *
 * @param[in,out] str Pointer to string.
 * @param[in] rem Pointer to character in @p str to be removed.
 */
static void
ay_string_remove_character(char *str, const char *rem)
{
    uint64_t idx, len;

    assert(str && rem && (rem >= str));
    len = strlen(str);
    idx = rem - str;
    assert(idx < len);
    memmove(&str[idx], &str[idx + 1], len - idx);
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
 * @brief Go through all the lenses and set various counters.
 *
 * @param[in] lens Main lense where to start.
 * @param[out] ltree_size Number of lenses.
 * @param[out] yforest_size Number of lenses with L_SUBTREE tag.
 * @param[out] tpatt_size Maximum number of records in translation table of lens patterns.
 */
static void
ay_lense_summary(struct lens *lens, uint32_t *ltree_size, uint32_t *yforest_size, uint32_t *tpatt_size)
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
 * @param[in] modname_len Set string length if not terminated by '\0'.
 * @return Pointer to Augeas module or NULL.
 */
static struct module *
ay_get_module(struct augeas *aug, const char *modname, size_t modname_len)
{
    struct module *mod = NULL, *mod_iter;
    size_t len;

    len = modname_len ? modname_len : strlen(modname);
    LY_LIST_FOR(aug->modules, mod_iter) {
        if (!strncmp(mod_iter->name, modname, len)) {
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

    mod = ay_get_module(ay_get_augeas_ctx2(lens), modname, 0);
    ret = mod ? ay_get_lense_name_by_mod(mod, lens) : NULL;

    return ret;
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
        if (strcmp(bind_iter->ident->str, lensname)) {
            continue;
        } else if (bind_iter->value->tag != V_REGEXP) {
            continue;
        }

        return bind_iter->value->regexp;
    }

    return NULL;
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
 * @brief Get lense name which is not directly related to @p node.
 *
 * This function is a bit experimental. The point is that, for example, list nodes often have the identifier
 * 'config-entries', which often causes name collisions. But there may be unused lense identifiers in the augeas module
 * and it would be a pity not to use them. So even though the identifier isn't quite directly related to the @p node,
 * it's still better than the default name ('config-entries').
 *
 * @param[in] mod Module in which search the @p lens.
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
 * @brief Merge @p key2 and its values into @p key1.
 *
 * The @p key2 becomes value of @p key1.
 *
 * @param[in,out] dict Dictionary in which @p key1 and @p key2 is located.
 * @param[in] key1 Key which will be enriched with new elements.
 * @param[in] key2 Key which will be moved together with the values.
 * @return 0 on success.
 */
static int
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

/**
 * @brief Check if @p value is already in @p key values.
 *
 * @param[in] key Dictionary key which contains values.
 * @param[in] value Value that will be searched in @p key.
 * @param[in] equal Function by which the values will be compared.
 * @return 1 if @p value is unique and thus is not found in @p key values.
 */
static ly_bool
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

/**
 * @brief Insert new KEY and VALUE pair or insert new VALUE for @p key to the dictionary.
 *
 * @param[in,out] dict Dictionary into which it is inserted.
 * @param[in] key The KEY to search or KEY to insert.
 * @param[in] value The VALUE to be added under @p key. If it is not unique, then another will NOT be added.
 * @param[in] equal Function by which the values will be compared.
 * @return 0 on success.
 */
static int
ay_dnode_insert(struct ay_dnode *dict, const void *key, const void *value, int (*equal)(const void *, const void *))
{
    int ret = 0;
    struct ay_dnode *dkey, *dval, *gap;

    dkey = ay_dnode_find(dict, key);
    dval = ay_dnode_find(dict, value);
    if (dkey && AY_DNODE_IS_VAL(dkey)) {
        return ret;
    } else if (dval && AY_DNODE_IS_KEY(dval)) {
        /* The dval will no longer be dictionary key. It will be value of dkey. */
        ret = ay_dnode_merge_keys(dict, dkey, dval);
        return ret;
    } else if (dkey && !ay_dnode_value_is_unique(dkey, value, equal)) {
        /* The value is not unique, so nothing is inserted. */
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

/**
 * @brief Find @p origin item in @p table.
 *
 * @param[in] table Array of translation records.
 * @param[in] origin Pointer according to which the record will be searched.
 * @return Record containing @p origin or NULL.
 */
static struct ay_transl *
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

/**
 * @brief Release translation table.
 *
 * @param[in] table Array of translation records.
 */
static void
ay_transl_table_free(struct ay_transl *table)
{
    LY_ARRAY_COUNT_TYPE i, j;

    LY_ARRAY_FOR(table, i) {
        LY_ARRAY_FOR(table[i].substr, j) {
            free(table[i].substr[j]);
        }
        LY_ARRAY_FREE(table[i].substr);
    }

    LY_ARRAY_FREE(table);
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
        ay_term_visitor(term->left, data, func);
        ay_term_visitor(term->right, data, func);
        break;
    case A_COMPOSE:
        ay_term_visitor(term->left, data, func);
        ay_term_visitor(term->right, data, func);
        break;
    case A_UNION:
        ay_term_visitor(term->left, data, func);
        ay_term_visitor(term->right, data, func);
        break;
    case A_MINUS:
        ay_term_visitor(term->left, data, func);
        ay_term_visitor(term->right, data, func);
        break;
    case A_CONCAT:
        ay_term_visitor(term->left, data, func);
        ay_term_visitor(term->right, data, func);
        break;
    case A_APP:
        ay_term_visitor(term->left, data, func);
        ay_term_visitor(term->right, data, func);
        break;
    case A_VALUE:
        break;
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
        break;
    default:
        break;
    }
}

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
 * @brief Set pnode ay_pnode.term and ay_pnode.descendants.
 *
 * @param[in] term Current term.
 * @param[in,out] data Pnode iterator.
 */
static void
ay_pnode_set_term(struct term *term, void *data)
{
    struct ay_pnode **iter;
    uint32_t cnt = 0;

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

/**
 * @brief Defined in augeas project in the file parser.y.
 */
int augl_parse_file(struct augeas *aug, const char *name, struct term **term);

/**
 * @brief Parse augeas module @p filename and create pnode tree.
 *
 * @param[in] aug Augeas context.
 * @param[in] filename Name of the module to parse.
 * @param[out] ptree Tree of pnodes.
 * @return 0 on success.
 */
static int
ay_pnode_create(struct augeas *aug, const char *filename, struct ay_pnode **ptree)
{
    int ret;
    struct ay_pnode *tree, *iter;
    struct term *term;
    uint32_t cnt = 0;

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

    *ptree = tree;

    return 0;
}

/**
 * @brief Release pnode tree.
 *
 * @param[in] tree Tree of pnodes.
 */
static void
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
 * @brief Check if term info are equal.
 *
 * @param[in] inf1 First info to check.
 * @param[in] inf2 Second info to check.
 * @return 1 if info are equal.
 */
static ly_bool
ay_term_info_equal(const struct info *inf1, const struct info *inf2)
{
    if (inf1->first_line != inf2->first_line) {
        return 0;
    } else if (inf1->first_column != inf2->first_column) {
        return 0;
    } else if (inf1->last_line != inf2->last_line) {
        return 0;
    } else if (inf1->last_column != inf2->last_column) {
        return 0;
    } else if (strcmp(inf1->filename->str, inf2->filename->str)) {
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
        if (iter->term->tag != A_FUNC) {
            continue;
        } else if (iter->parent->term->tag != A_LET) {
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
    struct ay_pnode *pnode;
    struct augeas *aug;

    aug = ay_get_augeas_ctx2(tree->lens);
    LY_ARRAY_FOR(tree, i) {
        iter = &tree[i];
        if ((iter->lens->tag != L_STORE) && (iter->lens->tag != L_KEY)) {
            continue;
        } else if (!ay_regex_is_long(iter->lens->regexp->pattern->str)) {
            continue;
        }

        pnode = ay_pnode_find_by_info(ptree, iter->lens->info);
        if (!pnode || (pnode->term->tag != A_APP)) {
            continue;
        }

        pnode = pnode->child->next;
        ay_pnode_set_ref(aug, ptree, pnode);
        pnode = ay_pnode_ref_apply(pnode);
        ay_pnode_swap_rep_minus(pnode);

        /* TODO For AY_PNODE_REG_UNMIN form of regex, there can be more minuses. */
        if (!ay_pnode_is_simple_minus_regex(pnode)) {
            continue;
        }
        pnode->flags |= AY_PNODE_REG_MINUS;
        iter->pnode = pnode;
    }

    /* Store root of pnode tree in lnode. */
    assert((tree->lens->tag != L_STORE) && (tree->lens->tag != L_KEY));
    tree->pnode = ptree;
}

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
 * @brief Reset choice to original value or to NULL.
 *
 * Use if the unified choice value (from ::ay_ynode_unite_choice()) is no longer needed.
 *
 * @param[in,out] node Node whose choice will be reset.
 * @param[in] choice Upper search limit.
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
            break;
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
        assert(iter->value == root->value);
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
 * @brief Iterate over siblings from @p node and find next choice group.
 *
 * The @p node may or may not be in any choice group.
 * @param[in] node Starting node from which the next choice group will be sought.
 * @return First node in next choice group or NULL.
 */
static struct ay_ynode *
ay_ynode_next_choice_group(struct ay_ynode *node)
{
    struct ay_ynode *iter;

    if (!node) {
        return NULL;
    } else if (node->choice) {
        /* Leave current choice group. */
        for (iter = node->next; iter && (iter->choice == node->choice); iter = iter->next) {}
        if (!iter) {
            return NULL;
        }
        node = iter;
    }

    /* Find choice group. */
    for (iter = node; iter; iter = iter->next) {
        if (iter->choice && iter->next && (iter->choice == iter->next->choice)) {
            return iter;
        }
    }

    return NULL;
}

/**
 * @brief Check if node is the only one in the choice.
 *
 * @param[in] node Node to check.
 * @return 1 if @p node is alone.
 */
static ly_bool
ay_ynode_alone_in_choice(struct ay_ynode *node)
{
    if (!node->choice) {
        return 0;
    } else if (node != ay_ynode_get_first_in_choice(node->parent, node->choice)) {
        return 0;
    } else if (!node->next) {
        return 1;
    } else if (node->next->choice == node->choice) {
        return 0;
    } else {
        return 1;
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
ay_ynode_get_value_node(const struct ay_ynode *tree, const struct ay_ynode *node, const struct ay_lnode *label,
        const struct ay_lnode *value)
{
    struct ay_ynode *iter, *gr, *valnode;

    valnode = NULL;
    for (iter = node->child; iter; iter = iter->next) {
        if ((iter->type == YN_VALUE) && (iter->label->lens == label->lens) && (iter->value->lens == value->lens)) {
            valnode = iter;
            break;
        } else if (iter->type == YN_USES) {
            gr = ay_ynode_get_grouping(tree, iter->ref);
            assert(gr);
            valnode = ay_ynode_get_value_node(tree, gr, label, value);
            break;

        }
    }

    return valnode;
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

    for (iter = start->parent; iter && iter != stop; iter = iter->parent) {
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
ay_ynode_subtree_contains_rec(struct ay_ynode *subtree, ly_bool only_one)
{
    uint64_t i, ret;
    struct ay_ynode *iter;

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
 * @brief Check if 'when' value is valid and can therefore be printed.
 *
 * @param[in] node Node with ay_ynode.when_val to check.
 * @return 1 if 'when' value should be valid.
 */
static ly_bool
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
 * @return 1 for equal.
 */
static ly_bool
ay_ynode_equal(const struct ay_ynode *n1, const struct ay_ynode *n2, ly_bool ignore_choice)
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
    } else if (!ignore_choice && !alone1 && !alone2 && ((!n1->choice && n2->choice) || (n1->choice && !n2->choice))) {
        return 0;
    } else if ((n1->type != YN_LEAFREF) && (n1->ref != n2->ref)) {
        return 0;
    } else if ((n1->flags & AY_YNODE_FLAGS_CMP_MASK) != (n2->flags & AY_YNODE_FLAGS_CMP_MASK)) {
        return 0;
    } else if ((n1->type == YN_LIST) && (n1->min_elems != n2->min_elems)) {
        return 0;
    } else if (n1->when_ref != n2->when_ref) {
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
    const struct ay_ynode *node1, *node2, *inner1, *inner2;
    uint64_t inner_cnt;

    if (compare_roots) {
        if (!ay_ynode_equal(tree1, tree2, 1)) {
            return 0;
        }
        if (tree1->descendants != tree2->descendants) {
            return 0;
        }
        for (i = 0; i < tree1->descendants; i++) {
            node1 = &tree1[i + 1];
            node2 = &tree2[i + 1];
            if (!ay_ynode_equal(node1, node2, 0)) {
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
            if (!ay_ynode_equal(node1, node2, 0)) {
                return 0;
            }
        }

        return 1;
    }

    return 1;
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
        if (choice_stop && (iter->lens->tag == L_UNION)) {
            return 0;
        } else if (star_stop && (iter->lens->tag == L_STAR)) {
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
 * @param[in] attribut Searched parent lnode.
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
 * This function converts an augeas regular expression to a double-quoted yang pattern. Backslash cases are a bit
 * complicated. Examples of backslash conversions are in the following table, where augeas regular expression is on
 * the left and yang double-quoted pattern is on the right:
 *
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
            case '[':
            case ']':
                if (charClassExpr && !charClassEmpty) {
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
            if (j && (buffer[j - 1] == '-')) {
                j--;
            } else if (j == 0) {
                j--;
            } else {
                AY_CHECK_COND(j >= AY_MAX_IDENT_SIZE, AYE_IDENT_LIMIT);
                buffer[j] = opt == AY_IDENT_NODE_NAME ? '-' : ' ';
            }
            break;
        case '(':
            j--;
            break;
        case ')':
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
    } else if ((*ch == '\\') && (*(ch + 1) == '.')) {
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

/**
 * @brief Check if string @p str is equal to the allowed pattern.
 *
 * Typically, it is a regular expression related to spaces.
 *
 * @param[in] str String (identifier) to check.
 * @param[out] shift Length of the found pattern - 1.
 * @return 1 if pattern in identifier is valid.
 */
static ly_bool
ay_ident_pattern_is_valid(const char *str, uint32_t *shift)
{
    *shift = 0;

    if (!strncmp(str, "[ ]+", 4)) {
        *shift = 3;
        return 1;
    } else {
        return 0;
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

/**
 * @brief Get next part where should be identifier in the pattern.
 *
 * This function does not validate identifier.
 *
 * @param[in] patt Pointer to some part in the lense pattern.
 * @return Pointer to next identifier.
 */
const char *
ay_lense_pattern_next_union(const char *patt)
{
    const char *ret;

    ret = strchr(patt, '|');
    return ret ? ret + 1 : NULL;
}

/**
 * @brief Check if lense pattern does not have a fairly regular expression,
 * but rather a sequence of identifiers separated by '|'.
 *
 * @param[in] tree Tree of ynodes. If set to NULL then the whole pattern in @p lens will be checked.
 * Otherwise ay_ynode_root.patt_table will be used which should be faster.
 * @param[in] lens Lense to check.
 * @return non-zero/(ay_transl *) if lense contains identifiers in his pattern. Otherwise NULL.
 */
static struct ay_transl *
ay_lense_pattern_has_idents(const struct ay_ynode *tree, const struct lens *lens)
{
    const char *iter, *patt;
    uint32_t shift;

    if (!lens || (lens->tag != L_KEY)) {
        return NULL;
    }

    patt = lens->regexp->pattern->str;

    if (tree) {
        return ay_transl_find(AY_YNODE_ROOT_PATT_TABLE(tree), patt);
    }

    for (iter = patt; *iter != '\0'; iter++) {
        switch (*iter) {
        case '(':
        case ')':
        case '|':
        case '\n': /* '\n'-> TODO pattern is probably written wrong -> bugfix lense? */
            continue;
        default:
            if (ay_ident_character_is_valid(iter, &shift) || ay_ident_pattern_is_valid(iter, &shift)) {
                iter = shift ? iter + shift : iter;
            } else {
                return NULL;
            }
        }
    }

    /* Success - return some non-NULL address. */
    return (struct ay_transl *)patt;
}

/**
 * @brief Count number of identifiers in the lense pattern.
 *
 * @param[in] patt Lense pattern to check.
 * @return Number of identifiers.
 */
static uint64_t
ay_pattern_idents_count(const char *patt)
{
    uint64_t ret;

    ret = 1;
    patt = ay_lense_pattern_next_union(patt);
    if (!patt) {
        return ret;
    }

    ret = 2;
    for (patt = ay_lense_pattern_next_union(patt); patt; patt = ay_lense_pattern_next_union(patt)) {
        ++ret;
    }

    return ret;
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

/**
 * @brief Get main union token from pattern (lens.regexp.pattern.str).
 *
 * Pattern must be for example in form: name1 | name2 | (pref1|pref2)name3 | name4(post1|post2)
 * Then the tokens are: name1, name2, (pref1|pref2)name3, name4(post1|post2)
 * If pattern is for example in form: name1 | name2) | name3 | name4
 * Then the tokens are: name1, name2
 * (Tokens name3 and name4 are not accessible by index)
 *
 * @param[in] patt Pattern string from lens.regexp.pattern.str.
 * @param[in] idx Index of the requested token.
 * @param[out] token_len Token length. It ends at '|' or ')'.
 * @return Pointer to token in @p patt on index @p idx or NULL.
 */
static const char *
ay_pattern_union_token(const char *patt, uint64_t idx, uint64_t *token_len)
{
    uint64_t par, cnt;
    const char *ret, *iter, *start, *stop;
    ly_bool end;

    assert(patt && token_len);

    if (*patt == '\0') {
        return NULL;
    } else if (*patt == '|') {
        patt++;
    }

    start = patt;
    stop = NULL;
    par = 0;
    cnt = 0;
    end = 0;
    for (iter = patt; *iter && !end; iter++) {
        switch (*iter) {
        case '(':
            par++;
            break;
        case ')':
            if (!par) {
                /* Interpret as the end of input. */
                stop = iter;
                end = 1;
            } else {
                par--;
            }
            break;
        case '|':
            if (par) {
                break;
            } else if (cnt == idx) {
                /* Token on index 'idx' has been read. */
                stop = iter;
                end = 1;
            } else if ((cnt + 1) == idx) {
                /* The beginning of the token is found. */
                start = iter + 1;
                cnt++;
            } else {
                cnt++;
            }
            break;
        }
    }
    assert(*start != '\0');

    if (cnt != idx) {
        /* Token not found. */
        *token_len = 0;
        return NULL;
    }
    if (!stop) {
        assert(*iter == '\0');
        stop = iter;
    }
    assert(stop > start);
    *token_len = (uint64_t)(stop - start);

    if ((start == patt) && (idx != 0)) {
        /* Token not found. */
        ret = NULL;
    } else {
        ret = start;
    }

    return ret;
}

/**
 * @brief Duplicate pattern and remove all unnecessary parentheses.
 *
 * Examples of unnecessary parentheses: (abc) -> abc, (abc)|(efg) -> abc|efg, ((abc)|(efg))|hij -> abc|efg|hij
 *
 * @param[in] patt Pattern string from lens.regexp.pattern.str.
 * @return New allocated pattern string without parentheses or NULL in case of memory error.
 */
static char *
ay_pattern_remove_parentheses(const char *patt)
{
    uint64_t i, len, par_removed, par;
    char *buf, *buffer;
    const char *ptoken;

    buffer = strdup(patt);
    if (!buffer) {
        return NULL;
    }
    buf = buffer;

    while ((ptoken = ay_pattern_union_token(buf, 0, &len))) {
        par_removed = 0;
        if ((ptoken[0] == '(') && (ptoken[len - 1] == ')')) {
            par = 1;
            for (i = 1; (i < len) && (par != 0); i++) {
                if (ptoken[i] == '(') {
                    par++;
                } else if (ptoken[i] == ')') {
                    par--;
                }
            }
            if (i == len) {
                /* remove parentheses */
                ay_string_remove_character(buf, &ptoken[len - 1]);
                ay_string_remove_character(buf, ptoken);
                len -= 2;
                par_removed = 1;
            }
        }
        if (!par_removed) {
            /* Shift to the next token. */
            buf = (char *)(ptoken + len);
        }
        /* Else try remove parentheses again. */
    }

    return buffer;
}

/**
 * @brief Get identifier from union token located in pattern.
 *
 * @p ptoken must be in form:
 * a) (prefix1 | prefix2 | ... ) some_name,
 * b) Expecting: some_name (postfix1 | postfix2 | ...)
 *
 * @param[in] ptoken Union pattern token. See ay_pattern_union_token().
 * @param[in] ptoken_len Length of @p ptoken.
 * @param[in] idx Index to the requested identifier.
 * @param[out] buffer Buffer of sufficient size in which the identifier will be written.
 * @return AYE_IDENT_NOT_FOUND, AYE_IDENT_LIMIT or 0 on success.
 */
static int
ay_pattern_identifier(const char *ptoken, uint64_t ptoken_len, uint64_t idx, char *buffer)
{
    const char *iter, *start, *stop, *prefix, *name, *postfix, *par;
    uint64_t len1, len2;

    stop = ptoken + ptoken_len;
    assert((*stop == '\0') || (*stop == '|'));

    start = ptoken;

    buffer[0] = '\0';
    if (*start == '(') {
        /* Expecting: (prefix1 | prefix2 | ... ) some_name */
        prefix = ay_pattern_union_token(start + 1, idx, &len1);
        if (!prefix) {
            return AYE_IDENT_NOT_FOUND;
        }
        for (iter = start; *iter && (*iter != ')'); iter++) {}
        assert(*iter);
        name = iter + 1;
        assert(stop > name);
        AY_CHECK_COND(len1 >= AY_MAX_IDENT_SIZE, AYE_IDENT_LIMIT);
        strncat(buffer, prefix, len1);
        len2 = stop - name;
        AY_CHECK_COND((len1 + len2) >= AY_MAX_IDENT_SIZE, AYE_IDENT_LIMIT);
        strncat(buffer, name, len2);
    } else {
        /* Expecting: some_name (postfix1 | postfix2 | ...) */
        name = start;
        par = (const char *)memchr(start, '(', stop - start);
        if (par) {
            len1 = par - name;
            AY_CHECK_COND(len1 >= AY_MAX_IDENT_SIZE, AYE_IDENT_LIMIT);
            strncat(buffer, name, len1);
            assert(*par == '(');
            postfix = ay_pattern_union_token(par + 1, idx, &len2);
            if (!postfix) {
                return AYE_IDENT_NOT_FOUND;
            }
            AY_CHECK_COND((len1 + len2) >= AY_MAX_IDENT_SIZE, AYE_IDENT_LIMIT);
            strncat(buffer, postfix, len2);
        } else if (idx == 0) {
            len1 = stop - name;
            AY_CHECK_COND(len1 >= AY_MAX_IDENT_SIZE, AYE_IDENT_LIMIT);
            strncat(buffer, name, len1);
        } else {
            return AYE_IDENT_NOT_FOUND;
        }
    }

    return 0;
}

/**
 * @brief Special allowed patterns are deleted in the @p substr.
 *
 * @param[in,out] substr String to be converted.
 * @param[in] len Length of @p substr.
 */
static void
ay_trans_substr_conversion(char *substr, uint64_t len)
{
    uint64_t i, j;
    uint32_t shift;

    for (i = 0; i < len; i++) {
        if (ay_ident_pattern_is_valid(&substr[i], &shift)) {
            /* Remove subpattern and replaced it with ' '. */
            for (j = 0; j < shift; j++) {
                ay_string_remove_character(substr, &substr[i]);
            }
            substr[i] = ' ';
        }
    }
}

/**
 * @brief Create and fill ay_transl.substr LY_ARRAY based on ay_transl.origin.
 *
 * @param[in,out] tran Translation record.
 * @return 0 on success.
 */
static int
ay_transl_create_substr(struct ay_transl *tran)
{
    int ret;
    uint64_t idx_cnt, cnt, i, len;
    const char *ptoken, *patt;
    char *pattern, *substr;
    char buffer[AY_MAX_IDENT_SIZE];

    assert(tran && tran->origin);

    /* Allocate enough memory space for ay_transl.substr. */
    cnt = ay_pattern_idents_count(tran->origin);
    LY_ARRAY_CREATE_RET(NULL, tran->substr, cnt, AYE_MEMORY);

    pattern = ay_pattern_remove_parentheses(tran->origin);
    AY_CHECK_COND(!pattern, AYE_MEMORY);

    ret = 0;
    idx_cnt = 0;
    patt = pattern;
    while ((ptoken = ay_pattern_union_token(patt, 0, &len))) {
        for (i = 0; !(ret = ay_pattern_identifier(ptoken, len, i, buffer)); i++) {
            substr = strdup(buffer);
            if (!substr) {
                ret = AYE_MEMORY;
                goto clean;
            }
            ay_trans_substr_conversion(substr, len);
            tran->substr[idx_cnt] = substr;
            idx_cnt++;
            LY_ARRAY_INCREMENT(tran->substr);
        }
        AY_CHECK_GOTO(ret == AYE_IDENT_LIMIT, clean);
        patt = ptoken + len;
    }
    ret = ret == AYE_IDENT_NOT_FOUND ? 0 : ret;

clean:
    free(pattern);

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
 * @brief Get identifier of the ynode from the label lense.
 *
 * @param[in] tree Tree of ynodes.
 * @param[in] node Node to process.
 * @param[in] opt Where the identifier will be placed.
 * @param[out] buffer Identifier can be written to the @p buffer and in this case return value points to @p buffer.
 * @param[out] erc Error code is 0 on success.
 * @return Exact identifier, pointer to @p buffer or NULL.
 */
static const char *
ay_get_yang_ident_from_label(const struct ay_ynode *tree, struct ay_ynode *node, enum ay_ident_dst opt, char *buffer,
        int *erc)
{
    struct lens *label;

    if (*erc) {
        return NULL;
    }
    *erc = 0;

    label = AY_LABEL_LENS(node);
    if (!label) {
        return NULL;
    }

    if ((label->tag == L_LABEL) || (label->tag == L_SEQ)) {
        return label->string->str;
    } else if (node->label->flags & AY_LNODE_KEY_IS_LABEL) {
        if ((opt == AY_IDENT_DATA_PATH) || (opt == AY_IDENT_VALUE_YPATH)) {
            /* remove backslashes */
            ay_string_remove_characters(label->regexp->pattern->str, '\\', buffer);
            return buffer;
        } else {
            /* It is assumed that the ay_get_ident_standardized() will be called later. */
            return label->regexp->pattern->str;
        }
    } else if (node->label->flags & AY_LNODE_KEY_HAS_IDENTS) {
        *erc = ay_ynode_get_ident_from_transl_table(tree, node, opt, buffer);
        return buffer;
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
        if (iter->next || (iter->type == YN_LEAFREF)) {
            break;
        } else if (iter->type == YN_CASE) {
            continue;
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
ay_get_yang_ident(struct yprinter_ctx *ctx, struct ay_ynode *node, enum ay_ident_dst opt, char *buffer)
{
    int ret = 0;
    const char *str, *tmp, *ident_from_label;
    struct lens *label, *value, *snode;
    struct ay_ynode *tree, *iter;
    uint64_t len = 0;
    ly_bool internal = 0;
    ly_bool ch_tag = 0;

    tree = ctx->tree;
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
            /* Try snode from ex-parent. */
            str = ay_get_lense_name(ctx->mod, snode);
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
        } else if ((tmp = ay_get_lense_name(ctx->mod, label)) && strcmp(tmp, "lns")) {
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
        } else if ((tmp = ay_get_lense_name(ctx->mod, snode))) {
            str = tmp;
        } else if ((tmp = ay_get_lense_name(ctx->mod, label))) {
            str = tmp;
        } else if ((tmp = ay_get_yang_ident_from_label(tree, node, opt, buffer, &ret))) {
            AY_CHECK_RET(ret);
            str = tmp;
        } else if (!node->label) {
            ret = ay_get_yang_ident(ctx, node->child, opt, buffer);
            AY_CHECK_RET(ret);
            str = buffer;
        } else {
            str = "node";
        }
    } else if ((node->type == YN_CONTAINER) && (opt == AY_IDENT_DATA_PATH)) {
        if ((tmp = ay_get_yang_ident_from_label(tree, node, opt, buffer, &ret))) {
            AY_CHECK_RET(ret);
            str = tmp;
        } else {
            str = "$$";
        }
    } else if ((node->type == YN_CONTAINER) && (opt == AY_IDENT_VALUE_YPATH)) {
        assert(node->child && node->child->next && (node->child->next->type == YN_VALUE));
        ret = ay_get_yang_ident(ctx, node->child->next, AY_IDENT_NODE_NAME, buffer);
        return ret;
    } else if (node->type == YN_KEY) {
        ident_from_label = ay_get_yang_ident_from_label(tree, node, opt, buffer, &ret);
        if (ident_from_label && (label->tag != L_SEQ) && value && (tmp = ay_get_lense_name(ctx->mod, value))) {
            AY_CHECK_RET(ret);
            str = tmp;
        } else if (ident_from_label) {
            AY_CHECK_RET(ret);
            str = ident_from_label;
        } else if ((tmp = ay_get_lense_name(ctx->mod, label))) {
            str = tmp;
        } else {
            str = "label";
        }
    } else if (node->type == YN_CASE) {
        ay_get_yang_ident(ctx, node->child, opt, buffer);
        str = buffer;
    } else if (node->type == YN_VALUE) {
        if ((tmp = ay_get_lense_name(ctx->mod, value))) {
            str = tmp;
        } else {
            str = "value";
        }
    } else if (((node->type == YN_LEAF) || (node->type == YN_LEAFLIST)) && (opt == AY_IDENT_NODE_NAME)) {
        if ((tmp = ay_get_yang_ident_from_label(tree, node, opt, buffer, &ret))) {
            AY_CHECK_RET(ret);
            str = tmp;
        } else if ((tmp = ay_get_lense_name(ctx->mod, snode))) {
            str = tmp;
        } else if ((tmp = ay_get_lense_name(ctx->mod, label))) {
            str = tmp;
        } else {
            str = "node";
        }
    } else if (((node->type == YN_LEAF) || (node->type == YN_LEAFLIST)) && (opt == AY_IDENT_DATA_PATH)) {
        if ((tmp = ay_get_yang_ident_from_label(tree, node, opt, buffer, &ret))) {
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
        ret = ay_get_ident_standardized(str, opt, internal, buffer);
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

    if (!root) {
        iter = iter->parent;
        while (iter && (iter->type == YN_CASE)) {
            iter = iter->parent;
        }
        assert(iter);
        return iter;
    } else if (!iter) {
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

/**
 * @brief Detect for duplicates for the identifier.
 *
 * @param[in] tree Tree of ynodes.
 * @param[in] node Node for which the duplicates will be searched.
 * @param[out] dupl_rank Duplicate number for @p ident. Rank may be greater than @p dupl_count because it is also
 * derived from the number of the previous duplicate identifier.
 * @param[out] dupl_count Number of all duplicates.
 * @return 0 on success.
 */
static int
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

    root = ay_yang_ident_iter(NULL, node);
    for (iter = ay_yang_ident_iter(root, NULL); iter; iter = ay_yang_ident_iter(root, iter)) {
        if ((iter->type == YN_KEY) || (iter->type == YN_LEAFREF)) {
            continue;
        } else if (iter == node) {
            rnk = cnt;
            continue;
        } else if (!iter->ident) {
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

/**
 * @brief Set ay_ynode.ident for every ynode in the tree.
 *
 * @param[in,out] tree Context for printing.
 * @param[in] solve_duplicates Flag for call ay_yang_ident_duplications().
 * @return 0 on success.
 */
static int
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

    if ((node->type == YN_CASE) || (node->type == YN_KEY) || (node->type == YN_VALUE)) {
        return ret;
    } else if (!value) {
        return ret;
    } else if ((node->type == YN_LEAF) && AY_LABEL_LENS_IS_IDENT(node)) {
        return ret;
    }

    ly_print(ctx->out, "%*s"AY_EXT_PREFIX ":"AY_EXT_VALPATH " \"", ctx->space, "");

    valnode = ay_ynode_get_value_node(ctx->tree, node, node->label, node->value);
    assert(valnode);
    ret = ay_print_yang_ident(ctx, valnode, AY_IDENT_VALUE_YPATH);
    ly_print(ctx->out, "\";\n");

    return ret;
}

static void
ay_print_yang_minelements(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    if (node->min_elems) {
        ly_print(ctx->out, "%*smin-elements %" PRIu16 ";\n", ctx->space, "", node->min_elems);
    } else if (node->flags & AY_YNODE_MAND_TRUE) {
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
                ((lv_type == AY_LV_TYPE_ANY) && (AY_TAG_IS_VALUE(tag)))) {
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
 * @brief Check or set flag AY_PNODE_REG_UNMIN.
 *
 * @param[in] node Corresponding ynode to check flags.
 * @param[in] pnode Pnode to which a flag can be set.
 * @return 1 if AY_PNODE_REG_UNMIN flag is set.
 */
static ly_bool
ay_yang_type_is_regex_unmin(const struct ay_ynode *node, struct ay_pnode *pnode)
{
    if (!pnode) {
        return 0;
    } else if (pnode->flags & AY_PNODE_REG_UNMIN) {
        return 1;
    } else if (!(pnode->flags & AY_PNODE_REG_MINUS)) {
        return 0;
    } else if (node->flags & AY_WHEN_TARGET) {
        return 0;
    } else if (pnode->term->tag == A_UNION) {
        pnode->flags |= AY_PNODE_REG_UNMIN;
        return 1;
    } else {
        return 0;
    }
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

    assert(regex->term->tag == A_MINUS);
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

    if (!(node->flags & AY_WHEN_TARGET) && lnode->pnode) {
        ay_print_yang_pattern_minus(ctx, lnode->pnode);
        return ret;
    } else if (lnode->lens->tag == L_VALUE) {
        ly_print(ctx->out, "%*spattern \"%s\";\n", ctx->space, "", lnode->lens->string->str);
        ay_print_yang_nesting_end(ctx);
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

    if (ay_pnode_peek(regex, A_MINUS)) {
        wrapper.pnode = ay_pnode_ref_apply(regex);
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
    const char *ident = NULL, *type;
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
    char *str;

    str = (lnode->lens->tag == L_VALUE) ? lnode->lens->string->str : NULL;
    ret = ay_print_yang_type_builtin(ctx, lnode->lens);
    if (ret) {
        /* The builtin print failed, so print just string pattern. */
        if ((lnode->lens->tag == L_VALUE) && (lnode->lens->string->str[0] == '\0')) {
            /* It is assumed that the empty string has already been printed. */
            return 0;
        } else if ((lnode->lens->tag == L_VALUE) && !isspace(str[0]) && !isspace(str[strlen(str) - 1])) {
            ret = ay_print_yang_enumeration(ctx, lnode->lens);
        } else {
            ret = ay_print_yang_type_string(ctx, node, lnode);
        }
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
    if (node->type == YN_VALUE) {
        lnode = node->value;
        lv_type = AY_LV_TYPE_VALUE;
    } else if (AY_LABEL_LENS_IS_IDENT(node) && value) {
        lnode = node->value;
        lv_type = AY_LV_TYPE_VALUE;
    } else if ((node->type == YN_LEAF) && (node->label->flags & AY_LNODE_KEY_NOREGEX) && !value) {
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
        ret = ay_print_yang_type_string(ctx, node, NULL);
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
        path_cnt = (parent->type != YN_CASE) ? path_cnt + 1 : path_cnt;
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
    if (!refnode) {
        /* Warning: when is ignored. */
        fprintf(stderr, "augyang warn: 'when' has invalid path and therefore will not be generated "
                "(id = %" PRIu32 ", when_ref = %" PRIu32 ").\n", node->id, node->when_ref);
        return;
    }

    if ((node->type == YN_CASE) && (path_cnt > 0)) {
        /* In YANG, the case-stmt is not counted in the path. */
        path_cnt--;
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
        ly_print(ctx->out, "=\'%s\'\";\n", str);
    } else {
        /* The 'when' expression is more complex, continue with printing of re-match function. */
        ly_print(ctx->out, ", \'");
        ay_print_regex_standardized(ctx->out, str);
        ly_print(ctx->out, "\')\";\n");
    }
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
    ay_print_yang_config(ctx, node);
    ay_print_yang_when(ctx, node);
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

    ay_print_yang_config(ctx, node);

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
    ay_print_yang_config(ctx, node);
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
    ay_print_yang_config(ctx, node);
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
    ay_print_yang_config(ctx, node);
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
    ay_print_yang_config(ctx, node);
    ay_print_yang_when(ctx, node);
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
        return 1;
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
static int
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
        if (node->flags & AY_CHOICE_MAND_FALSE) {
            ly_print(ctx->out, " choice_mand_false");
        }
        if (node->flags & AY_VALUE_IN_CHOICE) {
            ly_print(ctx->out, " value_in_choice");
        }
        if (node->flags & AY_GROUPING_CHILDREN) {
            ly_print(ctx->out, " gr_children");
        }
        if (node->flags & AY_CONFIG_FALSE) {
            ly_print(ctx->out, " conf_false");
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
        ly_print(ctx->out, "\n");
    }

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
    char *str1 = NULL;
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
static char *
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
    LY_ARRAY_COUNT_TYPE i;
    struct ay_transl *patt, *dst;
    uint64_t ret;
    char *origin;
    ly_bool has_idents;

    /* Fill ay_transl.origin. */
    LY_ARRAY_FOR(tree, i) {
        if (tree[i].lens->tag != L_KEY) {
            continue;
        }
        /* Find if pattern is already in table. */
        origin = tree[i].lens->regexp->pattern->str;

        has_idents = !(tree[i].flags & AY_LNODE_KEY_IS_LABEL) && ay_lense_pattern_has_idents(NULL, tree[i].lens);
        if (has_idents) {
            tree[i].flags |= AY_LNODE_KEY_HAS_IDENTS;
        }

        patt = ay_transl_find(table, origin);
        if (!patt && has_idents) {
            dst = &table[LY_ARRAY_COUNT(table)];
            dst->origin = origin;
            LY_ARRAY_INCREMENT(table);
        }
    }

    /* Fill ay_transl.substr. */
    LY_ARRAY_FOR(table, i) {
        ret = ay_transl_create_substr(&table[i]);
        AY_CHECK_RET(ret);
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
    if (ay_lense_pattern_is_label(lens)) {
        node->flags |= AY_LNODE_KEY_IS_LABEL;
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
 * @param[in] tpatt_size Required memory space for ay_ynode_root.patt_table LY_ARRAY.
 * @param[out] tree Resulting ynode tree (new dynamic memory is allocated).
 * @return 0 on success.
 */
static int
ay_ynode_create_tree(struct ay_ynode *forest, struct ay_lnode *ltree, uint32_t tpatt_size, struct ay_ynode **tree)
{
    int ret;
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

    /* Create translation table for lens.regexp.pattern. */
    LY_ARRAY_CREATE(NULL, AY_YNODE_ROOT_PATT_TABLE(*tree), tpatt_size, return AYE_MEMORY);
    ret = ay_transl_create_pattern_table(ltree, AY_YNODE_ROOT_PATT_TABLE(*tree));
    AY_CHECK_RET(ret);

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
ay_ynode_get_repetition(const struct ay_ynode *node)
{
    const struct ay_lnode *ret = NULL, *liter, *lstart, *lstop;
    const struct ay_ynode *yiter;

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
ay_ynode_rule_list(const struct ay_ynode *node)
{
    ly_bool has_value, has_idents;
    struct lens *label;

    label = AY_LABEL_LENS(node);
    has_value = label && ((label->tag == L_KEY) || (label->tag == L_SEQ)) && node->value;
    has_idents = label && (node->label->flags & AY_LNODE_KEY_NOREGEX);
    return (node->child || has_value || has_idents) && label && ay_ynode_get_repetition(node);
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
 * @brief YN_LEAFLIST detection rule.
 *
 * @param[in] node Node to check.
 * @return 1 if ynode is of type YN_LEAFLIST.
 */
static ly_bool
ay_ynode_rule_leaflist(const struct ay_ynode *node)
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
    if (!label) {
        return 0;
    } else if ((node->type != YN_CONTAINER) && !AY_YNODE_IS_SEQ_LIST(node)) {
        return 0;
    } else if (AY_LABEL_LENS_IS_IDENT(node)) {
        return value ? 1 : 0;
    } else if (label->tag == L_SEQ) {
        return value ? 2 : 1;
    } else {
        assert(label->tag == L_KEY);
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
    if (!node1 || !node2) {
        return 0;
    } else if (!node1->choice || !node2->choice) {
        return 0;
    } else if (node1->choice != node2->choice) {
        return 0;
    } else if (!node1->snode || !node2->snode) {
        return 0;
    } else {
        return 1;
    }
}

/**
 * @brief Rule for inserting YN_CASE node which must wrap some nodes due to the choice statement.
 *
 * @param[in] node Node to check.
 * @return 1 if container should be inserted. Also the function returns 1 only for the first node in the wrap.
 */
static uint32_t
ay_ynode_rule_insert_case(const struct ay_ynode *node)
{
    const struct ay_ynode *first, *iter;
    uint64_t cnt, rank;

    if (!node->choice) {
        return 0;
    }

    first = ay_ynode_get_first_in_choice(node, node->choice);
    if (!first) {
        return 0;
    }

    /* Every even node can theoretically have a case. */
    cnt = 1;
    for (iter = first; iter->next && (iter->choice == iter->next->choice); iter = iter->next) {
        if (iter == node) {
            rank = cnt;
        }
        cnt++;
    }

    return rank % 2;
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

    label = AY_LABEL_LENS(node);

    if ((node->type == YN_ROOT) || !label || (label->tag != L_KEY)) {
        return 0;
    } else if ((node->type == YN_KEY) || (node->type == YN_VALUE)) {
        return 0;
    } else if ((count = ay_lense_pattern_idents_count(tree, label)) && (count > 1)) {
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
 * @param[in] tree Tree of ynodes.
 * @return Number of nodes to insert.
 */
static uint64_t
ay_ynode_rule_recursive_form(const struct ay_ynode *tree)
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
    uint32_t deleted_nodes, index, i;

    deleted_nodes = subtree->descendants + 1;

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
    uint32_t deleted_nodes, index, i;

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
 * @param[in] dst Index of the place where the subtree is copied. Gaps are inserted on this index.
 * @param[in] src Index to the root of subtree.
 */
static void
ay_ynode_copy_subtree(struct ay_ynode *tree, uint32_t dst, uint32_t src)
{
    uint32_t subtree_size;
    struct ay_ynode node;

    subtree_size = tree[src].descendants + 1;
    for (uint32_t i = 0; i < subtree_size; i++) {
        node = tree[src];
        ay_ynode_insert_gap(tree, dst);
        src = src >= dst ? src + 1 : src;
        ay_ynode_copy_data(&tree[dst], &node);
        tree[dst].descendants = node.descendants;
        dst++;
        src++;
    }
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
    LY_ARRAY_COUNT_TYPE i;
    struct ay_ynode *iter, *node_ref, *src, *dst;

    assert(copied_subtree && original_subtree);

    for (i = 0; i < original_subtree->descendants; i++) {
        node_ref = &original_subtree[i + 1];
        if (!node_ref->when_ref) {
            continue;
        }
        /* Original node with when_ref is found. */
        assert(original_subtree->type != YN_ROOT);
        for (iter = node_ref->parent; node_ref != original_subtree->parent; node_ref = node_ref->parent) {
            if (node_ref->when_ref == iter->id) {
                /* Correction of when_ref in the copied subtree. */
                dst = &copied_subtree[AY_INDEX(original_subtree, node_ref)];
                src = &copied_subtree[AY_INDEX(original_subtree, iter)];
                dst->when_ref = src->id;
            }
        }
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

/**
 * @brief Set YN_LIST mandatory false if children contain choice and one choice branch is empty.
 *
 * An empty branch in L_UNION is one that contains nodes that will soon be deleted in ynodes (eg comment nodes
 * or nodes without a label). Or, an empty branch is considered to be one that contains only L_DEL nodes.
 *
 * @param[in,out] tree Tree of ynodes.
 */
static void
ay_ynode_mandatory_empty_branch(struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i, j, k;
    struct ay_ynode *list, *child, *iter, *start;
    const struct ay_lnode *choice, *branch, *snode, *label, *stop;
    ly_bool empty_branch = 0;

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        list = &tree[i];
        if ((list->type != YN_LIST) || !list->min_elems) {
            continue;
        }
        assert(list->child);
        child = AY_YNODE_IS_SEQ_LIST(list) ? ay_ynode_inner_nodes(list) : list->child;
        child = !child ? list->child : child;
        start = child->choice ? child : ay_ynode_next_choice_group(child);
        if (!start || (start->flags & AY_CHOICE_CREATED)) {
            continue;
        }
        /* Set stop */
        stop = NULL;
        if (AY_YNODE_IS_SEQ_LIST(list)) {
            assert(list->snode);
            stop = list->snode;
        } else {
            for (iter = list->parent; iter; iter = iter->parent) {
                if (iter->snode) {
                    stop = iter->snode;
                    break;
                }
            }
        }

        /* For every choice group in list. */
        for (child = start; child; child = ay_ynode_next_choice_group(child)) {
            /* Every possible choice towards the parents. */
            for (choice = child->choice; choice && (choice != stop); choice = choice->parent) {
                if (choice->lens->tag != L_UNION) {
                    continue;
                }
                assert(choice->child);
                /* Find empty choice branch. */
                for (branch = choice->child; branch; branch = branch->next) {
                    empty_branch = 1;
                    /* For every node in the choice branch. */
                    for (j = 0; j <= branch->descendants; j++) {
                        snode = &branch[j];
                        if (child->choice == snode) {
                            /* This branch is already processed. */
                            empty_branch = 0;
                            break;
                        } else if (list->value && (snode->lens == list->value->lens)) {
                            /* It is list's value node (value-yang-path). */
                            empty_branch = 0;
                            break;
                        } else if (snode->lens->tag != L_SUBTREE) {
                            continue;
                        }

                        /* Get label for snode. */
                        label = NULL;
                        for (k = 1; k <= snode->descendants; k++) {
                            if (snode[k].lens->tag == L_SUBTREE) {
                                k += snode[k].descendants;
                            } else if (AY_TAG_IS_LABEL(snode[k].lens->tag)) {
                                label = &snode[k];
                                break;
                            }
                        }
                        if (snode && !label) {
                            /* The snode without label -> this branch is empty. */
                            break;
                        }

                        /* Find snode in the list. */
                        for (k = 0; k <= list->descendants; k++) {
                            iter = &list[k];
                            if (iter->label && ay_lnode_lense_equal(label->lens, iter->label->lens)) {
                                /* The snode is found so it is not empty branch. */
                                empty_branch = 0;
                                break;
                            }
                        }
                        if (!empty_branch) {
                            /* This branch is not empty. Let's continue with another choice branch. */
                            break;
                        }
                    }
                    if (empty_branch) {
                        /* The 'branch' is empty. Let's set min-elements to 0. */
                        break;
                    }
                }
                if (empty_branch) {
                    break;
                }
            }
            if (empty_branch) {
                list->min_elems = 0;
                list->flags &= ~AY_YNODE_MAND_MASK;
                break;
            }
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
    LY_ARRAY_COUNT_TYPE i, j;
    struct ay_ynode *node, *iter;
    const struct ay_lnode *lnode;
    ly_bool maybe;

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        node = &tree[i];

        /* setting AY_CHOICE_MAND_FALSE */
        if (!(node->flags & AY_CHOICE_MAND_FALSE) &&
                (node == ay_ynode_get_first_in_choice(node->parent, node->choice)) &&
                !ay_ynode_alone_in_choice(node)) {
            maybe = 1;
            for (iter = node; iter && (iter->choice == node->choice); iter = iter->next) {
                lnode = iter->flags & AY_CHOICE_CREATED ? iter->snode : iter->choice;
                if (!ay_lnode_has_maybe(lnode, 0, 0)) {
                    maybe = 0;
                    break;
                }
            }
            if (maybe) {
                node->flags |= AY_CHOICE_MAND_FALSE;
            }
        }

        if (node->flags & AY_CHILDREN_MAND_FALSE) {
            for (j = 0; j < node->descendants; j++) {
                iter = node + j + 1;
                iter->flags |= AY_HINT_MAND_FALSE;
            }
        } else if ((node->type == YN_LEAF) && node->label && (node->label->flags & AY_LNODE_KEY_NOREGEX)) {
            if (ay_lnode_has_maybe(node->snode, 0, 0) && !ay_ynode_alone_in_choice(node)) {
                node->flags |= AY_CHOICE_MAND_FALSE;
            } else {
                node->flags |= AY_YNODE_MAND_FALSE;
            }
        } else if (node->choice && (node->type != YN_CASE) && (node->type != YN_LIST) &&
                !(ay_ynode_get_first_in_choice(node->parent, node->choice)->flags & AY_CHOICE_MAND_FALSE)) {
            /* The mandatory true information is useless because choice is mandatory true. */
            node->flags |= AY_YNODE_MAND_FALSE;
        } else if ((node->type == YN_VALUE) && (node->flags & AY_VALUE_MAND_FALSE)) {
            node->flags |= AY_YNODE_MAND_FALSE;
        } else if ((node->type == YN_VALUE) && ay_yang_type_is_empty(node->value)) {
            node->flags |= AY_YNODE_MAND_FALSE;
        } else if (node->type == YN_LIST) {
            lnode = AY_YNODE_IS_SEQ_LIST(node) ? node->snode : node->label;
            if (ay_lnode_has_maybe(lnode, 0, 0)) {
                node->min_elems = 0;
            }
        } else if (node->type == YN_LEAFLIST) {
            if (ay_lnode_has_maybe(node->snode, 0, 0)) {
                node->flags |= AY_YNODE_MAND_FALSE;
                node->min_elems = 0;
            }
        } else if (node->type == YN_KEY) {
            node->flags &= ~AY_YNODE_MAND_MASK;
            node->flags |= AY_YNODE_MAND_TRUE;
        } else if (node->type == YN_CONTAINER) {
            node->flags |= AY_YNODE_MAND_FALSE;
        } else {
            if (ay_lnode_has_maybe(node->snode, 0, 0)) {
                node->flags |= AY_YNODE_MAND_FALSE;
                node->min_elems = 0;
            } else {
                node->flags |= AY_YNODE_MAND_TRUE;
            }
        }

        /* Exception due to merge_cases. */
        if ((node->type != YN_KEY) && (node->flags & AY_YNODE_MAND_TRUE) &&
                !(node->flags & AY_HINT_MAND_TRUE) && (node->flags & AY_HINT_MAND_FALSE)) {
            node->flags &= ~AY_YNODE_MAND_MASK;
            node->flags |= AY_YNODE_MAND_FALSE;
        }
    }

    ay_ynode_mandatory_empty_branch(tree);

    /* TODO: in YN_CASE node should be at least one node with mandatory true (if choice is mandatory true). */
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
                !strcmp("!comment", label->string->str) ||
                !strcmp("#mcomment", label->string->str) ||
                !strcmp("#scomment", label->string->str))) {
            ay_ynode_delete_subtree(tree, &tree[i]);
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
    } else if (!node2->label || !node2->snode) {
        return 0;
    } else if (list_check && (node2->type != YN_LIST) && (node2->type != YN_LEAFLIST)) {
        return 0;
    } else if (list_check &&
            ((node1->type == YN_LIST) || (node1->type == YN_LEAFLIST)) &&
            ((ay_lnode_has_attribute(node1->snode, L_STAR) == ay_lnode_has_attribute(node2->snode, L_STAR)) ||
            (node1->choice == node2->choice))) {
        return 0;
    } else if (!ay_lnode_lense_equal(node1->label->lens, node2->label->lens)) {
        return 0;
    } else if ((node1->value && !node2->value) || (!node1->value && node2->value)) {
        return 0;
    } else if (node1->value && node2->value &&
            !ay_lnode_lense_equal(node1->value->lens, node2->value->lens)) {
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
                if (ay_ynode_build_list_match(it1, it2, 1) && (it1->type != YN_LIST) && (it1->type != YN_LEAFLIST)) {
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

    ay_ynode_insert_parent(tree, &tree[1]);
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
 * @param[in,out] Tree of ynodes.
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
 * @param[in,out] node Node to process.
 */
static void
ay_ynode_set_choice_for_value(struct ay_ynode *node)
{
    assert((node->type == YN_VALUE) && node->value && node->parent);

    if (node->next && ((node->parent->flags & AY_VALUE_IN_CHOICE) || (ay_lnode_has_attribute(node->value, L_UNION)))) {
        if (node->next->type == YN_GROUPING) {
            assert(node->next->next->type == YN_USES);
            node->choice = node->next->next->choice;
        } else if (node->next) {
            node->choice = node->next->choice;
        }
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
        if (into_case && (iter->type == YN_CASE) && (ret = ay_ynode_get_child_by_snode(iter, snode, 1))) {
            ret = iter;
        } else if (iter->snode && (snode->lens == iter->snode->lens)) {
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
    const struct ay_lnode *iterl, *choice, *choice_wanted;
    struct ay_ynode *dst, *value;

    assert(node->value);

    if (!node->snode) {
        return ay_ynode_place_value_as_usual(tree, node);
    }

    /* Find L_SUBTREE before 'value' */
    assert(node->snode < node->value);
    dst = NULL;
    for (iterl = node->value; (iterl != node->snode) && !dst; iterl--) {
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
                ay_ynode_set_choice_for_value(value);
            }
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
                ay_ynode_set_choice_for_value(value);
            }
        }
    }

    return 0;
}

/**
 * @brief Insert YN_CASE node which groups nodes under one case-stmt.
 *
 * @param[in,out] tree Tree of ynodes.
 * @return 0.
 */
static int
ay_ynode_insert_case(struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i;
    struct ay_ynode *first, *cas, *iter;
    const struct ay_lnode *common_choice, *con;
    uint64_t j, cnt;

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        first = &tree[i];
        cnt = 0;
        /* Count how many subtrees will be in case. */
        for (iter = first->next; iter; iter = iter->next) {
            if (!ay_ynode_insert_case_prerequisite(first, iter)) {
                break;
            }
            /* Get first common choice. */
            common_choice = ay_ynode_common_choice(first->snode, iter->snode, first->choice);
            /* Get last common concatenation. */
            if (!(con = ay_ynode_common_concat(first, iter, common_choice))) {
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
        if (ay_ynode_alone_in_choice(cas)) {
            cas->child->choice = cas->choice;
            ay_ynode_delete_node(tree, cas);
            continue;
        }
        i++;
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

    if (!ns->choice) {
        ns->flags |= AY_CHOICE_CREATED;
    }

    if (ns->next) {
        /* Create YN_CASE for ns. */
        ay_ynode_insert_parent_for_rest(tree, ns);
        cas = ns;
        /* Unify choice. */
        if (choice) {
            cas->choice = choice;
        } else {
            assert(cas->parent->choice);
            cas->choice = cas->parent->choice;
        }
        cas->type = YN_CASE;
        cas->when_ref = cas->child->when_ref;
        cas->when_val = cas->child->when_val;
        cas->child->when_ref = 0;
        cas->child->when_val = NULL;
        return 1;
    } else {
        ns->choice = ns->parent->choice;
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

    /* Set 'when' for child. */
    if (first1->child && !first2->child && first1->value) {
        first1->child->when_ref = first1->id;
        first1->child->when_val = first1->value;
        first1->flags |= AY_WHEN_TARGET;
    } else if (!first1->child && first2->child && first2->value) {
        first2->child->when_ref = first1->id;
        first2->child->when_val = first2->value;
        first1->flags |= AY_WHEN_TARGET;
    } else if (first1->child && first2->child) {
        if (first1->value) {
            first1->child->when_ref = first1->id;
            first1->child->when_val = first1->value;
            first1->flags |= AY_WHEN_TARGET;
        }
        if (first2->value) {
            first2->child->when_ref = first1->id;
            first2->child->when_val = first2->value;
            first1->flags |= AY_WHEN_TARGET;
        }
    }

    /* Set 'when' for sibling. */
    if ((br1->type == YN_CASE) && (br2->type == YN_CASE)) {
        if (first1->value) {
            first1->next->when_ref = first1->id;
            first1->next->when_val = first1->value;
            first1->flags |= AY_WHEN_TARGET;
        }
        if (first2->value) {
            first2->next->when_ref = first1->id;
            first2->next->when_val = first2->value;
            first1->flags |= AY_WHEN_TARGET;
        }
    } else if ((br1->type != YN_CASE) && (br2->type == YN_CASE)) {
        if (first2->value) {
            first2->next->when_ref = first1->id;
            first2->next->when_val = first2->value;
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
        first1->child->flags |= AY_CHOICE_MAND_FALSE;
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

        first1->type = YN_CONTAINER;
        ay_ynode_merge_nodes(tree, first1, first2->child, 1);
        first1->child->flags |= AY_CHOICE_MAND_FALSE;
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
 * @param[in,out] tree Tree of ynodes.
 * @param[in,out] br1 First branch.
 * @param[in,out] br2 Second branch.
 * @return 1 for success and the @p br2 subtree can be deleted.
 * @return 0 if the branches are differ, function will not change any data.
 */
static ly_bool
ay_ynode_merge_cases_only_by_value(struct ay_ynode *tree, struct ay_ynode *br1, struct ay_ynode *br2, int *err)
{
    struct ay_ynode *first1, *first2, *st1, *st2;

    assert(br1 && br2 && err);

    /* The branches must have the same form. */
    if ((br1->type != YN_CASE) && (br2->type == YN_CASE)) {
        return 0;
    } else if ((br1->type == YN_CASE) && (br2->type != YN_CASE)) {
        return 0;
    }

    first1 = br1->type == YN_CASE ? br1->child : br1;
    first2 = br2->type == YN_CASE ? br2->child : br2;

    if (br1->type == YN_CASE) {
        assert(br2->type == YN_CASE);
        /* Check if all sibling subtrees except the first one are equal. */
        for (st1 = first1->next, st2 = first2->next; st1 && st2; st1 = st1->next, st2 = st2->next) {
            if (!ay_ynode_subtree_equal(st1, st2, 1)) {
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
            (first1->child && first2->child && !ay_ynode_subtree_equal(first1, first2, 0))) {
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

    /* The min_elems must be reset before @p br2 is deleted. */
    first1->min_elems = first1->min_elems < first2->min_elems ? first1->min_elems : first2->min_elems;

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
 * @return 0 on success.
 */
static int
ay_ynode_merge_cases(struct ay_ynode *tree)
{
    int ret;
    LY_ARRAY_COUNT_TYPE i;
    struct ay_ynode *first_child, *chn1, *chn2;
    ly_bool match;
    int err;

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        /* Get first child. */
        first_child = &tree[i];
        if (first_child != first_child->parent->child) {
            continue;
        }
        /* Iterate over siblings. */
        chn1 = first_child;
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
                if (ay_ynode_subtree_equal(chn1, chn2, 1)) {
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
                if (ay_ynode_alone_in_choice(chn1) && (chn1->type == YN_CASE)) {
                    /* YN_CASE is useless. */
                    ay_ynode_delete_node(tree, chn1);
                }
                if (chn1->when_ref && ay_ynode_alone_in_choice(chn1)) {
                    /* The when reference is meaningless if the node is not in choice. */
                    chn1->when_ref = 0;
                    chn1->when_val = NULL;
                }
                /* Repeat comparing because chn1 can be modified. */
                continue;
            }
            /* Current chn1 is processed. Continue with the next one. */
            chn1 = chn1->next;
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
    struct ay_ynode *iti, *itj, *inner_nodes;
    ly_bool children_eq, subtree_eq, alone;

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        /* Get default subtree. */
        iti = &tree[i];
        if ((iti->type == YN_LIST) && (iti->parent->type == YN_ROOT)) {
            continue;
        } else if ((iti->type != YN_CONTAINER) && (iti->type != YN_LIST)) {
            continue;
        } else if (iti->ref) {
            i += iti->descendants;
            continue;
        } else if (ay_ynode_set_ref_leafref_restriction(iti)) {
            continue;
        } else if (iti->when_ref || !ay_ynode_when_paths_are_valid(iti, 1)) {
            continue;
        }

        /* Find subtrees which are the same. */
        subtree_eq = 0;
        children_eq = 0;
        alone = ay_ynode_inner_node_alone(iti);
        inner_nodes = ay_ynode_inner_nodes(iti);
        start = i + iti->descendants + 1;
        for (j = start; j < LY_ARRAY_COUNT(tree); j++) {
            itj = &tree[j];
            if (itj->ref) {
                j += itj->descendants;
                continue;
            } else if (itj->when_ref || !ay_ynode_when_paths_are_valid(itj, 1)) {
                continue;
            }

            if ((itj->type == YN_CONTAINER) &&
                    ((alone && ay_ynode_inner_node_alone(itj)) || !ay_ynode_inner_nodes(itj)) &&
                    !children_eq && ay_ynode_subtree_equal(iti, itj, 1)) {
                /* Subtrees including root node are equal. */
                subtree_eq = 1;
                itj->ref = iti->id;
                j += itj->descendants;
            } else if ((itj->type == YN_LIST) && !children_eq && ay_ynode_subtree_equal(iti, itj, 1)) {
                subtree_eq = 1;
                itj->ref = iti->id;
                j += itj->descendants;
            } else if (inner_nodes && inner_nodes->next && ay_ynode_subtree_equal(iti, itj, 0)) {
                /* Subtrees without root node are equal. */
                children_eq = 1;
                itj->ref = iti->id;
                j += itj->descendants;
            }
        }

        /* Setting 'iti->ref' and flag AY_GROUPING_CHILDREN. */
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

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        iti = &tree[i];
        if (!iti->ref) {
            continue;
        } else if ((iti->type == YN_USES) || (iti->type == YN_LEAFREF)) {
            continue;
        }
        assert(iti->id == iti->ref);

        /* Insert YN_GROUPING. */
        if (iti->flags & AY_GROUPING_CHILDREN) {
            assert(iti->child);
            inner_nodes = ay_ynode_inner_nodes(iti);
            grouping = inner_nodes ? inner_nodes : iti->child;
            ay_ynode_insert_parent_for_rest(tree, grouping);
            grouping->snode = iti->snode;
        } else {
            ay_ynode_insert_wrapper(tree, iti);
            grouping = iti;
            iti++;
            grouping->snode = grouping->parent->snode;
        }
        grouping->type = YN_GROUPING;

        /* Find, remove duplicate subtree and create YN_USES. */
        start = grouping + grouping->descendants + 1;
        for (j = AY_INDEX(tree, start); j < LY_ARRAY_COUNT(tree); j++) {
            itj = &tree[j];
            if ((itj->ref != iti->ref) || (itj->type == YN_USES)) {
                continue;
            }

            /* Create YN_USES at 'itj' node. */
            if (iti->flags & AY_GROUPING_CHILDREN) {
                /* YN_USES for children. */
                ay_ynode_delete_children(tree, itj, 1);
                uses = ay_ynode_insert_child_last(tree, itj);
            } else {
                /* YN_USES for subtree (itj node). */
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
    struct ay_ynode *node, *node_new, *grouping, *inner_nodes, *key, *value;
    ly_bool rec_form, valid_when;

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        node = &tree[i];

        if (!ay_ynode_rule_node_is_splittable(tree, node)) {
            continue;
        } else if (ay_ynode_splitted_seq_index(node) != 0) {
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
        if (inner_nodes && (inner_nodes->type == YN_USES)) {
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
    uint64_t i;
    struct ay_ynode *parent, *child, *list, *iter;
    const struct ay_lnode *choice, *star;

    for (i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        parent = &tree[i];

        for (iter = parent->child; iter; iter = iter->next) {
            if ((iter->type != YN_LIST) && (iter->type != YN_LEAFLIST) && (iter->type != YN_REC)) {
                continue;
            } else if ((iter->type == YN_LEAFLIST) && !iter->choice) {
                continue;
            } else if ((iter->type == YN_REC) && (parent->type == YN_LIST) &&
                    (parent->parent->type != YN_ROOT)) {
                continue;
            }

            star = ay_ynode_get_repetition(iter);
            if (!star) {
                continue;
            }

            choice = iter->choice;

            if (!choice && AY_YNODE_IS_SEQ_LIST(iter)) {
                /* This kind of list is 'seq_list'. It is less common and it should be treated like a container.*/
                continue;
            }

            /* wrapper is list to maintain the order of the augeas data */
            ay_ynode_insert_wrapper(tree, iter);
            list = iter;
            list->type = YN_LIST;
            list->min_elems = list->child->min_elems;
            list->choice = choice;
            list->flags |= list->child->flags & (AY_CHOICE_MAND_FALSE | AY_CHOICE_CREATED);
            list->child->flags &= ~AY_CHOICE_MAND_FALSE;
            /* Move 'when' data to list. */
            list->when_ref = list->child->when_ref;
            list->when_val = list->child->when_val;
            list->child->when_ref = 0;
            list->child->when_val = NULL;

            /* every next LIST or LEAFLIST or YN_REC move to wrapper */
            while (list->next && (choice == list->next->choice) &&
                    ((list->next->type == YN_LIST) || (list->next->type == YN_LEAFLIST) || (list->next->type == YN_REC)) &&
                    (list->min_elems == list->next->min_elems) &&
                    (star == ay_ynode_get_repetition(list->next))) {
                assert(!list->next->when_ref && !list->next->when_val);
                ay_ynode_move_subtree_as_last_child(tree, list, list->next);
            }

            /* for every child in wrapper set type to container */
            for (child = list->child; child; child = child->next) {
                if (child->type != YN_REC) {
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
    uint64_t desc;
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
        if (iter->type == YN_LIST) {
            if (iter->choice) {
                for (iter2 = iter->child; iter2; iter2 = iter2->next) {
                    iter2->choice = iter->choice;
                }
            }
            ay_ynode_delete_node(tree, iter);
            /* Correction of lrec_internal pointer. */
            (*lrec_internal) = *lrec_internal > iter ? *lrec_internal - 1 : *lrec_internal;
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
                listrec->flags |= AY_CONFIG_FALSE;
                lrec_internal->ref = listrec->id;
            } else if (!listrec) {
                /* Create listrec. */
                ay_ynode_insert_wrapper(tree, branch);
                lrec_internal++;
                listrec = branch;
                listrec->type = YN_LIST;
                listrec->choice = listrec->child->choice;
                listrec->snode = lrec_external->snode;
                listrec->flags |= AY_CONFIG_FALSE;
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
 * @param[in,out] Tree of ynodes. Only flags can be modified.
 * @return Number of nodes that must be inserted if grouping reduction is applied.
 */
static uint64_t
ay_ynode_grouping_reduction_count(struct ay_ynode *tree)
{
    int ret;
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
            ret = ay_yang_ident_duplications(tree, uses, gr->child->ident, NULL, &dupl_count);
            assert(!ret);
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
 * @param[in,out] Tree of ynodes.
 * @return 0 on success.
 */
static int
ay_ynode_grouping_reduction(struct ay_ynode *tree)
{
    LY_ARRAY_COUNT_TYPE i;
    struct ay_ynode *gr, *uses, *prev, *new, *parent;
    struct ay_ynode data = {0};
    uint32_t ref;

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

            if (!ref) {
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
 * @param[in,out] Tree of ynodes.
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
        } else if (node->type == YN_REC) {
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
    uint64_t free_space, new_size;
    void *old;

    free_space = AY_YNODE_ROOT_ARRSIZE(*tree) - LY_ARRAY_COUNT(*tree);
    if (free_space < items_count) {
        new_size = AY_YNODE_ROOT_ARRSIZE(*tree) + (items_count - free_space);
        old = *tree;
        LY_ARRAY_CREATE(NULL, *tree, new_size, return AYE_MEMORY);
        if (*tree != old) {
            ay_ynode_tree_correction(*tree);
        }
        AY_YNODE_ROOT_ARRSIZE(*tree) = new_size;
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
 * @reutrn 0 on success.
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

    /* delete unnecessary nodes */
    ay_delete_comment(*tree);

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

    /* ... | [key lns1 . lns2] . lns3 | [key lns1 . lns2] . lns4 | ... ->
     * ... | [key lns1 . lns2] . (lns3 | lns4) | ... */
    /* If lns3 or lns4 missing then choice between lns3 and lns4 is not mandatory. */
    TRANSF(ay_ynode_merge_cases, ay_ynode_rule_merge_cases(*tree));

    /* insert top-level list for storing configure file */
    TRANSF(ay_insert_list_files, 1);

    /* Make a tree that reflects the order of records.
     * list A {} list B{} -> list C { container A{} container B{}}
     */
    TRANSF(ay_ynode_ordered_entries, ay_ynode_rule_ordered_entries(AY_YNODE_ROOT_LTREE(*tree)));

    /* Apply recursive yang form for recursive lenses. */
    TRANSF(ay_ynode_recursive_form, ay_ynode_rule_recursive_form(*tree));

    /* [label str store lns]*   -> container { YN_KEY{} } */
    /* [key lns1 store lns2]*   -> container { YN_KEY{} YN_VALUE{} } */
    TRANSF(ay_insert_node_key_and_value, ay_ynode_summary2(*tree, ay_ynode_rule_node_key_and_value));

    ay_ynode_tree_set_mandatory(*tree);

    /* Groupings are resolved in functions ay_ynode_set_ref() and ay_ynode_create_groupings_toplevel() */
    /* Link nodes that should be in grouping by number. */
    ay_ynode_set_ref(*tree);

    /* Create groupings and uses-stmt. Grouping are moved to the top-level part of the module. */
    TRANSF(ay_ynode_create_groupings_toplevel, ay_ynode_summary(*tree, ay_ynode_rule_create_groupings_toplevel));

    /* Delete YN_REC nodes. */
    ay_ynode_delete_ynrec(*tree);

    /* [key "a" | "b"] -> list a {} list b {} */
    /* It is for generally nodes, not just a list nodes. */
    TRANSF(ay_ynode_node_split, ay_ynode_rule_node_split(*tree, *tree));

    /* No other groupings will not be added, so move groupings in front of config-file list. */
    AY_CHECK_RV(ay_ynode_groupings_ahead(*tree));

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
    struct ay_ynode *yforest = NULL, *ytree = NULL;
    struct ay_pnode *ptree = NULL;
    uint32_t ltree_size = 0, yforest_size = 0, tpatt_size = 0;

    assert(sizeof(struct ay_ynode) == sizeof(struct ay_ynode_root));

    lens = ay_lense_get_root(mod);
    AY_CHECK_COND(!lens, AYE_LENSE_NOT_FOUND);

    ay_lense_summary(lens, &ltree_size, &yforest_size, &tpatt_size);

    /* Create lnode tree. */
    LY_ARRAY_CREATE_GOTO(NULL, ltree, ltree_size, ret, cleanup);
    ay_lnode_create_tree(ltree, lens, ltree);
    ret = ay_lnode_tree_check(ltree, mod);
    AY_CHECK_GOTO(ret, cleanup);
    ay_test_lnode_tree(vercode, mod, ltree);

    /* Create pnode tree. */
    ret = ay_pnode_create(ay_get_augeas_ctx1(mod), lens->info->filename->str, &ptree);
    AY_CHECK_GOTO(ret, cleanup);
    /* Set ptree in lnode tree. */
    ay_lnode_set_pnode(ltree, ptree);
    ay_pnode_print_verbose(vercode, ptree);

    /* Create ynode forest. */
    LY_ARRAY_CREATE_GOTO(NULL, yforest, yforest_size, ret, cleanup);
    ay_ynode_create_forest(ltree, yforest);
    ay_test_ynode_forest(vercode, mod, yforest);

    /* Convert ynode forest to tree. */
    ret = ay_test_ynode_copy(vercode, yforest);
    AY_CHECK_GOTO(ret, cleanup);
    ret = ay_ynode_create_tree(yforest, ltree, tpatt_size, &ytree);
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
    ret = ay_ynode_transformations(mod, &ytree);
    AY_CHECK_GOTO(ret, cleanup);
    ret = ay_debug_ynode_tree(vercode, AYV_YTREE_AFTER_TRANS, ytree);
    AY_CHECK_GOTO(ret, cleanup);

    ret = ay_print_yang(mod, ytree, vercode, str);

cleanup:
    LY_ARRAY_FREE(ltree);
    ay_pnode_free(ptree);
    LY_ARRAY_FREE(yforest);
    ay_ynode_tree_free(ytree);

    return ret;
}
