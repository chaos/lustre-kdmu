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
 * Copyright (c) 1999, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#ifndef _LINUX_OBD_SUPPORT
#define _LINUX_OBD_SUPPORT

#ifndef _OBD_SUPPORT
#error Do not #include this file directly. #include <obd_support.h> instead
#endif

#ifdef __KERNEL__
#ifndef AUTOCONF_INCLUDED
#include <linux/config.h>
#endif
#include <linux/seq_file.h>
#include <linux/module.h>
#include <linux/autoconf.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/swap.h>
#endif
#include <libcfs/libcfs.h>
#include <linux/lustre_compat25.h>
#include <lustre/lustre_idl.h>


#ifdef __KERNEL__
# include <linux/types.h>
# include <linux/blkdev.h>
# include <lvfs.h>
# define OBD_SLEEP_ON(wq, state)  wait_event_interruptible(wq, state)
#else /* !__KERNEL__ */
# define LTIME_S(time) (time)
/* for obd_class.h */
# ifndef ERR_PTR
#  define ERR_PTR(a) ((void *)(a))
# endif
#endif  /* __KERNEL__ */

#endif
