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
 * Copyright (c) 2004, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#ifndef EXPORT_SYMTAB
# define EXPORT_SYMTAB
#endif
#define DEBUG_SUBSYSTEM S_LMV
#ifdef __KERNEL__
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <asm/div64.h>
#include <linux/seq_file.h>
#include <linux/namei.h>
# ifndef HAVE_VFS_INTENT_PATCHES
# include <linux/lustre_intent.h>
# endif
#else
#include <liblustre.h>
#endif

#include <obd_support.h>
#include <lustre/lustre_idl.h>
#include <lustre_lib.h>
#include <lustre_net.h>
#include <lustre_dlm.h>
#include <obd_class.h>
#include <lprocfs_status.h>
#include "lmv_internal.h"

int lmv_intent_remote(struct obd_export *exp, void *lmm,
                      int lmmsize, struct lookup_intent *it,
                      int flags, struct ptlrpc_request **reqp,
                      ldlm_blocking_callback cb_blocking,
                      int extra_lock_flags)
{
        struct obd_device      *obd = exp->exp_obd;
        struct lmv_obd         *lmv = &obd->u.lmv;
        struct ptlrpc_request  *req = NULL;
        struct lustre_handle    plock;
        struct md_op_data      *op_data;
        struct lmv_tgt_desc    *tgt;
        struct mdt_body        *body;
        int                     pmode;
        int                     rc = 0;
        ENTRY;

        body = req_capsule_server_get(&(*reqp)->rq_pill, &RMF_MDT_BODY);
        if (body == NULL)
                RETURN(-EPROTO);

        /*
         * Not cross-ref case, just get out of here.
         */
        if (!(body->valid & OBD_MD_MDS))
                RETURN(0);

        /*
         * Unfortunately, we have to lie to MDC/MDS to retrieve
         * attributes llite needs and provideproper locking.
         */
        if (it->it_op & IT_LOOKUP)
                it->it_op = IT_GETATTR;

        /*
         * We got LOOKUP lock, but we really need attrs.
         */
        pmode = it->d.lustre.it_lock_mode;
        if (pmode) {
                plock.cookie = it->d.lustre.it_lock_handle;
                it->d.lustre.it_lock_mode = 0;
                it->d.lustre.it_data = NULL;
        }

        LASSERT(fid_is_sane(&body->fid1));

        tgt = lmv_find_target(lmv, &body->fid1);
        if (IS_ERR(tgt))
                GOTO(out, rc = PTR_ERR(tgt));

        OBD_ALLOC_PTR(op_data);
        if (op_data == NULL)
                GOTO(out, rc = -ENOMEM);

        op_data->op_fid1 = body->fid1;
        op_data->op_bias = MDS_CROSS_REF;

        CDEBUG(D_INODE,
               "REMOTE_INTENT with fid="DFID" -> mds #%d\n",
               PFID(&body->fid1), tgt->ltd_idx);

        it->d.lustre.it_disposition &= ~DISP_ENQ_COMPLETE;
        rc = md_intent_lock(tgt->ltd_exp, op_data, lmm, lmmsize, it,
                            flags, &req, cb_blocking, extra_lock_flags);
        if (rc)
                GOTO(out_free_op_data, rc);

        /*
         * LLite needs LOOKUP lock to track dentry revocation in order to
         * maintain dcache consistency. Thus drop UPDATE lock here and put
         * LOOKUP in request.
         */
        if (it->d.lustre.it_lock_mode != 0) {
                ldlm_lock_decref((void *)&it->d.lustre.it_lock_handle,
                                 it->d.lustre.it_lock_mode);
                it->d.lustre.it_lock_mode = 0;
        }
        it->d.lustre.it_lock_handle = plock.cookie;
        it->d.lustre.it_lock_mode = pmode;

        EXIT;
out_free_op_data:
        OBD_FREE_PTR(op_data);
out:
        if (rc && pmode)
                ldlm_lock_decref(&plock, pmode);

        ptlrpc_req_finished(*reqp);
        *reqp = req;
        return rc;
}

struct lmv_stripe_md *lmv_get_lsm_from_req(struct obd_export *exp,
                                           struct ptlrpc_request *req)
{
        struct mdt_body         *body;
        struct lmv_mds_md       *lmm;
        struct lmv_stripe_md    *lsm = NULL;

        LASSERT(req != NULL);

        body = req_capsule_server_get(&req->rq_pill, &RMF_MDT_BODY);

        if (!body || !S_ISDIR(body->mode) || !body->eadatasize)
                return NULL;

        lmm = req_capsule_server_sized_get(&req->rq_pill, &RMF_MDT_MD,
                                           body->eadatasize);
        LASSERT(lmm != NULL);

        if (lmm->lmv_count == 0)
                return NULL;
        if( lmm->lmv_magic != LMV_MAGIC_V1)
                return NULL;

        lmv_unpack_md(exp, &lsm, lmm, 0);
        return lsm;
}

/*
 * IT_OPEN is intended to open (and create, possible) an object. Parent (pid)
 * may be split dir.
 */
int lmv_intent_open(struct obd_export *exp, struct md_op_data *op_data,
                    void *lmm, int lmmsize, struct lookup_intent *it,
                    int flags, struct ptlrpc_request **reqp,
                    ldlm_blocking_callback cb_blocking,
                    int extra_lock_flags)
{
        struct obd_device     *obd = exp->exp_obd;
        struct lu_fid          rpid = op_data->op_fid1;
        struct lmv_obd        *lmv = &obd->u.lmv;
        struct lmv_stripe_md  *lsm;
        struct lmv_tgt_desc   *tgt;
        struct mdt_body       *body;
        int                    rc;
        ENTRY;

        tgt = lmv_locate_mds(lmv, op_data, op_data->op_mea1,
                             &op_data->op_fid1);
        if (IS_ERR(tgt))
                RETURN(PTR_ERR(tgt));

        if (it->it_op & IT_CREAT) {
                /*
                 * For open with IT_CREATE and for IT_CREATE cases allocate new
                 * fid and setup FLD for it.
                 */
                op_data->op_fid3 = op_data->op_fid2;
                rc = lmv_fid_alloc(exp, &op_data->op_fid2, op_data);
                if (rc)
                        RETURN(rc);
        }

        CDEBUG(D_INODE,
               "OPEN_INTENT with fid1="DFID", fid2="DFID","
               " name='%s' -> mds #%d\n", PFID(&op_data->op_fid1),
               PFID(&op_data->op_fid2), op_data->op_name, tgt->ltd_idx);

        rc = md_intent_lock(tgt->ltd_exp, op_data, lmm, lmmsize, it, flags,
                            reqp, cb_blocking, extra_lock_flags);
        if (rc != 0)
                RETURN(rc);

        /*
         * Nothing is found, do not access body->fid1 as it is zero and thus
         * pointless.
         */
        if ((it->d.lustre.it_disposition & DISP_LOOKUP_NEG) &&
            !(it->d.lustre.it_disposition & DISP_OPEN_CREATE) &&
            !(it->d.lustre.it_disposition & DISP_OPEN_OPEN))
                RETURN(rc);

        /*
         * Okay, MDS has returned success. Probably name has been resolved in
         * remote inode.
         */
        rc = lmv_intent_remote(exp, lmm, lmmsize, it, flags, reqp,
                               cb_blocking, extra_lock_flags);
        if (rc != 0) {
                LASSERT(rc < 0);
                /*
                 * This is possible, that some userspace application will try to
                 * open file as directory and we will have -ENOTDIR here. As
                 * this is normal situation, we should not print error here,
                 * only debug info.
                 */
                CDEBUG(D_INODE, "Can't handle remote %s: dir "DFID"("DFID"):"
                       "%*s: %d\n", LL_IT2STR(it), PFID(&op_data->op_fid2),
                       PFID(&rpid), op_data->op_namelen, op_data->op_name, rc);
                RETURN(rc);
        }

        /*
         * Caller may use attrs MDS returns on IT_OPEN lock request so, we have
         * to update them for split dir.
         */
        body = req_capsule_server_get(&(*reqp)->rq_pill, &RMF_MDT_BODY);
        LASSERT(body != NULL);

        if (!(body->valid & OBD_MD_FLID))
                RETURN(rc);

        lsm = lmv_get_lsm_from_req(exp, *reqp);
        if (lsm != NULL) {
                rc = lmv_revalidate_slaves(exp, reqp, lsm, it, 1,
                                           cb_blocking, extra_lock_flags);
                lmv_free_memmd(lsm);
        }
        RETURN(rc);
}

/*
 * Handler for: getattr, lookup and revalidate cases.
 */
int lmv_intent_lookup(struct obd_export *exp, struct md_op_data *op_data,
                      void *lmm, int lmmsize, struct lookup_intent *it,
                      int flags, struct ptlrpc_request **reqp,
                      ldlm_blocking_callback cb_blocking,
                      int extra_lock_flags)
{
        struct obd_device      *obd = exp->exp_obd;
        struct lmv_obd         *lmv = &obd->u.lmv;
        struct lmv_tgt_desc    *tgt = NULL;
        struct lmv_stripe_md   *lsm;
        struct mdt_body        *body;
        int                     rc = 0;
        ENTRY;

        tgt = lmv_locate_mds(lmv, op_data, op_data->op_mea1,
                             &op_data->op_fid1);
        if (IS_ERR(tgt))
                RETURN(PTR_ERR(tgt));

        if (!fid_is_sane(&op_data->op_fid2))
                fid_zero(&op_data->op_fid2);

        CDEBUG(D_INODE,
               "LOOKUP_INTENT with fid1="DFID", fid2="DFID
               ", name='%s' -> mds #%d\n",
               PFID(&op_data->op_fid1), PFID(&op_data->op_fid2),
               op_data->op_name ? op_data->op_name : "<NULL>",
               tgt->ltd_idx);

        op_data->op_bias &= ~MDS_CROSS_REF;

        rc = md_intent_lock(tgt->ltd_exp, op_data, lmm, lmmsize, it,
                            flags, reqp, cb_blocking, extra_lock_flags);
        if (rc < 0)
                RETURN(rc);
#if 0
        if (op_data->op_mea1 && rc > 0) {
                /*
                 * This is split dir. In order to optimize things a bit, we
                 * consider obj valid updating missing parts.
                 */
                CDEBUG(D_INODE,
                       "Revalidate slaves for "DFID", rc %d\n",
                       PFID(&op_data->op_fid1), rc);

                LASSERT(fid_is_sane(&op_data->op_fid2));
                lmv_revalidate_slaves(exp, reqp, op_data->op_mea1, it, rc,
                                      cb_blocking, extra_lock_flags);
                RETURN(rc);
        }
#endif
        if (*reqp == NULL)
                RETURN(rc);

        /*
         * MDS has returned success. Probably name has been resolved in
         * remote inode. Let's check this.
         */
        rc = lmv_intent_remote(exp, lmm, lmmsize, it, flags,
                               reqp, cb_blocking, extra_lock_flags);
        if (rc < 0)
                RETURN(rc);
        /*
         * Nothing is found, do not access body->fid1 as it is zero and thus
         * pointless.
         */
        if (it->d.lustre.it_disposition & DISP_LOOKUP_NEG)
                RETURN(0);

        LASSERT(*reqp != NULL);
        LASSERT((*reqp)->rq_repmsg != NULL);
        body = req_capsule_server_get(&(*reqp)->rq_pill, &RMF_MDT_BODY);
        LASSERT(body != NULL);

        /*
         * Could not find object, FID is not present in response.
         */
        if (!(body->valid & OBD_MD_FLID))
                RETURN(0);

        lsm = lmv_get_lsm_from_req(exp, *reqp);
        if (lsm != NULL) {
                rc = lmv_revalidate_slaves(exp, reqp, lsm, it, 1,
                                           cb_blocking, extra_lock_flags);
                lmv_free_memmd(lsm);
        }
        RETURN(rc);
}

int lmv_intent_lock(struct obd_export *exp, struct md_op_data *op_data,
                    void *lmm, int lmmsize, struct lookup_intent *it,
                    int flags, struct ptlrpc_request **reqp,
                    ldlm_blocking_callback cb_blocking,
                    int extra_lock_flags)
{
        struct obd_device *obd = exp->exp_obd;
        int                rc;
        ENTRY;

        LASSERT(it != NULL);
        LASSERT(fid_is_sane(&op_data->op_fid1));

        CDEBUG(D_INODE, "INTENT LOCK '%s' for '%*s' on "DFID"\n",
               LL_IT2STR(it), op_data->op_namelen, op_data->op_name,
               PFID(&op_data->op_fid1));

        rc = lmv_check_connect(obd);
        if (rc)
                RETURN(rc);

        if (it->it_op & (IT_LOOKUP | IT_GETATTR))
                rc = lmv_intent_lookup(exp, op_data, lmm, lmmsize, it,
                                       flags, reqp, cb_blocking,
                                       extra_lock_flags);
        else if (it->it_op & IT_OPEN)
                rc = lmv_intent_open(exp, op_data, lmm, lmmsize, it,
                                     flags, reqp, cb_blocking,
                                     extra_lock_flags);
        else if (it->it_op & IT_READDIR)
                rc = lmv_readdir_lock(exp, op_data, lmm, lmmsize, it,
                                      flags, reqp, cb_blocking,
                                      extra_lock_flags);
        else
                LBUG();
        RETURN(rc);
}

int lmv_revalidate_slaves(struct obd_export *exp, struct ptlrpc_request **reqp,
                          struct lmv_stripe_md *lsm, struct lookup_intent *oit,
                          int master_valid, ldlm_blocking_callback cb_blocking,
                          int extra_lock_flags)
{
        struct obd_device      *obd = exp->exp_obd;
        struct lmv_obd         *lmv = &obd->u.lmv;
        int                     master_lockm = 0;
        struct lustre_handle   *lockh = NULL;
        struct ptlrpc_request  *mreq = *reqp;
        struct lustre_handle    master_lockh = { 0 };
        struct md_op_data      *op_data;
        unsigned long           size = 0;
        struct mdt_body        *body;
        int                     i;
        int                     rc = 0;
        struct lu_fid           fid;
        struct lu_fid           *mid = &lsm->mea_oinfo[0].lmo_fid;
        struct ptlrpc_request  *req;
        struct lookup_intent    it;
        struct lmv_tgt_desc    *tgt;
        ENTRY;

        /**
         * revalidate slaves has some problems, temporarily return,
         * we may not need that
         */
        if (lsm->mea_count <= 1)
                RETURN(0);

        CDEBUG(D_INODE, "Revalidate master obj "DFID"\n", PFID(mid));
        OBD_ALLOC_PTR(op_data);
        if (op_data == NULL)
                RETURN(-ENOMEM);

        /**
         * Loop over the stripe information, check validity and update them
         * from MDS if needed.
         */
        for (i = 0; i < lsm->mea_count; i++) {
                fid = lsm->mea_oinfo[i].lmo_fid;
                /*
                 * We need i_size and we would like to check possible cached locks,
                 * so this is is IT_GETATTR intent.
                 */
                memset(&it, 0, sizeof(it));
                it.it_op = IT_GETATTR;
                req = NULL;

                if (master_valid && i == 0) {
                        /*
                         * lmv_intent_lookup() already checked
                         * validness and took the lock.
                         */
                        if (mreq != NULL) {
                                body = req_capsule_server_get(&mreq->rq_pill,
                                                              &RMF_MDT_BODY);
                                LASSERT(body != NULL);
                                goto update;
                        }
                        /*
                         * Take already cached attrs into account.
                         */
                        CDEBUG(D_INODE,
                               "Master "DFID"is locked and cached\n",
                               PFID(mid));
                        goto release_lock;
                }

                /*
                 * Prepare op_data for revalidating. Note that @fid2 shuld be
                 * defined otherwise it will go to server and take new lock
                 * which is what we reall not need here.
                 */
                memset(op_data, 0, sizeof(*op_data));
                op_data->op_bias = MDS_CROSS_REF;
                op_data->op_fid1 = fid;
                op_data->op_fid2 = fid;

                tgt = lmv_locate_mds(lmv, op_data, NULL, &fid);
                if (IS_ERR(tgt))
                        GOTO(cleanup, rc = PTR_ERR(tgt));

                CDEBUG(D_INODE, "Revalidate slave "DFID" -> mds #%d\n",
                       PFID(&fid), tgt->ltd_idx);

                rc = md_intent_lock(tgt->ltd_exp, op_data, NULL, 0, &it, 0,
                                    &req, cb_blocking, extra_lock_flags);

                lockh = (struct lustre_handle *)&it.d.lustre.it_lock_handle;
                if (rc > 0 && req == NULL) {
                        /*
                         * Nice, this slave is valid.
                         */
                        CDEBUG(D_INODE, "Cached slave "DFID"\n", PFID(&fid));
                        goto release_lock;
                }

                if (rc < 0)
                        GOTO(cleanup, rc);

                if (i == 0) { /* master object */
                        /*
                         * Save lock on master to be returned to the caller.
                         */
                        CDEBUG(D_INODE, "No lock on master "DFID" yet\n",
                               PFID(mid));
                        memcpy(&master_lockh, lockh, sizeof(master_lockh));
                        master_lockm = it.d.lustre.it_lock_mode;
                        it.d.lustre.it_lock_mode = 0;
                }

                if (*reqp == NULL) {
                        /*
                         * This is first reply, we'll use it to return updated
                         * data back to the caller.
                         */
                        LASSERT(req != NULL);
                        ptlrpc_request_addref(req);
                        *reqp = req;
                }

                body = req_capsule_server_get(&req->rq_pill, &RMF_MDT_BODY);
                LASSERT(body != NULL);

update:
                lsm->mea_oinfo[i].lmo_size = body->size;

                CDEBUG(D_INODE, "Fresh size %lu from "DFID"\n",
                       (unsigned long)lsm->mea_oinfo[i].lmo_size, PFID(&fid));

                if (req)
                        ptlrpc_req_finished(req);
release_lock:
                size += lsm->mea_oinfo[i].lmo_size;

                if (it.d.lustre.it_lock_mode && lockh) {
                        ldlm_lock_decref(lockh, it.d.lustre.it_lock_mode);
                        it.d.lustre.it_lock_mode = 0;
                }
        }

        if (*reqp) {
                /*
                 * Some attrs got refreshed, we have reply and it's time to put
                 * fresh attrs to it.
                 */
                CDEBUG(D_INODE, "Return refreshed attrs: size = %lu for "DFID"\n",
                       (unsigned long)size, PFID(mid));

                body = req_capsule_server_get(&(*reqp)->rq_pill, &RMF_MDT_BODY);
                LASSERT(body != NULL);
                body->size = size;
                body->fid1 = lsm->mea_oinfo[0].lmo_fid;

                if (mreq == NULL) {
                        /*
                         * Very important to maintain mds num the same because
                         * of revalidation. mreq == NULL means that caller has
                         * no reply and the only attr we can return is size.
                         */
                        body->valid = OBD_MD_FLSIZE;
                }
                if (master_valid == 0 && oit != NULL) {
                        oit->d.lustre.it_lock_handle = master_lockh.cookie;
                        oit->d.lustre.it_lock_mode = master_lockm;
                }
                rc = 0;
        } else {
                /*
                 * It seems all the attrs are fresh and we did no request.
                 */
                CDEBUG(D_INODE, "All the attrs were fresh on "DFID"\n",
                       PFID(mid));
                if (master_valid == 0 && oit != NULL)
                        oit->d.lustre.it_lock_mode = master_lockm;
                rc = 1;
        }

        EXIT;
cleanup:
        OBD_FREE_PTR(op_data);
        return rc;
}

