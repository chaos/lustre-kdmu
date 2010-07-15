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
 * libcfs/include/libcfs/solaris/solaris-types.h
 *
 */

#ifndef __LIBCFS_SOLARIS_SOLARIS_TYPES_H__
#define __LIBCFS_SOLARIS_SOLARIS_TYPES_H__

#ifndef __LIBCFS_LIBCFS_H__
#error Do not #include this file directly. #include <libcfs/libcfs.h> instead
#endif

typedef uint8_t        __u8;
typedef uint16_t       __u16;
typedef uint32_t       __u32;
typedef uint64_t       __u64;

typedef	uint8_t        u_int8_t;
typedef	uint16_t       u_int16_t;
typedef	uint32_t       u_int32_t;
typedef	uint64_t       u_int64_t;

typedef uint8_t        u8;
typedef uint16_t       u16;
typedef uint32_t       u32;
typedef uint64_t       u64;

typedef int8_t         __s8;
typedef int16_t        __s16;
typedef int32_t        __s32;
typedef int64_t        __s64;

typedef off_t          loff_t;
typedef uint16_t       cfs_umode_t;

/* long integer with size equal to pointer */
typedef unsigned long ulong_ptr_t;
typedef long long_ptr_t;

#define __user

#define typecheck(type,x) \
({ type __dummy; \
   typeof(x) __dummy2; \
   (void)(&__dummy == &__dummy2); \
   1; \
})

#endif /* __LIBCFS_SOLARIS_SOLARIS_TYPES_H__ */
