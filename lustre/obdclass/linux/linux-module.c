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
 * lustre/obdclass/linux/linux-module.c
 *
 * Object Devices Class Driver
 * These are the only exported functions, they provide some generic
 * infrastructure for managing object devices
 */

#define DEBUG_SUBSYSTEM S_CLASS
#ifndef EXPORT_SYMTAB
# define EXPORT_SYMTAB
#endif

#ifdef __KERNEL__
#ifndef AUTOCONF_INCLUDED
#include <linux/config.h> /* for CONFIG_PROC_FS */
#endif
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/lp.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/fcntl.h>
#include <linux/delay.h>
#include <linux/skbuff.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/highmem.h>
#include <asm/io.h>
#include <asm/ioctls.h>
#include <asm/system.h>
#include <asm/poll.h>
#include <asm/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/smp_lock.h>
#include <linux/seq_file.h>
#else
# include <liblustre.h>
#endif

#include <libcfs/libcfs.h>
#include <obd_support.h>
#include <obd_class.h>
#include <lnet/lnetctl.h>
#include <lprocfs_status.h>
#include <lustre_ver.h>
#include <lustre/lustre_build_version.h>
#ifdef __KERNEL__
#include <linux/lustre_version.h>

int proc_version;

/*  opening /dev/obd */
static int obd_class_open(struct inode * inode, struct file * file)
{
        ENTRY;

        PORTAL_MODULE_USE;
        RETURN(0);
}

/*  closing /dev/obd */
static int obd_class_release(struct inode * inode, struct file * file)
{
        ENTRY;

        PORTAL_MODULE_UNUSE;
        RETURN(0);
}

/* to control /dev/obd */
static int obd_class_ioctl(struct inode *inode, struct file *filp,
                           unsigned int cmd, unsigned long arg)
{
        int err = 0;
        ENTRY;

        /* Allow non-root access for OBD_IOC_PING_TARGET - used by lfs check */
        if (!cfs_capable(CFS_CAP_SYS_ADMIN) && (cmd != OBD_IOC_PING_TARGET))
                RETURN(err = -EACCES);
        if ((cmd & 0xffffff00) == ((int)'T') << 8) /* ignore all tty ioctls */
                RETURN(err = -ENOTTY);

        err = class_handle_ioctl(cmd, (unsigned long)arg);

        RETURN(err);
}

/* declare character device */
static struct file_operations obd_psdev_fops = {
        .owner   = THIS_MODULE,
        .ioctl   = obd_class_ioctl,     /* ioctl */
        .open    = obd_class_open,      /* open */
        .release = obd_class_release,   /* release */
};

/* modules setup */
cfs_psdev_t obd_psdev = {
        .minor = OBD_DEV_MINOR,
        .name  = OBD_DEV_NAME,
        .fops  = &obd_psdev_fops,
};

#endif
/* Check that we're building against the appropriate version of the Lustre
 * kernel patch */
#include <linux/lustre_version.h>
#ifdef LUSTRE_KERNEL_VERSION
#define LUSTRE_MIN_VERSION 45
#define LUSTRE_MAX_VERSION 47
#if (LUSTRE_KERNEL_VERSION < LUSTRE_MIN_VERSION)
# error Cannot continue: Your Lustre kernel patch is older than the sources
#elif (LUSTRE_KERNEL_VERSION > LUSTRE_MAX_VERSION)
# error Cannot continue: Your Lustre sources are older than the kernel patch
#endif
#endif
