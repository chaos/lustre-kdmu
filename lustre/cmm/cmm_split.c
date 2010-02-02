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
 * lustre/cmm/cmm_split.c
 *
 * Lustre splitting dir
 *
 * Author: Alex Thomas  <alex@clusterfs.com>
 * Author: Wang Di      <wangdi@clusterfs.com>
 * Author: Yury Umanets <umka@clusterfs.com>
 */

#ifndef EXPORT_SYMTAB
# define EXPORT_SYMTAB
#endif

#define DEBUG_SUBSYSTEM S_MDS

#include <obd_class.h>
#include <lustre_fid.h>
#include <lustre_mds.h>
#include <lustre/lustre_idl.h>
#include "cmm_internal.h"
#include "mdc_internal.h"

/**
 * Check whether the directory can be set stripe
 * */
static int cmm_dirstripe_sanity_check(const struct lu_env *env,
                                      struct md_object *mo,
                                      struct md_attr *ma,
                                      struct lmv_user_md *lum)
{
        struct cmm_device *cmm = cmm_obj2dev(md2cmm_obj(mo));
        struct lu_fid root_fid;
        int rc;
        ENTRY;

        /* Valid lum */
        if (lum->lum_hash_type > LMV_HASH_MAX)
                RETURN(-EINVAL);

        if (lum->lum_type != LMV_STRIPE_TYPE)
                RETURN(-EINVAL);

        /* Can not split root object. */
        rc = cmm_child_ops(cmm)->mdo_root_get(env, cmm->cmm_child,
                                              &root_fid);
        if (rc) {
                CERROR("Can not get root fid rc %d \n", rc);
                RETURN(rc);
        }

        if (lu_fid_eq(&root_fid, cmm2fid(md2cmm_obj(mo))))
                RETURN(-EACCES);

        /* Only set dirstripe for empty dir */
        if (mo_is_empty(env, md_object_next(mo)) != 0)
                RETURN(-EACCES);

        /* No need split for already split object */
        if (ma->ma_valid & MA_LMV) {
                LASSERT(ma->ma_lmv_size > 0);
                RETURN(-EEXIST);
        }

        RETURN(0);
}

struct cmm_object *cmm_object_find(const struct lu_env *env,
                                   struct cmm_device *d,
                                   const struct lu_fid *fid)
{
        return md2cmm_obj(md_object_find_slice(env, &d->cmm_md_dev, fid));
}

static inline void cmm_object_put(const struct lu_env *env,
                                  struct cmm_object *o)
{
        lu_object_put(env, &o->cmo_obj.mo_lu);
}

/*
 * Allocate new on passed @mc for slave object which is going to create there
 * soon.
 */
static int cmm_split_fid_alloc(const struct lu_env *env,
                               struct cmm_device *cmm,
                               struct mdc_device *mc,
                               struct lu_fid *fid)
{
        int rc;
        ENTRY;

        LASSERT(cmm != NULL && mc != NULL && fid != NULL);

        cfs_down(&mc->mc_fid_sem);

        /* Alloc new fid on @mc. */
        rc = obd_fid_alloc(mc->mc_desc.cl_exp, fid, NULL);
        if (rc > 0)
                rc = 0;
        cfs_up(&mc->mc_fid_sem);

        RETURN(rc);
}

/* Allocate new slave object on passed @mc */
static int cmm_split_slave_create(const struct lu_env *env,
                                  struct cmm_device *cmm,
                                  struct mdc_device *mc,
                                  struct lu_fid *fid,
                                  struct md_attr *ma,
                                  struct lmv_mds_md *lmv,
                                  int lmv_size)
{
        struct md_op_spec *spec = &cmm_env_info(env)->cmi_spec;
        struct cmm_object *obj;
        int rc;
        ENTRY;

        /* Allocate new fid and store it to @fid */
        rc = cmm_split_fid_alloc(env, cmm, mc, fid);
        if (rc) {
                CERROR("Can't alloc new fid on %u, rc %d\n",
                        mc->mc_num, rc);
                RETURN(rc);
        }

        /* Allocate new object on @mc */
        obj = cmm_object_find(env, cmm, fid);
        if (IS_ERR(obj))
                RETURN(PTR_ERR(obj));

        cmm_set_specea_by_ma(spec, ma, (const struct lu_fid *)fid);

        spec->u.sp_ea.lmvdata = lmv;
        spec->u.sp_ea.lmvdatalen = lmv_size;
        spec->sp_cr_flags |= MDS_CREATE_SLAVE_OBJ;
        rc = mo_object_create(env, md_object_next(&obj->cmo_obj),
                              spec, ma);
        cmm_object_put(env, obj);
        RETURN(rc);
}

/**
 * Create stripe object.
 */
static int cmm_split_slaves_create(const struct lu_env *env,
                                   struct md_object *mo,
                                   struct md_attr *ma)
{
        struct cmm_device    *cmm = cmm_obj2dev(md2cmm_obj(mo));
        struct lu_fid        *lf  = cmm2fid(md2cmm_obj(mo));
        struct lmv_mds_md    *slave_lmv;
        struct mdc_device    *mc, *tmp;
        struct lmv_stripe_md *lmv = ma->ma_lmv;
        int                  i = 1, rc = 0;
        int                  count = ma->ma_lmv->mea_count;
        ENTRY;

        /*
         * Store master FID to local node idx number. Local node is always
         * master and its stripe number if 0.
         */
        lmv->mea_oinfo[0].lmo_fid = *lf;

        if (count == 1)
                GOTO(cleanup, rc);

        slave_lmv = (struct lmv_mds_md *)&cmm_env_info(env)->cmi_lmv;
        memset(slave_lmv, 0, sizeof *slave_lmv);
        slave_lmv->lmv_master = cmm->cmm_local_num;
        slave_lmv->lmv_magic = LMV_MAGIC_V1;
        slave_lmv->lmv_count = 0;
        /*
         * XXX: stripe index does not work now.
         * XXX: QoS comes here
         */
        cfs_list_for_each_entry_safe(mc, tmp, &cmm->cmm_targets, mc_linkage) {
                if (!--count)
                        break;
                rc = cmm_split_slave_create(env, cmm, mc, &lmv->mea_oinfo[i].lmo_fid,
                                            ma, slave_lmv, sizeof(*slave_lmv));
                if (rc)
                        GOTO(cleanup, rc);
                i++;
        }
        EXIT;
cleanup:
        return rc;
}

/*
 * Split a dir by mea, this is called from mdt_setattr to expand the
 * dir to the specific set of mdses.
 */
int cmm_set_dirstripe(const struct lu_env *env, struct md_object *mo,
                      const struct md_attr *md_set)
{
        struct cmm_device       *cmm = cmm_obj2dev(md2cmm_obj(mo));
        struct md_attr          *ma  = &cmm_env_info(env)->cmi_ma;
        struct lmv_user_md      *lum = md_set->ma_defaultlmv;
        struct lmv_stripe_md    *lsm;
        struct lmv_mds_md       *lmm = NULL;
        int                     rc = 0;
        int                     stripe_count;
        int                     lmv_size;
        struct lu_buf           *buf;
        ENTRY;

        /* sanity check for lum */
        if (md_set->ma_defaultlmv_size != sizeof(*lum))
                RETURN(-EPROTO);

        if (lum->lum_magic == __swab32(LMV_USER_MAGIC)) {
                lustre_swab_lmv_user_md(lum);
        } else if (lum->lum_magic != LMV_USER_MAGIC) {
                CERROR("lmv unknown magic %x \n", lum->lum_magic);
                RETURN(-EINVAL);
        }

        cmm_lprocfs_time_start(env);

        LASSERT(S_ISDIR(lu_object_attr(&mo->mo_lu)));
        memset(ma, 0, sizeof(*ma));

        ma->ma_defaultlmv = &cmm_env_info(env)->cmi_default_lmv;
        ma->ma_defaultlmv_size = sizeof(struct lmv_user_md);
        ma->ma_lmm = (struct lov_mds_md *)&cmm_env_info(env)->cmi_lov;
        ma->ma_lmm_size = sizeof(struct lov_mds_md);

        ma->ma_need = MA_INODE | MA_LMV | MA_LOV | MA_LMV_DEF;
        /**
         * Note: ma->ma_lmv = NULL, ma_lmv_size = 0, but
         * ma_need |= MA_LMV, so it only check whether the
         * the object has dirstripe and it is assumed the
         * backup fs support that.
         */
        rc = mo_attr_get(env, mo, ma);
        if (rc)
                RETURN(rc);

        rc = cmm_dirstripe_sanity_check(env, mo, ma, lum);
        if (rc)
                GOTO(out, rc);

        stripe_count = lum->lum_stripe_count == 0 ? 1 : lum->lum_stripe_count;
        if (stripe_count < 0 || stripe_count > cmm->cmm_tgt_count + 1)
                stripe_count = cmm->cmm_tgt_count + 1;

        lmv_alloc_memmd(&ma->ma_lmv, stripe_count);
        if (ma->ma_lmv == NULL)
                GOTO(out, rc = -ENOMEM);

        /* Init the split MEA */
        lsm = ma->ma_lmv;
        lsm->mea_magic = LMV_MAGIC_V1;
        lsm->mea_count = stripe_count;
        lsm->mea_hash_type = lum->lum_hash_type;
        lsm->mea_master = cmm->cmm_local_num;
        rc = cmm_split_slaves_create(env, mo, ma);
        if (rc) {
                CERROR("Can't create slaves for split, rc %d\n", rc);
                GOTO(free_lsm, rc);
        }

        lmv_size = lmv_pack_md(&lmm, ma->ma_lmv, 0);
        if (lmv_size <= 0)
                GOTO(free_lsm, rc = lmv_size);

        buf = cmm_buf_get(env, lmm, lmv_size);
        rc = mo_xattr_set(env, md_object_next(mo), buf, XATTR_NAME_LMV, 0);
        if (rc) {
                /* FIXME: Cleanup the split slaves objects*/
                CERROR("Can't set stripe info to master dir, " "rc %d\n", rc);
                GOTO(free_lmv, rc);
        }
        EXIT;
free_lmv:
        lmv_free_md(lmm);
free_lsm:
        lmv_free_memmd(ma->ma_lmv);
out:
        cmm_lprocfs_time_end(env, cmm, LPROC_CMM_SPLIT);
        return rc;
}
