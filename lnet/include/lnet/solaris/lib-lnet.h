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
 * lnet/include/lnet/solaris/lib-lnet.h
 *
 */

#ifndef __LNET_SOLARIS_LIB_LNET_H__
#define __LNET_SOLARIS_LIB_LNET_H__

#ifndef __LNET_LIB_LNET_H__
#error Do not #include this file directly. #include <lnet/lib-lnet.h> instead
#endif

/* It's not clear yet which system includes we need on Solaris */

#define LNET_ROUTER

#ifdef __KERNEL__
# include <libcfs/libcfs.h>

static inline __u64
lnet_page2phys (cfs_page_t *cfs_pg)
{
        pfn_t pfn = page_pptonum(cfs_page_2_ospage(cfs_pg));
        return (ptob(pfn));
}

#endif /* __KERNEL__ */

#endif
