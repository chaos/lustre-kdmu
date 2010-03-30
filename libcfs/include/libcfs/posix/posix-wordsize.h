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
 * Copyright  2008 Sun Microsystems, Inc. All rights reserved
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * libcfs/include/libcfs/posix/posix-wordsize.h
 *
 * Wordsize related  defines for posix userspace.
 *
 * Author: Robert Read <rread@sun.com>
 */

#ifndef __LIBCFS_LINUX_KP30_H__
#define __LIBCFS_LINUX_KP30_H__



#if defined(__CYGWIN__)
# include <cygwin-ioctl.h>
#endif
#ifdef HAVE_SYS_IOCTL_H
# include <sys/ioctl.h>
#endif
#ifndef _IOWR
# include "ioctl.h"
#endif

# define CFS_MODULE_PARM(name, t, type, perm, desc)
#define PORTAL_SYMBOL_GET(x) inter_module_get(#x)
#define PORTAL_SYMBOL_PUT(x) inter_module_put(#x)


#ifdef __CYGWIN__
# ifndef BITS_PER_LONG
#  if (~0UL) == 0xffffffffUL
#   define BITS_PER_LONG 32
#  else
#   define BITS_PER_LONG 64
#  endif
# endif
#else 
#  define BITS_PER_LONG __WORDSIZE
#endif


/******************************************************************************/
/* Light-weight trace
 * Support for temporary event tracing with minimal Heisenberg effect. */
#define LWT_SUPPORT  0

#define LWT_MEMORY   (16<<20)

typedef struct {
        long long   lwte_when;
        char       *lwte_where;
        void       *lwte_task;
        long        lwte_p1;
        long        lwte_p2;
        long        lwte_p3;
        long        lwte_p4;
# if BITS_PER_LONG > 32
        long        lwte_pad;
# endif
} lwt_event_t;

#if LWT_SUPPORT
#define LWT_EVENT(p1,p2,p3,p4)     /* no userland implementation yet */
#endif /* LWT_SUPPORT */

/* ------------------------------------------------------------------ */

#define IOCTL_LIBCFS_TYPE long


#if BITS_PER_LONG > 32
# define LI_POISON ((int)0x5a5a5a5a5a5a5a5a)
# define LL_POISON ((long)0x5a5a5a5a5a5a5a5a)
# define LP_POISON ((void *)(long)0x5a5a5a5a5a5a5a5a)
#else
# define LI_POISON ((int)0x5a5a5a5a)
# define LL_POISON ((long)0x5a5a5a5a)
# define LP_POISON ((void *)(long)0x5a5a5a5a)
#endif

#if (defined(__KERNEL__) && defined(HAVE_KERN__U64_LONG_LONG)) || \
    (!defined(__KERNEL__) && defined(HAVE_USER__U64_LONG_LONG))
/* x86_64 defines __u64 as "long" in userspace, but "long long" in the kernel */
# define LPU64 "%Lu"
# define LPD64 "%Ld"
# define LPX64 "%#Lx"
# define LPX64i "%Lx"
# define LPF64 "L"
#elif (BITS_PER_LONG == 32)
# define LPU64 "%Lu"
# define LPD64 "%Ld"
# define LPX64 "%#Lx"
# define LPX64i "%Lx"
# define LPF64 "L"
#elif (BITS_PER_LONG == 64)
# define LPU64 "%lu"
# define LPD64 "%ld"
# define LPX64 "%#lx"
# define LPX64i "%lx"
# define LPF64 "l"
#endif

#ifdef HAVE_SIZE_T_LONG
# define LPSZ  "%lu"
# define LPSZX "%lx"
#else
# define LPSZ  "%u"
# define LPSZX "%x"
#endif

#ifdef HAVE_SSIZE_T_LONG
# define LPSSZ "%ld"
#else
# define LPSSZ "%d"
#endif

#ifndef LPU64
# error "No word size defined"
#endif

/*
 * long_ptr_t & ulong_ptr_t, same to "long" for gcc
 */
# define LPLU "%lu"
# define LPLD "%ld"
# define LPLX "%#lx"

/*
 * pid_t
 */
# define LPPID "%d"

#endif
