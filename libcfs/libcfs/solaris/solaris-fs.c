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
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * libcfs/libcfs/solaris/solaris-fs.c
 *
 */

#include <libcfs/libcfs.h>

cfs_file_t *
cfs_filp_open(const char *name, int flags, int mode, int *err)
{
        cfs_file_t *cfp;

        cfp = kmem_alloc(sizeof(*cfp), KM_NOSLEEP);
        if (cfp == NULL) {
                *err = -ENOMEM;
                return NULL;
        }

        flags = cfs_filp_fcntl2file(flags);

        *err = vn_open((char *)name, UIO_SYSSPACE,
                       flags, mode,
                       &cfp->cfl_vp, CRCREAT, 0);

        if (*err != 0) {
                kmem_free(cfp, sizeof(*cfp));
                *err = -*err;
                return NULL;
        }
                
        cfp->cfl_oflag = flags;
        cfp->cfl_pos   = 0;
        cfp->cfl_fp    = NULL;

        return cfp;
}

int cfs_filp_close(cfs_file_t *cfp)
{
        LASSERT (cfp != NULL && cfp->cfl_vp != NULL);

        if (cfp->cfl_fp != NULL) {
                LBUG();
        }
        
        (void) VOP_CLOSE(cfp->cfl_vp, cfp->cfl_oflag, 1, 0, CRED(), NULL);
        VN_RELE(cfp->cfl_vp);

        kmem_free(cfp, sizeof(*cfp));
        return 0;
}

int
cfs_filp_write(cfs_file_t *cfp, void *buf, size_t nbytes, loff_t *pos_p)
{
        int rc;
        offset_t pos;
        
        LASSERT (cfp != NULL && cfp->cfl_vp != NULL);
        LASSERT ((cfp->cfl_oflag & FWRITE) != 0);

        pos = (pos_p == NULL ? cfp->cfl_pos : *pos_p);
        
        rc = vn_rdwr(UIO_WRITE, cfp->cfl_vp, buf, nbytes, pos, UIO_SYSSPACE,
                     0, RLIM64_INFINITY, CRED(), NULL);

        if (rc == 0) {
                cfp->cfl_pos = pos + nbytes;

                if (pos_p != NULL)
                        *pos_p = cfp->cfl_pos;
        }
        
        return -rc;
}

int cfs_filp_fsync(cfs_file_t *cfp)
{
        LASSERT (cfp != NULL && cfp->cfl_vp != NULL);
        
        return -VOP_FSYNC(cfp->cfl_vp, FSYNC, CRED(), NULL);
}

/* XXX Consider changing API to return rc for error handling */
loff_t cfs_filp_size(cfs_file_t *cfp)
{
        vattr_t vattr;

        LASSERT (cfp != NULL && cfp->cfl_vp != NULL);

        vattr.va_mask = AT_SIZE;
        if(VOP_GETATTR(cfp->cfl_vp, &vattr, 0, CRED(), NULL) != 0) {
                LBUG();
        }

        return vattr.va_size;
}

cfs_file_t *
cfs_get_fd(int fd)
{
        cfs_file_t *cfp;
        file_t     *fp;

        cfp = kmem_zalloc(sizeof(*cfp), KM_NOSLEEP);
        if (cfp == NULL)
                return NULL;

        fp = getf(fd);
        if (fp == NULL) {
                kmem_free(cfp, sizeof(*cfp));
		return NULL;
        }

        if (fp->f_vnode == NULL) {
                CERROR("NULL vnode for fd=%d (fp=%p)\n", fd, fp);
                releasef(fd);
                kmem_free(cfp, sizeof(*cfp));
		return NULL;
        }

        cfp->cfl_vp    = fp->f_vnode;
        cfp->cfl_oflag = fp->f_flag;
        cfp->cfl_fp    = fp;

        mutex_enter(&fp->f_tlock);
        fp->f_count++;
        mutex_exit(&fp->f_tlock);

        releasef(fd);

        return cfp;
}

void
cfs_put_file(cfs_file_t *cfp)
{
        LASSERT (cfp->cfl_fp != NULL);
        closef(cfp->cfl_fp);
        kmem_free(cfp, sizeof(*cfp));
}

/* write to pipe; user-space will read written data from other end of pipe
 * NOTE: this returns 0 on success, not the number of bytes written. */
ssize_t
cfs_user_write (cfs_file_t *cfp, const char *buf, size_t count, loff_t *offset)
{
        vnode_t *vp   = cfp->cfl_vp;
        ssize_t  size = count;

        LASSERT (offset == NULL);
        LASSERT (vp != NULL);

        if (vp->v_type != VFIFO)
                return -ENOTSUPP;

        while (size > 0) {
                ssize_t resid;
                if (vn_rdwr(UIO_WRITE, vp, (caddr_t)buf, size, 0,
                            UIO_SYSSPACE, 0, RLIM64_INFINITY, CRED(), &resid) ||
                    resid >= size)
                        return -EIO;

                buf += size - resid;
                size = resid;
        }

        return 0;
}
