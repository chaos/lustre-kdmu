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
 * libcfs/libcfs/solaris/solaris-module.c
 *
 */

#include <sys/types.h>
#include <sys/cred.h>
#include <sys/ddi.h>

#define DEBUG_SUBSYSTEM S_LNET
#define LNET_NAME "lnet"

#include <libcfs/libcfs.h>

#include <vm/seg_kpm.h> /* for kpm_enable only */

/* required for generic libcfs module.c; it's dummy on solaris */
cfs_psdev_t libcfs_dev;

/* what user sees in /dev */
#define LUSTREFS_LNET_MINOR_NAME "lnet"
#define LUSTREFS_OBD_MINOR_NAME  "obd"

/* minor node numbers correspondent to /dev names */
#define LUSTREFS_LNET_MINOR_NUMBER 0
#define LUSTREFS_OBD_MINOR_NUMBER  1

/* how many processes can open /dev/lnet concurrently */
#define CFS_MINOR_CHUNK 32

/* all Lustre modules are NOT unloadable by default */
int cfs_mods_unloadable = 1; /* Change it back to zero
                              * for production use */

static vmem_t     *lustrefs_arena;
static void       *lustrefs_softstate;
static dev_info_t *lustrefs_devi;

extern struct cfs_psdev_ops libcfs_psdev_ops;

/* Change the following  line to
 * 'extern struct cfs_psdev_ops obd_psdev_ops;'
 * when common class_obd.c is compiled in lsutrefs */
struct cfs_psdev_ops obd_psdev_ops;

#define LIBCFS_MAX_IOCTL_BUFFER 8192

int libcfs_ioctl_getdata(char **buf, int *len, void *arg)
{
        struct libcfs_ioctl_hdr   hdr;
        struct libcfs_ioctl_data *data;

        if (ddi_copyin(arg, &hdr, sizeof(hdr), 0))
                return (-EFAULT);

        if (hdr.ioc_version != LIBCFS_IOCTL_VERSION) {
                CERROR("PORTALS: version mismatch kernel vs application\n");
                return (-EINVAL);
        }

        if (hdr.ioc_len > LIBCFS_MAX_IOCTL_BUFFER) {
                CERROR("User buffer len %d exceeds %d max buffer\n",
                       hdr.ioc_len, LIBCFS_MAX_IOCTL_BUFFER);
                return (-EINVAL);
        }


        if (hdr.ioc_len < sizeof(struct libcfs_ioctl_data)) {
                CERROR("PORTALS: user buffer too small for ioctl\n");
                return (-EINVAL);
        }

        /* XXX allocate this more intelligently, using kmalloc when
         * appropriate */
        LIBCFS_ALLOC(*buf, hdr.ioc_len);
        if (*buf == NULL) {
                CERROR("Cannot allocate control buffer of len %d\n",
                       hdr.ioc_len);
                return(-EINVAL);
        }
        *len = hdr.ioc_len;
        data = (struct libcfs_ioctl_data *)(*buf);

        if (ddi_copyin(arg, *buf, hdr.ioc_len, 0))
                return (-EFAULT);

        if (libcfs_ioctl_is_invalid(data)) {
                CERROR("PORTALS: ioctl not correctly formatted\n");
                return (-EINVAL);
        }

        if (data->ioc_inllen1)
                data->ioc_inlbuf1 = &data->ioc_bulk[0];

        if (data->ioc_inllen2)
                data->ioc_inlbuf2 = &data->ioc_bulk[0] +
                        cfs_size_round(data->ioc_inllen1);

        return (0);
}

/* XXX: Not sure that it works as expected - need more testing
 *      when lctl is ported to Solaris */
int libcfs_ioctl_popdata(void *arg, void *data, int size)
{
	if (ddi_copyout(data, arg, size, 0))
		return (-EFAULT);
	return (0);
}

void libcfs_ioctl_freedata(char *buf, int len)
{       
        LIBCFS_FREE(buf, len);
}

static int
lustrefs_psdev_open(dev_t *devp, int flag, int otyp, cred_t *cred)
{
        int                              rc;
        minor_t                          mn;
        struct libcfs_device_userstate **pdu;

        /* /dev/obd doesn't need dynamic numbers because it
         * doesn't save any kernel state from open to close for any
         * of its ioctls */
        if (getminor(*devp) == LUSTREFS_OBD_MINOR_NUMBER) {

                if (obd_psdev_ops.p_open != NULL)
                        return obd_psdev_ops.p_open(0, NULL);

                return (-EPERM);
        }
        
        /* allocate a new minor number starting with 2 */
        mn = (minor_t)(uintptr_t)vmem_alloc(lustrefs_arena, 1, VM_SLEEP);

        *devp = makedevice(getmajor(*devp), mn);

        if (ddi_soft_state_zalloc(lustrefs_softstate, mn)
            != DDI_SUCCESS) {
                vmem_free(lustrefs_arena, (void *)(uintptr_t)mn, 1);
                return (ENXIO);
        }

        pdu = (struct libcfs_device_userstate **)
                ddi_get_soft_state(lustrefs_softstate, mn);
        LASSERT (pdu != NULL);

        if (libcfs_psdev_ops.p_open != NULL)
                rc = libcfs_psdev_ops.p_open(0, (void *)pdu);
        else
                rc = -EPERM;

        if (rc != 0) {
                ddi_soft_state_free(lustrefs_softstate, mn);
                vmem_free(lustrefs_arena, (void *)(uintptr_t)mn, 1);
        }
        
        LASSERT (rc <= 0);
        return (-rc);
}

static int
lustrefs_psdev_close(dev_t dev, int flag, int otyp, cred_t *cred)
{
        int                              rc;
        minor_t                          mn;
        struct libcfs_device_userstate **pdu;

        mn = getminor(dev);

        /* see comment about /dev/obd in lustrefs_psdev_open */
        if (mn == LUSTREFS_OBD_MINOR_NUMBER) {

                if (obd_psdev_ops.p_close != NULL)
                        return obd_psdev_ops.p_close(0, NULL);

                return (-EPERM);
        }

        pdu = (struct libcfs_device_userstate **)
                ddi_get_soft_state(lustrefs_softstate, mn);
        LASSERT (pdu != NULL);

	if (libcfs_psdev_ops.p_close != NULL)
		rc = libcfs_psdev_ops.p_close(0, (void *)*pdu);
	else
		rc = -EPERM;

        ddi_soft_state_free(lustrefs_softstate, mn);
        vmem_free(lustrefs_arena, (void *)(uintptr_t)mn, 1);

        LASSERT (rc <= 0);
        return (-rc);
}

static int
lustrefs_psdev_ioctl(dev_t dev, int cmd, intptr_t arg, int mode, cred_t *c,
                     int *rval)
{
        int                              rc;
        minor_t                          mn;
        struct libcfs_device_userstate **pdu;
        struct cfs_psdev_file            pfile;
        int                              dfd;

        rc = drv_priv(c);
	if (rc != 0)
		return (EACCES);

        mn = getminor(dev);

        /* NB: lustre uses 'f' as IOC TYPE but we cannot check it here
         * because it's in lustre_lib.h and libcfs shoudn't include
         * lustre h-files. See also comment about /dev/obd in
         * lustrefs_psdev_open */
        if (mn == LUSTREFS_OBD_MINOR_NUMBER) {
                /* obd on linux does: 'if (current->fsuid != 0) ...', but
                /* permissions are already checked above with drv_priv() */

                /* ignore all tty ioctls */
                if ((cmd & 0xffffff00) == ((int)'T') << 8)
                        return (-ENOTTY);

                if (obd_psdev_ops.p_ioctl != NULL)
                        return obd_psdev_ops.p_ioctl(NULL, cmd, (void *)arg);

                return (-EPERM);
        }        
        
	if ( (cmd & IOCTYPE) >> 8 != IOC_LIBCFS_TYPE ) {
		CDEBUG(D_IOCTL, "invalid ioctl ( type %d )\n",
		       cmd & IOCTYPE);
		return (EINVAL);
	}

	/* Handle platform-dependent IOC requests */
	switch (cmd) {
	case IOC_LIBCFS_PANIC:
		if (!cfs_capable(CFS_CAP_SYS_BOOT))
			return (EPERM);
		panic("debugctl-invoked panic");
		return (0);
	case IOC_LIBCFS_SET_DFD:
		if (ddi_copyin((void *)arg, &dfd, sizeof (dfd), 0))
			return (EFAULT);

		if (cfs_set_door(dfd))
                        return (EINVAL);

		return(0);
	case IOC_LIBCFS_MEMHOG:
		if (!cfs_capable(CFS_CAP_SYS_ADMIN))
			return (EPERM);
		/* fall through */
	}
        
        pdu = (struct libcfs_device_userstate **)
                ddi_get_soft_state(lustrefs_softstate, mn);
        LASSERT (pdu != NULL);

	pfile.off = 0;
	pfile.private_data = *pdu;
	if (libcfs_psdev_ops.p_ioctl != NULL)
		rc = libcfs_psdev_ops.p_ioctl(&pfile, cmd, (void *)arg);
	else
		rc = -EPERM;

        LASSERT (rc <= 0);
        return (-rc);
}

static struct cb_ops cb_ops = {
        lustrefs_psdev_open,  /* open */
        lustrefs_psdev_close, /* close */
        nodev,                /* strategy */
        nodev,                /* print */
        nodev,                /* dump */
        nodev,                /* read */
        nodev,                /* write */
        lustrefs_psdev_ioctl, /* ioctl */
        nodev,                /* devmap */
        nodev,                /* mmap */
        nodev,                /* segmap */
        nochpoll,             /* poll */
        ddi_prop_op,
        NULL,                 /* streamtab */
        D_MP,
        CB_REV,
        nodev,                /* aread */
        nodev,                /* awrite */
};

int
cfs_psdev_register(cfs_psdev_t *dev)
{
        return 0;
}

int
cfs_psdev_deregister(cfs_psdev_t *dev)
{
	return 0;
}


extern cfs_lumodule_desc_t libcfs_module_desc;
extern cfs_lumodule_desc_t lnet_module_desc;
extern cfs_lumodule_desc_t ksocknal_module_desc;
extern cfs_lumodule_desc_t lnet_selftest_module_desc;

static int
libcfs_all_modules_init(void)
{
        int rc;

        rc = libcfs_module_desc.mdesc_init();

        if (rc != 0)
                return rc;

        rc = lnet_module_desc.mdesc_init();

        if (rc != 0) {
                libcfs_module_desc.mdesc_fini();
                return rc;
        }
        
        rc = ksocknal_module_desc.mdesc_init();

        if (rc != 0) {
                lnet_module_desc.mdesc_fini();
                libcfs_module_desc.mdesc_fini();
                return rc;
        }

        rc = lnet_selftest_module_desc.mdesc_init();

        if (rc != 0) {
                ksocknal_module_desc.mdesc_fini();
                lnet_module_desc.mdesc_fini();
                libcfs_module_desc.mdesc_fini();
        }

        return rc;
}

static void
libcfs_all_modules_fini(void)
{
        lnet_selftest_module_desc.mdesc_fini();
        ksocknal_module_desc.mdesc_fini();
        lnet_module_desc.mdesc_fini();
        libcfs_module_desc.mdesc_fini();        
}

static int
lustrefs_getinfo(dev_info_t *dip, ddi_info_cmd_t cmd, void *arg, void **res)
{
        int rc = DDI_FAILURE;

        switch (cmd) {
        case DDI_INFO_DEVT2DEVINFO:
                if (lustrefs_devi == NULL)
                        rc = DDI_FAILURE;
                else {
                        *res = (void *)lustrefs_devi;
                        rc = DDI_SUCCESS;
                }
                break;
        case DDI_INFO_DEVT2INSTANCE:
                *res = (void *)0;
                rc = DDI_SUCCESS;
                break;
        default:
                break;
        }

        return rc;
}

static int
lustrefs_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
        int rc;

        switch (cmd) {
        case DDI_ATTACH:
                break;
        case DDI_RESUME:
                return DDI_SUCCESS;
        default:
                return DDI_FAILURE;
        }

        /* we could update tunables calling ddi_prop_get_int here */

        rc = ddi_soft_state_init(&lustrefs_softstate, sizeof(void *), 0);
        if (rc != DDI_SUCCESS) {
                cmn_err(CE_CONT,
                        "lustrefs failed to initialize soft state: %d", rc);
                return rc;
        }

        rc = ddi_create_minor_node(devi, LUSTREFS_LNET_MINOR_NAME,
                                   S_IFCHR, LUSTREFS_LNET_MINOR_NUMBER,
                                   DDI_PSEUDO, 0);

        if (rc != DDI_SUCCESS) {
                cmn_err(CE_CONT,
                        "lustrefs failed to create minor node 0 for %s: %d",
                        LUSTREFS_LNET_MINOR_NAME, rc);
                ddi_soft_state_fini(&lustrefs_softstate);
                return rc;
        }

        rc = ddi_create_minor_node(devi, LUSTREFS_OBD_MINOR_NAME,
                                   S_IFCHR, LUSTREFS_OBD_MINOR_NUMBER,
                                   DDI_PSEUDO, 0);

        if (rc != DDI_SUCCESS) {
                cmn_err(CE_CONT,
                        "lustrefs failed to create minor node 1 for %s: %d",
                        LUSTREFS_OBD_MINOR_NAME, rc);
                ddi_remove_minor_node(devi, NULL);
                ddi_soft_state_fini(&lustrefs_softstate);
                return rc;
        }

        /* lustrefs_getinfo will need devi */
        lustrefs_devi = devi;

        /* '2' below means that mn==0 is reserved for /dev/lnet and
         *  mn==1 for /dev/obd, so 2-32 are to be used for
         *  dynamic mn-s */
        lustrefs_arena = vmem_create("lustrefs", (void *)2,
                                     CFS_MINOR_CHUNK - 2, 1, NULL, NULL, NULL,
                                     0, VM_SLEEP | VMC_IDENTIFIER);

        return DDI_SUCCESS;
}

static int
lustrefs_detach(dev_info_t *devi, ddi_detach_cmd_t cmd)
{
        switch (cmd) {
        case DDI_DETACH:
                break;
        case DDI_SUSPEND:
                return DDI_SUCCESS;
        default:
                return DDI_FAILURE;
        }

        ddi_remove_minor_node(lustrefs_devi, NULL);
        lustrefs_devi = 0;

        vmem_destroy(lustrefs_arena);
        lustrefs_arena = NULL;

        ddi_soft_state_fini(&lustrefs_softstate);

        return DDI_SUCCESS;
}

static struct dev_ops dev_ops = {
        DEVO_REV,              /* devo_rev */
        0,                     /* devo_refcnt */
        lustrefs_getinfo,      /* devo_getinfo */
        nulldev,               /* devo_identify */
        nulldev,               /* devo_probe */
        lustrefs_attach,       /* devo_attach */
        lustrefs_detach,       /* devo_detach */
        nodev,                 /* devo_reset */
        &cb_ops,               /* devo_cb_ops */
        (struct bus_ops *)0,   /* devo_bus_ops */
        NULL                   /* devo_power */
};

static struct modldrv modldrv = {
        &mod_driverops, /* type of module */
        "1.0.0",        /* description */
        &dev_ops,       /* driver ops */
};

static struct modlinkage modlinkage = {
        MODREV_1, &modldrv, NULL
};

int _init(void)
{
        int rc;

        if (kpm_enable == 0) {
                cmn_err(CE_CONT, "lustrefs only supports kpm_enabled mode");
                return ENOTSUPP;
        }
        
        rc = libcfs_all_modules_init();
        if (rc != 0) {
                if (rc > 0)
                        panic("libcfs_all_modules_init() returned %d", rc);
                return -rc;
        }

        rc = mod_install(&modlinkage);
        if (rc != 0)
                libcfs_all_modules_fini();

        return rc;
}

int _fini(void)
{
        int rc;

        if (!cfs_mods_unloadable)
                return EBUSY;

        rc = mod_remove(&modlinkage);
        if (rc != 0)
                return rc;

        libcfs_all_modules_fini();

        return rc;
}

int _info(struct modinfo *modinfop)
{
        return mod_info(&modlinkage, modinfop);
}
