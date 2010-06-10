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
 * lustre/include/solaris/lustre_log.h
 *
 * Generic infrastructure for managing a collection of logs.
 * These logs are used for:
 *  - orphan recovery: OST adds record on create
 *  - mtime/size consistency: the OST adds a record on first write
 *  - open/unlinked objects: OST adds a record on destroy
 *
 *  - mds unlink log: the MDS adds an entry upon delete
 *
 *  - raid1 replication log between OST's
 *  - MDS replication logs
 */

#ifndef __SOLARIS_LUSTRE_LOG_H__
#define __SOLARIS_LUSTRE_LOG_H__

#ifndef _LUSTRE_LOG_H
#error Do not #include this file directly. #include <lustre_log.h> instead
#endif

/* #define LUSTRE_LOG_SERVER */

#endif /* __SOLARIS_LUSTRE_LOG_H__ */
