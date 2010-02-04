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
 * lustre/lod/lod_lov.c
 *
 * Lustre Multi-oBject Device
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
#include <obd_lov.h>

#include "lod_internal.h"

/* IDIF stuff */
#include <lustre_fid.h>


/* XXX: make the following definitions global */
static inline void lu_idif_build(struct lu_fid *fid, obd_id id, obd_gr gr)
{
        LASSERT((id >> 48) == 0);
        fid->f_seq = (IDIF_SEQ_START| id >> 32);
        fid->f_oid = (__u32)(id & 0xffffffff);
        fid->f_ver = gr;
}

static inline obd_id lu_idif_id(const struct lu_fid *fid)
{
        return ((fid->f_seq & 0xffff) << 32) | fid->f_oid;
}

static inline obd_gr lu_idif_gr(const struct lu_fid * fid)
{
        return fid->f_ver;
}

static int lov_add_target(struct lov_obd *lov, struct obd_device *tgt_obd,
                          __u32 index, int gen, int active)
{
        struct lov_tgt_desc *tgt;
        int                  rc;
        ENTRY;

        LASSERT(lov);
        LASSERT(tgt_obd);

        if (gen <= 0) {
                CERROR("request to add OBD %s with invalid generation: %d\n",
                       tgt_obd->obd_name, gen);
                RETURN(-EINVAL);
        }

        mutex_down(&lov->lov_lock);

        if ((index < lov->lov_tgt_size) && (lov->lov_tgts[index] != NULL)) {
                tgt = lov->lov_tgts[index];
                CERROR("UUID %s already assigned at LOV target index %d\n",
                       obd_uuid2str(&tgt->ltd_uuid), index);
                mutex_up(&lov->lov_lock);
                RETURN(-EEXIST);
        }

        if (index >= lov->lov_tgt_size) {
                /* We need to reallocate the lov target array. */
                struct lov_tgt_desc **newtgts, **old = NULL;
                __u32 newsize, oldsize = 0;

                newsize = max(lov->lov_tgt_size, (__u32)2);
                while (newsize < index + 1)
                        newsize = newsize << 1;
                OBD_ALLOC(newtgts, sizeof(*newtgts) * newsize);
                if (newtgts == NULL) {
                        mutex_up(&lov->lov_lock);
                        RETURN(-ENOMEM);
                }

                if (lov->lov_tgt_size) {
                        memcpy(newtgts, lov->lov_tgts, sizeof(*newtgts) *
                               lov->lov_tgt_size);
                        old = lov->lov_tgts;
                        oldsize = lov->lov_tgt_size;
                }

                lov->lov_tgts = newtgts;
                lov->lov_tgt_size = newsize;
#ifdef __KERNEL__
                smp_rmb();
#endif
                if (old)
                        OBD_FREE(old, sizeof(*old) * oldsize);

                CDEBUG(D_CONFIG, "tgts: %p size: %d\n",
                       lov->lov_tgts, lov->lov_tgt_size);
        }

        OBD_ALLOC_PTR(tgt);
        if (!tgt) {
                mutex_up(&lov->lov_lock);
                RETURN(-ENOMEM);
        }

        rc = lov_ost_pool_add(&lov->lov_packed, index, lov->lov_tgt_size);
        if (rc) {
                mutex_up(&lov->lov_lock);
                OBD_FREE_PTR(tgt);
                RETURN(rc);
        }

        memset(tgt, 0, sizeof(*tgt));
        tgt->ltd_uuid = tgt_obd->u.cli.cl_target_uuid;
        tgt->ltd_obd = tgt_obd;
        /* XXX - add a sanity check on the generation number. */
        tgt->ltd_gen = gen;
        tgt->ltd_index = index;
        /* XXX: how do we control active? */
        tgt->ltd_active = active;
        tgt->ltd_activate = active;
        lov->lov_tgts[index] = tgt;
        if (index >= lov->desc.ld_tgt_count)
                lov->desc.ld_tgt_count = index + 1;

        mutex_up(&lov->lov_lock);

        CDEBUG(D_CONFIG, "idx=%d ltd_gen=%d ld_tgt_count=%d\n",
                index, tgt->ltd_gen, lov->desc.ld_tgt_count);

        RETURN(0);
}

int lod_lov_add_device(const struct lu_env *env, struct lod_device *m,
                        char *osp, unsigned index, unsigned gen)
{
        struct obd_connect_data *data = NULL;
        struct obd_export       *exp = NULL;
        struct obd_device       *obd;
        struct dt_device        *d;
        int                     rc;
        ENTRY;

        CDEBUG(D_CONFIG, "osp:%s idx:%d gen:%d\n", osp, index, gen);

        /* XXX: should be dynamic */
        if (index >= LOD_MAX_OSTNR) {
                CERROR("too large index %d\n", index);
                RETURN(-EINVAL);
        }

        if (gen <= 0) {
                CERROR("request to add OBD %s with invalid generation: %d\n",
                       osp, gen);
                RETURN(-EINVAL);
        }

        obd = class_name2obd(osp);
        if (obd == NULL) {
                CERROR("can't find OSP\n");
                RETURN(-EINVAL);
        }

        OBD_ALLOC(data, sizeof(*data));
        if (data == NULL)
                RETURN(-ENOMEM);
        /* XXX: which flags we need on MDS? */
        data->ocd_version = LUSTRE_VERSION_CODE;

        rc = obd_connect(NULL, &exp, obd, &obd->obd_uuid, data, NULL);
        if (rc) {
                CERROR("cannot connect to next dev %s (%d)\n", osp, rc);
                GOTO(out_free, rc);
        }

        LASSERT(obd->obd_lu_dev);
        LASSERT(obd->obd_lu_dev->ld_site = m->mbd_dt_dev.dd_lu_dev.ld_site);

        d = lu2dt_dev(obd->obd_lu_dev);

        mutex_down(&m->mbd_mutex);

        rc = lov_add_target(&m->mbd_obd->u.lov, obd, index, gen, 1);
        if (rc) {
                CERROR("can't add target: %d\n", rc);
                GOTO(out, rc);
        }

        rc = qos_add_tgt(m->mbd_obd, index);
        if (rc) {
                CERROR("can't add: %d\n", rc);
                GOTO(out, rc);
        }

        if (m->mbd_ost[index] == NULL) {
                /* XXX: grab reference on the device */
                m->mbd_ost[index] = d;
                m->mbd_ost_exp[index] = exp;
                m->mbd_ostnr++;
                set_bit(index, m->mbd_ost_bitmap);
                rc = 0;
        } else {
                CERROR("device %d is registered already\n", index);
                GOTO(out, rc = -EINVAL);
        }

out:
        mutex_up(&m->mbd_mutex);

        if (rc) {
                /* XXX: obd_disconnect(), qos_del_tgt(), lov_del_target() */
        }

out_free:
        if (data)
                OBD_FREE(data, sizeof(*data));
        RETURN(rc);
}

/*
 * allocates a buffer
 * generate LOV EA for given striped object into the buffer
 *
 * XXX: V3 LOV EA is aupported only yet
 */
int lod_generate_and_set_lovea(const struct lu_env *env,
                                struct lod_object *mo,
                                struct thandle *th)
{
        struct dt_object     *next = dt_object_child(&mo->mbo_obj);
        const struct lu_fid  *fid  = lu_object_fid(&mo->mbo_obj.do_lu);
        struct lov_mds_md_v3 *lmm;
        struct lu_buf         buf;
        int                   i, rc;
        ENTRY;

        LASSERT(mo);
        LASSERT(mo->mbo_stripenr > 0);

        buf.lb_vmalloc = 0;
        buf.lb_len = lov_mds_md_size(mo->mbo_stripenr, LOV_MAGIC_V3);
        OBD_ALLOC(lmm, buf.lb_len);
        if (lmm == NULL)
                RETURN(-ENOMEM);
        buf.lb_buf = lmm;

        lmm->lmm_magic = cpu_to_le32(LOV_MAGIC_V3);
        lmm->lmm_pattern = cpu_to_le32(LOV_PATTERN_RAID0);
        lmm->lmm_object_id = cpu_to_le64(fid_flatten(fid)); /* XXX: what? */
        lmm->lmm_object_gr = cpu_to_le64(mdt_to_obd_objgrp(0)); /* XXX: what? */
        lmm->lmm_stripe_size = cpu_to_le32(1024 * 1024); /* XXX */
        lmm->lmm_stripe_count = cpu_to_le32(mo->mbo_stripenr);
        lmm->lmm_pool_name[0] = '\0';

        for (i = 0; i < mo->mbo_stripenr; i++) {
                const struct lu_fid *fid;

                LASSERT(mo->mbo_stripe[i]);
                fid = lu_object_fid(&mo->mbo_stripe[i]->do_lu);

                lmm->lmm_objects[i].l_object_id  = cpu_to_le64(lu_idif_id(fid));
                lmm->lmm_objects[i].l_object_gr  = cpu_to_le64(mdt_to_obd_objgrp(0));
                lmm->lmm_objects[i].l_ost_gen    = cpu_to_le32(1); /* XXX */
                lmm->lmm_objects[i].l_ost_idx    = cpu_to_le32(lu_idif_gr(fid));
        }
        
        rc = dt_xattr_set(env, next, &buf, XATTR_NAME_LOV, 0, th, BYPASS_CAPA);

        OBD_FREE(lmm, buf.lb_len);

        RETURN(rc);
}

/*
 *
 */
int lod_init_striping(const struct lu_env *env,
                       struct lod_object *mo,
                       struct lu_buf *lb)
{
        LBUG();
        RETURN(0);
}

extern int lov_setup(struct obd_device *obd, struct lustre_cfg *cfg);

int lod_lov_init(struct lod_device *m, struct lustre_cfg *cfg)
{
        int                         rc;
        ENTRY;

        m->mbd_obd = class_name2obd(lustre_cfg_string(cfg, 0));
        LASSERT(m->mbd_obd != NULL);
        m->mbd_obd->obd_lu_dev = &m->mbd_dt_dev.dd_lu_dev;

        rc = lov_setup(m->mbd_obd, cfg);
        CERROR("rc %d\n", rc);

        RETURN(rc);
}

int lod_lov_fini(struct lod_device *m)
{
        struct obd_device   *obd = m->mbd_obd;
        struct lov_obd      *lov = &obd->u.lov;
        struct list_head    *pos, *tmp;
        struct pool_desc    *pool;
        struct obd_export   *exp;
        int                  i, rc;
        ENTRY;

        list_for_each_safe(pos, tmp, &lov->lov_pool_list) {
                pool = list_entry(pos, struct pool_desc, pool_list);
                /* free pool structs */
                CDEBUG(D_INFO, "delete pool %p\n", pool);
                lov_pool_del(obd, pool->pool_name);
        }
        cfs_hash_destroy(lov->lov_pools_hash_body);
        lov_ost_pool_free(&(lov->lov_qos.lq_rr.lqr_pool));
        lov_ost_pool_free(&lov->lov_packed);

        for (i = 0; i < LOD_MAX_OSTNR; i++) {
                exp = m->mbd_ost_exp[i];
                if (exp == NULL)
                        continue;

                rc = qos_del_tgt(m->mbd_obd, i);
                LASSERT(rc == 0);

                rc = obd_disconnect(exp);
                if (rc)
                        CERROR("error in disconnect from #%u: %d\n", i, rc);

                if (lov->lov_tgts && lov->lov_tgts[i])
                        OBD_FREE_PTR(lov->lov_tgts[i]);
        }
 
        if (lov->lov_tgts) {
                OBD_FREE(lov->lov_tgts, sizeof(*lov->lov_tgts) *
                         lov->lov_tgt_size);
                lov->lov_tgt_size = 0;
        }

        /* clear pools parent proc entry only after all pools is killed */
        lprocfs_obd_cleanup(obd);

        OBD_FREE_PTR(lov->lov_qos.lq_statfs_data);

        RETURN(0);
}

