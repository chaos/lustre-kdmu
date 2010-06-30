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
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#define DEBUG_SUBSYSTEM S_LLITE

#include <linux/module.h>
#include <linux/types.h>
#include <linux/random.h>
#include <linux/version.h>
#include <lustre_lite.h>
#include <lustre_ha.h>
#include <lustre_dlm.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <lprocfs_status.h>
#include "llite_internal.h"

static cfs_mem_cache_t *ll_inode_cachep;

static struct inode *ll_alloc_inode(struct super_block *sb)
{
        struct ll_inode_info *lli;
        ll_stats_ops_tally(ll_s2sbi(sb), LPROC_LL_ALLOC_INODE, 1);
        OBD_SLAB_ALLOC_PTR_GFP(lli, ll_inode_cachep, CFS_ALLOC_IO);
        if (lli == NULL)
                return NULL;

        inode_init_once(&lli->lli_vfs_inode);
        ll_lli_init(lli);

        return &lli->lli_vfs_inode;
}

static void ll_destroy_inode(struct inode *inode)
{
        struct ll_inode_info *ptr = ll_i2info(inode);
        OBD_SLAB_FREE_PTR(ptr, ll_inode_cachep);
}

int ll_init_inodecache(void)
{
        ll_inode_cachep = cfs_mem_cache_create("lustre_inode_cache",
                                               sizeof(struct ll_inode_info),
                                               0, CFS_SLAB_HWCACHE_ALIGN);
        if (ll_inode_cachep == NULL)
                return -ENOMEM;
        return 0;
}

void ll_destroy_inodecache(void)
{
        int rc;

        rc = cfs_mem_cache_destroy(ll_inode_cachep);
        LASSERTF(rc == 0, "ll_inode_cache: not all structures were freed\n");
}

/* exported operations */
struct super_operations lustre_super_operations =
{
        .alloc_inode   = ll_alloc_inode,
        .destroy_inode = ll_destroy_inode,
        .clear_inode   = ll_clear_inode,
        .delete_inode  = ll_delete_inode,
        .put_super     = ll_put_super,
        .statfs        = ll_statfs,
        .umount_begin  = ll_umount_begin,
        .remount_fs    = ll_remount_fs,
        .show_options  = ll_show_options,
};


void lustre_register_client_process_config(int (*cpc)(struct lustre_cfg *lcfg));

int vvp_global_init(void);
void vvp_global_fini(void);

static int __init init_lustre_lite(void)
{
        int i, rc, seed[2];
        struct timeval tv;
        lnet_process_id_t lnet_id;

        /* print an address of _any_ initialized kernel symbol from this
         * module, to allow debugging with gdb that doesn't support data
         * symbols from modules.*/
        CDEBUG(D_CONSOLE, "Lustre client module (%p).\n",
               &lustre_super_operations);

        rc = ll_init_inodecache();
        if (rc)
                return -ENOMEM;
        ll_file_data_slab = cfs_mem_cache_create("ll_file_data",
                                                 sizeof(struct ll_file_data), 0,
                                                 CFS_SLAB_HWCACHE_ALIGN);
        if (ll_file_data_slab == NULL) {
                ll_destroy_inodecache();
                return -ENOMEM;
        }

        ll_remote_perm_cachep = cfs_mem_cache_create("ll_remote_perm_cache",
                                                  sizeof(struct ll_remote_perm),
                                                      0, 0);
        if (ll_remote_perm_cachep == NULL) {
                cfs_mem_cache_destroy(ll_file_data_slab);
                ll_file_data_slab = NULL;
                ll_destroy_inodecache();
                return -ENOMEM;
        }

        ll_rmtperm_hash_cachep = cfs_mem_cache_create("ll_rmtperm_hash_cache",
                                                   REMOTE_PERM_HASHSIZE *
                                                   sizeof(cfs_list_t),
                                                   0, 0);
        if (ll_rmtperm_hash_cachep == NULL) {
                cfs_mem_cache_destroy(ll_remote_perm_cachep);
                ll_remote_perm_cachep = NULL;
                cfs_mem_cache_destroy(ll_file_data_slab);
                ll_file_data_slab = NULL;
                ll_destroy_inodecache();
                return -ENOMEM;
        }

        proc_lustre_fs_root = proc_lustre_root ?
                              lprocfs_register("llite", proc_lustre_root, NULL, NULL) : NULL;

        lustre_register_client_fill_super(ll_fill_super);
        lustre_register_kill_super_cb(ll_kill_super);

        lustre_register_client_process_config(ll_process_config);

        ll_get_random_bytes(seed, sizeof(seed));

        /* Nodes with small feet have little entropy
         * the NID for this node gives the most entropy in the low bits */
        for (i=0; ; i++) {
                if (LNetGetId(i, &lnet_id) == -ENOENT) {
                        break;
                }
                if (LNET_NETTYP(LNET_NIDNET(lnet_id.nid)) != LOLND) {
                        seed[0] ^= LNET_NIDADDR(lnet_id.nid);
                }
        }

        cfs_gettimeofday(&tv);
        ll_srand(tv.tv_sec ^ seed[0], tv.tv_usec ^ seed[1]);

        init_timer(&ll_capa_timer);
        ll_capa_timer.function = ll_capa_timer_callback;
        rc = ll_capa_thread_start();
        /*
         * XXX normal cleanup is needed here.
         */
        if (rc == 0)
                rc = vvp_global_init();

        return rc;
}

static void __exit exit_lustre_lite(void)
{
        int rc;

        vvp_global_fini();
        del_timer(&ll_capa_timer);
        ll_capa_thread_stop();
        LASSERTF(capa_count[CAPA_SITE_CLIENT] == 0,
                 "client remaining capa count %d\n",
                 capa_count[CAPA_SITE_CLIENT]);

        lustre_register_client_fill_super(NULL);
        lustre_register_kill_super_cb(NULL);

        lustre_register_client_process_config(NULL);

        ll_destroy_inodecache();

        rc = cfs_mem_cache_destroy(ll_rmtperm_hash_cachep);
        LASSERTF(rc == 0, "couldn't destroy ll_rmtperm_hash_cachep\n");
        ll_rmtperm_hash_cachep = NULL;

        rc = cfs_mem_cache_destroy(ll_remote_perm_cachep);
        LASSERTF(rc == 0, "couldn't destroy ll_remote_perm_cachep\n");
        ll_remote_perm_cachep = NULL;

        rc = cfs_mem_cache_destroy(ll_file_data_slab);
        LASSERTF(rc == 0, "couldn't destroy ll_file_data slab\n");
        if (proc_lustre_fs_root)
                lprocfs_remove(&proc_lustre_fs_root);
}

MODULE_AUTHOR("Sun Microsystems, Inc. <http://www.lustre.org/>");
MODULE_DESCRIPTION("Lustre Lite Client File System");
MODULE_LICENSE("GPL");

module_init(init_lustre_lite);
module_exit(exit_lustre_lite);
