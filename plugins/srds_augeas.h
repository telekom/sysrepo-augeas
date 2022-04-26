/**
 * @file srds_augeas.h
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief sysrepo DS Augeas plugin internal header
 *
 * @copyright
 * Copyright (c) 2021 - 2022 Deutsche Telekom AG.
 * Copyright (c) 2021 - 2022 CESNET, z.s.p.o.
 *
 * This source code is licensed under BSD 3-Clause License (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://opensource.org/licenses/BSD-3-Clause
 */

#ifndef SRDS_AUGEAS_H_
#define SRDS_AUGEAS_H_

#include "srdsa_common.h"

#include <stdint.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include <augeas.h>
#include <libyang/libyang.h>
#include <sysrepo.h>

#define srpds_name "augeas DS"  /**< plugin name */

#define AUG_PCRE2_MSG_LIMIT 256

#define AUG_FILE_BACKUP_SUFFIX ".augsave"

#define AUG_LOG_ERRINT SRPLG_LOG_ERR(srpds_name, "Internal error (%s:%d).", __FILE__, __LINE__)
#define AUG_LOG_ERRMEM SRPLG_LOG_ERR(srpds_name, "Memory allocation failed (%s:%d).", __FILE__, __LINE__)

#define AUG_LOG_ERRINT_RET AUG_LOG_ERRINT;return SR_ERR_INTERNAL
#define AUG_LOG_ERRINT_GOTO(rc, label) AUG_LOG_ERRINT;rc = SR_ERR_INTERNAL; goto label
#define AUG_LOG_ERRMEM_RET AUG_LOG_ERRMEM;return SR_ERR_NO_MEMORY
#define AUG_LOG_ERRMEM_GOTO(rc, label) AUG_LOG_ERRMEM;rc = SR_ERR_NO_MEMORY; goto label
#define AUG_LOG_ERRAUG_GOTO(augeas, rc, label) rc = augds_check_erraug(augeas);assert(rc);goto label
#define AUG_LOG_ERRLY_RET(ctx) augds_log_errly(ctx);return SR_ERR_LY
#define AUG_LOG_ERRLY_GOTO(ctx, rc, label) augds_log_errly(ctx);rc = SR_ERR_LY;goto label

enum augds_ext_node_type {
    AUGDS_EXT_NODE_NONE,            /**< YANG-only node without representation in Augeas data */
    AUGDS_EXT_NODE_VALUE,           /**< matches specific augeas node value */
    AUGDS_EXT_NODE_LABEL,           /**< matches any augeas node with value being the label, is string '$$' */
    AUGDS_EXT_NODE_REC_LIST,        /**< YANG-only list that represents recursive Augeas data */
    AUGDS_EXT_NODE_REC_LREF         /**< YANG-only term leafref node referencing the recursive list */
};

enum augds_diff_op {
    AUGDS_OP_UNKNOWN = 0,
    AUGDS_OP_INSERT,
    AUGDS_OP_DELETE,
    AUGDS_OP_REPLACE,
    AUGDS_OP_RENAME,
    AUGDS_OP_MOVE,
    AUGDS_OP_NONE
};

struct auginfo {
    augeas *aug;    /**< augeas handle */

    struct augmod {
        const struct lys_module *mod;       /**< libyang module */
        struct augnode {
            const char *data_path;          /**< data-path of the augeas-extension in the schema node */
            const char *value_path;         /**< value-yang-path of the augeas-extension in the schema node */
            const struct lysc_node *schema; /**< schema node */
            const struct lysc_node *schema2;    /**< optional second node if the data-path references 2 YANG nodes */
            const pcre2_code *pcode;        /**< optional compiled PCRE2 pattern of the schema pattern matching Augeas labels */
            uint64_t next_idx;              /**< index to be used for the next list instance, if applicable */
            struct augnode *child;          /**< array of children of this node */
            uint32_t child_count;           /**< number of children */
            struct augnode *parent;         /**< augnode parent */
        } *toplevel;    /**< array of top-level nodes */
        uint32_t toplevel_count;    /**< top-level node count */
    } *mods;            /**< array of all loaded libyang/augeas modules */
    uint32_t mod_count; /**< module count */

    pcre2_code *pcode_uint64;       /**< compiled PCRE2 pattern to match uint64 values, to be reused */
};

/**
 * @brief Initialize augeas structure for a YANG module.
 *
 * @param[in] auginfo Base auginfo structure to use.
 * @param[in] mod YANG module.
 * @param[out] augmod Optional created/found augmod structure for @p mod.
 * @return SR error code.
 */
int augds_init(struct auginfo *auginfo, const struct lys_module *mod, struct augmod **augmod);

/**
 * @brief Destroy augeas structure.
 *
 * @param[in] auginfo Base auginfo structure.
 */
void augds_destroy(struct auginfo *auginfo);

/**
 * @brief Learn operation of a diff node.
 *
 * @param[in] diff_node Diff node.
 * @param[in] parent_op Operation of the parent of @p diff_node.
 * @return Diff node operation.
 */
enum augds_diff_op augds_diff_get_op(const struct lyd_node *diff_node, enum augds_diff_op parent_op);

/**
 * @brief Apply YANG data diff to augeas data performing the changes for the subtree, recursively.
 *
 * @param[in] aug Augeas context.
 * @param[in] diff_node YANG diff subtree to apply.
 * @param[in] parent_path Augeas path of the YANG data diff parent of @p diff.
 * @param[in] parent_op YANG data diff parent operation, 0 if none.
 * @param[in] diff_data Pre-diff data tree to apply the diff on and keep exact data state.
 * @return SR error code.
 */
int augds_store_diff_r(augeas *aug, const struct lyd_node *diff_node, const char *parent_path,
        enum augds_diff_op parent_op, struct lyd_node *diff_data);


/**
 * @brief Append converted augeas data to YANG data. Convert all data handled by a YANG module
 * using the context in the augeas handle.
 *
 * @param[in] aug Augeas handle.
 * @param[in] augnodes Array of augnodes to transform.
 * @param[in] augnode_count Count of @p augnodes.
 * @param[in] parent_label Augeas data parent label (absolute path).
 * @param[in] parent YANG data current parent to append to, may be NULL.
 * @param[in,out] first YANG data first top-level sibling.
 * @return SR error code.
 */
int augds_aug2yang_augnode_r(augeas *aug, struct augnode *augnodes, uint32_t augnode_count, const char *parent_label,
        struct lyd_node *parent, struct lyd_node **first);

#endif /* SRDS_AUGEAS_H_ */
