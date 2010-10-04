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
 * lnet/klnds/o2iblnd/o2iblnd_lib-solaris.h
 *
 */

#include <sys/ib/clients/of/sol_ofs/sol_ofs_common.h>
#include <sys/ib/clients/of/ofed_kernel.h>
#include <sys/ib/clients/of/rdma/ib_addr.h>
#include <sys/ib/clients/of/rdma/rdma_cm.h>

#define IB_CM_REJ_STALE_CONN         IBT_CM_CONN_STALE
#define IB_CM_REJ_INVALID_SERVICE_ID IBT_CM_INVALID_SID
#define IB_CM_REJ_CONSUMER_DEFINED   IBT_CM_CONSUMER

typedef ibt_recv_wr_t kib_recv_wr_t;
typedef ibt_send_wr_t kib_send_wr_t;
typedef ibt_wr_ds_t   kib_sge_t;

typedef struct buf    kib_frags_t;

/* On solaris, one "struct buf" may describe many frags
   (see B_SHADOW semantic) */
#define IBLND_TX_RDMA_FRAGS_NUM 1

typedef struct
{
        ibt_mi_hdl_t   pm_mi_hdl;
        kib_sge_t      pm_sgl[0];
} kib_page_map_t;

#define kiblnd_init_page_map(pm, dir)   do {} while (0)

#define kiblnd_get_dma_address(p, idx) (p->ibp_map->pm_sgl[idx].ds_va)
#define kiblnd_get_dma_len(p, idx)     (p->ibp_map->pm_sgl[idx].ds_len)

struct ib_mr
{
        ibt_mr_hdl_t hdl;
        ibt_lkey_t   lkey;
        ibt_rkey_t   rkey;
};

typedef union {
        ibt_mr_hdl_t        mr_hdl;
        ibt_mi_hdl_t        mi_hdl;
} kib_mr_t;

#define LASSERT_TX_NOMR(kib_mr) LASSERT(kib_mr.mr_hdl == NULL)

typedef enum {
        IBLND_MR_UNDEF = 0, /* undefined */
        IBLND_MR_GLOBAL,    /* global MR used */
        IBLND_MR_NO_GLOBAL, /* global MR not used */
} kib_tx_mr_type_t;

typedef struct {} kib_plat_tunables_t;
typedef struct {} kib_plat_net_t;

typedef struct {
        kib_tx_mr_type_t     tx_plat_mr_type;   /* use global MR or not */
        struct iovec        *tx_plat_iov;       /* iov[] to submit directly
                                                 * to ibt_map_mem_iov() */
#define tx_mr_type tx_plat.tx_plat_mr_type
#define tx_iov     tx_plat.tx_plat_iov
} kib_tx_plat_t;

#define KIBLND_CONN_PARAM(e)            ((e)->param.conn.private_data)
#define KIBLND_CONN_PARAM_LEN(e)        ((e)->param.conn.private_data_len)

#define IB_MTU_1024 IB_MTU_1K
#define IB_MTU_2048 IB_MTU_2K
#define IB_MTU_4096 IB_MTU_4K

/* copy/paste from linux rdma/ib_verbs.h */
static inline int ib_mtu_enum_to_int(ib_mtu_t mtu)
{
	switch (mtu) {
	case IB_MTU_256:  return  256;
	case IB_MTU_512:  return  512;
	case IB_MTU_1024: return 1024;
	case IB_MTU_2048: return 2048;
	case IB_MTU_4096: return 4096;
	default: 	  return -1;
	}
}

#define kiblnd_init_tx_frags(tx)                                           \
do {                                                                       \
        tx->tx_frags->b_shadow = (page_t **)tx->tx_pages;                  \
} while (0)

enum dma_data_direction {
	DMA_TO_DEVICE =   1,
	DMA_FROM_DEVICE = 2
};

#define kiblnd_set_sge(sge, key, addr, len)                                \
do {                                                                       \
        (sge)->ds_key = key;                                               \
        (sge)->ds_va  = addr;                                              \
        (sge)->ds_len = len;                                               \
} while (0)

#define kiblnd_set_rx_wrq(wrq, nxt, sgl, nsge, id)                         \
do {                                                                       \
        (wrq)->wr_sgl = sgl;                                               \
        (wrq)->wr_nds = nsge;                                              \
        (wrq)->wr_id  = id;                                                \
} while (0)

#define kiblnd_set_tx_wrq(wrq, nxt, sgl, nsge, id)                         \
do {                                                                       \
        kiblnd_set_rx_wrq(wrq, nxt, sgl, nsge, id);                        \
        (wrq)->wr_opcode = IBT_WRC_SEND;                                   \
        (wrq)->wr_flags  = IBT_WR_SEND_SIGNAL;                             \
} while (0)

#define kiblnd_set_rdma_wrq(wrq, nxt, sgl, nsge, id, raddr, rkey)          \
do {                                                                       \
        kiblnd_set_rx_wrq(wrq, nxt, sgl, nsge, id);                        \
        (wrq)->wr_opcode                  = IBT_WRC_RDMAW;                 \
        (wrq)->wr_flags                   = 0;                             \
        (wrq)->wr.rc.rcwr.rdma.rdma_raddr = raddr;                         \
        (wrq)->wr.rc.rcwr.rdma.rdma_rkey  = rkey;                          \
} while (0)

static inline int kiblnd_post_recv(struct ib_qp	  *qp,
                                   kib_recv_wr_t  *wrq,
                                   kib_recv_wr_t **bad_wrq,
                                   unsigned int    size)
{
        return ibt_post_recv(qp->ibt_qp, wrq, size, NULL);
}

static inline int kiblnd_post_send(struct ib_qp	  *qp,
                                   kib_send_wr_t  *wrq,
                                   kib_send_wr_t **bad_wrq,
                                   unsigned int    size)
{
        return ibt_post_send(qp->ibt_qp, wrq, size, NULL);
}
