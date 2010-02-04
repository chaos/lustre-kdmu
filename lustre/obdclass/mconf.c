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
 * Copyright  2010 Sun Microsystems, Inc. All rights reserved
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/obdclass/mconf.c
 *
 * trivial dt device to allow mountconf to access disk storage
 *
 * Author: Alex Zhuravlev <bzzz@sun.com> 
 */


#define DEBUG_SUBSYSTEM S_CLASS
#define D_MOUNT D_SUPER|D_CONFIG /*|D_WARNING */
#define PRINT_CMD CDEBUG
#define PRINT_MASK D_SUPER|D_CONFIG

#include <obd.h>
#include <lvfs.h>
#include <obd_class.h>
#include <lustre/lustre_user.h>
#include <linux/version.h>
#include <lustre_log.h>
#include <lustre_disk.h>
#include <lustre_param.h>

static const struct lu_device_operations mconf_lu_ops;

struct mconf_device {
        struct lu_device   mcf_lu_dev;
        struct lu_device  *mcf_bottom;
        struct obd_export *mcf_bottom_exp;
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

static int mconf_connect(const struct lu_env *env, struct mconf_device *m,
                         const char *nextdev)
{
        struct obd_connect_data *data = NULL;
        struct obd_device       *obd;
        int                      rc;
        ENTRY;

        LASSERT(m->mcf_bottom_exp == NULL);

        OBD_ALLOC(data, sizeof(*data));
        if (data == NULL)
                GOTO(out, rc = -ENOMEM);

        obd = class_name2obd(nextdev);
        if (obd == NULL) {
                CERROR("can't locate next device: %s\n", nextdev);
                GOTO(out, rc = -ENOTCONN);
        }

        data->ocd_version = LUSTRE_VERSION_CODE;
        
        rc = obd_connect(NULL, &m->mcf_bottom_exp, obd, &obd->obd_uuid, data, NULL);
        if (rc) {
                CERROR("cannot connect to next dev %s (%d)\n", nextdev, rc);
                GOTO(out, rc);
        }

        m->mcf_bottom = m->mcf_bottom_exp->exp_obd->obd_lu_dev;
        m->mcf_lu_dev.ld_site = m->mcf_bottom->ld_site;
        LASSERT(m->mcf_lu_dev.ld_site);
        m->mcf_lu_dev.ld_site->ls_top_dev = &m->mcf_lu_dev;

out:
        if (data)
                OBD_FREE(data, sizeof(*data));
        RETURN(rc);
}

static struct lu_device *mconf_device_alloc(const struct lu_env *env,
                                            struct lu_device_type *t,
                                            struct lustre_cfg *cfg)
{
        struct mconf_device *o;
        struct lu_device    *l;
        int                  rc;

        OBD_ALLOC_PTR(o);
        if (o != NULL) {
                int result;

                result = lu_device_init(&o->mcf_lu_dev, t);
                if (result == 0) {
                        l = &o->mcf_lu_dev;
                        l->ld_ops = &mconf_lu_ops;
                        //o->od_dt_dev.dd_ops = &osd_dt_ops;
                        
                        rc = mconf_connect(env, o, lustre_cfg_string(cfg, 1));
                        LASSERT(rc == 0);

                } else
                        l = ERR_PTR(result);
        } else
                l = ERR_PTR(-ENOMEM);
        return l;
}

static struct lu_device *mconf_device_free(const struct lu_env *env,
                                           struct lu_device *d)
{
        struct mconf_device *o = mconf_dev(d);

        lu_device_fini(&o->mcf_lu_dev);
        OBD_FREE_PTR(o);
        RETURN (NULL);
}

static int mconf_device_init(const struct lu_env *env, struct lu_device *d,
                             const char *name, struct lu_device *next)
{
        return 0;
}

static struct lu_device *mconf_device_fini(const struct lu_env *env,
                                           struct lu_device *d)
{
        struct mconf_device *o = mconf_dev(d);
        ENTRY;

        LASSERT(o->mcf_bottom_exp);
        obd_disconnect(o->mcf_bottom_exp);

        RETURN(NULL);
}

static const struct lu_device_operations mconf_lu_ops = {
        .ldo_object_alloc    = mconf_object_alloc,
};

static int mconf_type_init(struct lu_device_type *t)
{
        return 0;
}

static void mconf_type_fini(struct lu_device_type *t)
{
}

static void mconf_type_start(struct lu_device_type *t)
{
}

static void mconf_type_stop(struct lu_device_type *t)
{
}

static struct lu_device_type_operations mconf_device_type_ops = {
        .ldto_init           = mconf_type_init,
        .ldto_fini           = mconf_type_fini,

        .ldto_start          = mconf_type_start,
        .ldto_stop           = mconf_type_stop,

        .ldto_device_alloc   = mconf_device_alloc,
        .ldto_device_free    = mconf_device_free,

        .ldto_device_init    = mconf_device_init,
        .ldto_device_fini    = mconf_device_fini
};

struct obd_ops mconf_obd_device_ops = {
        .o_owner = THIS_MODULE
};

struct lu_device_type mconf_device_type = {
        .ldt_tags     = LU_DEVICE_MC,
        .ldt_name     = LUSTRE_MCF_NAME,
        .ldt_ops      = &mconf_device_type_ops,
        .ldt_ctx_tags = LCT_DT_THREAD
};

int mconf_set_label(struct lustre_sb_info *lsi, char *label)
{
        struct dt_device *dt = lsi->lsi_dt_dev;
        struct lu_env     env;
        int               rc;

        lu_env_init(&env, dt->dd_lu_dev.ld_type->ldt_ctx_tags);
        rc = dt->dd_ops->dt_label_set(&env, dt, label);
        lu_env_fini(&env);
        return rc;
}

char *mconf_get_label(struct lustre_sb_info *lsi)
{
        struct dt_device *dt = lsi->lsi_dt_dev;
        struct lu_env     env;
        char             *label;

        lu_env_init(&env, dt->dd_lu_dev.ld_type->ldt_ctx_tags);
        label = dt->dd_ops->dt_label_get(&env, dt);
        lu_env_fini(&env);

        return label;
}

int mconf_sync_dev(struct lustre_sb_info *lsi)
{
        return 0;
}

int mconf_read_file(struct lustre_sb_info *lsi, char *dir, char *filename,
                    struct lu_buf *lb, loff_t *pos)
{
        struct dt_device *dt = lsi->lsi_dt_dev;
        struct dt_object *o;
        struct lu_env     env;
        struct lu_fid     fid;
        int               rc;
        
        rc = lu_env_init(&env, dt->dd_lu_dev.ld_type->ldt_ctx_tags);
        LASSERT(rc == 0);

        o = dt_store_open(&env, dt, MOUNT_CONFIGS_DIR, CONFIGS_FILE, &fid);
        if (IS_ERR(o))
                GOTO(out, rc = PTR_ERR(o));

        rc = o->do_body_ops->dbo_read(&env, o, lb, pos, BYPASS_CAPA);

        lu_object_put(&env, &o->do_lu);

out:
        lu_site_purge(&env, dt->dd_lu_dev.ld_site, ~0);
        lu_env_fini(&env);
        RETURN(rc);
}

int mconf_write_file(struct lustre_sb_info *lsi, char *dir, char *filename,
                     struct lu_buf *lb, loff_t pos)
{
        struct dt_device *dt = lsi->lsi_dt_dev;
        struct dt_object *o;
        struct thandle   *th;
        struct lu_env     env;
        struct lu_fid     fid;
        int               rc;
       
        rc = lu_env_init(&env, dt->dd_lu_dev.ld_type->ldt_ctx_tags);
        LASSERT(rc == 0);

        o = dt_store_open(&env, dt, MOUNT_CONFIGS_DIR, CONFIGS_FILE, &fid);
        if (IS_ERR(o))
                GOTO(out, rc = PTR_ERR(o));

        th = dt->dd_ops->dt_trans_create(&env, dt);
        LASSERT(!IS_ERR(th));
        rc = dt_declare_record_write(&env, o, lb->lb_len, pos, th);
        LASSERT(rc == 0);
        rc = dt->dd_ops->dt_trans_start(&env, dt, th);
        LASSERT(rc == 0);

        rc = dt_record_write(&env, o, lb, &pos, th);

        dt->dd_ops->dt_trans_stop(&env, th);

        lu_object_put(&env, &o->do_lu);

out:
        lu_site_purge(&env, dt->dd_lu_dev.ld_site, ~0);
        lu_env_fini(&env);
        
        RETURN(rc);
}

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

int mconf_start(struct lustre_sb_info *lsi)
{
        struct obd_device   *obd;
        struct mconf_device *m;
        int                  rc;
        ENTRY;

        strcpy(lsi->lsi_mconf_obdname, lsi->lsi_ldd->ldd_svname);
        strcat(lsi->lsi_mconf_obdname, "-mconf");
        strcpy(lsi->lsi_mconf_uuid, lsi->lsi_mconf_obdname);
        strcat(lsi->lsi_mconf_uuid, "_UUID");
        
        rc = lustre_start_simple(lsi->lsi_mconf_obdname, LUSTRE_MCF_NAME,
                                 lsi->lsi_mconf_uuid, lsi->lsi_osd_obdname, NULL);
        if (rc)
                RETURN(rc);

        obd = class_name2obd(lsi->lsi_mconf_obdname);
        LASSERT(obd);

        m = mconf_dev(obd->obd_lu_dev);
        LASSERT(m);
        LASSERT(m->mcf_lu_dev.ld_ops = &mconf_lu_ops);

        LASSERT(m->mcf_bottom);        
        lsi->lsi_dt_dev = lu2dt_dev(m->mcf_bottom);

        RETURN(rc);
}

void mconf_stop(struct lustre_sb_info *lsi)
{
        struct obd_device *obd;

        obd = class_name2obd(lsi->lsi_mconf_obdname);
        LASSERT(obd);

        class_manual_cleanup(obd);
}

