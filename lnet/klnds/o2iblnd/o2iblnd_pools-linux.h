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
 * lnet/klnds/o2iblnd/o2iblnd_pools-linux.h
 *
 */

struct kib_pmr_pool;

typedef struct kib_phys_mr {
        cfs_list_t              pmr_list;               /* chain node */
        struct ib_phys_buf     *pmr_ipb;                /* physical buffer */
        struct ib_mr           *pmr_mr;                 /* IB MR */
        struct kib_pmr_pool    *pmr_pool;               /* owner of this MR */
        __u64                   pmr_iova;               /* Virtual I/O address */
        int                     pmr_refcount;           /* reference count */
} kib_phys_mr_t;

typedef struct kib_pmr_poolset {
        kib_poolset_t           pps_poolset;            /* pool-set */
} kib_pmr_poolset_t;

typedef struct kib_pmr_pool {
        kib_pool_t              ppo_pool;               /* pool */
} kib_pmr_pool_t;

typedef struct kib_fmr_poolset {
        cfs_spinlock_t          fps_lock;               /* serialize */
        struct kib_net         *fps_net;                /* IB network */
        cfs_list_t              fps_pool_list;          /* FMR pool list */
        __u64                   fps_version;            /* validity stamp */
        int                     fps_increasing;         /* is allocating new pool */
        cfs_time_t              fps_next_retry;         /* time stamp for retry if failed to allocate */
} kib_fmr_poolset_t;

typedef struct kib_fmr_pool
{
        cfs_list_t              fpo_list;               /* chain on pool list */
        struct kib_fmr_poolset *fpo_owner;              /* owner of this pool */
        struct ib_fmr_pool     *fpo_fmr_pool;           /* IB FMR pool */
        cfs_time_t              fpo_deadline;           /* deadline of this pool */
        int                     fpo_map_count;          /* # of mapped FMR */
} kib_fmr_pool_t;

int  kiblnd_fmr_pool_map(kib_fmr_poolset_t *fps, __u64 *pages,
                         int npages, __u64 iov, struct kib_fmr *fmr);
void kiblnd_fmr_pool_unmap(struct kib_fmr *fmr, int status);

int  kiblnd_pmr_pool_map(kib_pmr_poolset_t *pps, kib_rdma_desc_t *rd,
                         __u64 *iova, kib_phys_mr_t **pp_pmr);
void kiblnd_pmr_pool_unmap(kib_phys_mr_t *pmr);
