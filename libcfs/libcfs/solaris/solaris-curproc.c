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
 * libcfs/libcfs/solaris/solaris-curproc.c
 *
 */

/*
 * Implementation of cfs_curproc API
 */

#include <libcfs/libcfs.h>

uid_t  cfs_curproc_uid(void)
{
        return 0;
}

gid_t  cfs_curproc_gid(void)
{
        return 0;
}

uid_t  cfs_curproc_fsuid(void)
{
        return 0;
}

gid_t  cfs_curproc_fsgid(void)
{
        return 0;
}

pid_t  cfs_curproc_pid(void)
{
        return (pid_t)ddi_get_kt_did();
}

int    cfs_curproc_groups_nr(void)
{
        return 0;
}

void   cfs_curproc_groups_dump(gid_t *array, int size)
{
        memset(array, 0, size * sizeof(__u32));
}

int    cfs_curproc_is_in_groups(gid_t gid)
{
        return 0;
}

mode_t cfs_curproc_umask(void)
{
        return 0;
}

char  *cfs_curproc_comm(void)
{
        if (curthread->t_lwp != NULL &&
            curthread->t_lwp->lwp_procp != NULL)
                return curthread->t_lwp->lwp_procp->p_user.u_comm;
        else
                return "unknown";
}

/*
 *  capabilities support 
 */

void cfs_cap_raise(cfs_cap_t cap)
{
}

void cfs_cap_lower(cfs_cap_t cap)
{
}

int cfs_cap_raised(cfs_cap_t cap)
{
        return 1;
}

cfs_cap_t cfs_curproc_cap_pack(void)
{
        return -1;
}

void cfs_curproc_cap_unpack(cfs_cap_t cap)
{
}

int cfs_capable(cfs_cap_t cap)
{
        return 1;
}
