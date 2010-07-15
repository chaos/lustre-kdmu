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
 * libcfs/include/libcfs/solaris/kp30.h
 *
 */

#ifndef __LIBCFS_SOLARIS_SOLARIS_KP30_H__
#define __LIBCFS_SOLARIS_SOLARIS_KP30_H__

#ifdef __KERNEL__

#define LASSERT_SPIN_LOCKED(lock) LASSERT(cfs_spin_is_locked(lock))
#define LINVRNT_SPIN_LOCKED(lock) LINVRNT(cfs_spin_is_locked(lock))
#define LASSERT_SEM_LOCKED(sem)   LASSERT(cfs_sem_is_locked(sem))

#define LIBCFS_PANIC(msg) panic(msg)

#define BUG_ON(x) LASSERT(!x)

#define PORTAL_SYMBOL_REGISTER(x)               do {} while(0)
#define PORTAL_SYMBOL_UNREGISTER(x)             do {} while(0)

#define PORTAL_SYMBOL_GET(x)                    (&x)
#define PORTAL_SYMBOL_PUT(x)                    do {} while(0)


#define PORTAL_MODULE_USE                       do {} while(0)
#define PORTAL_MODULE_UNUSE                     do {} while(0)

#define CFS_MODULE_PARM(name, t, type, perm, desc)
#define CFS_MODULE_PARM_STR(name, string, len, perm, desc)

#endif /* End of __KERNEL__ */

#define IOCTL_LIBCFS_TYPE long

#define LI_POISON ((int)0x5a5a5a5a5a5a5a5a)
#define LL_POISON ((long)0x5a5a5a5a5a5a5a5a)
#define LP_POISON ((void *)(long)0x5a5a5a5a5a5a5a5a)

#define LPU64 "%llu"
#define LPD64 "%lld"
#define LPX64 "0x%llx"
#define LPX64i "%llx"
#define LPF64 "ll"
#define LPPID "%d"

#define LPCFSPID "%d"

/*
 * long_ptr_t & ulong_ptr_t, same to "long" for gcc
 */
# define LPLU "%lu"
# define LPLD "%ld"
# define LPLX "%#lx"

/*
 * get_cpu & put_cpu
 */
static inline int
cfs_get_cpu()
{
        kpreempt_disable();
        affinity_set(CPU->cpu_id);

        return CPU->cpu_id;
}

static inline void
cfs_put_cpu()
{
        affinity_clear();
        kpreempt_enable();
}
#endif /* __LIBCFS_SOLARIS_SOLARIS_KP30_H__ */
