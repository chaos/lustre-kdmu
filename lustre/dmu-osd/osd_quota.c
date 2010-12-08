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
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/dmu-osd/osd_quota.c
 *
 * Quota code is specific to dmu
 *
 * Author: Landen Tian <landen@sun.com>
 */

#ifndef EXPORT_SYMTAB
# define EXPORT_SYMTAB
#endif
#include "udmu.h"
#include <osd_quota.h>

#include "osd_internal.h"

static struct dt_it *qs_zap_it_init(const struct lu_env *env,
                                    struct dt_object *dt,
                                    struct lustre_capa *capa)
{
        struct lu_object    *lo  = &dt->do_lu;
        const struct lu_fid *fid = lu_object_fid(lo);
        uint64_t             quota_obj = 0;
        struct osd_zap_it   *it;
        struct osd_object   *obj = osd_dt_obj(dt);
        struct osd_device   *osd = osd_obj2dev(obj);

        if (lu_fid_eq(fid, &quota_slave_uid_fid))
                quota_obj = DMU_USERUSED_OBJECT;
        if (lu_fid_eq(fid, &quota_slave_gid_fid))
                quota_obj = DMU_GROUPUSED_OBJECT;
        if (quota_obj == 0)
                return ERR_PTR(-EINVAL);

        OBD_ALLOC_PTR(it);
        if (it != NULL) {
                if (udmu_zap_cursor_init(&it->ozi_zc, &osd->od_objset,
                                         quota_obj, 0))
                        RETURN(ERR_PTR(-ENOMEM));

                it->ozi_obj   = obj;
                it->ozi_capa  = capa;
                it->ozi_reset = 0;
                lu_object_get(lo);
                RETURN((struct dt_it *)it);
        }
        RETURN(ERR_PTR(-ENOMEM));
}

static int qs_zap_it_key_rec(const struct lu_env *env, const struct dt_it *di,
                             void *key_rec)
{
        struct osd_zap_it *it = (struct osd_zap_it *)di;
        osd_quota_entry_t *qe = (osd_quota_entry_t *)key_rec;
        zap_attribute_t    za;
        int rc;

        rc = zap_cursor_retrieve(it->ozi_zc, &za);
        if (rc == 0) {
                LASSERTF(za.za_integer_length * za.za_num_integers ==
                         sizeof(uint64_t), "interger_length: %d, num_interger:"
                         LPU64"\n", za.za_integer_length, za.za_num_integers);
                qe->qe_id    = simple_strtoull(za.za_name, NULL, 16);
                qe->qe_value = za.za_first_integer;
        }

        return rc;
}

static struct dt_index_operations quota_slave_index_ops = {
        .dio_it     = {
                .init     = qs_zap_it_init,
                .fini     = osd_zap_it_fini,
                .next     = osd_zap_it_next,
                .key_rec  = qs_zap_it_key_rec
        }
};

void osd_set_quota_index_ops(struct dt_object *dt)
{
        struct osd_object   *obj;
        const struct lu_fid *fid;
        ENTRY;

        LASSERT(dt);
        obj = osd_dt_obj(dt);
        fid = lu_object_fid(&dt->do_lu);

        if (lu_fid_eq(fid, &quota_slave_uid_fid) ||
            lu_fid_eq(fid, &quota_slave_gid_fid))
                obj->oo_dt.do_index_ops = &quota_slave_index_ops;
}

#define ISUSER       0
#define ISGROUP      1

static void *quota_start(cfs_seq_file_t *p, loff_t *pos)
{
        return cfs_seq_private(p);
}

static int quota_show(cfs_seq_file_t *f, void *it)
{
        struct dt_object       *dt   = DT_IT2DT(it);
        const struct dt_it_ops *iops = &dt->do_index_ops->dio_it;
        osd_quota_entry_t       qe;
        int                     rc;

        rc = iops->key_rec(NULL, (struct dt_it *)it, &qe);
        if (rc == 0)
                return cfs_seq_printf(f, LPU64"\t\t"LPU64"\n",
                                         qe.qe_id, qe.qe_value);
        else if (rc == ENOENT)
                return 0;
        else
                return -rc;
}

static void *quota_next(cfs_seq_file_t *f, void *it, loff_t *pos)
{
        struct dt_object       *dt   = DT_IT2DT(it);
        const struct dt_it_ops *iops = &dt->do_index_ops->dio_it;
        int rc;

        rc = iops->next(NULL, (struct dt_it *)it);
        if (rc == 0)
                return it;
        else if (rc > 0)
                return NULL;
        else
                return ERR_PTR(rc);
}

static void quota_stop(cfs_seq_file_t *f, void *v)
{
        /* Nothing to do */
}

static cfs_seq_ops_t quota_sops = {
        .start                 = quota_start,
        .stop                  = quota_stop,
        .next                  = quota_next,
        .show                  = quota_show,
};

static int quota_seq_open(cfs_inode_t *inode, cfs_param_file_t *filp, int isgroup)
{
        cfs_param_dentry_t      *dp = CFS_PDE(inode);
        struct osd_device       *osd = cfs_dentry_data(dp);
        cfs_seq_file_t          *seq;
        struct dt_object        *tdo;
        const struct dt_it_ops  *iops;
        struct dt_it            *it;
        int                      rc;

        LPROCFS_ENTRY_AND_CHECK(dp);
        if (isgroup)
                tdo = osd->od_qctxt.qc_slave_gid_dto;
        else
                tdo = osd->od_qctxt.qc_slave_uid_dto;
        iops = &tdo->do_index_ops->dio_it;

        rc = cfs_seq_open(filp, &quota_sops);
        if (rc == 0)
                seq = cfs_file_private(filp);
        else
                return -EINVAL;

        it = iops->init(NULL, tdo, NULL);
        if (IS_ERR(it))
                return PTR_ERR(it);

        cfs_seq_private(seq) = it;
        return rc;
}

static int quota_user_seq_open(cfs_inode_t *inode, cfs_param_file_t *filp)
{
        return quota_seq_open(inode, filp, ISUSER);
}

static int quota_group_seq_open(cfs_inode_t *inode, cfs_param_file_t *filp)
{
        return quota_seq_open(inode, filp, ISGROUP);
}

static int quota_seq_release(cfs_inode_t *inode, cfs_param_file_t *filp)
{
        cfs_seq_file_t *seq  = cfs_file_private(filp);
        struct dt_it      *dit  = seq->private;
        struct dt_object  *dt   = DT_IT2DT(dit);
        const struct dt_it_ops *iops = &dt->do_index_ops->dio_it;

        iops->fini(NULL, dit);
        return(cfs_seq_release(inode, filp));
}

static cfs_param_file_ops_t quota_user_file_ops = {
        .owner   = CFS_PARAM_MODULE,
        .open    = quota_user_seq_open,
        .read    = cfs_seq_read,
        .release = quota_seq_release,
};

static cfs_param_file_ops_t quota_group_file_ops = {
        .owner   = CFS_PARAM_MODULE,
        .open    = quota_group_seq_open,
        .read    = cfs_seq_read,
        .release = quota_seq_release,
};

void osd_quota_procfs_init(struct osd_device *osd)
{
        int rc;

        rc = lprocfs_seq_create(osd->od_proc_entry, "uquota", 0444,
                                &quota_user_file_ops, osd);
        if (rc)
                CERROR("Initializing userquota entry failed!\n");
        rc = lprocfs_seq_create(osd->od_proc_entry, "gquota", 0444,
                                &quota_group_file_ops, osd);
        if (rc)
                CERROR("Initializing groupquota entry failed!\n");
}
