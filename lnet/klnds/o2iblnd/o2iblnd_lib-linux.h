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
 * lnet/klnds/o2iblnd/o2iblnd_lib-linux.h
 *
 */

#ifndef AUTOCONF_INCLUDED
#include <linux/config.h>
#endif
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/smp_lock.h>
#include <linux/unistd.h>
#include <linux/uio.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/stat.h>
#include <linux/list.h>
#include <linux/kmod.h>
#include <linux/sysctl.h>
#include <linux/random.h>
#include <linux/pci.h>

#include <net/sock.h>
#include <linux/in.h>

#if !HAVE_GFP_T
typedef int gfp_t;
#endif

#include <rdma/rdma_cm.h>
#include <rdma/ib_cm.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_fmr_pool.h>

#if defined(CONFIG_SYSCTL) && !CFS_SYSFS_MODULE_PARM

#ifndef HAVE_SYSCTL_UNNUMBERED

enum {
        O2IBLND_SERVICE  = 1,
        O2IBLND_CKSUM,
        O2IBLND_TIMEOUT,
        O2IBLND_NTX,
        O2IBLND_CREDITS,
        O2IBLND_PEER_TXCREDITS,
        O2IBLND_PEER_CREDITS_HIW,
        O2IBLND_PEER_RTRCREDITS,
        O2IBLND_PEER_TIMEOUT,
        O2IBLND_IPIF_BASENAME,
        O2IBLND_RETRY_COUNT,
        O2IBLND_RNR_RETRY_COUNT,
        O2IBLND_KEEPALIVE,
        O2IBLND_CONCURRENT_SENDS,
        O2IBLND_IB_MTU,
        O2IBLND_MAP_ON_DEMAND,
        O2IBLND_FMR_POOL_SIZE,
        O2IBLND_FMR_FLUSH_TRIGGER,
        O2IBLND_FMR_CACHE,
        O2IBLND_PMR_POOL_SIZE
};
#else

#define O2IBLND_SERVICE          CTL_UNNUMBERED
#define O2IBLND_CKSUM            CTL_UNNUMBERED
#define O2IBLND_TIMEOUT          CTL_UNNUMBERED
#define O2IBLND_NTX              CTL_UNNUMBERED
#define O2IBLND_CREDITS          CTL_UNNUMBERED
#define O2IBLND_PEER_TXCREDITS   CTL_UNNUMBERED
#define O2IBLND_PEER_CREDITS_HIW CTL_UNNUMBERED
#define O2IBLND_PEER_RTRCREDITS  CTL_UNNUMBERED
#define O2IBLND_PEER_TIMEOUT     CTL_UNNUMBERED
#define O2IBLND_IPIF_BASENAME    CTL_UNNUMBERED
#define O2IBLND_RETRY_COUNT      CTL_UNNUMBERED
#define O2IBLND_RNR_RETRY_COUNT  CTL_UNNUMBERED
#define O2IBLND_KEEPALIVE        CTL_UNNUMBERED
#define O2IBLND_CONCURRENT_SENDS CTL_UNNUMBERED
#define O2IBLND_IB_MTU           CTL_UNNUMBERED
#define O2IBLND_MAP_ON_DEMAND    CTL_UNNUMBERED
#define O2IBLND_FMR_POOL_SIZE    CTL_UNNUMBERED
#define O2IBLND_FMR_FLUSH_TRIGGER CTL_UNNUMBERED
#define O2IBLND_FMR_CACHE        CTL_UNNUMBERED
#define O2IBLND_PMR_POOL_SIZE    CTL_UNNUMBERED

#endif

#endif /* defined(CONFIG_SYSCTL) && !CFS_SYSFS_MODULE_PARM */

typedef struct ib_recv_wr  kib_recv_wr_t;
typedef struct ib_send_wr  kib_send_wr_t;
typedef struct ib_sge      kib_sge_t;

typedef struct scatterlist kib_frags_t;

typedef struct
{
        enum dma_data_direction pm_dir;
        struct scatterlist      pm_sgl[0];
} kib_page_map_t;

#define kiblnd_init_page_map(pm, dir)                                       \
do {                                                                        \
        pm->pm_dir = dir;                                                   \
} while (0)

#define kiblnd_get_dma_address(p, idx) kiblnd_sg_dma_address(p->ibp_device, \
                                                             &p->ibp_map->pm_sgl[idx])
#define kiblnd_get_dma_len(p, idx)     kiblnd_sg_dma_len(p->ibp_device,     \
                                                         &p->ibp_map->pm_sgl[idx])

#define IBLND_TX_RDMA_FRAGS_NUM IBLND_MAX_RDMA_FRAGS

struct kib_fmr_pool;

typedef struct kib_fmr {
        struct ib_pool_fmr     *fmr_pfmr;               /* IB pool fmr */
        struct kib_fmr_pool    *fmr_pool;               /* pool of FMR */
} kib_fmr_t;

struct kib_phys_mr;

typedef union {
        struct kib_phys_mr *pmr;        /* MR for physical buffer */
        kib_fmr_t           fmr;        /* FMR */
} kib_mr_t;

#define LASSERT_TX_NOMR(kib_mr) LASSERT(kib_mr.pmr == NULL)

typedef struct {
        int     *kib_plat_pmr_pool_size;     /* # physical MR in pool */
        int     *kib_plat_fmr_pool_size;     /* # FMRs in pool */
        int     *kib_plat_fmr_flush_trigger; /* When to trigger FMR flush */
        int     *kib_plat_fmr_cache;         /* enable FMR pool cache? */

#define kib_pmr_pool_size     kib_plat_tunes.kib_plat_pmr_pool_size
#define kib_fmr_pool_size     kib_plat_tunes.kib_plat_fmr_pool_size
#define kib_fmr_flush_trigger kib_plat_tunes.kib_plat_fmr_flush_trigger
#define kib_fmr_cache         kib_plat_tunes.kib_plat_fmr_cache
} kib_plat_tunables_t;

struct kib_fmr_poolset;
struct kib_pmr_poolset;

typedef struct {
        struct kib_fmr_poolset *ibn_plat_fmr_ps;     /* fmr pool-set */
        struct kib_pmr_poolset *ibn_plat_pmr_ps;     /* pmr pool-set */

#define ibn_fmr_ps     ibn_plat.ibn_plat_fmr_ps
#define ibn_pmr_ps     ibn_plat.ibn_plat_pmr_ps
} kib_plat_net_t;

typedef struct {} kib_tx_plat_t;

#define kiblnd_init_tx_frags(tx)        do {} while (0)

#ifdef HAVE_OFED_IB_DMA_MAP

static inline int kiblnd_dma_map_sg(struct ib_device *dev,
                                    struct scatterlist *sg, int nents,
                                    enum dma_data_direction direction)
{
        return ib_dma_map_sg(dev, sg, nents, direction);
}

static inline void kiblnd_dma_unmap_sg(struct ib_device *dev,
                                       struct scatterlist *sg, int nents,
                                       enum dma_data_direction direction)
{
        ib_dma_unmap_sg(dev, sg, nents, direction);
}

static inline __u64 kiblnd_sg_dma_address(struct ib_device *dev,
                                          struct scatterlist *sg)
{
        return ib_sg_dma_address(dev, sg);
}

static inline unsigned int kiblnd_sg_dma_len(struct ib_device *dev,
                                             struct scatterlist *sg)
{
        return ib_sg_dma_len(dev, sg);
}

/* XXX We use KIBLND_CONN_PARAM(e) as writable buffer, it's not strictly
 * right because OFED1.2 defines it as const, to use it we have to add
 * (void *) cast to overcome "const" */

#define KIBLND_CONN_PARAM(e)            ((e)->param.conn.private_data)
#define KIBLND_CONN_PARAM_LEN(e)        ((e)->param.conn.private_data_len)

#else

static inline int kiblnd_dma_map_sg(struct ib_device *dev,
                                    struct scatterlist *sg, int nents,
                                    enum dma_data_direction direction)
{
        return dma_map_sg(dev->dma_device, sg, nents, direction);
}

static inline void kiblnd_dma_unmap_sg(struct ib_device *dev,
                                       struct scatterlist *sg, int nents,
                                       enum dma_data_direction direction)
{
        return dma_unmap_sg(dev->dma_device, sg, nents, direction);
}


static inline dma_addr_t kiblnd_sg_dma_address(struct ib_device *dev,
                                               struct scatterlist *sg)
{
        return sg_dma_address(sg);
}


static inline unsigned int kiblnd_sg_dma_len(struct ib_device *dev,
                                             struct scatterlist *sg)
{
        return sg_dma_len(sg);
}

#define KIBLND_CONN_PARAM(e)            ((e)->private_data)
#define KIBLND_CONN_PARAM_LEN(e)        ((e)->private_data_len)

#endif

#define UNUSED(x)       ( (void)(x) )

#define kiblnd_set_sge(sge, key, address, len)                             \
do {                                                                       \
        (sge)->lkey   = key;                                               \
        (sge)->addr   = address;                                           \
        (sge)->length = len;                                               \
} while (0)

#define kiblnd_set_rx_wrq(wrq, nxt, sgl, nsge, id)                         \
do {                                                                       \
        (wrq)->next    = nxt;                                              \
        (wrq)->sg_list = sgl;                                              \
        (wrq)->num_sge = nsge;                                             \
        (wrq)->wr_id   = id;                                               \
} while (0)

#define kiblnd_set_tx_wrq(wrq, nxt, sgl, nsge, id)                         \
do {                                                                       \
        kiblnd_set_rx_wrq(wrq, nxt, sgl, nsge, id);                        \
        (wrq)->opcode     = IB_WR_SEND;                                    \
        (wrq)->send_flags = IB_SEND_SIGNALED;                              \
} while (0)

#define kiblnd_set_rdma_wrq(wrq, nxt, sgl, nsge, id, raddr, remote_key)    \
do {                                                                       \
        kiblnd_set_rx_wrq(wrq, nxt, sgl, nsge, id);                        \
        (wrq)->opcode              = IB_WR_RDMA_WRITE;                     \
        (wrq)->send_flags          = 0;                                    \
        (wrq)->wr.rdma.remote_addr = raddr;                                \
        (wrq)->wr.rdma.rkey        = remote_key;                           \
} while (0)

static inline int kiblnd_post_recv(struct ib_qp	  *qp,
                                   kib_recv_wr_t  *wrq,
                                   kib_recv_wr_t **bad_wrq,
                                   unsigned int    size)
{
        return ib_post_recv(qp, wrq, bad_wrq);
}

static inline int kiblnd_post_send(struct ib_qp	  *qp,
                                   kib_send_wr_t  *wrq,
                                   kib_send_wr_t **bad_wrq,
                                   unsigned int    size)
{
        return ib_post_send(qp, wrq, bad_wrq);
}
