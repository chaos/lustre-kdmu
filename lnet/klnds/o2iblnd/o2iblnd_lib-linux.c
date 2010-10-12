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
 * lnet/klnds/o2iblnd/o2iblnd_lib-linux.c
 *
 */

#include "o2iblnd.h"
#include "o2iblnd_pools-linux.h"

static int fmr_pool_size = 512;
CFS_MODULE_PARM(fmr_pool_size, "i", int, 0444,
                "size of the fmr pool (>= ntx / 4)");

static int fmr_flush_trigger = 384;
CFS_MODULE_PARM(fmr_flush_trigger, "i", int, 0444,
                "# dirty FMRs that triggers pool flush");

static int fmr_cache = 1;
CFS_MODULE_PARM(fmr_cache, "i", int, 0444,
                "non-zero to enable FMR caching");

static int pmr_pool_size = 512;
CFS_MODULE_PARM(pmr_pool_size, "i", int, 0444,
                "size of the MR cache pmr pool");

#if defined(CONFIG_SYSCTL) && !CFS_SYSFS_MODULE_PARM

static cfs_sysctl_table_t kiblnd_plat_ctl_table[] = {
        {
                .ctl_name = O2IBLND_FMR_POOL_SIZE,
                .procname = "fmr_pool_size",
                .data     = &fmr_pool_size,
                .maxlen   = sizeof(int),
                .mode     = 0444,
                .proc_handler = &proc_dointvec
        },
        {
                .ctl_name = O2IBLND_FMR_FLUSH_TRIGGER,
                .procname = "fmr_flush_trigger",
                .data     = &fmr_flush_trigger,
                .maxlen   = sizeof(int),
                .mode     = 0444,
                .proc_handler = &proc_dointvec
        },
        {
                .ctl_name = O2IBLND_FMR_CACHE,
                .procname = "fmr_cache",
                .data     = &fmr_cache,
                .maxlen   = sizeof(int),
                .mode     = 0444,
                .proc_handler = &proc_dointvec
        },
        {
                .ctl_name = O2IBLND_PMR_POOL_SIZE,
                .procname = "pmr_pool_size",
                .data     = &pmr_pool_size,
                .maxlen   = sizeof(int),
                .mode     = 0444,
                .proc_handler = &proc_dointvec
        },
        {0}
};

static cfs_sysctl_table_t kiblnd_plat_top_ctl_table[] = {
        {
                .ctl_name = CTL_O2IBLND,
                .procname = "o2iblnd",
                .data     = NULL,
                .maxlen   = 0,
                .mode     = 0555,
                .child    = kiblnd_plat_ctl_table
        },
        {0}
};

#endif /* defined(CONFIG_SYSCTL) && !CFS_SYSFS_MODULE_PARM */

static struct libcfs_param_ctl_table libcfs_plat_param_kiblnd_ctl_table[] = {
        {
                .name     = "fmr_pool_size",
                .data     = &fmr_pool_size,
                .mode     = 0444,
                .read     = libcfs_param_intvec_read
        },
        {
                .name     = "fmr_flush_trigger",
                .data     = &fmr_flush_trigger,
                .mode     = 0444,
                .read     = libcfs_param_intvec_read
        },
        {
                .name     = "fmr_cache",
                .data     = &fmr_cache,
                .mode     = 0444,
                .read     = libcfs_param_intvec_read
        },
        {
                .name     = "pmr_pool_size",
                .data     = &pmr_pool_size,
                .mode     = 0444,
                .read     = libcfs_param_intvec_read
        },
        {0}
};

void
kiblnd_plat_modparams_init(void)
{
        kiblnd_tunables.kib_fmr_pool_size     = &fmr_pool_size;
        kiblnd_tunables.kib_fmr_flush_trigger = &fmr_flush_trigger;
        kiblnd_tunables.kib_fmr_cache         = &fmr_cache;
        kiblnd_tunables.kib_pmr_pool_size     = &pmr_pool_size;

        libcfs_param_sysctl_init("o2iblnd", libcfs_plat_param_kiblnd_ctl_table,
                                 libcfs_param_lnet_root);
        
#if defined(CONFIG_SYSCTL) && !CFS_SYSFS_MODULE_PARM
        kiblnd_tunables.kib_plat_sysctl =
                cfs_register_sysctl_table(kiblnd_plat_top_ctl_table, 0);

        if (kiblnd_tunables.kib_plat_sysctl == NULL)
                CWARN("Can't setup plat /proc tunables\n");
#endif
}

void
kiblnd_plat_modparams_fini(void)
{
#if defined(CONFIG_SYSCTL) && !CFS_SYSFS_MODULE_PARM
        if (kiblnd_tunables.kib_plat_sysctl != NULL)
                cfs_unregister_sysctl_table(kiblnd_tunables.kib_plat_sysctl);
#endif
}

void
kiblnd_destroy_fmr_pool(kib_fmr_pool_t *pool)
{
        LASSERT (pool->fpo_map_count == 0);

        if (pool->fpo_fmr_pool != NULL)
                ib_destroy_fmr_pool(pool->fpo_fmr_pool);

        LIBCFS_FREE(pool, sizeof(kib_fmr_pool_t));
}

void
kiblnd_destroy_fmr_pool_list(cfs_list_t *head)
{
        kib_fmr_pool_t *pool;

        while (!cfs_list_empty(head)) {
                pool = cfs_list_entry(head->next, kib_fmr_pool_t, fpo_list);
                cfs_list_del(&pool->fpo_list);
                kiblnd_destroy_fmr_pool(pool);
        }
}

int
kiblnd_create_fmr_pool(kib_fmr_poolset_t *fps, kib_fmr_pool_t **pp_fpo)
{
        /* FMR pool for RDMA */
        kib_fmr_pool_t          *fpo;
        struct ib_fmr_pool_param param = {
                .max_pages_per_fmr = LNET_MAX_PAYLOAD/PAGE_SIZE,
                .page_shift        = PAGE_SHIFT,
                .access            = (IB_ACCESS_LOCAL_WRITE |
                                      IB_ACCESS_REMOTE_WRITE),
                .pool_size         = *kiblnd_tunables.kib_fmr_pool_size,
                .dirty_watermark   = *kiblnd_tunables.kib_fmr_flush_trigger,
                .flush_function    = NULL,
                .flush_arg         = NULL,
                .cache             = !!*kiblnd_tunables.kib_fmr_cache};
        int rc;

        LASSERT (fps->fps_net->ibn_dev != NULL &&
                 fps->fps_net->ibn_dev->ibd_pd != NULL);

        LIBCFS_ALLOC(fpo, sizeof(kib_fmr_pool_t));
        if (fpo == NULL)
                return -ENOMEM;

        memset(fpo, 0, sizeof(kib_fmr_pool_t));
        fpo->fpo_fmr_pool = ib_create_fmr_pool(fps->fps_net->ibn_dev->ibd_pd, &param);
        if (IS_ERR(fpo->fpo_fmr_pool)) {
                CERROR("Failed to create FMR pool: %ld\n",
                       PTR_ERR(fpo->fpo_fmr_pool));
                rc = PTR_ERR(fpo->fpo_fmr_pool);
                LIBCFS_FREE(fpo, sizeof(kib_fmr_pool_t));
                return rc;
        }

        fpo->fpo_deadline = cfs_time_shift(IBLND_POOL_DEADLINE);
        fpo->fpo_owner    = fps;
        *pp_fpo = fpo;

        return 0;
}

static void
kiblnd_fini_fmr_pool_set(kib_fmr_poolset_t *fps)
{
        kiblnd_destroy_fmr_pool_list(&fps->fps_pool_list);
}

static int
kiblnd_init_fmr_pool_set(kib_fmr_poolset_t *fps, kib_net_t *net)
{
        kib_fmr_pool_t *fpo;
        int             rc;

        memset(fps, 0, sizeof(kib_fmr_poolset_t));

        fps->fps_net = net;
        cfs_spin_lock_init(&fps->fps_lock);
        CFS_INIT_LIST_HEAD(&fps->fps_pool_list);
        rc = kiblnd_create_fmr_pool(fps, &fpo);
        if (rc == 0)
                cfs_list_add_tail(&fpo->fpo_list, &fps->fps_pool_list);

        return rc;
}

void
kiblnd_fmr_pool_unmap(kib_fmr_t *fmr, int status)
{
        CFS_LIST_HEAD     (zombies);
        kib_fmr_pool_t    *fpo = fmr->fmr_pool;
        kib_fmr_poolset_t *fps = fpo->fpo_owner;
        kib_fmr_pool_t    *tmp;
        int                rc;

        rc = ib_fmr_pool_unmap(fmr->fmr_pfmr);
        LASSERT (rc == 0);

        if (status != 0) {
                rc = ib_flush_fmr_pool(fpo->fpo_fmr_pool);
                LASSERT (rc == 0);
        }

        fmr->fmr_pool = NULL;
        fmr->fmr_pfmr = NULL;

        cfs_spin_lock(&fps->fps_lock);
        fpo->fpo_map_count --;  /* decref the pool */

        cfs_list_for_each_entry_safe(fpo, tmp, &fps->fps_pool_list, fpo_list) {
                /* the first pool is persistent */
                if (fps->fps_pool_list.next == &fpo->fpo_list)
                        continue;

                if (fpo->fpo_map_count == 0 &&  /* no more reference */
                    cfs_time_aftereq(cfs_time_current(), fpo->fpo_deadline)) {
                        cfs_list_move(&fpo->fpo_list, &zombies);
                        fps->fps_version ++;
                }
        }
        cfs_spin_unlock(&fps->fps_lock);

        if (!cfs_list_empty(&zombies))
                kiblnd_destroy_fmr_pool_list(&zombies);
}

int
kiblnd_fmr_pool_map(kib_fmr_poolset_t *fps, __u64 *pages, int npages,
                    __u64 iov, kib_fmr_t *fmr)
{
        struct ib_pool_fmr *pfmr;
        kib_fmr_pool_t     *fpo;
        __u64               version;
        int                 rc;

        LASSERT (fps->fps_net->ibn_alloc_tx_pages);
 again:
        cfs_spin_lock(&fps->fps_lock);
        version = fps->fps_version;
        cfs_list_for_each_entry(fpo, &fps->fps_pool_list, fpo_list) {
                fpo->fpo_deadline = cfs_time_shift(IBLND_POOL_DEADLINE);
                fpo->fpo_map_count ++;
                cfs_spin_unlock(&fps->fps_lock);

                pfmr = ib_fmr_pool_map_phys(fpo->fpo_fmr_pool,
                                            pages, npages, iov);
                if (likely(!IS_ERR(pfmr))) {
                        fmr->fmr_pool = fpo;
                        fmr->fmr_pfmr = pfmr;
                        return 0;
                }

                cfs_spin_lock(&fps->fps_lock);
                fpo->fpo_map_count --;
                if (PTR_ERR(pfmr) != -EAGAIN) {
                        cfs_spin_unlock(&fps->fps_lock);
                        return PTR_ERR(pfmr);
                }

                /* EAGAIN and ... */
                if (version != fps->fps_version) {
                        cfs_spin_unlock(&fps->fps_lock);
                        goto again;
                }
        }

        if (fps->fps_increasing) {
                cfs_spin_unlock(&fps->fps_lock);
                CDEBUG(D_NET, "Another thread is allocating new "
                              "FMR pool, waiting for her to complete\n");
                cfs_schedule();
                goto again;

        }

        if (cfs_time_before(cfs_time_current(), fps->fps_next_retry)) {
                /* someone failed recently */
                cfs_spin_unlock(&fps->fps_lock);
                return -EAGAIN;
        }

        fps->fps_increasing = 1;
        cfs_spin_unlock(&fps->fps_lock);

        CDEBUG(D_NET, "Allocate new FMR pool\n");
        rc = kiblnd_create_fmr_pool(fps, &fpo);
        cfs_spin_lock(&fps->fps_lock);
        fps->fps_increasing = 0;
        if (rc == 0) {
                fps->fps_version ++;
                cfs_list_add_tail(&fpo->fpo_list, &fps->fps_pool_list);
        } else {
                fps->fps_next_retry = cfs_time_shift(10);
        }
        cfs_spin_unlock(&fps->fps_lock);

        goto again;
}

void
kiblnd_pmr_pool_unmap(kib_phys_mr_t *pmr)
{
        kib_pmr_pool_t      *ppo = pmr->pmr_pool;
        struct ib_mr        *mr  = pmr->pmr_mr;

        pmr->pmr_mr = NULL;
        kiblnd_pool_free_node(&ppo->ppo_pool, &pmr->pmr_list);
        if (mr != NULL)
                ib_dereg_mr(mr);
}

int
kiblnd_pmr_pool_map(kib_pmr_poolset_t *pps, kib_rdma_desc_t *rd,
                    __u64 *iova, kib_phys_mr_t **pp_pmr)
{
        kib_phys_mr_t *pmr;
        cfs_list_t    *node;
        int            rc;
        int            i;

        node = kiblnd_pool_alloc_node(&pps->pps_poolset);
        if (node == NULL) {
                CERROR("Failed to allocate PMR descriptor\n");
                return -ENOMEM;
        }

        pmr = container_of(node, kib_phys_mr_t, pmr_list);
        for (i = 0; i < rd->rd_nfrags; i ++) {
                pmr->pmr_ipb[i].addr = rd->rd_frags[i].rf_addr;
                pmr->pmr_ipb[i].size = rd->rd_frags[i].rf_nob;
        }

        pmr->pmr_mr = ib_reg_phys_mr(pps->pps_poolset.ps_net->ibn_dev->ibd_pd,
                                     pmr->pmr_ipb, rd->rd_nfrags,
                                     IB_ACCESS_LOCAL_WRITE |
                                     IB_ACCESS_REMOTE_WRITE,
                                     iova);
        if (!IS_ERR(pmr->pmr_mr)) {
                pmr->pmr_iova = *iova;
                *pp_pmr = pmr;
                return 0;
        }

        rc = PTR_ERR(pmr->pmr_mr);
        CERROR("Failed ib_reg_phys_mr: %d\n", rc);

        pmr->pmr_mr = NULL;
        kiblnd_pool_free_node(&pmr->pmr_pool->ppo_pool, node);

        return rc;
}

static void
kiblnd_destroy_pmr_pool(kib_pool_t *pool)
{
        kib_pmr_pool_t *ppo = container_of(pool, kib_pmr_pool_t, ppo_pool);
        kib_phys_mr_t  *pmr;

        LASSERT (pool->po_allocated == 0);

        while (!cfs_list_empty(&pool->po_free_list)) {
                pmr = cfs_list_entry(pool->po_free_list.next,
                                     kib_phys_mr_t, pmr_list);

                LASSERT (pmr->pmr_mr == NULL);
                cfs_list_del(&pmr->pmr_list);

                if (pmr->pmr_ipb != NULL) {
                        LIBCFS_FREE(pmr->pmr_ipb,
                                    IBLND_MAX_RDMA_FRAGS *
                                    sizeof(struct ib_phys_buf));
                }

                LIBCFS_FREE(pmr, sizeof(kib_phys_mr_t));
        }

        kiblnd_fini_pool(pool);
        LIBCFS_FREE(ppo, sizeof(kib_pmr_pool_t));
}

static int
kiblnd_create_pmr_pool(kib_poolset_t *ps, int size, kib_pool_t **pp_po)
{
        kib_pmr_pool_t      *ppo;
        kib_pool_t          *pool;
        kib_phys_mr_t       *pmr;
        int                  i;

        LIBCFS_ALLOC(ppo, sizeof(kib_pmr_pool_t));
        if (ppo == NULL) {
                CERROR("Failed to allocate PMR pool\n");
                return -ENOMEM;
        }

        pool = &ppo->ppo_pool;
        kiblnd_init_pool(ps, pool, size);

        for (i = 0; i < size; i++) {
                LIBCFS_ALLOC(pmr, sizeof(kib_phys_mr_t));
                if (pmr == NULL)
                        break;

                memset(pmr, 0, sizeof(kib_phys_mr_t));
                pmr->pmr_pool = ppo;
                LIBCFS_ALLOC(pmr->pmr_ipb,
                             IBLND_MAX_RDMA_FRAGS *
                             sizeof(struct ib_phys_buf));
                if (pmr->pmr_ipb == NULL)
                        break;

                cfs_list_add(&pmr->pmr_list, &pool->po_free_list);
        }

        if (i < size) {
                ps->ps_pool_destroy(pool);
                return -ENOMEM;
        }

        *pp_po = pool;
        return 0;
}

static inline void
kiblnd_plat_net_free_pools(kib_net_t *net)
{
        if (net->ibn_fmr_ps != NULL) {
                LIBCFS_FREE(net->ibn_fmr_ps, sizeof(*net->ibn_fmr_ps));
                net->ibn_fmr_ps = NULL;
        }

        if (net->ibn_pmr_ps != NULL) {
                LIBCFS_FREE(net->ibn_pmr_ps, sizeof(*net->ibn_pmr_ps));
                net->ibn_pmr_ps = NULL;
        }
}

void
kiblnd_plat_net_fini_pools(kib_net_t *net)
{
        if (net->ibn_fmr_ps != NULL)
                kiblnd_fini_fmr_pool_set(net->ibn_fmr_ps);
        else if (net->ibn_pmr_ps != NULL)
                kiblnd_fini_pool_set(&net->ibn_pmr_ps->pps_poolset);

        kiblnd_plat_net_free_pools(net);
}

int
kiblnd_plat_net_init_pools(kib_net_t *net)
{
        kib_fmr_poolset_t *fps;
        kib_pmr_poolset_t *pps;
        int                rc  = 0;

        if (*kiblnd_tunables.kib_fmr_pool_size <
            *kiblnd_tunables.kib_ntx / 4) {
                CERROR("Can't set fmr pool size (%d) < ntx / 4(%d)\n",
                       *kiblnd_tunables.kib_fmr_pool_size,
                       *kiblnd_tunables.kib_ntx / 4);
                return -EINVAL;
        }

        if (*kiblnd_tunables.kib_pmr_pool_size <
            *kiblnd_tunables.kib_ntx / 4) {
                CERROR("Can't set pmr pool size (%d) < ntx / 4(%d)\n",
                       *kiblnd_tunables.kib_pmr_pool_size,
                       *kiblnd_tunables.kib_ntx / 4);
                return -EINVAL;
        }

        if (*kiblnd_tunables.kib_map_on_demand > 0 ||
            net->ibn_dev->ibd_nmrs > 1) { /* premapping can fail if ibd_nmr > 1,
                                           * so we always create FMR/PMR pool and
                                           * map-on-demand if premapping failed */

                LIBCFS_ALLOC(fps, sizeof(*fps));
                if (fps == NULL) {
                        CERROR("Can't allocate memory for FMR poolset\n");
                        return -ENOMEM;
                }
                net->ibn_fmr_ps = fps;

                rc = kiblnd_init_fmr_pool_set(fps, net);
                if (rc == 0) {
                        net->ibn_alloc_tx_pages = 1;
                } else if (rc == -ENOSYS) {
                        LIBCFS_FREE(fps, sizeof(*fps));
                        net->ibn_fmr_ps = NULL;

                        LIBCFS_ALLOC(pps, sizeof(*pps));
                        if (pps == NULL) {
                                CERROR("Can't allocate memory for PMR poolset\n");
                                LIBCFS_FREE(fps, sizeof(*fps));
                                return -ENOMEM;
                        }        
                        net->ibn_pmr_ps = pps;

                        rc = kiblnd_init_pool_set(&pps->pps_poolset, net, "PMR",
                                                  *kiblnd_tunables.kib_pmr_pool_size,
                                                  kiblnd_create_pmr_pool,
                                                  kiblnd_destroy_pmr_pool,
                                                  NULL, NULL);
                }
        }

        if (rc != 0)
                kiblnd_plat_net_free_pools(net);

        return rc;
}

int
kiblnd_dev_get_attr(kib_dev_t *ibdev)
{
        struct ib_device_attr *attr;
        int                    rc;

        /* It's safe to assume a HCA can handle a page size
         * matching that of the native system */
        ibdev->ibd_page_shift = PAGE_SHIFT;
        ibdev->ibd_page_size  = 1 << PAGE_SHIFT;
        ibdev->ibd_page_mask  = ~((__u64)ibdev->ibd_page_size - 1);

        LIBCFS_ALLOC(attr, sizeof(*attr));
        if (attr == NULL) {
                CERROR("Out of memory\n");
                return -ENOMEM;
        }

        rc = ib_query_device(ibdev->ibd_cmid->device, attr);
        if (rc == 0)
                ibdev->ibd_mr_size = attr->max_mr_size;

        LIBCFS_FREE(attr, sizeof(*attr));

        if (rc != 0) {
                CERROR("Failed to query IB device: %d\n", rc);
                return rc;
        }

#ifdef HAVE_OFED_TRANSPORT_IWARP
        /* XXX We can't trust this value returned by Chelsio driver, it's wrong
         * and we have reported the bug, remove these in the future when Chelsio
         * bug got fixed. */
        if (rdma_node_get_transport(ibdev->ibd_cmid->device->node_type) ==
            RDMA_TRANSPORT_IWARP)
                ibdev->ibd_mr_size = (1ULL << 32) - 1;
#endif

        if (ibdev->ibd_mr_size == ~0ULL) {
                ibdev->ibd_mr_shift = 64;
                return 0;
        }

        for (ibdev->ibd_mr_shift = 0;
             ibdev->ibd_mr_shift < 64; ibdev->ibd_mr_shift ++) {
                if (ibdev->ibd_mr_size == (1ULL << ibdev->ibd_mr_shift) ||
                    ibdev->ibd_mr_size == (1ULL << ibdev->ibd_mr_shift) - 1)
                        return 0;
        }

        CERROR("Invalid mr size: "LPX64"\n", ibdev->ibd_mr_size);
        return -EINVAL;
}

struct page *
kiblnd_kvaddr_to_page (unsigned long vaddr)
{
        struct page *page;

        if (vaddr >= VMALLOC_START &&
            vaddr < VMALLOC_END) {
                page = vmalloc_to_page ((void *)vaddr);
                LASSERT (page != NULL);
                return page;
        }
#ifdef CONFIG_HIGHMEM
        if (vaddr >= PKMAP_BASE &&
            vaddr < (PKMAP_BASE + LAST_PKMAP * PAGE_SIZE)) {
                /* No highmem pages only used for bulk (kiov) I/O */
                CERROR("find page for address in highmem\n");
                LBUG();
        }
#endif
        page = virt_to_page (vaddr);
        LASSERT (page != NULL);
        return page;
}

static int
kiblnd_fmr_map_tx(kib_net_t *net, kib_tx_t *tx, kib_rdma_desc_t *rd, int nob)
{
        kib_dev_t          *ibdev  = net->ibn_dev;
        __u64              *pages  = tx->tx_pages;
        int                 npages;
        int                 size;
        int                 rc;
        int                 i;

        for (i = 0, npages = 0; i < rd->rd_nfrags; i++) {
                for (size = 0; size <  rd->rd_frags[i].rf_nob;
                               size += ibdev->ibd_page_size) {
                        pages[npages ++] = (rd->rd_frags[i].rf_addr &
                                            ibdev->ibd_page_mask) + size;
                }
        }

        rc = kiblnd_fmr_pool_map(net->ibn_fmr_ps, pages, npages, 0,
                                 &tx->tx_mr_u.fmr);
        if (rc != 0) {
                CERROR ("Can't map %d pages: %d\n", npages, rc);
                return rc;
        }

        /* If rd is not tx_rd, it's going to get sent to a peer, who will need
         * the rkey */
        rd->rd_key = (rd != tx->tx_rd) ? tx->tx_mr_u.fmr.fmr_pfmr->fmr->rkey :
                                         tx->tx_mr_u.fmr.fmr_pfmr->fmr->lkey;
        rd->rd_frags[0].rf_addr &= ~ibdev->ibd_page_mask;
        rd->rd_frags[0].rf_nob   = nob;
        rd->rd_nfrags = 1;

        return 0;
}

static int
kiblnd_pmr_map_tx(kib_net_t *net, kib_tx_t *tx, kib_rdma_desc_t *rd, int nob)
{
        __u64   iova;
        int     rc;

        iova = rd->rd_frags[0].rf_addr & ~net->ibn_dev->ibd_page_mask;

        rc = kiblnd_pmr_pool_map(net->ibn_pmr_ps, rd, &iova, &tx->tx_mr_u.pmr);
        if (rc != 0) {
                CERROR("Failed to create MR by phybuf: %d\n", rc);
                return rc;
        }

        /* If rd is not tx_rd, it's going to get sent to a peer, who will need
         * the rkey */
        rd->rd_key = (rd != tx->tx_rd) ? tx->tx_mr_u.pmr->pmr_mr->rkey :
                                         tx->tx_mr_u.pmr->pmr_mr->lkey;
        rd->rd_nfrags = 1;
        rd->rd_frags[0].rf_addr = iova;
        rd->rd_frags[0].rf_nob  = nob;

        return 0;
}

void
kiblnd_unmap_tx(lnet_ni_t *ni, kib_tx_t *tx)
{
        kib_net_t  *net = ni->ni_data;

        LASSERT (net != NULL);

        if (net->ibn_fmr_ps != NULL && tx->tx_mr_u.fmr.fmr_pfmr != NULL) {
                kiblnd_fmr_pool_unmap(&tx->tx_mr_u.fmr, tx->tx_status);
                tx->tx_mr_u.fmr.fmr_pfmr = NULL;
        } else if (net->ibn_pmr_ps != NULL && tx->tx_mr_u.pmr != NULL) {
                kiblnd_pmr_pool_unmap(tx->tx_mr_u.pmr);
                tx->tx_mr_u.pmr = NULL;
        }

        if (tx->tx_nfrags != 0) {
                kiblnd_dma_unmap_sg(net->ibn_dev->ibd_cmid->device,
                                    tx->tx_frags, tx->tx_nfrags, tx->tx_dmadir);
                tx->tx_nfrags = 0;
        }
}

int
kiblnd_map_tx(lnet_ni_t *ni, kib_tx_t *tx,
              kib_rdma_desc_t *rd, int nfrags)
{
        kib_net_t          *net   = ni->ni_data;
        struct ib_mr       *mr    = NULL;
        __u32               nob;
        int                 i;

        /* If rd is not tx_rd, it's going to get sent to a peer and I'm the
         * RDMA sink */
        tx->tx_dmadir = (rd != tx->tx_rd) ? DMA_FROM_DEVICE : DMA_TO_DEVICE;
        tx->tx_nfrags = nfrags;

        rd->rd_nfrags =
                kiblnd_dma_map_sg(net->ibn_dev->ibd_cmid->device,
                                  tx->tx_frags, tx->tx_nfrags, tx->tx_dmadir);

        for (i = 0, nob = 0; i < rd->rd_nfrags; i++) {
                rd->rd_frags[i].rf_nob  = kiblnd_sg_dma_len(
                        net->ibn_dev->ibd_cmid->device, &tx->tx_frags[i]);
                rd->rd_frags[i].rf_addr = kiblnd_sg_dma_address(
                        net->ibn_dev->ibd_cmid->device, &tx->tx_frags[i]);
                nob += rd->rd_frags[i].rf_nob;
        }

        /* looking for pre-mapping MR */
        mr = kiblnd_find_rd_dma_mr(net, rd);
        if (mr != NULL) {
                /* found pre-mapping MR */
                rd->rd_key = (rd != tx->tx_rd) ? mr->rkey : mr->lkey;
                return 0;
        }

        if (net->ibn_fmr_ps != NULL)
                return kiblnd_fmr_map_tx(net, tx, rd, nob);
        else if (net->ibn_pmr_ps != NULL)
                return kiblnd_pmr_map_tx(net, tx, rd, nob);

        return -EINVAL;
}

int
kiblnd_kiov2frags(lnet_kiov_t *kiov, int nkiov,
                  int offset, int nob, kib_tx_t *tx)
{
        struct scatterlist *sg = tx->tx_frags;
        int                 fragnob;

        do {
                LASSERT (nkiov > 0);

                fragnob = min((int)(kiov->kiov_len - offset), nob);

                memset(sg, 0, sizeof(*sg));
                sg_set_page(sg, kiov->kiov_page, fragnob,
                            kiov->kiov_offset + offset);
                sg++;

                offset = 0;
                kiov++;
                nkiov--;
                nob -= fragnob;
        } while (nob > 0);

        return sg - tx->tx_frags;
}

int
kiblnd_iov2frags(struct iovec *iov, int niov,
                  int offset, int nob, kib_tx_t *tx)
{
        struct scatterlist *sg = tx->tx_frags;
        struct page        *page;
        int                 fragnob;
        unsigned long       vaddr;
        int                 page_offset;

        do {
                LASSERT (niov > 0);

                vaddr = ((unsigned long)iov->iov_base) + offset;
                page_offset = vaddr & (PAGE_SIZE - 1);
                page = kiblnd_kvaddr_to_page(vaddr);
                if (page == NULL) {
                        CERROR ("Can't find page\n");
                        return -EFAULT;
                }

                fragnob = min((int)(iov->iov_len - offset), nob);
                fragnob = min(fragnob, (int)PAGE_SIZE - page_offset);

                sg_set_page(sg, page, fragnob, page_offset);
                sg++;

                if (offset + fragnob < iov->iov_len) {
                        offset += fragnob;
                } else {
                        offset = 0;
                        iov++;
                        niov--;
                }
                nob -= fragnob;
        } while (nob > 0);

        return sg - tx->tx_frags;
}

struct ib_mr *
kiblnd_get_dma_mr(kib_dev_t *ibdev, int mr_access_flags)
{
        return ib_get_dma_mr(ibdev->ibd_pd, mr_access_flags);
}

void
kiblnd_dereg_mr(kib_dev_t *ibdev, struct ib_mr *mr)
{
        ib_dereg_mr(mr);
}

int
kiblnd_plat_dev_setup(kib_dev_t *ibdev, int acflags)
{
        __u64         mm_size;
        __u64         mr_size;
        int           i;

        mr_size = (1ULL << ibdev->ibd_mr_shift);
        mm_size = (unsigned long)high_memory - PAGE_OFFSET;

        ibdev->ibd_nmrs = (int)((mm_size + mr_size - 1) >> ibdev->ibd_mr_shift);

        if (ibdev->ibd_mr_shift < 32 || ibdev->ibd_nmrs > 1024) {
                /* it's 4T..., assume we will re-code at that time */
                CERROR("Can't support memory size: x"LPX64
                       " with MR size: x"LPX64"\n", mm_size, mr_size);
                return -EINVAL;
        }

        /* create an array of MRs to cover all memory */
        LIBCFS_ALLOC(ibdev->ibd_mrs, sizeof(*ibdev->ibd_mrs) * ibdev->ibd_nmrs);
        if (ibdev->ibd_mrs == NULL) {
                CERROR("Failed to allocate MRs' table\n");
                return -ENOMEM;
        }

        memset(ibdev->ibd_mrs, 0, sizeof(*ibdev->ibd_mrs) * ibdev->ibd_nmrs);

        for (i = 0; i < ibdev->ibd_nmrs; i++) {
                struct ib_phys_buf ipb;
                __u64              iova;
                struct ib_mr      *mr;

                ipb.size = ibdev->ibd_mr_size;
                ipb.addr = i * mr_size;
                iova     = ipb.addr;

                mr = ib_reg_phys_mr(ibdev->ibd_pd, &ipb, 1, acflags, &iova);
                if (IS_ERR(mr)) {
                        CERROR("Failed ib_reg_phys_mr addr "LPX64
                               " size "LPX64" : %ld\n",
                               ipb.addr, ipb.size, PTR_ERR(mr));
                        kiblnd_dev_cleanup(ibdev);
                        return PTR_ERR(mr);
                }

                LASSERT (iova == ipb.addr);

                ibdev->ibd_mrs[i] = mr;
        }

        return 0;
}

void
kiblnd_unmap_pages(kib_pages_t *p)
{
        kiblnd_dma_unmap_sg(p->ibp_device, p->ibp_map->pm_sgl,
                            p->ibp_npages, p->ibp_map->pm_dir);
}

int
kiblnd_map_pages(kib_net_t *net, kib_pages_t *p)
{
        int i;
        UNUSED(net);

        for (i = 0; i < p->ibp_npages; i++)
                sg_set_page(&p->ibp_map->pm_sgl[i], p->ibp_pages[i],
                            CFS_PAGE_SIZE, 0);
        
        p->ibp_nsgl = kiblnd_dma_map_sg(p->ibp_device, p->ibp_map->pm_sgl,
                                        p->ibp_npages, p->ibp_map->pm_dir);

        return 0;
}
