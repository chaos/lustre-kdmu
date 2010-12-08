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
 * lnet/klnds/o2iblnd/o2iblnd_lib-solaris.c
 *
 */

#include "o2iblnd.h"

kiblnd_native_ops_t kiblnd_native_ops;

static ddi_modhandle_t kiblnd_ibtl_h;
static ddi_modhandle_t kiblnd_sol_ofs_h;

static int kiblnd_symbols_loaded = 0;

/**
 * Return 0 if OK (i.e. syms were loaded)
 */
int
kiblnd_symbols_check(void)
{
        return !kiblnd_symbols_loaded;
}

void
kiblnd_symbols_fini(void)
{
        kiblnd_symbols_loaded = 0;

        if (kiblnd_sol_ofs_h != NULL) {
                ddi_modclose(kiblnd_sol_ofs_h);
                kiblnd_sol_ofs_h = NULL;
        }

        if (kiblnd_ibtl_h != NULL) {
                ddi_modclose(kiblnd_ibtl_h);
                kiblnd_ibtl_h = NULL;
        }
}

/**
 * Return 0 if OK (i.e. sanity test passed)
 */
static int
kiblnd_symbols_sanity(void)
{
        int n = sizeof(kiblnd_native_ops)/sizeof(kiblnd_native_ops.raw[0]);
        int i;

        for (i = 0; i < n; i++) {
                if (kiblnd_native_ops.raw[i] == NULL) {
                        CERROR("kiblnd_native_ops.raw[%d] is not set!\n", i);
                        return -1;
                }
        }

        return 0;
}

#define KIB_SYM_CWARN(reason) CWARN("o2iblnd disabled because %s\n", reason)

#define KIB_MOD_OPEN(mod)                                                  \
do {                                                                       \
        kiblnd_##mod##_h = ddi_modopen("misc/"#mod, KRTLD_MODE_FIRST, &rc);\
        if (kiblnd_##mod##_h == NULL) {                                    \
                KIB_SYM_CWARN(#mod" is not available");                    \
                goto symbols_error;                                        \
        }                                                                  \
} while (0)

#define KIB_SYM_LOAD(mod, sym)                                             \
do {                                                                       \
        kiblnd_##sym = ddi_modsym(kiblnd_##mod##_h, #sym, &rc);            \
        if (kiblnd_##sym == NULL) {                                        \
                KIB_SYM_CWARN(#sym"() is not visible");                    \
                goto symbols_error;                                        \
        }                                                                  \
} while (0)

int
kiblnd_symbols_init(void)
{
        int rc;

        KIB_MOD_OPEN(ibtl);
        KIB_MOD_OPEN(sol_ofs);

        KIB_SYM_LOAD(sol_ofs, rdma_create_id);
        KIB_SYM_LOAD(sol_ofs, rdma_destroy_id);
        KIB_SYM_LOAD(sol_ofs, rdma_create_qp);
        KIB_SYM_LOAD(sol_ofs, rdma_destroy_qp);
        KIB_SYM_LOAD(sol_ofs, rdma_resolve_addr);
        KIB_SYM_LOAD(sol_ofs, rdma_resolve_route);
        KIB_SYM_LOAD(sol_ofs, rdma_connect);
        KIB_SYM_LOAD(sol_ofs, rdma_reject);
        KIB_SYM_LOAD(sol_ofs, rdma_bind_addr);
        KIB_SYM_LOAD(sol_ofs, rdma_listen);
        KIB_SYM_LOAD(sol_ofs, rdma_accept);
        KIB_SYM_LOAD(sol_ofs, rdma_disconnect);

        KIB_SYM_LOAD(sol_ofs, ib_alloc_pd);
        KIB_SYM_LOAD(sol_ofs, ib_dealloc_pd);
        KIB_SYM_LOAD(sol_ofs, ib_create_cq);
        KIB_SYM_LOAD(sol_ofs, ib_destroy_cq);
        KIB_SYM_LOAD(sol_ofs, ib_req_notify_cq);
        KIB_SYM_LOAD(sol_ofs, ib_poll_cq);
        KIB_SYM_LOAD(sol_ofs, ib_modify_qp);

        KIB_SYM_LOAD(ibtl, ibt_post_recv);
        KIB_SYM_LOAD(ibtl, ibt_post_send);
        KIB_SYM_LOAD(ibtl, ibt_register_dma_mr);
        KIB_SYM_LOAD(ibtl, ibt_register_buf);
        KIB_SYM_LOAD(ibtl, ibt_deregister_mr);
        KIB_SYM_LOAD(ibtl, ibt_map_mem_iov);
        KIB_SYM_LOAD(ibtl, ibt_unmap_mem_iov);
        
        /* someone added new entry to kiblnd_native_ops_t but missed to
           update the code above */
        LASSERT (kiblnd_symbols_sanity() == 0);
        
        kiblnd_symbols_loaded = 1;
        return 0;

symbols_error:
        kiblnd_symbols_fini();
        return -1;
}

int
kiblnd_plat_modparams_init(void)
{
        return 0;
}

void
kiblnd_plat_modparams_fini(void)
{
}

void
kiblnd_plat_net_fini_pools(kib_net_t *net)
{
}

int
kiblnd_plat_net_init_pools(kib_net_t *net)
{
        /* only to allocate tx_pages in create_tx_pool */
        net->ibn_alloc_tx_pages = 1;

        return 0;
}

int
kiblnd_dev_get_attr(kib_dev_t *ibdev)
{
        ibdev->ibd_mr_shift = 64;

        return 0;
}

void
kiblnd_unmap_tx(lnet_ni_t *ni, kib_tx_t *tx)
{
        kib_net_t     *net = ni->ni_data;
        ibt_hca_hdl_t  hca;

        LASSERT (net != NULL);

        hca = net->ibn_dev->ibd_cmid->device->hca_hdl;

        if (tx->tx_nfrags != 0) {
                switch (tx->tx_mr_type) {
                default:
                        LBUG();

                case IBLND_MR_NO_GLOBAL:
                        kiblnd_ibt_deregister_mr(hca, tx->tx_mr_u.mr_hdl);
                        tx->tx_mr_u.mr_hdl = NULL;
                        break;
                case IBLND_MR_GLOBAL:
                        kiblnd_ibt_unmap_mem_iov(hca, tx->tx_mr_u.mi_hdl);
                        tx->tx_mr_u.mi_hdl = NULL;
                        break;
                }

                tx->tx_mr_type = IBLND_MR_UNDEF;
                tx->tx_nfrags  = 0;
        }
}

int
kiblnd_no_global_mr_map_tx(lnet_ni_t *ni, kib_tx_t *tx,
              kib_rdma_desc_t *rd, int nfrags)
{
        kib_net_t          *net   = ni->ni_data;
        int                 rc;
        ibt_mr_hdl_t        mr_hdl;
        ibt_mr_desc_t       mem_desc;
        ibt_smr_attr_t      mem_bpattr;
        int                 nob = tx->tx_frags->b_bcount;

        bzero(&mem_desc, sizeof (ibt_mr_desc_t));
        bzero(&mem_bpattr, sizeof (ibt_smr_attr_t));

        mem_bpattr.mr_flags = IBT_MR_ENABLE_REMOTE_WRITE | IBT_MR_ENABLE_LOCAL_WRITE;

        rc = kiblnd_ibt_register_buf(net->ibn_dev->ibd_cmid->device->hca_hdl,
                                     net->ibn_dev->ibd_pd->ibt_pd,
                                     &mem_bpattr, tx->tx_frags, &mr_hdl,
                                     &mem_desc);

        if (rc != 0) {
                CERROR("ibt_register_buf() failed: rc = %d", rc);
                return -EIO;
        }

        tx->tx_mr_u.mr_hdl = mr_hdl;
        tx->tx_nfrags   = 1; /* flag to unmap */

        rd->rd_key = (rd != tx->tx_rd) ? mem_desc.md_rkey :
                                         mem_desc.md_lkey;
        rd->rd_frags[0].rf_addr = mem_desc.md_vaddr;
        rd->rd_frags[0].rf_nob  = nob;
        rd->rd_nfrags = 1;

        return 0;
}

int
kiblnd_global_mr_map_tx(lnet_ni_t *ni, kib_tx_t *tx,
                        kib_rdma_desc_t *rd, int nfrags)
{
        kib_net_t     *net = ni->ni_data;
        ibt_hca_hdl_t  hca = net->ibn_dev->ibd_cmid->device->hca_hdl;
        ibt_iov_attr_t iov_attr;
        ibt_mi_hdl_t   mi_hdl;
        ibt_send_wr_t  swr;
        int            ret, j;
        int            nob = tx->tx_frags->b_bcount;

	bzero(&iov_attr, sizeof (ibt_iov_attr_t));
	iov_attr.iov_flags = IBT_IOV_SLEEP | IBT_IOV_ALT_LKEY;
	iov_attr.iov_lso_hdr_sz = 0;
        iov_attr.iov_alt_lkey = net->ibn_dev->ibd_mrs[0]->lkey;

        if (tx->tx_iov == NULL) {
                iov_attr.iov_flags |= IBT_IOV_BUF;
                iov_attr.iov_buf = tx->tx_frags;
        } else {
                iov_attr.iov = (ibt_iov_t *)tx->tx_iov;
                iov_attr.iov_list_len = nfrags;
        }

        iov_attr.iov_wr_nds = LNET_MAX_IOV;

        swr.wr_sgl = tx->tx_sge;

        ret = kiblnd_ibt_map_mem_iov(hca, &iov_attr, (ibt_all_wr_t *)&swr,
                                     &mi_hdl);

        if (ret != 0) {
                CERROR("ibt_map_mem_iov() failed: rc = %d", ret);
                return -EIO;
        }

        LASSERT (swr.wr_nds <= LNET_MAX_IOV);

        tx->tx_mr_u.mi_hdl = mi_hdl;
        tx->tx_nfrags   = swr.wr_nds; /* flag to unmap */

        rd->rd_key = (rd != tx->tx_rd) ? net->ibn_dev->ibd_mrs[0]->rkey :
                                         net->ibn_dev->ibd_mrs[0]->lkey;
        for (j = 0; j < swr.wr_nds; j++) {
                rd->rd_frags[j].rf_addr = swr.wr_sgl[j].ds_va;
                rd->rd_frags[j].rf_nob  = swr.wr_sgl[j].ds_len;
        }
        rd->rd_nfrags = swr.wr_nds;

        return 0;
}

int
kiblnd_map_tx(lnet_ni_t *ni, kib_tx_t *tx,
              kib_rdma_desc_t *rd, int nfrags)
{
        switch (tx->tx_mr_type) {

        case IBLND_MR_GLOBAL:
                return kiblnd_global_mr_map_tx(ni, tx, rd, nfrags);

        case IBLND_MR_NO_GLOBAL:
                return kiblnd_no_global_mr_map_tx(ni, tx, rd, nfrags);
        default:
                LBUG();
        }
}

int
kiblnd_kiov2frags(lnet_kiov_t *kiov, int nkiov,
                  int offset, int nob, kib_tx_t *tx)
{
        page_t **plist = (page_t **)tx->tx_pages;
        int      i;

        tx->tx_frags->b_flags = B_BUSY | B_PHYS | B_WRITE | B_SHADOW;
        tx->tx_frags->b_un.b_addr = (caddr_t)(uintptr_t)(kiov->kiov_offset
                                                        + offset);
        LASSERT ((uintptr_t)tx->tx_frags->b_un.b_addr < CFS_PAGE_SIZE);
        tx->tx_frags->b_bcount = nob;

        for (i=0; i < nkiov; i++) {
                if ((i != 0 &&
                     kiov[i].kiov_offset != 0) ||
                    (i != nkiov - 1 &&
                     kiov[i].kiov_offset + kiov[i].kiov_len != CFS_PAGE_SIZE)) {
                        CERROR("gap detected in %d slot of kiov (%p/%d/%d)\n",
                               i, kiov[i].kiov_page,
                               kiov[i].kiov_offset, kiov[i].kiov_len);
                        return -EINVAL;
                }

                plist[i] = cfs_page_2_ospage(kiov[i].kiov_page);
                nob     -= kiov[i].kiov_len;

                if (nob <= 0) {
                        nkiov = i + 1;
                        break;
                }
        }

        LASSERT (nob <= 0); /* nob must be exhausted by the loop above! */

        tx->tx_iov = NULL;

        /* it's not strictly correct to make decision based on
         * 'nkiov', but calling ibt_map_mem_iov() every time is
         * too expensive */
        if (*kiblnd_tunables.kib_map_on_demand > 0 &&
            *kiblnd_tunables.kib_map_on_demand <= nkiov)
                tx->tx_mr_type = IBLND_MR_NO_GLOBAL;
        else
                tx->tx_mr_type = IBLND_MR_GLOBAL;

        return nkiov;
}

page_t *
kiblnd_kvaddr_to_page (unsigned long vaddr)
{
        pfn_t pfn = hat_getpfnum(kas.a_hat, (caddr_t)vaddr);
        page_t *p = page_numtopp_nolock(pfn);

        return p;
}

#define KIB_PAGE_OFF(addr) ((int)((unsigned long)(addr) & (CFS_PAGE_SIZE - 1)))

static int
kiblnd_iov_frags_num(struct iovec *iov, int niov, int offset, int nob)
{
        int i = 0;

        do {
                int           fragnob;
                int           page_offset;
                unsigned long vaddr;

                LASSERT (niov > 0);

                vaddr = ((unsigned long)iov->iov_base) + offset;
                page_offset = KIB_PAGE_OFF(vaddr);
                
                fragnob = min((int)(iov->iov_len - offset), nob);
                fragnob = min(fragnob, (int)CFS_PAGE_SIZE - page_offset);

                if (fragnob == nob)
                        break;

                /* Assuming nob > fragnob and KIB_PAGE_OFF(vaddr + fragnob)
                   is not zero, we still need next frag if current frag
                   spans up to the end of current iov, but iov and iov+1
                   are not adjacent. */
                if (KIB_PAGE_OFF(vaddr + fragnob) == 0 ||
                    (niov > 1 &&
                     iov->iov_base + iov->iov_len != (iov+1)->iov_base))
                        i++; /* next frag needed */

                if (offset + fragnob < iov->iov_len) {
                        offset += fragnob;
                } else {
                        offset = 0;
                        iov++;
                        niov--;
                }
                nob -= fragnob;
        } while (nob > 0);

        return i + 1;
}

int
kiblnd_iov2frags(struct iovec *iov, int niov,
                  int offset, int nob, kib_tx_t *tx)
{
        page_t            **plist    = (page_t **)tx->tx_pages;
        int                 i        = 0;
        page_t             *page;
        int                 fragnob;
        unsigned long       vaddr;
        int                 page_offset;

        /* check fast path first */
        if (*kiblnd_tunables.kib_map_on_demand == 0 ||
            *kiblnd_tunables.kib_map_on_demand >
            kiblnd_iov_frags_num(iov, niov, offset, nob)) {
                tx->tx_iov = iov;
                tx->tx_mr_type = IBLND_MR_GLOBAL;
                return niov;
        }
        tx->tx_mr_type = IBLND_MR_NO_GLOBAL;
        
        vaddr = ((unsigned long)iov->iov_base) + offset;

        tx->tx_frags->b_un.b_addr = (caddr_t)(uintptr_t)KIB_PAGE_OFF(vaddr);
        tx->tx_frags->b_flags = B_BUSY | B_PHYS | B_WRITE | B_SHADOW;
        tx->tx_frags->b_bcount = nob;

        do {
                LASSERT (niov > 0);

                vaddr = ((unsigned long)iov->iov_base) + offset;
                page_offset = KIB_PAGE_OFF(vaddr);
                page = kiblnd_kvaddr_to_page(vaddr);
                if (page == NULL) {
                        CERROR("Can't find page\n");
                        return -EFAULT;
                }

                
                fragnob = min((int)(iov->iov_len - offset), nob);
                fragnob = min(fragnob, (int)CFS_PAGE_SIZE - page_offset);

                plist[i] = page;

                if (KIB_PAGE_OFF(vaddr + fragnob) == 0 && nob > fragnob) {
                        i++; /* next page needed */

                        if (i == LNET_MAX_IOV) {
                                CERROR("iov spans too many pages (%d/%d/%d)\n",
                                       niov, iov->iov_len, nob);
                                return -EINVAL;
                        }
                }

                if (offset + fragnob < iov->iov_len) {
                        offset += fragnob;
                } else {
                        offset = 0;
                        iov++;
                        niov--;

                        if (nob > fragnob && niov > 0 &&
                            vaddr + fragnob != (unsigned long)iov->iov_base &&
                            !(KIB_PAGE_OFF(vaddr + fragnob) == 0 &&
                              KIB_PAGE_OFF(iov->iov_base) == 0)) {
                                CERROR("gap detected in iov (%p/%d/%d)\n",
                                       iov->iov_base, iov->iov_len, nob);
                                return -EINVAL;
                        }
                }
                nob -= fragnob;
        } while (nob > 0);

        return i + 1;
}

struct ib_mr *
kiblnd_get_dma_mr(kib_dev_t *ibdev, int mr_access_flags)
{
        struct ib_mr   *mr;
        ibt_dmr_attr_t  mr_attr;
        ibt_mr_desc_t   mr_desc;
        ibt_mr_hdl_t    mr_hdl;
        int             rc;
        int             access_flags = 0;

        LIBCFS_ALLOC(mr, sizeof(*mr));
        if (mr == NULL) {
                CERROR("Failed to allocate mr\n");
                return ERR_PTR(-ENOMEM);
        }

	if ((mr_access_flags & IB_ACCESS_LOCAL_WRITE) ==
	    IB_ACCESS_LOCAL_WRITE)
		access_flags |= IBT_MR_ENABLE_LOCAL_WRITE;

	if ((mr_access_flags & IB_ACCESS_REMOTE_WRITE) ==
	    IB_ACCESS_REMOTE_WRITE)
		access_flags |= IBT_MR_ENABLE_REMOTE_WRITE;

	if ((mr_access_flags & IB_ACCESS_REMOTE_READ) ==
	    IB_ACCESS_REMOTE_READ)
		access_flags |= IBT_MR_ENABLE_REMOTE_READ;

	if ((mr_access_flags & IB_ACCESS_REMOTE_ATOMIC) ==
	    IB_ACCESS_REMOTE_ATOMIC)
		access_flags |= IBT_MR_ENABLE_REMOTE_ATOMIC;

	if ((mr_access_flags & IB_ACCESS_MW_BIND) ==
            IB_ACCESS_MW_BIND)
		access_flags |= IBT_MR_ENABLE_WINDOW_BIND;

        mr_attr.dmr_paddr = 0;
        mr_attr.dmr_len   = -1ULL;
        mr_attr.dmr_flags = access_flags;

        memset(&mr_desc, 0, sizeof(mr_desc)); /* for debug only */
        rc = kiblnd_ibt_register_dma_mr(ibdev->ibd_cmid->device->hca_hdl,
                                        ibdev->ibd_pd->ibt_pd,
                                        &mr_attr, &mr_hdl, &mr_desc);

        if (rc != IBT_SUCCESS) {
                LIBCFS_FREE(mr, sizeof(*mr));
                return ERR_PTR(-rc);
        }

        mr->hdl  = mr_hdl;
        mr->lkey = mr_desc.md_lkey;
        mr->rkey = mr_desc.md_rkey;

        return mr;
}

void
kiblnd_dereg_mr(kib_dev_t *ibdev, struct ib_mr *mr)
{
        LASSERT (mr->hdl != NULL);

        CDEBUG(D_NET, "Deregestring global dma MR (hdl=%p) ...", mr->hdl);

        kiblnd_ibt_deregister_mr(ibdev->ibd_cmid->device->hca_hdl,
                                 mr->hdl);
        LIBCFS_FREE(mr, sizeof(*mr));
}

int
kiblnd_plat_dev_setup(kib_dev_t *ibdev, int acflags)
{
        return 0;
}

void
kiblnd_unmap_pages(kib_pages_t *p)
{
        if (p->ibp_map->pm_mi_hdl == NULL)
                return;

        kiblnd_ibt_unmap_mem_iov(p->ibp_device->hca_hdl,
                                 p->ibp_map->pm_mi_hdl);

        p->ibp_map->pm_mi_hdl = NULL;
}

int
kiblnd_map_pages(kib_net_t *net, kib_pages_t *p)
{
        int              npages = p->ibp_npages;
        struct buf       a_buf;
        page_t         **plist;
        ibt_iov_attr_t   iov_attr;
        ibt_mi_hdl_t     mi_hdl;
        ibt_send_wr_t    swr;
        int              i;
        int              rc;

        LIBCFS_ALLOC(plist, sizeof(*plist) * npages);
        if (plist == NULL) {
                CERROR("Failed to allocate plist[%d]\n", npages);
                return -ENOMEM;
        }        

        for (i=0; i < npages; i++)
                plist[i] = cfs_page_2_ospage(p->ibp_pages[i]);

        bzero(&a_buf, sizeof(a_buf));
        a_buf.b_shadow    = plist;
        a_buf.b_flags     = B_BUSY | B_PHYS | B_WRITE | B_SHADOW;
        a_buf.b_un.b_addr = 0;
        a_buf.b_bcount    = CFS_PAGE_SIZE * npages;

	bzero(&iov_attr, sizeof(iov_attr));
	iov_attr.iov_flags = IBT_IOV_SLEEP | IBT_IOV_BUF | IBT_IOV_ALT_LKEY;
	iov_attr.iov_lso_hdr_sz = 0;
        iov_attr.iov_alt_lkey   = net->ibn_dev->ibd_mrs[0]->lkey;
        iov_attr.iov_buf        = &a_buf;
        iov_attr.iov_wr_nds     = npages;

        swr.wr_sgl = p->ibp_map->pm_sgl;

        rc = kiblnd_ibt_map_mem_iov(p->ibp_device->hca_hdl,
                                    &iov_attr, (ibt_all_wr_t *)&swr, &mi_hdl);

        LIBCFS_FREE(plist, sizeof(*plist) * npages);

        if (rc != 0) {
                CERROR("ibt_map_mem_iov() failed: rc = %d", rc);
                return -EIO;
        }

        p->ibp_nsgl = swr.wr_nds;
        p->ibp_map->pm_mi_hdl = mi_hdl;

        return 0;
}
