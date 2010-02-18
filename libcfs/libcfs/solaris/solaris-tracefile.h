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
 *
 * libcfs/libcfs/solaris/solaris-tracefile.h
 *
 */

#ifndef __LIBCFS_SOLARIS_TRACEFILE_H__
#define __LIBCFS_SOLARIS_TRACEFILE_H__

/* Only one type of trace_data in Solaris for now:
 * we're not supposed to be executed in interrupts, so there is
 * explicit LASSERT (!servicing_interrupt()) in trace_get_tcd().
 *
 * It's easy to support execution in interrupts but requires much
 * more types of trace_data than in Linux case because Solaris can
 * run as many interrupt threads concurrently as PILs exist (and
 * consequently much more memory would be used for tcd-s
 */
typedef enum {
        CFS_TCD_TYPE_PROC = 0,
        CFS_TCD_TYPE_INTERRUPT,
        CFS_TCD_TYPE_MAX
} cfs_trace_buf_type_t;

#endif
