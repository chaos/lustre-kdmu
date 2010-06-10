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
 * Copyright  2008 Sun Microsystems, Inc. All rights reserved
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/stat.h>
#include <linux/smp_lock.h>
#include <linux/version.h>
#define DEBUG_SUBSYSTEM S_LLITE

#include <lustre_lite.h>
#include "llite_internal.h"

static int ll_readlink_internal(struct inode *inode,
                                struct ptlrpc_request **request, char **symname)
{
        struct ll_inode_info *lli = ll_i2info(inode);
        struct ll_sb_info *sbi = ll_i2sbi(inode);
        int rc, symlen = i_size_read(inode) + 1;
        struct mdt_body *body;
        struct md_op_data *op_data;
        ENTRY;

        *request = NULL;

        if (lli->lli_symlink_name) {
                *symname = lli->lli_symlink_name;
                CDEBUG(D_INODE, "using cached symlink %s\n", *symname);
                RETURN(0);
        }

        op_data = ll_prep_md_op_data(NULL, inode, NULL, NULL, 0, symlen,
                                     LUSTRE_OPC_ANY, NULL);
        op_data->op_valid = OBD_MD_LINKNAME;
        rc = md_getattr(sbi->ll_md_exp, op_data, request);
        ll_finish_md_op_data(op_data);
        if (rc) {
                if (rc != -ENOENT)
                        CERROR("inode %lu: rc = %d\n", inode->i_ino, rc);
                GOTO (failed, rc);
        }

        body = req_capsule_server_get(&(*request)->rq_pill, &RMF_MDT_BODY);
        LASSERT(body != NULL);
        if ((body->valid & OBD_MD_LINKNAME) == 0) {
                CERROR("OBD_MD_LINKNAME not set on reply\n");
                GOTO(failed, rc = -EPROTO);
        }

        LASSERT(symlen != 0);
        if (body->eadatasize != symlen) {
                CERROR("inode %lu: symlink length %d not expected %d\n",
                        inode->i_ino, body->eadatasize - 1, symlen - 1);
                GOTO(failed, rc = -EPROTO);
        }

        *symname = req_capsule_server_get(&(*request)->rq_pill, &RMF_MDT_MD);
        if (*symname == NULL ||
            strnlen(*symname, symlen) != symlen - 1) {
                /* not full/NULL terminated */
                CERROR("inode %lu: symlink not NULL terminated string"
                        "of length %d\n", inode->i_ino, symlen - 1);
                GOTO(failed, rc = -EPROTO);
        }

        OBD_ALLOC(lli->lli_symlink_name, symlen);
        /* do not return an error if we cannot cache the symlink locally */
        if (lli->lli_symlink_name) {
                memcpy(lli->lli_symlink_name, *symname, symlen);
                *symname = lli->lli_symlink_name;
        }
        RETURN(0);

failed:
        RETURN (rc);
}

static int ll_readlink(struct dentry *dentry, char *buffer, int buflen)
{
        struct inode *inode = dentry->d_inode;
        struct ptlrpc_request *request;
        char *symname;
        int rc;
        ENTRY;

        CDEBUG(D_VFSTRACE, "VFS Op\n");
        /* on symlinks lli_open_sem protects lli_symlink_name allocation/data */
        ll_inode_size_lock(inode, 0);
        rc = ll_readlink_internal(inode, &request, &symname);
        if (rc)
                GOTO(out, rc);

        rc = vfs_readlink(dentry, buffer, buflen, symname);
 out:
        ptlrpc_req_finished(request);
        ll_inode_size_unlock(inode, 0);
        RETURN(rc);
}

#ifdef HAVE_COOKIE_FOLLOW_LINK
# define LL_FOLLOW_LINK_RETURN_TYPE void *
#else
# define LL_FOLLOW_LINK_RETURN_TYPE int
#endif

static LL_FOLLOW_LINK_RETURN_TYPE ll_follow_link(struct dentry *dentry,
                                                 struct nameidata *nd)
{
        struct inode *inode = dentry->d_inode;
#ifdef HAVE_VFS_INTENT_PATCHES
        struct lookup_intent *it = ll_nd2it(nd);
#endif
        struct ptlrpc_request *request = NULL;
        int rc;
        char *symname;
        ENTRY;

#ifdef HAVE_VFS_INTENT_PATCHES
        if (it != NULL) {
                int op = it->it_op;
                int mode = it->it_create_mode;

                ll_intent_release(it);
                it->it_op = op;
                it->it_create_mode = mode;
        }
#endif

        CDEBUG(D_VFSTRACE, "VFS Op\n");
        /* Limit the recursive symlink depth to 5 instead of default
         * 8 links when kernel has 4k stack to prevent stack overflow.
         * For 8k stacks we need to limit it to 7 for local servers. */
        if (THREAD_SIZE < 8192 && current->link_count >= 6) {
                rc = -ELOOP;
        } else if (THREAD_SIZE == 8192 && current->link_count >= 8) {
                rc = -ELOOP;
        } else {
                ll_inode_size_lock(inode, 0);
                rc = ll_readlink_internal(inode, &request, &symname);
                ll_inode_size_unlock(inode, 0);
        }
        if (rc) {
                cfs_path_put(nd); /* Kernel assumes that ->follow_link()
                                     releases nameidata on error */
                GOTO(out, rc);
        }

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,8))
        rc = vfs_follow_link(nd, symname);
#else
#ifdef HAVE_COOKIE_FOLLOW_LINK
        nd_set_link(nd, symname);
        /* @symname may contain a pointer to the request message buffer,
           we delay request releasing until ll_put_link then. */
        RETURN(request);
#else
        if (lli->lli_symlink_name == NULL) {
                /* falling back to recursive follow link if the request
                 * needs to be cleaned up still. */
                rc = vfs_follow_link(nd, symname);
                GOTO(out, rc);
        }
        nd_set_link(nd, symname);
        RETURN(0);
#endif
#endif
out:
        ptlrpc_req_finished(request);
#ifdef HAVE_COOKIE_FOLLOW_LINK
        RETURN(ERR_PTR(rc));
#else
        RETURN(rc);
#endif
}

#ifdef HAVE_COOKIE_FOLLOW_LINK
static void ll_put_link(struct dentry *dentry, struct nameidata *nd, void *cookie)
{
        ptlrpc_req_finished(cookie);
}
#endif

struct inode_operations ll_fast_symlink_inode_operations = {
        .readlink       = ll_readlink,
        .setattr        = ll_setattr,
#ifdef HAVE_VFS_INTENT_PATCHES
        .setattr_raw    = ll_setattr_raw,
#endif
        .follow_link    = ll_follow_link,
#ifdef HAVE_COOKIE_FOLLOW_LINK
        .put_link       = ll_put_link,
#endif
        .getattr        = ll_getattr,
        .permission     = ll_inode_permission,
        .setxattr       = ll_setxattr,
        .getxattr       = ll_getxattr,
        .listxattr      = ll_listxattr,
        .removexattr    = ll_removexattr,
};
