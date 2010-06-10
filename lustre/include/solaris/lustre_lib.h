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
 * lustre/include/solaris/lustre_lib.h
 *
 * Basic Lustre library routines.
 */

#ifndef __SOLARIS_LUSTRE_LIB_H__
#define __SOLARIS_LUSTRE_LIB_H__

#ifndef _LUSTRE_LIB_H
#error Do not #include this file directly. #include <lustre_lib.h> instead
#endif

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

/*
 * Block all signals except the ones specified by sigs argument.
 */
static inline cfs_sigset_t
l_w_e_set_sigs(int sigs)
{
        cfs_sigset_t        old;
        cfs_sigset_t        invfatalsigs;
        extern cfs_sigset_t cfs_invlfatalsigs;
        extern cfs_sigset_t cfs_int_to_invsigset(int, cfs_sigset_t *);

        if (ttolwp(curthread) == NULL) {
                sigfillset(&old);
                return (old);
        }

        if (sigs == 0) {
                sigfillset(&invfatalsigs);
        } else if (sigs == LUSTRE_FATAL_SIGS) {
                invfatalsigs = cfs_invlfatalsigs;
        } else {
                cfs_int_to_invsigset(sigs, &invfatalsigs);
        }

        sigreplace(&invfatalsigs, &old);
        return (old);
}

#endif

#endif /* __SOLARIS_LUSTRE_LIB_H__ */
