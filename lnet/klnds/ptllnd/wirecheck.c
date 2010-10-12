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
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lnet/klnds/ptllnd/wirecheck.c
 *
 * Author: PJ Kirner <pjkirner@clusterfs.com>
 */
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <config.h>

#include <lnet/api-support.h>

/* This ghastly hack to allows me to include lib-types.h It doesn't affect any
 * assertions generated here (but fails-safe if it ever does) */
typedef struct {
        int     counter;
} cfs_atomic_t;

#include <lnet/lib-types.h>
#include <lnet/ptllnd_wire.h>

#ifndef HAVE_STRNLEN
#define strnlen(s, i) strlen(s)
#endif

#define BLANK_LINE()                            \
do {                                            \
        printf ("\n");                          \
} while (0)

#define COMMENT(c)                              \
do {                                            \
        printf ("        /* "c" */\n");         \
} while (0)

#undef STRINGIFY
#define STRINGIFY(a) #a

#define CHECK_DEFINE(a)                                         \
do {                                                            \
        printf ("        CLASSERT ("#a" == "STRINGIFY(a)");\n"); \
} while (0)

#define CHECK_VALUE(a)                                  \
do {                                                    \
        printf ("        CLASSERT ("#a" == %d);\n", a);  \
} while (0)

#define CHECK_MEMBER_OFFSET(s,m)                \
do {                                            \
        CHECK_VALUE((int)offsetof(s, m));       \
} while (0)

#define CHECK_MEMBER_SIZEOF(s,m)                \
do {                                            \
        CHECK_VALUE((int)sizeof(((s *)0)->m));  \
} while (0)

#define CHECK_MEMBER(s,m)                       \
do {                                            \
        CHECK_MEMBER_OFFSET(s, m);              \
        CHECK_MEMBER_SIZEOF(s, m);              \
} while (0)

#define CHECK_STRUCT(s)                         \
do {                                            \
        BLANK_LINE ();                          \
        COMMENT ("Checks for struct "#s);       \
        CHECK_VALUE((int)sizeof(s));            \
} while (0)

void
system_string (char *cmdline, char *str, int len)
{
        int   fds[2];
        int   rc;
        pid_t pid;

        rc = pipe (fds);
        if (rc != 0)
                abort ();

        pid = fork ();
        if (pid == 0) {
                /* child */
                int   fd = fileno(stdout);

                rc = dup2(fds[1], fd);
                if (rc != fd)
                        abort();

                exit(system(cmdline));
                /* notreached */
        } else if ((int)pid < 0) {
                abort();
        } else {
                FILE *f = fdopen (fds[0], "r");

                if (f == NULL)
                        abort();

                close(fds[1]);

                if (fgets(str, len, f) == NULL)
                        abort();

                if (waitpid(pid, &rc, 0) != pid)
                        abort();

                if (!WIFEXITED(rc) ||
                    WEXITSTATUS(rc) != 0)
                        abort();

                if (strnlen(str, len) == len)
                        str[len - 1] = 0;

                if (str[strlen(str) - 1] == '\n')
                        str[strlen(str) - 1] = 0;

                fclose(f);
        }
}

int
main (int argc, char **argv)
{
        char unameinfo[80];
        char gccinfo[80];

        system_string("uname -a", unameinfo, sizeof(unameinfo));
        system_string("gcc -v 2>&1 | tail -1", gccinfo, sizeof(gccinfo));

        printf ("void kptllnd_assert_wire_constants (void)\n"
                "{\n"
                "        /* Wire protocol assertions generated by 'wirecheck'\n"
                "         * running on %s\n"
                "         * with %s */\n"
                "\n", unameinfo, gccinfo);

        BLANK_LINE ();

        COMMENT ("Constants...");
        CHECK_DEFINE (PTL_RESERVED_MATCHBITS);
        CHECK_DEFINE (LNET_MSG_MATCHBITS);
        
        CHECK_DEFINE (PTLLND_MSG_MAGIC);
        CHECK_DEFINE (PTLLND_MSG_VERSION);

        CHECK_DEFINE (PTLLND_RDMA_OK);
        CHECK_DEFINE (PTLLND_RDMA_FAIL);

        CHECK_DEFINE (PTLLND_MSG_TYPE_INVALID);
        CHECK_DEFINE (PTLLND_MSG_TYPE_PUT);
        CHECK_DEFINE (PTLLND_MSG_TYPE_GET);
        CHECK_DEFINE (PTLLND_MSG_TYPE_IMMEDIATE);
        CHECK_DEFINE (PTLLND_MSG_TYPE_NOOP);
        CHECK_DEFINE (PTLLND_MSG_TYPE_HELLO);
        CHECK_DEFINE (PTLLND_MSG_TYPE_NAK);

        CHECK_STRUCT (kptl_msg_t);
        CHECK_MEMBER (kptl_msg_t, ptlm_magic);
        CHECK_MEMBER (kptl_msg_t, ptlm_version);
        CHECK_MEMBER (kptl_msg_t, ptlm_type);
        CHECK_MEMBER (kptl_msg_t, ptlm_credits);
        CHECK_MEMBER (kptl_msg_t, ptlm_nob);
        CHECK_MEMBER (kptl_msg_t, ptlm_cksum);
        CHECK_MEMBER (kptl_msg_t, ptlm_srcnid);
        CHECK_MEMBER (kptl_msg_t, ptlm_srcstamp);
        CHECK_MEMBER (kptl_msg_t, ptlm_dstnid);
        CHECK_MEMBER (kptl_msg_t, ptlm_dststamp);
        CHECK_MEMBER (kptl_msg_t, ptlm_srcpid);
        CHECK_MEMBER (kptl_msg_t, ptlm_dstpid);
        CHECK_MEMBER (kptl_msg_t, ptlm_u.immediate);
        CHECK_MEMBER (kptl_msg_t, ptlm_u.rdma);
        CHECK_MEMBER (kptl_msg_t, ptlm_u.hello);

        CHECK_STRUCT (kptl_immediate_msg_t);
        CHECK_MEMBER (kptl_immediate_msg_t, kptlim_hdr);
        CHECK_MEMBER (kptl_immediate_msg_t, kptlim_payload[13]);

        CHECK_STRUCT (kptl_rdma_msg_t);
        CHECK_MEMBER (kptl_rdma_msg_t, kptlrm_hdr);
        CHECK_MEMBER (kptl_rdma_msg_t, kptlrm_matchbits);

        CHECK_STRUCT (kptl_hello_msg_t);
        CHECK_MEMBER (kptl_hello_msg_t, kptlhm_matchbits);
        CHECK_MEMBER (kptl_hello_msg_t, kptlhm_max_msg_size);

        printf ("}\n\n");

        return (0);
}
