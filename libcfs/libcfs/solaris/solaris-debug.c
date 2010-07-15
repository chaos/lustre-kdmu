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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * libcfs/libcfs/solaris/solaris-debug.c
 *
 */

# define DEBUG_SUBSYSTEM S_LNET

#include <libcfs/libcfs.h>
#include "../tracefile.h"

char lnet_upcall[1024] = "/usr/lib/lustre/lnet_upcall";
char lnet_debug_log_upcall[1024] = "/usr/lib/lustre/lnet_debug_log_upcall";

/**
 * Upcall function once a Lustre log has been dumped.
 *
 * \param file  path of the dumped log
 */
void libcfs_run_debug_log_upcall(char *file)
{
        char *argv[3];
        int   rc;
        char *envp[] = {
                "HOME=/",
                "PATH=/sbin:/bin:/usr/sbin:/usr/bin",
                NULL};

        argv[0] = lnet_debug_log_upcall;

        LASSERTF(file != NULL, "called on a null filename\n");
        argv[1] = file; //only need to pass the path of the file

        argv[2] = NULL;

        rc = USERMODEHELPER(argv[0], argv, envp);
        if (rc < 0 && rc != -ENOENT) {
                CERROR("Error %d invoking LNET debug log upcall %s %s; "
                       "check /proc/sys/lnet/debug_log_upcall\n",
                       rc, argv[0], argv[1]);
        } else {
                CDEBUG(D_HA, "Invoked LNET debug log upcall %s %s\n",
                       argv[0], argv[1]);
        }
}

void libcfs_run_upcall(char **argv)
{
        int   rc;
        int   argc;
        char *envp[] = {
                "HOME=/",
                "PATH=/sbin:/bin:/usr/sbin:/usr/bin",
                NULL};

        argv[0] = lnet_upcall;
        argc = 1;
        while (argv[argc] != NULL)
                argc++;

        LASSERT(argc >= 2);

        rc = USERMODEHELPER(argv[0], argv, envp);
        if (rc < 0 && rc != -ENOENT) {
                CERROR("Error %d invoking LNET upcall %s %s%s%s%s%s%s%s%s; "
                       "check /proc/sys/lnet/upcall\n",
                       rc, argv[0], argv[1],
                       argc < 3 ? "" : ",", argc < 3 ? "" : argv[2],
                       argc < 4 ? "" : ",", argc < 4 ? "" : argv[3],
                       argc < 5 ? "" : ",", argc < 5 ? "" : argv[4],
                       argc < 6 ? "" : ",...");
        } else {
                CDEBUG(D_HA, "Invoked LNET upcall %s %s%s%s%s%s%s%s%s\n",
                       argv[0], argv[1],
                       argc < 3 ? "" : ",", argc < 3 ? "" : argv[2],
                       argc < 4 ? "" : ",", argc < 4 ? "" : argv[3],
                       argc < 5 ? "" : ",", argc < 5 ? "" : argv[4],
                       argc < 6 ? "" : ",...");
        }
}

void libcfs_run_lbug_upcall(const char *file, const char *fn, const int line)
{
        char *argv[6];
        char buf[32];

        snprintf (buf, sizeof buf, "%d", line);

        argv[1] = "LBUG";
        argv[2] = (char *)file;
        argv[3] = (char *)fn;
        argv[4] = buf;
        argv[5] = NULL;

        libcfs_run_upcall (argv);
}

void lbug_with_loc(const char *file, const char *func, const int line)
{
        libcfs_catastrophe = 1;
        libcfs_debug_msg(NULL, 0, D_EMERG, file, func, line, "LBUG\n");

        if (servicing_interrupt()) {
                panic("LBUG in interrupt.\n");
                /* not reached */
        }

        /* calling libcfs_debug_dumpstack() is useless here as far as
         * we don't support it on Solaris: libcfs_debug_dumpstack(NULL); */

        if (!libcfs_panic_on_lbug)
                libcfs_debug_dumplog();
        libcfs_run_lbug_upcall(file, func, line);
        if (libcfs_panic_on_lbug)
                panic("LBUG");

        CWARN("current thread (%p) got LBUG() but libcfs_panic_on_lbug is off -"
              "suspending futher execution of this thread...");
        while (1)
                delay(10000);
}

void libcfs_debug_dumpstack(cfs_task_t *tsk)
{
        CWARN("can't show stack: not supported\n");
}

void libcfs_register_panic_notifier(void)
{
}

void libcfs_unregister_panic_notifier(void)
{
}
