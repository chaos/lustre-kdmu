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
 * lustre/osp/osp_object.c
 *
 * Lustre OST Proxy Device
 *
 * Author: Alex Zhuravlev <bzzz@sun.com>
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

#include "osp_internal.h"

static int osp_declare_attr_set(const struct lu_env *env,
                                 struct dt_object *dt,
                                 const struct lu_attr *attr,
                                 struct thandle *th)
{
        struct osp_object  *o = dt2osp_obj(dt);
        int                 rc = 0;
        ENTRY;

        if (attr && !(attr->la_valid & (LA_UID | LA_GID)))
                RETURN(0);

        /*
         * track all UID/GID changes via llog
         */
        rc = osp_sync_declare_add(env, o, MDS_SETATTR64_REC, th);

        RETURN(rc);
}

static int osp_attr_set(const struct lu_env *env,
                         struct dt_object *dt,
                         const struct lu_attr *attr,
                         struct thandle *th,
                         struct lustre_capa *capa)
{
        struct osp_object  *o = dt2osp_obj(dt);
        int                 rc = 0;
        ENTRY;

        /* 
         * XXX: first ->attr_set() can be ignored, but this is a trick
         * better would be to make ->do_create() to initialize attributes
         * and not do the very first ->attr_set() at all
         */
        if (o->opo_no_attrs) {
                o->opo_no_attrs = 0;
                RETURN(0);
        }

        /* we're interested in uid/gid changes only */
        if (!(attr->la_valid & (LA_UID | LA_GID)))
                RETURN(0);

        /*
         * once transaction is committed put proper command on
         * the queue going to our OST
         */
        rc = osp_sync_add(env, o, MDS_SETATTR64_REC, th);

        /* XXX: we need a change in OSD API to track committness */

        /* XXX: send new uid/gid to OST ASAP? */

        RETURN(rc);
}

static int osp_declare_object_create(const struct lu_env *env,
                                      struct dt_object *dt,
                                      struct lu_attr *attr,
                                      struct dt_allocation_hint *hint,
                                      struct dt_object_format *dof,
                                      struct thandle *th)
{
        struct osp_device  *d = lu2osp_dev(dt->do_lu.lo_dev);
        struct osp_object   *o = dt2osp_obj(dt);
        int                 rc = 0;
        ENTRY;

        LASSERT(d->opd_last_used_file);

        /*
         * in declaration we need to reserve object so that we don't block
         * awaiting precreation RPC to complete
         */
        rc = osp_precreate_reserve(d);

         /*
         * we also need to declare update to local "last used id" file for recovery
         * if object isn't used for a reason, we need to release reservation,
         * this can be made in osd_object_release()
         */
        if (rc == 0) {
                /* mark id is reserved: in create we don't want to talk to OST */
                LASSERT(o->opo_reserved == 0);
                o->opo_reserved = 1;

                /* XXX: for compatibility use common for all OSPs file */
                rc = dt_declare_record_write(env, d->opd_last_used_file, 8, 0, th);
        }

        RETURN(rc);
}

static int osp_object_create(const struct lu_env *env,
                              struct dt_object *dt,
                              struct lu_attr *attr,
                              struct dt_allocation_hint *hint,
                              struct dt_object_format *dof,
                              struct thandle *th)
{
        struct osp_device   *d = lu2osp_dev(dt->do_lu.lo_dev);
        struct osp_object   *o = dt2osp_obj(dt);
        struct lu_buf        lb;
        struct lu_fid        fid;
        obd_id               objid;
        loff_t               offset;
        int                  rc = 0;
        int                  update = 0;
        ENTRY;

        /* XXX: to support CMD we need group here, to be put into config? */

        LASSERT(o->opo_reserved != 0);
        o->opo_reserved = 0;

        /* grab next id from the pool */
        cfs_spin_lock(&d->opd_pre_lock);
        LASSERT(d->opd_pre_next <= d->opd_pre_last_created);
        objid = d->opd_pre_next++;
        d->opd_pre_reserved--;

        /*
         * update last_used object id for our OST
         * XXX: can we use 0-copy OSD methods to save memcpy()
         * which is going to be each creation * <# stripes>
         */
        if (objid > d->opd_last_used_id) {
                d->opd_last_used_id = objid;
                update = 1;
        }
        cfs_spin_unlock(&d->opd_pre_lock);

        /* assign fid to anonymous object */
        lu_idif_build(&fid, objid, d->opd_index);
        lu_object_assign_fid(env, &dt->do_lu, &fid);

        if (update) {
                /* we updated last_used in-core, so we update on a disk */
                objid = cpu_to_le64(objid);
                lb.lb_buf = &objid;
                lb.lb_len = sizeof(objid);

                offset = d->opd_index * sizeof(objid);
                rc = dt_record_write(env, d->opd_last_used_file, &lb,
                                     &offset, th);
        }

        /* object is created, we can ignore first attr_set from being logged */
        o->opo_no_attrs = 1;

        RETURN(rc);
}

static int osp_declare_object_destroy(const struct lu_env *env,
                                      struct dt_object *dt,
                                      struct thandle *th)
{
        struct osp_object  *o = dt2osp_obj(dt);
        int                 rc = 0;
        ENTRY;

        /*
         * track objects to be destroyed via llog
         */
        rc = osp_sync_declare_add(env, o, MDS_UNLINK_REC, th);

        RETURN(rc);
}

static int osp_object_destroy(const struct lu_env *env,
                         struct dt_object *dt,
                         struct thandle *th)
{
        struct osp_object  *o = dt2osp_obj(dt);
        int                 rc = 0;
        ENTRY;

        /*
         * once transaction is committed put proper command on
         * the queue going to our OST
         */
        rc = osp_sync_add(env, o, MDS_UNLINK_REC, th);

        /* XXX: we need a change in OSD API to track committness */


        /* not needed in cache any more */
        set_bit(LU_OBJECT_HEARD_BANSHEE, &dt->do_lu.lo_header->loh_flags);

        RETURN(rc);
}

struct dt_object_operations osp_obj_ops = {
        .do_declare_attr_set  = osp_declare_attr_set,
        .do_attr_set          = osp_attr_set,
        .do_declare_create    = osp_declare_object_create,
        .do_create            = osp_object_create,
        .do_declare_destroy   = osp_declare_object_destroy,
        .do_destroy           = osp_object_destroy,
};

static int osp_object_init(const struct lu_env *env, struct lu_object *o,
                            const struct lu_object_conf *unused)
{
        struct osp_object *po = lu2osp_obj(o);

        po->opo_obj.do_ops = &osp_obj_ops;

        return 0;
}

static void osp_object_free(const struct lu_env *env, struct lu_object *o)
{
        struct osp_object *obj = lu2osp_obj(o);

        dt_object_fini(&obj->opo_obj);

        OBD_FREE_PTR(obj);
}

static void osp_object_release(const struct lu_env *env, struct lu_object *o)
{
        struct osp_object *po = lu2osp_obj(o);
        struct osp_device *d  = lu2osp_dev(o->lo_dev);

        /*
         * release reservation if object was declared but not created
         * this may require lu_object_put() in LOD
         */
        if (unlikely(po->opo_reserved)) {
                LASSERT(d->opd_pre_reserved > 0);
                cfs_spin_lock(&d->opd_pre_lock);
                d->opd_pre_reserved--;
                cfs_spin_unlock(&d->opd_pre_lock);

                /* not needed in cache any more */
                set_bit(LU_OBJECT_HEARD_BANSHEE, &o->lo_header->loh_flags);
        }
}

static int osp_object_print(const struct lu_env *env, void *cookie,
                             lu_printer_t p, const struct lu_object *l)
{
        const struct osp_object *o = lu2osp_obj((struct lu_object *) l);

        return (*p)(env, cookie, LUSTRE_OSP_NAME"-object@%p", o);
}

static int osp_object_invariant(const struct lu_object *o)
{
        LBUG();
}


struct lu_object_operations osp_lu_obj_ops = {
        .loo_object_init      = osp_object_init,
        .loo_object_free      = osp_object_free,
        .loo_object_release   = osp_object_release,
        .loo_object_print     = osp_object_print,
        .loo_object_invariant = osp_object_invariant
};

