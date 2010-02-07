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
 * Copyright  2008 Sun Microsystems, Inc. All rights reserved
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/mdd/mdd_lov.c
 *
 * Lustre Metadata Server (mds) handling of striped file data
 *
 * Author: Peter Braam <braam@clusterfs.com>
 * Author: wangdi <wangdi@clusterfs.com>
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
#include <obd_lov.h>
#include <lprocfs_status.h>
#include <lustre_mds.h>
#include <lustre_fid.h>
#include <lustre/lustre_idl.h>

#include "mdd_internal.h"

static int mdd_notify(struct obd_device *host, struct obd_device *watched,
                      enum obd_notify_event ev, void *owner, void *data)
{
        struct mdd_device *mdd = owner;
        int rc = 0;
        ENTRY;

        LASSERT(owner != NULL);
        switch (ev)
        {
                case OBD_NOTIFY_ACTIVE:
                case OBD_NOTIFY_SYNC:
                case OBD_NOTIFY_SYNC_NONBLOCK:
                        rc = md_do_upcall(NULL, &mdd->mdd_md_dev,
                                          MD_LOV_SYNC, data);
                        break;
                case OBD_NOTIFY_CONFIG:
                        rc = md_do_upcall(NULL, &mdd->mdd_md_dev,
                                          MD_LOV_CONFIG, data);
                        break;
#ifdef HAVE_QUOTA_SUPPORT
                case OBD_NOTIFY_QUOTA:
                        rc = md_do_upcall(NULL, &mdd->mdd_md_dev,
                                          MD_LOV_QUOTA, data);
                        break;
#endif
                default:
                        CDEBUG(D_INFO, "Unhandled notification %#x\n", ev);
        }

        RETURN(rc);
}

/* The obd is created for handling data stack for mdd */
int mdd_init_obd(const struct lu_env *env, struct mdd_device *mdd,
                 struct lustre_cfg *cfg)
{
        char                   *dev = lustre_cfg_string(cfg, 0);
        int                     rc, name_size, uuid_size;
        char                   *name, *uuid;
        __u32                   mds_id;
        struct lustre_cfg_bufs *bufs;
        struct lustre_cfg      *lcfg;
        struct obd_device      *obd;
        struct mds_obd         *mds;
        ENTRY;

        mds_id = lu_site2md(mdd2lu_dev(mdd)->ld_site)->ms_node_id;
        name_size = strlen(MDD_OBD_NAME) + 35;
        uuid_size = strlen(MDD_OBD_UUID) + 35;

        OBD_ALLOC(name, name_size);
        OBD_ALLOC(uuid, uuid_size);
        if (name == NULL || uuid == NULL)
                GOTO(cleanup_mem, rc = -ENOMEM);

        OBD_ALLOC_PTR(bufs);
        if (!bufs)
                GOTO(cleanup_mem, rc = -ENOMEM);

        snprintf(name, strlen(MDD_OBD_NAME) + 35, "%s-%s-%d",
                 MDD_OBD_NAME, dev, mds_id);

        snprintf(uuid, strlen(MDD_OBD_UUID) + 35, "%s-%s-%d",
                 MDD_OBD_UUID, dev, mds_id);

        lustre_cfg_bufs_reset(bufs, name);
        lustre_cfg_bufs_set_string(bufs, 1, MDD_OBD_TYPE);
        lustre_cfg_bufs_set_string(bufs, 2, uuid);
        lustre_cfg_bufs_set_string(bufs, 3, (char*)dev/* MDD_OBD_PROFILE */);
        lustre_cfg_bufs_set_string(bufs, 4, (char*)dev);

        lcfg = lustre_cfg_new(LCFG_ATTACH, bufs);
        OBD_FREE_PTR(bufs);
        if (!lcfg)
                GOTO(cleanup_mem, rc = -ENOMEM);

        rc = class_attach(lcfg);
        if (rc)
                GOTO(lcfg_cleanup, rc);

        obd = class_name2obd(name);
        if (!obd) {
                CERROR("Can not find obd %s\n", MDD_OBD_NAME);
                LBUG();
        }

        mds = &obd->u.mds;
        mds->mds_next_dev = mdd->mdd_child;
        obd->obd_recovering = 1;
        obd->u.mds.mds_id = mds_id;
        rc = class_setup(obd, lcfg);
        if (rc)
                GOTO(class_detach, rc);

        /*
         * Add here for obd notify mechanism, when adding a new ost, the mds
         * will notify this mdd. The mds will be used for quota also.
         */
        obd->obd_upcall.onu_upcall = mdd_notify;
        obd->obd_upcall.onu_owner = mdd;
        mdd->mdd_obd_dev = obd;
        EXIT;
class_detach:
        if (rc)
                class_detach(obd, lcfg);
lcfg_cleanup:
        lustre_cfg_free(lcfg);
cleanup_mem:
        if (name)
                OBD_FREE(name, name_size);
        if (uuid)
                OBD_FREE(uuid, uuid_size);
        return rc;
}

int mdd_fini_obd(const struct lu_env *env, struct mdd_device *mdd,
                 struct lustre_cfg *lcfg)
{
        struct obd_device      *obd;
        int rc;
        ENTRY;

        obd = mdd2obd_dev(mdd);
        LASSERT(obd);

        rc = class_cleanup(obd, lcfg);
        if (rc)
                GOTO(lcfg_cleanup, rc);

        obd->obd_upcall.onu_upcall = NULL;
        obd->obd_upcall.onu_owner = NULL;
        rc = class_detach(obd, lcfg);
        if (rc)
                GOTO(lcfg_cleanup, rc);
        mdd->mdd_obd_dev = NULL;

        EXIT;
lcfg_cleanup:
        return rc;
}

int mdd_get_md(const struct lu_env *env, struct mdd_object *obj,
               void *md, int *md_size, const char *name)
{
        int rc;
        ENTRY;

        rc = mdo_xattr_get(env, obj, mdd_buf_get(env, md, *md_size), name,
                           mdd_object_capa(env, obj));
        /*
         * XXX: Handling of -ENODATA, the right way is to have ->do_md_get()
         * exported by dt layer.
         */
        if (rc == 0 || rc == -ENODATA) {
                *md_size = 0;
                rc = 0;
        } else if (rc < 0) {
                CERROR("Error %d reading eadata - %d\n", rc, *md_size);
        } else {
                /* XXX: Convert lov EA but fixed after verification test. */
                *md_size = rc;
        }

        RETURN(rc);
}

int mdd_get_md_locked(const struct lu_env *env, struct mdd_object *obj,
                      void *md, int *md_size, const char *name)
{
        int rc = 0;
        mdd_read_lock(env, obj, MOR_TGT_CHILD);
        rc = mdd_get_md(env, obj, md, md_size, name);
        mdd_read_unlock(env, obj);
        return rc;
}

static int mdd_lov_set_stripe_md(const struct lu_env *env,
                                 struct mdd_object *obj, struct lu_buf *buf,
                                 struct thandle *handle)
{
        struct mdd_device       *mdd = mdo2mdd(&obj->mod_obj);
        struct obd_device       *obd = mdd2obd_dev(mdd);
        struct obd_export       *lov_exp = obd->u.mds.mds_osc_exp;
        struct lov_stripe_md    *lsm = NULL;
        int rc;
        ENTRY;

        LASSERT(S_ISDIR(mdd_object_type(obj)) || S_ISREG(mdd_object_type(obj)));
        rc = obd_iocontrol(OBD_IOC_LOV_SETSTRIPE, lov_exp, 0,
                           &lsm, buf->lb_buf);
        if (rc)
                RETURN(rc);
        obd_free_memmd(lov_exp, &lsm);

        rc = mdd_xattr_set_txn(env, obj, buf, XATTR_NAME_LOV, 0, handle);

        CDEBUG(D_INFO, "set lov ea of "DFID" rc %d \n", PFID(mdo2fid(obj)), rc);
        RETURN(rc);
}

/*
 * Permission check is done before call it,
 * no need check again.
 */
static int mdd_lov_set_dir_md(const struct lu_env *env,
                              struct mdd_object *obj, struct lu_buf *buf,
                              struct thandle *handle)
{
        struct lov_user_md *lum = NULL;
        int rc = 0;
        ENTRY;

        LASSERT(S_ISDIR(mdd_object_type(obj)));
        lum = (struct lov_user_md*)buf->lb_buf;

        /* if { size, offset, count } = { 0, -1, 0 } and no pool (i.e. all default
         * values specified) then delete default striping from dir. */
        if (lum->lmm_stripe_size == 0 && lum->lmm_stripe_count == 0 &&
            lum->lmm_stripe_offset == (typeof(lum->lmm_stripe_offset))(-1) &&
            lum->lmm_magic != LOV_USER_MAGIC_V3) {
                rc = mdd_xattr_set_txn(env, obj, &LU_BUF_NULL,
                                       XATTR_NAME_LOV, 0, handle);
                if (rc == -ENODATA)
                        rc = 0;
                CDEBUG(D_INFO, "delete lov ea of "DFID" rc %d \n",
                                PFID(mdo2fid(obj)), rc);
        } else {
                rc = mdd_lov_set_stripe_md(env, obj, buf, handle);
        }
        RETURN(rc);
}

int mdd_lsm_sanity_check(const struct lu_env *env,  struct mdd_object *obj)
{
        struct lu_attr   *tmp_la = &mdd_env_info(env)->mti_la;
        struct md_ucred  *uc     = md_ucred(env);
        int rc;
        ENTRY;

        rc = mdd_la_get(env, obj, tmp_la, BYPASS_CAPA);
        if (rc)
                RETURN(rc);

        if ((uc->mu_fsuid != tmp_la->la_uid) &&
            !mdd_capable(uc, CFS_CAP_FOWNER))
                rc = mdd_permission_internal_locked(env, obj, tmp_la,
                                                    MAY_WRITE, MOR_TGT_CHILD);

        RETURN(rc);
}

int mdd_lov_set_md(const struct lu_env *env, struct mdd_object *pobj,
                   struct mdd_object *child, struct lov_mds_md *lmmp,
                   int lmm_size, struct thandle *handle, int set_stripe)
{
        struct lu_buf *buf;
        cfs_umode_t mode;
        int rc = 0;
        ENTRY;

        if (mdd2obd_dev(mdd_obj2mdd_dev(child)) == NULL) {
                /* XXX: temp hack as we don't have mdd obd yet */
                RETURN(0);
        }

        buf = mdd_buf_get(env, lmmp, lmm_size);
        mode = mdd_object_type(child);
        if (S_ISREG(mode) && lmm_size > 0) {
                if (set_stripe) {
                        rc = mdd_lov_set_stripe_md(env, child, buf, handle);
                } else {
                        rc = mdd_xattr_set_txn(env, child, buf,
                                               XATTR_NAME_LOV, 0, handle);
                }
        } else if (S_ISDIR(mode)) {
                if (lmmp == NULL && lmm_size == 0) {
                        struct mdd_device *mdd = mdd_obj2mdd_dev(child);
                        struct lov_mds_md *lmm = mdd_max_lmm_get(env, mdd);
                        int size = sizeof(struct lov_mds_md_v3);

                        /* Get parent dir stripe and set */
                        if (pobj != NULL)
                                rc = mdd_get_md_locked(env, pobj, lmm, &size,
                                                       XATTR_NAME_LOV);
                        if (rc > 0) {
                                buf = mdd_buf_get(env, lmm, size);
                                rc = mdd_xattr_set_txn(env, child, buf,
                                               XATTR_NAME_LOV, 0, handle);
                                if (rc)
                                        CERROR("error on copy stripe info: rc "
                                                "= %d\n", rc);
                        }
                } else {
                        LASSERT(lmmp != NULL && lmm_size > 0);
                        rc = mdd_lov_set_dir_md(env, child, buf, handle);
                }
        }
        CDEBUG(D_INFO, "Set lov md %p size %d for fid "DFID" rc %d\n",
                        lmmp, lmm_size, PFID(mdo2fid(child)), rc);
        RETURN(rc);
}

