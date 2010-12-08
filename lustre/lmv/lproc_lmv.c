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

#define DEBUG_SUBSYSTEM S_CLASS

#include <linux/version.h>
#include <linux/seq_file.h>
#include <asm/statfs.h>
#include <lprocfs_status.h>
#include <obd_class.h>

#ifndef __KERNEL__
static struct lprocfs_vars lprocfs_module_vars[] = { {0} };
static struct lprocfs_vars lprocfs_obd_vars[] = { {0} };
#else
static int lmv_rd_numobd(char *page, char **start, off_t off, int count,
                         int *eof, void *data)
{
        struct obd_device    *dev;
        struct lmv_desc      *desc;

        cfs_param_get_data(dev, data, NULL);
        LASSERT(dev != NULL);
        desc = &dev->u.lmv.desc;

        return cfs_param_snprintf(page, count, data, CFS_PARAM_U32,
                                  "%u\n", desc->ld_tgt_count);
}

static const char *placement_name[] = {
        [PLACEMENT_CHAR_POLICY] = "CHAR",
        [PLACEMENT_NID_POLICY]  = "NID"
};

static placement_policy_t placement_name2policy(char *name, int len)
{
        int                     i;

        for (i = 0; i < PLACEMENT_MAX_POLICY; i++) {
                if (!strncmp(placement_name[i], name, len))
                        return i;
        }
        return PLACEMENT_INVAL_POLICY;
}

static const char *placement_policy2name(placement_policy_t placement)
{
        LASSERT(placement < PLACEMENT_MAX_POLICY);
        return placement_name[placement];
}

static int lmv_rd_placement(char *page, char **start, off_t off, int count,
                            int *eof, void *data)
{
        struct obd_device           *dev;
        struct lmv_obd              *lmv;

        cfs_param_get_data(dev, data, NULL);
        LASSERT(dev != NULL);
        lmv = &dev->u.lmv;

        return cfs_param_snprintf(page, count, data, CFS_PARAM_STR, "%s\n",
                                  placement_policy2name(lmv->lmv_placement));
}

#define MAX_POLICY_STRING_SIZE 64

static int lmv_wr_placement(cfs_param_file_t *file, const char *buffer,
                            unsigned long count, void *data)
{
        struct obd_device       *dev;
        char                     dummy[MAX_POLICY_STRING_SIZE + 1];
        int                      len = count;
        placement_policy_t       policy;
        struct lmv_obd          *lmv;
        int                      rc;
        int                      flag = 0;

        cfs_param_get_data(dev, data, &flag);
        rc = cfs_param_copy(flag, dummy, buffer, MAX_POLICY_STRING_SIZE);
        if (rc < 0)
                return rc;

        LASSERT(dev != NULL);
        lmv = &dev->u.lmv;

        if (len > MAX_POLICY_STRING_SIZE)
                len = MAX_POLICY_STRING_SIZE;

        if (dummy[len - 1] == '\n')
                len--;
        dummy[len] = '\0';

        policy = placement_name2policy(dummy, len);
        if (policy != PLACEMENT_INVAL_POLICY) {
                cfs_spin_lock(&lmv->lmv_lock);
                lmv->lmv_placement = policy;
                cfs_spin_unlock(&lmv->lmv_lock);
        } else {
                CERROR("Invalid placement policy \"%s\"!\n", dummy);
                return -EINVAL;
        }
        return count;
}

static int lmv_rd_activeobd(char *page, char **start, off_t off, int count,
                            int *eof, void *data)
{
        struct obd_device           *dev;
        struct lmv_desc             *desc;

        cfs_param_get_data(dev, data, NULL);
        LASSERT(dev != NULL);
        desc = &dev->u.lmv.desc;

        return cfs_param_snprintf(page, count, data, CFS_PARAM_U32,
                                  "%u\n", desc->ld_active_tgt_count);
}

static int lmv_rd_desc_uuid(char *page, char **start, off_t off, int count,
                            int *eof, void *data)
{
        struct obd_device           *dev;
        struct lmv_obd              *lmv;

        cfs_param_get_data(dev, data, NULL);
        LASSERT(dev != NULL);
        lmv = &dev->u.lmv;

        return cfs_param_snprintf(page, count, data, CFS_PARAM_STR,
                                  "%s\n", lmv->desc.ld_uuid.uuid);
}

static void *lmv_tgt_seq_start(cfs_seq_file_t *p, loff_t *pos)
{
        struct obd_device       *dev = cfs_seq_private(p);
        struct lmv_obd          *lmv = &dev->u.lmv;
        return (*pos >= lmv->desc.ld_tgt_count) ? NULL : &(lmv->tgts[*pos]);

}

static void lmv_tgt_seq_stop(cfs_seq_file_t *p, void *v)
{
        return;
}

static void *lmv_tgt_seq_next(cfs_seq_file_t *p, void *v, loff_t *pos)
{
        struct obd_device       *dev = cfs_seq_private(p);
        struct lmv_obd          *lmv = &dev->u.lmv;
        ++*pos;
        return (*pos >=lmv->desc.ld_tgt_count) ? NULL : &(lmv->tgts[*pos]);
}

static int lmv_tgt_seq_show(cfs_seq_file_t *p, void *v)
{
        struct lmv_tgt_desc     *tgt = v;
        struct obd_device       *dev = cfs_seq_private(p);
        struct lmv_obd          *lmv = &dev->u.lmv;
        int                      idx = tgt - &(lmv->tgts[0]);

        return cfs_seq_printf(p, "%d: %s %sACTIVE\n", idx, tgt->ltd_uuid.uuid,
                                 tgt->ltd_active ? "" : "IN");
}

cfs_seq_ops_t lmv_tgt_sops = {
        .start                 = lmv_tgt_seq_start,
        .stop                  = lmv_tgt_seq_stop,
        .next                  = lmv_tgt_seq_next,
        .show                  = lmv_tgt_seq_show,
};

static int lmv_target_seq_open(cfs_inode_t *inode, cfs_param_file_t *file)
{
        return cfs_param_seq_fopen(inode, file, &lmv_tgt_sops);
}

cfs_param_file_ops_t lmv_proc_target_fops = {
        .owner                = CFS_PARAM_MODULE,
        .open                 = lmv_target_seq_open,
        .read                 = cfs_seq_read,
        .llseek               = cfs_seq_lseek,
        .release              = cfs_seq_release,
};
struct lprocfs_vars lprocfs_lmv_obd_vars[] = {
        { "numobd",             lmv_rd_numobd,          0, 0 },
        { "placement",          lmv_rd_placement,       lmv_wr_placement, 0 },
        { "activeobd",          lmv_rd_activeobd,       0, 0 },
        { "uuid",               lprocfs_rd_uuid,        0, 0 },
        { "desc_uuid",          lmv_rd_desc_uuid,       0, 0 },
        { 0 }
};

static struct lprocfs_vars lprocfs_lmv_module_vars[] = {
        { "num_refs",           lprocfs_rd_numrefs,     0, 0 },
        { 0 }
};
#endif /* __KERNEL__ */
void lprocfs_lmv_init_vars(struct lprocfs_static_vars *lvars)
{
        lvars->module_vars    = lprocfs_lmv_module_vars;
        lvars->obd_vars       = lprocfs_lmv_obd_vars;
}
