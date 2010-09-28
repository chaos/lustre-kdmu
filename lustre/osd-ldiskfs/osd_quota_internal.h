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
 * lustre/osd-ldiskfs/osd_quota_internal.h
 *
 * Header of quota code is specific to ldiskfs
 *
 * Author: Landen Tian <landen@sun.com>
 */

#ifndef _OSD_LDISKFS_QUOTA_H
#define _OSD_LDISKFS_QUOTA_H

#include <lvfs.h>

#include "osd_internal.h"

#ifdef HAVE_QUOTA_SUPPORT

/* structures to access admin quotafile */
struct lustre_mem_dqinfo {
        unsigned int dqi_bgrace;
        unsigned int dqi_igrace;
        unsigned long dqi_flags;
        unsigned int dqi_blocks;
        unsigned int dqi_free_blk;
        unsigned int dqi_free_entry;
};

#ifdef __KERNEL__

struct lustre_quota_info {
        struct file *qi_files[MAXQUOTAS];
        struct lustre_mem_dqinfo qi_info[MAXQUOTAS];
        lustre_quota_version_t qi_version;
};

#else

struct lustre_quota_info {
};

#endif

#define DQ_STATUS_AVAIL         0x0     /* Available dquot */
#define DQ_STATUS_SET           0x01    /* Sombody is setting dquot */
#define DQ_STATUS_RECOVERY      0x02    /* dquot is in recovery */

struct lustre_mem_dqblk {
        __u64 dqb_bhardlimit;	/**< absolute limit on disk blks alloc */
        __u64 dqb_bsoftlimit;	/**< preferred limit on disk blks */
        __u64 dqb_curspace;	/**< current used space */
        __u64 dqb_ihardlimit;	/**< absolute limit on allocated inodes */
        __u64 dqb_isoftlimit;	/**< preferred inode limit */
        __u64 dqb_curinodes;	/**< current # allocated inodes */
        time_t dqb_btime;	/**< time limit for excessive disk use */
        time_t dqb_itime;	/**< time limit for excessive inode use */
};

struct lustre_dquot {
        /** Hash list in memory, protect by dquot_hash_lock */
        cfs_list_t dq_hash;
        /** Protect the data in lustre_dquot */
        cfs_semaphore_t dq_sem;
        /** Use count */
        int dq_refcnt;
        /** Pointer of quota info it belongs to */
        struct lustre_quota_info *dq_info;
        /** Offset of dquot on disk */
        loff_t dq_off;
        /** ID this applies to (uid, gid) */
        unsigned int dq_id;
        /** Type of quota (CFS_USRQUOTA, CFS_GRPQUOUTA) */
        int dq_type;
        /** See DQ_STATUS_ */
        unsigned short dq_status;
        /** See DQ_ in quota.h */
        unsigned long dq_flags;
        /** Diskquota usage */
        struct lustre_mem_dqblk dq_dqb;
};

/* admin quotafile operations */
int lustre_check_quota_file(struct lustre_quota_info *lqi, int type);
int lustre_read_quota_info(struct lustre_quota_info *lqi, int type);
int lustre_write_quota_info(struct lustre_quota_info *lqi, int type);
int lustre_read_dquot(struct lustre_dquot *dquot);
int lustre_commit_dquot(struct lustre_dquot *dquot);
int lustre_init_quota_info(struct lustre_quota_info *lqi, int type);
int lustre_get_qids(struct file *file, struct inode *inode, int type,
                    cfs_list_t *list);
int lustre_quota_convert(struct lustre_quota_info *lqi, int type);

typedef int (*dqacq_handler_t) (struct obd_device * obd, struct qunit_data * qd,
                                int opc);

/*
#ifdef HAVE_VFS_DQ_OFF
#define LL_DQUOT_OFF(sb, remount)    vfs_dq_off(sb, remount)
#else
#define LL_DQUOT_OFF(sb, remount)    DQUOT_OFF(sb)
#endif
*/

#define LL_DQUOT_OFF(sb)    DQUOT_OFF(sb)

struct quotacheck_thread_args {
        struct obd_export   *qta_exp;   /** obd export */
        struct obd_device   *qta_obd;   /** obd device */
        struct obd_quotactl  qta_oqctl; /** obd_quotactl args */
        struct super_block  *qta_sb;    /** obd super block */
        cfs_semaphore_t     *qta_sem;   /** obt_quotachecking */
};

#define LUSTRE_ADMIN_QUOTAFILES_V2 {\
        "admin_quotafile_v2.usr",       /** user admin quotafile */\
        "admin_quotafile_v2.grp"        /** group admin quotafile */\
}

#else

#define LL_DQUOT_OFF(sb) do {} while(0)

struct lustre_quota_info {
};

#endif /* !HAVE_QUOTA_SUPPORT */

#endif  /* _OSD_LDISKFS_QUOTA_H */
