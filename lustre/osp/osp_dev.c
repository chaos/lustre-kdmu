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
 * lustre/osp/osp_dev.c
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

extern struct lu_object_operations osp_lu_obj_ops;

struct lu_object *osp_object_alloc(const struct lu_env *env,
                                    const struct lu_object_header *hdr,
                                    struct lu_device *d)
{
        struct lu_object_header *h;
        struct osp_object       *o;
        struct lu_object        *l;

        LASSERT(hdr == NULL);

        OBD_ALLOC_PTR(o);
        if (o != NULL) {
                l = &o->opo_obj.do_lu;
                h = &o->opo_header;
                
                lu_object_header_init(h);
                dt_object_init(&o->opo_obj, h, d);
                lu_object_add_top(h, l);

                l->lo_ops = &osp_lu_obj_ops;

                return l;
        } else {
                return NULL;
        }
}

static int osp_shutdown(const struct lu_env *env, struct osp_device *d)
{
        int rc = 0;
        ENTRY;

        /* release last_used file */
        lu_object_put(env, &d->opd_last_used_file->do_lu);

        /* stop precreate thread */
        osp_precreate_fini(d);

        /* stop sync thread */
        osp_sync_fini(d);

        RETURN(rc);
}

static int osp_process_config(const struct lu_env *env,
                              struct lu_device *dev,
                              struct lustre_cfg *lcfg)
{
        struct osp_device *d = lu2osp_dev(dev);
        int                rc;
        ENTRY;

        switch(lcfg->lcfg_command) {

                case LCFG_CLEANUP:
                        rc = osp_shutdown(env, d);
                        break;
                default:
                        CERROR("unknown command %u\n", lcfg->lcfg_command);
                        rc = 0;
                        break;
        }

        RETURN(rc);
}

static int osp_recovery_complete(const struct lu_env *env,
                                     struct lu_device *dev)
{
        struct osp_device *osp = lu2osp_dev(dev);
        struct lu_device  *next = &osp->opd_storage->dd_lu_dev;
        int rc;
        ENTRY;
        LBUG();
        rc = next->ld_ops->ldo_recovery_complete(env, next);
        RETURN(rc);
}

const struct lu_device_operations osp_lu_ops = {
        .ldo_object_alloc      = osp_object_alloc,
        .ldo_process_config    = osp_process_config,
        .ldo_recovery_complete = osp_recovery_complete,
};

/**
 * provides with statfs from corresponded OST
 *
 */
static int osp_statfs(const struct lu_env *env,
                       struct dt_device *dev, cfs_kstatfs_t *sfs)
{
        struct osp_device *d = dt2osp_dev(dev);
        ENTRY;

        /*
         * XXX: shouldn't we wait till first OST_STATFS is done?
         * XXX: how to proceed when import is disconnected?
         */
        if (d->opd_statfs.f_type == 0) {
                /* statfs data isn't ready yet */
                RETURN(-EAGAIN);
        }

        /* return recently updated data */
        *sfs = d->opd_statfs;

        /*
         * layer above osp (usually lod) can use ffree to estimate
         * how many objects are available for immediate creation
         */
        cfs_spin_lock(&d->opd_pre_lock);
        sfs->f_ffree = d->opd_pre_last_created - d->opd_pre_next;
        cfs_spin_unlock(&d->opd_pre_lock);

        RETURN(0);
}

static int osp_sync(const struct lu_env *env, struct dt_device *dev)
{
        struct osp_device *d = dt2osp_dev(dev);
        struct dt_device  *next = d->opd_storage;
        int                 rc;
        ENTRY;

        CERROR("not implemented yet\n");
        /*
         * XXX: wake up sync thread, command it to start flushing asap?
         */

        RETURN(0);
}

static const struct dt_device_operations osp_dt_ops = {
        .dt_statfs         = osp_statfs,
        .dt_sync           = osp_sync,
};

static int osp_init_last_used(const struct lu_env *env, struct osp_device *m)
{
        struct dt_object  *o;
        struct lu_fid      fid;
        struct lu_attr     attr;
        int                rc;
        struct lu_buf      lb;
        obd_id             tmp;
        loff_t             off;
        ENTRY;

        memset(&attr, 0, sizeof(attr));
        attr.la_valid = LA_MODE;
        attr.la_mode = S_IFREG | 0666;
        lu_local_obj_fid(&fid, MDD_LOV_OBJ_OID);
        o = dt_reg_open_or_create(env, m->opd_storage, &fid, &attr);
        if (IS_ERR(o))
                RETURN(PTR_ERR(o));

        m->opd_last_used_file = o;

        rc = dt_attr_get(env, o, &attr, NULL);
        if (rc)
                GOTO(out, rc);

        if (attr.la_size >= sizeof(tmp) * (m->opd_index + 1)) {
                lb.lb_buf = &tmp;
                lb.lb_len = sizeof(tmp);
                off = sizeof(tmp) * m->opd_index;
                rc = o->do_body_ops->dbo_read(env, o, &lb, &off, BYPASS_CAPA);
                if (rc < 0)
                        GOTO(out, rc);
        }

        if (rc == sizeof(tmp)) {
                m->opd_last_used_id = le64_to_cpu(tmp);
        } else {
                /* XXX: is this that simple? what if the file got corrupted? */
                m->opd_last_used_id = 0;
        }
        rc = 0;

out:
        /* object will be released in device cleanup path */

        RETURN(rc);
}

static int osp_connect_to_osd(const struct lu_env *env, struct osp_device *m,
                              const char *nextdev)
{
        struct obd_connect_data *data = NULL;
        struct obd_device       *obd;
        int                      rc;
        ENTRY;

        LASSERT(m->opd_storage_exp == NULL);

        OBD_ALLOC(data, sizeof(*data));
        if (data == NULL)
                GOTO(out, rc = -ENOMEM);

        obd = class_name2obd(nextdev);
        if (obd == NULL) {
                CERROR("can't locate next device: %s\n", nextdev);
                GOTO(out, rc = -ENOTCONN);
        }

        rc = obd_connect(NULL, &m->opd_storage_exp, obd, &obd->obd_uuid, data, NULL);
        if (rc) {
                CERROR("cannot connect to next dev %s (%d)\n", nextdev, rc);
                GOTO(out, rc);
        }

        m->opd_dt_dev.dd_lu_dev.ld_site =
                m->opd_storage_exp->exp_obd->obd_lu_dev->ld_site;
        LASSERT(m->opd_dt_dev.dd_lu_dev.ld_site);
        m->opd_storage = lu2dt_dev(m->opd_storage_exp->exp_obd->obd_lu_dev);

out:
        if (data)
                OBD_FREE(data, sizeof(*data));
        RETURN(rc);
}

static int osp_init0(const struct lu_env *env, struct osp_device *m,
                      struct lu_device_type *ldt, struct lustre_cfg *cfg)
{
        struct lprocfs_static_vars  lvars = { 0 };
        struct obd_import          *imp;
        class_uuid_t                uuid;
        int                         rc;
        ENTRY;

        dt_device_init(&m->opd_dt_dev, ldt);

        if (sscanf(lustre_cfg_buf(cfg, 4), "%d", &m->opd_index) != 1) {
                CERROR("can't find index in configuration\n");
                RETURN(-EINVAL);
        }

        rc = osp_connect_to_osd(env, m, lustre_cfg_string(cfg, 3));
        if (rc)
                RETURN(rc);

        /* setup regular network client */
        m->opd_obd = class_name2obd(lustre_cfg_string(cfg, 0));
        LASSERT(m->opd_obd != NULL);
        m->opd_obd->obd_lu_dev = &m->opd_dt_dev.dd_lu_dev;

        rc = ptlrpcd_addref();
        if (rc)
                RETURN(rc);
        rc = client_obd_setup(m->opd_obd, cfg);
        if (rc) {
                CERROR("can't setup obd: %d\n", rc);
                GOTO(out, rc);
        }

        /* XXX: how do we do this well? */
        m->opd_obd->obd_set_up = 1;

        lprocfs_osp_init_vars(&lvars);
        if (lprocfs_obd_setup(m->opd_obd, lvars.obd_vars) == 0)
                ptlrpc_lprocfs_register_obd(m->opd_obd);

        /*
         * Initialize last id from the storage - will be used in orphan cleanup
         */
        rc = osp_init_last_used(env, m);
        if (rc)
                GOTO(out, rc);

        /*
         * Initialize precreation thread, it handles new connections as well
         */
        rc = osp_init_precreate(m);
        if (rc)
                GOTO(out, rc);

        /*
         * Initialize synhronization mechanism taking care of propogating
         * changes to OST in near transactional manner
         */
        rc = osp_sync_init(m);
        if (rc)
                GOTO(out, rc);

        /*
         * Initiate connect to OST
         */
        ll_generate_random_uuid(uuid);
        class_uuid_unparse(uuid, &m->opd_cluuid);

        imp = m->opd_obd->u.cli.cl_import;

        rc = ptlrpc_init_import(imp);
        LASSERT(rc == 0);


out:
        /* XXX: release all resource in error case */
        RETURN(rc);
}

static struct lu_device *osp_device_alloc(const struct lu_env *env,
                                          struct lu_device_type *t,
                                          struct lustre_cfg *lcfg)
{
        struct osp_device *m;
        struct lu_device   *l;

        OBD_ALLOC_PTR(m);
        if (m == NULL) {
                l = ERR_PTR(-ENOMEM);
        } else {
                osp_init0(env, m, t, lcfg);
                l = osp2lu_dev(m);
                l->ld_ops = &osp_lu_ops;
                m->opd_dt_dev.dd_ops = &osp_dt_ops;
                /* XXX: dt_upcall_init(&m->opd_dt_dev, NULL); */
        }

        return l;
}

static struct lu_device *osp_device_fini(const struct lu_env *env,
                                         struct lu_device *d)
{
        struct osp_device *m = lu2osp_dev(d);
        struct obd_import *imp;
        int                rc;
        ENTRY;

        LASSERT(m->opd_storage_exp);
        obd_disconnect(m->opd_storage_exp);

        imp = m->opd_obd->u.cli.cl_import;

        /* Mark import deactivated now, so we don't try to reconnect if any
         * of the cleanup RPCs fails (e.g. ldlm cancel, etc).  We don't
         * fully deactivate the import, or that would drop all requests. */
        cfs_spin_lock(&imp->imp_lock);
        imp->imp_deactive = 1;
        cfs_spin_unlock(&imp->imp_lock);

        /* Some non-replayable imports (MDS's OSCs) are pinged, so just
         * delete it regardless.  (It's safe to delete an import that was
         * never added.) */
        (void) ptlrpc_pinger_del_import(imp);

        rc = ptlrpc_disconnect_import(imp, 0);
        LASSERT(rc == 0);

        ptlrpc_invalidate_import(imp);

        if (imp->imp_rq_pool) {
                ptlrpc_free_rq_pool(imp->imp_rq_pool);
                imp->imp_rq_pool = NULL;
        }

        client_destroy_import(imp);

        LASSERT(m->opd_obd);
        ptlrpc_lprocfs_unregister_obd(m->opd_obd);
        lprocfs_obd_cleanup(m->opd_obd);

        rc = client_obd_cleanup(m->opd_obd);
        LASSERTF(rc == 0, "error %d\n", rc);

        ptlrpcd_decref();

        RETURN(NULL);
}

static struct lu_device *osp_device_free(const struct lu_env *env,
                                         struct lu_device *lu)
{
        struct osp_device *m = lu2osp_dev(lu);
        ENTRY;

        LASSERT(atomic_read(&lu->ld_ref) == 0);
        dt_device_fini(&m->opd_dt_dev);
        OBD_FREE_PTR(m);
        RETURN(NULL);
}


static int osp_reconnect(const struct lu_env *env,
                         struct obd_export *exp, struct obd_device *obd,
                         struct obd_uuid *cluuid,
                         struct obd_connect_data *data,
                         void *localdata)
{
        /* XXX: nothing to do ? */
        return 0;
}

/*
 * we use exports to track all LOD users
 */
static int osp_obd_connect(const struct lu_env *env, struct obd_export **exp,
                           struct obd_device *obd, struct obd_uuid *cluuid,
                           struct obd_connect_data *data, void *localdata)
{
        struct osp_device       *osp = lu2osp_dev(obd->obd_lu_dev);
        struct obd_connect_data *ocd;
        struct obd_import       *imp;
        struct lustre_handle     conn;
        int                      rc;
        ENTRY;

        CDEBUG(D_CONFIG, "connect #%d\n", osp->opd_connects);

        rc = class_connect(&conn, obd, cluuid);
        if (rc)
                RETURN(rc);

        *exp = class_conn2export(&conn);

        /* Why should there ever be more than 1 connect? */
        /* XXX: locking ? */
        osp->opd_connects++;
        LASSERT(osp->opd_connects == 1);

        imp = osp->opd_obd->u.cli.cl_import;
        imp->imp_dlm_handle = conn;

        /* XXX: which flags we need on OST? */
        ocd = &imp->imp_connect_data;
        ocd->ocd_version = LUSTRE_VERSION_CODE;
        ocd->ocd_connect_flags = OBD_CONNECT_VERSION
                                 | OBD_CONNECT_AT
                                 | OBD_CONNECT_MDS
                                 | OBD_CONNECT_SKIP_ORPHAN;
        imp->imp_connect_flags_orig = ocd->ocd_connect_flags;

        rc = ptlrpc_connect_import(imp, NULL);
        LASSERT(rc == 0);

        if (rc) {
                CERROR("can't connect obd: %d\n", rc);
                GOTO(out, rc);
        }

        ptlrpc_pinger_add_import(imp);

out:
        RETURN(rc);
}

/*
 * once last export (we don't count self-export) disappeared
 * osp can be released
 */
static int osp_obd_disconnect(struct obd_export *exp)
{
        struct obd_device *obd = exp->exp_obd;
        struct osp_device *osp = lu2osp_dev(obd->obd_lu_dev);
        int                rc;
        ENTRY;

        /* Only disconnect the underlying layers on the final disconnect. */
        /* XXX: locking ? */
        LASSERT(osp->opd_connects == 1);
        osp->opd_connects--;

        rc = class_disconnect(exp); /* bz 9811 */

        /* destroy the device */
        if (rc == 0)
                class_manual_cleanup(obd);

        RETURN(rc);
}

/*
 * lprocfs helpers still use OBD API, let's keep obd_statfs() support for a while
 */
static int osp_obd_statfs(struct obd_device *obd, struct obd_statfs *osfs,
                          __u64 max_age, __u32 flags)
{
        struct obd_statfs     *msfs;
        struct ptlrpc_request *req;
        struct obd_import     *imp = NULL;
        int rc;
        ENTRY;

        /*Since the request might also come from lprocfs, so we need
         *sync this with client_disconnect_export Bug15684*/
        cfs_down_read(&obd->u.cli.cl_sem);
        if (obd->u.cli.cl_import)
                imp = class_import_get(obd->u.cli.cl_import);
        cfs_up_read(&obd->u.cli.cl_sem);
        if (!imp)
                RETURN(-ENODEV);

        /* We could possibly pass max_age in the request (as an absolute
         * timestamp or a "seconds.usec ago") so the target can avoid doing
         * extra calls into the filesystem if that isn't necessary (e.g.
         * during mount that would help a bit).  Having relative timestamps
         * is not so great if request processing is slow, while absolute
         * timestamps are not ideal because they need time synchronization. */
        req = ptlrpc_request_alloc(imp, &RQF_OST_STATFS);

        class_import_put(imp);

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

        if (flags & OBD_STATFS_NODELAY) {
                /* procfs requests not want stat in wait for avoid deadlock */
                req->rq_no_resend = 1;
                req->rq_no_delay = 1;
        }

        rc = ptlrpc_queue_wait(req);
        if (rc)
                GOTO(out, rc);

        msfs = req_capsule_server_get(&req->rq_pill, &RMF_OBD_STATFS);
        if (msfs == NULL) {
                GOTO(out, rc = -EPROTO);
        }

        *osfs = *msfs;

        EXIT;
 out:
        ptlrpc_req_finished(req);
        return rc;
}

static int osp_import_event(struct obd_device *obd,
                            struct obd_import *imp,
                            enum obd_import_event event)
{
        struct osp_device *d = lu2osp_dev(obd->obd_lu_dev);

        switch (event) {
                case IMP_EVENT_DISCON:
                case IMP_EVENT_INACTIVE:
                        /* XXX: disallow OSP to create objects */
                        d->opd_got_disconnected = 1;
                        cfs_waitq_signal(&d->opd_pre_waitq);
                        CDEBUG(D_HA, "got disconnected\n");
                        break;

                case IMP_EVENT_INVALIDATE:
                        /* XXX: what? */
                        break;

                case IMP_EVENT_ACTIVE:
                        d->opd_new_connection = 1;
                        cfs_waitq_signal(&d->opd_pre_waitq);
                        CDEBUG(D_HA, "got connected\n");
                        break;

                case IMP_EVENT_OCD:
                        /* XXX: what? */
                        break;

                default:
                        LBUG();
        }
        return 0;
}



/* context key constructor/destructor: mdt_key_init, mdt_key_fini */
LU_KEY_INIT_FINI(osp, struct osp_thread_info);

/* context key: osp_thread_key */
LU_CONTEXT_KEY_DEFINE(osp, LCT_DT_THREAD);

LU_TYPE_INIT_FINI(osp, &osp_thread_key);

static struct lu_device_type_operations osp_device_type_ops = {
        .ldto_init           = osp_type_init,
        .ldto_fini           = osp_type_fini,

        .ldto_start          = osp_type_start,
        .ldto_stop           = osp_type_stop,

        .ldto_device_alloc   = osp_device_alloc,
        .ldto_device_free    = osp_device_free,

        .ldto_device_fini    = osp_device_fini
};

static struct lu_device_type osp_device_type = {
        .ldt_tags     = LU_DEVICE_DT,
        .ldt_name     = LUSTRE_OSP_NAME,
        .ldt_ops      = &osp_device_type_ops,
        .ldt_ctx_tags = LCT_DT_THREAD | LCT_MD_THREAD
};

static struct obd_ops osp_obd_device_ops = {
        .o_owner                = THIS_MODULE,
        .o_add_conn             = client_import_add_conn,
        .o_del_conn             = client_import_del_conn,
        .o_reconnect            = osp_reconnect,
        .o_connect              = osp_obd_connect,
        .o_disconnect           = osp_obd_disconnect,
        .o_import_event         = osp_import_event,
        .o_statfs               = osp_obd_statfs,
};


static int __init osp_mod_init(void)
{
        struct lprocfs_static_vars lvars;
        lprocfs_osp_init_vars(&lvars);

        return class_register_type(&osp_obd_device_ops, NULL, lvars.module_vars,
                                   LUSTRE_OSP_NAME, &osp_device_type);
}

static void __exit osp_mod_exit(void)
{

        class_unregister_type(LUSTRE_OSP_NAME);
}

MODULE_AUTHOR("Sun Microsystems, Inc. <http://www.lustre.org/>");
MODULE_DESCRIPTION("Lustre OST Proxy Device ("LUSTRE_OSP_NAME")");
MODULE_LICENSE("GPL");

cfs_module(osp, "0.1.0", osp_mod_init, osp_mod_exit);
