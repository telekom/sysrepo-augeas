/**
 * @file srdsa_common.h
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief sysrepo DS Augeas plugin common functions header
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

#ifndef SRDSA_COMMON_H_
#define SRDSA_COMMON_H_

#include <sys/types.h>

#include <augeas.h>
#include <libyang/libyang.h>

enum augds_ext_node_type;

/**
 * @brief Get UID of a user or vice versa.
 *
 * @param[in,out] uid UID to search for or the found UID.
 * @param[in,out] user User to search for or the found user.
 * @return SR error code.
 */
int augds_get_pwd(uid_t *uid, char **user);

/**
 * @brief Get GID of a group or vice versa.
 *
 * @param[in,out] gid GID to search for or the found GID.
 * @param[in,out] group Group to search for or the found group.
 * @return SR error code.
 */
int augds_get_grp(gid_t *gid, char **group);

/**
 * @brief Change owner/permissions of a file.
 *
 * @param[in] path Path to the file.
 * @param[in] owner Owner to set, keep previous if not set.
 * @param[in] group Group to set, keep previous if not set.
 * @param[in] perm Permissions to set, keep previous if 0.
 * @return SR error code.
 */
int augds_chmodown(const char *path, const char *owner, const char *group, mode_t perm);

/**
 * @brief Check file existence.
 *
 * @param[in] path Path to the file.
 * @return Whether the file exists or not.
 */
int augds_file_exists(const char *path);

/**
 * @brief Copy file contents to another file.
 *
 * @param[in] to Target file to copy to.
 * @param[in] from Source file to copy from.
 * @return SR error code.
 */
int augds_cp_path(const char *to, const char *from);

/**
 * @brief Get augeas lens name from a YANG module.
 *
 * @param[in] mod YANG module to use.
 * @param[out] lens Augeas lens name.
 * @return SR error code to return.
 */
int augds_get_lens(const struct lys_module *mod, const char **lens);

/**
 * @brief Get last segment (node) from an Augeas label.
 *
 * @param[in] label Augeas label.
 * @param[out] dyn If the node cannot be returned without modifications, return the dynamic memory here.
 * @return Last node.
 */
const char *augds_get_label_node(const char *label, char **dyn);

/**
 * @brief Check for augeas errors.
 *
 * @param[in] aug Augeas handle.
 * @return SR error code to return.
 */
int augds_check_erraug(augeas *aug);

/**
 * @brief Log libyang errors.
 *
 * @param[in] ly_ctx Context to read errors from.
 */
void augds_log_errly(const struct ly_ctx *ly_ctx);

/**
 * @brief Get all config files parsed by an augeas lens.
 *
 * @param[in] aug Augeas context.
 * @param[in] mod Augeas lens YANG module.
 * @param[in] fs_path Whether to skip "/files" for each returned file.
 * @param[out] files Array of config file paths.
 * @param[out] file_count Count of @p files.
 * @return SR error code.
 */
int augds_get_config_files(augeas *aug, const struct lys_module *mod, int fs_path, const char ***files, uint32_t *file_count);

/**
 * @brief Get node type and augeas-extension arguments for this node.
 *
 * @param[in] node Schema node to use.
 * @param[out] node_type Node type.
 * @param[out] data_path Optional Augeas extension data-path value, NULL if none defined.
 * @param[out] value_path Optional Augeas extension value-yang-path value, NULL if none defined.
 */
void augds_node_get_type(const struct lysc_node *node, enum augds_ext_node_type *node_type, const char **data_path,
        const char **value_path);

/**
 * @brief Get value of a term node. It is NULL for empty types.
 *
 * @param[in] node Term node to read from.
 * @return Node value.
 */
const char *augds_get_term_value(const struct lyd_node *node);

#endif /* SRDSA_COMMON_H_ */
