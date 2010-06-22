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

#ifndef __LINUX_MDT_H
#define __LINUX_MDT_H

/** \defgroup mdt mdt
 *
 * @{
 */

#include <libcfs/libcfs.h>

#include <lustre/lustre_idl.h>
#include <lustre_req_layout.h>
#include <md_object.h>
#include <dt_object.h>

/*
 * Common thread info for mdt, seq and fld
 */
struct com_thread_info {
        /*
         * for req-layout interface.
         */
        struct req_capsule *cti_pill;
};

enum {
        ESERIOUS = 0x0001000
};

static inline int err_serious(int rc)
{
        LASSERT(rc < 0);
        LASSERT(-rc < ESERIOUS);
        return -(-rc | ESERIOUS);
}

static inline int clear_serious(int rc)
{
        if (rc < 0)
                rc = -(-rc & ~ESERIOUS);
        return rc;
}

static inline int is_serious(int rc)
{
        return (rc < 0 && -rc & ESERIOUS);
}

/** @} mdt */

#endif
