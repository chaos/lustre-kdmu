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
 * lustre/obdclass/obd_mount.c
 *
 * Client/server mount routines
 *
 * Author: Nathan Rutman <nathan@clusterfs.com>
 */


#define DEBUG_SUBSYSTEM S_CLASS
#define D_MOUNT D_SUPER|D_CONFIG /*|D_WARNING */
#define PRINT_CMD CDEBUG
#define PRINT_MASK D_SUPER|D_CONFIG

#include <obd.h>
#include <lvfs.h>
#include <lustre_fsfilt.h>
#include <obd_class.h>
#include <lustre/lustre_user.h>
#include <lustre_log.h>
#include <lustre_disk.h>
#include <lustre_param.h>

extern void lustre_osvfs_update(void *, void *);
extern int lustre_osvfs_mount(void *);

struct mconf_device {
        struct lu_device  mcf_lu_dev;
        struct lu_device *mcf_bottom;
};

struct mconf_object {
        struct lu_object_header mco_header;
        struct lu_object        mco_obj;
};

static inline struct mconf_device *mconf_dev(struct lu_device *d)
{
        return container_of0(d, struct mconf_device, mcf_lu_dev);
}

static struct mconf_object *mconf_obj(struct lu_object *o)
{
        return container_of0(o, struct mconf_object, mco_obj);
}

static char *mconf_get_label(struct dt_device *dt);
static int mconf_set_label(struct dt_device *dt, char *label);


/*********** mount lookup *********/

CFS_DECLARE_MUTEX(lustre_mount_info_lock);
static CFS_LIST_HEAD(server_mount_info_list);

static struct lustre_mount_info *server_find_mount(const char *name)
{
        cfs_list_t *tmp;
        struct lustre_mount_info *lmi;
        ENTRY;

        cfs_list_for_each(tmp, &server_mount_info_list) {
                lmi = cfs_list_entry(tmp, struct lustre_mount_info,
                                     lmi_list_chain);
                if (strcmp(name, lmi->lmi_name) == 0)
                        RETURN(lmi);
        }
        RETURN(NULL);
}

/* we must register an obd for a mount before we call the setup routine.
   *_setup will call server_get_mount to get the lustre_mount_info struct
   by obd_name, since we can't pass the pointer to setup. */
static int server_register_mount(const char *name, struct lustre_sb_info *lsi,
                          struct dt_device *dt)
{
        struct lustre_mount_info *lmi;
        char *name_cp;
        ENTRY;

        LASSERT(dt);
        LASSERT(lsi);

        OBD_ALLOC(lmi, sizeof(*lmi));
        if (!lmi)
                RETURN(-ENOMEM);
        OBD_ALLOC(name_cp, strlen(name) + 1);
        if (!name_cp) {
                OBD_FREE(lmi, sizeof(*lmi));
                RETURN(-ENOMEM);
        }
        strcpy(name_cp, name);

        cfs_down(&lustre_mount_info_lock);

        if (server_find_mount(name)) {
                cfs_up(&lustre_mount_info_lock);
                OBD_FREE(lmi, sizeof(*lmi));
                OBD_FREE(name_cp, strlen(name) + 1);
                CERROR("Already registered %s\n", name);
                RETURN(-EEXIST);
        }
        lmi->lmi_name = name_cp;
        lmi->lmi_lsi = lsi;
        lmi->lmi_dt = dt;
        cfs_list_add(&lmi->lmi_list_chain, &server_mount_info_list);

        cfs_up(&lustre_mount_info_lock);

        CDEBUG(D_MOUNT, "reg_dt %p from %s\n", lmi->lmi_dt, name);

        RETURN(0);
}

/* when an obd no longer needs a mount */
static int server_deregister_mount(const char *name)
{
        struct lustre_mount_info *lmi;
        ENTRY;

        cfs_down(&lustre_mount_info_lock);
        lmi = server_find_mount(name);
        if (!lmi) {
                cfs_up(&lustre_mount_info_lock);
                CERROR("%s not registered\n", name);
                RETURN(-ENOENT);
        }

        CDEBUG(D_MOUNT, "dereg_dt %p from %s\n", lmi->lmi_dt, name);

        OBD_FREE(lmi->lmi_name, strlen(lmi->lmi_name) + 1);
        cfs_list_del(&lmi->lmi_list_chain);
        OBD_FREE(lmi, sizeof(*lmi));
        cfs_up(&lustre_mount_info_lock);

        RETURN(0);
}

/* obd's look up a registered mount using their obdname. This is just
   for initial obd setup to find the mount struct.  It should not be
   called every time you want to mntget. */
struct lustre_mount_info *server_get_mount(const char *name)
{
        struct lustre_mount_info *lmi;
        struct lustre_sb_info *lsi;
        ENTRY;

        cfs_down(&lustre_mount_info_lock);
        lmi = server_find_mount(name);
        cfs_up(&lustre_mount_info_lock);
        if (!lmi) {
                CERROR("Can't find mount for %s\n", name);
                RETURN(NULL);
        }
        lsi = lmi->lmi_lsi;
        LASSERT(lsi != NULL);
        cfs_atomic_inc(&lsi->lsi_mounts);

        CDEBUG(D_MOUNT, "%p/%p from %s, refs=%d\n",
               lmi, lsi, name, cfs_atomic_read(&lsi->lsi_mounts));

        RETURN(lmi);
}

/*
 * Used by mdt to get mount_info from obdname.
 * There are no blocking when using the mount_info.
 * Do not use server_get_mount for this purpose.
 */
struct lustre_mount_info *server_get_mount_2(const char *name)
{
        struct lustre_mount_info *lmi;
        ENTRY;

        cfs_down(&lustre_mount_info_lock);
        lmi = server_find_mount(name);
        cfs_up(&lustre_mount_info_lock);
        if (!lmi)
                CERROR("Can't find mount for %s\n", name);

        RETURN(lmi);
}

static int lustre_put_lsi(struct lustre_sb_info *lsi);

/* to be called from obd_cleanup methods */
int server_put_mount(const char *name)
{
        struct lustre_mount_info *lmi;
        struct lustre_sb_info *lsi;
        ENTRY;

        cfs_down(&lustre_mount_info_lock);
        lmi = server_find_mount(name);
        cfs_up(&lustre_mount_info_lock);
        if (!lmi) {
                CERROR("Can't find mount for %s\n", name);
                RETURN(-ENOENT);
        }
        lsi = lmi->lmi_lsi;
        LASSERT(lsi != NULL);

        CDEBUG(D_MOUNT, "%p/%p from %s, refs=%d\n",
               lmi, lsi, name, cfs_atomic_read(&lsi->lsi_mounts));


        if (lustre_put_lsi(lsi)) {
                CDEBUG(D_MOUNT, "Last put from %s\n", name);
#if 0
                /* last mount is the One True Mount */
                if (count > 1)
                        CERROR("%s: mount busy, vfscount=%d!\n", name, count);
#endif
        }

        /* this obd should never need the mount again */
        server_deregister_mount(name);
        RETURN(0);
}

/* Corresponding to server_get_mount_2 */
int server_put_mount_2(const char *name)
{
        ENTRY;
        RETURN(0);
}

/******* mount helper utilities *********/

#if 0
static void ldd_print(struct lustre_disk_data *ldd)
{
        PRINT_CMD(PRINT_MASK, "  disk data:\n");
        PRINT_CMD(PRINT_MASK, "server:  %s\n", ldd->ldd_svname);
        PRINT_CMD(PRINT_MASK, "uuid:    %s\n", (char *)ldd->ldd_uuid);
        PRINT_CMD(PRINT_MASK, "fs:      %s\n", ldd->ldd_fsname);
        PRINT_CMD(PRINT_MASK, "index:   %04x\n", ldd->ldd_svindex);
        PRINT_CMD(PRINT_MASK, "config:  %d\n", ldd->ldd_config_ver);
        PRINT_CMD(PRINT_MASK, "flags:   %#x\n", ldd->ldd_flags);
        PRINT_CMD(PRINT_MASK, "diskfs:  %s\n", MT_STR(ldd));
        PRINT_CMD(PRINT_MASK, "options: %s\n", ldd->ldd_mount_opts);
        PRINT_CMD(PRINT_MASK, "params:  %s\n", ldd->ldd_params);
        PRINT_CMD(PRINT_MASK, "comment: %s\n", ldd->ldd_userdata);
}
#endif

static int ldd_write(struct dt_device *dt, struct lustre_disk_data *ldd)
{
        struct lu_env          env;
        int                    rc;
        struct dt_object      *file;
        struct lu_fid          fid;
        struct lu_buf          buf;
        loff_t                 pos;
        struct thandle        *th;

        if (dt->dd_lu_dev.ld_site == NULL) {
                /* no configuration file */
                return 0;
        }

        rc = lu_env_init(&env, dt->dd_lu_dev.ld_type->ldt_ctx_tags);
        LASSERT(rc == 0);

        file = dt_store_open(&env, dt, MOUNT_CONFIGS_DIR, CONFIGS_FILE, &fid);
        LASSERT(!IS_ERR(file));

        buf.lb_buf = ldd;
        buf.lb_len = sizeof(*ldd);
        pos = 0;

        th = dt->dd_ops->dt_trans_create(&env, dt);
        LASSERT(!IS_ERR(th));
        rc = dt_declare_record_write(&env, file, buf.lb_len, pos, th);
        LASSERT(rc == 0);
        rc = dt->dd_ops->dt_trans_start(&env, dt, th);
        LASSERT(rc == 0);

        rc = dt_record_write(&env, file, &buf, &pos, th);

        dt->dd_ops->dt_trans_stop(&env, th);

        lu_object_put(&env, &file->do_lu);
        lu_env_fini(&env);
        RETURN(0);
}


/**************** config llog ********************/

/** Get a config log from the MGS and process it.
 * This func is called for both clients and servers.
 * Continue to process new statements appended to the logs
 * (whenever the config lock is revoked) until lustre_end_log
 * is called.
 * @param lsi is used by the MGC to write to the local copy of
 *   the config log
 * @param logname The name of the llog to replicate from the MGS
 * @param cfg Since the same mgc may be used to follow multiple config logs
 *   (e.g. ost1, ost2, client), the config_llog_instance keeps the state for
 *   this log, and is added to the mgc's list of logs to follow.
 */
int lustre_log_process(struct lustre_sb_info *lsi, char *logname,
                     struct config_llog_instance *cfg)
{
        struct lustre_cfg *lcfg;
        struct lustre_cfg_bufs bufs;
        struct obd_device *mgc = lsi->lsi_mgc;
        int rc;
        ENTRY;

        LASSERT(mgc);
        LASSERT(cfg);

        /* mgc_process_config */
        lustre_cfg_bufs_reset(&bufs, mgc->obd_name);
        lustre_cfg_bufs_set_string(&bufs, 1, logname);
        lustre_cfg_bufs_set(&bufs, 2, cfg, sizeof(*cfg));
        lustre_cfg_bufs_set(&bufs, 3, &lsi, sizeof(lsi));
        lcfg = lustre_cfg_new(LCFG_LOG_START, &bufs);
        rc = obd_process_config(mgc, sizeof(*lcfg), lcfg);
        lustre_cfg_free(lcfg);

        if (rc == -EINVAL)
                LCONSOLE_ERROR_MSG(0x15b, "%s: The configuration from log '%s'"
                                   "failed from the MGS (%d).  Make sure this "
                                   "client and the MGS are running compatible "
                                   "versions of Lustre.\n",
                                   mgc->obd_name, logname, rc);

        if (rc)
                LCONSOLE_ERROR_MSG(0x15c, "%s: The configuration from log '%s' "
                                   "failed (%d). This may be the result of "
                                   "communication errors between this node and "
                                   "the MGS, a bad configuration, or other "
                                   "errors. See the syslog for more "
                                   "information.\n", mgc->obd_name, logname,
                                   rc);

        /* class_obd_list(); */
        RETURN(rc);
}

/* Stop watching this config log for updates */
int lustre_log_end(struct lustre_sb_info *lsi, char *logname,
                       struct config_llog_instance *cfg)
{
        struct lustre_cfg *lcfg;
        struct lustre_cfg_bufs bufs;
        struct obd_device *mgc = lsi->lsi_mgc;
        int rc;
        ENTRY;

        if (!mgc)
                RETURN(-ENOENT);

        /* mgc_process_config */
        lustre_cfg_bufs_reset(&bufs, mgc->obd_name);
        lustre_cfg_bufs_set_string(&bufs, 1, logname);
        if (cfg)
                lustre_cfg_bufs_set(&bufs, 2, cfg, sizeof(*cfg));
        lcfg = lustre_cfg_new(LCFG_LOG_END, &bufs);
        rc = obd_process_config(mgc, sizeof(*lcfg), lcfg);
        lustre_cfg_free(lcfg);
        RETURN(rc);
}

/**************** obd start *******************/

/** lustre_cfg_bufs are a holdover from 1.4; we can still set these up from
 * lctl (and do for echo cli/srv).
 */
static int do_lcfg(char *cfgname, lnet_nid_t nid, int cmd,
            char *s1, char *s2, char *s3, char *s4)
{
        struct lustre_cfg_bufs bufs;
        struct lustre_cfg    * lcfg = NULL;
        int rc;

        CDEBUG(D_TRACE, "lcfg %s %#x %s %s %s %s\n", cfgname,
               cmd, s1, s2, s3, s4);

        lustre_cfg_bufs_reset(&bufs, cfgname);
        if (s1)
                lustre_cfg_bufs_set_string(&bufs, 1, s1);
        if (s2)
                lustre_cfg_bufs_set_string(&bufs, 2, s2);
        if (s3)
                lustre_cfg_bufs_set_string(&bufs, 3, s3);
        if (s4)
                lustre_cfg_bufs_set_string(&bufs, 4, s4);

        lcfg = lustre_cfg_new(cmd, &bufs);
        lcfg->lcfg_nid = nid;
        rc = class_process_config(lcfg);
        lustre_cfg_free(lcfg);
        return(rc);
}

/** Call class_attach and class_setup.  These methods in turn call
 * obd type-specific methods.
 */
static int lustre_start_simple(char *obdname, char *type, char *uuid,
                               char *s1, char *s2)
{
        int rc;
        CDEBUG(D_MOUNT, "Starting obd %s (typ=%s)\n", obdname, type);

        rc = do_lcfg(obdname, 0, LCFG_ATTACH, type, uuid, 0, 0);
        if (rc) {
                CERROR("%s attach error %d\n", obdname, rc);
                return(rc);
        }
        rc = do_lcfg(obdname, 0, LCFG_SETUP, s1, s2, 0, 0);
        if (rc) {
                CERROR("%s setup error %d\n", obdname, rc);
                do_lcfg(obdname, 0, LCFG_DETACH, 0, 0, 0, 0);
        }
        return rc;
}

/* Set up a MGS to serve startup logs */
static int server_start_mgs(struct lustre_sb_info *lsi)
{
        struct lustre_mount_info *lmi;
        int    rc = 0;
        ENTRY;
        LASSERT(lsi->lsi_dt_dev);

        /* It is impossible to have more than 1 MGS per node, since
           MGC wouldn't know which to connect to */
        lmi = server_find_mount(LUSTRE_MGS_OBDNAME);
        if (lmi) {
                LASSERT(lmi->lmi_lsi);
                lsi = lmi->lmi_lsi;
                LCONSOLE_ERROR_MSG(0x15d, "The MGS service was already started"
                                   " from server %s\n",
                                   lsi->lsi_ldd->ldd_svname);
                RETURN(-EALREADY);
        }

        CDEBUG(D_CONFIG, "Start MGS service %s\n", LUSTRE_MGS_OBDNAME);

        rc = server_register_mount(LUSTRE_MGS_OBDNAME, lsi, lsi->lsi_dt_dev);

        if (!rc) {
                rc = lustre_start_simple(LUSTRE_MGS_OBDNAME, LUSTRE_MGS_NAME,
                                         LUSTRE_MGS_OBDNAME, 0, 0);
                /* Do NOT call server_deregister_mount() here. This leads to
                 * inability cleanup cleanly and free lsi and other stuff when
                 * mgs calls server_put_mount() in error handling case. -umka */
        }

        if (rc)
                LCONSOLE_ERROR_MSG(0x15e, "Failed to start MGS '%s' (%d). "
                                   "Is the 'mgs' module loaded?\n",
                                   LUSTRE_MGS_OBDNAME, rc);
        RETURN(rc);
}

static int server_stop_mgs(struct lustre_sb_info *lsi)
{
        struct obd_device *obd;
        int rc;
        ENTRY;

        CDEBUG(D_MOUNT, "Stop MGS service %s\n", LUSTRE_MGS_OBDNAME);

        /* There better be only one MGS */
        obd = class_name2obd(LUSTRE_MGS_OBDNAME);
        if (!obd) {
                CDEBUG(D_CONFIG, "mgs %s not running\n", LUSTRE_MGS_OBDNAME);
                RETURN(-EALREADY);
        }

        /* The MGS should always stop when we say so */
        obd->obd_force = 1;
        rc = class_manual_cleanup(obd);
        RETURN(rc);
}

CFS_DECLARE_MUTEX(mgc_start_lock);

/** Set up a mgc obd to process startup logs
 *
 * \param lsi [in] lustre super block info of the mgc obd
 *
 * \retval 0 success, otherwise error code
 */
static int lustre_start_mgc(struct lustre_sb_info *lsi)
{
        struct obd_connect_data *data = NULL;
        struct obd_device *obd;
        struct obd_export *exp;
        struct obd_uuid *uuid;
        class_uuid_t uuidc;
        lnet_nid_t nid;
        char *mgcname, *niduuid, *mgssec;
        char *ptr;
        int recov_bk;
        int rc = 0, i = 0, j = 0, len;
        ENTRY;

        LASSERT(lsi->lsi_lmd);

        /* Find the first MGS nid for our MGC name */
        if (lsi->lsi_flags & LSI_SERVER) {
                /* mount -o mgsnode=nid */
                if (lsi->lsi_lmd->lmd_mgs &&
                    (class_parse_nid(lsi->lsi_lmd->lmd_mgs, &nid,
                                     &ptr, 0) == 0)) {
                        i++;
                /* mkfs --mgsnode=nid */
                } else if ((class_find_param(lsi->lsi_ldd->ldd_params,
                                             PARAM_MGSNODE, &ptr) == 0) &&
                           (class_parse_nid(ptr, &nid, &ptr, 0) == 0)) {
                        i++;
                /* use local non-LO nid */
                } else if (IS_MGS(lsi->lsi_ldd)) {
                        lnet_process_id_t id;
                        while ((rc = LNetGetId(j++, &id)) != -ENOENT) {
                                if (LNET_NETTYP(LNET_NIDNET(id.nid)) == LOLND)
                                        continue;
                                nid = id.nid;
                                i++;
                                ptr = NULL;
                                break;
                        }
                }
        } else { /* client */
                /* Use nids from mount line: uml1,1@elan:uml2,2@elan:/lustre */
                if (class_parse_nid(lsi->lsi_lmd->lmd_dev, &nid, &ptr, 0) == 0)
                        i++;
        }
        if (i == 0) {
                CERROR("No valid MGS nids found.\n");
                RETURN(-EINVAL);
        }

        len = strlen(LUSTRE_MGC_OBDNAME) + strlen(libcfs_nid2str(nid)) + 1;
        OBD_ALLOC(mgcname, len);
        OBD_ALLOC(niduuid, len + 2);
        if (!mgcname || !niduuid)
                GOTO(out_free, rc = -ENOMEM);
        sprintf(mgcname, "%s%s", LUSTRE_MGC_OBDNAME, libcfs_nid2str(nid));

        mgssec = lsi->lsi_lmd->lmd_mgssec ? lsi->lsi_lmd->lmd_mgssec : "";

        cfs_mutex_down(&mgc_start_lock);

        obd = class_name2obd(mgcname);
        if (obd && !obd->obd_stopping) {
                rc = obd_set_info_async(obd->obd_self_export,
                                        strlen(KEY_MGSSEC), KEY_MGSSEC,
                                        strlen(mgssec), mgssec, NULL);
                if (rc)
                        GOTO(out_free, rc);

                /* Re-using an existing MGC */
                cfs_atomic_inc(&obd->u.cli.cl_mgc_refcount);

                recov_bk = 0;
                /* If we are restarting the MGS, don't try to keep the MGC's
                   old connection, or registration will fail. */
                if ((lsi->lsi_flags & LSI_SERVER) && IS_MGS(lsi->lsi_ldd)) {
                        CDEBUG(D_MOUNT, "New MGS with live MGC\n");
                        recov_bk = 1;
                }

                /* Try all connections, but only once (again).
                   We don't want to block another target from starting
                   (using its local copy of the log), but we do want to connect
                   if at all possible. */
                recov_bk++;
                CDEBUG(D_MOUNT, "%s: Set MGC reconnect %d\n", mgcname,recov_bk);
                rc = obd_set_info_async(obd->obd_self_export,
                                        sizeof(KEY_INIT_RECOV_BACKUP),
                                        KEY_INIT_RECOV_BACKUP,
                                        sizeof(recov_bk), &recov_bk, NULL);
                GOTO(out, rc = 0);
        }

        CDEBUG(D_MOUNT, "Start MGC '%s'\n", mgcname);

        /* At this point nid=the first nid found, ptr points to its end
         * or NULL, meaning use local nids. */

        /* Add the primary nids for the MGS */
        i = 0;
        sprintf(niduuid, "%s_%x", mgcname, i);
        rc = do_lcfg(mgcname, nid, LCFG_ADD_UUID, niduuid, 0, 0, 0);
        /* Add alternate locations for primary nid (i.e. up to ':') */
        if (ptr) {
                while ((*ptr != ':') &&
                       class_parse_nid(ptr, &nid, &ptr, 0) == 0) {
                        rc = do_lcfg(mgcname, nid, LCFG_ADD_UUID, niduuid,
                                     0, 0, 0);
                }
        } else if ((lsi->lsi_flags & LSI_SERVER) && IS_MGS(lsi->lsi_ldd)) {
                /* Use local nids (including LO) */
                lnet_process_id_t id;
                while ((rc = LNetGetId(i++, &id)) != -ENOENT) {
                        if (i == j) /* already added above */
                                continue;
                        rc = do_lcfg(mgcname, id.nid, LCFG_ADD_UUID, niduuid,
                                     0, 0, 0);
                }
        }
        lsi->lsi_lmd->lmd_mgs_failnodes = 1;

        /* Random uuid for MGC allows easier reconnects */
        OBD_ALLOC_PTR(uuid);
        ll_generate_random_uuid(uuidc);
        class_uuid_unparse(uuidc, uuid);

        /* Start the MGC */
        rc = lustre_start_simple(mgcname, LUSTRE_MGC_NAME,
                                 (char *)uuid->uuid, LUSTRE_MGS_OBDNAME,
                                 niduuid);
        OBD_FREE_PTR(uuid);
        if (rc)
                GOTO(out_free, rc);

        /* Add any failover MGS nids (must be done after mgc is started
         * due to lcfg design) */
        i = 1;
        while (ptr && ((*ptr == ':') ||
                       class_find_param(ptr, PARAM_MGSNODE, &ptr) == 0)) {
                int j = 0;

                /* New failover node */
                sprintf(niduuid, "%s_%x", mgcname, i);
                while (class_parse_nid(ptr, &nid, &ptr, 1) == 0) {
                        j++;
                        rc = do_lcfg(mgcname, nid,
                                     LCFG_ADD_UUID, niduuid, 0,0,0);
                        if (*ptr == ':')
                                break;
                }
                if (j > 0) {
                        rc = do_lcfg(mgcname, 0, LCFG_ADD_CONN,
                                     niduuid, 0, 0, 0);
                        i++;
                } else {
                        /* at ":/fsname" */
                        break;
                }
        }
        lsi->lsi_lmd->lmd_mgs_failnodes = i;

        obd = class_name2obd(mgcname);
        if (!obd) {
                CERROR("Can't find mgcobd %s\n", mgcname);
                GOTO(out_free, rc = -ENOTCONN);
        }

        rc = obd_set_info_async(obd->obd_self_export,
                                strlen(KEY_MGSSEC), KEY_MGSSEC,
                                strlen(mgssec), mgssec, NULL);
        if (rc)
                GOTO(out_free, rc);

        /* Keep a refcount of servers/clients who started with "mount",
           so we know when we can get rid of the mgc. */
        cfs_atomic_set(&obd->u.cli.cl_mgc_refcount, 1);

        /* Try all connections, but only once. */
        recov_bk = 1;
        rc = obd_set_info_async(obd->obd_self_export,
                                sizeof(KEY_INIT_RECOV_BACKUP),
                                KEY_INIT_RECOV_BACKUP,
                                sizeof(recov_bk), &recov_bk, NULL);
        if (rc)
                /* nonfatal */
                CWARN("can't set %s %d\n", KEY_INIT_RECOV_BACKUP, rc);
        /* We connect to the MGS at setup, and don't disconnect until cleanup */
        OBD_ALLOC_PTR(data);
        if (data == NULL)
                GOTO(out, rc = -ENOMEM);
        data->ocd_connect_flags = OBD_CONNECT_VERSION | OBD_CONNECT_FID |
                                  OBD_CONNECT_AT | OBD_CONNECT_FULL20;
        data->ocd_version = LUSTRE_VERSION_CODE;
        rc = obd_connect(NULL, &exp, obd, &(obd->obd_uuid), data, NULL);
        OBD_FREE_PTR(data);
        if (rc) {
                CERROR("connect failed %d\n", rc);
                GOTO(out, rc);
        }

        obd->u.cli.cl_mgc_mgsexp = exp;

out:
        /* Keep the mgc info in the lsi. Note that many lsi's can point
           to the same mgc.*/
        lsi->lsi_mgc = obd;
out_free:
        cfs_mutex_up(&mgc_start_lock);

        if (mgcname)
                OBD_FREE(mgcname, len);
        if (niduuid)
                OBD_FREE(niduuid, len + 2);
        RETURN(rc);
}

static int lustre_stop_mgc(struct lustre_sb_info *lsi)
{
        struct obd_device *obd;
        char *niduuid = 0, *ptr = 0;
        int i, rc = 0, len = 0;
        ENTRY;

        if (!lsi)
                RETURN(-ENOENT);
        obd = lsi->lsi_mgc;
        if (!obd)
                RETURN(-ENOENT);
        lsi->lsi_mgc = NULL;

        cfs_mutex_down(&mgc_start_lock);
        LASSERT(cfs_atomic_read(&obd->u.cli.cl_mgc_refcount) > 0);
        if (!cfs_atomic_dec_and_test(&obd->u.cli.cl_mgc_refcount)) {
                /* This is not fatal, every client that stops
                   will call in here. */
                CDEBUG(D_MOUNT, "mgc still has %d references.\n",
                       cfs_atomic_read(&obd->u.cli.cl_mgc_refcount));
                GOTO(out, rc = -EBUSY);
        }

        /* The MGC has no recoverable data in any case.
         * force shotdown set in umount_begin */
        obd->obd_no_recov = 1;

        if (obd->u.cli.cl_mgc_mgsexp) {
                /* An error is not fatal, if we are unable to send the
                   disconnect mgs ping evictor cleans up the export */
                rc = obd_disconnect(obd->u.cli.cl_mgc_mgsexp);
                if (rc)
                        CDEBUG(D_MOUNT, "disconnect failed %d\n", rc);
        }

        /* Save the obdname for cleaning the nid uuids, which are
           obdname_XX */
        len = strlen(obd->obd_name) + 6;
        OBD_ALLOC(niduuid, len);
        if (niduuid) {
                strcpy(niduuid, obd->obd_name);
                ptr = niduuid + strlen(niduuid);
        }

        rc = class_manual_cleanup(obd);
        if (rc)
                GOTO(out, rc);

        /* Clean the nid uuids */
        if (!niduuid)
                GOTO(out, rc = -ENOMEM);

        for (i = 0; i < lsi->lsi_lmd->lmd_mgs_failnodes; i++) {
                sprintf(ptr, "_%x", i);
                rc = do_lcfg(LUSTRE_MGC_OBDNAME, 0, LCFG_DEL_UUID,
                             niduuid, 0, 0, 0);
                if (rc)
                        CERROR("del MDC UUID %s failed: rc = %d\n",
                               niduuid, rc);
        }
out:
        if (niduuid)
                OBD_FREE(niduuid, len);

        /* class_import_put will get rid of the additional connections */
        cfs_mutex_up(&mgc_start_lock);
        RETURN(rc);
}

/* Since there's only one mgc per node, we have to change it's fs to get
   access to the right disk. */
static int server_mgc_set_fs(struct obd_device *mgc, struct lustre_sb_info *lsi)
{
        int rc;
        ENTRY;

        CDEBUG(D_MOUNT, "Set mgc disk for %s\n", lsi->lsi_lmd->lmd_dev);

        /* cl_mgc_sem in mgc insures we sleep if the mgc_fs is busy */
        rc = obd_set_info_async(mgc->obd_self_export,
                                sizeof(KEY_SET_FS), KEY_SET_FS,
                                sizeof(*lsi), lsi, NULL);
        if (rc) {
                CERROR("can't set_fs %d\n", rc);
        }

        RETURN(rc);
}

static int server_mgc_clear_fs(struct obd_device *mgc)
{
        int rc;
        ENTRY;

        CDEBUG(D_MOUNT, "Unassign mgc disk\n");

        rc = obd_set_info_async(mgc->obd_self_export,
                                sizeof(KEY_CLEAR_FS), KEY_CLEAR_FS,
                                0, NULL, NULL);
        RETURN(rc);
}

CFS_DECLARE_MUTEX(server_start_lock);

/* Stop MDS/OSS if nobody is using them */
static int server_stop_servers(int lddflags, int lsiflags)
{
        struct obd_device *obd = NULL;
        struct obd_type *type = NULL;
        int rc = 0;
        ENTRY;

        cfs_mutex_down(&server_start_lock);

        /* Either an MDT or an OST or neither  */
        /* if this was an MDT, and there are no more MDT's, clean up the MDS */
        if ((lddflags & LDD_F_SV_TYPE_MDT) &&
            (obd = class_name2obd(LUSTRE_MDS_OBDNAME))) {
                /*FIXME pre-rename, should eventually be LUSTRE_MDT_NAME*/
                type = class_search_type(LUSTRE_MDS_NAME);
        }
        /* if this was an OST, and there are no more OST's, clean up the OSS */
        if ((lddflags & LDD_F_SV_TYPE_OST) &&
            (obd = class_name2obd(LUSTRE_OSS_OBDNAME))) {
                type = class_search_type(LUSTRE_OST_NAME);
        }

        if (obd && (!type || !type->typ_refcnt)) {
                int err;
                obd->obd_force = 1;
                /* obd_fail doesn't mean much on a server obd */
                err = class_manual_cleanup(obd);
                if (!rc)
                        rc = err;
        }

        cfs_mutex_up(&server_start_lock);

        RETURN(rc);
}

int server_mti_print(char *title, struct mgs_target_info *mti)
{
        PRINT_CMD(PRINT_MASK, "mti %s\n", title);
        PRINT_CMD(PRINT_MASK, "server: %s\n", mti->mti_svname);
        PRINT_CMD(PRINT_MASK, "fs:     %s\n", mti->mti_fsname);
        PRINT_CMD(PRINT_MASK, "uuid:   %s\n", mti->mti_uuid);
        PRINT_CMD(PRINT_MASK, "ver: %d  flags: %#x\n",
                  mti->mti_config_ver, mti->mti_flags);
        return(0);
}

/* Get the fsname from the obd name.
   fsname must have at least 'strlen(svname) + 1' chars.
   rc < 0  on error
   if endptr isn't NULL it is set to end of fsname */
static int server_name2fsname(char *svname, char *fsname, char **endptr)
{
        char *dash = strrchr(svname, '-');
        if (!dash) {
                dash = strrchr(svname, ':');
                if (!dash)
                        return -EINVAL;
        }

        /* interpret <fsname>-MDTXXXXX-mdc as mdt, the better way is to pass
         * in the fsname, then determine the server index */
        if (!strcmp(LUSTRE_MDC_NAME, dash + 1)) {
                dash--;
                for (; dash > svname && *dash != '-' && *dash != ':'; dash--);
                if (dash == svname)
                        return -EINVAL;
        }

        if (fsname != NULL) {
                strncpy(fsname, svname, dash - svname);
                fsname[dash - svname] = '\0';
        }

        if (endptr != NULL)
                *endptr = dash;

        return 0;
}

/**
 * Get service name (svname) from string
 * rc < 0 on error
 * if endptr isn't NULL it is set to end of fsname *
 */

int server_name2svname(char *label, char *svname, char **endptr)
{
        int rc;
        char *dash;

        /* We use server_name2fsname() just for parsing */
        rc = server_name2fsname(label, NULL, &dash);
        if (rc != 0)
                return rc;

        if (*dash == ':')
                strncpy(svname, dash + 1, MTI_NAME_MAXLEN);
        else
                strncpy(svname, label, MTI_NAME_MAXLEN);

        return 0;
}


/* Get the index from the obd name.
   rc = server type, or
   rc < 0  on error
   if endptr isn't NULL it is set to end of name */
int server_name2index(char *svname, __u32 *idx, char **endptr)
{
        unsigned long index;
        int rc;
        char *dash;

        /* We use server_name2fsname() just for parsing */
        rc = server_name2fsname(svname, NULL, &dash);
        if (rc != 0)
                return rc;

        if (*dash == ':')
                rc |= LDD_F_VIRGIN;

        dash++;

        if (strncmp(dash, "MDT", 3) == 0)
                rc |= LDD_F_SV_TYPE_MDT;
        else if (strncmp(dash, "OST", 3) == 0)
                rc |= LDD_F_SV_TYPE_OST;
        else
                return(-EINVAL);

        dash += 3;

        if (strcmp(dash, "all") == 0)
                return rc | LDD_F_SV_ALL;

        if (strcmp(dash, "ffff") == 0) {
                rc |= LDD_F_NEED_INDEX;
                *idx = 65535;
                return rc;
        }

        if (*dash == 'u') {
                rc |= LDD_F_NEED_INDEX;
                dash++;
        }

        index = simple_strtoul(dash, endptr, 16);
        *idx = index;
        return rc;
}

static int server_label2mti(struct lustre_sb_info *lsi, struct mgs_target_info *mti)
{
        struct dt_device_param  dt_param;
        char                   *label;
        int                     rc;

        LASSERT(lsi);
        LASSERT(lsi->lsi_dt_dev);

        /* first, retrieve label */
        label = mconf_get_label(lsi->lsi_dt_dev);
        if (label == NULL)
                return -EINVAL;

        lsi->lsi_dt_dev->dd_ops->dt_conf_get(NULL, lsi->lsi_dt_dev, &dt_param);
        lsi->lsi_ldd->ldd_mount_type = dt_param.ddp_mount_type;

        rc = server_name2fsname(label, mti->mti_fsname, NULL);
        if (rc != 0)
                return rc;

        rc = server_name2svname(label, mti->mti_svname, NULL);
        if (rc != 0)
                return rc;

        rc = server_name2index(label, &mti->mti_stripe_index, NULL);
        if (rc < 0)
                return rc;

        mti->mti_flags = rc;
        if (mti->mti_flags & LDD_F_VIRGIN)
                mti->mti_flags |= LDD_F_UPDATE;

        return 0;
}

static int server_lsi2mti(struct lustre_sb_info *lsi, struct mgs_target_info *mti)
{
        struct lustre_disk_data  *ldd = lsi->lsi_ldd;
        lnet_process_id_t         id;
        int                       i = 0;
        ENTRY;

        if (!(lsi->lsi_flags & LSI_SERVER))
                RETURN(-EINVAL);

        mti->mti_nid_count = 0;
        while (LNetGetId(i++, &id) != -ENOENT) {
                if (LNET_NETTYP(LNET_NIDNET(id.nid)) == LOLND)
                        continue;

                if (class_find_param(ldd->ldd_params,
                                     PARAM_NETWORK, NULL) == 0 &&
                    !class_match_net(ldd->ldd_params, id.nid)) {
                        /* can't match specified network */
                        continue;
                }

                mti->mti_nids[mti->mti_nid_count] = id.nid;
                mti->mti_nid_count++;
                if (mti->mti_nid_count >= MTI_NIDS_MAX) {
                        CWARN("Only using first %d nids for %s\n",
                              mti->mti_nid_count, mti->mti_svname);
                        break;
                }
        }

        mti->mti_lustre_ver = LUSTRE_VERSION_CODE;
        mti->mti_config_ver = 0;

        if (ldd->ldd_magic == 0) {
                /* no config, generate data for registration */
                RETURN(server_label2mti(lsi, mti));
        }

        strncpy(mti->mti_fsname, ldd->ldd_fsname,
                sizeof(mti->mti_fsname));
        strncpy(mti->mti_svname, ldd->ldd_svname,
                sizeof(mti->mti_svname));

        if (lsi->lsi_lmd->lmd_flags & LMD_FLG_WRITECONF)
                ldd->ldd_flags |= LDD_F_WRITECONF;
        mti->mti_flags = ldd->ldd_flags;
        mti->mti_stripe_index = ldd->ldd_svindex;
        memcpy(mti->mti_uuid, ldd->ldd_uuid, sizeof(mti->mti_uuid));
        if (strlen(ldd->ldd_params) > sizeof(mti->mti_params)) {
                CERROR("params too big for mti\n");
                RETURN(-ENOMEM);
        }
        memcpy(mti->mti_params, ldd->ldd_params, sizeof(mti->mti_params));
        RETURN(0);
}

static int mconf_set_label(struct dt_device *dt, char *label)
{
        struct lu_env          env;
        int                    rc;

        lu_env_init(&env, dt->dd_lu_dev.ld_type->ldt_ctx_tags);
        rc = dt->dd_ops->dt_label_set(&env, dt, label);
        lu_env_fini(&env);
        return rc;
}

static char *mconf_get_label(struct dt_device *dt)
{
        struct lu_env          env;
        char                  *label;

        lu_env_init(&env, dt->dd_lu_dev.ld_type->ldt_ctx_tags);
        label = dt->dd_ops->dt_label_get(&env, dt);
        lu_env_fini(&env);

        return label;
}

static int mconf_sync_dev(struct dt_device *dt)
{
        return 0;
}


/* Register an old or new target with the MGS. If needed MGS will construct
   startup logs and assign index */
static int server_register_target(struct lustre_sb_info *lsi)
{
        struct obd_device *mgc = lsi->lsi_mgc;
        struct lustre_disk_data *ldd = lsi->lsi_ldd;
        struct mgs_target_info *mti = NULL;
        int rc;
        ENTRY;

        LASSERT(mgc);

        if (!(lsi->lsi_flags & LSI_SERVER))
                RETURN(-EINVAL);

        OBD_ALLOC_PTR(mti);
        if (!mti)
                RETURN(-ENOMEM);
        rc = server_lsi2mti(lsi, mti);
        if (rc)
                GOTO(out, rc);

        CDEBUG(D_MOUNT, "Registration %s, fs=%s, %s, index=%04x, flags=%#x\n",
               mti->mti_svname, mti->mti_fsname,
               libcfs_nid2str(mti->mti_nids[0]), mti->mti_stripe_index,
               mti->mti_flags);

        /* Register the target */
        /* FIXME use mgc_process_config instead */
        rc = obd_set_info_async(mgc->u.cli.cl_mgc_mgsexp,
                                sizeof(KEY_REGISTER_TARGET), KEY_REGISTER_TARGET,
                                sizeof(*mti), mti, NULL);
        if (rc)
                GOTO(out, rc);

        /* we don't have persistent ldd probably,
         * but MGS * supplies us withservice name */
        ldd->ldd_svindex = mti->mti_stripe_index;
        strncpy(ldd->ldd_svname, mti->mti_svname,
                        sizeof(ldd->ldd_svname));

        /* Always update our flags */
        ldd->ldd_flags = mti->mti_flags & ~LDD_F_REWRITE_LDD;

        /* If this flag is set, it means the MGS wants us to change our
           on-disk data. (So far this means just the index.) */
        if (mti->mti_flags & LDD_F_REWRITE_LDD) {
                char *label;
                int err;
                CDEBUG(D_MOUNT, "Changing on-disk index from %#x to %#x "
                       "for %s\n", ldd->ldd_svindex, mti->mti_stripe_index,
                       mti->mti_svname);
                /* or ldd_make_sv_name(ldd); */
                ldd_write(lsi->lsi_dt_dev, ldd);
                err = mconf_set_label(lsi->lsi_dt_dev, mti->mti_svname);
                if (err)
                        CERROR("Label set error %d\n", err);
                label = mconf_get_label(lsi->lsi_dt_dev);
                if (label)
                        CDEBUG(D_MOUNT, "Disk label changed to %s\n", label);

                /* Flush the new ldd to disk */
                mconf_sync_dev(lsi->lsi_dt_dev);
        }

out:
        if (mti)
                OBD_FREE_PTR(mti);
        RETURN(rc);
}

static void stop_temp_site(struct lustre_sb_info *lsi)
{
        struct lu_env          env;
        struct mconf_device   *mdev;
        int                    rc;
        struct lu_site        *site;

        LASSERT(lsi);
        LASSERT(lsi->lsi_dt_dev);

        site = lsi->lsi_dt_dev->dd_lu_dev.ld_site;
        if (site == NULL)
                return;

        mdev = mconf_dev(site->ls_top_dev);
        LASSERT(mdev);

        rc = lu_env_init(&env, mdev->mcf_lu_dev.ld_type->ldt_ctx_tags);
        LASSERT(rc == 0);
        lu_site_purge(&env, site, ~0);
        lu_site_fini(site);
        lu_env_fini(&env);
        lsi->lsi_dt_dev->dd_lu_dev.ld_site = NULL;
        OBD_FREE_PTR(site);
        lu_device_fini(&mdev->mcf_lu_dev);
        OBD_FREE_PTR(mdev);
}

/** Start server targets: MDTs and OSTs
 */
static int server_start_targets(struct lustre_sb_info *lsi, struct dt_device *dt)
{
        struct obd_device *obd;
        struct config_llog_instance cfg;
        int rc;
        ENTRY;

        CDEBUG(D_MOUNT, "starting target %s\n", lsi->lsi_ldd->ldd_svname);

#if 0
        /* If we're an MDT, make sure the global MDS is running */
        if (lsi->lsi_ldd->ldd_flags & LDD_F_SV_TYPE_MDT) {
                /* make sure the MDS is started */
                cfs_mutex_down(&server_start_lock);
                obd = class_name2obd(LUSTRE_MDS_OBDNAME);
                if (!obd) {
                        rc = lustre_start_simple(LUSTRE_MDS_OBDNAME,
                    /* FIXME pre-rename, should eventually be LUSTRE_MDS_NAME */
                                                 LUSTRE_MDT_NAME,
                                                 LUSTRE_MDS_OBDNAME"_uuid",
                                                 0, 0);
                        if (rc) {
                                cfs_mutex_up(&server_start_lock);
                                CERROR("failed to start MDS: %d\n", rc);
                                RETURN(rc);
                        }
                }
                cfs_mutex_up(&server_start_lock);
        }
#endif

        /* Register with MGS */
        rc = server_register_target(lsi);

        /* destroy temporary site */
        stop_temp_site(lsi);

        /* If we're an OST, make sure the global OSS is running */
        if (lsi->lsi_ldd->ldd_flags & LDD_F_SV_TYPE_OST) {
                /* make sure OSS is started */
                cfs_mutex_down(&server_start_lock);
                obd = class_name2obd(LUSTRE_OSS_OBDNAME);
                if (!obd) {
                        rc = lustre_start_simple(LUSTRE_OSS_OBDNAME,
                                                 LUSTRE_OSS_NAME,
                                                 LUSTRE_OSS_OBDNAME"_uuid",
                                                 0, 0);
                        if (rc) {
                                cfs_mutex_up(&server_start_lock);
                                CERROR("failed to start OSS: %d\n", rc);
                                RETURN(rc);
                        }
                }
                cfs_mutex_up(&server_start_lock);
        }

        /* Set the mgc fs to our server disk.  This allows the MGC to
         * read and write configs locally, in case it can't talk to the MGS. */
        rc = server_mgc_set_fs(lsi->lsi_mgc, lsi);
        if (rc)
                RETURN(rc);

        if (rc && (lsi->lsi_ldd->ldd_flags &
                   (LDD_F_NEED_INDEX | LDD_F_UPDATE | LDD_F_UPGRADE14))){
                CERROR("Required registration failed for %s: %d\n",
                       lsi->lsi_ldd->ldd_svname, rc);
                if (rc == -EIO) {
                        LCONSOLE_ERROR_MSG(0x15f, "Communication error with "
                                           "the MGS.  Is the MGS running?\n");
                }
                GOTO(out_mgc, rc);
        }
        if (rc == -EINVAL) {
                LCONSOLE_ERROR_MSG(0x160, "The MGS is refusing to allow this "
                                   "server (%s) to start. Please see messages"
                                   " on the MGS node.\n",
                                   lsi->lsi_ldd->ldd_svname);
                GOTO(out_mgc, rc);
        }
        /* non-fatal error of registeration with MGS */
        if (rc)
                CDEBUG(D_MOUNT, "Cannot register with MGS: %d\n", rc);

        /* Let the target look up the mount using the target's name */
        rc = server_register_mount(lsi->lsi_ldd->ldd_svname, lsi, dt);
        if (rc)
                GOTO(out_mgc, rc);

        /* Start targets using the llog named for the target */
        memset(&cfg, 0, sizeof(cfg));
        rc = lustre_log_process(lsi, lsi->lsi_ldd->ldd_svname, &cfg);
        if (rc) {
                CERROR("failed to start server %s: %d\n",
                       lsi->lsi_ldd->ldd_svname, rc);
                /* Do NOT call server_deregister_mount() here. This makes it
                 * impossible to find mount later in cleanup time and leaves
                 * @lsi and othder stuff leaked. -umka */
                GOTO(out_mgc, rc);
        }

out_mgc:
        /* Release the mgc fs for others to use */
        server_mgc_clear_fs(lsi->lsi_mgc);

        if (!rc) {
                obd = class_name2obd(lsi->lsi_ldd->ldd_svname);
                if (!obd) {
                        CERROR("no server named %s was started\n",
                               lsi->lsi_ldd->ldd_svname);
                        RETURN(-ENXIO);
                }

                if ((lsi->lsi_lmd->lmd_flags & LMD_FLG_ABORT_RECOV) &&
                    (OBP(obd, iocontrol))) {
                        obd_iocontrol(OBD_IOC_ABORT_RECOVERY,
                                      obd->obd_self_export, 0, NULL, NULL);
                }

                /* log has been fully processed */
                obd_notify(obd, NULL, OBD_NOTIFY_CONFIG, (void *)CONFIG_LOG);
        }

        RETURN(rc);
}

/***************** lustre superblock **************/

static struct lustre_sb_info *lustre_init_lsi(void *osvfsp)
{
        struct lustre_sb_info *lsi;
        ENTRY;

        OBD_ALLOC_PTR(lsi);
        if (!lsi)
                RETURN(NULL);
        OBD_ALLOC_PTR(lsi->lsi_lmd);
        if (!lsi->lsi_lmd) {
                OBD_FREE_PTR(lsi);
                RETURN(NULL);
        }

        lsi->lsi_lmd->lmd_exclude_count = 0;
        lsi->lsi_lmd->lmd_recovery_time_soft = 0;
        lsi->lsi_lmd->lmd_recovery_time_hard = 0;
        /* we take 1 extra ref for our setup */
        cfs_atomic_set(&lsi->lsi_mounts, 1);
        /* Default umount style */
        lsi->lsi_flags = LSI_UMOUNT_FAILOVER;

        lustre_osvfs_update(osvfsp, lsi);
        lsi->lsi_vfsp = osvfsp;

        RETURN(lsi);
}

static int lustre_free_lsi(struct lustre_sb_info *lsi)
{
        ENTRY;

        LASSERT(lsi != NULL);
        CDEBUG(D_MOUNT, "Freeing lsi %p\n", lsi);

        /* someone didn't call server_put_mount. */
        LASSERT(cfs_atomic_read(&lsi->lsi_mounts) == 0);

        if (lsi->lsi_ldd != NULL)
                OBD_FREE(lsi->lsi_ldd, sizeof(*lsi->lsi_ldd));

        if (lsi->lsi_lmd != NULL) {
                if (lsi->lsi_lmd->lmd_dev != NULL)
                        OBD_FREE(lsi->lsi_lmd->lmd_dev,
                                 strlen(lsi->lsi_lmd->lmd_dev) + 1);
                if (lsi->lsi_lmd->lmd_profile != NULL)
                        OBD_FREE(lsi->lsi_lmd->lmd_profile,
                                 strlen(lsi->lsi_lmd->lmd_profile) + 1);
                if (lsi->lsi_lmd->lmd_mgssec != NULL)
                        OBD_FREE(lsi->lsi_lmd->lmd_mgssec,
                                 strlen(lsi->lsi_lmd->lmd_mgssec) + 1);
                if (lsi->lsi_lmd->lmd_opts != NULL)
                        OBD_FREE(lsi->lsi_lmd->lmd_opts,
                                 strlen(lsi->lsi_lmd->lmd_opts) + 1);
                if (lsi->lsi_lmd->lmd_exclude_count)
                        OBD_FREE(lsi->lsi_lmd->lmd_exclude,
                                 sizeof(lsi->lsi_lmd->lmd_exclude[0]) *
                                 lsi->lsi_lmd->lmd_exclude_count);
                if (lsi->lsi_lmd->lmd_mgs)
                        OBD_FREE(lsi->lsi_lmd->lmd_mgs,
                                        strlen(lsi->lsi_lmd->lmd_mgs) + 1);
                OBD_FREE(lsi->lsi_lmd, sizeof(*lsi->lsi_lmd));
        }

        LASSERT(lsi->lsi_llsbi == NULL);

        lustre_osvfs_update(lsi->lsi_vfsp, NULL);
        lsi->lsi_vfsp = NULL;

        OBD_FREE(lsi, sizeof(*lsi));

        RETURN(0);
}

/* The lsi has one reference for every server that is using the disk -
   e.g. MDT, MGS, and potentially MGC */
static int lustre_put_lsi(struct lustre_sb_info *lsi)
{
        ENTRY;

        LASSERT(lsi != NULL);

        CDEBUG(D_MOUNT, "put %p %d\n", lsi, cfs_atomic_read(&lsi->lsi_mounts));
        if (cfs_atomic_dec_and_test(&lsi->lsi_mounts)) {
                lustre_free_lsi(lsi);
                RETURN(1);
        }
        RETURN(0);
}

/*************** server mount ******************/

static int mconf_object_init(const struct lu_env *env, struct lu_object *o,
                             const struct lu_object_conf *_)
{
        struct mconf_device *d = mconf_dev(o->lo_dev);
        struct lu_device  *under;
        struct lu_object  *below;
        int                rc = 0;
        ENTRY;

        under = d->mcf_bottom;
        below = under->ld_ops->ldo_object_alloc(env, o->lo_header, under);
        if (below != NULL) {
                lu_object_add(o, below);
        } else
                rc = -ENOMEM;

        RETURN(rc);
}

static void mconf_object_free(const struct lu_env *env, struct lu_object *o)
{
        struct mconf_object *mo = mconf_obj(o);
        struct lu_object_header *h;
        ENTRY;

        h = o->lo_header;
        CDEBUG(D_INFO, "object free, fid = "DFID"\n",
               PFID(lu_object_fid(o)));

        lu_object_fini(o);
        lu_object_header_fini(h);
        OBD_FREE_PTR(mo);
        EXIT;
}


static const struct lu_object_operations mconf_obj_ops = {
        .loo_object_init    = mconf_object_init,
        .loo_object_free    = mconf_object_free
};

static struct lu_object *
mconf_object_alloc(const struct lu_env *env,
                   const struct lu_object_header *hdr,
                   struct lu_device *d)
{
        struct mconf_object *mo;

        ENTRY;

        OBD_ALLOC_PTR(mo);
        if (mo != NULL) {
                struct lu_object *o;
                struct lu_object_header *h;

                o = &mo->mco_obj;
                h = &mo->mco_header;
                lu_object_header_init(h);
                lu_object_init(o, h, d);
                lu_object_add_top(h, o);
                o->lo_ops = &mconf_obj_ops;
                RETURN(o);
        } else
                RETURN(NULL);
}

static const struct lu_device_operations mconf_lu_ops = {
        .ldo_object_alloc   = mconf_object_alloc,
};

static struct lu_device_type_operations mconf_device_type_ops = {
};

static struct lu_device_type mconf_device_type = {
        .ldt_tags     = LU_DEVICE_MC,
        .ldt_name     = LUSTRE_MCF_NAME,
        .ldt_ops      = &mconf_device_type_ops,
        .ldt_ctx_tags = LCT_DT_THREAD
};


static struct lu_device *try_start_osd(struct lustre_mount_data *lmd,
                                       char *typename,
                                       unsigned long mflags)
{
        struct obd_type       *type = NULL;
        struct lu_device_type *ldt = NULL;
        struct lu_device      *d = NULL;
        struct dt_device      *dt;
        struct lu_env          env;
        int                    rc = 0;
        struct                 lustre_cfg *lcfg;
        struct                 lustre_cfg_bufs bufs;


        /* find the type */
        type = class_get_type(typename);
        if (!type) {
                CERROR("Unknown type: '%s'\n", typename);
                GOTO(out, rc = -ENODEV);
        }

        ldt = type->typ_lu;
        if (ldt == NULL) {
                CERROR("type: '%s'\n", typename);
                GOTO(out_type, rc = -EINVAL);
        }

        rc = lu_env_init(&env, ldt->ldt_ctx_tags);
        LASSERT(rc == 0);

        ldt->ldt_obd_type = type;
        d = ldt->ldt_ops->ldto_device_alloc(&env, ldt, NULL);
        if (IS_ERR(d)) {
                CERROR("Cannot allocate device: '%s'\n", typename);
                GOTO(out_env, rc = -ENODEV);
        }

        type->typ_refcnt++;
        rc = ldt->ldt_ops->ldto_device_init(&env, d, NULL, NULL);
        if (rc) {
                CERROR("can't init device '%s', rc %d\n", typename, rc);
                GOTO(out_alloc, rc);
        }

        /* ask osd to mount underlying disk filesystem */
        lustre_cfg_bufs_reset(&bufs, lmd->lmd_dev);
        lustre_cfg_bufs_set(&bufs, 1, &mflags, sizeof(mflags));
        lcfg = lustre_cfg_new(LCFG_SETUP, &bufs);
        dt = lu2dt_dev(d);
        rc = dt->dd_lu_dev.ld_ops->ldo_process_config(&env, d, lcfg);
        lustre_cfg_free(lcfg);

        if (rc)
                GOTO(out_init, rc);

        lu_device_get(d);
        lu_ref_add(&d->ld_reference, "lu-stack", &lu_site_init);
        lu_env_fini(&env);
        RETURN(d);

out_init:
        ldt->ldt_ops->ldto_device_fini(&env, d);
out_alloc:
        type->typ_refcnt--;
        ldt->ldt_ops->ldto_device_free(&env, d);
out_env:
        lu_env_fini(&env);
out_type:
        class_put_type(type);
out:
        LASSERT(rc);
        d = ERR_PTR(rc);
        RETURN(d);
}

static struct lu_device *start_osd(struct lustre_mount_data *lmd,
                                   unsigned long mflags)
{
        struct lu_device *d;
        ENTRY;

        d = try_start_osd(lmd, LUSTRE_OSD_NAME, mflags);
        if (IS_ERR(d))
                d = try_start_osd(lmd, LUSTRE_ZFS_NAME, mflags);
        RETURN(d);
}

/* XXX: to stop OSD used by standalone MGS */
static void stop_osd(struct dt_device *dt)
{
        struct lustre_cfg_bufs  *bufs;
        struct lustre_cfg       *lcfg;
        struct lu_device        *d;
        struct lu_device_type   *ldt;
        struct obd_type         *type;
        struct lu_env            env;
        char                     flags[3]="";
        int                      rc;

        OBD_ALLOC(bufs, sizeof(*bufs));
        if (bufs == NULL) {
                CERROR("Cannot alloc bufs!\n");
                return;
        }

        LASSERT(dt);
        d = &dt->dd_lu_dev;

        ldt = d->ld_type;
        LASSERT(ldt);

        type = ldt->ldt_obd_type;
        LASSERT(type != NULL);

        rc = lu_env_init(&env, ldt->ldt_ctx_tags);
        LASSERT(rc == 0);

        /* process cleanup, pass mdt obd name to get obd umount flags */
        lustre_cfg_bufs_reset(bufs, NULL);
        lustre_cfg_bufs_set_string(bufs, 1, flags);
        lcfg = lustre_cfg_new(LCFG_CLEANUP, bufs);
        if (!lcfg) {
                CERROR("Cannot alloc lcfg!\n");
                OBD_FREE(bufs, sizeof(*bufs));
                return;
        }

        d->ld_ops->ldo_process_config(&env, d, lcfg);
        lustre_cfg_free(lcfg);

        lu_ref_del(&d->ld_reference, "lu-stack", &lu_site_init);
        lu_device_put(d);

        type->typ_refcnt--;
        ldt->ldt_ops->ldto_device_fini(&env, d);
        ldt->ldt_ops->ldto_device_free(&env, d);
        lu_env_fini(&env);

        class_put_type(type);

        OBD_FREE(bufs, sizeof(*bufs));
}

static int ldd_parse(struct mconf_device *mdev, struct lustre_disk_data *ldd)
{
        struct dt_device      *dev = lu2dt_dev(mdev->mcf_bottom);
        struct lu_env          env;
        int                    rc;
        struct dt_object      *file;
        struct lu_fid          fid;
        struct lu_buf          buf;
        loff_t                 pos;

        rc = lu_env_init(&env, mdev->mcf_lu_dev.ld_type->ldt_ctx_tags);
        LASSERT(rc == 0);

        file = dt_store_open(&env, dev, MOUNT_CONFIGS_DIR, CONFIGS_FILE, &fid);
        if (IS_ERR(file))
                GOTO(out, rc = PTR_ERR(file));

        buf.lb_buf = ldd;
        buf.lb_len = sizeof(*ldd);
        pos = 0;
        rc = file->do_body_ops->dbo_read(&env, file, &buf, &pos, BYPASS_CAPA);
        if (rc == buf.lb_len) {
                if (ldd->ldd_magic != LDD_MAGIC) {
                        /* FIXME add swabbing support */
                        CERROR("Bad magic in %s: %x!=%x\n", MOUNT_DATA_FILE,
                                        ldd->ldd_magic, LDD_MAGIC);
                        GOTO(out, rc = -EINVAL);
                }

                if (ldd->ldd_feature_incompat & ~LDD_INCOMPAT_SUPP) {
                        CERROR("%s: unsupported incompat filesystem feature(s) %x\n",
                               ldd->ldd_svname,
                               ldd->ldd_feature_incompat & ~LDD_INCOMPAT_SUPP);
                        GOTO(out, rc = -EINVAL);
                }
                if (ldd->ldd_feature_rocompat & ~LDD_ROCOMPAT_SUPP) {
                        CERROR("%s: unsupported read-only filesystem feature(s) %x\n",
                               ldd->ldd_svname,
                               ldd->ldd_feature_rocompat & ~LDD_ROCOMPAT_SUPP);
                        /* Do something like remount filesystem read-only */
                        GOTO(out, rc = -EINVAL);
                }
                rc = 0;
        } else if (rc >= 0) {
                CERROR("disk data size does not match: see %d expect %Zd\n",
                       rc, buf.lb_len);
                rc = -EIO;
        }
        lu_object_put(&env, &file->do_lu);
out:
        lu_env_fini(&env);
        RETURN(rc);
}


/** Kernel mount using mount options in MOUNT_DATA_FILE.
 * Since this file lives on the disk, we pre-mount using a common
 * type, read the file, then re-mount using the type specified in the
 * file.
 */
static struct dt_device *server_kernel_mount(struct lustre_sb_info *lsi,
                                             unsigned long mflags)
{
        struct lustre_disk_data *ldd;
        struct lustre_mount_data *lmd = lsi->lsi_lmd;
        struct lu_device *dev;
        struct mconf_device *mdev;
        struct lu_site *site;
        int rc;

        ENTRY;

        OBD_ALLOC(ldd, sizeof(*ldd));
        if (!ldd)
                RETURN(ERR_PTR(-ENOMEM));
        lsi->lsi_ldd = ldd;

        /* start OSD on given device */
        dev = start_osd(lmd, mflags);
        if (IS_ERR(dev)) {
                OBD_FREE(ldd, sizeof(*ldd));
                lsi->lsi_ldd = NULL;
                RETURN((void *) dev);
        }

        lsi->lsi_dt_dev = lu2dt_dev(dev);

        /* create temporary site to access ldd */
        OBD_ALLOC_PTR(site);
        LASSERT(site);

        /* create temporary top-device to access ldd */
        OBD_ALLOC_PTR(mdev);
        LASSERT(mdev != NULL);
        lu_device_init(&mdev->mcf_lu_dev, &mconf_device_type);
        mdev->mcf_lu_dev.ld_ops = &mconf_lu_ops;
        mdev->mcf_bottom = dev;

        rc = lu_site_init(site, &mdev->mcf_lu_dev);
        LASSERT(rc == 0);
        dev->ld_site = site;
        rc = lu_site_init_finish(site);
        LASSERT(rc == 0);

        rc = ldd_parse(mdev, ldd);
        if (rc == -ENOENT) {
                /* no configuration found, use disk label */
                stop_temp_site(lsi);
        } else {
                if (rc != 0)
                        CERROR("Error reading disk data: %d\n", rc);
        }

        RETURN(lu2dt_dev(dev));
}

/** Wait here forever until the mount refcount is 0 before completing umount,
 * else we risk dereferencing a null pointer.
 * LNET may take e.g. 165s before killing zombies.
 */
static void server_wait_finished(struct lustre_sb_info *lsi)
{
       cfs_waitq_t             waitq;
       int                     rc, waited = 0;
       cfs_sigset_t            blocked;

       cfs_waitq_init(&waitq);

       while (cfs_atomic_read(&lsi->lsi_mounts) > 1) {
               if (waited && (waited % 30 == 0))
                       LCONSOLE_WARN("Mount still busy with %d refs after "
                                      "%d secs.\n",
                                      cfs_atomic_read(&lsi->lsi_mounts),
                                      waited);
               /* Cannot use l_event_wait() for an interruptible sleep. */
               waited += 3;
               blocked = l_w_e_set_sigs(sigmask(SIGKILL));
               cfs_waitq_wait_event_interruptible_timeout(
                       waitq,
                       (cfs_atomic_read(&lsi->lsi_mounts) == 1),
                       cfs_time_seconds(3),
                       rc);
               cfs_block_sigs(blocked);
               if (rc < 0) {
                       LCONSOLE_EMERG("Danger: interrupted umount %s with "
                                      "%d refs!\n",
                                      "FIXME(devname)",
                                      cfs_atomic_read(&lsi->lsi_mounts));
                       break;
               }
       }
}

/** Mount the device with the correct options.
 * Read the on-disk config file.
 * Start the services.
 */
static int lustre_server_mount(struct lustre_sb_info *lsi, unsigned long mflags)
{
        struct dt_device *dt;
        int rc;

        ENTRY;

        /* the One True Mount */
        dt = server_kernel_mount(lsi, mflags);
        if (IS_ERR(dt)) {
                rc = PTR_ERR(dt);
                CERROR("Unable to mount device %s: %d\n",
                       lsi->lsi_lmd->lmd_dev, rc);
                lustre_put_lsi(lsi);
                RETURN(rc);
        }

        LASSERT(lsi->lsi_ldd);
        CDEBUG(D_MOUNT, "Found service %s for fs '%s' on device %s\n",
               lsi->lsi_ldd->ldd_svname, lsi->lsi_ldd->ldd_fsname,
               lsi->lsi_lmd->lmd_dev);

        if (class_name2obd(lsi->lsi_ldd->ldd_svname)) {
                LCONSOLE_ERROR_MSG(0x161, "The target named %s is already "
                                   "running. Double-mount may have compromised"
                                   " the disk journal.\n",
                                   lsi->lsi_ldd->ldd_svname);
                lustre_put_lsi(lsi);
                /* XXX: cleanup osd */
                LBUG();
                RETURN(-EALREADY);
        }

        /* Start MGS before MGC */
        if (IS_MGS(lsi->lsi_ldd) && !(lsi->lsi_lmd->lmd_flags & LMD_FLG_NOMGS)){
                rc = server_start_mgs(lsi);
                if (rc)
                        GOTO(out_mnt, rc);
        }

        /* Start MGC before servers */
        rc = lustre_start_mgc(lsi);
        if (rc)
                GOTO(out_mnt, rc);

        /* Set up all obd devices for service */
        if (lsi->lsi_ldd->ldd_magic == 0) {
                /* no configuration found, try to register on MGS and get conf */
                rc = server_start_targets(lsi, dt);
                if (rc < 0) {
                        CERROR("Unable to start targets: %d\n", rc);
                        GOTO(out_mnt, rc);
                }
        } else if (!(lsi->lsi_lmd->lmd_flags & LMD_FLG_NOSVC) &&
                (IS_OST(lsi->lsi_ldd) || IS_MDT(lsi->lsi_ldd))) {

                rc = server_start_targets(lsi, dt);
                if (rc < 0) {
                        CERROR("Unable to start targets: %d\n", rc);
                        GOTO(out_mnt, rc);
                }
        } else {
                /* destroy temporary site */
                stop_temp_site(lsi);
        }

        rc = lustre_osvfs_mount(lsi->lsi_vfsp);
        if (rc)
                GOTO(out_mnt, rc);
 
        RETURN(0);
out_mnt:
        /* We jump here in case of failure while starting targets or MGS.
         * In this case we can't just put @mnt and have to do real cleanup
         * with stoping targets, etc. */
        stop_temp_site(lsi);
        lustre_server_umount(lsi);
        RETURN(rc);
}

/*************** mount common betweeen server and client ***************/

/* Common umount */
int lustre_common_umount(struct lustre_sb_info *lsi)
{
        int rc;
        ENTRY;

        CDEBUG(D_MOUNT, "dropping lsi %p\n", lsi);

        /* Drop a ref to the MGC */
        rc = lustre_stop_mgc(lsi);
        if (rc && (rc != -ENOENT)) {
                if (rc != -EBUSY) {
                        CERROR("Can't stop MGC: %d\n", rc);
                        RETURN(rc);
                }
                /* BUSY just means that there's some other obd that
                   needs the mgc.  Let him clean it up. */
                CDEBUG(D_MOUNT, "MGC still in use\n");
        }
        /* Drop a ref to the mounted disk */
        lustre_put_lsi(lsi);
        lu_types_stop();
        RETURN(rc);
}

static void lmd_print(struct lustre_mount_data *lmd)
{
        int i;

        PRINT_CMD(PRINT_MASK, "  mount data:\n");
        if (lmd_is_client(lmd))
                PRINT_CMD(PRINT_MASK, "profile: %s\n", lmd->lmd_profile);
        PRINT_CMD(PRINT_MASK, "device:  %s\n", lmd->lmd_dev);
        PRINT_CMD(PRINT_MASK, "flags:   %x\n", lmd->lmd_flags);

        if (lmd->lmd_opts)
                PRINT_CMD(PRINT_MASK, "options: %s\n", lmd->lmd_opts);

        if (lmd->lmd_recovery_time_soft)
                PRINT_CMD(PRINT_MASK, "recovery time soft: %d\n",
                          lmd->lmd_recovery_time_soft);

        if (lmd->lmd_recovery_time_hard)
                PRINT_CMD(PRINT_MASK, "recovery time hard: %d\n",
                          lmd->lmd_recovery_time_hard);

        for (i = 0; i < lmd->lmd_exclude_count; i++) {
                PRINT_CMD(PRINT_MASK, "exclude %d:  OST%04x\n", i,
                          lmd->lmd_exclude[i]);
        }
}

/* Is this server on the exclusion list */
int lustre_check_exclusion(struct lustre_sb_info *lsi, char *svname)
{
        struct lustre_mount_data *lmd = lsi->lsi_lmd;
        __u32 index;
        int i, rc;
        ENTRY;

        rc = server_name2index(svname, &index, NULL);
        if (rc != LDD_F_SV_TYPE_OST)
                /* Only exclude OSTs */
                RETURN(0);

        CDEBUG(D_MOUNT, "Check exclusion %s (%d) in %d of %s\n", svname,
               index, lmd->lmd_exclude_count, lmd->lmd_dev);

        for(i = 0; i < lmd->lmd_exclude_count; i++) {
                if (index == lmd->lmd_exclude[i]) {
                        CWARN("Excluding %s (on exclusion list)\n", svname);
                        RETURN(1);
                }
        }
        RETURN(0);
}

/* mount -v  -o exclude=lustre-OST0001:lustre-OST0002 -t lustre ... */
static int lmd_make_exclusion(struct lustre_mount_data *lmd, char *ptr)
{
        char *s1 = ptr, *s2;
        __u32 index, *exclude_list;
        int rc = 0, devmax;
        ENTRY;

        /* The shortest an ost name can be is 8 chars: -OST0000.
           We don't actually know the fsname at this time, so in fact
           a user could specify any fsname. */
        devmax = strlen(ptr) / 8 + 1;

        /* temp storage until we figure out how many we have */
        OBD_ALLOC(exclude_list, sizeof(index) * devmax);
        if (!exclude_list)
                RETURN(-ENOMEM);

        /* we enter this fn pointing at the '=' */
        while (*s1 && *s1 != ' ' && *s1 != ',') {
                s1++;
                rc = server_name2index(s1, &index, &s2);
                if (rc < 0) {
                        CERROR("Can't parse server name '%s'\n", s1);
                        break;
                }
                if (rc == LDD_F_SV_TYPE_OST)
                        exclude_list[lmd->lmd_exclude_count++] = index;
                else
                        CDEBUG(D_MOUNT, "ignoring exclude %.7s\n", s1);
                s1 = s2;
                /* now we are pointing at ':' (next exclude)
                   or ',' (end of excludes) */
                if (lmd->lmd_exclude_count >= devmax)
                        break;
        }
        if (rc >= 0) /* non-err */
                rc = 0;

        if (lmd->lmd_exclude_count) {
                /* permanent, freed in lustre_free_lsi */
                OBD_ALLOC(lmd->lmd_exclude, sizeof(index) *
                          lmd->lmd_exclude_count);
                if (lmd->lmd_exclude) {
                        memcpy(lmd->lmd_exclude, exclude_list,
                               sizeof(index) * lmd->lmd_exclude_count);
                } else {
                        rc = -ENOMEM;
                        lmd->lmd_exclude_count = 0;
                }
        }
        OBD_FREE(exclude_list, sizeof(index) * devmax);
        RETURN(rc);
}

static int lmd_parse_mgssec(struct lustre_mount_data *lmd, char *ptr)
{
        char   *tail;
        int     length;

        if (lmd->lmd_mgssec != NULL) {
                OBD_FREE(lmd->lmd_mgssec, strlen(lmd->lmd_mgssec) + 1);
                lmd->lmd_mgssec = NULL;
        }

        tail = strchr(ptr, ',');
        if (tail == NULL)
                length = strlen(ptr);
        else
                length = tail - ptr;

        OBD_ALLOC(lmd->lmd_mgssec, length + 1);
        if (lmd->lmd_mgssec == NULL)
                return -ENOMEM;

        memcpy(lmd->lmd_mgssec, ptr, length);
        lmd->lmd_mgssec[length] = '\0';
        return 0;
}

/* Collect multiple values for mgsnid specifiers */
static int lmd_parse_mgs(struct lustre_mount_data *lmd, char **ptr)
{
        lnet_nid_t nid;
        char *tail = *ptr;
        char *mgsnid;
        int   length;
        int   oldlen = 0;

        /* Find end of nidlist */
        while (class_parse_nid(tail, &nid, &tail, 1) == 0) {}
        length = tail - *ptr;
        if (length == 0) {
                LCONSOLE_ERROR_MSG(0x159, "Can't parse NID '%s'\n", *ptr);
                return -EINVAL;
        }

        if (lmd->lmd_mgs != NULL)
                oldlen = strlen(lmd->lmd_mgs) + 1;

        OBD_ALLOC(mgsnid, oldlen + length + 1);
        if (mgsnid == NULL)
                return -ENOMEM;

        if (lmd->lmd_mgs != NULL) {
                /* Multiple mgsnid= are taken to mean failover locations */
                memcpy(mgsnid, lmd->lmd_mgs, oldlen);
                mgsnid[oldlen - 1] = ':';
                OBD_FREE(lmd->lmd_mgs, oldlen);
        }
        memcpy(mgsnid + oldlen, *ptr, length);
        mgsnid[oldlen + length] = '\0';
        lmd->lmd_mgs = mgsnid;
        *ptr = tail;

        return 0;
}

/** Parse mount line options
 * e.g. mount -v -t lustre -o abort_recov uml1:uml2:/lustre-client /mnt/lustre
 * dev is passed as device=uml1:/lustre by mount.lustre
 */
static int lmd_parse(char *options, struct lustre_mount_data *lmd)
{
        char *s1, *s2, *devname = NULL;
        struct lustre_mount_data *raw = (struct lustre_mount_data *)options;
        int rc = 0;
        ENTRY;

        LASSERT(lmd);
        if (!options) {
                LCONSOLE_ERROR_MSG(0x162, "Missing mount data: check that "
                                   "/sbin/mount.lustre is installed.\n");
                RETURN(-EINVAL);
        }

        /* Options should be a string - try to detect old lmd data */
        if ((raw->lmd_magic & 0xffffff00) == (LMD_MAGIC & 0xffffff00)) {
                LCONSOLE_ERROR_MSG(0x163, "You're using an old version of "
                                   "/sbin/mount.lustre.  Please install "
                                   "version %s\n", LUSTRE_VERSION_STRING);
                RETURN(-EINVAL);
        }
        lmd->lmd_magic = LMD_MAGIC;

        /* Set default flags here */

        s1 = options;
        while (*s1) {
                int clear = 0;
                int time_min = 2 * (CONNECTION_SWITCH_MAX +
                               2 * INITIAL_CONNECT_TIMEOUT);

                /* Skip whitespace and extra commas */
                while (*s1 == ' ' || *s1 == ',')
                        s1++;
                /* Find next opt */
                s2 = strchr(s1, ',');

                /* Client options are parsed in ll_options: eg. flock,
                   user_xattr, acl */

                /* Parse non-ldiskfs options here. Rather than modifying
                   ldiskfs, we just zero these out here */
                if (strncmp(s1, "abort_recov", 11) == 0) {
                        lmd->lmd_flags |= LMD_FLG_ABORT_RECOV;
                        clear++;
                } else if (strncmp(s1, "recovery_time_soft=", 19) == 0) {
                        lmd->lmd_recovery_time_soft = max_t(int,
                                simple_strtoul(s1 + 19, NULL, 10), time_min);
                        clear++;
                } else if (strncmp(s1, "recovery_time_hard=", 19) == 0) {
                        lmd->lmd_recovery_time_hard = max_t(int,
                                simple_strtoul(s1 + 19, NULL, 10), time_min);
                        clear++;
                } else if (strncmp(s1, "nosvc", 5) == 0) {
                        lmd->lmd_flags |= LMD_FLG_NOSVC;
                        clear++;
                } else if (strncmp(s1, "nomgs", 5) == 0) {
                        lmd->lmd_flags |= LMD_FLG_NOMGS;
                        clear++;
                } else if (strncmp(s1, "writeconf", 9) == 0) {
                        lmd->lmd_flags |= LMD_FLG_WRITECONF;
                        clear++;
                } else if (strncmp(s1, "mgssec=", 7) == 0) {
                        rc = lmd_parse_mgssec(lmd, s1 + 7);
                        if (rc)
                                goto invalid;
                        clear++;
                /* ost exclusion list */
                } else if (strncmp(s1, "exclude=", 8) == 0) {
                        rc = lmd_make_exclusion(lmd, s1 + 7);
                        if (rc)
                                goto invalid;
                        clear++;
                } else if (strncmp(s1, PARAM_MGSNODE,
                                   sizeof(PARAM_MGSNODE) - 1) == 0) {
                        s2 = s1 + sizeof(PARAM_MGSNODE) - 1;
                        /* Assume the next mount opt is the first
                           invalid nid we get to. */
                        rc = lmd_parse_mgs(lmd, &s2);
                        if (rc)
                                goto invalid;
                        clear++;
                }
                /* Linux 2.4 doesn't pass the device, so we stuck it at the
                   end of the options. */
                else if (strncmp(s1, "device=", 7) == 0) {
                        devname = s1 + 7;
                        /* terminate options right before device.  device
                           must be the last one. */
                        *s1 = '\0';
                        break;
                }

                /* s2 should be pointing at the next comma after this opt */
                if (s2 == NULL) {
                        if (clear)
                                *s1 = '\0';
                        break;
                }
                s2++;
                if (clear)
                        memmove(s1, s2, strlen(s2) + 1);
                else
                        s1 = s2;
        }

        if (!devname) {
                LCONSOLE_ERROR_MSG(0x164, "Can't find the device name "
                                   "(need mount option 'device=...')\n");
                goto invalid;
        }

        s1 = devname_is_client(devname);
        if (s1) {
                ++s1;
                lmd->lmd_flags = LMD_FLG_CLIENT;
                /* Remove leading /s from fsname */
                while (*++s1 == '/') ;
                /* Freed in lustre_free_lsi */
                OBD_ALLOC(lmd->lmd_profile, strlen(s1) + 8);
                if (!lmd->lmd_profile)
                        RETURN(-ENOMEM);
                sprintf(lmd->lmd_profile, "%s-client", s1);
        }

        /* Freed in lustre_free_lsi */
        OBD_ALLOC(lmd->lmd_dev, strlen(devname) + 1);
        if (!lmd->lmd_dev)
                RETURN(-ENOMEM);
        strcpy(lmd->lmd_dev, devname);

        /* Save mount options */
        s1 = options + strlen(options) - 1;
        while (s1 >= options && (*s1 == ',' || *s1 == ' '))
                *s1-- = 0;
        if (*options != 0) {
                /* Freed in lustre_free_lsi */
                OBD_ALLOC(lmd->lmd_opts, strlen(options) + 1);
                if (!lmd->lmd_opts)
                        RETURN(-ENOMEM);
                strcpy(lmd->lmd_opts, options);
        }

        lmd_print(lmd);
        lmd->lmd_magic = LMD_MAGIC;

        RETURN(rc);

invalid:
        CERROR("Bad mount options %s\n", options);
        RETURN(-EINVAL);
}

/*************** Common Entrypoints called by OS specific vfs code ***********/

int lustre_server_statfs(struct lustre_sb_info *lsi, cfs_kstatfs_t *buf)
{
        /*
         * XXX Call osd_statfs here to fill statfs buf.
         */
        RETURN(0);
}

/** Called only for 'umount -f'
 */
void lustre_umount_server_force_flag_set(struct lustre_sb_info *lsi)
{
        ENTRY;

        CDEBUG(D_MOUNT, "umount -f\n");
        /* umount = failover
         * umount -f = force
         * no third way to do non-force, non-failover */
        lsi->lsi_flags &= ~LSI_UMOUNT_FAILOVER;
        lsi->lsi_flags |= LSI_UMOUNT_FORCE;

        EXIT;
}

/** Start the shutdown of servers at umount.
 */
void lustre_server_umount(struct lustre_sb_info *lsi)
{
        struct obd_device     *obd;
        char *tmpname, *extraname = NULL;
        int tmpname_sz;
        int lddflags = lsi->lsi_ldd->ldd_flags;
        int lsiflags = lsi->lsi_flags;
        ENTRY;

        LASSERT(lsiflags & LSI_SERVER);

        tmpname_sz = strlen(lsi->lsi_ldd->ldd_svname) + 1;
        OBD_ALLOC(tmpname, tmpname_sz);
        memcpy(tmpname, lsi->lsi_ldd->ldd_svname, tmpname_sz);
        CDEBUG(D_MOUNT, "lustre_server_umount %s\n", tmpname);
        if (IS_MDT(lsi->lsi_ldd) && (lsi->lsi_lmd->lmd_flags & LMD_FLG_NOSVC))
                snprintf(tmpname, tmpname_sz, "MGS");

        /* Stop the target */
        if (!(lsi->lsi_lmd->lmd_flags & LMD_FLG_NOSVC) &&
            (IS_MDT(lsi->lsi_ldd) || IS_OST(lsi->lsi_ldd))) {
                struct lustre_profile *lprof = NULL;

                /* tell the mgc to drop the config log */
                lustre_log_end(lsi, lsi->lsi_ldd->ldd_svname, NULL);

                /* COMPAT_146 - profile may get deleted in mgc_cleanup.
                   If there are any setup/cleanup errors, save the lov
                   name for safety cleanup later. */
                lprof = class_get_profile(lsi->lsi_ldd->ldd_svname);
                if (lprof && lprof->lp_dt) {
                        OBD_ALLOC(extraname, strlen(lprof->lp_dt) + 1);
                        strcpy(extraname, lprof->lp_dt);
                }

                obd = class_name2obd(lsi->lsi_ldd->ldd_svname);
                if (obd) {
                        CDEBUG(D_MOUNT, "stopping %s\n", obd->obd_name);
                        if (lsi->lsi_flags & LSI_UMOUNT_FAILOVER)
                                obd->obd_fail = 1;
                        obd->obd_force = 1;
                        class_manual_cleanup(obd);
                } else {
                        CERROR("no obd %s\n", lsi->lsi_ldd->ldd_svname);
                        server_deregister_mount(lsi->lsi_ldd->ldd_svname);
                }
        }

        /* If they wanted the mgs to stop separately from the mdt, they
           should have put it on a different device. */
        if (IS_MGS(lsi->lsi_ldd)) {
                /* if MDS start with --nomgs, don't stop MGS then */
                if (!(lsi->lsi_lmd->lmd_flags & LMD_FLG_NOMGS)) {
                        server_stop_mgs(lsi);
                        if (!(lddflags &
                              (LDD_F_SV_TYPE_MDT | LDD_F_SV_TYPE_OST))) {
                                /* XXX: OSD isn't part of MSG stack yet
                                 *      shutdown OSD manually */
                                stop_osd(lsi->lsi_dt_dev);
                        }
                }
        }

        /* Wait for the targets to really clean up - can't exit
         * while the mount is still in use */
        server_wait_finished(lsi);

        /* Clean the mgc and lsi */
        lustre_common_umount(lsi);

        /* Stop the servers (MDS, OSS) if no longer needed.  We must wait
           until the target is really gone so that our type refcount check
           is right. */
        server_stop_servers(lddflags, lsiflags);

        /* In case of startup or cleanup err, stop related obds */
        if (extraname) {
                obd = class_name2obd(extraname);
                if (obd) {
                        CWARN("Cleaning orphaned obd %s\n", extraname);
                        obd->obd_force = 1;
                        class_manual_cleanup(obd);
                }
                OBD_FREE(extraname, strlen(extraname) + 1);
        }

        LCONSOLE_WARN("server umount %s complete\n", tmpname);
        OBD_FREE(tmpname, tmpname_sz);
        EXIT;
}

/** This is the entry point for the mount call into Lustre.
 * This is called when a server or client is mounted,
 * and this is where we start setting things up.
 * @param data Mount options (e.g. -o flock,abort_recov)
 */
int lustre_mount(void *osvfsp, void *data, unsigned long mflags)
{
        struct lustre_mount_data *lmd;
        struct lustre_sb_info *lsi;
        int rc;
        ENTRY;

        CDEBUG(D_MOUNT|D_VFSTRACE, "VFS mount Op: data %p\n", data);

        lsi = lustre_init_lsi(osvfsp);
        if (!lsi)
                RETURN(-ENOMEM);
        lmd = lsi->lsi_lmd;

        /*
         * Disable lockdep during mount, because mount locking patterns are
         * `special'.
         */
        cfs_lockdep_off();

        /* Figure out the lmd from the mount options */
        if (lmd_parse((char *)data, lmd)) {
                lustre_put_lsi(lsi);
                GOTO(out, rc = -EINVAL);
        }

        if (lmd_is_client(lmd)) {
#if defined(__sun__)
                LIBCFS_PANIC("lustre_mount: no client mounts on solaris\n");
#else /* __sun__ */
                extern int (*client_fill_super)(struct super_block *sb);

                CDEBUG(D_MOUNT, "Mounting client %s\n", lmd->lmd_profile);
                if (!client_fill_super) {
                        LCONSOLE_ERROR_MSG(0x165, "Nothing registered for "
                                           "client mount! Is the 'lustre' "
                                           "module loaded?\n");
                        lustre_put_lsi(lsi);
                        rc = -ENODEV;
                } else {
                        rc = lustre_start_mgc(lsi);
                        if (rc) {
                                lustre_put_lsi(lsi);
                                GOTO(out, rc);
                        }
                        /* Connect and start */
                        /* (should always be ll_fill_super) */
                        rc = (*client_fill_super)((struct super_block *)osvfsp);
                        /* c_f_s will call lustre_common_umount() on failure */
                }
#endif /* __sun__ */
        } else {
                CDEBUG(D_MOUNT, "Mounting server from %s\n", lmd->lmd_dev);
                lsi->lsi_flags |= LSI_SERVER;
                rc = lustre_server_mount(lsi, mflags);
        }

out:
        if (rc) {
                CERROR("Unable to mount (%d)\n", rc);
        } else {
                CDEBUG(D_SUPER, "Mount %s complete\n",
                       lmd->lmd_dev);
        }
        cfs_lockdep_on();
        return rc;
}

EXPORT_SYMBOL(lustre_common_umount);
EXPORT_SYMBOL(lustre_log_process);
EXPORT_SYMBOL(lustre_log_end);
EXPORT_SYMBOL(server_get_mount);
EXPORT_SYMBOL(server_get_mount_2);
EXPORT_SYMBOL(server_put_mount);
EXPORT_SYMBOL(server_put_mount_2);
EXPORT_SYMBOL(server_name2index);
EXPORT_SYMBOL(server_mti_print);
