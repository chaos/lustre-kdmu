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
 * Copyright (c) 2004, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#ifndef EXPORT_SYMTAB
# define EXPORT_SYMTAB
#endif
#define DEBUG_SUBSYSTEM S_LNET
#include <lnet/lib-lnet.h>

static int config_on_load = 0;
CFS_MODULE_PARM(config_on_load, "i", int, 0444,
                "configure network at module load");

static cfs_semaphore_t lnet_config_mutex;

int
lnet_configure (void *arg)
{
        /* 'arg' only there so I can be passed to cfs_kernel_thread() */
        int    rc = 0;

        LNET_MUTEX_DOWN(&lnet_config_mutex);

        if (!the_lnet.ln_niinit_self) {
                rc = LNetNIInit(LUSTRE_SRV_LNET_PID);
                if (rc >= 0) {
                        the_lnet.ln_niinit_self = 1;
                        rc = 0;
                }
        }

        LNET_MUTEX_UP(&lnet_config_mutex);
        return rc;
}

int
lnet_unconfigure (void)
{
        int   refcount;
        
        LNET_MUTEX_DOWN(&lnet_config_mutex);

        if (the_lnet.ln_niinit_self) {
                the_lnet.ln_niinit_self = 0;
                LNetNIFini();
        }

        LNET_MUTEX_DOWN(&the_lnet.ln_api_mutex);
        refcount = the_lnet.ln_refcount;
        LNET_MUTEX_UP(&the_lnet.ln_api_mutex);

        LNET_MUTEX_UP(&lnet_config_mutex);
        return (refcount == 0) ? 0 : -EBUSY;
}

int
lnet_ioctl(unsigned int cmd, struct libcfs_ioctl_data *data)
{
        int   rc;

        switch (cmd) {
        case IOC_LIBCFS_CONFIGURE:
                return lnet_configure(NULL);

        case IOC_LIBCFS_UNCONFIGURE:
                return lnet_unconfigure();
                
        default:
                /* Passing LNET_PID_ANY only gives me a ref if the net is up
                 * already; I'll need it to ensure the net can't go down while
                 * I'm called into it */
                rc = LNetNIInit(LNET_PID_ANY);
                if (rc >= 0) {
                        rc = LNetCtl(cmd, data);
                        LNetNIFini();
                }
                return rc;
        }
}

DECLARE_IOCTL_HANDLER(lnet_ioctl_handler, lnet_ioctl);

int
init_lnet(void)
{
        int                  rc;
        ENTRY;

        cfs_init_mutex(&lnet_config_mutex);

        rc = LNetInit();
        if (rc != 0) {
                CERROR("LNetInit: error %d\n", rc);
                RETURN(rc);
        }

        rc = libcfs_register_ioctl(&lnet_ioctl_handler);
        LASSERT (rc == 0);

        if (config_on_load) {
                /* Have to schedule a separate thread to avoid deadlocking
                 * in modload */
                (void) cfs_kernel_thread(lnet_configure, NULL, 0);
        }

        RETURN(0);
}

void
fini_lnet(void)
{
        int rc;

        rc = libcfs_deregister_ioctl(&lnet_ioctl_handler);
        LASSERT (rc == 0);

        LNetFini();
}

EXPORT_SYMBOL(lnet_register_lnd);
EXPORT_SYMBOL(lnet_unregister_lnd);

EXPORT_SYMBOL(LNetMEAttach);
EXPORT_SYMBOL(LNetMEInsert);
EXPORT_SYMBOL(LNetMEUnlink);
EXPORT_SYMBOL(LNetEQAlloc);
EXPORT_SYMBOL(LNetMDAttach);
EXPORT_SYMBOL(LNetMDUnlink);
EXPORT_SYMBOL(LNetNIInit);
EXPORT_SYMBOL(LNetNIFini);
EXPORT_SYMBOL(LNetInit);
EXPORT_SYMBOL(LNetFini);
EXPORT_SYMBOL(LNetSnprintHandle);
EXPORT_SYMBOL(LNetPut);
EXPORT_SYMBOL(LNetGet);
EXPORT_SYMBOL(LNetEQWait);
EXPORT_SYMBOL(LNetEQFree);
EXPORT_SYMBOL(LNetEQGet);
EXPORT_SYMBOL(LNetGetId);
EXPORT_SYMBOL(LNetMDBind);
EXPORT_SYMBOL(LNetDist);
EXPORT_SYMBOL(LNetSetAsync);
EXPORT_SYMBOL(LNetCtl);
EXPORT_SYMBOL(LNetSetLazyPortal);
EXPORT_SYMBOL(LNetClearLazyPortal);
EXPORT_SYMBOL(the_lnet);
EXPORT_SYMBOL(lnet_iov_nob);
EXPORT_SYMBOL(lnet_extract_iov);
EXPORT_SYMBOL(lnet_kiov_nob);
EXPORT_SYMBOL(lnet_extract_kiov);
EXPORT_SYMBOL(lnet_copy_iov2iov);
EXPORT_SYMBOL(lnet_copy_iov2kiov);
EXPORT_SYMBOL(lnet_copy_kiov2iov);
EXPORT_SYMBOL(lnet_copy_kiov2kiov);
EXPORT_SYMBOL(lnet_finalize);
EXPORT_SYMBOL(lnet_parse);
EXPORT_SYMBOL(lnet_create_reply_msg);
EXPORT_SYMBOL(lnet_set_reply_msg_len);
EXPORT_SYMBOL(lnet_msgtyp2str);
EXPORT_SYMBOL(lnet_net2ni_locked);

MODULE_AUTHOR("Peter J. Braam <braam@clusterfs.com>");
MODULE_DESCRIPTION("Portals v3.1");
MODULE_LICENSE("GPL");

cfs_module(lnet, "1.0.0", init_lnet, fini_lnet);
