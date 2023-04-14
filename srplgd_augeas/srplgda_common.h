/**
 * @file srplgda_common.h
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief Augeas sysrepo-plugind plugin common functions header
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

#include <sys/wait.h>

/**
 * @brief Wrapper for execl(3) function.
 *
 * @param[in] plg_name Plugin name to use for logging.
 * @param[in] pathname Path to the executable to exec.
 * @param[in] ... Arguments to use ended with NULL.
 * @return SR_ERR value.
 */
int aug_execl(const char *plg_name, const char *pathname, ...);

/**
 * @brief Read PID from a PID file.
 *
 * @param[in] plg_name Plugin name to use for logging.
 * @param[in] path PID file path.
 * @param[out] pid Read PID, 0 if none found.
 * @return SR_ERR value.
 */
int aug_pidfile(const char *plg_name, const char *path, pid_t *pid);

/**
 * @brief Send SIGHUP to a process.
 *
 * @param[in] plg_name Plugin name to use for logging.
 * @param[in] pid Pid of the process to signal.
 * @return SR_ERR value.
 */
int aug_send_sighup(const char *plg_name, pid_t pid);
