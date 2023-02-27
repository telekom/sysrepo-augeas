/**
 * @file common.h
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

#include <stddef.h>
#include <stdint.h>

struct module;
struct augeas;
struct lens;
struct regexp;
typedef uint8_t ly_bool;

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
    AY_CHECK_COND(strlen(BUFFER) + strlen(STR) + 1 > AY_MAX_IDENT_SIZE, AYE_IDENT_LIMIT)

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
    *(((LY_ARRAY_COUNT_TYPE *)(ARRAY)) - 1) = SIZE

struct ay_pnode;

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
    struct module *mod;         /**< Access to the Augeas module where the ay_lnode.lens is located. */
    struct lens *lens;          /**< Pointer to lense node. Always set. */
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
    ((TAG == L_STORE) || (TAG == L_VALUE))

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
 *
 * From the beginning of the development of Augyang, nodes were stored in a Sized array, because the tree was not
 * manipulated that much back then. Finally, adding and removing nodes is applied quite a bit. Although there are
 * advantages to using an array, a list structure would be more appropriate. It might be worth rewriting sometime
 * in the future.
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
#define AY_GROUPING_CHILDREN    0x040   /**< Grouping is applied to the subtree except the root. */
/* #define AY_FREE_FLAG         0x080 */
#define AY_GROUPING_REDUCTION   0x100   /**< Grouping is reduced due to node name collisions. */
#define AY_HINT_MAND_TRUE       0x200   /**< Node can be mandatory false only due to the maybe operator. */
#define AY_HINT_MAND_FALSE      0x400   /**< maybe operator > AY_HINT_MAND_TRUE > AY_HINT_MAND_FALSE. */
#define AY_CHOICE_CREATED       0x800   /**< A choice is created by the transform and is not in lense. Or it's in the
                                             lense, but it's moved so it doesn't match the ay_lnode tree. */
#define AY_WHEN_TARGET          0x1000  /**< A node is the target of some when statement. */
#define AY_GROUPING_CHOICE      0x2000  /**< Nodes in choice will be in Grouping except for branches containing YN_LEAFREF. */

#define AY_YNODE_FLAGS_CMP_MASK 0xFF    /**< Bitmask to use when comparing ay_ynode.flags. */
/** @} ynodeflags */

/**
 * @brief Get lense from ynode.label.
 *
 * @param[in] YNODE Node ynode which may not have the label set.
 * @return Lense of ay_ynode.label or NULL.
 */
#define AY_LABEL_LENS(YNODE) \
    (YNODE->label ? YNODE->label->lens : NULL)

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
    (YNODE->value ? YNODE->value->lens : NULL)

/**
 * @brief Get lense from ynode.snode.
 *
 * @param[in] YNODE Node ynode which may not have the snode set.
 * @return Lense of ay_ynode.snode or NULL.
 */
#define AY_SNODE_LENS(YNODE) \
    (YNODE->snode ? YNODE->snode->lens : NULL)

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
    ((YNODE->type == YN_LIST) && YNODE->label && (YNODE->label->lens->tag == L_SEQ))

/**
 * @brief Check if ynode was created as an implicit list.
 *
 * Such a list is added in case there are two L_STAR nodes above each other.
 * In other words, two L_STAR belong to an ynode, so an additional implicit
 * list containing the top star must be inserted.
 *
 * @param[in] YNODE Pointer to ynode.
 * @return 1 if ynode is implicit list.
 */
#define AY_YNODE_IS_IMPLICIT_LIST(YNODE) \
    ((YNODE->type == YN_LIST) && YNODE->label && YNODE->snode && (YNODE->label == YNODE->snode) && \
     (YNODE->label->lens->tag == L_STAR))

/**
 * @brief Specific structure for ynode of type YN_ROOT.
 *
 * The ynode of type YN_ROOT is always the first node in the ynode tree (in the Sized Array) and nowhere else.
 * This data structure must always be the same size as struct ay_ynode!
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
    uint32_t ref;                   /**< Can be used as flag between transformations. */
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
    ((struct ay_ynode_root *)(TREE))->ltree

/**
 * @brief Get ay_ynode_root.labels from ynode tree.
 *
 * @param[in] TREE Tree of ynodes. First item in the tree must be YN_ROOT.
 */
#define AY_YNODE_ROOT_LABELS(TREE) \
    ((struct ay_ynode_root *)(TREE))->labels

/**
 * @brief Get ay_ynode_root.values from ynode tree.
 *
 * @param[in] TREE Tree of ynodes. First item in the tree must be YN_ROOT.
 */
#define AY_YNODE_ROOT_VALUES(TREE) \
    ((struct ay_ynode_root *)(TREE))->values

/**
 * @brief Get ay_ynode_root.patt_table from ynode tree.
 *
 * @param[in] TREE Tree of ynodes. First item in the tree must be YN_ROOT.
 */
#define AY_YNODE_ROOT_PATT_TABLE(TREE) \
    ((struct ay_ynode_root *)(TREE))->patt_table

/**
 * @brief Get ay_ynode_root.idcnt from ynode tree.
 *
 * @param[in] TREE Tree of ynodes. First item in the tree must be YN_ROOT.
 */
#define AY_YNODE_ROOT_IDCNT(TREE) \
    ((struct ay_ynode_root *)(TREE))->idcnt

/**
 * @brief Increment ay_ynode_root.idcnt in the ynode tree.
 *
 * @param[in] TREE Tree of ynodes. First item in the tree must be YN_ROOT.
 */
#define AY_YNODE_ROOT_IDCNT_INC(TREE) \
    ((struct ay_ynode_root *)(TREE))->idcnt++

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
 * @brief Get Augeas context from module.
 *
 * @param[in] mod Module from which the context is taken.
 * @return Augeas context.
 */
struct augeas *ay_get_augeas_ctx1(struct module *mod);

/**
 * @brief Get Augeas context from lense.
 *
 * @param[in] lens Lense from which the context is taken.
 * @return Augeas context.
 */
struct augeas *ay_get_augeas_ctx2(struct lens *lens);

/**
 * @brief Get module by the module name.
 *
 * @param[in] aug Augeas context.
 * @param[in] modname Name of the required module.
 * @param[in] modname_len Set string length if not terminated by '\0'.
 * @return Pointer to Augeas module or NULL.
 */
struct module *ay_get_module(struct augeas *aug, const char *modname, size_t modname_len);

/**
 * @brief Get module by lense.
 *
 * @param[in] lens Lense to process.
 * @return Pointer to module or NULL.
 */
struct module *ay_get_module_by_lens(struct lens *lens);

/**
 * @brief Get name of a file (without filename extension) from path.
 *
 * @param[in] path String containing the path to process.
 * @param[out] name Set to part where filename is is.
 * @param[out] len Length of the name.
 */
void ay_get_filename(const char *path, const char **name, uint64_t *len);

/**
 * @brief Get main lense which augeas will use for parsing.
 *
 * @param[in] mod Current augeas module.
 * @return Main lense or NULL.
 */
struct lens *ay_lense_get_root(struct module *mod);

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
const struct ay_lnode *ay_lnode_next_lv(const struct ay_lnode *lv, uint8_t lv_type);

/**
 * @brief Insert new KEY and VALUE pair or insert new VALUE for @p key to the dictionary.
 *
 * @param[in,out] dict Dictionary into which it is inserted.
 * @param[in] key The KEY to search or KEY to insert.
 * @param[in] value The VALUE to be added under @p key. If it is not unique, then another will NOT be added.
 * @param[in] equal Function by which the values will be compared.
 * @return 0 on success.
 */
int ay_dnode_insert(struct ay_dnode *dict, const void *key, const void *value, int (*equal)(const void *, const void *));

/**
 * @brief Search dnode KEY/VALUE in the dictionary.
 *
 * @param[in] dict Dictionary in which the @p kvd.
 * @param[in] kvd The Key or Value Data to be searched in the dnode.
 * @return The dnode with the same kvd or NULL.
 */
struct ay_dnode *ay_dnode_find(struct ay_dnode *dict, const void *kvd);

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
int ay_dnode_merge_keys(struct ay_dnode *dict, struct ay_dnode *key1, struct ay_dnode *key2);

/**
 * @brief Check if @p value is already in @p key values.
 *
 * @param[in] key Dictionary key which contains values.
 * @param[in] value Value that will be searched in @p key.
 * @param[in] equal Function by which the values will be compared.
 * @return 1 if @p value is unique and thus is not found in @p key values.
 */
ly_bool ay_dnode_value_is_unique(struct ay_dnode *key, const void *value,  int (*equal)(const void *, const void *));

/**
 * @brief Find @p origin item in @p table.
 *
 * @param[in] table Array of translation records.
 * @param[in] origin Pointer according to which the record will be searched.
 * @return Record containing @p origin or NULL.
 */
struct ay_transl *ay_transl_find(struct ay_transl *table, const char *origin);

/**
 * @brief Check if pattern is so simple that can be interpreted as label.
 *
 * Function check if there is exactly one identifier in the pattern.
 * In case of detection of more identifiers, use ::ay_lense_pattern_has_idents().
 *
 * @param[in] lens Lense to check.
 * @return 1 if pattern is label.
 */
ly_bool ay_lense_pattern_is_label(struct lens *lens);

/**
 * @brief Check if "type empty;" should be printed.
 *
 * @param[in] lnode Node to check.
 * @return 1 if type empty is in the @p lnode.
 */
ly_bool ay_yang_type_is_empty(const struct ay_lnode *lnode);

/**
 * @brief Get first node which belongs to @p choice.
 *
 * @param[in] parent Node in which some of his immediate children contain @p choice.
 * @param[in] choice Choice id to find.
 * @return First node in choice or NULL.
 */
struct ay_ynode *ay_ynode_get_first_in_choice(const struct ay_ynode *parent, const struct ay_lnode *choice);

/**
 * @brief Check if node is the only one in the choice.
 *
 * @param[in] node Node to check.
 * @return 1 if @p node is alone.
 */
ly_bool ay_ynode_alone_in_choice(struct ay_ynode *node);

/**
 * @brief Check if 'when' value is valid and can therefore be printed.
 *
 * @param[in] node Node with ay_ynode.when_val to check.
 * @return 1 if 'when' value should be valid.
 */
ly_bool ay_ynode_when_value_is_valid(const struct ay_ynode *node);

/**
 * @brief Find the order number of the divided node whose pattern consists of identifiers.
 *
 * It counts how many nodes with the same label precede. From this, it can then be deduced
 * which identifier in the pattern belongs to @p node.
 *
 * @param[in] node One of the node which was created due to pattern with sequence of identifiers.
 * @return Index of splitted node in the sequence of splitted nodes.
 */
uint64_t ay_ynode_splitted_seq_index(const struct ay_ynode *node);
