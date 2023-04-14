/**
 * @file srplgda_common.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief Augeas sysrepo-plugind plugin common functions
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
#define _POSIX_C_SOURCE 200809L /* kill() */

#include "srplgda_common.h"

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <sysrepo.h>

int
aug_execl(const char *plg_name, const char *pathname, ...)
{
    int rc = SR_ERR_OK, wstatus;
    va_list ap;
    const char **args = NULL, *arg;
    uint32_t arg_count = 0;
    void *mem;
    pid_t ch_pid;

    /* process variable args into an array */
    va_start(ap, pathname);
    do {
        arg = va_arg(ap, const char *);

        /* add new arg */
        mem = realloc(args, (arg_count + 1) * sizeof *args);
        if (!mem) {
            SRPLG_LOG_ERR(plg_name, "Memory allocation failed (%s:%d).", __FILE__, __LINE__);
            rc = SR_ERR_NO_MEMORY;
            goto cleanup;
        }
        args = mem;

        args[arg_count] = arg;
        ++arg_count;
    } while (arg);
    va_end(ap);

    /* fork and execv */
    if (!(ch_pid = fork())) {
        execv(pathname, (char * const *)args);
        exit(-1);
    } else if (ch_pid == -1) {
        SRPLG_LOG_ERR(plg_name, "fork() failed (%s).", strerror(errno));
        rc = SR_ERR_SYS;
        goto cleanup;
    }

    /* check return value of the forked process */
    if (waitpid(ch_pid, &wstatus, 0) == -1) {
        SRPLG_LOG_ERR(plg_name, "waitpid() failed (%s).", strerror(errno));
        rc = SR_ERR_SYS;
        goto cleanup;
    }
    if (!WIFEXITED(wstatus)) {
        SRPLG_LOG_ERR(plg_name, "Exec of \"%s\" did not terminate normally.", pathname);
        rc = SR_ERR_OPERATION_FAILED;
        goto cleanup;
    }
    if (WEXITSTATUS(wstatus)) {
        SRPLG_LOG_ERR(plg_name, "Exec of \"%s\" returned %d.", pathname, WEXITSTATUS(wstatus));
        rc = SR_ERR_OPERATION_FAILED;
        goto cleanup;
    }

cleanup:
    free(args);
    return rc;
}

int
aug_pidfile(const char *plg_name, const char *path, pid_t *pid)
{
    int rc = SR_ERR_OK, fd = -1;
    off_t size;
    char *buf = NULL;

    *pid = 0;

    /* open the pidfile */
    fd = open(path, O_RDONLY);
    if (fd == -1) {
        if (errno != ENOENT) {
            SRPLG_LOG_ERR(plg_name, "open() on \"%s\" failed (%s).", path, strerror(errno));
            rc = SR_ERR_SYS;
        } /* else no PID file exists */
        goto cleanup;
    }

    /* learn size */
    if (((size = lseek(fd, 0, SEEK_END)) == -1) || (lseek(fd, 0, SEEK_SET) == -1)) {
        SRPLG_LOG_ERR(plg_name, "lseek() failed (%s).", strerror(errno));
        rc = SR_ERR_SYS;
        goto cleanup;
    }
    if (!size) {
        /* no PID stored */
        goto cleanup;
    }

    /* alloc buffer */
    buf = malloc(size + 1);
    if (!buf) {
        SRPLG_LOG_ERR(plg_name, "Memory allocation failed (%s:%d).", __FILE__, __LINE__);
        rc = SR_ERR_NO_MEMORY;
        goto cleanup;
    }

    /* read PID */
    if (read(fd, buf, size) != size) {
        SRPLG_LOG_ERR(plg_name, "read() failed (%s).", strerror(errno));
        rc = SR_ERR_SYS;
        goto cleanup;
    }
    buf[size] = '\0';

    /* get the PID number */
    if (!(*pid = atoi(buf))) {
        SRPLG_LOG_ERR(plg_name, "Invalid PID \"%s\" in \"%s\".", buf, path);
        rc = SR_ERR_SYS;
        goto cleanup;
    }

cleanup:
    if (fd > -1) {
        close(fd);
    }
    free(buf);
    return rc;
}

int
aug_send_sighup(const char *plg_name, pid_t pid)
{
    int rc = SR_ERR_OK;

    if (kill(pid, SIGHUP) == -1) {
        SRPLG_LOG_ERR(plg_name, "Failed to send SIGHUP.");
        rc = SR_ERR_SYS;
    }

    return rc;
}
