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

#include <sys/policy.h>
#include <sys/mount.h>
#include <fs/fs_subr.h>
#include <sys/vfs_opreg.h>

#include <libcfs/libcfs.h>
#include <obd_class.h>
#include <lustre/lustre_user.h>
#include <lustre_disk.h>

static int lustrefstype = 0;
static major_t lustrefsmaj;
static minor_t lustrefsmin = 0;
static kmutex_t lustrefs_minor_lock;

int lustrefs_active_count = 0;

static struct vnodeops *lustrefs_vnodeops = NULL;

typedef struct lustrevfs {
        struct lustre_sb_info *lvfs_lsi;
        vnode_t               *lvfs_rvp;
} lustrevfs_t;

/*************** lustrefs vnode ops. ***************/

static void
lustrefs_inactive(vnode_t *vp, cred_t *cr, caller_context_t *ct)
{
        vfs_t *vfsp = vp->v_vfsp;
        lustrevfs_t *lvfsp;

        LASSERT(vfsp != NULL);

        lvfsp = (lustrevfs_t *)vfsp->vfs_data;
        LASSERT(lvfsp != NULL);
        LASSERT(lvfsp->lvfs_rvp == vp);

        mutex_enter(&vp->v_lock);
        LASSERT(vp->v_count >= 1);
        if (--vp->v_count != 0) {
                mutex_exit(&vp->v_lock);
                return;
        }
        mutex_exit(&vp->v_lock);
        vn_invalid(vp);
        vn_free(vp);

        lvfsp->lvfs_rvp = NULL;

        VFS_RELE(vfsp);
}

/********** Callbacks from common lustre code **********/

int lustre_register_fs(void)
{
        RETURN(0);
}

int lustre_unregister_fs(void)
{
        if (lustrefstype) {
                LASSERT(lustrefs_vnodeops != NULL);
                vn_freevnodeops(lustrefs_vnodeops);
                (void) vfs_freevfsops_by_type(lustrefstype);
                mutex_destroy(&lustrefs_minor_lock);
                lustrefstype = 0;
                lustrefs_vnodeops = NULL;
        }

        RETURN(0);
}

void lustre_osvfs_update(void *osvfs, struct lustre_sb_info *lsi)
{
        vfs_t           *vfsp = (vfs_t *)osvfs;
        lustrevfs_t     *lvfsp;

        LASSERT(vfsp != NULL);
        lvfsp = (lustrevfs_t *)vfsp->vfs_data;
        LASSERT(lvfsp != NULL);
        lvfsp->lvfs_lsi = lsi;
}

int lustre_osvfs_mount(void *osvfs)
{
        vfs_t           *vfsp = (vfs_t *)osvfs;
        dev_t           dev;
        lustrevfs_t     *lvfsp;
        vnode_t         *vp;

        vfsp->vfs_fstype = lustrefstype;

        /*
         * L_MAXMIN32 is 256K-1.
         *
         * XXX If we have more than 256K active Lustre server targets per host
         * the loop below will spin forever.

         * Also if 256K mounts/umounts manage to happen after we allocate a
         * minor # below but before vfs framework on return from here adds us
         * to a vfs list we may end up with duplicate minor #'s for separate
         * lustrefs's.  Not good, ah? But many other solaris fs's that don't
         * have real disk major/minor to mount (e.g. zfs) have the same
         * bug. Well we just join the club and hope 256K unique minor numbers
         * is enough to avoid either issue for lustre since typically there're
         * just a few OSTs/MDTs per host.
         */

        mutex_enter(&lustrefs_minor_lock);
        do {
                lustrefsmin = (lustrefsmin + 1) & L_MAXMIN32;
                vfsp->vfs_dev = makedevice(lustrefsmaj, lustrefsmin);
        } while (vfs_devismounted(vfsp->vfs_dev));
        mutex_exit(&lustrefs_minor_lock);

        vfs_make_fsid(&vfsp->vfs_fsid, vfsp->vfs_dev, lustrefstype);
        vfsp->vfs_bsize = 1024;

        lvfsp = (lustrevfs_t *)vfsp->vfs_data;
        LASSERT(lvfsp != NULL);

        vp = vn_alloc(KM_SLEEP);
        vp->v_vfsp = vfsp;
        vn_setops(vp, lustrefs_vnodeops);
        vp->v_type = VDIR;
        vp->v_data = NULL;
        vp->v_flag |= VROOT;
        
        lvfsp->lvfs_rvp = vp;

        /*
         * keep vfs around until its root vnode goes away.
         */
        VFS_HOLD(vfsp);

        RETURN(0);
}

/*************** lustrefs vfs ops. ***************/

static int
lustrefs_mount(vfs_t *vfsp, vnode_t *mvp, struct mounta *uap, cred_t *cr)
{
        lustrevfs_t          *lvfsp;
        char                 *opts = uap->optptr;
        int                  optlen = uap->optlen;
        char                 *inargs;
        int                  error;

        if (secpolicy_fs_mount(cr, mvp, vfsp) != 0)
                RETURN(EPERM);

        if (mvp->v_type != VDIR)
                RETURN(ENOTDIR);

        if (!(uap->flags & MS_OPTIONSTR))
                RETURN(EINVAL);

        if (optlen <= 1 || optlen > MAX_MNTOPT_STR)
                RETURN(EINVAL);

        if (uap->flags & MS_SYSSPACE)
                RETURN(EINVAL);

        mutex_enter(&mvp->v_lock);
        if ((uap->flags & MS_OVERLAY) == 0 &&
            (uap->flags & MS_REMOUNT) == 0 &&
            (mvp->v_count > 1 || (mvp->v_flag & VROOT))) {
                mutex_exit(&mvp->v_lock);
                RETURN(EBUSY);
        }
        mutex_exit(&mvp->v_lock);

        inargs = kmem_alloc(MAX_MNTOPT_STR, KM_SLEEP);
        error = copyinstr(opts, inargs, (size_t)optlen, NULL);
        if (error)
                RETURN(error);

        lvfsp = kmem_zalloc(sizeof (lustrevfs_t), KM_SLEEP);
        vfsp->vfs_data = lvfsp;

#if 0
        /*
         * lustre_mount() will call lustre_osvfs_mount().
         */
        error = lustre_mount(vfsp, inargs, 0);
        if (error < 0)
                error = -error;
#else
        error = lustre_osvfs_mount(vfsp);
        LASSERT(error == 0);
        LCONSOLE(0, "lustre_mount with options: %s\n", inargs);
#endif

        kmem_free(inargs, MAX_MNTOPT_STR);
        if (error) {
                kmem_free(lvfsp, sizeof (lustrevfs_t));
                vfsp->vfs_data = NULL;
        } else {
                atomic_add_32(&lustrefs_active_count, 1);
        }

        RETURN(error);
}

static void
lustrefs_freevfs(vfs_t *vfsp)
{
        lustrevfs_t *lvfsp = vfsp->vfs_data;

        LASSERT(lvfsp->lvfs_lsi == NULL);
        LASSERT(lvfsp->lvfs_rvp == NULL);

        kmem_free(lvfsp, sizeof (lustrevfs_t));

        atomic_add_32(&lustrefs_active_count, -1);
        LASSERT(lustrefs_active_count >= 0);
}

/* ARGSUSED */
static int
lustrefs_umount(vfs_t *vfsp, int flag, cred_t *cr)
{
        lustrevfs_t *lvfsp;
#if 0
        struct lustre_sb_info *lsi;
#endif
        vnode_t *rvp;

        if (secpolicy_fs_unmount(cr, vfsp) != 0)
                RETURN(EPERM);

        lvfsp = (lustrevfs_t *)vfsp->vfs_data;
        LASSERT(lvfsp != NULL);

        rvp = (vnode_t *)lvfsp->lvfs_rvp;
        LASSERT(rvp != NULL);

        if (!(flag & MS_FORCE)) {
                if (rvp->v_count > 1)
                        RETURN(EBUSY);
        }

#if 0
        lsi = (struct lustre_sb_info *)lvfsp->lvfs_lsi;
        LASSERT(lsi != NULL);
        if (flag & MS_FORCE)
                lustre_umount_server_force_flag_set(lsi);
        lustre_server_umount(lsi);
#endif

        VN_RELE(rvp);

        RETURN(0);
}

static int
lustrefs_root(vfs_t *vfsp, vnode_t **vpp)
{
        lustrevfs_t *lvfsp = (lustrevfs_t *)vfsp->vfs_data;
        vnode_t *vp;

        vp = (vnode_t *)lvfsp->lvfs_rvp;

        VN_HOLD(vp);
        *vpp = vp;
        RETURN(0);
}

/*
 * No locking required because I held the root vnode before calling this
 * function so the vfs won't disappear on me.
 */
static int
lustrefs_statvfs(struct vfs *vfsp, struct statvfs64 *sp)
{
        dev32_t d32;

        /*
         * XXX Need to call lustre_server_statfs() here.
         */

        bzero(sp, sizeof (*sp));
        sp->f_bsize = vfsp->vfs_bsize;
        sp->f_frsize = vfsp->vfs_bsize;
        sp->f_blocks = (fsblkcnt64_t)1;
        sp->f_bfree = (fsblkcnt64_t)0;
        sp->f_bavail = (fsblkcnt64_t)0;
        sp->f_files = (fsfilcnt64_t)1;
        sp->f_ffree = (fsfilcnt64_t)0;
        sp->f_favail = (fsfilcnt64_t)0;
        (void) cmpldev(&d32, vfsp->vfs_dev);
        sp->f_fsid = d32;
        (void) strcpy(sp->f_basetype, vfssw[lustrefstype].vsw_name);
        sp->f_flag = vf_to_stf(vfsp->vfs_flag);
        sp->f_namemax = MAXNAMELEN - 1;
        (void) strlcpy(sp->f_fstr, "lustrefs", sizeof (sp->f_fstr));

        RETURN(0);
}

static const fs_operation_def_t lustrefs_vnodeops_template[] = {
        VOPNAME_SETFL,          { .error = fs_nosys },
        VOPNAME_FRLOCK,         { .error = fs_nosys },
        VOPNAME_SHRLOCK,        { .error = fs_nosys },
        VOPNAME_DISPOSE,        { .error = fs_error },
        VOPNAME_SETSECATTR,     { .error = fs_nosys },
        VOPNAME_GETSECATTR,     { .error = fs_nosys },
        VOPNAME_PATHCONF,       { .error = fs_nosys },
        VOPNAME_POLL,           { .error = fs_error },
        VOPNAME_INACTIVE,       { .vop_inactive = lustrefs_inactive },
        NULL,                   NULL
};

static const fs_operation_def_t lustrefs_vfsops_template[] = {
        VFSNAME_MOUNT,          { .vfs_mount = lustrefs_mount },
        VFSNAME_UNMOUNT,        { .vfs_unmount = lustrefs_umount },
        VFSNAME_ROOT,           { .vfs_root = lustrefs_root },
        VFSNAME_STATVFS,        { .vfs_statvfs = lustrefs_statvfs },
        VFSNAME_FREEVFS,        { .vfs_freevfs = lustrefs_freevfs },
        NULL,                   NULL
};

/*************** lustrefs global setup ***************/

static int
lustrefsinit(int fstype, char *name)
{
        int error;

        lustrefstype = fstype;
        ASSERT(lustrefstype != 0);

        /*
         * Associate VFS ops vector with this fstype.
         */
        error = vfs_setfsops(fstype, lustrefs_vfsops_template, NULL);
        if (error != 0) {
                cmn_err(CE_WARN, "lustrefsinit: bad vfs ops template");
                RETURN(error);
        }

        error = vn_make_ops(name, lustrefs_vnodeops_template, &lustrefs_vnodeops);
        if (error != 0) {
                (void) vfs_freevfsops_by_type(fstype);
                cmn_err(CE_WARN, "lustrefsinit: bad vnode ops template");
                RETURN(error);
        }

        lustrefsmaj = ddi_name_to_major(LUSTREFS_DRIVER);

        mutex_init(&lustrefs_minor_lock, NULL, MUTEX_DEFAULT, NULL);

        RETURN(0);
}

static vfsdef_t vfw = {
        VFSDEF_VERSION,
        "lustrefs",
        lustrefsinit,
        0,
        NULL
};

struct modlfs lustrefs_modlfs = {
        &mod_fsops,
        "lustrefs fs module",
        &vfw
};
