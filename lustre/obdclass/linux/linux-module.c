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

/* buffer MUST be at least the size of obd_ioctl_hdr */
int obd_ioctl_getdata(char **buf, int *len, void *arg)
{
        struct obd_ioctl_hdr hdr;
        struct obd_ioctl_data *data;
        int err;
        int offset = 0;
        ENTRY;

        err = cfs_copy_from_user(&hdr, (void *)arg, sizeof(hdr));
        if ( err )
                RETURN(err);

        if (hdr.ioc_version != OBD_IOCTL_VERSION) {
                CERROR("Version mismatch kernel (%x) vs application (%x)\n",
                       OBD_IOCTL_VERSION, hdr.ioc_version);
                RETURN(-EINVAL);
        }

        if (hdr.ioc_len > OBD_MAX_IOCTL_BUFFER) {
                CERROR("User buffer len %d exceeds %d max buffer\n",
                       hdr.ioc_len, OBD_MAX_IOCTL_BUFFER);
                RETURN(-EINVAL);
        }

        if (hdr.ioc_len < sizeof(struct obd_ioctl_data)) {
                CERROR("User buffer too small for ioctl (%d)\n", hdr.ioc_len);
                RETURN(-EINVAL);
        }

        /* XXX allocate this more intelligently, using kmalloc when
         * appropriate */
        OBD_VMALLOC(*buf, hdr.ioc_len);
        if (*buf == NULL) {
                CERROR("Cannot allocate control buffer of len %d\n",
                       hdr.ioc_len);
                RETURN(-EINVAL);
        }
        *len = hdr.ioc_len;
        data = (struct obd_ioctl_data *)*buf;

        err = cfs_copy_from_user(*buf, (void *)arg, hdr.ioc_len);
        if ( err ) {
                OBD_VFREE(*buf, hdr.ioc_len);
                RETURN(err);
        }

        if (obd_ioctl_is_invalid(data)) {
                CERROR("ioctl not correctly formatted\n");
                OBD_VFREE(*buf, hdr.ioc_len);
                RETURN(-EINVAL);
        }

        if (data->ioc_inllen1) {
                data->ioc_inlbuf1 = &data->ioc_bulk[0];
                offset += cfs_size_round(data->ioc_inllen1);
        }

        if (data->ioc_inllen2) {
                data->ioc_inlbuf2 = &data->ioc_bulk[0] + offset;
                offset += cfs_size_round(data->ioc_inllen2);
        }

        if (data->ioc_inllen3) {
                data->ioc_inlbuf3 = &data->ioc_bulk[0] + offset;
                offset += cfs_size_round(data->ioc_inllen3);
        }

        if (data->ioc_inllen4) {
                data->ioc_inlbuf4 = &data->ioc_bulk[0] + offset;
        }

        EXIT;
        return 0;
}

int obd_ioctl_popdata(void *arg, void *data, int len)
{
        int err;

        err = cfs_copy_to_user(arg, data, len);
        if (err)
                err = -EFAULT;
        return err;
}

EXPORT_SYMBOL(obd_ioctl_getdata);
EXPORT_SYMBOL(obd_ioctl_popdata);

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
