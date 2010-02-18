/* -*- mode: c; c-basic-offset: 8; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright  2009 Sun Microsystems, Inc. All rights reserved
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/utils/solaris/cfsd.c
 *
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <door.h>

/* remove the five lines below as soon as cfsd.c includes <libcfs/libcfs.h> */
#ifndef __LIBCFS_LIBCFS_H__
#define __LIBCFS_LIBCFS_H__
#include <libcfs/solaris/solaris-utils.h>
#define IOC_LIBCFS_SET_DFD 0xc0086552
#endif

#define CFSD_LOCKFILE   "/var/run/cfsd.lock"

/* we need a device to open and use ioctl() to pass the door fd down */
#define CFS_DEV "/dev/lnet"


void cfsd_door_func(void *, char *, size_t, door_desc_t *, uint_t);

static int
cfsd_enter_daemon_lock(void)
{
        int lock_fd;

        lock_fd = open(CFSD_LOCKFILE, O_CREAT|O_RDWR, 644);
        if (lock_fd < 0) {
                syslog(LOG_ERR, "Failed to open lock file: %d\n",errno);
                exit(EXIT_FAILURE);
        }

        return (lockf(lock_fd, F_TLOCK, 0));
}

static int
cfsd_create_door(void)
{
        int dfd = -1;

        if ((dfd = door_create(cfsd_door_func, NULL,
                               DOOR_REFUSE_DESC | DOOR_NO_CANCEL)) == -1) {
                syslog(LOG_ERR, "Unable to create door, no user mode helper\n");
                exit(EXIT_FAILURE);
        }

        return (dfd);
}

/* open a pseudo device and send the door fd down to the kernel */
static void
cfsd_send_to_kernel(int doorfd)
{
        int fd, err;

        if ((fd = open(CFS_DEV, O_RDWR)) == -1) {
                syslog(LOG_ERR,
                       "Unable to open CFS_DEV, no user mode helper\n");
                exit(EXIT_FAILURE);
        }

        if ((err = ioctl(fd, IOC_LIBCFS_SET_DFD, &doorfd)) == -1) {
                syslog(LOG_ERR,
                       "Unable to send door fd, no user mode helper(%d)\n",
                       errno);
                exit(EXIT_FAILURE);
        }

        close(fd);
}

int
main(int argc, char *argv[])
{
        int pid;
        int door_fd;

        /*
         * Close existing file descriptors, open "/dev/null" as
         * standard input, output, and error, and detach from
         * controlling terminal.
         */

        closefrom(0);
        (void) open("/dev/null", O_RDONLY);
        (void) open("/dev/null", O_WRONLY);
        (void) dup(1);

        (void) setsid();
        (void) chdir("/");

        /* create a child to do the work, parent will exit. */
        if ((pid = fork()) < 0)
                exit(EXIT_FAILURE);
        if (pid > 0)
                exit(EXIT_SUCCESS);

        openlog("cfsd", LOG_PID | LOG_NDELAY, LOG_DAEMON);

        /*
         * establish our lock on the lock file and write our pid to it.
         * exit if some other process holds the lock, or if there's any
         * error in writing/locking the file.
         */
        pid = cfsd_enter_daemon_lock();
        if (pid != 0) {
                syslog(LOG_ERR, "Failed to get lock, exiting");
                exit(EXIT_FAILURE);
        }

        door_fd = cfsd_create_door();

        cfsd_send_to_kernel(door_fd);

        /*CONSTCOND*/
        while (1)
                (void) pause();

        return (1);
}

/*
 * fork() and exec() arbitrary programs which are passed up from the kernel
 * via a door.  the payload of the door arg contains an argv and envp.
 */
void
cfsd_usr_md_hlpr(cfs_cfsd_arg_t *argp)
{
        cfs_cfsd_res_t res;
        char         **argv;
        char         **envp;
        int            argc = argp->argc;
        int            envc = argp->envc;
        int            i;
        int            err = 0;
        int            len;
        int            status;
        pid_t          pid, epid;
        char          *datap;

        envp = NULL;
        argv = malloc((sizeof (char *) * (argc + 1)));
        if (argv == NULL) {
                err = ENOMEM;
                status = CFS_CFSD_DR_NOMEM;
                syslog(LOG_ERR, "No memory for user mode helper");
                goto out;
        }

        envp = malloc((sizeof (char *) * (envc + 1)));
        if (envp == NULL) {
                err = ENOMEM;
                status = CFS_CFSD_DR_NOMEM;
                syslog(LOG_ERR, "No memory for user mode helper");
                goto out;
        }

        datap = argp->data;
        for (i = 0; i < argc; i++) {
                argv[i] = datap;
                len = strlen(argv[i]) + 1;      /* add 1 for null */
                datap += len;
        }
        argv[i] = NULL;

        for (i = 0; i < envc; i++) {
                envp[i] = datap;
                len = strlen(envp[i]) + 1;      /* add 1 for null */
                datap += len;
        }
        envp[i] = NULL;

        pid = fork();
        if (pid == -1) {
                err = errno;
                status = CFS_CFSD_DR_NOFORK;
                syslog(LOG_ERR, "Fork of user mode helper failed");
                goto out;
        }
        if (pid == 0) {
                (void) execve(argv[0], argv, envp);
                exit(errno);
        }

        /* parent process (we can't be here in child) */
        epid = waitpid(pid, &status, 0);

        if (epid != pid)
                err = EINTR;
        else
                err = WEXITSTATUS(status);

        status = CFS_CFSD_DR_OK;

out:
        if (argv)
                free(argv);
        if (envp)
                free(envp);
        res.err = err;
        res.status = status;
        (void) door_return((char *)&res, sizeof (cfs_cfsd_res_t), NULL, 0);
}

void
cfsd_door_func(void *c, char *argp, size_t sz, door_desc_t *dp, uint_t cnt)
{
        cfs_cfsd_arg_t *cfsd_argp = (cfs_cfsd_arg_t *)argp;
        cfs_cfsd_res_t  res;

        /* validate the arg */
        if (sz < sizeof (cfs_cfsd_arg_t) || sz < cfsd_argp->len) {
                syslog(LOG_ERR, "Bad arg passed to cfsd_door_func()");
                res.err = EINVAL;
                res.status = CFS_CFSD_DR_BADARG;
                (void) door_return((char *)&res, sizeof (cfs_cfsd_res_t),
                                   NULL, 0);
        }

        switch (cfsd_argp->cmd) {
        case CFS_CFSD_UMH:
                cfsd_usr_md_hlpr(cfsd_argp);
                return;
        default:
                syslog(LOG_ERR, "Bad command passed to cfsd_door_func()");
                break;
        }

        res.err = EINVAL;
        res.status = CFS_CFSD_DR_BADCMD;
        (void) door_return((char *)&res, sizeof (cfs_cfsd_res_t), NULL, 0);
}
