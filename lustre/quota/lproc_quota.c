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
 * Copyright  2008 Sun Microsystems, Inc. All rights reserved
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#define DEBUG_SUBSYSTEM S_LQUOTA

#include <linux/version.h>
#include <lprocfs_status.h>
#include <obd.h>
#include <linux/seq_file.h>
#include <lustre_fsfilt.h>

#include "quota_internal.h"

#ifdef HAVE_QUOTA_SUPPORT

#ifdef LPROCFS
int lprocfs_quota_rd_bunit(char *page, char **start, off_t off, int count,
                           int *eof, void *data)
{
        struct obd_device *obd = (struct obd_device *)data;
        LASSERT(obd != NULL);

        return snprintf(page, count, "%lu\n",
                        obd->u.obt.obt_qctxt.lqc_bunit_sz);
}
EXPORT_SYMBOL(lprocfs_quota_rd_bunit);

int lprocfs_quota_wr_bunit(struct file *file, const char *buffer,
                           unsigned long count, void *data)
{
        struct obd_device *obd = (struct obd_device *)data;
        int val, rc;
        LASSERT(obd != NULL);

        rc = lprocfs_write_helper(buffer, count, &val);
        if (rc)
                return rc;

        if (val % QUOTABLOCK_SIZE ||
            val <= obd->u.obt.obt_qctxt.lqc_btune_sz)
                return -EINVAL;

        obd->u.obt.obt_qctxt.lqc_bunit_sz = val;
        return count;
}
EXPORT_SYMBOL(lprocfs_quota_wr_bunit);

int lprocfs_quota_rd_btune(char *page, char **start, off_t off, int count,
                           int *eof, void *data)
{
        struct obd_device *obd = (struct obd_device *)data;
        LASSERT(obd != NULL);

        return snprintf(page, count, "%lu\n",
                        obd->u.obt.obt_qctxt.lqc_btune_sz);
}
EXPORT_SYMBOL(lprocfs_quota_rd_btune);

int lprocfs_quota_wr_btune(struct file *file, const char *buffer,
                           unsigned long count, void *data)
{
        struct obd_device *obd = (struct obd_device *)data;
        int val, rc;
        LASSERT(obd != NULL);

        rc = lprocfs_write_helper(buffer, count, &val);
        if (rc)
                return rc;

        if (val <= QUOTABLOCK_SIZE * MIN_QLIMIT || val % QUOTABLOCK_SIZE ||
            val >= obd->u.obt.obt_qctxt.lqc_bunit_sz)
                return -EINVAL;

        obd->u.obt.obt_qctxt.lqc_btune_sz = val;
        return count;
}
EXPORT_SYMBOL(lprocfs_quota_wr_btune);

int lprocfs_quota_rd_iunit(char *page, char **start, off_t off, int count,
                           int *eof, void *data)
{
        struct obd_device *obd = (struct obd_device *)data;
        LASSERT(obd != NULL);

        return snprintf(page, count, "%lu\n",
                        obd->u.obt.obt_qctxt.lqc_iunit_sz);
}
EXPORT_SYMBOL(lprocfs_quota_rd_iunit);

int lprocfs_quota_wr_iunit(struct file *file, const char *buffer,
                           unsigned long count, void *data)
{
        struct obd_device *obd = (struct obd_device *)data;
        int val, rc;
        LASSERT(obd != NULL);

        rc = lprocfs_write_helper(buffer, count, &val);
        if (rc)
                return rc;

        if (val <= obd->u.obt.obt_qctxt.lqc_itune_sz)
                return -EINVAL;

        obd->u.obt.obt_qctxt.lqc_iunit_sz = val;
        return count;
}
EXPORT_SYMBOL(lprocfs_quota_wr_iunit);

int lprocfs_quota_rd_itune(char *page, char **start, off_t off, int count,
                           int *eof, void *data)
{
        struct obd_device *obd = (struct obd_device *)data;
        LASSERT(obd != NULL);

        return snprintf(page, count, "%lu\n",
                        obd->u.obt.obt_qctxt.lqc_itune_sz);
}
EXPORT_SYMBOL(lprocfs_quota_rd_itune);

int lprocfs_quota_wr_itune(struct file *file, const char *buffer,
                           unsigned long count, void *data)
{
        struct obd_device *obd = (struct obd_device *)data;
        int val, rc;
        LASSERT(obd != NULL);

        rc = lprocfs_write_helper(buffer, count, &val);
        if (rc)
                return rc;

        if (val <= MIN_QLIMIT ||
            val >= obd->u.obt.obt_qctxt.lqc_iunit_sz)
                return -EINVAL;

        obd->u.obt.obt_qctxt.lqc_itune_sz = val;
        return count;
}
EXPORT_SYMBOL(lprocfs_quota_wr_itune);

#define USER_QUOTA      1
#define GROUP_QUOTA     2

#define MAX_STYPE_SIZE  5

int lprocfs_quota_rd_type(char *page, char **start, off_t off, int count,
                          int *eof, void *data)
{
        struct obd_device *obd = (struct obd_device *)data;
        char stype[MAX_STYPE_SIZE + 1] = "";
        int oq_type;
        struct obd_device_target *obt;

        LASSERT(obd != NULL);

        obt = &obd->u.obt;

        /* Collect the needed information */
        oq_type = obd->u.obt.obt_qctxt.lqc_flags;

        /* Transform the collected data into a user-readable string */
        if (oq_type & LQC_USRQUOTA_FLAG)
                strcat(stype, "u");
        if (oq_type & LQC_GRPQUOTA_FLAG)
                strcat(stype, "g");

        strcat(stype, "3");

        return snprintf(page, count, "%s\n", stype);
}
EXPORT_SYMBOL(lprocfs_quota_rd_type);

static int auto_quota_on(struct obd_device *obd, int type,
                         struct super_block *sb, int is_master)
{
        struct obd_quotactl *oqctl;
        struct lvfs_run_ctxt saved;
        int rc = 0, rc1 = 0, id;
        struct obd_device_target *obt = &obd->u.obt;
        struct lustre_quota_ctxt *qctxt = &obt->obt_qctxt;
        struct mds_obd *mds = NULL;
        ENTRY;

        LASSERT(type == USRQUOTA || type == GRPQUOTA || type == UGQUOTA);

        OBD_ALLOC_PTR(oqctl);
        if (!oqctl)
                RETURN(-ENOMEM);

        cfs_down(&obt->obt_quotachecking);
        id = UGQUOTA2LQC(type);
        /* quota already turned on */
        if ((obt->obt_qctxt.lqc_flags & id) == id)
                GOTO(out, rc);

        oqctl->qc_type = type;
        oqctl->qc_cmd = Q_QUOTAON;
        oqctl->qc_id = obt->obt_qfmt;

        push_ctxt(&saved, &obd->obd_lvfs_ctxt, NULL);

        if (is_master) {
                mds = &obd->u.mds;
                cfs_down(&mds->mds_qonoff_sem);
                /* turn on cluster wide quota */
                rc1 = mds_admin_quota_on(obd, oqctl);
                if (rc1 && rc1 != -EALREADY) {
                        CDEBUG_LIMIT(rc1 == -ENOENT ? D_QUOTA : D_ERROR,
                                     "%s: auto-enable admin quota failed with "
                                     "rc=%d\n", obd->obd_name, rc1);
                        GOTO(out_ctxt, rc1);
                }
        }

        /* turn on local quota */
        rc = fsfilt_quotactl(obd, sb, oqctl);
        if (!rc) {
                obt->obt_qctxt.lqc_flags |= UGQUOTA2LQC(type);
                build_lqs(obd);
        } else if (rc == -EBUSY && quota_is_on(qctxt, oqctl)) {
                CWARN("%s: mds local quota[%d] is on already\n",
                      obd->obd_name, oqctl->qc_type);
                rc = -EALREADY;
        } else {
                CDEBUG_LIMIT(rc == -ENOENT ? D_QUOTA : D_ERROR,
                             "%s: auto-enable local quota failed with rc=%d\n",
                             obd->obd_name, rc);
                if (rc1 == -EALREADY) {
                        oqctl->qc_cmd = Q_QUOTAOFF;
                        mds_admin_quota_off(obd, oqctl);
                }
                if (rc == -ENOENT)
                        CWARN("%s: quotaon failed because quota files don't "
                              "exist, please run quotacheck firstly\n",
                              obd->obd_name);

                GOTO(out_ctxt, rc);
        }

        EXIT;

out_ctxt:
        if (mds != NULL)
                cfs_up(&mds->mds_qonoff_sem);
        pop_ctxt(&saved, &obd->obd_lvfs_ctxt, NULL);

out:
        cfs_up(&obt->obt_quotachecking);
        OBD_FREE_PTR(oqctl);
        return rc;
}

int lprocfs_quota_wr_type(struct file *file, const char *buffer,
                          unsigned long count, void *data)
{
        struct obd_device *obd = (struct obd_device *)data;
        struct obd_device_target *obt;
        int type = 0, is_mds;
        unsigned long i;
        char stype[MAX_STYPE_SIZE + 1] = "";

        LASSERT(obd != NULL);

        obt = &obd->u.obt;

        is_mds = !strcmp(obd->obd_type->typ_name, LUSTRE_MDS_NAME);

        if (count > MAX_STYPE_SIZE)
                return -EINVAL;

        if (cfs_copy_from_user(stype, buffer, count))
                return -EFAULT;

        for (i = 0 ; i < count ; i++) {
                switch (stype[i]) {
                case 'u' :
                        type |= USER_QUOTA;
                        break;
                case 'g' :
                        type |= GROUP_QUOTA;
                        break;
                case '1' :
                case '2' :
                        CWARN("quota_type options 1 and 2 are obsolete, "
                              "they will be ignored\n");
                        break;
                case '3' : /* the only valid version spec, do nothing */
                default  : /* just skip stray symbols like \n */
                        break;
                }
        }

        if (type != 0) {
                int rc = auto_quota_on(obd, type - 1, obt->obt_sb, is_mds);

                if (rc && rc != -EALREADY && rc != -ENOENT)
                        return rc;
        }

        return count;
}
EXPORT_SYMBOL(lprocfs_quota_wr_type);

int lprocfs_quota_rd_switch_seconds(char *page, char **start, off_t off,
                                    int count, int *eof, void *data)
{
        struct obd_device *obd = (struct obd_device *)data;
        LASSERT(obd != NULL);

        return snprintf(page, count, "%d\n",
                        obd->u.obt.obt_qctxt.lqc_switch_seconds);
}
EXPORT_SYMBOL(lprocfs_quota_rd_switch_seconds);

int lprocfs_quota_wr_switch_seconds(struct file *file, const char *buffer,
                                    unsigned long count, void *data)
{
        struct obd_device *obd = (struct obd_device *)data;
        int val, rc;
        LASSERT(obd != NULL);

        rc = lprocfs_write_helper(buffer, count, &val);
        if (rc)
                return rc;

        if (val <= 10)
                return -EINVAL;

        obd->u.obt.obt_qctxt.lqc_switch_seconds = val;
        return count;
}
EXPORT_SYMBOL(lprocfs_quota_wr_switch_seconds);

int lprocfs_quota_rd_sync_blk(char *page, char **start, off_t off,
                              int count, int *eof, void *data)
{
        struct obd_device *obd = (struct obd_device *)data;
        LASSERT(obd != NULL);

        return snprintf(page, count, "%d\n",
                        obd->u.obt.obt_qctxt.lqc_sync_blk);
}
EXPORT_SYMBOL(lprocfs_quota_rd_sync_blk);

int lprocfs_quota_wr_sync_blk(struct file *file, const char *buffer,
                              unsigned long count, void *data)
{
        struct obd_device *obd = (struct obd_device *)data;
        int val, rc;
        LASSERT(obd != NULL);

        rc = lprocfs_write_helper(buffer, count, &val);
        if (rc)
                return rc;

        if (val < 0)
                return -EINVAL;

        obd->u.obt.obt_qctxt.lqc_sync_blk = val;
        return count;
}
EXPORT_SYMBOL(lprocfs_quota_wr_sync_blk);

int lprocfs_quota_rd_switch_qs(char *page, char **start, off_t off,
                               int count, int *eof, void *data)
{
        struct obd_device *obd = (struct obd_device *)data;
        LASSERT(obd != NULL);

        return snprintf(page, count, "changing qunit size is %s\n",
                        obd->u.obt.obt_qctxt.lqc_switch_qs ?
                        "enabled" : "disabled");
}
EXPORT_SYMBOL(lprocfs_quota_rd_switch_qs);

int lprocfs_quota_wr_switch_qs(struct file *file, const char *buffer,
                               unsigned long count, void *data)
{
        struct obd_device *obd = (struct obd_device *)data;
        int val, rc;
        LASSERT(obd != NULL);

        rc = lprocfs_write_helper(buffer, count, &val);
        if (rc)
                return rc;

        if (val)
            obd->u.obt.obt_qctxt.lqc_switch_qs = 1;
        else
            obd->u.obt.obt_qctxt.lqc_switch_qs = 0;

        return count;
}
EXPORT_SYMBOL(lprocfs_quota_wr_switch_qs);

int lprocfs_quota_rd_boundary_factor(char *page, char **start, off_t off,
                                     int count, int *eof, void *data)
{
        struct obd_device *obd = (struct obd_device *)data;
        LASSERT(obd != NULL);


        return snprintf(page, count, "%lu\n",
                        obd->u.obt.obt_qctxt.lqc_cqs_boundary_factor);
}
EXPORT_SYMBOL(lprocfs_quota_rd_boundary_factor);

int lprocfs_quota_wr_boundary_factor(struct file *file, const char *buffer,
                                     unsigned long count, void *data)
{
        struct obd_device *obd = (struct obd_device *)data;
        int val, rc;
        LASSERT(obd != NULL);

        rc = lprocfs_write_helper(buffer, count, &val);
        if (rc)
                return rc;

        if (val < 2)
                return -EINVAL;

        obd->u.obt.obt_qctxt.lqc_cqs_boundary_factor = val;
        return count;
}
EXPORT_SYMBOL(lprocfs_quota_wr_boundary_factor);

int lprocfs_quota_rd_least_bunit(char *page, char **start, off_t off,
                                 int count, int *eof, void *data)
{
        struct obd_device *obd = (struct obd_device *)data;
        LASSERT(obd != NULL);


        return snprintf(page, count, "%lu\n",
                        obd->u.obt.obt_qctxt.lqc_cqs_least_bunit);
}
EXPORT_SYMBOL(lprocfs_quota_rd_least_bunit);

int lprocfs_quota_wr_least_bunit(struct file *file, const char *buffer,
                                 unsigned long count, void *data)
{
        struct obd_device *obd = (struct obd_device *)data;
        int val, rc;
        LASSERT(obd != NULL);

        rc = lprocfs_write_helper(buffer, count, &val);
        if (rc)
                return rc;

        if (val < PTLRPC_MAX_BRW_SIZE ||
            val >= obd->u.obt.obt_qctxt.lqc_bunit_sz)
                return -EINVAL;

        obd->u.obt.obt_qctxt.lqc_cqs_least_bunit = val;
        return count;
}
EXPORT_SYMBOL(lprocfs_quota_wr_least_bunit);

int lprocfs_quota_rd_least_iunit(char *page, char **start, off_t off,
                                 int count, int *eof, void *data)
{
        struct obd_device *obd = (struct obd_device *)data;
        LASSERT(obd != NULL);


        return snprintf(page, count, "%lu\n",
                        obd->u.obt.obt_qctxt.lqc_cqs_least_iunit);
}
EXPORT_SYMBOL(lprocfs_quota_rd_least_iunit);

int lprocfs_quota_wr_least_iunit(struct file *file, const char *buffer,
                                 unsigned long count, void *data)
{
        struct obd_device *obd = (struct obd_device *)data;
        int val, rc;
        LASSERT(obd != NULL);

        rc = lprocfs_write_helper(buffer, count, &val);
        if (rc)
                return rc;

        if (val < 1 || val >= obd->u.obt.obt_qctxt.lqc_iunit_sz)
                return -EINVAL;

        obd->u.obt.obt_qctxt.lqc_cqs_least_iunit = val;
        return count;
}
EXPORT_SYMBOL(lprocfs_quota_wr_least_iunit);

int lprocfs_quota_rd_qs_factor(char *page, char **start, off_t off,
                               int count, int *eof, void *data)
{
        struct obd_device *obd = (struct obd_device *)data;
        LASSERT(obd != NULL);


        return snprintf(page, count, "%lu\n",
                        obd->u.obt.obt_qctxt.lqc_cqs_qs_factor);
}
EXPORT_SYMBOL(lprocfs_quota_rd_qs_factor);

int lprocfs_quota_wr_qs_factor(struct file *file, const char *buffer,
                               unsigned long count, void *data)
{
        struct obd_device *obd = (struct obd_device *)data;
        int val, rc;
        LASSERT(obd != NULL);

        rc = lprocfs_write_helper(buffer, count, &val);
        if (rc)
                return rc;

        if (val < 2)
                return -EINVAL;

        obd->u.obt.obt_qctxt.lqc_cqs_qs_factor = val;
        return count;
}
EXPORT_SYMBOL(lprocfs_quota_wr_qs_factor);

struct lprocfs_vars lprocfs_quota_common_vars[] = {
        { "quota_bunit_sz", lprocfs_quota_rd_bunit,
                            lprocfs_quota_wr_bunit, 0},
        { "quota_btune_sz", lprocfs_quota_rd_btune,
                            lprocfs_quota_wr_btune, 0},
        { "quota_iunit_sz", lprocfs_quota_rd_iunit,
                            lprocfs_quota_wr_iunit, 0},
        { "quota_itune_sz", lprocfs_quota_rd_itune,
                            lprocfs_quota_wr_itune, 0},
        { "quota_type",     lprocfs_quota_rd_type,
                            lprocfs_quota_wr_type, 0},
        { "quota_switch_seconds",  lprocfs_quota_rd_switch_seconds,
                                   lprocfs_quota_wr_switch_seconds, 0 },
        { "quota_sync_blk", lprocfs_quota_rd_sync_blk,
                            lprocfs_quota_wr_sync_blk, 0},
        { NULL }
};

struct lprocfs_vars lprocfs_quota_master_vars[] = {
        { "quota_switch_qs", lprocfs_quota_rd_switch_qs,
                             lprocfs_quota_wr_switch_qs, 0 },
        { "quota_boundary_factor", lprocfs_quota_rd_boundary_factor,
                                   lprocfs_quota_wr_boundary_factor, 0 },
        { "quota_least_bunit", lprocfs_quota_rd_least_bunit,
                               lprocfs_quota_wr_least_bunit, 0 },
        { "quota_least_iunit", lprocfs_quota_rd_least_iunit,
                               lprocfs_quota_wr_least_iunit, 0 },
        { "quota_qs_factor",   lprocfs_quota_rd_qs_factor,
                               lprocfs_quota_wr_qs_factor, 0 },
        { NULL }
};

int lquota_proc_setup(struct obd_device *obd, int is_master)
{
        struct lustre_quota_ctxt *qctxt = &obd->u.obt.obt_qctxt;
        int rc = 0;
        ENTRY;

        LASSERT(lquota_type_proc_dir && obd);
        qctxt->lqc_proc_dir = lprocfs_register(obd->obd_name,
                                               lquota_type_proc_dir,
                                               lprocfs_quota_common_vars, obd);
        if (IS_ERR(qctxt->lqc_proc_dir)) {
                rc = PTR_ERR(qctxt->lqc_proc_dir);
                CERROR("%s: error %d setting up lprocfs\n",
                       obd->obd_name, rc);
                qctxt->lqc_proc_dir = NULL;
                GOTO(out, rc);
        }

        if (is_master) {
                rc = lprocfs_add_vars(qctxt->lqc_proc_dir,
                                      lprocfs_quota_master_vars, obd);
                if (rc) {
                        CERROR("%s: error %d setting up lprocfs for "
                               "quota master\n", obd->obd_name, rc);
                        GOTO(out_free_proc, rc);
                }
        }

        qctxt->lqc_stats = lprocfs_alloc_stats(LQUOTA_LAST_STAT -
                                               LQUOTA_FIRST_STAT, 0);
        if (!qctxt->lqc_stats)
                GOTO(out_free_proc, rc = -ENOMEM);

        lprocfs_counter_init(qctxt->lqc_stats, LQUOTA_SYNC_ACQ,
                             LPROCFS_CNTR_AVGMINMAX, "sync_acq_req", "us");
        lprocfs_counter_init(qctxt->lqc_stats, LQUOTA_SYNC_REL,
                             LPROCFS_CNTR_AVGMINMAX, "sync_rel_req", "us");
        lprocfs_counter_init(qctxt->lqc_stats, LQUOTA_ASYNC_ACQ,
                             LPROCFS_CNTR_AVGMINMAX, "async_acq_req", "us");
        lprocfs_counter_init(qctxt->lqc_stats, LQUOTA_ASYNC_REL,
                             LPROCFS_CNTR_AVGMINMAX, "async_rel_req", "us");

        lprocfs_counter_init(qctxt->lqc_stats, LQUOTA_WAIT_FOR_CHK_BLK,
                             LPROCFS_CNTR_AVGMINMAX,
                             "wait_for_blk_quota(lquota_chkquota)", "us");
        lprocfs_counter_init(qctxt->lqc_stats, LQUOTA_WAIT_FOR_CHK_INO,
                             LPROCFS_CNTR_AVGMINMAX,
                             "wait_for_ino_quota(lquota_chkquota)", "us");
        lprocfs_counter_init(qctxt->lqc_stats, LQUOTA_WAIT_FOR_COMMIT_BLK,
                             LPROCFS_CNTR_AVGMINMAX,
                             "wait_for_blk_quota(lquota_pending_commit)",
                             "us");
        lprocfs_counter_init(qctxt->lqc_stats, LQUOTA_WAIT_FOR_COMMIT_INO,
                             LPROCFS_CNTR_AVGMINMAX,
                             "wait_for_ino_quota(lquota_pending_commit)",
                             "us");

        lprocfs_counter_init(qctxt->lqc_stats, LQUOTA_WAIT_PENDING_BLK_QUOTA,
                             LPROCFS_CNTR_AVGMINMAX,
                             "wait_for_pending_blk_quota_req"
                             "(qctxt_wait_pending_dqacq)", "us");
        lprocfs_counter_init(qctxt->lqc_stats, LQUOTA_WAIT_PENDING_INO_QUOTA,
                             LPROCFS_CNTR_AVGMINMAX,
                             "wait_for_pending_ino_quota_req"
                             "(qctxt_wait_pending_dqacq)", "us");
        lprocfs_counter_init(qctxt->lqc_stats, LQUOTA_NOWAIT_PENDING_BLK_QUOTA,
                             LPROCFS_CNTR_AVGMINMAX,
                             "nowait_for_pending_blk_quota_req"
                             "(qctxt_wait_pending_dqacq)", "us");
        lprocfs_counter_init(qctxt->lqc_stats, LQUOTA_NOWAIT_PENDING_INO_QUOTA,
                             LPROCFS_CNTR_AVGMINMAX,
                             "nowait_for_pending_ino_quota_req"
                             "(qctxt_wait_pending_dqacq)", "us");

        lprocfs_counter_init(qctxt->lqc_stats, LQUOTA_QUOTA_CTL,
                             LPROCFS_CNTR_AVGMINMAX, "quota_ctl", "us");
        lprocfs_counter_init(qctxt->lqc_stats, LQUOTA_ADJUST_QUNIT,
                             LPROCFS_CNTR_AVGMINMAX, "adjust_qunit", "us");

        lprocfs_register_stats(qctxt->lqc_proc_dir, "stats", qctxt->lqc_stats);

        RETURN(rc);

out_free_proc:
        lprocfs_remove(&qctxt->lqc_proc_dir);
out:
        RETURN(rc);
}

int lquota_proc_cleanup(struct lustre_quota_ctxt *qctxt)
{
        if (!qctxt || !qctxt->lqc_proc_dir)
                return -EINVAL;

        if (qctxt->lqc_stats != NULL)
                 lprocfs_free_stats(&qctxt->lqc_stats);

        lprocfs_remove(&qctxt->lqc_proc_dir);
        return 0;
}

#endif  /* LPROCFS */
#endif
