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
#define DEBUG_SUBSYSTEM S_CLASS

#include <linux/version.h>
#include <asm/statfs.h>
#include <lprocfs_status.h>
#include <obd_class.h>
#include <linux/seq_file.h>
#include "lov_internal.h"

#ifdef __KERNEL__
static int lov_rd_stripesize(char *page, char **start, off_t off, int count,
                             int *eof, void *data)
{
        struct obd_device *dev;
        struct lov_desc *desc;

        LIBCFS_PARAM_GET_DATA(dev, data, NULL);
        LASSERT(dev != NULL);
        desc = &dev->u.lov.desc;
        *eof = 1;

        return libcfs_param_snprintf(page, count, data, LP_U64, LPU64"\n",
                                     desc->ld_default_stripe_size);
}

static int lov_wr_stripesize(libcfs_file_t *file, const char *buffer,
                               unsigned long count, void *data)
{
        struct obd_device *dev;
        struct lov_desc *desc;
        __u64 val;
        int   flag = 0;
        int rc;

        LIBCFS_PARAM_GET_DATA(dev, data, NULL);
        LASSERT(dev != NULL);
        desc = &dev->u.lov.desc;
        rc = lprocfs_write_u64_helper(buffer, count, &val, flag);
        if (rc)
                return rc;

        lov_fix_desc_stripe_size(&val);
        desc->ld_default_stripe_size = val;
        return count;
}

static int lov_rd_stripeoffset(char *page, char **start, off_t off, int count,
                               int *eof, void *data)
{
        struct obd_device *dev;
        struct lov_desc *desc;

        LIBCFS_PARAM_GET_DATA(dev, data, NULL);
        LASSERT(dev != NULL);
        desc = &dev->u.lov.desc;
        *eof = 1;

        return libcfs_param_snprintf(page, count, data, LP_U64, LPU64"\n",
                                     desc->ld_default_stripe_offset);
}

static int lov_wr_stripeoffset(libcfs_file_t *file, const char *buffer,
                               unsigned long count, void *data)
{
        struct obd_device *dev;
        struct lov_desc *desc;
        __u64 val;
        int flag = 0;
        int rc;

        LIBCFS_PARAM_GET_DATA(dev, data, NULL);
        LASSERT(dev != NULL);
        desc = &dev->u.lov.desc;
        rc = lprocfs_write_u64_helper(buffer, count, &val, flag);
        if (rc)
                return rc;

        desc->ld_default_stripe_offset = val;
        return count;
}

static int lov_rd_stripetype(char *page, char **start, off_t off, int count,
                             int *eof, void *data)
{
        struct obd_device *dev;
        struct lov_desc *desc;

        LIBCFS_PARAM_GET_DATA(dev, data, NULL);
        LASSERT(dev != NULL);
        desc = &dev->u.lov.desc;
        *eof = 1;

        return libcfs_param_snprintf(page, count, data, LP_U32,
                                     "%u\n", desc->ld_pattern);
}

static int lov_wr_stripetype(libcfs_file_t *file, const char *buffer,
                             unsigned long count, void *data)
{
        struct obd_device *dev;
        struct lov_desc *desc;
        int val, rc, flag = 0;

        LIBCFS_PARAM_GET_DATA(dev, data, &flag);
        LASSERT(dev != NULL);
        desc = &dev->u.lov.desc;
        rc = lprocfs_write_helper(buffer, count, &val, flag);
        if (rc)
                return rc;

        lov_fix_desc_pattern(&val);
        desc->ld_pattern = val;
        return count;
}

static int lov_rd_stripecount(char *page, char **start, off_t off, int count,
                              int *eof, void *data)
{
        struct obd_device *dev;
        struct lov_desc *desc;
        int temp;

        LIBCFS_PARAM_GET_DATA(dev, data, NULL);
        LASSERT(dev != NULL);
        desc = &dev->u.lov.desc;
        *eof = 1;
        temp = (__s16)(desc->ld_default_stripe_count + 1) - 1;

        return libcfs_param_snprintf(page, count, data, LP_D32, "%d\n", temp);
}

static int lov_wr_stripecount(libcfs_file_t *file, const char *buffer,
                              unsigned long count, void *data)
{
        struct obd_device *dev;
        struct lov_desc *desc;
        int val, rc, flag = 0;

        LIBCFS_PARAM_GET_DATA(dev, data, &flag);
        LASSERT(dev != NULL);
        desc = &dev->u.lov.desc;
        rc = lprocfs_write_helper(buffer, count, &val, flag);
        if (rc)
                return rc;

        lov_fix_desc_stripe_count(&val);
        desc->ld_default_stripe_count = val;
        return count;
}

static int lov_rd_numobd(char *page, char **start, off_t off, int count,
                         int *eof, void *data)
{
        struct obd_device *dev;
        struct lov_desc *desc;

        LIBCFS_PARAM_GET_DATA(dev, data, NULL);
        LASSERT(dev != NULL);
        desc = &dev->u.lov.desc;
        *eof = 1;

        return libcfs_param_snprintf(page, count, data, LP_U32,
                                     "%u\n", desc->ld_tgt_count);
}

static int lov_rd_activeobd(char *page, char **start, off_t off, int count,
                            int *eof, void *data)
{
        struct obd_device *dev;
        struct lov_desc *desc;

        LIBCFS_PARAM_GET_DATA(dev, data, NULL);
        LASSERT(dev != NULL);
        desc = &dev->u.lov.desc;
        *eof = 1;

        return libcfs_param_snprintf(page, count, data, LP_U32, "%u\n",
                                     desc->ld_active_tgt_count);
}

static int lov_rd_desc_uuid(char *page, char **start, off_t off, int count,
                            int *eof, void *data)
{
        struct obd_device *dev;
        struct lov_obd *lov;

        LIBCFS_PARAM_GET_DATA(dev, data, NULL);
        LASSERT(dev != NULL);
        lov = &dev->u.lov;
        *eof = 1;

        return libcfs_param_snprintf(page, count, data, LP_STR, "%s\n",
                                     lov->desc.ld_uuid.uuid);
}

/* free priority (0-255): how badly user wants to choose empty osts */
static int lov_rd_qos_priofree(char *page, char **start, off_t off, int count,
                               int *eof, void *data)
{
        struct obd_device *dev;
        struct lov_obd *lov;
        int flag = 0;

        LIBCFS_PARAM_GET_DATA(dev, data, &flag);
        LASSERT(dev != NULL);
        lov = &dev->u.lov;
        *eof = 1;

        return libcfs_param_snprintf(page, count, data, LP_STR, "%d%%\n",
                                     (lov->lov_qos.lq_prio_free * 100) >> 8);
}

static int lov_wr_qos_priofree(libcfs_file_t *file, const char *buffer,
                               unsigned long count, void *data)
{
        struct obd_device *dev;
        struct lov_obd *lov;
        int val, rc, flag = 0;

        LIBCFS_PARAM_GET_DATA(dev, data, &flag);
        LASSERT(dev != NULL);
        lov = &dev->u.lov;
        rc = lprocfs_write_helper(buffer, count, &val, flag);
        if (rc)
                return rc;

        if (val > 100)
                return -EINVAL;
        lov->lov_qos.lq_prio_free = (val << 8) / 100;
        lov->lov_qos.lq_dirty = 1;
        lov->lov_qos.lq_reset = 1;
        return count;
}

static int lov_rd_qos_thresholdrr(char *page, char **start, off_t off,
                                  int count, int *eof, void *data)
{
        struct obd_device *dev;
        struct lov_obd *lov;
        int flag = 0;

        LIBCFS_PARAM_GET_DATA(dev, data, &flag);
        LASSERT(dev != NULL);
        lov = &dev->u.lov;
        *eof = 1;

        return libcfs_param_snprintf(page, count, data, LP_STR, "%d%%\n",
                                     (lov->lov_qos.lq_threshold_rr * 100) >> 8);
}

static int lov_wr_qos_thresholdrr(libcfs_file_t *file, const char *buffer,
                                  unsigned long count, void *data)
{
        struct obd_device *dev;
        struct lov_obd *lov;
        int val, rc;
        int flag = 0;

        LIBCFS_PARAM_GET_DATA(dev, data, &flag);
        LASSERT(dev != NULL);
        lov = &dev->u.lov;
        rc = lprocfs_write_helper(buffer, count, &val, flag);
        if (rc)
                return rc;

        if (val > 100 || val < 0)
                return -EINVAL;

        lov->lov_qos.lq_threshold_rr = (val << 8) / 100;
        lov->lov_qos.lq_dirty = 1;
        return count;
}

static int lov_rd_qos_maxage(char *page, char **start, off_t off, int count,
                             int *eof, void *data)
{
        struct obd_device *dev;
        struct lov_obd *lov;

        LIBCFS_PARAM_GET_DATA(dev, data, NULL);
        LASSERT(dev != NULL);
        lov = &dev->u.lov;
        *eof = 1;

        return libcfs_param_snprintf_common(page, count, data, LP_U32,
                        NULL, "sec", "%u", lov->desc.ld_qos_maxage);
}

static int lov_wr_qos_maxage(libcfs_file_t *file, const char *buffer,
                             unsigned long count, void *data)
{
        struct obd_device *dev;
        struct lov_obd *lov;
        int val, rc;
        int flag = 0;

        LIBCFS_PARAM_GET_DATA(dev, data, &flag);
        LASSERT(dev != NULL);
        lov = &dev->u.lov;
        rc = lprocfs_write_helper(buffer, count, &val, flag);
        if (rc)
                return rc;

        if (val <= 0)
                return -EINVAL;
        lov->desc.ld_qos_maxage = val;
        return count;
}

static void *lov_tgt_seq_start(libcfs_seq_file_t *p, loff_t *pos)
{
        struct obd_device *dev = LIBCFS_SEQ_PRIVATE(p);
        struct lov_obd *lov;

        lov = &dev->u.lov;
        while (*pos < lov->desc.ld_tgt_count) {
                if (lov->lov_tgts[*pos])
                        return lov->lov_tgts[*pos];
                ++*pos;
        }
        return NULL;
}

static void lov_tgt_seq_stop(libcfs_seq_file_t *p, void *v)
{
}

static void *lov_tgt_seq_next(libcfs_seq_file_t *p, void *v,
                              loff_t *pos)
{
        struct obd_device *dev = LIBCFS_SEQ_PRIVATE(p);
        struct lov_obd *lov;

        lov = &dev->u.lov;
        while (++*pos < lov->desc.ld_tgt_count) {
                if (lov->lov_tgts[*pos])
                        return lov->lov_tgts[*pos];
        }
        return NULL;
}

static int lov_tgt_seq_show(libcfs_seq_file_t *seq, void *v)
{
        struct lov_tgt_desc *tgt = (struct lov_tgt_desc *)v;

        return LIBCFS_SEQ_PRINTF(seq, "%d: %s %sACTIVE\n",
                                 tgt->ltd_index, obd_uuid2str(&tgt->ltd_uuid),
                                 tgt->ltd_active ? "" : "IN");
}

libcfs_seq_ops_t lov_tgt_sops = {
        .start = lov_tgt_seq_start,
        .stop = lov_tgt_seq_stop,
        .next = lov_tgt_seq_next,
        .show = lov_tgt_seq_show,
};

static int lov_target_seq_open(libcfs_inode_t *inode,
                               libcfs_file_t *file)
{
        libcfs_param_dentry_t *dp = LIBCFS_PDE(inode);
        libcfs_seq_file_t *seq;
        int rc;

        LPROCFS_ENTRY_AND_CHECK(dp);
        LIBCFS_SEQ_OPEN(file, &lov_tgt_sops, rc);
        if (rc) {
                LPROCFS_EXIT();
                return rc;
        }
        seq = LIBCFS_FILE_PRIVATE(file);
        LIBCFS_SEQ_PRIVATE(seq) = LIBCFS_DENTRY_DATA(dp);
        return rc;
}

libcfs_file_ops_t lov_proc_target_fops = {
        .owner   = THIS_MODULE,
        .open    = lov_target_seq_open,
        .read    = LIBCFS_SEQ_READ_COMMON,
        .llseek  = LIBCFS_SEQ_LSEEK_COMMON,
        .release = libcfs_param_seq_release_common,
};

struct lprocfs_vars lprocfs_lov_obd_vars[] = {
        { "uuid",         lprocfs_rd_uuid,        0, 0 },
        { "stripesize",   lov_rd_stripesize,      lov_wr_stripesize, 0 },
        { "stripeoffset", lov_rd_stripeoffset,    lov_wr_stripeoffset, 0 },
        { "stripecount",  lov_rd_stripecount,     lov_wr_stripecount, 0 },
        { "stripetype",   lov_rd_stripetype,      lov_wr_stripetype, 0 },
        { "numobd",       lov_rd_numobd,          0, 0 },
        { "activeobd",    lov_rd_activeobd,       0, 0 },
        { "filestotal",   lprocfs_rd_filestotal,  0, 0 },
        { "filesfree",    lprocfs_rd_filesfree,   0, 0 },
        /*{ "filegroups", lprocfs_rd_filegroups,  0, 0 },*/
        { "blocksize",    lprocfs_rd_blksize,     0, 0 },
        { "kbytestotal",  lprocfs_rd_kbytestotal, 0, 0 },
        { "kbytesfree",   lprocfs_rd_kbytesfree,  0, 0 },
        { "kbytesavail",  lprocfs_rd_kbytesavail, 0, 0 },
        { "desc_uuid",    lov_rd_desc_uuid,       0, 0 },
        { "qos_prio_free",lov_rd_qos_priofree,    lov_wr_qos_priofree, 0 },
        { "qos_threshold_rr",  lov_rd_qos_thresholdrr, lov_wr_qos_thresholdrr, 0 },
        { "qos_maxage",   lov_rd_qos_maxage,      lov_wr_qos_maxage, 0 },
        { 0 }
};

static struct lprocfs_vars lprocfs_lov_module_vars[] = {
        { "num_refs",     lprocfs_rd_numrefs,     0, 0 },
        { 0 }
};

void lprocfs_lov_init_vars(struct lprocfs_static_vars *lvars)
{
    lvars->module_vars  = lprocfs_lov_module_vars;
    lvars->obd_vars     = lprocfs_lov_obd_vars;
}
#endif /* __KERNEL__ */
