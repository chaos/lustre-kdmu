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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */

#define DEBUG_SUBSYSTEM S_CLASS
#define D_MOUNT D_SUPER|D_CONFIG /*|D_WARNING */
#define PRINT_CMD CDEBUG
#define PRINT_MASK D_SUPER|D_CONFIG

#include <obd.h>
#include <lvfs.h>
#include <lustre_fsfilt.h>
#include <obd_class.h>
#include <lustre/lustre_user.h>
#include <lustre_log.h>
#include <lustre_disk.h>
#include <lustre_param.h>

void lustre_osvfs_update(void *osvfs, void *data)
{
        LASSERT(osvfs != NULL);
}

int lustre_osvfs_mount(void *osvfs)
{
        RETURN(0);
}
