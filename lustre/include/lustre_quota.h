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
 */

#ifndef _LUSTRE_QUOTA_H
#define _LUSTRE_QUOTA_H

/** \defgroup quota quota
 *
 * @{
 */

#if defined(__linux__)
#include <linux/lustre_quota.h>
#elif defined(__APPLE__)
#include <darwin/lustre_quota.h>
#elif defined(__WINNT__)
#include <winnt/lustre_quota.h>
#else
#error Unsupported operating system.
#endif

#include <lustre_net.h>
#include <lustre/lustre_idl.h>
#include <obd_support.h>

#if defined(__linux__)
#define CFS_MAXQUOTAS MAXQUOTAS
#define CFS_USRQUOTA USRQUOTA
#define CFS_GRPQUOTA GRPQUOTA
#else
#define CFS_MAXQUOTAS 2
#define CFS_USRQUOTA 0
#define CFS_GRPQUOTA 1
#endif /* __linux__ */

/*
 * Definitions of structures for vfsv0 quota format
 * Source linux/fs/quota/quotaio_v2.h
 *
 * The following definitions are normally found in private kernel headers.
 * However, some sites build Lustre against kernel development headers rather
 * than than full kernel source, so we provide them here for compatibility.
 */
#ifdef __KERNEL__
# if !defined(HAVE_QUOTAIO_V1_H) && !defined(HAVE_FS_QUOTA_QUOTAIO_V1_H) && \
     !defined(HAVE_FS_QUOTAIO_V1_H)

#include <linux/types.h>
#include <linux/quota.h>

#define V2_INITQMAGICS {\
        0xd9c01f11,     /* USRQUOTA */\
        0xd9c01927      /* GRPQUOTA */\
}

/* Header with type and version specific information */
struct v2_disk_dqinfo {
        __le32 dqi_bgrace;      /* Time before block soft limit becomes hard limit */
        __le32 dqi_igrace;      /* Time before inode soft limit becomes hard limit */
        __le32 dqi_flags;       /* Flags for quotafile (DQF_*) */
        __le32 dqi_blocks;      /* Number of blocks in file */
        __le32 dqi_free_blk;    /* Number of first free block in the list */
        __le32 dqi_free_entry;  /* Number of block with at least one free entry */
};

/* First generic header */
struct v2_disk_dqheader {
        __le32 dqh_magic;       /* Magic number identifying file */
        __le32 dqh_version;     /* File version */
};
#define V2_DQINFOOFF    sizeof(struct v2_disk_dqheader) /* Offset of info header in file */
#define QT_TREEOFF      1                               /* Offset of tree in file in blocks */
#define V2_DQTREEOFF    QT_TREEOFF

# endif /* !defined(HAVE_QUOTAIO_V1_H) ... */
#endif  /* __KERNEL__ */

/** @} quota */

#endif /* _LUSTRE_QUOTA_H */
