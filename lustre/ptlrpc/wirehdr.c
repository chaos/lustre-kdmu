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

#define DEBUG_SUBSYSTEM S_RPC
#ifndef __KERNEL__
# include <liblustre.h>
#endif

#ifdef __KERNEL__
# ifndef AUTOCONF_INCLUDED
#  include <linux/config.h>
# endif
# ifdef CONFIG_FS_POSIX_ACL
#  include <linux/fs.h>
#  ifdef HAVE_XATTR_ACL
#   include <linux/xattr_acl.h>
#  else
#   define xattr_acl_entry  posix_acl_xattr_entry
#   define xattr_acl_header posix_acl_xattr_header
#  endif
#  ifdef HAVE_LINUX_POSIX_ACL_XATTR_H
#   include <linux/posix_acl_xattr.h>
#  endif
# endif
#endif

#include <obd_support.h>
#include <obd_class.h>
#include <lustre_net.h>
#include <lustre_disk.h>
