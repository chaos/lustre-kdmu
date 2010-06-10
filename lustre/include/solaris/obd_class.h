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
 */

#ifndef __SOLARIS_CLASS_OBD_H__
#define __SOLARIS_CLASS_OBD_H__

#ifndef __CLASS_OBD_H
#error Do not #include this file directly. #include <obd_class.h> instead
#endif

#ifdef __KERNEL__

/* hash info structure used by the directory hash */
#  define LDISKFS_DX_HASH_LEGACY        0
#  define LDISKFS_DX_HASH_HALF_MD4      1
#  define LDISKFS_DX_HASH_TEA           2
#  define LDISKFS_DX_HASH_R5            3
#  define LDISKFS_DX_HASH_SAME          4
#  define LDISKFS_DX_HASH_MAX           4

/* hash info structure used by the directory hash */
struct ldiskfs_dx_hash_info
{
        u32     hash;
        u32     minor_hash;
        int     hash_version;
        u32     *seed;
};

#  define LDISKFS_HTREE_EOF     0x7fffffff

int ldiskfsfs_dirhash(const char *name, int len, struct ldiskfs_dx_hash_info *hinfo);

/* obdo.c */
void obdo_from_la(struct obdo *dst, struct lu_attr *la, obd_flag valid);
void la_from_obdo(struct lu_attr *la, struct obdo *dst, obd_flag valid);

#endif /* __KERNEL__ */

#endif /* __SOLARIS_CLASS_OBD_H__ */
