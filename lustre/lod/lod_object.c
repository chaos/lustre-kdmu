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
 * lustre/lod/lod_object.c
 *
 * Author: Alex Zhuravlev <bzzz@sun.com>
 */

#ifndef EXPORT_SYMTAB
# define EXPORT_SYMTAB
#endif
#define DEBUG_SUBSYSTEM S_MDS

#include <obd.h>
#include <obd_class.h>
#include <lustre_ver.h>
#include <obd_support.h>
#include <lprocfs_status.h>

#include <lustre_disk.h>
#include <lustre_fid.h>
#include <lustre/lustre_idl.h>
#include <lustre_param.h>
#include <lustre_fid.h>
#include <obd_lov.h>

#include "lod_internal.h"

static const struct dt_body_operations lod_body_lnk_ops;

static int lod_index_lookup(const struct lu_env *env, struct dt_object *dt,
                            struct dt_rec *rec, const struct dt_key *key,
                            struct lustre_capa *capa)
{
        struct dt_object   *next = dt_object_child(dt);
        int                 rc;
        ENTRY;

        rc = next->do_index_ops->dio_lookup(env, next, rec, key, capa);

        RETURN(rc);
}

static int lod_declare_index_insert(const struct lu_env *env,
                                    struct dt_object *dt,
                                    const struct dt_rec *rec,
                                    const struct dt_key *key,
                                    struct thandle *handle)
{
        struct dt_object   *next = dt_object_child(dt);
        int                 rc;
        ENTRY;

        rc = next->do_index_ops->dio_declare_insert(env, next, rec, key, handle);

        RETURN(rc);
}

static int lod_index_insert(const struct lu_env *env,
                            struct dt_object *dt,
                            const struct dt_rec *rec,
                            const struct dt_key *key,
                            struct thandle *th,
                            struct lustre_capa *capa,
                            int ign)
{
        struct dt_object   *next = dt_object_child(dt);
        int                 rc;
        ENTRY;

        rc = next->do_index_ops->dio_insert(env, next, rec, key, th, capa, ign);

        RETURN(rc);
}

static int lod_declare_index_delete(const struct lu_env *env,
                                    struct dt_object *dt,
                                    const struct dt_key *key,
                                    struct thandle *th)
{
        struct dt_object   *next = dt_object_child(dt);
        int                 rc;
        ENTRY;

        rc = next->do_index_ops->dio_declare_delete(env, next, key, th);

        RETURN(rc);
}

static int lod_index_delete(const struct lu_env *env,
                            struct dt_object *dt,
                            const struct dt_key *key,
                            struct thandle *th,
                            struct lustre_capa *capa)
{
        struct dt_object   *next = dt_object_child(dt);
        int                 rc;
        ENTRY;

        rc = next->do_index_ops->dio_delete(env, next, key, th, capa);

        RETURN(rc);
}

static struct dt_it *lod_it_init(const struct lu_env *env,
                                 struct dt_object *dt,
                                 struct lustre_capa *capa)
{
        struct dt_object   *next = dt_object_child(dt);
        struct dt_it       *it;
        ENTRY;

        it = next->do_index_ops->dio_it.init(env, next, capa);

        RETURN(it);
}

static struct dt_index_operations lod_index_ops = {
        .dio_lookup         = lod_index_lookup,
        .dio_declare_insert = lod_declare_index_insert,
        .dio_insert         = lod_index_insert,
        .dio_declare_delete = lod_declare_index_delete,
        .dio_delete         = lod_index_delete,
        .dio_it     = {
                .init       = lod_it_init,
        }
};

static void lod_object_read_lock(const struct lu_env *env,
                                 struct dt_object *dt, unsigned role)
{
        struct dt_object *next = dt_object_child(dt);
        ENTRY;
        next->do_ops->do_read_lock(env, next, role);
        EXIT;
}

static void lod_object_write_lock(const struct lu_env *env,
                                  struct dt_object *dt, unsigned role)
{
        struct dt_object *next = dt_object_child(dt);
        ENTRY;
        next->do_ops->do_write_lock(env, next, role);
        EXIT;
}

static void lod_object_read_unlock(const struct lu_env *env,
                                   struct dt_object *dt)
{
        struct dt_object *next = dt_object_child(dt);
        ENTRY;
        next->do_ops->do_read_unlock(env, next);
        EXIT;
}

static void lod_object_write_unlock(const struct lu_env *env,
                                    struct dt_object *dt)
{
        struct dt_object *next = dt_object_child(dt);
        ENTRY;
        next->do_ops->do_write_unlock(env, next);
        EXIT;
}

static int lod_object_write_locked(const struct lu_env *env,
                                   struct dt_object *dt)
{
        struct dt_object *next = dt_object_child(dt);
        int               rc;
        ENTRY;
        rc = next->do_ops->do_write_locked(env, next);
        RETURN(rc);
}

static int lod_attr_get(const struct lu_env *env,
                        struct dt_object *dt,
                        struct lu_attr *attr,
                        struct lustre_capa *capa)
{
        struct dt_object   *next = dt_object_child(dt);
        int                 rc;
        ENTRY;

        rc = next->do_ops->do_attr_get(env, next, attr, capa);

        RETURN(rc);
}

static int lod_declare_attr_set(const struct lu_env *env,
                                struct dt_object *dt,
                                const struct lu_attr *attr,
                                struct thandle *handle)
{
        struct dt_object  *next = dt_object_child(dt);
        struct lod_object *mo = lod_dt_obj(dt);
        int                rc, i;
        ENTRY;

        /*
         * declare setattr on the local object
         */
        rc = next->do_ops->do_declare_attr_set(env, next, attr, handle);
        if (rc)
                RETURN(rc);

        /*
         * load striping information, notice we don't do this when object
         * is being initialized as we don't need this information till
         * few specific cases like destroy, chown
         */
        rc = lod_load_striping(env, mo);
        if (rc)
                RETURN(rc);

        /*
         * if object is striped declare changes on the stripes
         */
        LASSERT(mo->mbo_stripe || mo->mbo_stripenr == 0);
        for (i = 0; i < mo->mbo_stripenr; i++) {
                LASSERT(mo->mbo_stripe[i]);
                rc = dt_declare_attr_set(env, mo->mbo_stripe[i], attr, handle);
                if (rc) {
                        CERROR("failed declaration: %d\n", rc);
                        break;
                }
        }

        RETURN(rc);
}

static int lod_attr_set(const struct lu_env *env,
                        struct dt_object *dt,
                        const struct lu_attr *attr,
                        struct thandle *handle,
                        struct lustre_capa *capa)
{
        struct dt_object   *next = dt_object_child(dt);
        struct lod_object *mo = lod_dt_obj(dt);
        int                 rc, i;
        ENTRY;

        /* 
         * apply changes to the local object
         */
        rc = next->do_ops->do_attr_set(env, next, attr, handle, capa);
        if (rc)
                RETURN(rc);

        /*
         * if object is striped, apply changes to all the stripes
         */
        LASSERT(mo->mbo_stripe || mo->mbo_stripenr == 0);
        for (i = 0; i < mo->mbo_stripenr; i++) {
                LASSERT(mo->mbo_stripe[i]);
                rc = dt_attr_set(env, mo->mbo_stripe[i], attr, handle, capa);
                if (rc) {
                        CERROR("failed declaration: %d\n", rc);
                        break;
                }
        }

        RETURN(rc);
}

static int lod_declare_punch(const struct lu_env *env, struct dt_object *dt,
                             __u64 from, __u64 to, struct thandle *th)
{
        struct dt_object   *next = dt_object_child(dt);
        int                 rc;
        ENTRY;

        rc = next->do_ops->do_declare_punch(env, next, from, to, th);

        RETURN(rc);
}

static int lod_punch(const struct lu_env *env, struct dt_object *dt,
                     __u64 from, __u64 to, struct thandle *th,
                     struct lustre_capa *capa)
{
        struct dt_object   *next = dt_object_child(dt);
        int                 rc;
        ENTRY;

        rc = next->do_ops->do_punch(env, next, from, to, th, capa);

        RETURN(rc);
}

static int lod_xattr_get(const struct lu_env *env, struct dt_object *dt,
                         struct lu_buf *buf, const char *name,
                         struct lustre_capa *capa)
{
        struct dt_object *next = dt_object_child(dt);
        int               rc, dir;
        ENTRY;

        rc = next->do_ops->do_xattr_get(env, next, buf, name, capa);

        /*
         * if this is a directory with no own default striping,
         * supply the caller with filesystem-wide default striping
         */
        dir = S_ISDIR(dt->do_lu.lo_header->loh_attr & S_IFMT);

        if (rc == -ENODATA && dir && !strcmp(XATTR_NAME_LOV, name)) {
                struct lov_mds_md *lmm = buf->lb_buf;
                struct lod_device *d;
                struct lov_desc   *desc;

                d = lu2lod_dev(dt->do_lu.lo_dev);
                desc = &d->lod_obd->u.lov.desc; 
                rc = sizeof(struct lov_mds_md);
                if (buf->lb_len >= sizeof(struct lov_mds_md)) {
                        lmm->lmm_magic = LOV_MAGIC_V1;
                        lmm->lmm_object_gr = LOV_OBJECT_GROUP_DEFAULT;
                        lmm->lmm_pattern = desc->ld_pattern;
                        lmm->lmm_stripe_size = desc->ld_default_stripe_size;
                        lmm->lmm_stripe_count = desc->ld_default_stripe_count;
                }
        }

        RETURN(rc);
}

static int lod_declare_xattr_set(const struct lu_env *env,
                                 struct dt_object *dt,
                                 const int buflen, const char *name, int fl,
                                 struct thandle *th)
{
        struct dt_object   *next = dt_object_child(dt);
        int                 rc;
        ENTRY;

        rc = next->do_ops->do_declare_xattr_set(env, next, buflen, name, fl, th);

        RETURN(rc);
}

static int lod_xattr_set(const struct lu_env *env,
                         struct dt_object *dt, const struct lu_buf *buf,
                         const char *name, int fl, struct thandle *th,
                         struct lustre_capa *capa)
{
        struct dt_object   *next = dt_object_child(dt);
        struct lod_object  *l = lod_dt_obj(dt);
        int                 rc;
        ENTRY;

        /*
         * XXX: if default per-directory striping is setting,
         * shouldn't we make sure it's sane?
         */
        if (S_ISDIR(dt->do_lu.lo_header->loh_attr & S_IFMT)) {
                /*
                 * some xattr is changing. that might be lovea storing
                 * default striping for files in this directory
                 */
                LASSERT(l->mbo_stripe == NULL);
                l->mbo_striping_cached = 0;
                l->mbo_def_stripe_size = 0;
                l->mbo_def_stripenr = 0;
        }

        rc = next->do_ops->do_xattr_set(env, next, buf, name, fl, th, capa);

        RETURN(rc);
}

static int lod_declare_xattr_del(const struct lu_env *env,
                                 struct dt_object *dt,
                                 const char *name,
                                 struct thandle *th)
{
        struct dt_object   *next = dt_object_child(dt);
        int                 rc;
        ENTRY;

        rc = next->do_ops->do_declare_xattr_del(env, next, name, th);

        RETURN(rc);
}

static int lod_xattr_del(const struct lu_env *env,
                         struct dt_object *dt,
                         const char *name, struct thandle *th,
                         struct lustre_capa *capa)
{
        struct dt_object   *next = dt_object_child(dt);
        int                 rc;
        ENTRY;

        rc = next->do_ops->do_xattr_del(env, next, name, th, capa);

        RETURN(rc);
}

static int lod_xattr_list(const struct lu_env *env,
                          struct dt_object *dt, struct lu_buf *buf,
                          struct lustre_capa *capa)
{
        struct dt_object   *next = dt_object_child(dt);
        int                 rc;
        ENTRY;

        rc = next->do_ops->do_xattr_list(env, next, buf, capa);

        RETURN(rc);
}

static inline int lod_object_will_be_striped(int is_reg, const struct lu_fid *fid)
{
        return (is_reg && fid_seq(fid) != FID_SEQ_LOCAL_FILE);
}

/**
 * used to transfer default striping data to the object being created
 */
static void lod_ah_init(const struct lu_env *env,
                        struct dt_allocation_hint *ah,
                        struct dt_object *parent,
                        struct dt_object *child,
                        cfs_umode_t child_mode)
{
        struct lod_device *d = lu2lod_dev(parent->do_lu.lo_dev);
        struct dt_object  *nextp = dt_object_child(parent);
        struct dt_object  *nextc = dt_object_child(parent);
        struct lod_object *lp = lod_dt_obj(parent);
        struct lod_object *lc = lod_dt_obj(child);
        struct lov_mds_md *lmm;
        struct lov_desc   *desc;
        int                rc;
        ENTRY;

        LASSERT(lod_mti_get(env));
        LASSERT(parent);
        LASSERT(child);

        nextp = dt_object_child(parent);
        nextc = dt_object_child(parent);
        lp = lod_dt_obj(parent);
        lc = lod_dt_obj(child);
        d = lu2lod_dev(parent->do_lu.lo_dev);

        LASSERT(lc->mbo_stripenr == 0);

        /*
         * local object may want some hints
         */
        nextp->do_ops->do_ah_init(env, ah, nextp, nextc, child_mode);

        /*
         * if object is going to be striped over OSTs, transfer default
         * striping information to the child, so that we can use it
         * during declaration and creation
         */
        if (!lod_object_will_be_striped(S_ISREG(child_mode),
                                        lu_object_fid(&child->do_lu)))
                return;

        /*
         * try from the parent
         */
        if (lp->mbo_striping_cached == 0) {
                /* we haven't tried to get default striping for
                 * the directory yet, let's cache it in the object */
                rc = lod_get_lov_ea(env, lod_dt_obj(parent));
                lmm = (struct lov_mds_md *) lod_mti_get(env)->lti_ea_store;
                if (rc >= sizeof(struct lov_mds_md)) {
                        LASSERT(le32_to_cpu(lmm->lmm_magic) == LOV_MAGIC_V3 ||
                                le32_to_cpu(lmm->lmm_magic) == LOV_MAGIC_V1);
                        LASSERT(le32_to_cpu(lmm->lmm_pattern) == LOV_PATTERN_RAID0
                                || lmm->lmm_pattern == 0);
                        lp->mbo_def_stripenr = le32_to_cpu(lmm->lmm_stripe_count);
                        lp->mbo_def_stripe_size = le32_to_cpu(lmm->lmm_stripe_size);
                        lp->mbo_striping_cached = 1;
                } else if (rc == 0) {
                        /* don't lookup for non-existing striping */
                        lp->mbo_striping_cached = 1;
                        lp->mbo_def_stripe_size = 0;
                        lp->mbo_def_stripenr = 0;
                }
        }
        if (lp->mbo_def_stripe_size) {
                lc->mbo_stripenr = lp->mbo_def_stripenr;
                if (lc->mbo_stripenr == (__u32) -1)
                        lc->mbo_stripenr = d->lod_ostnr;
                lc->mbo_stripe_size = lp->mbo_def_stripe_size;
                return;
        }

        /*
         * the parent doesn't provide with specific pattern, grab fs-wide one
         */
        desc = &d->lod_obd->u.lov.desc;

        lc->mbo_stripenr = desc->ld_default_stripe_count;
        if (lc->mbo_stripenr == (__u32) -1)
                lc->mbo_stripenr = d->lod_ostnr;
        lc->mbo_stripe_size = desc->ld_default_stripe_size;

        EXIT;
}

/**
 * Create declaration of striped object
 */
static int lod_declare_striped_object(const struct lu_env *env,
                                      struct dt_object *dt,
                                      struct lu_attr *attr,
                                      struct dt_allocation_hint *hint,
                                      struct dt_object_format *dof,
                                      struct thandle *th)
{
        struct dt_object   *next = dt_object_child(dt);
        struct lod_object  *mo = lod_dt_obj(dt);
        struct lod_device  *md = lu2lod_dev(mo->mbo_obj.do_lu.lo_dev);
        int                 lmm_size;
        int                 rc, i;
        ENTRY;

        LASSERT(dof->dof_type == DFT_REGULAR);
        LASSERT(!dt_object_exists(&mo->mbo_obj));
        LASSERT(mo->mbo_stripenr > 0);
        LASSERT(mo->mbo_stripenr <= md->lod_ostnr);

        /* no OST available */
        /* XXX: should we be waiting a bit to prevent failures during
         * cluster initialization? */
        if (md->lod_ostnr == 0)
                GOTO(out, rc = -EIO);

        i = sizeof(struct dt_object *) * mo->mbo_stripenr;
        OBD_ALLOC(mo->mbo_stripe, i);
        if (mo->mbo_stripe == NULL)
                GOTO(out, rc = -ENOMEM);
        mo->mbo_stripes_allocated = mo->mbo_stripenr;

        /* choose OST and generate appropriate objects */
        rc = lod_qos_prep_create(env, mo, attr, th);
        if (rc)
                GOTO(out, rc);

        /*
         * declare storage for striping data
         */
        lmm_size = lov_mds_md_size(mo->mbo_stripenr, LOV_MAGIC_V3);
        rc = dt_declare_xattr_set(env, next, lmm_size, XATTR_NAME_LOV, 0, th);

out:
        RETURN(rc);
}

static int lod_declare_object_create(const struct lu_env *env,
                                     struct dt_object *dt,
                                     struct lu_attr *attr,
                                     struct dt_allocation_hint *hint,
                                     struct dt_object_format *dof,
                                     struct thandle *th)
{
        struct dt_object    *next = dt_object_child(dt);
        const struct lu_fid *fid = lu_object_fid(&dt->do_lu);
        int                  rc;
        ENTRY;

        LASSERT(dof);
        LASSERT(attr);
        LASSERT(th);

        /*
         * first of all, we declare creation of local object
         */
        rc = dt_declare_create(env, next, attr, hint, dof, th);
        if (rc)
                GOTO(out, rc);

        if (dof->dof_type == DFT_SYM)
                dt->do_body_ops = &lod_body_lnk_ops;
        /*
         * then decide whether this object will be local or striped:
         * 1) directories and special files are local always
         * 2) special (not visible to users) files are local
         */
         
        if (lod_object_will_be_striped(dof->dof_type == DFT_REGULAR, fid))
                rc = lod_declare_striped_object(env, dt, attr, hint, dof, th);

out:
        RETURN(rc);
}

static int lod_object_create(const struct lu_env *env,
                             struct dt_object *dt,
                             struct lu_attr *attr,
                             struct dt_allocation_hint *hint,
                             struct dt_object_format *dof,
                             struct thandle *th)
{
        struct dt_object   *next = dt_object_child(dt);
        struct lod_object  *mo = lod_dt_obj(dt);
        int                 rc, i;
        ENTRY;

        /* create local object */
        rc = dt_create(env, next, attr, hint, dof, th);

        if (mo->mbo_stripe) {
                LASSERT(mo->mbo_stripe > 0);
                LASSERT(mo->mbo_striping_cached == 0);

                /* create all underlying objects */
                for (i = 0; i < mo->mbo_stripenr; i++) {
                        LASSERT(mo->mbo_stripe[i]);
                        rc = dt_create(env, mo->mbo_stripe[i], attr, NULL, dof, th);

                        /* XXX: can't proceed with this OST? */
                        LASSERTF(rc == 0, "can't declare creation: %d\n", rc);
                }
                rc = lod_generate_and_set_lovea(env, mo, th);
        }

        RETURN(rc);
}

static int lod_declare_object_destroy(const struct lu_env *env,
                                       struct dt_object *dt,
                                       struct thandle *th)
{
        struct dt_object   *next = dt_object_child(dt);
        struct lod_object  *mo = lod_dt_obj(dt);
        int                 rc, i;
        ENTRY;

        /*
         * we declare destroy for the local object
         */
        rc = dt_declare_destroy(env, next, th);
        if (rc)
                RETURN(rc);

        /*
         * load striping information, notice we don't do this when object
         * is being initialized as we don't need this information till
         * few specific cases like destroy, chown
         */
        rc = lod_load_striping(env, mo);
        if (rc)
                RETURN(rc);

        /* declare destroy for all underlying objects */
        for (i = 0; i < mo->mbo_stripenr; i++) {
                LASSERT(mo->mbo_stripe[i]);
                rc = dt_declare_destroy(env, mo->mbo_stripe[i], th);

                /* XXX: can't proceed with this OST? */
                LASSERTF(rc == 0, "can't destroy: %d\n", rc);
        }

        RETURN(rc);
}

static int lod_object_destroy(const struct lu_env *env,
                               struct dt_object *dt,
                               struct thandle *th)
{
        struct dt_object   *next = dt_object_child(dt);
        struct lod_object *mo = lod_dt_obj(dt);
        int                 rc, i;
        ENTRY;

        /* destroy local object */
        rc = dt_destroy(env, next, th);
        if (rc)
                RETURN(rc);

        /* destroy all underlying objects */
        for (i = 0; i < mo->mbo_stripenr; i++) {
                LASSERT(mo->mbo_stripe[i]);
                rc = dt_destroy(env, mo->mbo_stripe[i], th);

                /* XXX: can't proceed with this OST? */
                LASSERTF(rc == 0, "can't destroy: %d\n", rc);
        }

        RETURN(rc);
}

static int lod_index_try(const struct lu_env *env, struct dt_object *dt,
                         const struct dt_index_features *feat)
{
        struct dt_object   *next = dt_object_child(dt);
        int                 rc;
        ENTRY;

        LASSERT(next->do_ops);
        LASSERT(next->do_ops->do_index_try);

        rc = next->do_ops->do_index_try(env, next, feat);
        if (next->do_index_ops && dt->do_index_ops == NULL) {
                dt->do_index_ops = &lod_index_ops;
                /* XXX: iterators don't accept device, so bypass LOD */
                if (lod_index_ops.dio_it.fini == NULL) {
                        lod_index_ops.dio_it = next->do_index_ops->dio_it;
                        lod_index_ops.dio_it.init = lod_it_init;
                }
        }

        RETURN(rc);
}

static int lod_declare_ref_add(const struct lu_env *env,
                               struct dt_object *dt, struct thandle *th)
{
        struct dt_object   *next = dt_object_child(dt);
        int                 rc;
        ENTRY;

        rc = next->do_ops->do_declare_ref_add(env, next, th);

        RETURN(rc);
}

static void lod_ref_add(const struct lu_env *env,
                        struct dt_object *dt, struct thandle *th)
{
        struct dt_object   *next = dt_object_child(dt);
        ENTRY;

        next->do_ops->do_ref_add(env, next, th);

        EXIT;
}

static int lod_declare_ref_del(const struct lu_env *env,
                               struct dt_object *dt, struct thandle *th)
{
        struct dt_object   *next = dt_object_child(dt);
        int                 rc;
        ENTRY;

        rc = next->do_ops->do_declare_ref_del(env, next, th);

        RETURN(rc);
}

static void lod_ref_del(const struct lu_env *env,
                        struct dt_object *dt, struct thandle *th)
{
        struct dt_object   *next = dt_object_child(dt);
        ENTRY;

        next->do_ops->do_ref_del(env, next, th);

        EXIT;
}

static struct obd_capa *lod_capa_get(const struct lu_env *env,
                                     struct dt_object *dt,
                                     struct lustre_capa *old,
                                     __u64 opc)
{
        struct dt_object   *next = dt_object_child(dt);
        struct obd_capa    *capa;
        ENTRY;

        capa = next->do_ops->do_capa_get(env, next,old, opc);

        RETURN(capa);
}

static int lod_object_sync(const struct lu_env *env, struct dt_object *dt)
{
        struct dt_object   *next = dt_object_child(dt);
        int                 rc;
        ENTRY;

        rc = next->do_ops->do_object_sync(env, next);

        RETURN(rc);
}

static dt_obj_version_t lod_object_version_get(const struct lu_env *env,
                                               struct dt_object *dt)
{
        struct dt_object   *next = dt_object_child(dt);
        dt_obj_version_t    ver;
        ENTRY;

        ver = next->do_ops->do_version_get(env, next);

        RETURN(ver);
}

static void lod_object_version_set(const struct lu_env *env,
                                   struct dt_object *dt,
                                   dt_obj_version_t new_version)
{
        struct dt_object   *next = dt_object_child(dt);
        ENTRY;

        next->do_ops->do_version_set(env, next, new_version);

        EXIT;
}

static int lod_data_get(const struct lu_env *env, struct dt_object *dt,
                        void **data)
{
        struct dt_object   *next = dt_object_child(dt);
        int                 rc;
        ENTRY;

        rc = next->do_ops->do_data_get(env, next, data);

        RETURN(rc);
}


struct dt_object_operations lod_obj_ops = {
        .do_read_lock         = lod_object_read_lock,
        .do_write_lock        = lod_object_write_lock,
        .do_read_unlock       = lod_object_read_unlock,
        .do_write_unlock      = lod_object_write_unlock,
        .do_write_locked      = lod_object_write_locked,
        .do_attr_get          = lod_attr_get,
        .do_declare_attr_set  = lod_declare_attr_set,
        .do_attr_set          = lod_attr_set,
        .do_declare_punch     = lod_declare_punch,
        .do_punch             = lod_punch,
        .do_xattr_get         = lod_xattr_get,
        .do_declare_xattr_set = lod_declare_xattr_set,
        .do_xattr_set         = lod_xattr_set,
        .do_declare_xattr_del = lod_declare_xattr_del,
        .do_xattr_del         = lod_xattr_del,
        .do_xattr_list        = lod_xattr_list,
        .do_ah_init           = lod_ah_init,
        .do_declare_create    = lod_declare_object_create,
        .do_create            = lod_object_create,
        .do_declare_destroy   = lod_declare_object_destroy,
        .do_destroy           = lod_object_destroy,
        .do_index_try         = lod_index_try,
        .do_declare_ref_add   = lod_declare_ref_add,
        .do_ref_add           = lod_ref_add,
        .do_declare_ref_del   = lod_declare_ref_del,
        .do_ref_del           = lod_ref_del,
        .do_capa_get          = lod_capa_get,
        .do_object_sync       = lod_object_sync,
        .do_version_get       = lod_object_version_get,
        .do_version_set       = lod_object_version_set,
        .do_data_get          = lod_data_get,
};

static ssize_t lod_read(const struct lu_env *env, struct dt_object *dt,
                        struct lu_buf *buf, loff_t *pos,
                        struct lustre_capa *capa)
{
        struct dt_object   *next = dt_object_child(dt);
        ssize_t             rc;
        ENTRY;

        rc = next->do_body_ops->dbo_read(env, next, buf, pos, capa);

        RETURN(rc);
}

static ssize_t lod_declare_write(const struct lu_env *env, struct dt_object *dt,
                                 const loff_t size, loff_t pos,
                                 struct thandle *th)
{
        struct dt_object   *next = dt_object_child(dt);
        ssize_t             rc;
        ENTRY;

        rc = next->do_body_ops->dbo_declare_write(env, next, size, pos, th);

        RETURN(rc);
}

static ssize_t lod_write(const struct lu_env *env, struct dt_object *dt,
                         const struct lu_buf *buf, loff_t *pos,
                         struct thandle *th, struct lustre_capa *capa, int iq)
{
        struct dt_object   *next = dt_object_child(dt);
        ssize_t             rc;
        ENTRY;

        rc = next->do_body_ops->dbo_write(env, next, buf, pos, th, capa, iq);

        RETURN(rc);
}

static const struct dt_body_operations lod_body_lnk_ops = {
        .dbo_read             = lod_read,
        .dbo_declare_write    = lod_declare_write,
        .dbo_write            = lod_write
};

static int lod_object_init(const struct lu_env *env, struct lu_object *o,
                           const struct lu_object_conf *conf)
{
        struct lod_device *d = lu2lod_dev(o->lo_dev);
        struct lu_object  *below;
        struct lu_device  *under;
        ENTRY;

        /*
         * create local object
         */
        under = &d->lod_child->dd_lu_dev;
        below = under->ld_ops->ldo_object_alloc(env, o->lo_header, under);
        if (below == NULL)
                RETURN(-ENOMEM);

        lu_object_add(o, below);

        RETURN(0);
}

/*
 * ->start is called once all slices are initialized, including header's
 * cache for mode (object type). using the type we can initialize ops
 */
static int lod_object_start(const struct lu_env *env, struct lu_object *o)
{
        if (S_ISLNK(o->lo_header->loh_attr & S_IFMT))
                lu2lod_obj(o)->mbo_obj.do_body_ops = &lod_body_lnk_ops;
        return 0;
}

static void lod_object_free(const struct lu_env *env, struct lu_object *o)
{
        struct lod_object *mo = lu2lod_obj(o);
        int                i;
        ENTRY;

        /*
         * release all underlying object pinned
         */

        for (i = 0; i < mo->mbo_stripenr; i++) {
                if (mo->mbo_stripe[i])
                        lu_object_put(env, &mo->mbo_stripe[i]->do_lu);
        }

        if (mo->mbo_stripe) {
                LASSERT(mo->mbo_stripenr > 0);
                i = sizeof(struct dt_object *) * mo->mbo_stripes_allocated;
                OBD_FREE(mo->mbo_stripe, i);
                mo->mbo_stripe = NULL;
                mo->mbo_stripenr = 0;
        }

        lu_object_fini(o);
        OBD_FREE_PTR(mo);

        EXIT;
}

static void lod_object_release(const struct lu_env *env, struct lu_object *o)
{
        /* XXX: shouldn't we release everything here in case if object
         * creation failed before? */
}

static int lod_object_print(const struct lu_env *env, void *cookie,
                             lu_printer_t p, const struct lu_object *l)
{
        struct lod_object *o = lu2lod_obj((struct lu_object *) l);

        return (*p)(env, cookie, LUSTRE_LOD_NAME"-object@%p", o);
}


struct lu_object_operations lod_lu_obj_ops = {
        .loo_object_init      = lod_object_init,
        .loo_object_start     = lod_object_start,
        .loo_object_free      = lod_object_free,
        .loo_object_release   = lod_object_release,
        .loo_object_print     = lod_object_print,
};

