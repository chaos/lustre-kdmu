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
 * Copyright (c) 2001, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/include/linux/lustre_lib.h
 *
 * Basic Lustre library routines.
 */

#ifndef _LINUX_LUSTRE_LIB_H
#define _LINUX_LUSTRE_LIB_H

#ifndef _LUSTRE_LIB_H
#error Do not #include this file directly. #include <lustre_lib.h> instead
#endif

#ifndef __KERNEL__
# include <string.h>
# include <sys/types.h>
#else
# include <linux/rwsem.h>
# include <linux/sched.h>
# include <linux/signal.h>
# include <linux/types.h>
#endif
#include <linux/lustre_compat25.h>

#ifndef LP_POISON
#if BITS_PER_LONG > 32
# define LI_POISON ((int)0x5a5a5a5a5a5a5a5a)
# define LL_POISON ((long)0x5a5a5a5a5a5a5a5a)
# define LP_POISON ((void *)(long)0x5a5a5a5a5a5a5a5a)
#else
# define LI_POISON ((int)0x5a5a5a5a)
# define LL_POISON ((long)0x5a5a5a5a)
# define LP_POISON ((void *)(long)0x5a5a5a5a)
#endif
#endif

#define OBD_IOC_DATA_TYPE               long

#define LUSTRE_FATAL_SIGS (sigmask(SIGKILL) | sigmask(SIGINT) |                \
                           sigmask(SIGTERM) | sigmask(SIGQUIT) |               \
                           sigmask(SIGALRM))

#ifdef __KERNEL__
static inline sigset_t l_w_e_set_sigs(int sigs)
{
        sigset_t old;
        unsigned long irqflags;

        SIGNAL_MASK_LOCK(current, irqflags);
        old = current->blocked;
        siginitsetinv(&current->blocked, sigs);
        RECALC_SIGPENDING;
        SIGNAL_MASK_UNLOCK(current, irqflags);

        return old;
}
#endif

#ifdef __KERNEL__
/* initialize ost_lvb according to inode */
static inline void inode_init_lvb(struct inode *inode, struct ost_lvb *lvb)
{
        lvb->lvb_size = i_size_read(inode);
        lvb->lvb_blocks = inode->i_blocks;
        lvb->lvb_mtime = LTIME_S(inode->i_mtime);
        lvb->lvb_atime = LTIME_S(inode->i_atime);
        lvb->lvb_ctime = LTIME_S(inode->i_ctime);
}
#else
/* defined in liblustre/llite_lib.h */
#endif

#endif /* _LUSTRE_LIB_H */
