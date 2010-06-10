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
 * libcfs/include/libcfs/solaris/libcfs.h
 *
 */

#ifndef __LIBCFS_SOLARIS_SOLARIS_LIBCFS_H__
#define __LIBCFS_SOLARIS_SOLARIS_LIBCFS_H__

#ifndef __LIBCFS_LIBCFS_H__
#error Do not #include this file directly. #include <libcfs/libcfs.h> instead
#endif

#ifndef __KERNEL__
#error This include is only for kernel use.
#endif 

#include <sys/stat.h>
#include <sys/cpuvar.h>
#include <sys/sunddi.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ksocket.h>
#include <sys/time.h>
#include <sys/atomic.h>
#include <sys/ksynch.h>
#include <sys/conf.h>      /* used by dev_ops and cb_ops */
#include <sys/sysmacros.h> /* for P2ROUNDUP_TYPED */
#include <sys/bitmap.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/ioccom.h>
#include <sys/param.h>
#include <sys/random.h>  /* for random_get_pseudo_bytes() */
#include <sys/cmn_err.h> /* for panic() */
#include <sys/cpuvar.h>  /* for kpreempt_* and affinity_* */
#include <sys/crc32.h>
#include <sys/byteorder.h>

#include <libcfs/solaris/solaris-types.h>
#include <libcfs/solaris/solaris-tcpip.h>
#include <libcfs/solaris/solaris-time.h>
#include <libcfs/solaris/solaris-mem.h>
#include <libcfs/solaris/solaris-lock.h>
#include <libcfs/solaris/solaris-prim.h>
#include <libcfs/solaris/solaris-bitops.h>
#include <libcfs/solaris/solaris-fs.h>
#include <libcfs/solaris/solaris-utils.h>
#include <libcfs/solaris/kp30.h>

/* For some reasons generic libcfs_private.h defines it only if !__KERNEL__ */
#ifdef __GNUC__
# define likely(x)      __builtin_expect(!!(x), 1)
# define unlikely(x)    __builtin_expect(!!(x), 0)
#else
# define likely(x)	(!!(x))
# define unlikely(x)	(!!(x))
#endif

/* Byte flipping */
#define __swab16s(x) do {*(x) = ddi_swap16(*(x));} while (0)
#define __swab32s(x) do {*(x) = ddi_swap32(*(x));} while (0)
#define __swab64s(x) do {*(x) = ddi_swap64(*(x));} while (0)
#define __swab16(x)  ddi_swap16(x)
#define __swab32(x)  ddi_swap32(x)
#define __swab64(x)  ddi_swap64(x)
#if defined(_LITTLE_ENDIAN)
# define __le16_to_cpu(x) (x)
# define __cpu_to_le16(x) (x)
# define __le32_to_cpu(x) (x)
# define __cpu_to_le32(x) (x)
# define __le64_to_cpu(x) (x)
# define __cpu_to_le64(x) (x)
# define __be16_to_cpu(x) ddi_swap16(x)
# define __cpu_to_be16(x) ddi_swap16(x)
# define __be32_to_cpu(x) ddi_swap32(x)
# define __cpu_to_be32(x) ddi_swap32(x)
# define __be64_to_cpu(x) ddi_swap64(x)
# define __cpu_to_be64(x) ddi_swap64(x)
#elif defined(_BIG_ENDIAN)
# define __le16_to_cpu(x) ddi_swap16(x)
# define __cpu_to_le16(x) ddi_swap16(x)
# define __le32_to_cpu(x) ddi_swap32(x)
# define __cpu_to_le32(x) ddi_swap32(x)
# define __le64_to_cpu(x) ddi_swap64(x)
# define __cpu_to_le64(x) ddi_swap64(x)
# define __be16_to_cpu(x) (x)
# define __cpu_to_be16(x) (x)
# define __be32_to_cpu(x) (x)
# define __cpu_to_be32(x) (x)
# define __be64_to_cpu(x) (x)
# define __cpu_to_be64(x) (x)
#else
#error "Undefined byteorder??"
#endif /* _LITTLE_ENDIAN */

# define le16_to_cpu(x) __le16_to_cpu(x)
# define cpu_to_le16(x) __cpu_to_le16(x)
# define le32_to_cpu(x) __le32_to_cpu(x)
# define cpu_to_le32(x) __cpu_to_le32(x)
# define le64_to_cpu(x) __le64_to_cpu(x)
# define cpu_to_le64(x) __cpu_to_le64(x)
# define be16_to_cpu(x) __be16_to_cpu(x)
# define cpu_to_be16(x) __cpu_to_be16(x)
# define be32_to_cpu(x) __be32_to_cpu(x)
# define cpu_to_be32(x) __cpu_to_be32(x)
# define be64_to_cpu(x) __be64_to_cpu(x)
# define cpu_to_be64(x) __cpu_to_be64(x)

extern cpuset_t	cpu_ready_set;

static inline void
cfs_cpuset_find_first(cpuset_t *s, int *id)
{
        CPUSET_FIND(*s, *id);
}

static inline void
cfs_cpuset_find_next(cpuset_t *s, int *id)
{
        uint_t  i   = (*id + 1) / BT_NBIPUL;
        int     off = (*id + 1) % BT_NBIPUL;

        *id = -1;

	/*
	 * Find a cpu in the cpuset
	 */
#if CPUSET_WORDS > 1
        for (; i < CPUSET_WORDS; i++, off = 0) {
                *id = (uint_t)(lowbit((s->cpub[i]) & (~0UL << off)) - 1);
                if (*id != -1) {
                        *id += i * BT_NBIPUL;
                        break;
                }
        }
#else
        if ( i == 0 )
                *id = (uint_t)(lowbit((*s) & (~0UL << off)) - 1);
#endif
}

#define cfs_for_each_possible_cpu(cpuid)                    \
        for (cfs_cpuset_find_first(&cpu_ready_set, &cpuid); \
             cpuid != -1;                                   \
             cfs_cpuset_find_next(&cpu_ready_set, &cpuid))

#define CFS_CHECK_STACK() do { } while(0)
#define CDEBUG_STACK() (0L)

#define LUSTRE_LNET_PID          12345

#define ENTRY_NESTING do {} while(0)
#define EXIT_NESTING  do {} while(0)
#define __current_nesting_level() (0)

#define CFS_CURPROC_COMM_MAX (16)

#endif /* __LIBCFS_SOLARIS_LIBCFS_H__ */
