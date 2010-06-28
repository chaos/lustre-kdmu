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
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/lustre/llite/llite_nfs.c
 *
 * NFS export of Lustre Light File System
 *
 * Author: Yury Umanets <umka@clusterfs.com>
 * Author: Huang Hua <huanghua@clusterfs.com>
 */

#define DEBUG_SUBSYSTEM S_LLITE
#include <lustre_lite.h>
#include "llite_internal.h"
#ifdef HAVE_LINUX_EXPORTFS_H
#include <linux/exportfs.h>
#endif

__u32 get_uuid2int(const char *name, int len)
{
        __u32 key0 = 0x12a3fe2d, key1 = 0x37abe8f9;
        while (len--) {
                __u32 key = key1 + (key0 ^ (*name++ * 7152373));
                if (key & 0x80000000) key -= 0x7fffffff;
                key1 = key0;
                key0 = key;
        }
        return (key0 << 1);
}

static int ll_nfs_test_inode(struct inode *inode, void *opaque)
{
        return lu_fid_eq(&ll_i2info(inode)->lli_fid,
                         (struct lu_fid *)opaque);
}

static struct inode *search_inode_for_lustre(struct super_block *sb,
                                             const struct lu_fid *fid)
{
        struct ll_sb_info     *sbi = ll_s2sbi(sb);
        struct ptlrpc_request *req = NULL;
        struct inode          *inode = NULL;
        int                   eadatalen = 0;
        unsigned long         hash = (unsigned long) cl_fid_build_ino(fid);
        struct  md_op_data    *op_data;
        int                   rc;
        ENTRY;

        CDEBUG(D_INFO, "searching inode for:(%lu,"DFID")\n", hash, PFID(fid));

        inode = ilookup5(sb, hash, ll_nfs_test_inode, (void *)fid);
        if (inode)
                RETURN(inode);

        rc = ll_get_max_mdsize(sbi, &eadatalen);
        if (rc)
                RETURN(ERR_PTR(rc));

        /* Because inode is NULL, ll_prep_md_op_data can not
         * be used here. So we allocate op_data ourselves */
        OBD_ALLOC_PTR(op_data);
        if (op_data == NULL)
                return ERR_PTR(-ENOMEM);

        op_data->op_fid1 = *fid;
        op_data->op_mode = eadatalen;
        op_data->op_valid = OBD_MD_FLEASIZE;

        /* mds_fid2dentry ignores f_type */
        rc = md_getattr(sbi->ll_md_exp, op_data, &req);
        OBD_FREE_PTR(op_data);
        if (rc) {
                CERROR("can't get object attrs, fid "DFID", rc %d\n",
                       PFID(fid), rc);
                RETURN(ERR_PTR(rc));
        }
        rc = ll_prep_inode(&inode, req, sb);
        ptlrpc_req_finished(req);
        if (rc)
                RETURN(ERR_PTR(rc));

        RETURN(inode);
}

static struct dentry *ll_iget_for_nfs(struct super_block *sb,
                                      const struct lu_fid *fid)
{
        struct inode  *inode;
        struct dentry *result;
        ENTRY;

        CDEBUG(D_INFO, "Get dentry for fid: "DFID"\n", PFID(fid));
        if (!fid_is_sane(fid))
                RETURN(ERR_PTR(-ESTALE));

        inode = search_inode_for_lustre(sb, fid);
        if (IS_ERR(inode))
                RETURN(ERR_PTR(PTR_ERR(inode)));

        if (is_bad_inode(inode)) {
                /* we didn't find the right inode.. */
                CERROR("can't get inode by fid "DFID"\n",
                       PFID(fid));
                iput(inode);
                RETURN(ERR_PTR(-ESTALE));
        }

        result = d_obtain_alias(inode);
        if (!result) {
                iput(inode);
                RETURN(ERR_PTR(-ENOMEM));
        }

        ll_dops_init(result, 1);

        RETURN(result);
}

#define LUSTRE_NFS_FID          0x97

struct lustre_nfs_fid {
        struct lu_fid   lnf_child;
        struct lu_fid   lnf_parent;
};

/**
 * \a connectable - is nfsd will connect himself or this should be done
 *                  at lustre
 *
 * The return value is file handle type:
 * 1 -- contains child file handle;
 * 2 -- contains child file handle and parent file handle;
 * 255 -- error.
 */
static int ll_encode_fh(struct dentry *de, __u32 *fh, int *plen,
                        int connectable)
{
        struct inode *inode = de->d_inode;
        struct inode *parent = de->d_parent->d_inode;
        struct lustre_nfs_fid *nfs_fid = (void *)fh;
        ENTRY;

        CDEBUG(D_INFO, "encoding for (%lu,"DFID") maxlen=%d minlen=%d\n",
              inode->i_ino, PFID(ll_inode2fid(inode)), *plen,
              (int)sizeof(struct lustre_nfs_fid));

        if (*plen < sizeof(struct lustre_nfs_fid)/4)
                RETURN(255);

        nfs_fid->lnf_child = *ll_inode2fid(inode);
        nfs_fid->lnf_parent = *ll_inode2fid(parent);
        *plen = sizeof(struct lustre_nfs_fid)/4;

        RETURN(LUSTRE_NFS_FID);
}

#ifdef HAVE_FH_TO_DENTRY
static struct dentry *ll_fh_to_dentry(struct super_block *sb, struct fid *fid,
                                      int fh_len, int fh_type)
{
        struct lustre_nfs_fid *nfs_fid = (struct lustre_nfs_fid *)fid;

        if (fh_type != LUSTRE_NFS_FID)
                RETURN(ERR_PTR(-EPROTO));

        RETURN(ll_iget_for_nfs(sb, &nfs_fid->lnf_child));
}

static struct dentry *ll_fh_to_parent(struct super_block *sb, struct fid *fid,
                                      int fh_len, int fh_type)
{
        struct lustre_nfs_fid *nfs_fid = (struct lustre_nfs_fid *)fid;

        if (fh_type != LUSTRE_NFS_FID)
                RETURN(ERR_PTR(-EPROTO));

        RETURN(ll_iget_for_nfs(sb, &nfs_fid->lnf_parent));
}

#else

/*
 * This length is counted as amount of __u32,
 *  It is composed of a fid and a mode
 */
static struct dentry *ll_decode_fh(struct super_block *sb, __u32 *fh, int fh_len,
                                   int fh_type,
                                   int (*acceptable)(void *, struct dentry *),
                                   void *context)
{
        struct lustre_nfs_fid *nfs_fid = (void *)fh;
        struct dentry *entry;
        ENTRY;

        CDEBUG(D_INFO, "decoding for "DFID" fh_len=%d fh_type=%x\n", 
                PFID(&nfs_fid->lnf_child), fh_len, fh_type);

        if (fh_type != LUSTRE_NFS_FID)
                RETURN(ERR_PTR(-EPROTO));

        entry = sb->s_export_op->find_exported_dentry(sb, &nfs_fid->lnf_child,
                                                      &nfs_fid->lnf_parent,
                                                      acceptable, context);
        RETURN(entry);
}

static struct dentry *ll_get_dentry(struct super_block *sb, void *data)
{
        struct lustre_nfs_fid *fid = data;
        struct dentry      *entry;
        ENTRY;

        entry = ll_iget_for_nfs(sb, &fid->lnf_child);
        RETURN(entry);
}
#endif
static struct dentry *ll_get_parent(struct dentry *dchild)
{
        struct ptlrpc_request *req = NULL;
        struct inode          *dir = dchild->d_inode;
        struct ll_sb_info     *sbi;
        struct dentry         *result = NULL;
        struct mdt_body       *body;
        static char           dotdot[] = "..";
        struct md_op_data     *op_data;
        int                   rc;
        ENTRY;

        LASSERT(dir && S_ISDIR(dir->i_mode));

        sbi = ll_s2sbi(dir->i_sb);

        CDEBUG(D_INFO, "getting parent for (%lu,"DFID")\n",
                        dir->i_ino, PFID(ll_inode2fid(dir)));

        op_data = ll_prep_md_op_data(NULL, dir, NULL, dotdot,
                                     strlen(dotdot), 0,
                                     LUSTRE_OPC_ANY, NULL);
        if (op_data == NULL)
                RETURN(ERR_PTR(-ENOMEM));

        rc = md_getattr_name(sbi->ll_md_exp, op_data, &req);
        ll_finish_md_op_data(op_data);
        if (rc) {
                CERROR("failure %d inode %lu get parent\n", rc, dir->i_ino);
                RETURN(ERR_PTR(rc));
        }
        body = req_capsule_server_get(&req->rq_pill, &RMF_MDT_BODY);
        LASSERT(body->valid & OBD_MD_FLID);

        CDEBUG(D_INFO, "parent for "DFID" is "DFID"\n",
                PFID(ll_inode2fid(dir)), PFID(&body->fid1));

        result = ll_iget_for_nfs(dir->i_sb, &body->fid1);

        ptlrpc_req_finished(req);
        RETURN(result);
}

struct export_operations lustre_export_operations = {
       .get_parent = ll_get_parent,
       .encode_fh  = ll_encode_fh,
#ifdef HAVE_FH_TO_DENTRY
        .fh_to_dentry = ll_fh_to_dentry,
        .fh_to_parent = ll_fh_to_parent,
#else
       .get_dentry = ll_get_dentry,
       .decode_fh  = ll_decode_fh,
#endif
};
