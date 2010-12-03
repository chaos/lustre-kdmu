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
 * lustre/mdd/mdd_dir.c
 *
 * Lustre Metadata Server (mdd) routines
 *
 * Author: Wang Di <wangdi@clusterfs.com>
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
#include <lustre_mds.h>
#include <lustre/lustre_idl.h>
#include <lustre_fid.h>

#include "mdd_internal.h"

static const char dot[] = ".";
static const char dotdot[] = "..";

static struct lu_name lname_dotdot = {
        (char *) dotdot,
        sizeof(dotdot) - 1
};

static int __mdd_lookup(const struct lu_env *env, struct md_object *pobj,
                        const struct lu_name *lname, struct lu_fid* fid,
                        int mask);
static int mdd_links_add(const struct lu_env *env,
                         struct mdd_object *mdd_obj,
                         const struct lu_fid *pfid,
                         const struct lu_name *lname,
                         struct mdd_thandle *handle);
static int mdd_links_rename(const struct lu_env *env,
                            struct mdd_object *mdd_obj,
                            const struct lu_fid *oldpfid,
                            const struct lu_name *oldlname,
                            const struct lu_fid *newpfid,
                            const struct lu_name *newlname,
                            struct mdd_thandle *handle);

static int
__mdd_lookup_locked(const struct lu_env *env, struct md_object *pobj,
                    const struct lu_name *lname, struct lu_fid* fid, int mask)
{
        const char *name = lname->ln_name;
        struct mdd_object *mdd_obj = md2mdd_obj(pobj);
        struct dynlock_handle *dlh;
        int rc;

        dlh = mdd_pdo_read_lock(env, mdd_obj, name, MOR_TGT_PARENT);
        if (unlikely(dlh == NULL))
                return -ENOMEM;
        rc = __mdd_lookup(env, pobj, lname, fid, mask);
        mdd_pdo_read_unlock(env, mdd_obj, dlh);

        return rc;
}

int mdd_lookup(const struct lu_env *env,
               struct md_object *pobj, const struct lu_name *lname,
               struct lu_fid* fid, struct md_op_spec *spec)
{
        int rc;
        ENTRY;
        rc = __mdd_lookup_locked(env, pobj, lname, fid, MAY_EXEC);
        RETURN(rc);
}

static int mdd_parent_fid(const struct lu_env *env, struct mdd_object *obj,
                          struct lu_fid *fid)
{
        return __mdd_lookup_locked(env, &obj->mod_obj, &lname_dotdot, fid, 0);
}

/*
 * For root fid use special function, which does not compare version component
 * of fid. Version component is different for root fids on all MDTs.
 */
int mdd_is_root(struct mdd_device *mdd, const struct lu_fid *fid)
{
        return fid_seq(&mdd->mdd_root_fid) == fid_seq(fid) &&
                fid_oid(&mdd->mdd_root_fid) == fid_oid(fid);
}

/*
 * return 1: if lf is the fid of the ancestor of p1;
 * return 0: if not;
 *
 * return -EREMOTE: if remote object is found, in this
 * case fid of remote object is saved to @pf;
 *
 * otherwise: values < 0, errors.
 */
static int mdd_is_parent(const struct lu_env *env,
                         struct mdd_device *mdd,
                         struct mdd_object *p1,
                         const struct lu_fid *lf,
                         struct lu_fid *pf)
{
        struct mdd_object *parent = NULL;
        struct lu_fid *pfid;
        int rc;
        ENTRY;

        LASSERT(!lu_fid_eq(mdo2fid(p1), lf));
        pfid = &mdd_env_info(env)->mti_fid;

        /* Check for root first. */
        if (mdd_is_root(mdd, mdo2fid(p1)))
                RETURN(0);

        for(;;) {
                /* this is done recursively, bypass capa for each obj */
                mdd_set_capainfo(env, 4, p1, BYPASS_CAPA);
                rc = mdd_parent_fid(env, p1, pfid);
                if (rc)
                        GOTO(out, rc);
                if (mdd_is_root(mdd, pfid))
                        GOTO(out, rc = 0);
                if (lu_fid_eq(pfid, lf))
                        GOTO(out, rc = 1);
                if (parent)
                        mdd_object_put(env, parent);
                parent = mdd_object_find(env, mdd, pfid);

                /* cross-ref parent */
                if (parent == NULL) {
                        if (pf != NULL)
                                *pf = *pfid;
                        GOTO(out, rc = -EREMOTE);
                } else if (IS_ERR(parent))
                        GOTO(out, rc = PTR_ERR(parent));
                p1 = parent;
        }
        EXIT;
out:
        if (parent && !IS_ERR(parent))
                mdd_object_put(env, parent);
        return rc;
}

/*
 * No permission check is needed.
 *
 * returns 1: if fid is ancestor of @mo;
 * returns 0: if fid is not a ancestor of @mo;
 *
 * returns EREMOTE if remote object is found, fid of remote object is saved to
 * @fid;
 *
 * returns < 0: if error
 */
int mdd_is_subdir(const struct lu_env *env, struct md_object *mo,
                  const struct lu_fid *fid, struct lu_fid *sfid)
{
        struct mdd_device *mdd = mdo2mdd(mo);
        int rc;
        ENTRY;

        if (!S_ISDIR(mdd_object_type(md2mdd_obj(mo))))
                RETURN(0);

        rc = mdd_is_parent(env, mdd, md2mdd_obj(mo), fid, sfid);
        if (rc == 0) {
                /* found root */
                fid_zero(sfid);
        } else if (rc == 1) {
                /* found @fid is parent */
                *sfid = *fid;
                rc = 0;
        }
        RETURN(rc);
}

/*
 * Check that @dir contains no entries except (possibly) dot and dotdot.
 *
 * Returns:
 *
 *             0        empty
 *      -ENOTDIR        not a directory object
 *    -ENOTEMPTY        not empty
 *           -ve        other error
 *
 */
static int mdd_dir_is_empty(const struct lu_env *env,
                            struct mdd_object *dir)
{
        struct dt_it     *it;
        struct dt_object *obj;
        const struct dt_it_ops *iops;
        int result;
        ENTRY;

        obj = mdd_object_child(dir);
        if (!dt_try_as_dir(env, obj))
                RETURN(-ENOTDIR);

        iops = &obj->do_index_ops->dio_it;
        it = iops->init(env, obj, BYPASS_CAPA);
        if (it != NULL) {
                result = iops->get(env, it, (const void *)"");
                if (result > 0) {
                        int i;
                        for (result = 0, i = 0; result == 0 && i < 3; ++i)
                                result = iops->next(env, it);
                        if (result == 0)
                                result = -ENOTEMPTY;
                        else if (result == +1)
                                result = 0;
                } else if (result == 0)
                        /*
                         * Huh? Index contains no zero key?
                         */
                        result = -EIO;

                iops->put(env, it);
                iops->fini(env, it);
        } else
                result = -ENOMEM;
        RETURN(result);
}

static int __mdd_may_link(const struct lu_env *env, struct mdd_object *obj)
{
        struct mdd_device *m = mdd_obj2mdd_dev(obj);
        struct lu_attr *la = &mdd_env_info(env)->mti_la;
        int rc;
        ENTRY;

        rc = mdd_la_get(env, obj, la, BYPASS_CAPA);
        if (rc)
                RETURN(rc);

        /*
         * Subdir count limitation can be broken through.
         */
        if (la->la_nlink >= m->mdd_dt_conf.ddp_max_nlink &&
            !S_ISDIR(la->la_mode))
                RETURN(-EMLINK);
        else
                RETURN(0);
}

/*
 * Check whether it may create the cobj under the pobj.
 * cobj maybe NULL
 */
int mdd_may_create(const struct lu_env *env, struct mdd_object *pobj,
                   struct mdd_object *cobj, int check_perm, int check_nlink)
{
        int rc = 0;
        ENTRY;

        if (cobj && mdd_object_exists(cobj))
                RETURN(-EEXIST);

        if (mdd_is_dead_obj(pobj))
                RETURN(-ENOENT);

        if (check_perm)
                rc = mdd_permission_internal(env, pobj, NULL,MAY_WRITE|MAY_EXEC);

        if (!rc && check_nlink)
                rc = __mdd_may_link(env, pobj);

        RETURN(rc);
}

/*
 * Check whether can unlink from the pobj in the case of "cobj == NULL".
 */
int mdd_may_unlink(const struct lu_env *env, struct mdd_object *pobj,
                   const struct md_attr *ma)
{
        int rc;
        ENTRY;

        if (mdd_is_dead_obj(pobj))
                RETURN(-ENOENT);

        if ((ma->ma_attr.la_valid & LA_FLAGS) &&
            (ma->ma_attr.la_flags & (LUSTRE_APPEND_FL | LUSTRE_IMMUTABLE_FL)))
                RETURN(-EPERM);

        rc = mdd_permission_internal(env, pobj, NULL, MAY_WRITE | MAY_EXEC);
        if (rc)
                RETURN(rc);

        if (mdd_is_append(pobj))
                RETURN(-EPERM);

        RETURN(rc);
}

/*
 * pobj == NULL is remote ops case, under such case, pobj's
 * VTX feature has been checked already, no need check again.
 */
static inline int mdd_is_sticky(const struct lu_env *env,
                                struct mdd_object *pobj,
                                struct mdd_object *cobj)
{
        struct lu_attr *tmp_la = &mdd_env_info(env)->mti_la;
        struct md_ucred *uc = md_ucred(env);
        int rc;

        if (pobj) {
                rc = mdd_la_get(env, pobj, tmp_la, BYPASS_CAPA);
                if (rc)
                        return rc;

                if (!(tmp_la->la_mode & S_ISVTX) ||
                     (tmp_la->la_uid == uc->mu_fsuid))
                        return 0;
        }

        rc = mdd_la_get(env, cobj, tmp_la, BYPASS_CAPA);
        if (rc)
                return rc;

        if (tmp_la->la_uid == uc->mu_fsuid)
                return 0;

        return !mdd_capable(uc, CFS_CAP_FOWNER);
}

/*
 * Check whether it may delete the cobj from the pobj.
 * pobj maybe NULL
 * the object is supposed to be locked
 */
int mdd_may_delete(const struct lu_env *env, struct mdd_object *pobj,
                   struct mdd_object *cobj, struct md_attr *ma,
                   int check_perm, int check_empty)
{
        int rc = 0;
        ENTRY;

        LASSERT(cobj);
        if (!mdd_object_exists(cobj))
                RETURN(-ENOENT);

        if (mdd_is_dead_obj(cobj))
                RETURN(-ESTALE);

        if (pobj) {
                if (!mdd_object_exists(pobj))
                        RETURN(-ENOENT);

                if (mdd_is_dead_obj(pobj))
                        RETURN(-ENOENT);

                if (check_perm) {
                        rc = mdd_permission_internal(env, pobj, NULL,
                                                     MAY_WRITE | MAY_EXEC);
                        if (rc)
                                RETURN(rc);
                }

                if (mdd_is_append(pobj))
                        RETURN(-EPERM);
        }

        if (!(ma->ma_attr_flags & MDS_VTX_BYPASS) &&
            mdd_is_sticky(env, pobj, cobj))
                RETURN(-EPERM);

        if (mdd_is_immutable(cobj) || mdd_is_append(cobj))
                RETURN(-EPERM);

        if ((ma->ma_attr.la_valid & LA_FLAGS) &&
            (ma->ma_attr.la_flags & (LUSTRE_APPEND_FL | LUSTRE_IMMUTABLE_FL)))
                RETURN(-EPERM);

        if (S_ISDIR(ma->ma_attr.la_mode)) {
                struct mdd_device *mdd = mdo2mdd(&cobj->mod_obj);

                if (!S_ISDIR(mdd_object_type(cobj)))
                        RETURN(-ENOTDIR);

                if (lu_fid_eq(mdo2fid(cobj), &mdd->mdd_root_fid))
                        RETURN(-EBUSY);
        } else if (S_ISDIR(mdd_object_type(cobj)))
                RETURN(-EISDIR);

        if (S_ISDIR(ma->ma_attr.la_mode) && check_empty)
                rc = mdd_dir_is_empty(env, cobj);

        RETURN(rc);
}

/*
 * tgt maybe NULL
 * has mdd_write_lock on src already, but not on tgt yet
 */
int mdd_link_sanity_check(const struct lu_env *env,
                          struct mdd_object *tgt_obj,
                          const struct lu_name *lname,
                          struct mdd_object *src_obj)
{
        struct mdd_device *m = mdd_obj2mdd_dev(src_obj);
        int rc = 0;
        ENTRY;

        if (!mdd_object_exists(src_obj))
                RETURN(-ENOENT);

        if (mdd_is_dead_obj(src_obj))
                RETURN(-ESTALE);

        /* Local ops, no lookup before link, check filename length here. */
        if (lname && (lname->ln_namelen > m->mdd_dt_conf.ddp_max_name_len))
                RETURN(-ENAMETOOLONG);

        if (mdd_is_immutable(src_obj) || mdd_is_append(src_obj))
                RETURN(-EPERM);

        if (S_ISDIR(mdd_object_type(src_obj)))
                RETURN(-EPERM);

        LASSERT(src_obj != tgt_obj);
        if (tgt_obj) {
                rc = mdd_may_create(env, tgt_obj, NULL, 1, 0);
                if (rc)
                        RETURN(rc);
        }

        rc = __mdd_may_link(env, src_obj);

        RETURN(rc);
}

/**
 * If subdir count is up to ddp_max_nlink, then enable MNLINK_OBJ flag and
 * assign i_nlink to 1 which means the i_nlink for subdir count is incredible
 * (maybe too large to be represented). It is a trick to break through the
 * "i_nlink" limitation for subdir count.
 */
void __mdd_ref_add(const struct lu_env *env, struct mdd_object *obj,
                   struct mdd_thandle *handle)
{
        struct lu_attr *tmp_la = &mdd_env_info(env)->mti_la;
        struct mdd_device *m = mdd_obj2mdd_dev(obj);

        if (!mdd_is_mnlink(obj)) {
                if (mdd_object_exists(obj) && S_ISDIR(mdd_object_type(obj))) {
                        if (mdd_la_get(env, obj, tmp_la, BYPASS_CAPA))
                                return;

                        if (tmp_la->la_nlink >= m->mdd_dt_conf.ddp_max_nlink) {
                                obj->mod_flags |= MNLINK_OBJ;
                                tmp_la->la_nlink = 1;
                                tmp_la->la_valid = LA_NLINK;
                                mdd_attr_set_internal(env, obj, tmp_la, handle, 0);
                                return;
                        }
                }
                mdd_tx_ref_add(env, obj, handle);
        }
}

void __mdd_ref_del(const struct lu_env *env, struct mdd_object *obj,
                   struct mdd_thandle *handle, int is_dot)
{
        if (!mdd_is_mnlink(obj) || is_dot)
                mdd_tx_ref_del(env, obj, handle);
}

/* insert named index, add reference if isdir */
static void __mdd_index_insert(const struct lu_env *env, struct mdd_object *pobj,
                               const struct lu_fid *lf, const char *name, int is_dir,
                               struct mdd_thandle *handle)
{
        ENTRY;

        mdd_tx_idx_insert(env, pobj, lf, name, handle);
        if (is_dir)
                __mdd_ref_add(env, pobj, handle);

        EXIT;
}

/* delete named index, drop reference if isdir */
static void __mdd_index_delete(const struct lu_env *env, struct mdd_object *pobj,
                              const char *name, int is_dir, struct mdd_thandle *handle)
{
        ENTRY;

        mdd_tx_idx_delete(env, pobj, name, handle);
        if (is_dir) {
                int is_dot = 0;

                if (name != NULL && name[0] == '.' && name[1] == 0)
                        is_dot = 1;
                __mdd_ref_del(env, pobj, handle, is_dot);
        }

        EXIT;
}


/** Store a namespace change changelog record
 * If this fails, we must fail the whole transaction; we don't
 * want the change to commit without the log entry.
 * \param target - mdd_object of change
 * \param parent - parent dir/object
 * \param tf - target lu_fid, overrides fid of \a target if this is non-null
 * \param tname - target name string
 * \param handle - transacion handle
 */
static int mdd_changelog_ns_store(const struct lu_env  *env,
                                  struct mdd_device    *mdd,
                                  enum changelog_rec_type type,
                                  struct mdd_object    *target,
                                  struct mdd_object    *parent,
                                  const struct lu_fid  *tf,
                                  const struct lu_name *tname,
                                  struct mdd_thandle *handle)
{
#ifdef XXX_MDD_CHANGELOG
        const struct lu_fid *tfid;
        const struct lu_fid *tpfid = mdo2fid(parent);
        struct llog_changelog_rec *rec;
        struct lu_buf *buf;
        int reclen;
        int rc;
        ENTRY;

        /* Not recording */
        if (!(mdd->mdd_cl.mc_flags & CLM_ON))
                RETURN(0);
        if ((mdd->mdd_cl.mc_mask & (1 << type)) == 0)
                RETURN(0);

        LASSERT(parent != NULL);
        LASSERT(tname != NULL);
        LASSERT(handle != NULL);

        /* target */
        reclen = llog_data_len(sizeof(*rec) + tname->ln_namelen);
        buf = mdd_buf_alloc(env, reclen);
        if (buf->lb_buf == NULL)
                RETURN(-ENOMEM);
        rec = (struct llog_changelog_rec *)buf->lb_buf;

        rec->cr.cr_flags = CLF_VERSION;
        rec->cr.cr_type = (__u32)type;
        tfid = tf ? tf : mdo2fid(target);
        rec->cr.cr_tfid = *tfid;
        rec->cr.cr_pfid = *tpfid;
        rec->cr.cr_namelen = tname->ln_namelen;
        memcpy(rec->cr.cr_name, tname->ln_name, rec->cr.cr_namelen);
        if (likely(target))
                target->mod_cltime = cfs_time_current_64();

        rc = mdd_changelog_llog_write(mdd, rec, handle);
        if (rc < 0) {
                CERROR("changelog failed: rc=%d, op%d %s c"DFID" p"DFID"\n",
                       rc, type, tname->ln_name, PFID(tfid), PFID(tpfid));
                return -EFAULT;
        }
#endif
        return 0;
}

static int mdd_link(const struct lu_env *env, struct md_object *tgt_obj,
                    struct md_object *src_obj, const struct lu_name *lname,
                    struct md_attr *ma)
{
        const char *name = lname->ln_name;
        struct lu_attr    *la = &mdd_env_info(env)->mti_la_for_fix;
        struct mdd_object *mdd_tobj = md2mdd_obj(tgt_obj);
        struct mdd_object *mdd_sobj = md2mdd_obj(src_obj);
        struct mdd_device *mdd = mdo2mdd(src_obj);
        struct dynlock_handle *dlh;
        struct mdd_thandle *handle;
#ifdef HAVE_QUOTA_SUPPORT
        struct obd_device *obd = mdd->mdd_obd_dev;
        struct obd_export *exp = md_quota(env)->mq_exp;
        struct mds_obd *mds = &obd->u.mds;
        unsigned int qids[MAXQUOTAS] = { 0, 0 };
        int quota_opc = 0, rec_pending[MAXQUOTAS] = { 0, 0 };
#endif
        int rc;
        ENTRY;

#ifdef HAVE_QUOTA_SUPPORT
        if (mds->mds_quota) {
                struct lu_attr *la_tmp = &mdd_env_info(env)->mti_la;

                rc = mdd_la_get(env, mdd_tobj, la_tmp, BYPASS_CAPA);
                if (!rc) {
                        void *data = NULL;
                        mdd_data_get(env, mdd_tobj, &data);
                        quota_opc = FSFILT_OP_LINK;
                        mdd_quota_wrapper(la_tmp, qids);
                        /* get block quota for parent */
                        lquota_chkquota(mds_quota_interface_ref, obd, exp,
                                        qids, rec_pending, 1, NULL,
                                        LQUOTA_FLAGS_BLK, data, 1);
                }
        }
#endif

        dlh = mdd_pdo_write_lock(env, mdd_tobj, name, MOR_TGT_CHILD);
        if (dlh == NULL)
                RETURN(rc = -ENOMEM);
        mdd_write_lock(env, mdd_sobj, MOR_TGT_CHILD);

        rc = mdd_link_sanity_check(env, mdd_tobj, lname, mdd_sobj);
        if (rc)
                GOTO(out_unlock, rc);

        handle = mdd_tx_start(env, mdd);
        if (IS_ERR(handle))
                GOTO(out_unlock, rc = PTR_ERR(handle));

        mdd_tx_idx_insert(env, mdd_tobj, mdo2fid(mdd_sobj), name, handle);
        __mdd_ref_add(env, mdd_sobj, handle);

        LASSERT(ma->ma_attr.la_valid & LA_CTIME);
        la->la_ctime = la->la_mtime = ma->ma_attr.la_ctime;
        la->la_valid = LA_CTIME | LA_MTIME;
        mdd_attr_check_set_internal_locked(env, mdd_tobj, la, handle, 0);

        la->la_valid = LA_CTIME;
        mdd_attr_check_set_internal(env, mdd_sobj, la, handle, 0);

        mdd_links_add(env, mdd_sobj, mdo2fid(mdd_tobj), lname, handle);

        EXIT;

        mdd_changelog_ns_store(env, mdd, CL_HARDLINK, mdd_sobj, mdd_tobj,
                               NULL, lname, handle);
        rc = mdd_tx_end(env, handle);

out_unlock:
        mdd_write_unlock(env, mdd_sobj);
        mdd_pdo_write_unlock(env, mdd_tobj, dlh);

#ifdef HAVE_QUOTA_SUPPORT
        if (quota_opc) {
                lquota_pending_commit(mds_quota_interface_ref, obd,
                                      qids, rec_pending, 1);
                /* Trigger dqacq for the parent owner. If failed,
                 * the next call for lquota_chkquota will process it. */
                lquota_adjust(mds_quota_interface_ref, obd, 0, qids, rc,
                              quota_opc);
        }
#endif
        return rc;
}

/* caller should take a lock before calling */
void mdd_finish_unlink(const struct lu_env *env,
                       struct mdd_object *obj, struct md_attr *ma,
                       struct mdd_thandle *th)
{
        int rc, is_dir;
        ENTRY;

        LASSERT(mdd_write_locked(env, obj) != 0);

        ma->ma_valid &= ~MA_INODE;
        rc = mdd_iattr_get(env, obj, ma);
        if (rc) {
                mdd_tx_set_error(th, rc);
                EXIT;
                return;
        }

        /*
         * notice at this point transaction is being declared
         * yet, so no changes have been applied to the object
         * IOW, if this is the last name is being removed, then
         * nlink == 1 (or 2 for directory)
         */
        is_dir = S_ISDIR(ma->ma_attr.la_mode);
        if (ma->ma_attr.la_nlink == 1 || (is_dir && ma->ma_attr.la_nlink == 2)) {
                obj->mod_flags |= DEAD_OBJ;
                /* add new orphan and the object
                 * will be deleted during mdd_close() */
                if (obj->mod_count) {
                        __mdd_orphan_add(env, obj, th);
                        CDEBUG(D_HA, "Object "DFID" is inserted into "
                               "orphan list, open count = %d\n",
                               PFID(mdd_object_fid(obj)),
                               obj->mod_count);
                } else {
                        mdd_tx_destroy(env, obj, th);
                }

        }
        ma->ma_valid &= ~(MA_LOV | MA_COOKIE);

        EXIT;
}

/*
 * pobj maybe NULL
 * has mdd_write_lock on cobj already, but not on pobj yet
 */
int mdd_unlink_sanity_check(const struct lu_env *env, struct mdd_object *pobj,
                            struct mdd_object *cobj, struct md_attr *ma)
{
        int rc;
        ENTRY;

        rc = mdd_may_delete(env, pobj, cobj, ma, 1, 1);

        RETURN(rc);
}

static int mdd_unlink(const struct lu_env *env, struct md_object *pobj,
                      struct md_object *cobj, const struct lu_name *lname,
                      struct md_attr *ma)
{
        const char         *name = lname->ln_name;
        struct lu_attr     *la = &mdd_env_info(env)->mti_la_for_fix;
        struct mdd_object  *mdd_pobj = md2mdd_obj(pobj);
        struct mdd_object  *mdd_cobj = md2mdd_obj(cobj);
        struct mdd_device  *mdd = mdo2mdd(pobj);
        struct dynlock_handle *dlh;
        struct mdd_thandle *handle;
#ifdef HAVE_QUOTA_SUPPORT
        struct obd_device  *obd = mdd->mdd_obd_dev;
        struct mds_obd *mds = &obd->u.mds;
        unsigned int qcids[MAXQUOTAS] = { 0, 0 };
        unsigned int qpids[MAXQUOTAS] = { 0, 0 };
        int quota_opc = 0;
#endif
        int rc, is_dir, nlink;
        ENTRY;

        LASSERTF(mdd_object_exists(mdd_cobj) > 0, "FID is "DFID"\n",
                 PFID(mdd_object_fid(mdd_cobj)));

        dlh = mdd_pdo_write_lock(env, mdd_pobj, name, MOR_TGT_PARENT);
        if (dlh == NULL)
                RETURN(rc = -ENOMEM);
        mdd_write_lock(env, mdd_cobj, MOR_TGT_CHILD);

        /* fetch nlink */
        rc = mdd_la_get(env, mdd_cobj, la, mdd_object_capa(env, mdd_cobj));
        if (rc)
                GOTO(cleanup, rc);
        is_dir = S_ISDIR(la->la_mode);
        nlink = la->la_nlink;

        rc = mdd_unlink_sanity_check(env, mdd_pobj, mdd_cobj, ma);
        if (rc)
                GOTO(cleanup, rc);

        handle = mdd_tx_start(env, mdd);
        if (IS_ERR(handle))
                GOTO(cleanup, rc = PTR_ERR(handle));

        __mdd_index_delete(env, mdd_pobj, name, is_dir, handle);
        __mdd_ref_del(env, mdd_cobj, handle, 0);
        if (is_dir)
                /* unlink dot */
                __mdd_ref_del(env, mdd_cobj, handle, 1);

        LASSERT(ma->ma_attr.la_valid & LA_CTIME);
        la->la_ctime = la->la_mtime = ma->ma_attr.la_ctime;
        la->la_valid = LA_CTIME | LA_MTIME;
        mdd_attr_check_set_internal_locked(env, mdd_pobj, la, handle, 0);

        if (ma->ma_attr.la_nlink > 0 || mdd_cobj->mod_count > 0) {
                /* update ctime of an unlinked file only if it is still
                 * opened or a link still exists */
                la->la_valid = LA_CTIME;
                mdd_attr_check_set_internal(env, mdd_cobj, la, handle, 0);
        }

        mdd_finish_unlink(env, mdd_cobj, ma, handle);
#ifdef HAVE_QUOTA_SUPPORT
        if (mds->mds_quota && ma->ma_valid & MA_INODE &&
            ma->ma_attr.la_nlink == 0) {
                struct lu_attr *la_tmp = &mdd_env_info(env)->mti_la;

                rc = mdd_la_get(env, mdd_pobj, la_tmp, BYPASS_CAPA);
                if (!rc) {
                        mdd_quota_wrapper(la_tmp, qpids);
                        if (mdd_cobj->mod_count == 0) {
                                quota_opc = FSFILT_OP_UNLINK;
                                mdd_quota_wrapper(&ma->ma_attr, qcids);
                        } else {
                                quota_opc = FSFILT_OP_UNLINK_PARTIAL_PARENT;
                        }
                }
        }
#endif
        if (!is_dir)
                /* old files may not have link ea; ignore errors */
                mdd_links_rename(env, mdd_cobj, mdo2fid(mdd_pobj),
                                 lname, NULL, NULL, handle);

        EXIT;

        mdd_changelog_ns_store(env, mdd, is_dir ? CL_RMDIR : CL_UNLINK,
                               mdd_cobj, mdd_pobj, NULL, lname, handle);

        rc = mdd_tx_end(env, handle);

cleanup:
        mdd_write_unlock(env, mdd_cobj);
        mdd_pdo_write_unlock(env, mdd_pobj, dlh);
#ifdef HAVE_QUOTA_SUPPORT
        if (quota_opc)
                /* Trigger dqrel on the owner of child and parent. If failed,
                 * the next call for lquota_chkquota will process it. */
                lquota_adjust(mds_quota_interface_ref, obd, qcids, qpids, rc,
                              quota_opc);
#endif
        return rc;
}

/* XXX: no partial ops supported yet */
#if 0
/* has not lock on pobj yet */
static int mdd_ni_sanity_check(const struct lu_env *env,
                               struct md_object *pobj,
                               const struct md_attr *ma)
{
        struct mdd_object *obj = md2mdd_obj(pobj);
        int rc;
        ENTRY;

        if (ma->ma_attr_flags & MDS_PERM_BYPASS)
                RETURN(0);

        rc = mdd_may_create(env, obj, NULL, 1, S_ISDIR(ma->ma_attr.la_mode));

        RETURN(rc);
}

/*
 * Partial operation.
 */
static int mdd_name_insert(const struct lu_env *env,
                           struct md_object *pobj,
                           const struct lu_name *lname,
                           const struct lu_fid *fid,
                           const struct md_attr *ma)
{
        const char *name = lname->ln_name;
        struct lu_attr   *la = &mdd_env_info(env)->mti_la_for_fix;
        struct mdd_object *mdd_obj = md2mdd_obj(pobj);
        struct dynlock_handle *dlh;
        struct mdd_thandle *handle;
        int is_dir = S_ISDIR(ma->ma_attr.la_mode);
#ifdef HAVE_QUOTA_SUPPORT
        struct md_ucred *uc = md_ucred(env);
        struct obd_device *obd = mdo2mdd(pobj)->mdd_obd_dev;
        struct obd_export *exp = md_quota(env)->mq_exp;
        struct mds_obd *mds = &obd->u.mds;
        unsigned int qids[MAXQUOTAS] = { 0, 0 };
        int quota_opc = 0, rec_pending[MAXQUOTAS] = { 0, 0 };
        cfs_cap_t save = uc->mu_cap;
#endif
        int rc;
        ENTRY;

#ifdef HAVE_QUOTA_SUPPORT
        if (mds->mds_quota) {
                if (!(ma->ma_attr_flags & MDS_QUOTA_IGNORE)) {
                        struct lu_attr *la_tmp = &mdd_env_info(env)->mti_la;

                        rc = mdd_la_get(env, mdd_obj, la_tmp, BYPASS_CAPA);
                        if (!rc) {
                                void *data = NULL;
                                mdd_data_get(env, mdd_obj, &data);
                                quota_opc = FSFILT_OP_LINK;
                                mdd_quota_wrapper(la_tmp, qids);
                                /* get block quota for parent */
                                lquota_chkquota(mds_quota_interface_ref, obd,
                                                exp, qids, rec_pending, 1, NULL,
                                                LQUOTA_FLAGS_BLK, data, 1);
                        }
                } else {
                        uc->mu_cap |= CFS_CAP_SYS_RESOURCE_MASK;
                }
        }
#endif
        handle = mdd_tx_start(env, pobj, fid, name, is_dir);
        if (IS_ERR(handle))
                GOTO(out_pending, rc = PTR_ERR(handle));

        dlh = mdd_pdo_write_lock(env, mdd_obj, name, MOR_TGT_PARENT);
        if (dlh == NULL)
                GOTO(out_trans, rc = -ENOMEM);

        rc = mdd_ni_sanity_check(env, pobj, ma);
        if (rc)
                GOTO(out_unlock, rc);

        rc = __mdd_index_insert(env, mdd_obj, fid, name, is_dir,
                                handle, BYPASS_CAPA);
        if (rc)
                GOTO(out_unlock, rc);

        /*
         * For some case, no need update obj's ctime (LA_CTIME is not set),
         * e.g. split_dir.
         * For other cases, update obj's ctime (LA_CTIME is set),
         * e.g. cmr_link.
         */
        if (ma->ma_attr.la_valid & LA_CTIME) {
                la->la_ctime = la->la_mtime = ma->ma_attr.la_ctime;
                la->la_valid = LA_CTIME | LA_MTIME;
                rc = mdd_attr_check_set_internal_locked(env, mdd_obj, la,
                                                        handle, 0);
        }
        EXIT;
out_unlock:
        mdd_pdo_write_unlock(env, mdd_obj, dlh);
out_trans:
        rc = mdd_tx_end(env, mdo2mdd(pobj), rc, handle);
out_pending:
#ifdef HAVE_QUOTA_SUPPORT
        if (mds->mds_quota) {
                if (quota_opc) {
                        lquota_pending_commit(mds_quota_interface_ref,
                                              obd, qids, rec_pending, 1);
                        /* Trigger dqacq for the parent owner. If failed,
                         * the next call for lquota_chkquota will process it*/
                        lquota_adjust(mds_quota_interface_ref, obd, 0, qids,
                                      rc, quota_opc);
                } else {
                        uc->mu_cap = save;
                }
        }
#endif
        return rc;
}

/* has not lock on pobj yet */
static int mdd_nr_sanity_check(const struct lu_env *env,
                               struct md_object *pobj,
                               const struct md_attr *ma)
{
        struct mdd_object *obj = md2mdd_obj(pobj);
        int rc;
        ENTRY;

        if (ma->ma_attr_flags & MDS_PERM_BYPASS)
                RETURN(0);

        rc = mdd_may_unlink(env, obj, ma);

        RETURN(rc);
}

/*
 * Partial operation.
 */
static int mdd_name_remove(const struct lu_env *env,
                           struct md_object *pobj,
                           const struct lu_name *lname,
                           const struct md_attr *ma)
{
        const char *name = lname->ln_name;
        struct lu_attr    *la = &mdd_env_info(env)->mti_la_for_fix;
        struct mdd_object *mdd_obj = md2mdd_obj(pobj);
        struct mdd_device *mdd = mdo2mdd(pobj);
        struct dynlock_handle *dlh;
        struct mdd_thandle *handle;
        int is_dir = S_ISDIR(ma->ma_attr.la_mode);
#ifdef HAVE_QUOTA_SUPPORT
        struct obd_device *obd = mdd->mdd_obd_dev;
        struct mds_obd *mds = &obd->u.mds;
        unsigned int qids[MAXQUOTAS] = { 0, 0 };
        int quota_opc = 0;
#endif
        int rc;
        ENTRY;

#ifdef HAVE_QUOTA_SUPPORT
        if (mds->mds_quota) {
                struct lu_attr *la_tmp = &mdd_env_info(env)->mti_la;

                rc = mdd_la_get(env, mdd_obj, la_tmp, BYPASS_CAPA);
                if (!rc) {
                        quota_opc = FSFILT_OP_UNLINK_PARTIAL_PARENT;
                        mdd_quota_wrapper(la_tmp, qids);
                }
        }
#endif
        handle = mdd_tx_start(env, mdd);
        if (IS_ERR(handle))
                GOTO(out_pending, rc = PTR_ERR(handle));

        rc = mdd_nr_sanity_check(env, pobj, ma);
        if (rc)
                GOTO(out_unlock, rc);

        rc = __mdd_index_delete(env, mdd_obj, name, is_dir,
                                handle, BYPASS_CAPA);
        if (rc)
                GOTO(out_unlock, rc);

        /*
         * For some case, no need update obj's ctime (LA_CTIME is not set),
         * e.g. split_dir.
         * For other cases, update obj's ctime (LA_CTIME is set),
         * e.g. cmr_unlink.
         */
        if (ma->ma_attr.la_valid & LA_CTIME) {
                la->la_ctime = la->la_mtime = ma->ma_attr.la_ctime;
                la->la_valid = LA_CTIME | LA_MTIME;
                rc = mdd_attr_check_set_internal_locked(env, mdd_obj, la,
                                                        handle, 0);
        }
        EXIT;
out_unlock:
        mdd_pdo_write_unlock(env, mdd_obj, dlh);
out_trans:
        rc = mdd_tx_end(env, mdd, rc, handle);
out_pending:
#ifdef HAVE_QUOTA_SUPPORT
        /* Trigger dqrel for the parent owner.
         * If failed, the next call for lquota_chkquota will process it. */
        if (quota_opc)
                lquota_adjust(mds_quota_interface_ref, obd, 0, qids, rc,
                              quota_opc);
#endif
        return rc;
}

/*
 * tobj maybe NULL
 * has mdd_write_lock on tobj alreay, but not on tgt_pobj yet
 */
static int mdd_rt_sanity_check(const struct lu_env *env,
                               struct mdd_object *tgt_pobj,
                               struct mdd_object *tobj,
                               struct md_attr *ma)
{
        int rc;
        ENTRY;

        if (unlikely(ma->ma_attr_flags & MDS_PERM_BYPASS))
                RETURN(0);

        /* XXX: for mdd_rename_tgt, "tobj == NULL" does not mean tobj not
         * exist. In fact, tobj must exist, otherwise the call trace will be:
         * mdt_reint_rename_tgt -> mdo_name_insert -> ... -> mdd_name_insert.
         * When get here, tobj must be NOT NULL, the other case has been
         * processed in cmr_rename_tgt before mdd_rename_tgt and enable
         * MDS_PERM_BYPASS.
         * So check may_delete, but not check nlink of tgt_pobj. */

        rc = mdd_may_delete(env, tgt_pobj, tobj, ma, 1, 1);

        RETURN(rc);
}

/* Partial rename op on slave MDD */
static int mdd_rename_tgt(const struct lu_env *env,
                          struct md_object *pobj, struct md_object *tobj,
                          const struct lu_fid *lf, const struct lu_name *lname,
                          struct md_attr *ma)
{
        const char *name = lname->ln_name;
        struct lu_attr    *la = &mdd_env_info(env)->mti_la_for_fix;
        struct mdd_object *mdd_tpobj = md2mdd_obj(pobj);
        struct mdd_object *mdd_tobj = md2mdd_obj(tobj);
        struct mdd_device *mdd = mdo2mdd(pobj);
        struct dynlock_handle *dlh;
        struct mdd_thandle *handle;
#ifdef HAVE_QUOTA_SUPPORT
        struct obd_device *obd = mdd->mdd_obd_dev;
        struct obd_export *exp = md_quota(env)->mq_exp;
        struct mds_obd *mds = &obd->u.mds;
        unsigned int qcids[MAXQUOTAS] = { 0, 0 };
        unsigned int qpids[MAXQUOTAS] = { 0, 0 };
        int quota_copc = 0, quota_popc = 0;
        int rec_pending[MAXQUOTAS] = { 0, 0 };
#endif
        int rc;
        ENTRY;

#ifdef HAVE_QUOTA_SUPPORT
        if (mds->mds_quota && !tobj) {
                struct lu_attr *la_tmp = &mdd_env_info(env)->mti_la;

                rc = mdd_la_get(env, mdd_tpobj, la_tmp, BYPASS_CAPA);
                if (!rc) {
                        void *data = NULL;
                        mdd_data_get(env, mdd_tpobj, &data);
                        quota_popc = FSFILT_OP_LINK;
                        mdd_quota_wrapper(la_tmp, qpids);
                        /* get block quota for target parent */
                        lquota_chkquota(mds_quota_interface_ref, obd, exp,
                                        qpids, rec_pending, 1, NULL,
                                        LQUOTA_FLAGS_BLK, data, 1);
                }
        }
#endif
        handle = mdd_declare_and_start_rename_tgt(env, pobj, tobj, lf, name, ma);
        if (IS_ERR(handle))
                GOTO(out_pending, rc = PTR_ERR(handle));

        dlh = mdd_pdo_write_lock(env, mdd_tpobj, name, MOR_TGT_PARENT);
        if (dlh == NULL)
                GOTO(out_trans, rc = -ENOMEM);
        if (tobj)
                mdd_write_lock(env, mdd_tobj, MOR_TGT_CHILD);

        rc = mdd_rt_sanity_check(env, mdd_tpobj, mdd_tobj, ma);
        if (rc)
                GOTO(cleanup, rc);

        /*
         * If rename_tgt is called then we should just re-insert name with
         * correct fid, no need to dec/inc parent nlink if obj is dir.
         */
        rc = __mdd_index_delete(env, mdd_tpobj, name, 0, handle, BYPASS_CAPA);
        if (rc)
                GOTO(cleanup, rc);

        rc = mdd_tx_idx_insert(env, mdd_tpobj, lf, name, handle);
        if (rc)
                GOTO(cleanup, rc);

        LASSERT(ma->ma_attr.la_valid & LA_CTIME);
        la->la_ctime = la->la_mtime = ma->ma_attr.la_ctime;

        la->la_valid = LA_CTIME | LA_MTIME;
        rc = mdd_attr_check_set_internal_locked(env, mdd_tpobj, la, handle, 0);
        if (rc)
                GOTO(cleanup, rc);

        /*
         * For tobj is remote case cmm layer has processed
         * and pass NULL tobj to here. So when tobj is NOT NULL,
         * it must be local one.
         */
        if (tobj && mdd_object_exists(mdd_tobj)) {
                __mdd_ref_del(env, mdd_tobj, handle, 0);

                /* Remove dot reference. */
                if (S_ISDIR(ma->ma_attr.la_mode))
                        __mdd_ref_del(env, mdd_tobj, handle, 1);

                la->la_valid = LA_CTIME;
                rc = mdd_attr_check_set_internal(env, mdd_tobj, la, handle, 0);
                if (rc)
                        GOTO(cleanup, rc);

                mdd_finish_unlink(env, mdd_tobj, ma, handle);

#ifdef HAVE_QUOTA_SUPPORT
                if (mds->mds_quota && ma->ma_valid & MA_INODE &&
                    ma->ma_attr.la_nlink == 0 && mdd_tobj->mod_count == 0) {
                        quota_copc = FSFILT_OP_UNLINK_PARTIAL_CHILD;
                        mdd_quota_wrapper(&ma->ma_attr, qcids);
                }
#endif
        }
        EXIT;
cleanup:
        if (tobj)
                mdd_write_unlock(env, mdd_tobj);
        mdd_pdo_write_unlock(env, mdd_tpobj, dlh);
out_trans:
        /* Bare EXT record with no RENAME in front of it signifies
           a partial slave op */
        mdd_changelog_ns_store(env, mdd, CL_EXT, mdd_tobj, mdd_tpobj, NULL, lname, handle);

        rc = mdd_tx_end(env, handle);

out_pending:
#ifdef HAVE_QUOTA_SUPPORT
        if (mds->mds_quota) {
                if (quota_popc)
                        lquota_pending_commit(mds_quota_interface_ref, obd,
                                              qpids, rec_pending, 1);

                if (quota_copc)
                        /* Trigger dqrel on the target owner of child.
                         * If failed, the next call for lquota_chkquota
                         * will process it. */
                        lquota_adjust(mds_quota_interface_ref, obd, qcids, qpids,
                                      rc, quota_copc);
        }
#endif
        return rc;
}
#endif

/*
 * The permission has been checked when obj created, no need check again.
 */
static int mdd_cd_sanity_check(const struct lu_env *env,
                               struct mdd_object *obj)
{
        ENTRY;

        /* EEXIST check */
        if (!obj || mdd_is_dead_obj(obj))
                RETURN(-ENOENT);

        RETURN(0);

}

static int mdd_create_data(const struct lu_env *env, struct md_object *pobj,
                           struct md_object *cobj, const struct md_op_spec *spec,
                           struct md_attr *ma)
{
        struct mdd_device *mdd = mdo2mdd(cobj);
        struct mdd_object *mdd_pobj = md2mdd_obj(pobj);
        struct mdd_object *son = md2mdd_obj(cobj);
        struct lu_buf      buf = { 0 };
        struct mdd_thandle    *handle;
        int                rc;
        ENTRY;

        rc = mdd_cd_sanity_check(env, son);
        if (rc)
                RETURN(rc);

        if (!md_should_create(spec->sp_cr_flags))
                RETURN(0);

        /*
         * there are following use cases for this function:
         * 1) late striping - file was created with MDS_OPEN_DELAY_CREATE
         *    striping can be specified or not
         * 2) CMD?
         */

        mdd_write_lock(env, son, MOR_TGT_CHILD);

        rc = mdd_iattr_get(env, son, ma);
        if (rc)
                GOTO(cleanup, rc);

        /* calling ->ah_make_hint() is used to transfer information from parent */
        mdd_object_make_hint(env, mdd_pobj, son, &ma->ma_attr);

        handle = mdd_tx_start(env, mdd);
        if (IS_ERR(handle))
                GOTO(cleanup, rc = PTR_ERR(handle));

        CDEBUG(D_OTHER, "ea %p/%u, cr_flags %o, no_create %u\n",
               spec->u.sp_ea.eadata, spec->u.sp_ea.eadatalen,
               spec->sp_cr_flags, spec->no_create);

        if (spec->no_create) {
                /* replay case */
                buf.lb_buf = (void *) spec->u.sp_ea.eadata;
                buf.lb_len = spec->u.sp_ea.eadatalen;
        } else  if (!(spec->sp_cr_flags & MDS_OPEN_HAS_OBJS)) {
                if (spec->sp_cr_flags & MDS_OPEN_HAS_EA) {
                        /* lfs setstripe */
                        buf.lb_buf = (void *) spec->u.sp_ea.eadata;
                        buf.lb_len = spec->u.sp_ea.eadatalen;
                }
        } else {
                /* MDS_OPEN_HAS_OBJS is not used anymore ? */
                LBUG();
        }

        mdd_tx_xattr_set(env, son, &buf, XATTR_NAME_LOV, 0, handle);

        rc = mdd_tx_end(env, handle);

        if (rc == 0)
               rc = mdd_attr_get_internal(env, son, ma);

cleanup:
        mdd_write_unlock(env, son);

        RETURN(rc);
}

/* Get fid from name and parent */
static int
__mdd_lookup(const struct lu_env *env, struct md_object *pobj,
             const struct lu_name *lname, struct lu_fid* fid, int mask)
{
        const char          *name = lname->ln_name;
        const struct dt_key *key = (const struct dt_key *)name;
        struct mdd_object   *mdd_obj = md2mdd_obj(pobj);
        struct mdd_device   *m = mdo2mdd(pobj);
        struct dt_object    *dir = mdd_object_child(mdd_obj);
        int rc;
        ENTRY;

        if (unlikely(mdd_is_dead_obj(mdd_obj)))
                RETURN(-ESTALE);

        rc = mdd_object_exists(mdd_obj);
        if (unlikely(rc == 0))
                RETURN(-ESTALE);
        else if (unlikely(rc < 0)) {
                CERROR("Object "DFID" locates on remote server\n",
                        PFID(mdo2fid(mdd_obj)));
                LBUG();
        }

        /* The common filename length check. */
        if (unlikely(lname->ln_namelen > m->mdd_dt_conf.ddp_max_name_len))
                RETURN(-ENAMETOOLONG);

        rc = mdd_permission_internal_locked(env, mdd_obj, NULL, mask,
                                            MOR_TGT_PARENT);
        if (rc)
                RETURN(rc);

        if (likely(S_ISDIR(mdd_object_type(mdd_obj)) &&
                   dt_try_as_dir(env, dir))) {

                rc = dir->do_index_ops->dio_lookup(env, dir,
                                                 (struct dt_rec *)fid, key,
                                                 mdd_object_capa(env, mdd_obj));
                if (rc > 0)
                        rc = 0;
                else if (rc == 0)
                        rc = -ENOENT;
        } else
                rc = -ENOTDIR;

        RETURN(rc);
}

void mdd_object_initialize(const struct lu_env *env, const struct lu_fid *pfid,
                           const struct lu_name *lname, struct mdd_object *child,
                           struct md_attr *ma, struct mdd_thandle *handle,
                           const struct md_op_spec *spec)
{
        ENTRY;

        /*
         * Update attributes for child.
         *
         * FIXME:
         *  (1) the valid bits should be converted between Lustre and Linux;
         *  (2) maybe, the child attributes should be set in OSD when creation.
         */

        mdd_attr_set_internal(env, child, &ma->ma_attr, handle, 0);

        /*
         * in case of replay we just set LOVEA provided by the client
         */
        if (spec->no_create || (spec->sp_cr_flags & MDS_OPEN_HAS_EA)) {
                struct lu_buf buf;
                buf.lb_buf = (void *) spec->u.sp_ea.eadata;
                buf.lb_len = spec->u.sp_ea.eadatalen;
                mdd_tx_xattr_set(env, child, &buf, XATTR_NAME_LOV, 0, handle);
        }

        if (S_ISDIR(ma->ma_attr.la_mode)) {
                /* Add "." and ".." for newly created dir */
                __mdd_ref_add(env, child, handle);
                mdd_tx_idx_insert(env, child, mdo2fid(child), dot, handle);
                mdd_tx_idx_insert(env, child, pfid, dotdot, handle);
        }

        mdd_links_add(env, child, pfid, lname, handle);

        EXIT;
}

/* has not lock on pobj yet */
static int mdd_create_sanity_check(const struct lu_env *env,
                                   struct md_object *pobj,
                                   const struct lu_name *lname,
                                   struct md_attr *ma,
                                   struct md_op_spec *spec)
{
        struct mdd_thread_info *info = mdd_env_info(env);
        struct lu_attr    *la        = &info->mti_la;
        struct lu_fid     *fid       = &info->mti_fid;
        struct mdd_object *obj       = md2mdd_obj(pobj);
        struct mdd_device *m         = mdo2mdd(pobj);
        int lookup                   = spec->sp_cr_lookup;
        int rc;
        ENTRY;

        /* EEXIST check */
        if (mdd_is_dead_obj(obj))
                RETURN(-ENOENT);

        /* with this flags should should be in mdd_create_data() */
        if (unlikely(spec->sp_cr_flags & MDS_OPEN_HAS_EA))
                RETURN(-EINVAL);

        /*
         * In some cases this lookup is not needed - we know before if name
         * exists or not because MDT performs lookup for it.
         * name length check is done in lookup.
         */
        if (lookup) {
                /*
                 * Check if the name already exist, though it will be checked in
                 * _index_insert also, for avoiding rolling back if exists
                 * _index_insert.
                 */
                rc = __mdd_lookup_locked(env, pobj, lname, fid,
                                         MAY_WRITE | MAY_EXEC);
                if (rc != -ENOENT)
                        RETURN(rc ? : -EEXIST);
        } else {
                /*
                 * Check WRITE permission for the parent.
                 * EXEC permission have been checked
                 * when lookup before create already.
                 */
                rc = mdd_permission_internal_locked(env, obj, NULL, MAY_WRITE,
                                                    MOR_TGT_PARENT);
                if (rc)
                        RETURN(rc);
        }

        /* sgid check */
        rc = mdd_la_get(env, obj, la, BYPASS_CAPA);
        if (rc != 0)
                RETURN(rc);

        if (la->la_mode & S_ISGID) {
                ma->ma_attr.la_gid = la->la_gid;
                if (S_ISDIR(ma->ma_attr.la_mode)) {
                        ma->ma_attr.la_mode |= S_ISGID;
                        ma->ma_attr.la_valid |= LA_MODE;
                }
        }

        switch (ma->ma_attr.la_mode & S_IFMT) {
        case S_IFLNK: {
                unsigned int symlen = strlen(spec->u.sp_symname) + 1;

                if (symlen > (1 << m->mdd_dt_conf.ddp_block_shift))
                        RETURN(-ENAMETOOLONG);
                else
                        RETURN(0);
        }
        case S_IFDIR:
        case S_IFREG:
        case S_IFCHR:
        case S_IFBLK:
        case S_IFIFO:
        case S_IFSOCK:
                rc = 0;
                break;
        default:
                rc = -EINVAL;
                break;
        }
        RETURN(rc);
}


/*
 * Create object and insert it into namespace.
 */
static int mdd_create(const struct lu_env *env,
                      struct md_object *pobj,
                      const struct lu_name *lname,
                      struct md_object *child,
                      struct md_op_spec *spec,
                      struct md_attr* ma)
{
        struct mdd_thread_info *info = mdd_env_info(env);
        struct lu_attr         *la = &info->mti_la_for_fix;
        struct md_attr         *ma_acl = &info->mti_ma;
        struct mdd_object      *mdd_pobj = md2mdd_obj(pobj);
        struct mdd_object      *son = md2mdd_obj(child);
        struct mdd_device      *mdd = mdo2mdd(pobj);
        struct lu_attr         *attr = &ma->ma_attr;
        struct mdd_thandle     *handle;
        struct dynlock_handle  *dlh;
        const char             *name = lname->ln_name;
        int                     rc, got_def_acl = 0;
#ifdef HAVE_QUOTA_SUPPORT
        struct obd_device *obd = mdd->mdd_obd_dev;
        struct obd_export *exp = md_quota(env)->mq_exp;
        struct mds_obd *mds = &obd->u.mds;
        unsigned int qcids[MAXQUOTAS] = { 0, 0 };
        unsigned int qpids[MAXQUOTAS] = { 0, 0 };
        int quota_opc = 0, block_count = 0;
        int inode_pending[MAXQUOTAS] = { 0, 0 };
        int block_pending[MAXQUOTAS] = { 0, 0 };
        int parent_pending[MAXQUOTAS] = { 0, 0 };
#endif
        ENTRY;

        /*
         * Two operations have to be performed:
         *
         *  - an allocation of a new object (->do_create()), and
         *
         *  - an insertion into a parent index (->dio_insert()).
         *
         * Due to locking, operation order is not important, when both are
         * successful, *but* error handling cases are quite different:
         *
         *  - if insertion is done first, and following object creation fails,
         *  insertion has to be rolled back, but this operation might fail
         *  also leaving us with dangling index entry.
         *
         *  - if creation is done first, is has to be undone if insertion
         *  fails, leaving us with leaked space, which is neither good, nor
         *  fatal.
         *
         * It seems that creation-first is simplest solution, but it is
         * sub-optimal in the frequent
         *
         *         $ mkdir foo
         *         $ mkdir foo
         *
         * case, because second mkdir is bound to create object, only to
         * destroy it immediately.
         *
         * To avoid this follow local file systems that do double lookup:
         *
         *     0. lookup -> -EEXIST (mdd_create_sanity_check())
         *
         *     1. create            (mdd_object_create_internal())
         *
         *     2. insert            (__mdd_index_insert(), lookup again)
         */

        /* Sanity checks before big job. */
        rc = mdd_create_sanity_check(env, pobj, lname, ma, spec);
        if (rc)
                RETURN(rc);

#ifdef HAVE_QUOTA_SUPPORT
        if (mds->mds_quota) {
                struct lu_attr *la_tmp = &mdd_env_info(env)->mti_la;

                rc = mdd_la_get(env, mdd_pobj, la_tmp, BYPASS_CAPA);
                if (!rc) {
                        int same = 0;

                        quota_opc = FSFILT_OP_CREATE;
                        mdd_quota_wrapper(&ma->ma_attr, qcids);
                        mdd_quota_wrapper(la_tmp, qpids);
                        /* get file quota for child */
                        lquota_chkquota(mds_quota_interface_ref, obd, exp,
                                        qcids, inode_pending, 1, NULL, 0, NULL,
                                        0);
                        switch (ma->ma_attr.la_mode & S_IFMT) {
                        case S_IFLNK:
                        case S_IFDIR:
                                block_count = 2;
                                break;
                        case S_IFREG:
                                block_count = 1;
                                break;
                        }
                        if (qcids[CFS_USRQUOTA] == qpids[CFS_USRQUOTA] &&
                            qcids[CFS_GRPQUOTA] == qpids[CFS_GRPQUOTA]) {
                                block_count += 1;
                                same = 1;
                        }
                        /* get block quota for child and parent */
                        if (block_count)
                                lquota_chkquota(mds_quota_interface_ref, obd,
                                                exp, qcids, block_pending,
                                                block_count, NULL,
                                                LQUOTA_FLAGS_BLK, NULL, 0);
                        if (!same)
                                lquota_chkquota(mds_quota_interface_ref, obd,
                                                exp, qpids, parent_pending, 1,
                                                NULL, LQUOTA_FLAGS_BLK, NULL,
                                                0);
                }
        }
#endif

        dlh = mdd_pdo_write_lock(env, mdd_pobj, name, MOR_TGT_PARENT);
        if (dlh == NULL)
                RETURN(rc = -ENOMEM);
        mdd_write_lock(env, son, MOR_TGT_CHILD);

        LASSERT(mdd_object_exists(son) == 0);

        if (!S_ISLNK(attr->la_mode)) {
                ma_acl->ma_acl_size = sizeof info->mti_xattr_buf;
                ma_acl->ma_acl = info->mti_xattr_buf;
                ma_acl->ma_need = MA_ACL_DEF;
                ma_acl->ma_valid = 0;

                rc = mdd_def_acl_get(env, mdd_pobj, ma_acl);
                if (rc)
                        GOTO(out_unlock, rc);
                else if (ma_acl->ma_valid & MA_ACL_DEF)
                        got_def_acl = 1;
        }

        mdd_object_make_hint(env, mdd_pobj, son, attr);

        handle = mdd_tx_start(env, mdd);
        if (IS_ERR(handle))
                GOTO(out_free, rc = PTR_ERR(handle));

        mdd_object_create_internal(env, mdd_pobj, son, ma, handle, spec);

#ifdef CONFIG_FS_POSIX_ACL
        if (got_def_acl) {
                struct lu_buf *acl_buf = &info->mti_buf;
                acl_buf->lb_buf = ma_acl->ma_acl;
                acl_buf->lb_len = ma_acl->ma_acl_size;

                rc = __mdd_acl_init(env, son, acl_buf, &attr->la_mode, handle);
                if (rc == 0)
                        ma->ma_attr.la_valid |= LA_MODE;
        }
#endif

        mdd_object_initialize(env, mdo2fid(mdd_pobj), lname, son, ma, handle, spec);
        __mdd_index_insert(env, mdd_pobj, mdo2fid(son),
                           name, S_ISDIR(attr->la_mode), handle);

        if (S_ISLNK(attr->la_mode)) {
                const struct lu_buf *buf;

                buf = mdd_buf_get_const(env, spec->u.sp_symname,
                                        strlen(spec->u.sp_symname));
                mdd_tx_write(env, son, buf, 0, handle);
        }

        *la = ma->ma_attr;
        la->la_valid = LA_CTIME | LA_MTIME;
        mdd_attr_check_set_internal_locked(env, mdd_pobj, la, handle, 0);

        EXIT;

        mdd_changelog_ns_store(env, mdd,
                               S_ISDIR(attr->la_mode) ? CL_MKDIR :
                               S_ISREG(attr->la_mode) ? CL_CREATE :
                               S_ISLNK(attr->la_mode) ? CL_SOFTLINK : CL_MKNOD,
                               son, mdd_pobj, NULL, lname, handle);

        rc = mdd_tx_end(env, handle);

out_unlock:
        mdd_pdo_write_unlock(env, mdd_pobj, dlh);

        /* Return attr back. */
        if (rc == 0)
                rc = mdd_attr_get_internal(env, son, ma);

        mdd_write_unlock(env, son);

out_free:
#ifdef HAVE_QUOTA_SUPPORT
        if (quota_opc) {
                lquota_pending_commit(mds_quota_interface_ref, obd, qcids,
                                      inode_pending, 0);
                lquota_pending_commit(mds_quota_interface_ref, obd, qcids,
                                      block_pending, 1);
                lquota_pending_commit(mds_quota_interface_ref, obd, qpids,
                                      parent_pending, 1);
                /* Trigger dqacq on the owner of child and parent. If failed,
                 * the next call for lquota_chkquota will process it. */
                lquota_adjust(mds_quota_interface_ref, obd, qcids, qpids, rc,
                              quota_opc);
        }
#endif
        return rc;
}

/*
 * Get locks on parents in proper order
 * RETURN: < 0 - error, rename_order if successful
 */
enum rename_order {
        MDD_RN_SAME,
        MDD_RN_SRCTGT,
        MDD_RN_TGTSRC
};

static int mdd_rename_order(const struct lu_env *env,
                            struct mdd_device *mdd,
                            struct mdd_object *src_pobj,
                            struct mdd_object *tgt_pobj)
{
        /* order of locking, 1 - tgt-src, 0 - src-tgt*/
        int rc;
        ENTRY;

        if (src_pobj == tgt_pobj)
                RETURN(MDD_RN_SAME);

        /* compared the parent child relationship of src_p&tgt_p */
        if (lu_fid_eq(&mdd->mdd_root_fid, mdo2fid(src_pobj))){
                rc = MDD_RN_SRCTGT;
        } else if (lu_fid_eq(&mdd->mdd_root_fid, mdo2fid(tgt_pobj))) {
                rc = MDD_RN_TGTSRC;
        } else {
                rc = mdd_is_parent(env, mdd, src_pobj, mdo2fid(tgt_pobj), NULL);
                if (rc == -EREMOTE)
                        rc = 0;

                if (rc == 1)
                        rc = MDD_RN_TGTSRC;
                else if (rc == 0)
                        rc = MDD_RN_SRCTGT;
        }

        RETURN(rc);
}

/* has not mdd_write{read}_lock on any obj yet. */
static int mdd_rename_sanity_check(const struct lu_env *env,
                                   struct mdd_object *src_pobj,
                                   struct mdd_object *tgt_pobj,
                                   struct mdd_object *sobj,
                                   struct mdd_object *tobj,
                                   struct md_attr *ma)
{
        int rc = 0;
        ENTRY;

        if (unlikely(ma->ma_attr_flags & MDS_PERM_BYPASS))
                RETURN(0);

        /* XXX: when get here, sobj must NOT be NULL,
         * the other case has been processed in cml_rename
         * before mdd_rename and enable MDS_PERM_BYPASS. */
        LASSERT(sobj);

        rc = mdd_iattr_get(env, sobj, ma);
        if (rc)
                RETURN(rc);

        rc = mdd_may_delete(env, src_pobj, sobj, ma, 1, 0);
        if (rc)
                RETURN(rc);

        /* XXX: when get here, "tobj == NULL" means tobj must
         * NOT exist (neither on remote MDS, such case has been
         * processed in cml_rename before mdd_rename and enable
         * MDS_PERM_BYPASS).
         * So check may_create, but not check may_unlink. */
        if (!tobj)
                rc = mdd_may_create(env, tgt_pobj, NULL,
                                    (src_pobj != tgt_pobj), 0);
        else
                rc = mdd_may_delete(env, tgt_pobj, tobj, ma,
                                    (src_pobj != tgt_pobj), 1);

        if (!rc && !tobj && (src_pobj != tgt_pobj) &&
            S_ISDIR(ma->ma_attr.la_mode))
                rc = __mdd_may_link(env, tgt_pobj);

        RETURN(rc);
}

/* src object can be remote that is why we use only fid and type of object */
static int mdd_rename(const struct lu_env *env,
                      struct md_object *src_pobj, struct md_object *tgt_pobj,
                      const struct lu_fid *lf, const struct lu_name *lsname,
                      struct md_object *tobj, const struct lu_name *ltname,
                      struct md_attr *ma)
{
        const char *sname = lsname->ln_name;
        const char *tname = ltname->ln_name;
        struct lu_attr    *la = &mdd_env_info(env)->mti_la_for_fix;
        struct mdd_object *mdd_spobj = md2mdd_obj(src_pobj); /* source parent */
        struct mdd_object *mdd_tpobj = md2mdd_obj(tgt_pobj);
        struct mdd_device *mdd = mdo2mdd(src_pobj);
        struct mdd_object *mdd_sobj = NULL;                  /* source object */
        struct mdd_object *mdd_tobj = NULL;
        struct dynlock_handle *sdlh, *tdlh;
        struct mdd_thandle *handle;
        const struct lu_fid *tpobj_fid = mdo2fid(mdd_tpobj);
        const struct lu_fid *spobj_fid = mdo2fid(mdd_spobj);
        int is_dir, rc;

#ifdef HAVE_QUOTA_SUPPORT
        struct obd_device *obd = mdd->mdd_obd_dev;
        struct obd_export *exp = md_quota(env)->mq_exp;
        struct mds_obd *mds = &obd->u.mds;
        unsigned int qspids[MAXQUOTAS] = { 0, 0 };
        unsigned int qtcids[MAXQUOTAS] = { 0, 0 };
        unsigned int qtpids[MAXQUOTAS] = { 0, 0 };
        int quota_copc = 0, quota_popc = 0;
        int rec_pending[MAXQUOTAS] = { 0, 0 };
#endif
        ENTRY;

        if (tobj)
                mdd_tobj = md2mdd_obj(tobj);
        mdd_sobj = mdd_object_find(env, mdd, lf);
        /* object has to be locked by mdt, so it must exist */
        LASSERT(mdd_sobj);

#ifdef HAVE_QUOTA_SUPPORT
        if (mds->mds_quota) {
                struct lu_attr *la_tmp = &mdd_env_info(env)->mti_la;

                rc = mdd_la_get(env, mdd_spobj, la_tmp, BYPASS_CAPA);
                if (!rc) {
                        mdd_quota_wrapper(la_tmp, qspids);
                        if (!tobj) {
                                rc = mdd_la_get(env, mdd_tpobj, la_tmp,
                                                BYPASS_CAPA);
                                if (!rc) {
                                        void *data = NULL;
                                        mdd_data_get(env, mdd_tpobj, &data);
                                        quota_popc = FSFILT_OP_LINK;
                                        mdd_quota_wrapper(la_tmp, qtpids);
                                        /* get block quota for target parent */
                                        lquota_chkquota(mds_quota_interface_ref,
                                                        obd, exp, qtpids,
                                                        rec_pending, 1, NULL,
                                                        LQUOTA_FLAGS_BLK,
                                                        data, 1);
                                }
                        }
                }
        }
#endif
        /* FIXME: Should consider tobj and sobj too in rename_lock. */
        rc = mdd_rename_order(env, mdd, mdd_spobj, mdd_tpobj);
        if (rc < 0)
                GOTO(out_put, rc);

        /* Get locks in determined order */
        if (rc == MDD_RN_SAME) {
                sdlh = mdd_pdo_write_lock(env, mdd_spobj,
                                          sname, MOR_SRC_PARENT);
                /* check hashes to determine do we need one lock or two */
                /* XXX: mdo_pdo_write_lock() grabs whole-object lock now */
                if (0 && mdd_name2hash(sname) != mdd_name2hash(tname))
                        tdlh = mdd_pdo_write_lock(env, mdd_tpobj, tname,
                                MOR_TGT_PARENT);
                else
                        tdlh = sdlh;
        } else if (rc == MDD_RN_SRCTGT) {
                sdlh = mdd_pdo_write_lock(env, mdd_spobj, sname,MOR_SRC_PARENT);
                tdlh = mdd_pdo_write_lock(env, mdd_tpobj, tname,MOR_TGT_PARENT);
        } else {
                tdlh = mdd_pdo_write_lock(env, mdd_tpobj, tname,MOR_SRC_PARENT);
                sdlh = mdd_pdo_write_lock(env, mdd_spobj, sname,MOR_TGT_PARENT);
        }
        if (sdlh == NULL || tdlh == NULL)
                GOTO(unlock_parents, rc = -ENOMEM);

        /*
         * source object and target one may have few parents,
         * so strict orderind is required here as well
         */
        if (mdd_tobj) {
                if ((unsigned long) mdd_tobj > (unsigned long) mdd_sobj) {
                        mdd_write_lock(env, mdd_tobj, MOR_TGT_CHILD);
                        mdd_write_lock(env, mdd_sobj, MOR_TGT_CHILD);
                } else {
                        mdd_write_lock(env, mdd_sobj, MOR_TGT_CHILD);
                        mdd_write_lock(env, mdd_tobj, MOR_TGT_CHILD);
                }
        } else {
                mdd_write_lock(env, mdd_sobj, MOR_SRC_CHILD);
        }

        rc = mdd_rename_sanity_check(env, mdd_spobj, mdd_tpobj,
                                     mdd_sobj, mdd_tobj, ma);
        if (rc)
                GOTO(unlock, rc);
        is_dir = S_ISDIR(ma->ma_attr.la_mode);

        handle = mdd_tx_start(env, mdd);
        if (IS_ERR(handle))
                GOTO(unlock, rc = PTR_ERR(handle));

        /* Remove source name from source directory */
        __mdd_index_delete(env, mdd_spobj, sname, is_dir, handle);

        /* "mv dir1 dir2" needs "dir1/.." link update */
        if (is_dir && !lu_fid_eq(spobj_fid, tpobj_fid)) {
                mdd_tx_idx_delete(env, mdd_sobj, dotdot, handle);
                mdd_tx_idx_insert(env, mdd_sobj, tpobj_fid, dotdot, handle);
        }

        /* apply to target parent and target child */
        LASSERT(ma->ma_attr.la_valid & LA_CTIME);
        la->la_ctime = la->la_mtime = ma->ma_attr.la_ctime;
        la->la_valid = LA_CTIME;

        /* Remove old target object
         * For tobj is remote case cmm layer has processed
         * and set tobj to NULL then. So when tobj is NOT NULL,
         * it must be local one.
         */
        if (tobj && mdd_object_exists(mdd_tobj)) {
                /* Remove target name from target directory
                 * Here tobj can be remote one, so we do index_delete unconditionally
                 * and -ENOENT is allowed.
                 */
                __mdd_index_delete(env, mdd_tpobj, tname, is_dir, handle);

                if (mdd_is_dead_obj(mdd_tobj)) {
                        /* shld not be dead, something is wrong */
                        CERROR("tobj is dead, something is wrong\n");
                        mdd_tx_set_error(handle, -EINVAL);
                }
                __mdd_ref_del(env, mdd_tobj, handle, 0);

                /* Remove dot reference. */
                if (is_dir)
                        __mdd_ref_del(env, mdd_tobj, handle, 1);

                mdd_attr_check_set_internal(env, mdd_tobj, la, handle, 0);

                mdd_finish_unlink(env, mdd_tobj, ma, handle);

#ifdef HAVE_QUOTA_SUPPORT
                if (mds->mds_quota && ma->ma_valid & MA_INODE &&
                    ma->ma_attr.la_nlink == 0 && mdd_tobj->mod_count == 0) {
                        quota_copc = FSFILT_OP_UNLINK_PARTIAL_CHILD;
                        mdd_quota_wrapper(&ma->ma_attr, qtcids);
                }
#endif
        }

        /* Insert new fid with target name into target dir */
        __mdd_index_insert(env, mdd_tpobj, lf, tname, is_dir, handle);

        mdd_attr_check_set_internal_locked(env, mdd_sobj, la, handle, 0);

        la->la_valid = LA_CTIME | LA_MTIME;
        mdd_attr_check_set_internal_locked(env, mdd_spobj, la, handle, 0);
        if (mdd_spobj != mdd_tpobj)
                mdd_attr_check_set_internal_locked(env, mdd_tpobj, la, handle, 0);

        if (mdd_sobj) {
                rc = mdd_links_rename(env, mdd_sobj, mdo2fid(mdd_spobj), lsname,
                                      mdo2fid(mdd_tpobj), ltname, handle);
                if (rc == -ENOENT)
                        /* Old files might not have EA entry */
                        mdd_links_add(env, mdd_sobj, mdo2fid(mdd_spobj),
                                      lsname, handle);
                /* We don't fail the transaction if the link ea can't be
                   updated -- fid2path will use alternate lookup method. */
        }

        EXIT;

        mdd_changelog_ns_store(env, mdd, CL_RENAME, mdd_tobj, mdd_spobj,
                               lf, lsname, handle);
        mdd_changelog_ns_store(env, mdd, CL_EXT, mdd_tobj,
                               mdd_tpobj, lf, ltname, handle);

        rc = mdd_tx_end(env, handle);

unlock:
        if (mdd_tobj)
                mdd_write_unlock(env, mdd_tobj);
        mdd_write_unlock(env, mdd_sobj);

unlock_parents:
        if (likely(tdlh) && sdlh != tdlh)
                mdd_pdo_write_unlock(env, mdd_tpobj, tdlh);
        if (likely(sdlh))
                mdd_pdo_write_unlock(env, mdd_spobj, sdlh);

out_put:
        if (mdd_sobj)
                mdd_object_put(env, mdd_sobj);
#ifdef HAVE_QUOTA_SUPPORT
        if (mds->mds_quota) {
                if (quota_popc)
                        lquota_pending_commit(mds_quota_interface_ref, obd,
                                              qtpids, rec_pending, 1);

                if (quota_copc) {
                        /* Trigger dqrel on the source owner of parent.
                         * If failed, the next call for lquota_chkquota will
                         * process it. */
                        lquota_adjust(mds_quota_interface_ref, obd, 0, qspids, rc,
                                      FSFILT_OP_UNLINK_PARTIAL_PARENT);

                        /* Trigger dqrel on the target owner of child.
                         * If failed, the next call for lquota_chkquota
                         * will process it. */
                        lquota_adjust(mds_quota_interface_ref, obd, qtcids,
                                      qtpids, rc, quota_copc);
                }
        }
#endif
        return rc;
}

/** enable/disable storing of hardlink info */
/* XXX: disabled by default due to missed declaration (kDMU) */
int mdd_linkea_enable = 1;
CFS_MODULE_PARM(mdd_linkea_enable, "d", int, 0644,
                "record hardlink info in EAs");

/** Read the link EA into a temp buffer.
 * Uses the name_buf since it is generally large.
 * \retval IS_ERR err
 * \retval ptr to \a lu_buf (always \a mti_big_buf)
 */
struct lu_buf *mdd_links_get(const struct lu_env *env,
                             struct mdd_object *mdd_obj)
{
        struct lu_buf *buf;
        struct lustre_capa *capa;
        struct link_ea_header *leh;
        int rc;

        /* First try a small buf */
        buf = mdd_buf_alloc(env, CFS_PAGE_SIZE);
        if (buf->lb_buf == NULL)
                return ERR_PTR(-ENOMEM);

        if (!mdd_object_exists(mdd_obj))
                return ERR_PTR(-ENODATA);

        capa = mdd_object_capa(env, mdd_obj);
        rc = mdo_xattr_get(env, mdd_obj, buf, XATTR_NAME_LINK, capa);
        if (rc == -ERANGE) {
                /* Buf was too small, figure out what we need. */
                mdd_buf_put(buf);
                rc = mdo_xattr_get(env, mdd_obj, buf, XATTR_NAME_LINK, capa);
                if (rc < 0)
                        return ERR_PTR(rc);
                buf = mdd_buf_alloc(env, rc);
                if (buf->lb_buf == NULL)
                        return ERR_PTR(-ENOMEM);
                rc = mdo_xattr_get(env, mdd_obj, buf, XATTR_NAME_LINK, capa);
        }
        if (rc < 0)
                return ERR_PTR(rc);

        leh = buf->lb_buf;
        if (leh->leh_magic == __swab32(LINK_EA_MAGIC)) {
                leh->leh_magic = LINK_EA_MAGIC;
                leh->leh_reccount = __swab32(leh->leh_reccount);
                leh->leh_len = __swab64(leh->leh_len);
                /* entries are swabbed by mdd_lee_unpack */
        }
        if (leh->leh_magic != LINK_EA_MAGIC)
                return ERR_PTR(-EINVAL);
        if (leh->leh_reccount == 0)
                return ERR_PTR(-ENODATA);

        return buf;
}

/** Pack a link_ea_entry.
 * All elements are stored as chars to avoid alignment issues.
 * Numbers are always big-endian
 * \retval record length
 */
static int mdd_lee_pack(struct link_ea_entry *lee, const struct lu_name *lname,
                        const struct lu_fid *pfid)
{
        struct lu_fid   tmpfid;
        int             reclen;

        fid_cpu_to_be(&tmpfid, pfid);
        memcpy(&lee->lee_parent_fid, &tmpfid, sizeof(tmpfid));
        memcpy(lee->lee_name, lname->ln_name, lname->ln_namelen);
        reclen = sizeof(struct link_ea_entry) + lname->ln_namelen;

        lee->lee_reclen[0] = (reclen >> 8) & 0xff;
        lee->lee_reclen[1] = reclen & 0xff;
        return reclen;
}

void mdd_lee_unpack(const struct link_ea_entry *lee, int *reclen,
                    struct lu_name *lname, struct lu_fid *pfid)
{
        *reclen = (lee->lee_reclen[0] << 8) | lee->lee_reclen[1];
        memcpy(pfid, &lee->lee_parent_fid, sizeof(*pfid));
        fid_be_to_cpu(pfid, pfid);
        lname->ln_name = lee->lee_name;
        lname->ln_namelen = *reclen - sizeof(struct link_ea_entry);
}

/** Add a record to the end of link ea buf */
static int __mdd_links_add(const struct lu_env *env, struct lu_buf *buf,
                           const struct lu_fid *pfid,
                           const struct lu_name *lname)
{
        struct link_ea_header *leh;
        struct link_ea_entry *lee;
        int reclen;

        if (lname == NULL || pfid == NULL)
                return -EINVAL;

        /* Make sure our buf is big enough for the new one */
        leh = buf->lb_buf;
        reclen = lname->ln_namelen + sizeof(struct link_ea_entry);
        if (leh->leh_len + reclen > buf->lb_len) {
                if (mdd_buf_grow(env, leh->leh_len + reclen) < 0)
                        return -ENOMEM;
        }

        leh = buf->lb_buf;
        lee = buf->lb_buf + leh->leh_len;
        reclen = mdd_lee_pack(lee, lname, pfid);
        leh->leh_len += reclen;
        leh->leh_reccount++;
        return 0;
}

/* For pathologic linkers, we don't want to spend lots of time scanning the
 * link ea.  Limit ourseleves to something reasonable; links not in the EA
 * can be looked up via (slower) parent lookup.
 */
#define LINKEA_MAX_COUNT 128

static int mdd_links_add(const struct lu_env *env,
                         struct mdd_object *mdd_obj,
                         const struct lu_fid *pfid,
                         const struct lu_name *lname,
                         struct mdd_thandle *handle)
{
        struct lu_buf *buf;
        struct link_ea_header *leh;
        int rc;
        ENTRY;

        if (!mdd_linkea_enable)
                RETURN(0);

        buf = mdd_links_get(env, mdd_obj);
        if (IS_ERR(buf)) {
                rc = PTR_ERR(buf);
                if (rc != -ENODATA) {
                        CERROR("link_ea read failed %d "DFID"\n", rc,
                               PFID(mdd_object_fid(mdd_obj)));
                        RETURN (rc);
                }
                /* empty EA; start one */
                buf = mdd_buf_alloc(env, CFS_PAGE_SIZE);
                if (buf->lb_buf == NULL)
                        RETURN(-ENOMEM);
                leh = buf->lb_buf;
                leh->leh_magic = LINK_EA_MAGIC;
                leh->leh_len = sizeof(struct link_ea_header);
                leh->leh_reccount = 0;
        }

        leh = buf->lb_buf;
        if (leh->leh_reccount > LINKEA_MAX_COUNT)
                RETURN(-EOVERFLOW);

        rc = __mdd_links_add(env, buf, pfid, lname);
        if (rc)
                RETURN(rc);

        leh = buf->lb_buf;
        __mdd_xattr_set(env, mdd_obj,
                             mdd_buf_get_const(env, buf->lb_buf, leh->leh_len),
                             XATTR_NAME_LINK, 0, handle);

        /*
         * don't release buffers as it will be used in execution, mdd_tx_end()
         */

        RETURN (rc);
}

static int mdd_links_rename(const struct lu_env *env,
                            struct mdd_object *mdd_obj,
                            const struct lu_fid *oldpfid,
                            const struct lu_name *oldlname,
                            const struct lu_fid *newpfid,
                            const struct lu_name *newlname,
                            struct mdd_thandle *handle)
{
        struct lu_buf  *buf;
        struct link_ea_header *leh;
        struct link_ea_entry  *lee;
        struct lu_name *tmpname = &mdd_env_info(env)->mti_name;
        struct lu_fid  *tmpfid = &mdd_env_info(env)->mti_fid;
        int reclen = 0;
        int count;
        int rc = 0;
        ENTRY;

        if (!mdd_linkea_enable)
                RETURN(0);

        if (mdd_obj->mod_flags & DEAD_OBJ)
                /* No more links, don't bother */
                RETURN(0);

        buf = mdd_links_get(env, mdd_obj);
        if (IS_ERR(buf)) {
                rc = PTR_ERR(buf);
                if (rc == -ENODATA)
                        CDEBUG(D_INODE, "link_ea read failed %d "DFID"\n",
                               rc, PFID(mdd_object_fid(mdd_obj)));
                else
                        CERROR("link_ea read failed %d "DFID"\n",
                               rc, PFID(mdd_object_fid(mdd_obj)));
                /* see comment in mdd_rename() about this failed */
                rc = 0;
                RETURN(rc);
        }
        leh = buf->lb_buf;
        lee = (struct link_ea_entry *)(leh + 1); /* link #0 */

        /* Find the old record */
        for(count = 0; count < leh->leh_reccount; count++) {
                mdd_lee_unpack(lee, &reclen, tmpname, tmpfid);
                if (tmpname->ln_namelen == oldlname->ln_namelen &&
                    lu_fid_eq(tmpfid, oldpfid) &&
                    (strncmp(tmpname->ln_name, oldlname->ln_name,
                             tmpname->ln_namelen) == 0))
                        break;
                lee = (struct link_ea_entry *)((char *)lee + reclen);
        }
        if ((count + 1) > leh->leh_reccount) {
                CDEBUG(D_INODE, "Old link_ea name '%.*s' not found\n",
                       oldlname->ln_namelen, oldlname->ln_name);
                GOTO(out, rc = -ENOENT);
        }

        /* Remove the old record */
        leh->leh_reccount--;
        leh->leh_len -= reclen;
        memmove(lee, (char *)lee + reclen, (char *)leh + leh->leh_len -
                (char *)lee);

        /* If renaming, add the new record */
        if (newpfid != NULL) {
                /* if the add fails, we still delete the out-of-date old link */
                __mdd_links_add(env, buf, newpfid, newlname);
                leh = buf->lb_buf;
        }

        __mdd_xattr_set(env, mdd_obj,
                             mdd_buf_get_const(env, buf->lb_buf, leh->leh_len),
                             XATTR_NAME_LINK, 0, handle);

out:
        if (rc)
                CDEBUG(D_INODE, "link_ea mv/unlink '%.*s' failed %d "DFID"\n",
                       oldlname->ln_namelen, oldlname->ln_name, rc,
                       PFID(mdd_object_fid(mdd_obj)));

        /*
         * don't release buffers as it will be used in execution, mdd_tx_end()
         */
        RETURN (rc);
}

const struct md_dir_operations mdd_dir_ops = {
        .mdo_is_subdir     = mdd_is_subdir,
        .mdo_lookup        = mdd_lookup,
        .mdo_create        = mdd_create,
        .mdo_rename        = mdd_rename,
        .mdo_link          = mdd_link,
        .mdo_unlink        = mdd_unlink,
#if 0
        .mdo_name_insert   = mdd_name_insert,
        .mdo_name_remove   = mdd_name_remove,
        .mdo_rename_tgt    = mdd_rename_tgt,
#endif
        .mdo_create_data   = mdd_create_data
};
