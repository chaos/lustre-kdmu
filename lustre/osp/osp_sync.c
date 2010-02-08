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
 * lustre/osp/osp_sync.c
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

/*
 * all changes OST is interested in are added to appropriate llog file
 * and OSP register a callback on corresponded transaction
 * once transaction is committed, a command is added to "ready-to-execute"
 * queue. RPC bulks to OST are formed from the queue. once RPC is reported
 * committed (via last committed transno), corresponded llog records are
 * cancelled.
 * 
 * at the startup OSP scans existing llog files and fills "ready-to-execute"
 * queue again.
 *
 * in case of failed OST queue can become too large to store in a memory.
 * we should be able to discard queue if OST is unresponsive and re-fill
 * the queue from the llog upon reconnect
 */

/* XXX: do math to learn reasonable threshold
 * should it be ~ number of changes fitting bulk? */
#define OSP_SYN_THRESHOLD       3
#define OSP_MAX_IN_FLIGHT       5
#define OSP_MAX_IN_PROGRESS     3

/*
 * Job is a job.
 *
 * Job contains request to OST, set of llog cookies corresponding the request
 */
struct osp_sync_job {
        cfs_list_t             osj_list;
        struct ptlrpc_request *osj_req;
        struct llog_cookie     osj_cookie;
        unsigned long          osj_magic;
};

#define OSP_JOB_MAGIC         0x26112005

 inline int osp_sync_running(struct osp_device *d)
{
        return !!(d->opd_syn_thread.t_flags & SVC_RUNNING);
}

 inline int osp_sync_stopped(struct osp_device *d)
{
        return !!(d->opd_syn_thread.t_flags & SVC_STOPPED);
}

 inline int osp_sync_has_new_job(struct osp_device *d)
{
        return d->opd_syn_changes > OSP_SYN_THRESHOLD || !d->opd_syn_prev_done;
}

 inline int osp_sync_low_in_progress(struct osp_device *d)
{
        return d->opd_syn_rpc_in_progress < OSP_MAX_IN_PROGRESS;
}

 inline int osp_sync_has_work(struct osp_device *d)
{
        /* has locally committed and low in-flight? */
        if (!cfs_list_empty(&d->opd_syn_committed_here)
                        && d->opd_syn_rpc_in_flight < OSP_MAX_IN_FLIGHT)
                return 1;

        /* has new/old changes and low in-progress? */
        if (osp_sync_has_new_job(d) && osp_sync_low_in_progress(d))
                return 1;

        /* has remotely committed? */
        if (!cfs_list_empty(&d->opd_syn_committed_there))
                return 1;

        return 0;
}

 inline int osp_sync_can_process_new(struct osp_device *d)
{
        return (osp_sync_low_in_progress(d) && osp_sync_has_new_job(d));
}

int osp_sync_declare_add(const struct lu_env *env, struct osp_object *o,
                         llog_op_type type, struct thandle *th)
{
        struct osp_device   *d = lu2osp_dev(o->opo_obj.do_lu.lo_dev);
        struct obd_device   *obd = d->opd_obd;
        struct llog_ctxt    *ctxt;
        struct llog_rec_hdr  hdr;
        int                  rc;
        ENTRY;

        switch (type) {
                case MDS_UNLINK_REC:
                        hdr.lrh_len = sizeof(struct llog_unlink_rec);
                        break;

                case MDS_SETATTR64_REC:
                        hdr.lrh_len = sizeof(struct llog_setattr64_rec);
                        break;

                default:
                        LBUG();
        }

        ctxt = llog_get_context(obd, LLOG_MDS_OST_ORIG_CTXT);
        LASSERT(ctxt);
        rc = llog_declare_add_2(ctxt, &hdr, NULL, th);
        llog_ctxt_put(ctxt);

        RETURN(rc);

}

int osp_sync_add(const struct lu_env *env, struct osp_object *o,
                 llog_op_type type, struct thandle *th)
{
        struct osp_device   *d = lu2osp_dev(o->opo_obj.do_lu.lo_dev);
        const struct lu_fid *fid = lu_object_fid(&o->opo_obj.do_lu);
        struct obd_device   *obd = d->opd_obd;
        struct llog_cookie  cookie;
        struct llog_ctxt    *ctxt;
        union {
                struct llog_unlink_rec          unlink;
                struct llog_setattr64_rec       setattr;
                struct llog_rec_hdr             hdr;
        } u;
        int                  rc;
        ENTRY;

        switch (type) {
                case MDS_UNLINK_REC:
                        u.unlink.lur_hdr.lrh_len = sizeof(u.unlink);
                        u.unlink.lur_hdr.lrh_type = MDS_UNLINK_REC;
                        u.unlink.lur_oid = lu_idif_id(fid);
                        u.unlink.lur_ogr = lu_idif_gr(fid);
                        u.unlink.lur_count = 1;
                        break;

                case MDS_SETATTR64_REC:
                        u.setattr.lsr_hdr.lrh_len = sizeof(u.setattr);
                        u.setattr.lsr_hdr.lrh_type = MDS_SETATTR64_REC;
                        u.setattr.lsr_oid = lu_idif_id(fid);
                        u.setattr.lsr_ogr = lu_idif_gr(fid);
                        break;

                default:
                        LBUG();
        }

        ctxt = llog_get_context(obd, LLOG_MDS_OST_ORIG_CTXT);
        LASSERT(ctxt);
        rc = llog_add_2(ctxt, &u.hdr, NULL, &cookie, 1, th);
        llog_ctxt_put(ctxt);

        /*printk("new record %lu:%lu:%lu/%lu: %d\n",
                (unsigned long) cookie.lgc_lgl.lgl_oid,
                (unsigned long) cookie.lgc_lgl.lgl_ogr,
                (unsigned long) cookie.lgc_lgl.lgl_ogen,
                (unsigned long) cookie.lgc_index, rc);*/

        if (rc > 0)
                rc = 0;

        if (likely(rc == 0)) {
                cfs_spin_lock(&d->opd_syn_lock);
                d->opd_syn_changes++;
                cfs_spin_unlock(&d->opd_syn_lock);

                if (osp_sync_has_work(d))
                        cfs_waitq_signal(&d->opd_syn_waitq);
        }

        RETURN(rc);

}

/*
 * it's quite obvious we can't maintain all the structures in the memory:
 * while OST is down, MDS can be processing thousands and thosands unlinks
 * filling persistent llogs and in-core respresentation
 *
 * this don't scale at all. so we need basically the following:
 * a) destroy/setattr append llog records
 * b) once llog has grown to X records, we process first Y committed records
 * 
 *  once record R is found via llog_process(), it becomes committed after any
 *  subsequent commit callback (at the most)
 */

/*
 * called for each atomic on-disk change (not once per transaction batch)
 * and goes over the list
 * XXX: should be optimized?
 */
 int osp_sync_txn_commit_cb(const struct lu_env *env,
                                  struct thandle *th,
                                  void *cookie)
{
        struct osp_device *d = cookie;
        cfs_list_t         list;
        
        /* even in case of some race there will be another commit callback
         * probably except the last one at umount ? */
        if (cfs_list_empty(&d->opd_syn_waiting_for_commit))
                return 0;

        /*
         *  All jobs listed on opd_syn_waiting_for_commit can be started now
         */
        CFS_INIT_LIST_HEAD(&list);
        cfs_spin_lock(&d->opd_syn_lock);
        cfs_list_splice(&d->opd_syn_waiting_for_commit,
                        &d->opd_syn_committed_here);
        CFS_INIT_LIST_HEAD(&d->opd_syn_waiting_for_commit);
        cfs_spin_unlock(&d->opd_syn_lock);

        if (osp_sync_has_work(d))
                cfs_waitq_signal(&d->opd_syn_waitq);

        return 0;
}

/**
 * called for each RPC reported committed
 */
 void osp_sync_request_commit_cb(struct ptlrpc_request *req)
{
        struct osp_device   *d = req->rq_cb_data;
        struct osp_sync_job *j;

        j = (struct osp_sync_job *) &req->rq_async_args;
        LASSERT(d);
        LASSERT(j->osj_magic == OSP_JOB_MAGIC);
        LASSERT(j->osj_magic == OSP_JOB_MAGIC);
        LASSERT(cfs_list_empty(&j->osj_list));

        cfs_spin_lock(&d->opd_syn_lock);
        cfs_list_add(&j->osj_list, &d->opd_syn_committed_there);
        cfs_spin_unlock(&d->opd_syn_lock);
        
        /* XXX: some batching wouldn't hurt */
        cfs_waitq_signal(&d->opd_syn_waitq);
}

 int osp_sync_interpret(const struct lu_env *env,
                              struct ptlrpc_request *req,
                              void *aa, int rc)
{
        struct osp_device    *d = req->rq_cb_data;
        struct osp_sync_job  *j;
       
        j = (struct osp_sync_job *) &req->rq_async_args;
        LASSERT(j->osj_magic == OSP_JOB_MAGIC);
        LASSERT(d);

        LASSERT(d->opd_syn_rpc_in_flight > 0);
        cfs_spin_lock(&d->opd_syn_lock);
        d->opd_syn_rpc_in_flight--;
        cfs_spin_unlock(&d->opd_syn_lock);
       
        if (osp_sync_has_work(d))
                cfs_waitq_signal(&d->opd_syn_waitq);

        return 0; 
}

 struct osp_sync_job *osp_sync_new_job(struct osp_device *d,
                                    struct llog_handle *llh,
                                    struct llog_rec_hdr *h,
                                    ost_cmd_t op,
                                    const struct req_format *format)
{
        struct ptlrpc_request  *req;
        struct obd_import      *imp;
        struct osp_sync_job    *j;
        int                     rc;

        /*
         * Prepare the request
         */
        imp = d->opd_obd->u.cli.cl_import;
        LASSERT(imp);
        req = ptlrpc_request_alloc(imp, format);
        if (req == NULL)
                RETURN(ERR_PTR(-ENOMEM));

        rc = ptlrpc_request_pack(req, LUSTRE_OST_VERSION, op);
        if (rc) {
                ptlrpc_req_finished(req);
                return(ERR_PTR(rc));
        }

        j = (struct osp_sync_job *) &req->rq_async_args;
        j->osj_magic = OSP_JOB_MAGIC;
        CFS_INIT_LIST_HEAD(&j->osj_list);
        j->osj_cookie.lgc_lgl = llh->lgh_id;
        j->osj_cookie.lgc_subsys = LLOG_MDS_OST_ORIG_CTXT;
        j->osj_cookie.lgc_index = h->lrh_index;
        j->osj_req = req;

        ptlrpc_request_addref(req);
        req->rq_interpret_reply = osp_sync_interpret;
        req->rq_commit_cb = osp_sync_request_commit_cb;
        req->rq_cb_data = d;

        ptlrpc_request_set_replen(req);

        return j;
}

 void osp_sync_add_job(struct osp_sync_job *j, struct osp_device *d)
{
        LASSERT(j);
        LASSERT(j->osj_magic = OSP_JOB_MAGIC);

        /*
         * Put the job on a list where it'll be awaiting commit notify
         * meaning corresponded local changes are committed and we can
         * start sync with OST
         * also maintain counter of changes being under sync, so that
         * we don't start processing them again till they get cancelled
         */
        LASSERT(cfs_list_empty(&j->osj_list));
        cfs_spin_lock(&d->opd_syn_lock);
        cfs_list_add(&j->osj_list, &d->opd_syn_waiting_for_commit);
        d->opd_syn_sync_in_progress++;
        cfs_spin_unlock(&d->opd_syn_lock);

}

 int osp_sync_new_setattr_job(struct osp_device *d,
                                    struct llog_handle *llh,
                                    struct llog_rec_hdr *h)
{
        struct llog_setattr64_rec *rec = (struct llog_setattr64_rec *) h;
        struct osp_sync_job       *j;
        struct ost_body           *body;

        LASSERT(h->lrh_type == MDS_SETATTR64_REC);

        j = osp_sync_new_job(d, llh, h, OST_SETATTR, &RQF_OST_SETATTR);
        if (IS_ERR(j))
                RETURN(PTR_ERR(j));

        body = req_capsule_client_get(&j->osj_req->rq_pill, &RMF_OST_BODY);
        LASSERT(body);
        body->oa.o_id  = rec->lsr_oid;
        body->oa.o_gr  = rec->lsr_ogr;
        body->oa.o_uid = rec->lsr_uid; /* XXX: what about hi 32bit? */
        body->oa.o_gid = rec->lsr_gid; /* XXX: what about hi 32bit? */
        body->oa.o_valid = OBD_MD_FLGROUP | OBD_MD_FLID |
                           OBD_MD_FLUID | OBD_MD_FLGID;

        osp_sync_add_job(j, d);

        RETURN(0);
}

 int osp_sync_new_unlink_job(struct osp_device *d,
                                    struct llog_handle *llh,
                                    struct llog_rec_hdr *h)
{
        struct llog_unlink_rec *rec = (struct llog_unlink_rec *) h;
        struct osp_sync_job    *j;
        struct ost_body        *body;

        LASSERT(h->lrh_type == MDS_UNLINK_REC);

        j = osp_sync_new_job(d, llh, h, OST_DESTROY, &RQF_OST_DESTROY);
        if (IS_ERR(j))
                RETURN(PTR_ERR(j));

        body = req_capsule_client_get(&j->osj_req->rq_pill, &RMF_OST_BODY);
        LASSERT(body);
        body->oa.o_id = rec->lur_oid;
        body->oa.o_gr = rec->lur_ogr;
        body->oa.o_valid = OBD_MD_FLGROUP | OBD_MD_FLID;

        osp_sync_add_job(j, d);

        RETURN(0);
}

 int osp_sync_process_record(struct osp_device *d,
                                   struct llog_handle *llh,
                                   struct llog_rec_hdr *rec)
{
        struct llog_cookie  cookie;
        int                 rc = 0;

        cookie.lgc_lgl = llh->lgh_id;
        cookie.lgc_subsys = LLOG_MDS_OST_ORIG_CTXT;
        cookie.lgc_index = rec->lrh_index;
               
        if (unlikely(rec->lrh_type == LLOG_GEN_REC)) {
                struct llog_gen_rec *gen = (struct llog_gen_rec *) rec;
                /* we're waiting for the record generated by this instance */
                LASSERT(d->opd_syn_prev_done == 0);
                if (!memcmp(&d->opd_syn_generation, &gen->lgr_gen,
                                sizeof(gen->lgr_gen))) {
                        CDEBUG(D_HA, "processed all old entries\n");
                        d->opd_syn_prev_done = 1;
                }

                /* cancel any generation record */
                rc = llog_cat_cancel_records(llh->u.phd.phd_cat_handle, 1, &cookie);
                
                return rc;
        }

        /*
         * now we prepare and fill requests to OST, put them on the queue
         * and fire after next commit callback
         */
       
        switch (rec->lrh_type) {
                case MDS_UNLINK_REC:
                        rc = osp_sync_new_unlink_job(d, llh, rec);
                        LASSERTF(rc == 0, "%d\n", rc);
                        break;

                case MDS_SETATTR64_REC:
                        rc = osp_sync_new_setattr_job(d, llh, rec);
                        break;

                default:
                        CERROR("unknown record type: %x\n", rec->lrh_type);
                        break;
        }

        cfs_spin_lock(&d->opd_syn_lock);
        if (d->opd_syn_prev_done) {
                LASSERT(d->opd_syn_changes > 0); 
                d->opd_syn_changes--;
        }
        d->opd_syn_rpc_in_progress++;
        cfs_spin_unlock(&d->opd_syn_lock);

        CDEBUG(D_HA, "found record %x, %d, idx %u: %d\n",
                rec->lrh_type, rec->lrh_len, rec->lrh_index, rc);
        return 0;
}

 void osp_sync_process_committed(struct osp_device *d)
{
        struct obd_device    *obd = d->opd_obd;
        struct osp_sync_job  *j, *tmp;
        struct llog_ctxt     *ctxt;
        struct llog_handle   *llh;
        cfs_list_t            list;
        int                   rc, done = 0;
        ENTRY;

        if (cfs_list_empty(&d->opd_syn_committed_there))
                return;

        /*
         * now cancel them all
         * XXX: can we improve this using some batching?
         *      with batch RPC that'll happen automatically?
         * XXX: can we store ctxt in lod_device and save few cycles ?
         */
        ctxt = llog_get_context(obd, LLOG_MDS_OST_ORIG_CTXT);
        LASSERT(ctxt);

        llh = ctxt->loc_handle;
        LASSERT(llh);

        CFS_INIT_LIST_HEAD(&list);
        cfs_spin_lock(&d->opd_syn_lock);
        cfs_list_splice(&d->opd_syn_committed_there, &list);
        CFS_INIT_LIST_HEAD(&d->opd_syn_committed_there);
        cfs_spin_unlock(&d->opd_syn_lock);

        cfs_list_for_each_entry_safe(j, tmp, &list, osj_list) {
               
                LASSERT(j->osj_magic == OSP_JOB_MAGIC);
                cfs_list_del_init(&j->osj_list);

                rc = llog_cat_cancel_records(llh, 1, &j->osj_cookie);
                if (rc)
                        CERROR("can't cancel record: %d\n", rc);
                
                ptlrpc_req_finished(j->osj_req);
                done++;
        }
        
        llog_ctxt_put(ctxt);

        //printk("%d changes synced\n", done);
        LASSERT(d->opd_syn_rpc_in_progress >= done);
        cfs_spin_lock(&d->opd_syn_lock);
        d->opd_syn_rpc_in_progress -= done;
        cfs_spin_unlock(&d->opd_syn_lock);

        if (osp_sync_has_work(d))
                cfs_waitq_signal(&d->opd_syn_waitq);

        EXIT;
}

 void osp_sync_send_new_rpcs(struct osp_device *d)
{
        struct osp_sync_job *j;
        int rc;
        ENTRY;

        if (cfs_list_empty(&d->opd_syn_committed_here))
                return;

        if (d->opd_syn_rpc_in_flight >= OSP_MAX_IN_FLIGHT)
                return;

        while (d->opd_syn_rpc_in_flight < OSP_MAX_IN_FLIGHT) {
                cfs_spin_lock(&d->opd_syn_lock);
                if (!cfs_list_empty(&d->opd_syn_committed_here)) {
                        j = cfs_list_entry(d->opd_syn_committed_here.next,
                                           struct osp_sync_job,
                                           osj_list);
                  
                        cfs_list_del_init(&j->osj_list);
                        d->opd_syn_rpc_in_flight++;
                        cfs_spin_unlock(&d->opd_syn_lock);
                        
                        if (j->osj_magic != OSP_JOB_MAGIC) {
                                CERROR("send new rpc %p, job %p\n", j->osj_req, j);      
                                LBUG();
                        }
                        rc = ptlrpcd_add_req(j->osj_req, PSCOPE_OTHER);
                        if (rc) {
                                CERROR("can't send RPC: %d\n", rc);
                                break;
                        }
                } else {
                        cfs_spin_unlock(&d->opd_syn_lock);
                        break;
                }
        }
       
        EXIT;
}

/*
 * 
 * we have three queues of work:
 * llog records to be read, records awaiting commit and records to be cancelled
 *
 * we read records (from llog itself) when it's not empty (so that processing
 * loop, llog_proces(), doesn't exit) and when 2nd queue isn't too full
 *
 * 2nd queue is essentially RPCs sent to OST - it's a flow control point.
 *
 * 3rd queue is RPCs reported committed by OST
 *
 */
 int osp_sync_process_queues(struct llog_handle *llh,
                                   struct llog_rec_hdr *rec,
                                   void *data)
{
        struct osp_device *d = data;
        int                rc;

        do { 
                struct l_wait_info lwi = { 0 };

                /* process requests committed by OST */
                osp_sync_process_committed(d);

                /* send new rpc to OST if something is committed locally
                 * and number of in-flight rpc isn't too high */
                osp_sync_send_new_rpcs(d);

                /* if we there are changes to be processed and we have
                 * resources for this ... do now */
                if (osp_sync_can_process_new(d)) {
                        if (llh == NULL) {
                                /* ask llog for another record */
                                return 0;
                        }
                        rc = osp_sync_process_record(d, llh, rec);
                        LASSERT(rc == 0);
                        llh = NULL;
                        rec = NULL;
                }

                l_wait_event(d->opd_syn_waitq,
                                !osp_sync_running(d) || osp_sync_has_work(d),
                                &lwi);

                if (!osp_sync_running(d))
                        return LLOG_PROC_BREAK;
                
        } while (1);
}

 int osp_sync_thread(void *_arg)
{
        struct osp_device      *d = _arg;
        struct ptlrpc_thread   *thread = &d->opd_syn_thread;
        struct llog_ctxt       *ctxt;
        struct obd_device      *obd = d->opd_obd;
        struct llog_handle     *llh;
        int                     rc;
        ENTRY;
       
        {
                char pname[16];
                sprintf(pname, "osp-syn-%u\n", d->opd_index);
                cfs_daemonize(pname);
        }

        cfs_spin_lock(&d->opd_syn_lock);
        thread->t_flags = SVC_RUNNING;
        cfs_spin_unlock(&d->opd_syn_lock);
        cfs_waitq_signal(&thread->t_ctl_waitq);


        if (d->opd_syn_sync_in_progress)
                RETURN(0);

        ctxt = llog_get_context(obd, LLOG_MDS_OST_ORIG_CTXT);
        if (ctxt == NULL) {
                CERROR("can't get appropriate context\n");
                GOTO(out, rc = -EINVAL);
        }

        llh = ctxt->loc_handle;
        if (llh == NULL) {
                CERROR("can't get llh\n");
                llog_ctxt_put(ctxt);
                GOTO(out, rc = -EINVAL);
        }

        rc = llog_cat_process(llh, osp_sync_process_queues, d, 0, 0);
        LASSERT(rc == 0 || rc == LLOG_PROC_BREAK);
        LASSERT(thread->t_flags != SVC_RUNNING);

        /* abort all in-flights */
        CERROR("abort all in-flight RPCs\n");

        /* finish all jobs */
        CERROR("abort all jobs\n");

        rc = llog_cleanup(ctxt);
        if (rc)
                CERROR("can't cleanup llog: %d\n", rc);

out:
        thread->t_flags = SVC_STOPPED;
        cfs_waitq_signal(&thread->t_ctl_waitq);

        RETURN(0);
}

 struct llog_operations osp_mds_ost_orig_logops;

 int osp_sync_llog_init(struct osp_device *d)
{
        struct obd_device   *obd = d->opd_obd;
        struct llog_gen_rec  genrecord;
        struct llog_cookie   cookie;
        struct llog_catid    catid;
        struct llog_ctxt    *ctxt;
        int                  rc;
        ENTRY;

        LASSERT(obd);

        /*
         * open llog corresponding to our OST
         */
        
        OBD_SET_CTXT_MAGIC(&obd->obd_lvfs_ctxt);
        obd->obd_lvfs_ctxt.dt = d->opd_storage;

        rc = llog_get_cat_list(obd, NULL, d->opd_index, 1, &catid);
        if (rc) {
                CERROR("can't get id from catalogs: %d\n", rc);
                GOTO(out, rc);
        }
        
        CDEBUG(D_INFO, "%s: Init llog for %d - catid "LPX64"/"LPX64":%x\n",
               obd->obd_name, d->opd_index, catid.lci_logid.lgl_oid,
               catid.lci_logid.lgl_ogr, catid.lci_logid.lgl_ogen);

        osp_mds_ost_orig_logops = llog_osd_ops;
        osp_mds_ost_orig_logops.lop_setup = llog_obd_origin_setup;
        osp_mds_ost_orig_logops.lop_cleanup = llog_obd_origin_cleanup;
        osp_mds_ost_orig_logops.lop_add = llog_obd_origin_add;
        osp_mds_ost_orig_logops.lop_add_2 = llog_obd_origin_add_2;
        osp_mds_ost_orig_logops.lop_declare_add_2 = llog_obd_origin_declare_add;
        osp_mds_ost_orig_logops.lop_connect = llog_origin_connect;

        rc = llog_setup(obd, &obd->obd_olg, LLOG_MDS_OST_ORIG_CTXT, obd, 1,
                        &catid.lci_logid, &osp_mds_ost_orig_logops);
        if (rc) {
                CERROR("rc: %d\n", rc);
                GOTO(out, rc);
        }

        rc = llog_put_cat_list(obd, NULL, d->opd_index, 1, &catid);
        if (rc) {
                CERROR("rc: %d\n", rc);
                GOTO(out, rc);
        }

        /*
         * put a mark in the llog till which we'll be processing
         * old records restless 
         */
        d->opd_syn_generation.mnt_cnt = cfs_time_current();
        d->opd_syn_generation.conn_cnt = cfs_time_current();

        genrecord.lgr_hdr.lrh_type = LLOG_GEN_REC;
        genrecord.lgr_hdr.lrh_len = sizeof(genrecord);

        memcpy(&genrecord.lgr_gen, &d->opd_syn_generation,
               sizeof(genrecord.lgr_gen));

        ctxt = llog_get_context(obd, LLOG_MDS_OST_ORIG_CTXT);
        LASSERT(ctxt);
        rc = llog_add(ctxt, &genrecord.lgr_hdr, NULL, &cookie, 1);
        llog_ctxt_put(ctxt);
        CERROR("add generation: %d\n", rc);

        if (rc == 1)
                rc = 0;

out:
        RETURN(rc);
}

/**
 * initializes sync component of OSP
 */
int osp_sync_init(struct osp_device *d)
{
        struct l_wait_info lwi = { 0 };
        int                rc;
        ENTRY;

        LASSERT(sizeof(struct osp_sync_job) <= sizeof(union ptlrpc_async_args));

        /*
         * initialize llog storing changes
         */
        rc = osp_sync_llog_init(d);
        if (rc)
                RETURN(rc);

        /*
         * Register commit callbacks on the local storage
         */
        d->opd_syn_txn_cb.dtc_txn_start = osp_sync_txn_commit_cb;
        d->opd_syn_txn_cb.dtc_cookie = d;
        d->opd_syn_txn_cb.dtc_tag = LCT_MD_THREAD;
        CFS_INIT_LIST_HEAD(&d->opd_syn_txn_cb.dtc_linkage);
        dt_txn_callback_add(d->opd_storage, &d->opd_syn_txn_cb);

        /*
         * Start synchronization thread
         */
        cfs_spin_lock_init(&d->opd_syn_lock);
        cfs_waitq_init(&d->opd_syn_waitq);
        cfs_waitq_init(&d->opd_syn_thread.t_ctl_waitq);
        CFS_INIT_LIST_HEAD(&d->opd_syn_waiting_for_commit);
        CFS_INIT_LIST_HEAD(&d->opd_syn_committed_here);
        CFS_INIT_LIST_HEAD(&d->opd_syn_committed_there);

        rc = cfs_kernel_thread(osp_sync_thread, d, 0);
        if (rc < 0) {
                CERROR("can't start sync thread %d\n", rc);
                RETURN(rc);
        }
        
        l_wait_event(d->opd_syn_thread.t_ctl_waitq,
                     osp_sync_running(d) || osp_sync_stopped(d),
                     &lwi);

        RETURN(0);
}

int osp_sync_fini(struct osp_device *d)
{
        struct ptlrpc_thread *thread = &d->opd_syn_thread;
        ENTRY;

        thread->t_flags = SVC_STOPPING;
        cfs_waitq_signal(&d->opd_syn_waitq);

        cfs_wait_event(thread->t_ctl_waitq, thread->t_flags & SVC_STOPPED);

        /*
         * unregister transaction callbacks only when sync thread
         * has finished operations with llog
         */
        dt_txn_callback_del(d->opd_storage, &d->opd_syn_txn_cb);

        RETURN(0);
}


