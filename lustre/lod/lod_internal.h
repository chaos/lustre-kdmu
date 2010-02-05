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
 * lustre/lod/lod_internal.h
 *
 * Author: Alex Zhuravlev <bzzz@sun.com>
 */

#ifndef _LOD_INTERNAL_H
#define _LOD_INTERNAL_H


#include <obd.h>
#include <dt_object.h>

#define LOV_USES_ASSIGNED_STRIPE        0
#define LOV_USES_DEFAULT_STRIPE         1


#define LOD_MAX_OSTNR                  1024  /* XXX: should be dynamic */
#define LOD_BITMAP_SIZE                (LOD_MAX_OSTNR / sizeof(unsigned long) + 1)

struct lod_device {
        struct dt_device                 mbd_dt_dev;
        struct obd_export               *mbd_child_exp;
        struct dt_device                *mbd_child;
        struct obd_device               *mbd_obd;
        struct dt_txn_callback           mbd_txn_cb;
        cfs_proc_dir_entry_t            *mbd_proc_entry;
        struct lprocfs_stats            *mbd_stats;
        int                              lod_connects;

        /* list of known OSTs */
        struct dt_device                *mbd_ost[LOD_MAX_OSTNR];
        struct obd_export               *mbd_ost_exp[LOD_MAX_OSTNR];

        /* bitmap of OSTs available */
        unsigned long                    mbd_ost_bitmap[LOD_BITMAP_SIZE];

        /* number of known OSTs */
        int                              mbd_ostnr;
        struct semaphore                 mbd_mutex;
};

struct lod_object {
        struct dt_object   mbo_obj;

        /* if object is striped, then the next fields describe stripes */
        int                mbo_stripenr;
        struct dt_object **mbo_stripe;
        /* to know how much memory to free, mbo_stripenr can be less */
        int                mbo_stripes_allocated;
};

struct lod_thread_info {
        /* array of OSTs selected in striping creation */
        int     ost_arr[LOD_MAX_OSTNR];    /* XXX: should be dynamic */
        char   *lti_ea_store;              /* a buffer for lov ea */
        int     lti_ea_store_size;
};

extern const struct lu_device_operations lod_lu_ops;

static inline int lu_device_is_lod(struct lu_device *d)
{
        return ergo(d != NULL && d->ld_ops != NULL, d->ld_ops == &lod_lu_ops);
}

static inline struct lod_device* lu2lod_dev(struct lu_device *d)
{
        LASSERT(lu_device_is_lod(d));
        return container_of0(d, struct lod_device, mbd_dt_dev.dd_lu_dev);
}

static inline struct lu_device *lod2lu_dev(struct lod_device *d)
{
        return (&d->mbd_dt_dev.dd_lu_dev);
}

static inline struct lod_device *dt2lod_dev(struct dt_device *d)
{
        LASSERT(lu_device_is_lod(&d->dd_lu_dev));
        return container_of0(d, struct lod_device, mbd_dt_dev);
}

static inline struct lod_object *lu2lod_obj(struct lu_object *o)
{
        LASSERT(ergo(o != NULL, lu_device_is_lod(o->lo_dev)));
        return container_of0(o, struct lod_object, mbo_obj.do_lu);
}

static inline struct lu_object *lod2lu_obj(struct lod_object *obj)
{
        return &obj->mbo_obj.do_lu;
}

static inline struct lod_object *lod_obj(const struct lu_object *o)
{
        LASSERT(lu_device_is_lod(o->lo_dev));
        return container_of0(o, struct lod_object, mbo_obj.do_lu);
}

static inline struct lod_object *lod_dt_obj(const struct dt_object *d)
{
        return lod_obj(&d->do_lu);
}

static inline struct dt_object* lod_object_child(struct lod_object *o)
{
        return container_of0(lu_object_next(lod2lu_obj(o)),
                             struct dt_object, do_lu);
}

static inline struct dt_object *lu2dt_obj(struct lu_object *o)
{
        LASSERT(ergo(o != NULL, lu_device_is_dt(o->lo_dev)));
        return container_of0(o, struct dt_object, do_lu);
}

static inline struct dt_object *dt_object_child(struct dt_object *o)
{
        return container_of0(lu_object_next(&(o)->do_lu),
                             struct dt_object, do_lu);
}

extern struct lu_context_key lod_thread_key;

static inline struct lod_thread_info *lod_mti_get(const struct lu_env *env)
{
        return lu_context_key_get(&env->le_ctx, &lod_thread_key);
}



/* lod_lov.c */
int lod_lov_add_device(const struct lu_env *env, struct lod_device *m,
                        char *osp, unsigned index, unsigned gen);
int lod_lov_del_device(const struct lu_env *env, struct lod_device *m,
                        char *osp, unsigned gen);
int lod_generate_and_set_lovea(const struct lu_env *env,
                                struct lod_object *mo,
                                struct thandle *th);
int lod_load_striping(const struct lu_env *env, struct lod_object *mo);
int lod_lov_init(struct lod_device *m, struct lustre_cfg *cfg);
int lod_lov_fini(struct lod_device *m);

/* lod_pool.c */
int lov_ost_pool_add(struct ost_pool *op, __u32 idx, unsigned int min_count);
int lov_ost_pool_extend(struct ost_pool *op, unsigned int min_count);
struct pool_desc *lov_find_pool(struct lov_obd *lov, char *poolname);
void lov_pool_putref(struct pool_desc *pool);
int lov_ost_pool_free(struct ost_pool *op);
int lov_pool_del(struct obd_device *obd, char *poolname);

/* lod_qos.c */
int lod_qos_prep_create(const struct lu_env *env, struct lod_object *lo,
                        struct lu_attr *attr, struct thandle *th);
int qos_add_tgt(struct obd_device *obd, int index);
int qos_del_tgt(struct obd_device *obd, int index);


#endif

