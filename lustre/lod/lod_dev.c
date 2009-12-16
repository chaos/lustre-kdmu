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
 * Copyright  2009 Sun Microsystems, Inc. All rights reserved
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/lod/lod_dev.c
 *
 * Lustre Multi-oBject Device
 *
 * Author: Alex Zhuravlev <bzzz@sun.com>
 */


/*
 * TODO
 *  - shutdown
 *  - support for different LOV EA formats
 *  - load/init striping info on demand
 *  - object allocation
 *  - ost removal/deactivation
 *  - lod_ah_init() to grab default striping from parent or fs
 *  - lod_qos_prep_create() to support non-zero files w/o objects
 *  - lod_alloc_idx_array() to support object creation on specified OSTs
 *  - lod_qos_prep_create() to support pools
 *  - how lod_alloc_qos() can learn OST slowness w/o obd_precreate()
 *  - improve locking in lod_qos_statfs_update()
 *  - lod_qos_statfs_update() to recalculate space with fixed block size
 *
 */


#ifndef EXPORT_SYMTAB
# define EXPORT_SYMTAB
#endif
#define DEBUG_SUBSYSTEM S_MDS

#include <linux/module.h>
#include <obd.h>
#include <obd_class.h>
#include <lustre_ver.h>
#include <obd_support.h>
#include <lprocfs_status.h>

#include <lustre_disk.h>
#include <lustre_fid.h>
#include <lustre_mds.h>
#include <lustre/lustre_idl.h>
#include <lustre_param.h>
#include <lustre_fid.h>

#include "lod_internal.h"

extern struct lu_object_operations lod_lu_obj_ops;
extern struct dt_object_operations lod_obj_ops;

struct lu_object *lod_object_alloc(const struct lu_env *env,
                                    const struct lu_object_header *hdr,
                                    struct lu_device *d)
{
        struct lod_object *o;

        OBD_ALLOC_PTR(o);
        if (o != NULL) {
                struct lu_object *l;

                l = lod2lu_obj(o);
                dt_object_init(&o->mbo_obj, NULL, d);
                o->mbo_obj.do_ops = &lod_obj_ops;
                l->lo_ops = &lod_lu_obj_ops;
                return l;
        } else {
                return NULL;
        }
}

static int lod_process_config(const struct lu_env *env,
                               struct lu_device *dev,
                               struct lustre_cfg *lcfg)
{
        struct lod_device *lod = lu2lod_dev(dev);
        char               *arg1;
        int                 rc;
        //struct lu_device   *next = &lod->mbd_child->dd_lu_dev;
        ENTRY;
        switch(lcfg->lcfg_command) {

                case LCFG_LOV_ADD_OBD: {
                        __u32 index;
                        int gen;
                        /* lov_modify_tgts add  0:lov_mdsA  1:osp  2:0  3:1 */
                        arg1 = lustre_cfg_string(lcfg, 1);

                        if (sscanf(lustre_cfg_buf(lcfg, 2), "%d", &index) != 1)
                                GOTO(out, rc = -EINVAL);
                        if (sscanf(lustre_cfg_buf(lcfg, 3), "%d", &gen) != 1)
                                GOTO(out, rc = -EINVAL);
                        rc = lod_lov_add_device(env, lod, arg1, index, gen);
                        break;
                }

                case LCFG_LOV_DEL_OBD:
                case LCFG_LOV_ADD_INA:
                        /* XXX: not implemented yet */
                        LBUG();
                        break;
                default:
                        CERROR("unknown command %x\n", lcfg->lcfg_command);
                        rc = 0;
                        break;
        }

        //rc = next->ld_ops->ldo_process_config(env, next, lcfg);
out:
        RETURN(rc);
}

static int lod_recovery_complete(const struct lu_env *env,
                                     struct lu_device *dev)
{
        struct lod_device *lod = lu2lod_dev(dev);
        struct lu_device   *next = &lod->mbd_child->dd_lu_dev;
        int rc;
        ENTRY;
        rc = next->ld_ops->ldo_recovery_complete(env, next);
        RETURN(rc);
}

static int lod_prepare(const struct lu_env *env,
                       struct lu_device *pdev,
                       struct lu_device *cdev)
{
        struct lod_device *lod = lu2lod_dev(cdev);
        struct lu_device   *next = &lod->mbd_child->dd_lu_dev;
        int rc;
        ENTRY;
        rc = next->ld_ops->ldo_prepare(env, pdev, next);
        RETURN(rc);
}

const struct lu_device_operations lod_lu_ops = {
        .ldo_object_alloc      = lod_object_alloc,
        .ldo_process_config    = lod_process_config,
        .ldo_recovery_complete = lod_recovery_complete,
        .ldo_prepare           = lod_prepare,
};

static int lod_root_get(const struct lu_env *env,
                        struct dt_device *dev, struct lu_fid *f)
{
        struct lod_device *d = dt2lod_dev(dev);
        struct dt_device   *next = d->mbd_child;
        int                 rc;
        ENTRY;

        rc = next->dd_ops->dt_root_get(env, next, f);

        RETURN(rc);
}

static int lod_statfs(const struct lu_env *env,
                       struct dt_device *dev, struct kstatfs *sfs)
{
        struct lod_device *d = dt2lod_dev(dev);
        struct dt_device   *next = d->mbd_child;
        int                 rc;
        ENTRY;

        rc = next->dd_ops->dt_statfs(env, next, sfs);

        RETURN(rc);
}

static struct thandle *lod_trans_create(const struct lu_env *env,
                                         struct dt_device *dev)
{
        struct lod_device *d = dt2lod_dev(dev);
        struct dt_device   *next = d->mbd_child;
        struct thandle     *th;
        ENTRY;

        th = next->dd_ops->dt_trans_create(env, next);

        RETURN(th);
}

static int lod_trans_start(const struct lu_env *env, struct dt_device *dev,
                            struct thandle *th)
{
        struct lod_device *d = dt2lod_dev(dev);
        struct dt_device   *next = d->mbd_child;
        int                 rc;
        ENTRY;

        rc = next->dd_ops->dt_trans_start(env, next, th);

        RETURN(rc);
}

static int lod_trans_stop(const struct lu_env *env, struct thandle *th)
{
        struct dt_device   *next = th->th_dev;
        int                 rc;
        ENTRY;

        /* XXX: currently broken as we don't know next device */
        rc = next->dd_ops->dt_trans_stop(env, th);

        RETURN(rc);
}

static void lod_conf_get(const struct lu_env *env,
                          const struct dt_device *dev,
                          struct dt_device_param *param)
{
        struct lod_device *d = dt2lod_dev((struct dt_device *) dev);
        struct dt_device   *next = d->mbd_child;
        ENTRY;

        next->dd_ops->dt_conf_get(env, next, param);

        EXIT;
        return;
}

static int lod_sync(const struct lu_env *env, struct dt_device *dev)
{
        struct lod_device *d = dt2lod_dev(dev);
        struct dt_device   *next = d->mbd_child;
        int                 rc;
        ENTRY;

        rc = next->dd_ops->dt_sync(env, next);

        RETURN(rc);
}

static void lod_ro(const struct lu_env *env, struct dt_device *dev)
{
        struct lod_device *d = dt2lod_dev(dev);
        struct dt_device   *next = d->mbd_child;
        ENTRY;

        next->dd_ops->dt_ro(env, next);

        EXIT;
}

static int lod_commit_async(const struct lu_env *env, struct dt_device *dev)
{
        struct lod_device *d = dt2lod_dev(dev);
        struct dt_device   *next = d->mbd_child;
        int                 rc;
        ENTRY;

        rc = next->dd_ops->dt_commit_async(env, next);

        RETURN(rc);
}

static int lod_init_capa_ctxt(const struct lu_env *env,
                                   struct dt_device *dev,
                                   int mode, unsigned long timeout,
                                   __u32 alg, struct lustre_capa_key *keys)
{
        struct lod_device *d = dt2lod_dev(dev);
        struct dt_device   *next = d->mbd_child;
        int                 rc;
        ENTRY;

        rc = next->dd_ops->dt_init_capa_ctxt(env, next, mode, timeout, alg, keys);

        RETURN(rc);
}

static void lod_init_quota_ctxt(const struct lu_env *env,
                                   struct dt_device *dev,
                                   struct dt_quota_ctxt *ctxt, void *data)
{
        struct lod_device *d = dt2lod_dev(dev);
        struct dt_device   *next = d->mbd_child;
        ENTRY;

        next->dd_ops->dt_init_quota_ctxt(env, next, ctxt, data);

        EXIT;
}

static char *lod_label_get(const struct lu_env *env, const struct dt_device *dev)
{
        struct lod_device *d = dt2lod_dev((struct dt_device *) dev);
        struct dt_device   *next = d->mbd_child;
        char *l;
        ENTRY;

        l = next->dd_ops->dt_label_get(env, next);

        RETURN(l);
}


static int lod_label_set(const struct lu_env *env, const struct dt_device *dev,
                          char *l)
{
        struct lod_device *d = dt2lod_dev((struct dt_device *) dev);
        struct dt_device   *next = d->mbd_child;
        int                 rc;
        ENTRY;

        rc = next->dd_ops->dt_label_set(env, next, l);

        RETURN(rc);
}

static const struct dt_device_operations lod_dt_ops = {
        .dt_root_get       = lod_root_get,
        .dt_statfs         = lod_statfs,
        .dt_trans_create   = lod_trans_create,
        .dt_trans_start    = lod_trans_start,
        .dt_trans_stop     = lod_trans_stop,
        .dt_conf_get       = lod_conf_get,
        .dt_sync           = lod_sync,
        .dt_ro             = lod_ro,
        .dt_commit_async   = lod_commit_async,
        .dt_init_capa_ctxt = lod_init_capa_ctxt,
        .dt_init_quota_ctxt= lod_init_quota_ctxt,
        .dt_label_get      = lod_label_get,
        .dt_label_set      = lod_label_set
};

static int lod_init0(const struct lu_env *env, struct lod_device *m,
                      struct lu_device_type *ldt, struct lustre_cfg *cfg)
{
        struct obd_device         *obd;
        int                        rc;
        ENTRY;

        dt_device_init(&m->mbd_dt_dev, ldt);

        obd = class_name2obd(lustre_cfg_string(cfg, 3));
        if (obd == NULL) {
                CERROR("can't found next device: %s\n",lustre_cfg_string(cfg, 3));
                RETURN(-EINVAL);
        }
        LASSERT(obd->obd_lu_dev);
        LASSERT(obd->obd_lu_dev->ld_site);
        m->mbd_child = lu2dt_dev(obd->obd_lu_dev);
        m->mbd_dt_dev.dd_lu_dev.ld_site = obd->obd_lu_dev->ld_site;
        /* XXX: grab reference on next device? */

        /* setup obd to be used with old lov code */
        rc = lod_lov_init(m, cfg);

        sema_init(&m->mbd_mutex, 1);

        RETURN(0);
}

static struct lu_device *lod_device_alloc(const struct lu_env *env,
                                          struct lu_device_type *t,
                                          struct lustre_cfg *lcfg)
{
        struct lod_device *m;
        struct lu_device   *l;

        OBD_ALLOC_PTR(m);
        if (m == NULL) {
                l = ERR_PTR(-ENOMEM);
        } else {
                lod_init0(env, m, t, lcfg);
                l = lod2lu_dev(m);
                l->ld_ops = &lod_lu_ops;
                m->mbd_dt_dev.dd_ops = &lod_dt_ops;
                /* XXX: dt_upcall_init(&m->mbd_dt_dev, NULL); */
        }

        return l;
}

static struct lu_device *lod_device_fini(const struct lu_env *env,
                                          struct lu_device *d)
{
        LBUG();
        return NULL;
}

static struct lu_device *lod_device_free(const struct lu_env *env,
                                         struct lu_device *lu)
{
        struct lod_device *m = lu2lod_dev(lu);
        struct lu_device   *next = &m->mbd_child->dd_lu_dev;
        ENTRY;

        LASSERT(atomic_read(&lu->ld_ref) == 0);
        dt_device_fini(&m->mbd_dt_dev);
        OBD_FREE_PTR(m);
        RETURN(next);
}

/* context key constructor/destructor: mdt_key_init, mdt_key_fini */
LU_KEY_INIT_FINI(lod, struct lod_thread_info);

/* context key: lod_thread_key */
LU_CONTEXT_KEY_DEFINE(lod, LCT_DT_THREAD);

LU_TYPE_INIT_FINI(lod, &lod_thread_key);

static struct lu_device_type_operations lod_device_type_ops = {
        .ldto_init           = lod_type_init,
        .ldto_fini           = lod_type_fini,

        .ldto_start          = lod_type_start,
        .ldto_stop           = lod_type_stop,

        .ldto_device_alloc   = lod_device_alloc,
        .ldto_device_free    = lod_device_free,

        .ldto_device_fini    = lod_device_fini
};

static struct lu_device_type lod_device_type = {
        .ldt_tags     = LU_DEVICE_DT,
        .ldt_name     = LUSTRE_LOD_NAME,
        .ldt_ops      = &lod_device_type_ops,
        .ldt_ctx_tags = LCT_DT_THREAD
};

static struct obd_ops lod_obd_device_ops = {
        .o_owner = THIS_MODULE
};


static int __init lod_mod_init(void)
{
        struct lprocfs_static_vars lvars;
        //lprocfs_lod_init_vars(&lvars);

        return class_register_type(&lod_obd_device_ops, NULL, lvars.module_vars,
                                   LUSTRE_LOD_NAME, &lod_device_type);
}

static void __exit lod_mod_exit(void)
{

        class_unregister_type(LUSTRE_LOD_NAME);
}

MODULE_AUTHOR("Sun Microsystems, Inc. <http://www.lustre.org/>");
MODULE_DESCRIPTION("Lustre Multi-oBject Device ("LUSTRE_MDD_NAME")");
MODULE_LICENSE("GPL");

cfs_module(lod, "0.1.0", lod_mod_init, lod_mod_exit);

