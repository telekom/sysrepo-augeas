/**
 * @file terms.h
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

#include <stdint.h>

struct augeas;
struct ay_pnode;
struct ay_lnode;

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
 * @brief Check if ay_pnode.ref is set.
 *
 * @param[in] PNODE Pointer to pnode.
 */
#define AY_PNODE_REF(PNODE) \
    (PNODE->ref && !(PNODE->flags & AY_PNODE_HAS_REGEXP))

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
#define AY_PNODE_FOR_SNODE      0x08    /**< A pnode is assigned to some ay_ynode.snode. */
#define AY_PNODE_FOR_SNODES     0x10    /**< This pnode is assigned for more than one snode. */
/** @} pnodeflags */

/**
 * @brief Parse augeas module @p filename and create pnode tree.
 *
 * @param[in] aug Augeas context.
 * @param[in] filename Name of the module to parse.
 * @param[in,out] tree Tree of lnodes.
 * @param[out] ptree Tree of pnodes. For every lnode set parsed node. Only lnodes tagged L_STORE and L_KEY
 * can have pnode set.
 * @return 0 on success.
 */
int ay_pnode_create(struct augeas *aug, const char *filename, struct ay_lnode *ltree, struct ay_pnode **ptree);

/**
 * @brief Release pnode tree.
 *
 * @param[in] tree Tree of pnodes.
 */
void ay_pnode_free(struct ay_pnode *tree);
