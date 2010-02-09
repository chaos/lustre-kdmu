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
 * statfs
 */

static inline int osp_statfs_need_update(struct osp_device *d)
{
        if (cfs_time_before(cfs_time_current(), d->opd_statfs_fresh_till))
                return 0;
        return 1;
}

static void osp_statfs_timer_cb(unsigned long _d)
{
        struct osp_device *d = (struct osp_device *) _d;
        LASSERT(d);
        cfs_waitq_signal(&d->opd_pre_waitq);
}

static int osp_statfs_interpret(const struct lu_env *env,
                                struct ptlrpc_request *req,
                                union ptlrpc_async_args *aa,
                                int rc)
{
        struct obd_statfs       *msfs;
        struct osp_device       *d;
        ENTRY;

        aa = ptlrpc_req_async_args(req);
        d = aa->pointer_arg[0];
        LASSERT(d);

        if (rc != 0)
                GOTO(out, rc);

        msfs = req_capsule_server_get(&req->rq_pill, &RMF_OBD_STATFS);
        if (msfs == NULL) {
                GOTO(out, rc = -EPROTO);
        }
        
        statfs_unpack(&d->opd_statfs, msfs);

out:
        if (rc == 0) {
                /* schedule next update */
                d->opd_statfs_fresh_till = cfs_time_shift(obd_timeout / 3);
                cfs_timer_arm(&d->opd_statfs_timer, d->opd_statfs_fresh_till);
                CDEBUG(D_CACHE, "updated statfs %p\n", d);
        } else {
                /* couldn't update statfs, try again as soon as possible */
                cfs_waitq_signal(&d->opd_pre_waitq);
                CERROR("couldn't update statfs: %d\n", rc);
        }

        RETURN(rc);
}

static int osp_statfs_update(struct osp_device *d)
{
        struct ptlrpc_request   *req;
        struct obd_import       *imp;
        union ptlrpc_async_args *aa;
        int                      rc;
        ENTRY;

        CDEBUG(D_CACHE, "going to update statfs\n");
        /*
         * if not connection/initialization is compeleted, ignore 
         */
        imp = d->opd_obd->u.cli.cl_import;
        LASSERT(imp);

        req = ptlrpc_request_alloc(imp, &RQF_OST_STATFS);

        if (req == NULL)
                RETURN(-ENOMEM);

        rc = ptlrpc_request_pack(req, LUSTRE_OST_VERSION, OST_STATFS);
        if (rc) {
                ptlrpc_request_free(req);
                RETURN(rc);
        }
        ptlrpc_request_set_replen(req);
        req->rq_request_portal = OST_CREATE_PORTAL;
        ptlrpc_at_set_req_timeout(req);

        req->rq_interpret_reply = (ptlrpc_interpterer_t) osp_statfs_interpret;
        aa = ptlrpc_req_async_args(req);
        aa->pointer_arg[0] = d;

        ptlrpcd_add_req(req, PSCOPE_OTHER);

        cfs_timer_disarm(&d->opd_statfs_timer);

        /*
         * no updates till reply
         */
        d->opd_statfs_fresh_till = cfs_time_shift(obd_timeout * 1000);

        RETURN(rc);
}


/*
 * OSP tries to maintain pool of available objects so that calls to create
 * objects don't block most of time
 *
 * each time OSP gets connected to OST, we should start from precreation cleanup
 */

static inline int osp_precreate_running(struct osp_device *d)
{
        return !!(d->opd_pre_thread.t_flags & SVC_RUNNING);
}

static inline int osp_precreate_stopped(struct osp_device *d)
{
        return !!(d->opd_pre_thread.t_flags & SVC_STOPPED);
}

static inline int osp_precreate_near_empty_nolock(struct osp_device *d)
{
        int window, rc = 0;

        /* XXX: of course 10 should be replaced with something more smart */
        window = d->opd_pre_last_created - d->opd_pre_next;
        if (window - d->opd_pre_reserved < 10)
                rc = 1;

        return rc;
}

static inline int osp_precreate_near_empty(struct osp_device *d)
{
        int rc;

        /* XXX: do we really need locking here? */
        cfs_spin_lock(&d->opd_pre_lock);
        rc = osp_precreate_near_empty_nolock(d);
        cfs_spin_unlock(&d->opd_pre_lock);

        return rc;
}

static int osp_precreate_send(struct osp_device *d)
{
        struct ptlrpc_request  *req;
        struct obd_import      *imp;
        struct ost_body        *body;
        int                     rc;
        ENTRY;
       
        /*
         * if not connection/initialization is compeleted, ignore 
         */
        imp = d->opd_obd->u.cli.cl_import;
        LASSERT(imp);

        req = ptlrpc_request_alloc(imp, &RQF_OST_CREATE);
        if (req == NULL)
                GOTO(out, rc = -ENOMEM);

        rc = ptlrpc_request_pack(req, LUSTRE_OST_VERSION, OST_CREATE);
        if (rc) {
                ptlrpc_request_free(req);
                GOTO(out, rc);
        }

        body = req_capsule_client_get(&req->rq_pill, &RMF_OST_BODY);
        LASSERT(body);
        /* XXX: the window should be adaptive */
        body->oa.o_id = d->opd_pre_last_created + 50;
        body->oa.o_gr = mdt_to_obd_objgrp(0);
        body->oa.o_valid = OBD_MD_FLGROUP;

        ptlrpc_request_set_replen(req);

        rc = ptlrpc_queue_wait(req);
        if (rc)
                GOTO(out_req, rc);

        body = req_capsule_server_get(&req->rq_pill, &RMF_OST_BODY);
        if (body == NULL)
                GOTO(out_req, rc = -EPROTO);

        CDEBUG(D_HA, "new last_created %lu\n", (unsigned long) body->oa.o_id);

        LASSERT(body->oa.o_id > d->opd_pre_next);
        cfs_spin_lock(&d->opd_pre_lock);
        d->opd_pre_last_created = body->oa.o_id;
        cfs_spin_unlock(&d->opd_pre_lock);

        /* probably somebody has been waiting for new id? */
        cfs_waitq_signal(&d->opd_pre_user_waitq);

out_req:
        ptlrpc_req_finished(req);

out:
        RETURN(rc);
}

/**
 * claims connection is originated by MDS
 */
static int osp_precreate_connection_from_mds(struct osp_device *d)
{
        struct ptlrpc_request  *req = NULL;
        struct obd_import      *imp;
        char                   *tmp;
        int                     rc, group;
        ENTRY;

        imp = d->opd_obd->u.cli.cl_import;
        LASSERT(imp);

        req = ptlrpc_request_alloc(imp, &RQF_OBD_SET_INFO);
        if (req == NULL) {
                CERROR("can't allocate request\n");
                RETURN(-ENOMEM);
        }
        
        req_capsule_set_size(&req->rq_pill, &RMF_SETINFO_KEY,
                             RCL_CLIENT, sizeof(KEY_MDS_CONN));
        req_capsule_set_size(&req->rq_pill, &RMF_SETINFO_VAL,
                             RCL_CLIENT, sizeof(group));
        rc = ptlrpc_request_pack(req, LUSTRE_OST_VERSION, OST_SET_INFO);
        if (rc) {
                ptlrpc_request_free(req);
                CERROR("can't pack request\n");
                RETURN(rc);
        }
        
        tmp = req_capsule_client_get(&req->rq_pill, &RMF_SETINFO_KEY);
        memcpy(tmp, KEY_MDS_CONN, sizeof(KEY_MDS_CONN));
        group = mdt_to_obd_objgrp(0); /* XXX: what about CMD? */
        tmp = req_capsule_client_get(&req->rq_pill, &RMF_SETINFO_VAL);
        memcpy(tmp, &group, sizeof(group));

        ptlrpc_request_set_replen(req);

        rc = ptlrpc_queue_wait(req);

        ptlrpc_req_finished(req);

        RETURN(rc);
}

/**
 * asks OST to clean precreate orphans
 * and gets next id for new objects
 */
static int osp_precreate_clean_orphans(struct osp_device *d)
{
        struct ptlrpc_request  *req = NULL;
        struct obd_import      *imp;
        struct ost_body        *body;
        int                     rc;
        ENTRY;

        imp = d->opd_obd->u.cli.cl_import;
        LASSERT(imp);

        req = ptlrpc_request_alloc(imp, &RQF_OST_CREATE);
        if (req == NULL)
                RETURN(rc = -ENOMEM);

        rc = ptlrpc_request_pack(req, LUSTRE_OST_VERSION, OST_CREATE);
        if (rc)
                GOTO(out_req, rc);

        body = req_capsule_client_get(&req->rq_pill, &RMF_OST_BODY);
        LASSERT(body);

        body->oa.o_flags = OBD_FL_DELORPHAN;
        body->oa.o_valid = OBD_MD_FLFLAGS | OBD_MD_FLGROUP;
        body->oa.o_gr = mdt_to_obd_objgrp(0); /* XXX: CMD support? */

        /* remove from NEXT after used one */
        body->oa.o_id = d->opd_last_used_id + 1;

        ptlrpc_request_set_replen(req);

        /* Don't resend the delorphan req */
        req->rq_no_resend = req->rq_no_delay = 1;

        rc = ptlrpc_queue_wait(req);
        if (rc)
                GOTO(out_req, rc);

        body = req_capsule_server_get(&req->rq_pill, &RMF_OST_BODY);
        if (body == NULL)
                GOTO(out_req, rc = -EPROTO);

        /*
         * OST provides us with id new pool starts from, grab it
         */
        CDEBUG(D_HA, "got next id %lu\n", (unsigned long) body->oa.o_id);

        cfs_spin_lock(&d->opd_pre_lock);
        d->opd_pre_next = body->oa.o_id + 1;
        /* nothing precreated yet, the pool is empty */
        d->opd_pre_last_created = d->opd_pre_next; 
        cfs_spin_unlock(&d->opd_pre_lock);

out_req:
        ptlrpc_req_finished(req);

        RETURN(rc);
}

/**
 * here we handle new connection to OST
 */
static int osp_precreate_new_connection(struct osp_device *d)
{       
        int rc;
        ENTRY;

        /* XXX: analyze connection data, etc */

        /*
         * first of all, we declare our connection is originated by MDS
         */
        rc = osp_precreate_connection_from_mds(d);
        if (rc)
                RETURN(rc);

        /*
         * then we remove precreate orphans and update next id
         */
        rc = osp_precreate_clean_orphans(d);

        RETURN(rc);
}

static int osp_precreate_thread(void *_arg)
{
        struct osp_device      *d = _arg;
        struct ptlrpc_thread   *thread = &d->opd_pre_thread;
        int                     rc;
        ENTRY;
       
        {
                char pname[16];
                sprintf(pname, "osp-pre-%u\n", d->opd_index);
                cfs_daemonize(pname);
        }

        cfs_spin_lock(&d->opd_pre_lock);
        thread->t_flags = SVC_RUNNING;
        cfs_spin_unlock(&d->opd_pre_lock);
        cfs_waitq_signal(&thread->t_ctl_waitq);
       
        while (osp_precreate_running(d)) {

                /*
                 * need to be connected to OST
                 */
                while (osp_precreate_running(d)) {
                        struct l_wait_info lwi = { 0 };

                        l_wait_event(d->opd_pre_waitq,
                                        !osp_precreate_running(d) ||
                                        d->opd_new_connection,
                                        &lwi);

                        if (!osp_precreate_running(d))
                                break;

                        if (!d->opd_new_connection)
                                continue;

                        /* got connected, let's initialize connection */
                        d->opd_new_connection = 0;
                        rc = osp_precreate_new_connection(d);

                        /* if initialization went well, move on */
                        if (rc == 0)
                                break;
                }

                /*
                 * connected, can handle precreates now
                 */
                while (osp_precreate_running(d)) {
                        struct l_wait_info lwi = { 0 };

                        l_wait_event(d->opd_pre_waitq,
                                     !osp_precreate_running(d) ||
                                     osp_precreate_near_empty(d) ||
                                     osp_statfs_need_update(d) ||
                                     d->opd_got_disconnected,
                                     &lwi);

                        if (!osp_precreate_running(d))
                                break;

                        /* something happened to the connection
                         * have to start from the beginning */
                        if (d->opd_got_disconnected) {
                                d->opd_got_disconnected = 0;
                                break;
                        }

                        if (osp_statfs_need_update(d))
                                osp_statfs_update(d);

                        if (osp_precreate_near_empty(d)) {
                                rc = osp_precreate_send(d);
                                /* XXX: error handling? */
                                LASSERTF(rc == 0, "%d\n", rc);
                        }
                }
        }

        /* abort all in-flights */
        CERROR("abort all in-flight stat RPCs\n");

        /* finish all jobs */
        CERROR("abort all jobs\n");

        thread->t_flags = SVC_STOPPED;
        cfs_waitq_signal(&thread->t_ctl_waitq);
        CERROR("@@@@@@@@@ EXIT\n");

        RETURN(0);
}

/*
 * called to reserve object in the pool
 * return codes:
 *  ENOSPC - no space on corresponded OST
 *  EAGAIN - precreation is in progress, try later
 *  EIO    - no access to OST
 */
int osp_precreate_reserve(struct osp_device *d)
{
        struct l_wait_info lwi = { 0 };
        int                precreated, rc;
        ENTRY;

        LASSERT(d->opd_pre_last_created >= d->opd_pre_next);

        while ((rc = d->opd_pre_status) == 0) {

                /*
                 * we never use the last object in the window
                 */
                cfs_spin_lock(&d->opd_pre_lock);
                precreated = d->opd_pre_last_created - d->opd_pre_next;
                if (precreated > d->opd_pre_reserved) {
                        d->opd_pre_reserved++;
                        cfs_spin_unlock(&d->opd_pre_lock);

                        /* XXX: don't wake up if precreation is in progress */
                        if (0 && osp_precreate_near_empty_nolock(d))
                               cfs_waitq_signal(&d->opd_pre_waitq);

                        break;
                }
                cfs_spin_unlock(&d->opd_pre_lock);

                /* XXX: don't wake up if precreation is in progress */
                cfs_waitq_signal(&d->opd_pre_waitq);

                l_wait_event(d->opd_pre_user_waitq,
                             (d->opd_pre_last_created > d->opd_pre_next) ||
                              d->opd_pre_status != 0,
                              &lwi);
        }

        RETURN(rc);
}


int osp_init_precreate(struct osp_device *d)
{
        struct l_wait_info lwi = { 0 };
        int                rc;
        ENTRY;

        d->opd_pre_next = 1;
        d->opd_pre_last_created = 1;
        d->opd_pre_reserved = 0;

        cfs_spin_lock_init(&d->opd_pre_lock);
        cfs_waitq_init(&d->opd_pre_waitq);
        cfs_waitq_init(&d->opd_pre_user_waitq);
        cfs_waitq_init(&d->opd_pre_thread.t_ctl_waitq);

        /*
         * Initialize statfs-related things
         */
        d->opd_statfs_fresh_till = cfs_time_shift(-1000);
        CDEBUG(D_OTHER, "current %Lu, fresh till %Lu\n",
               (unsigned long long) cfs_time_current(),
               (unsigned long long) d->opd_statfs_fresh_till);
        cfs_timer_init(&d->opd_statfs_timer, osp_statfs_timer_cb, d);

        /*
         * start thread handling precreation and statfs updates
         */
        rc = cfs_kernel_thread(osp_precreate_thread, d, 0);
        if (rc < 0) {
                CERROR("can't start precreate thread %d\n", rc);
                RETURN(rc);
        }
        
        l_wait_event(d->opd_pre_thread.t_ctl_waitq,
                     osp_precreate_running(d) || osp_precreate_stopped(d),
                     &lwi);

        RETURN(0);

}

void osp_precreate_fini(struct osp_device *d)
{
        struct ptlrpc_thread *thread = &d->opd_pre_thread;
        ENTRY;

        cfs_timer_disarm(&d->opd_statfs_timer);

        thread->t_flags = SVC_STOPPING;
        cfs_waitq_signal(&d->opd_pre_waitq);

        cfs_wait_event(thread->t_ctl_waitq, thread->t_flags & SVC_STOPPED);

        EXIT;
}

