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
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/llite/dir.c
 *
 * Directory code for lustre client.
 */

#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/mm.h>
#include <linux/version.h>
#include <linux/smp_lock.h>
#include <asm/uaccess.h>
#include <linux/buffer_head.h>   // for wait_on_buffer

#define DEBUG_SUBSYSTEM S_LLITE

#include <obd_support.h>
#include <obd_class.h>
#include <lustre_lib.h>
#include <lustre/lustre_idl.h>
#include <lustre_lite.h>
#include <lustre_dlm.h>
#include <lustre_fid.h>
#include "llite_internal.h"

#ifndef HAVE_PAGE_CHECKED
#ifdef HAVE_PG_FS_MISC
#define PageChecked(page)        test_bit(PG_fs_misc, &(page)->flags)
#define SetPageChecked(page)     set_bit(PG_fs_misc, &(page)->flags)
#else
#error PageChecked or PageFsMisc not defined in kernel
#endif
#endif

/*
 * (new) readdir implementation overview.
 *
 * Original lustre readdir implementation cached exact copy of raw directory
 * pages on the client. These pages were indexed in client page cache by
 * logical offset in the directory file. This design, while very simple and
 * intuitive had some inherent problems:
 *
 *     . it implies that byte offset to the directory entry serves as a
 *     telldir(3)/seekdir(3) cookie, but that offset is not stable: in
 *     ext3/htree directory entries may move due to splits, and more
 *     importantly,
 *
 *     . it is incompatible with the design of split directories for cmd3,
 *     that assumes that names are distributed across nodes based on their
 *     hash, and so readdir should be done in hash order.
 *
 * New readdir implementation does readdir in hash order, and uses hash of a
 * file name as a telldir/seekdir cookie. This led to number of complications:
 *
 *     . hash is not unique, so it cannot be used to index cached directory
 *     pages on the client (note, that it requires a whole pageful of hash
 *     collided entries to cause two pages to have identical hashes);
 *
 *     . hash is not unique, so it cannot, strictly speaking, be used as an
 *     entry cookie. ext3/htree has the same problem and lustre implementation
 *     mimics their solution: seekdir(hash) positions directory at the first
 *     entry with the given hash.
 *
 * Client side.
 *
 * 0. caching
 *
 * Client caches directory pages using hash of the first entry as an index. As
 * noted above hash is not unique, so this solution doesn't work as is:
 * special processing is needed for "page hash chains" (i.e., sequences of
 * pages filled with entries all having the same hash value).
 *
 * First, such chains have to be detected. To this end, server returns to the
 * client the hash of the first entry on the page next to one returned. When
 * client detects that this hash is the same as hash of the first entry on the
 * returned page, page hash collision has to be handled. Pages in the
 * hash chain, except first one, are termed "overflow pages".
 *
 * Solution to index uniqueness problem is to not cache overflow
 * pages. Instead, when page hash collision is detected, all overflow pages
 * from emerging chain are immediately requested from the server and placed in
 * a special data structure (struct ll_dir_chain). This data structure is used
 * by ll_readdir() to process entries from overflow pages. When readdir
 * invocation finishes, overflow pages are discarded. If page hash collision
 * chain weren't completely processed, next call to readdir will again detect
 * page hash collision, again read overflow pages in, process next portion of
 * entries and again discard the pages. This is not as wasteful as it looks,
 * because, given reasonable hash, page hash collisions are extremely rare.
 *
 * 1. directory positioning
 *
 * When seekdir(hash) is called, original
 *
 *
 *
 *
 *
 *
 *
 *
 * Server.
 *
 * identification of and access to overflow pages
 *
 * page format
 *
 *
 *
 *
 *
 */

/* returns the page unlocked, but with a reference */
void ll_release_page(struct page *page, __u64 hash, __u64 start, __u64 end)
{
        kunmap(page);
        lock_page(page);
        if (likely(page->mapping != NULL)) {
                ll_truncate_complete_page(page);
                unlock_page(page);
        } else {
                unlock_page(page);
                CWARN("NULL mapping page %p, truncated by others: "
                      "hash("LPX64") | start("LPX64") | end("LPX64")\n",
                      page, hash, start, end);
        }
        page_cache_release(page);
}

struct page *ll_get_dir_page(struct inode *dir, __u64 pos, __u64 stripe_offset,
                             int exact, struct ll_dir_chain *chain)
{
        struct page *page;
        struct md_op_data *op_data;
        struct md_page_callback cb_op;
        struct ptlrpc_request *request = NULL;
        int rc = 0;

        op_data = ll_prep_md_op_data(NULL, dir, dir, NULL, 0, 0,
                                     LUSTRE_OPC_ANY, dir);
        /**
         * FIXME choose the start offset of the readdir
         */
        op_data->op_stripe_offset = stripe_offset;
        op_data->op_hash_offset = pos;
        op_data->op_exact = exact;

        cb_op.md_blocking_ast = ll_md_blocking_ast;

        rc = md_readpage(ll_i2mdexp(dir), op_data, &cb_op, &request, &page);
        if (request != NULL) {
                struct mdt_body   *body;
                body = req_capsule_server_get(&request->rq_pill, &RMF_MDT_BODY);
                /* Checked by mdc_readpage() */
                LASSERT(body != NULL);
                if (body->valid & OBD_MD_FLSIZE)
                        cl_isize_write(dir, body->size);
                ptlrpc_req_finished(request);
        }
        ll_finish_md_op_data(op_data);
        if (rc)
                page = ERR_PTR(rc);
        return page;

}

int ll_put_page(struct inode *inode, __u64 stripe_offset, struct page *page)
{
        struct md_op_data *op_data;
        int rc;

        op_data = ll_prep_md_op_data(NULL, inode, NULL, NULL, 0, 0,
                                     LUSTRE_OPC_ANY, NULL);

        op_data->op_stripe_offset = stripe_offset;
        rc = md_put_page(ll_i2mdexp(inode), op_data, page);

        ll_finish_md_op_data(op_data);

        return rc;
}

int ll_readdir(struct file *filp, void *cookie, filldir_t filldir)
{
        struct inode         *inode     = filp->f_dentry->d_inode;
        struct ll_inode_info *info      = ll_i2info(inode);
        __u64                 pos       = filp->f_pos;
        struct ll_file_data  *lfd = LUSTRE_FPRIVATE(filp);
        struct page          *page;
        struct ll_dir_chain   chain;
        int rc;
        int done;
        int shift;
        __u16 type;
        ENTRY;

        CDEBUG(D_VFSTRACE, "VFS Op:inode=%lu/%u(%p) pos %lu/%llu\n",
               inode->i_ino, inode->i_generation, inode,
               (unsigned long)pos, i_size_read(inode));

        if (pos == DIR_END_OFF)
                /*
                 * end-of-file.
                 */
                RETURN(0);

        rc    = 0;
        done  = 0;
        shift = 0;
        ll_dir_chain_init(&chain);

        page = ll_get_dir_page(inode, pos, lfd->ll_fd_offset, 0, &chain);

        while (rc == 0 && !done) {
                struct lu_dirpage *dp;
                struct lu_dirent  *ent;

                if (!IS_ERR(page)) {
                        /*
                         * If page is empty (end of directory is reached),
                         * use this value.
                         */
                        __u64 hash = DIR_END_OFF;
                        __u64 next;
                        dp = page_address(page);
                        for (ent = lu_dirent_start(dp); ent != NULL && !done;
                             ent = lu_dirent_next(ent)) {
                                char          *name;
                                int            namelen;
                                struct lu_fid  fid;
                                __u64          ino;

                                /*
                                 * XXX: implement correct swabbing here.
                                 */

                                hash    = le64_to_cpu(ent->lde_hash);
                                namelen = le16_to_cpu(ent->lde_namelen);

                                if (hash < pos)
                                        /*
                                         * Skip until we find target hash
                                         * value.
                                         */
                                        continue;

                                if (namelen == 0)
                                        /*
                                         * Skip dummy record.
                                         */
                                        continue;

                                fid  = ent->lde_fid;
                                name = ent->lde_name;
                                fid_le_to_cpu(&fid, &fid);
                                if (cfs_curproc_is_32bit())
                                        ino = cl_fid_build_ino32(&fid);
                                else
                                        ino = cl_fid_build_ino(&fid);
                                type = ll_dirent_type_get(ent);
                                done = filldir(cookie, name, namelen,
                                               (loff_t)hash, ino, type);
                        }
                        next = le64_to_cpu(dp->ldp_hash_end);
                        ll_put_page(inode, lfd->ll_fd_offset, page);
                        if (!done) {
                                pos = next;
                                if (pos == DIR_END_OFF &&
                                    (info->lli_mea == NULL ||
                                     info->lli_mea->mea_count ==
                                          (lfd->ll_fd_offset + 1))){
                                        /*
                                         * End of directory reached.
                                         */
                                        done = 1;

                                } else if (1 /* chain is exhausted*/) {
                                        /*
                                         * Normal case: continue to the next
                                         * page.
                                         */
                                        if (pos == DIR_END_OFF) {
                                                lfd->ll_fd_offset ++;
                                                pos = 0;
                                                filp->f_pos = 0;
                                        }
                                        page = ll_get_dir_page(inode, pos,
                                                          lfd->ll_fd_offset, 1,
                                                               &chain);
                                } else {
                                        /*
                                         * go into overflow page.
                                         */
                                }
                        } else
                                pos = hash;
                } else {
                        rc = PTR_ERR(page);
                        CERROR("error reading dir "DFID" at %lu: rc %d\n",
                               PFID(&info->lli_fid), (unsigned long)pos, rc);
                }
        }

        filp->f_pos = (loff_t)pos;
        filp->f_version = inode->i_version;
        touch_atime(filp->f_vfsmnt, filp->f_dentry);

        ll_dir_chain_fini(&chain);

        RETURN(rc);
}

int ll_send_mgc_param(struct obd_export *mgc, char *string)
{
        struct mgs_send_param *msp;
        int rc = 0;

        OBD_ALLOC_PTR(msp);
        if (!msp)
                return -ENOMEM;

        strncpy(msp->mgs_param, string, MGS_PARAM_MAXLEN);
        rc = obd_set_info_async(mgc, sizeof(KEY_SET_INFO), KEY_SET_INFO,
                                sizeof(struct mgs_send_param), msp, NULL);
        if (rc)
                CERROR("Failed to set parameter: %d\n", rc);
        OBD_FREE_PTR(msp);

        return rc;
}

char *ll_get_fsname(struct inode *inode)
{
        struct lustre_sb_info *lsi = s2lsi(inode->i_sb);
        char *ptr, *fsname;
        int len;

        OBD_ALLOC(fsname, MGS_PARAM_MAXLEN);
        len = strlen(lsi->lsi_lmd->lmd_profile);
        ptr = strrchr(lsi->lsi_lmd->lmd_profile, '-');
        if (ptr && (strcmp(ptr, "-client") == 0))
                len -= 7;
        strncpy(fsname, lsi->lsi_lmd->lmd_profile, len);
        fsname[len] = '\0';

        return fsname;
}

int ll_dir_setdirstripe(struct inode *dir, struct lmv_user_md *lump,
                        char *filename)
{
        struct ptlrpc_request *request = NULL;
        struct md_op_data *op_data;
        struct ll_sb_info *sbi = ll_i2sbi(dir);
        int mode;
        int err;

        ENTRY;

        mode = (0755 & (S_IRWXUGO|S_ISVTX) & ~current->fs->umask) | S_IFDIR;
        op_data = ll_prep_md_op_data(NULL, dir, NULL, filename,
                                     strlen(filename), mode, LUSTRE_OPC_MKDIR, lump);
        if (IS_ERR(op_data))
                GOTO(err_exit, err = PTR_ERR(op_data));

        op_data->op_bias |= MDS_SET_MEA;
        err = md_create(sbi->ll_md_exp, op_data, lump, sizeof(*lump), mode,
                        current->fsuid, current->fsgid,
                        cfs_curproc_cap_pack(), 0, &request);
        ll_finish_md_op_data(op_data);
        if (err)
                GOTO(err_exit, err);

        ll_update_times(request, dir);
err_exit:
        ptlrpc_req_finished(request);
        return err;
}

int ll_dir_setstripe(struct inode *inode, struct lov_user_md *lump,
                     int set_default)
{
        struct ll_sb_info *sbi = ll_i2sbi(inode);
        struct md_op_data *op_data;
        struct ptlrpc_request *req = NULL;
        int rc = 0;
        struct lustre_sb_info *lsi = s2lsi(inode->i_sb);
        struct obd_device *mgc = lsi->lsi_mgc;
        char *fsname = NULL, *param = NULL;
        int lum_size;

        if (lump != NULL) {
                /*
                 * This is coming from userspace, so should be in
                 * local endian.  But the MDS would like it in little
                 * endian, so we swab it before we send it.
                 */
                switch (lump->lmm_magic) {
                case LOV_USER_MAGIC_V1: {
                        if (lump->lmm_magic != cpu_to_le32(LOV_USER_MAGIC_V1))
                                lustre_swab_lov_user_md_v1(lump);
                        lum_size = sizeof(struct lov_user_md_v1);
                        break;
                }
                case LMV_USER_MAGIC: {
                        /* XXX: borrow lsm lock */
                        if (lump->lmm_magic != cpu_to_le32(LMV_USER_MAGIC))
                                lustre_swab_lmv_user_md((struct lmv_user_md *)lump);
                        lum_size = sizeof(struct lmv_user_md);
                        break;
                }
                case LOV_USER_MAGIC_V3: {
                        if (lump->lmm_magic != cpu_to_le32(LOV_USER_MAGIC_V3))
                                lustre_swab_lov_user_md_v3(
                                        (struct lov_user_md_v3 *)lump);
                        lum_size = sizeof(struct lov_user_md_v3);
                        break;
                }
        	default: {
                        CDEBUG(D_IOCTL, "bad userland LOV MAGIC:"
                                        " %#08x != %#08x nor %#08x\n",
                                        lump->lmm_magic, LOV_USER_MAGIC_V1,
                                        LOV_USER_MAGIC_V3);
                        RETURN(-EINVAL);
                }
               }
        } else {
                lum_size = sizeof(struct lov_user_md_v1);
        }

        op_data = ll_prep_md_op_data(NULL, inode, NULL, NULL, 0, 0,
                                     LUSTRE_OPC_ANY, NULL);
        if (IS_ERR(op_data))
                RETURN(PTR_ERR(op_data));

        if (lump->lmm_magic == cpu_to_le32(LMV_USER_MAGIC))
                op_data->op_bias |= MDS_SET_MEA;

        /* swabbing is done in lov_setstripe() on server side */
        rc = md_setattr(sbi->ll_md_exp, op_data, lump, lum_size,
                        NULL, 0, &req, NULL);
        ll_finish_md_op_data(op_data);
        if (rc) {
                if (rc != -EPERM && rc != -EACCES)
                        CERROR("mdc_setattr fails: rc = %d\n", rc);
        }

        ptlrpc_req_finished(req);

        /* In the following we use the fact that LOV_USER_MAGIC_V1 and
         LOV_USER_MAGIC_V3 have the same initial fields so we do not
         need the make the distiction between the 2 versions */
        if (set_default && mgc->u.cli.cl_mgc_mgsexp) {
                OBD_ALLOC(param, MGS_PARAM_MAXLEN);

                /* Get fsname and assume devname to be -MDT0000. */
                fsname = ll_get_fsname(inode);
                /* Set root stripesize */
                sprintf(param, "%s-MDT0000.lov.stripesize=%u", fsname,
                        lump ? le32_to_cpu(lump->lmm_stripe_size) : 0);
                rc = ll_send_mgc_param(mgc->u.cli.cl_mgc_mgsexp, param);
                if (rc)
                        goto end;

                /* Set root stripecount */
                sprintf(param, "%s-MDT0000.lov.stripecount=%hd", fsname,
                        lump ? le16_to_cpu(lump->lmm_stripe_count) : 0);
                rc = ll_send_mgc_param(mgc->u.cli.cl_mgc_mgsexp, param);
                if (rc)
                        goto end;

                /* Set root stripeoffset */
                sprintf(param, "%s-MDT0000.lov.stripeoffset=%hd", fsname,
                        lump ? le16_to_cpu(lump->lmm_stripe_offset) :
                        (typeof(lump->lmm_stripe_offset))(-1));
                rc = ll_send_mgc_param(mgc->u.cli.cl_mgc_mgsexp, param);
                if (rc)
                        goto end;
end:
                if (fsname)
                        OBD_FREE(fsname, MGS_PARAM_MAXLEN);
                if (param)
                        OBD_FREE(param, MGS_PARAM_MAXLEN);
        }
        return rc;
}

int ll_dir_getstripe(struct inode *inode, void **lmdp,
                     int *lmm_size, struct ptlrpc_request **request,
                     obd_valid valid)
{
        struct ll_sb_info *sbi = ll_i2sbi(inode);
        struct mdt_body   *body;
        void *lmd = NULL;
        struct ptlrpc_request *req = NULL;
        int rc = 0;
        int need_swab = (LOV_MAGIC != cpu_to_le32(LOV_MAGIC));
        int lmmsize;
        struct md_op_data *op_data;
        struct lov_mds_md *lmm;

        if (valid & OBD_MD_MEA)
                lmmsize = obd_size_diskmd(sbi->ll_md_exp, NULL);
        else if (valid & OBD_MD_DEFAULTMEA)
                lmmsize = sizeof(struct lmv_user_md);
        else
                rc = ll_get_max_mdsize(sbi, &lmmsize);
        if (rc)
                RETURN(rc);

        op_data = ll_prep_md_op_data(NULL, inode, NULL, NULL,
                                     0, lmmsize, LUSTRE_OPC_ANY,
                                     NULL);
        if (op_data == NULL)
                RETURN(-ENOMEM);

        op_data->op_valid = valid | OBD_MD_FLEASIZE | OBD_MD_FLDIREA;
        rc = md_getattr(sbi->ll_md_exp, op_data, &req);
        ll_finish_md_op_data(op_data);
        if (rc < 0) {
                CDEBUG(D_INFO, "md_getattr failed on inode "
                       "%lu/%u: rc %d\n", inode->i_ino,
                       inode->i_generation, rc);
                GOTO(out, rc);
        }

        body = req_capsule_server_get(&req->rq_pill, &RMF_MDT_BODY);
        LASSERT(body != NULL);

        if (!(body->valid & OBD_MD_FLDIREA) ||
            !(body->valid & valid)) {
                lmmsize = 0;
                GOTO(out, rc = -ENODATA);
        }

        if (body->valid & OBD_MD_DEFAULTMEA) {
                lmmsize = body->defaulteasize;
                lmd = req_capsule_server_get(&req->rq_pill, &RMF_MDT_DEFAULTMD);
        } else {
                lmmsize = body->eadatasize;
                lmd = req_capsule_server_sized_get(&req->rq_pill,
                                           &RMF_MDT_MD, lmmsize);
        }

        LASSERT(lmd != NULL && lmmsize > 0);
        valid = body->valid;
        need_swab = (LOV_MAGIC != cpu_to_le32(LOV_MAGIC));

        /*
         * This is coming from the MDS, so is probably in
         * little endian.  We convert it to host endian before
         * passing it to userspace.
         */
        LASSERT(valid & OBD_MD_FLDIREA);
        lmm = lmd;
        /* We don't swab objects for directories */
        switch (le32_to_cpu(lmm->lmm_magic)) {
        case LOV_MAGIC_V1:
                if (need_swab)
                        lustre_swab_lov_user_md_v1(
                                        (struct lov_user_md_v1 *)lmm);
                break;
        case LOV_MAGIC_V3:
                if (need_swab)
                        lustre_swab_lov_user_md_v3(
                                        (struct lov_user_md_v3 *)lmm);
                break;
        case LMV_USER_MAGIC:
                if (need_swab)
                        lustre_swab_lmv_user_md(
                                        (struct lmv_user_md*)lmm);
                break;
        case LMV_MAGIC_V1:
                if (need_swab) {
                        struct lmv_mds_md *lmv = lmd;
                        int i;
                        lustre_swab_lmv_mds_md(lmv);
                        CDEBUG(D_INODE, "metadata magic %x count %u\n",
                                lmv->lmv_magic, lmv->lmv_count);
                        for (i = 0; i < lmv->lmv_count; i++)
                                lustre_swab_lu_fid(&lmv->lmv_ids[i]);
                }
                break;
        default:
                CERROR("unknown magic: %lX\n",
                       (unsigned long)lmm->lmm_magic);
                rc = -EPROTO;
        }
out:
        *lmdp = lmd;
        *lmm_size = lmmsize;
        *request = req;
        return rc;
}

/*
 *  Get MDT index for the inode.
 */
int ll_get_mdt_idx(struct inode *inode)
{
        struct ll_sb_info *sbi = ll_i2sbi(inode);
        struct md_op_data *op_data;
        int rc, mdtidx;
        ENTRY;

        op_data = ll_prep_md_op_data(NULL, inode, NULL, NULL, 0,
                                     0, LUSTRE_OPC_ANY, NULL);
        if (op_data == NULL)
                RETURN(-ENOMEM);

        op_data->op_valid |= OBD_MD_MDTIDX;
        rc = md_getattr(sbi->ll_md_exp, op_data, NULL);
        mdtidx = op_data->op_mds;
        ll_finish_md_op_data(op_data);
        if (rc < 0) {
                CDEBUG(D_INFO, "md_getattr_name: %d\n", rc);
                RETURN(rc);
        }
        return mdtidx;
}

static int copy_and_ioctl(int cmd, struct obd_export *exp, void *data, int len)
{
        void *ptr;
        int rc;

        OBD_ALLOC(ptr, len);
        if (ptr == NULL)
                return -ENOMEM;
        if (cfs_copy_from_user(ptr, data, len)) {
                OBD_FREE(ptr, len);
                return -EFAULT;
        }
        rc = obd_iocontrol(cmd, exp, len, data, NULL);
        OBD_FREE(ptr, len);
        return rc;
}

static int ll_dir_ioctl(struct inode *inode, struct file *file,
                        unsigned int cmd, unsigned long arg)
{
        struct ll_sb_info *sbi = ll_i2sbi(inode);
        struct obd_ioctl_data *data;
        int rc = 0;
        ENTRY;

        CDEBUG(D_VFSTRACE, "VFS Op:inode=%lu/%u(%p), cmd=%#x\n",
               inode->i_ino, inode->i_generation, inode, cmd);

        /* asm-ppc{,64} declares TCGETS, et. al. as type 't' not 'T' */
        if (_IOC_TYPE(cmd) == 'T' || _IOC_TYPE(cmd) == 't') /* tty ioctls */
                return -ENOTTY;

        ll_stats_ops_tally(ll_i2sbi(inode), LPROC_LL_IOCTL, 1);
        switch(cmd) {
        case FSFILT_IOC_GETFLAGS:
        case FSFILT_IOC_SETFLAGS:
                RETURN(ll_iocontrol(inode, file, cmd, arg));
        case FSFILT_IOC_GETVERSION_OLD:
        case FSFILT_IOC_GETVERSION:
                RETURN(put_user(inode->i_generation, (int *)arg));
        /* We need to special case any other ioctls we want to handle,
         * to send them to the MDS/OST as appropriate and to properly
         * network encode the arg field.
        case FSFILT_IOC_SETVERSION_OLD:
        case FSFILT_IOC_SETVERSION:
        */
        case LL_IOC_GET_MDTIDX: {
                int mdtidx;

                mdtidx = ll_get_mdt_idx(inode);
                if (mdtidx < 0)
                        RETURN(mdtidx);

                if (put_user((int)mdtidx, (int*)arg))
                        RETURN(-EFAULT);

                return 0;
        }
        case IOC_MDC_LOOKUP: {
                struct ptlrpc_request *request = NULL;
                int namelen, len = 0;
                char *buf = NULL;
                char *filename;
                struct md_op_data *op_data;

                rc = obd_ioctl_getdata(&buf, &len, (void *)arg);
                if (rc)
                        RETURN(rc);
                data = (void *)buf;

                filename = data->ioc_inlbuf1;
                namelen = strlen(filename);

                if (namelen < 1) {
                        CDEBUG(D_INFO, "IOC_MDC_LOOKUP missing filename\n");
                        GOTO(out_free, rc = -EINVAL);
                }

                op_data = ll_prep_md_op_data(NULL, inode, NULL, filename, namelen,
                                             0, LUSTRE_OPC_ANY, NULL);
                if (op_data == NULL)
                        GOTO(out_free, rc = -ENOMEM);

                op_data->op_valid = OBD_MD_FLID;
                rc = md_getattr_name(sbi->ll_md_exp, op_data, &request);
                ll_finish_md_op_data(op_data);
                if (rc < 0) {
                        CDEBUG(D_INFO, "md_getattr_name: %d\n", rc);
                        GOTO(out_free, rc);
                }
                ptlrpc_req_finished(request);
                EXIT;
out_free:
                obd_ioctl_freedata(buf, len);
                return rc;
        }
        case LL_IOC_LMV_SETSTRIPE: {
                struct lmv_user_md  *lum;
                char                *buf = NULL;
                char                *filename;
                int                 namelen = 0;
                int                 lumlen = 0;
                int                 len;
                int                 rc;

                rc = obd_ioctl_getdata(&buf, &len, (void *)arg);
                if (rc)
                        RETURN(rc);

                data = (void*)buf;
                if (data->ioc_inlbuf1 == NULL || data->ioc_inlbuf2 == NULL ||
                    data->ioc_inllen1 == 0 || data->ioc_inllen2 == 0)
                        GOTO(lmv_out_free, rc = -EINVAL);

                filename = data->ioc_inlbuf1;
                namelen = data->ioc_inllen1;

                if (namelen < 1) {
                        CDEBUG(D_INFO, "IOC_MDC_LOOKUP missing filename\n");
                        GOTO(lmv_out_free, rc = -EINVAL);
                }
                lum = (struct lmv_user_md *)data->ioc_inlbuf2;
                lumlen = data->ioc_inllen2;

                if (lum->lum_magic != LMV_USER_MAGIC ||
                    lumlen != sizeof(struct lmv_user_md)) {
                        CERROR("wrong lum magic %x or size %d \n",
                               lum->lum_magic, lumlen);
                        GOTO(lmv_out_free, rc = -EFAULT);
                }
                if (lum->lum_type == LMV_DEFAULT_TYPE)
                        /**
                         * ll_dir_setstripe will be used to set default dir/file stripe
                         * for the dir. mdc_setattr--->mdt_attr_set
                         */
                        rc = ll_dir_setstripe(inode, (struct lov_user_md*)lum, 0);
                else
                        /**
                         * ll_dir_setdirstripe will be used to set dir stripe
                         *  mdc_create--->mdt_reint_create (with dirstripe)
                         */
                        rc = ll_dir_setdirstripe(inode, lum, filename);
lmv_out_free:
                obd_ioctl_freedata(buf, len);
                RETURN(rc);

        }
        case LL_IOC_LOV_SETSTRIPE: {
                struct lov_user_md_v3 lumv3;
                struct lov_user_md_v1 *lumv1 = (struct lov_user_md_v1 *)&lumv3;
                struct lov_user_md_v1 *lumv1p = (struct lov_user_md_v1 *)arg;
                struct lov_user_md_v3 *lumv3p = (struct lov_user_md_v3 *)arg;

                int set_default = 0;

                LASSERT(sizeof(lumv3) == sizeof(*lumv3p));
                LASSERT(sizeof(lumv3.lmm_objects[0]) ==
                        sizeof(lumv3p->lmm_objects[0]));
                /* first try with v1 which is smaller than v3 */
                if (cfs_copy_from_user(lumv1, lumv1p, sizeof(*lumv1)))
                        RETURN(-EFAULT);

                if ((lumv1->lmm_magic == LOV_USER_MAGIC_V3) ||
                    (lumv1->lmm_magic == LMV_USER_MAGIC)) {
                        if (cfs_copy_from_user(&lumv3, lumv3p, sizeof(lumv3)))
                                RETURN(-EFAULT);
                }

                if (inode->i_sb->s_root == file->f_dentry)
                        set_default = 1;

                /* in v1 and v3 cases lumv1 points to data */
                rc = ll_dir_setstripe(inode, lumv1, set_default);

                RETURN(rc);
        }
        case LL_IOC_LMV_GETSTRIPE: {
                struct lmv_user_md *lmvp = (struct lmv_user_md *)arg;
                struct lmv_user_md lum;
                struct ptlrpc_request *request = NULL;
                struct mdt_body *body;
                struct lmv_mds_md *lmm = NULL;
                int lmmsize;
                int rc;
                obd_valid valid = OBD_MD_FLDIREA;

                if (copy_from_user(&lum, lmvp, sizeof(*lmvp)))
                        RETURN(-EFAULT);

                if (lum.lum_magic != LMV_USER_MAGIC) {
                        RETURN(-EINVAL);
                }

                if (lum.lum_type == LMV_STRIPE_TYPE)
                        valid |= OBD_MD_MEA;
                else
                        valid |= OBD_MD_DEFAULTMEA;

                rc = ll_dir_getstripe(inode, (void**)&lmm, &lmmsize,
                                      &request, valid);
                if (request) {
                        body = req_capsule_server_get(&request->rq_pill,
                                                      &RMF_MDT_BODY);
                        LASSERT(body != NULL);
                }

                if (valid & OBD_MD_DEFAULTMEA) { /* default dirstripe EA */
                        struct lmv_user_md *lump = (struct lmv_user_md *)lmm;
                        if (rc != 0)
                                GOTO(finish_req, rc);
                        if (lump->lum_type != LMV_DEFAULT_TYPE){
                                CERROR("wrong lum type lump %x type %d \n",
                                        lump->lum_magic, lump->lum_type);
                                GOTO(finish_req, rc = -EINVAL);
                        }
                        if (copy_to_user((void*)arg, lump, sizeof(*lump)))
                                GOTO(finish_req, rc = -EFAULT);

                } else {                        /* dirstripe EA */
                        struct lmv_user_md *tmp = NULL;
                        int copy_size;
                        mdsno_t mdsno;
                        int alloc_lmm = 0;
                        int i;

                        if (rc != 0 && rc != -ENODATA)
                                GOTO(finish_req, rc);

                        if (rc == -ENODATA) {
                                LASSERT(lmm == NULL);
                                lmv_alloc_md(&lmm, 1);
                                if (lmm == NULL)
                                        GOTO(free_lmv, rc = -ENOMEM);
                                lmm->lmv_magic = LMV_MAGIC_V1;
                                lmm->lmv_ids[0] = *ll_inode2fid(inode);
                                alloc_lmm = 1;
                                rc = 0;
                        }

                        LASSERT(lmm != NULL);

                        copy_size = sizeof(*lmm) + lmm->lmv_count *
                                    sizeof(struct lmv_user_mds_data);
                        OBD_ALLOC(tmp, copy_size);
                        if (tmp == NULL)
                                GOTO(free_lmv, rc = -ENOMEM);

                        LASSERT(sizeof(*tmp) == sizeof(*lmm));
                        memcpy(tmp, lmm, sizeof(*lmm));
                        tmp->lum_magic = LMV_USER_MAGIC;
                        tmp->lum_type = lum.lum_type;
                        for (i = 0; i < lmm->lmv_count; i++) {
                                rc = md_notify_object(sbi->ll_md_exp,
                                                      &lmm->lmv_ids[i],
                                                      MD_OBJECT_LOCATE,
                                                      &mdsno);
                                if (rc < 0)
                                        break;
                                tmp->lum_objects[i].lum_mds = (__u32)mdsno;
                                tmp->lum_objects[i].lum_fid = lmm->lmv_ids[i];
                        }
                        if (!rc && copy_to_user((void*)arg, tmp, copy_size))
                                GOTO(free_lmv, rc = -EFAULT);
               free_lmv:
                        if (tmp)
                                OBD_FREE(tmp, copy_size);
                        if (alloc_lmm == 1 && lmm != NULL)
                                lmv_free_md(lmm);
                }
        finish_req:
                ptlrpc_req_finished(request);
                return rc;
        }
        case LL_IOC_OBD_STATFS:
                RETURN(ll_obd_statfs(inode, (void *)arg));
        case LL_IOC_LOV_GETSTRIPE:
        case LL_IOC_MDC_GETINFO:
        case IOC_MDC_GETFILEINFO:
        case IOC_MDC_GETFILESTRIPE: {
                struct ptlrpc_request *request = NULL;
                struct lov_user_md *lump;
                struct lov_mds_md *lmm = NULL;
                struct mdt_body *body;
                char *filename = NULL;
                int lmmsize;

                if (cmd == IOC_MDC_GETFILEINFO ||
                    cmd == IOC_MDC_GETFILESTRIPE) {
                        filename = getname((const char *)arg);
                        if (IS_ERR(filename))
                                RETURN(PTR_ERR(filename));

                        rc = ll_lov_getstripe_ea_info(inode, filename, &lmm,
                                                      &lmmsize, &request);
                } else {
                        rc = ll_dir_getstripe(inode, (void **)&lmm, &lmmsize,
                                              &request, OBD_MD_FLDIREA);
                }

                if (request) {
                        body = req_capsule_server_get(&request->rq_pill,
                                                      &RMF_MDT_BODY);
                        LASSERT(body != NULL);
                } else {
                        GOTO(out_req, rc);
                }

                if (rc < 0) {
                        if (rc == -ENODATA && (cmd == IOC_MDC_GETFILEINFO ||
                                               cmd == LL_IOC_MDC_GETINFO))
                                GOTO(skip_lmm, rc = 0);
                        else
                                GOTO(out_req, rc);
                }

                if (cmd == IOC_MDC_GETFILESTRIPE ||
                    cmd == LL_IOC_LOV_GETSTRIPE) {
                        lump = (struct lov_user_md *)arg;
                } else {
                        struct lov_user_mds_data *lmdp;
                        lmdp = (struct lov_user_mds_data *)arg;
                        lump = &lmdp->lmd_lmm;
                }
                if (cfs_copy_to_user(lump, lmm, lmmsize))
                        GOTO(out_req, rc = -EFAULT);
        skip_lmm:
                if (cmd == IOC_MDC_GETFILEINFO || cmd == LL_IOC_MDC_GETINFO) {
                        struct lov_user_mds_data *lmdp;
                        lstat_t st = { 0 };

                        st.st_dev     = inode->i_sb->s_dev;
                        st.st_mode    = body->mode;
                        st.st_nlink   = body->nlink;
                        st.st_uid     = body->uid;
                        st.st_gid     = body->gid;
                        st.st_rdev    = body->rdev;
                        st.st_size    = body->size;
                        st.st_blksize = CFS_PAGE_SIZE;
                        st.st_blocks  = body->blocks;
                        st.st_atime   = body->atime;
                        st.st_mtime   = body->mtime;
                        st.st_ctime   = body->ctime;
                        st.st_ino     = inode->i_ino;

                        lmdp = (struct lov_user_mds_data *)arg;
                        if (cfs_copy_to_user(&lmdp->lmd_st, &st, sizeof(st)))
                                GOTO(out_req, rc = -EFAULT);
                }

                EXIT;
        out_req:
                ptlrpc_req_finished(request);
                if (filename)
                        putname(filename);
                return rc;
        }
        case IOC_LOV_GETINFO: {
                struct lov_user_mds_data *lumd;
                struct lov_stripe_md *lsm;
                struct lov_user_md *lum;
                struct lov_mds_md *lmm;
                int lmmsize;
                lstat_t st;

                lumd = (struct lov_user_mds_data *)arg;
                lum = &lumd->lmd_lmm;

                rc = ll_get_max_mdsize(sbi, &lmmsize);
                if (rc)
                        RETURN(rc);

                OBD_ALLOC(lmm, lmmsize);
                if (cfs_copy_from_user(lmm, lum, lmmsize))
                        GOTO(free_lmm, rc = -EFAULT);

                switch (lmm->lmm_magic) {
                case LOV_USER_MAGIC_V1:
                        if (LOV_USER_MAGIC_V1 == cpu_to_le32(LOV_USER_MAGIC_V1))
                                break;
                        /* swab objects first so that stripes num will be sane */
                        lustre_swab_lov_user_md_objects(
                                ((struct lov_user_md_v1 *)lmm)->lmm_objects,
                                ((struct lov_user_md_v1 *)lmm)->lmm_stripe_count);
                        lustre_swab_lov_user_md_v1((struct lov_user_md_v1 *)lmm);
                        break;
                case LOV_USER_MAGIC_V3:
                        if (LOV_USER_MAGIC_V3 == cpu_to_le32(LOV_USER_MAGIC_V3))
                                break;
                        /* swab objects first so that stripes num will be sane */
                        lustre_swab_lov_user_md_objects(
                                ((struct lov_user_md_v3 *)lmm)->lmm_objects,
                                ((struct lov_user_md_v3 *)lmm)->lmm_stripe_count);
                        lustre_swab_lov_user_md_v3((struct lov_user_md_v3 *)lmm);
                        break;
                default:
                        GOTO(free_lmm, rc = -EINVAL);
                }

                rc = obd_unpackmd(sbi->ll_dt_exp, &lsm, lmm, lmmsize);
                if (rc < 0)
                        GOTO(free_lmm, rc = -ENOMEM);

                /* Perform glimpse_size operation. */
                memset(&st, 0, sizeof(st));

                rc = ll_glimpse_ioctl(sbi, lsm, &st);
                if (rc)
                        GOTO(free_lsm, rc);

                if (cfs_copy_to_user(&lumd->lmd_st, &st, sizeof(st)))
                        GOTO(free_lsm, rc = -EFAULT);

                EXIT;
        free_lsm:
                obd_free_memmd(sbi->ll_dt_exp, &lsm);
        free_lmm:
                OBD_FREE(lmm, lmmsize);
                return rc;
        }
        case OBD_IOC_LLOG_CATINFO: {
                struct ptlrpc_request *req = NULL;
                char                  *buf = NULL;
                char                  *str;
                int                    len = 0;

                rc = obd_ioctl_getdata(&buf, &len, (void *)arg);
                if (rc)
                        RETURN(rc);
                data = (void *)buf;

                if (!data->ioc_inlbuf1) {
                        obd_ioctl_freedata(buf, len);
                        RETURN(-EINVAL);
                }

                req = ptlrpc_request_alloc(sbi2mdc(sbi)->cl_import,
                                           &RQF_LLOG_CATINFO);
                if (req == NULL)
                        GOTO(out_catinfo, rc = -ENOMEM);

                req_capsule_set_size(&req->rq_pill, &RMF_NAME, RCL_CLIENT,
                                     data->ioc_inllen1);
                req_capsule_set_size(&req->rq_pill, &RMF_STRING, RCL_CLIENT,
                                     data->ioc_inllen2);

                rc = ptlrpc_request_pack(req, LUSTRE_LOG_VERSION, LLOG_CATINFO);
                if (rc) {
                        ptlrpc_request_free(req);
                        GOTO(out_catinfo, rc);
                }

                str = req_capsule_client_get(&req->rq_pill, &RMF_NAME);
                memcpy(str, data->ioc_inlbuf1, data->ioc_inllen1);
                if (data->ioc_inllen2) {
                        str = req_capsule_client_get(&req->rq_pill,
                                                     &RMF_STRING);
                        memcpy(str, data->ioc_inlbuf2, data->ioc_inllen2);
                }

                req_capsule_set_size(&req->rq_pill, &RMF_STRING, RCL_SERVER,
                                     data->ioc_plen1);
                ptlrpc_request_set_replen(req);

                rc = ptlrpc_queue_wait(req);
                if (!rc) {
                        str = req_capsule_server_get(&req->rq_pill,
                                                     &RMF_STRING);
                        if (cfs_copy_to_user(data->ioc_pbuf1, str,
                                             data->ioc_plen1))
                                rc = -EFAULT;
                }
                ptlrpc_req_finished(req);
        out_catinfo:
                obd_ioctl_freedata(buf, len);
                RETURN(rc);
        }
        case OBD_IOC_QUOTACHECK: {
                struct obd_quotactl *oqctl;
                int error = 0;

                if (!cfs_capable(CFS_CAP_SYS_ADMIN) ||
                    sbi->ll_flags & LL_SBI_RMT_CLIENT)
                        RETURN(-EPERM);

                OBD_ALLOC_PTR(oqctl);
                if (!oqctl)
                        RETURN(-ENOMEM);
                oqctl->qc_type = arg;
                rc = obd_quotacheck(sbi->ll_md_exp, oqctl);
                if (rc < 0) {
                        CDEBUG(D_INFO, "md_quotacheck failed: rc %d\n", rc);
                        error = rc;
                }

                rc = obd_quotacheck(sbi->ll_dt_exp, oqctl);
                if (rc < 0)
                        CDEBUG(D_INFO, "obd_quotacheck failed: rc %d\n", rc);

                OBD_FREE_PTR(oqctl);
                return error ?: rc;
        }
        case OBD_IOC_POLL_QUOTACHECK: {
                struct if_quotacheck *check;

                if (!cfs_capable(CFS_CAP_SYS_ADMIN) ||
                    sbi->ll_flags & LL_SBI_RMT_CLIENT)
                        RETURN(-EPERM);

                OBD_ALLOC_PTR(check);
                if (!check)
                        RETURN(-ENOMEM);

                rc = obd_iocontrol(cmd, sbi->ll_md_exp, 0, (void *)check,
                                   NULL);
                if (rc) {
                        CDEBUG(D_QUOTA, "mdc ioctl %d failed: %d\n", cmd, rc);
                        if (cfs_copy_to_user((void *)arg, check,
                                             sizeof(*check)))
                                rc = -EFAULT;
                        GOTO(out_poll, rc);
                }

                rc = obd_iocontrol(cmd, sbi->ll_dt_exp, 0, (void *)check,
                                   NULL);
                if (rc) {
                        CDEBUG(D_QUOTA, "osc ioctl %d failed: %d\n", cmd, rc);
                        if (cfs_copy_to_user((void *)arg, check,
                                             sizeof(*check)))
                                rc = -EFAULT;
                        GOTO(out_poll, rc);
                }
        out_poll:
                OBD_FREE_PTR(check);
                RETURN(rc);
        }
        case OBD_IOC_QUOTACTL: {
                struct if_quotactl *qctl;
                int cmd, type, id, valid;

                OBD_ALLOC_PTR(qctl);
                if (!qctl)
                        RETURN(-ENOMEM);

                if (cfs_copy_from_user(qctl, (void *)arg, sizeof(*qctl)))
                        GOTO(out_quotactl, rc = -EFAULT);

                cmd = qctl->qc_cmd;
                type = qctl->qc_type;
                id = qctl->qc_id;
                valid = qctl->qc_valid;

                switch (cmd) {
                case LUSTRE_Q_INVALIDATE:
                case LUSTRE_Q_FINVALIDATE:
                case Q_QUOTAON:
                case Q_QUOTAOFF:
                case Q_SETQUOTA:
                case Q_SETINFO:
                        if (!cfs_capable(CFS_CAP_SYS_ADMIN) ||
                            sbi->ll_flags & LL_SBI_RMT_CLIENT)
                                GOTO(out_quotactl, rc = -EPERM);
                        break;
                case Q_GETQUOTA:
                        if (((type == USRQUOTA && cfs_curproc_euid() != id) ||
                             (type == GRPQUOTA && !in_egroup_p(id))) &&
                            (!cfs_capable(CFS_CAP_SYS_ADMIN) ||
                             sbi->ll_flags & LL_SBI_RMT_CLIENT))
                                GOTO(out_quotactl, rc = -EPERM);
                        break;
                case Q_GETINFO:
                        break;
                default:
                        CERROR("unsupported quotactl op: %#x\n", cmd);
                        GOTO(out_quotactl, rc = -ENOTTY);
                }

                if (valid != QC_GENERAL) {
                        if (sbi->ll_flags & LL_SBI_RMT_CLIENT)
                                GOTO(out_quotactl, rc = -EOPNOTSUPP);

                        if (cmd == Q_GETINFO)
                                qctl->qc_cmd = Q_GETOINFO;
                        else if (cmd == Q_GETQUOTA)
                                qctl->qc_cmd = Q_GETOQUOTA;
                        else
                                GOTO(out_quotactl, rc = -EINVAL);

                        switch (valid) {
                        case QC_MDTIDX:
                                rc = obd_iocontrol(OBD_IOC_QUOTACTL,
                                                   sbi->ll_md_exp,
                                                   sizeof(*qctl), qctl, NULL);
                                break;
                        case QC_OSTIDX:
                                rc = obd_iocontrol(OBD_IOC_QUOTACTL,
                                                   sbi->ll_dt_exp,
                                                   sizeof(*qctl), qctl, NULL);
                                break;
                        case QC_UUID:
                                rc = obd_iocontrol(OBD_IOC_QUOTACTL,
                                                   sbi->ll_md_exp,
                                                   sizeof(*qctl), qctl, NULL);
                                if (rc == -EAGAIN)
                                        rc = obd_iocontrol(OBD_IOC_QUOTACTL,
                                                           sbi->ll_dt_exp,
                                                           sizeof(*qctl), qctl,
                                                           NULL);
                                break;
                        default:
                                rc = -EINVAL;
                                break;
                        }

                        if (rc)
                                GOTO(out_quotactl, rc);
                        else
                                qctl->qc_cmd = cmd;
                } else {
                        struct obd_quotactl *oqctl;

                        OBD_ALLOC_PTR(oqctl);
                        if (!oqctl)
                                GOTO(out_quotactl, rc = -ENOMEM);

                        QCTL_COPY(oqctl, qctl);
                        rc = obd_quotactl(sbi->ll_md_exp, oqctl);
                        if (rc) {
                                if (rc != -EALREADY && cmd == Q_QUOTAON) {
                                        oqctl->qc_cmd = Q_QUOTAOFF;
                                        obd_quotactl(sbi->ll_md_exp, oqctl);
                                }
                                OBD_FREE_PTR(oqctl);
                                GOTO(out_quotactl, rc);
                        } else {
                                QCTL_COPY(qctl, oqctl);
                                OBD_FREE_PTR(oqctl);
                        }
                }

                if (cfs_copy_to_user((void *)arg, qctl, sizeof(*qctl)))
                        rc = -EFAULT;

        out_quotactl:
                OBD_FREE_PTR(qctl);
                RETURN(rc);
        }
        case OBD_IOC_GETNAME: {
                struct obd_device *obd = class_exp2obd(sbi->ll_dt_exp);
                if (!obd)
                        RETURN(-EFAULT);
                if (cfs_copy_to_user((void *)arg, obd->obd_name,
                                     strlen(obd->obd_name) + 1))
                        RETURN (-EFAULT);
                RETURN(0);
        }
        case OBD_IOC_GETMDNAME: {
                struct obd_device *obd = class_exp2obd(sbi->ll_md_exp);
                if (!obd)
                        RETURN(-EFAULT);
                if (copy_to_user((void *)arg, obd->obd_name,
                                strlen(obd->obd_name) + 1))
                        RETURN (-EFAULT);
                RETURN(0);
        }
        case LL_IOC_FLUSHCTX:
                RETURN(ll_flush_ctx(inode));
#ifdef CONFIG_FS_POSIX_ACL
        case LL_IOC_RMTACL: {
            if (sbi->ll_flags & LL_SBI_RMT_CLIENT &&
                inode == inode->i_sb->s_root->d_inode) {
                struct ll_file_data *fd = LUSTRE_FPRIVATE(file);

                LASSERT(fd != NULL);
                rc = rct_add(&sbi->ll_rct, cfs_curproc_pid(), arg);
                if (!rc)
                        fd->fd_flags |= LL_FILE_RMTACL;
                RETURN(rc);
            } else
                RETURN(0);
        }
#endif
        case LL_IOC_GETOBDCOUNT: {
                int count;

                if (cfs_copy_from_user(&count, (int *)arg, sizeof(int)))
                        RETURN(-EFAULT);

                if (!count) {
                        /* get ost count */
                        struct lov_obd *lov = &sbi->ll_dt_exp->exp_obd->u.lov;
                        count = lov->desc.ld_tgt_count;
                } else {
                        /* get mdt count */
                        struct lmv_obd *lmv = &sbi->ll_md_exp->exp_obd->u.lmv;
                        count = lmv->desc.ld_tgt_count;
                }

                if (cfs_copy_to_user((int *)arg, &count, sizeof(int)))
                        RETURN(-EFAULT);

                RETURN(0);
        }
        case LL_IOC_PATH2FID:
                if (cfs_copy_to_user((void *)arg, ll_inode2fid(inode),
                                     sizeof(struct lu_fid)))
                        RETURN(-EFAULT);
                RETURN(0);
        case LL_IOC_GET_CONNECT_FLAGS: {
                RETURN(obd_iocontrol(cmd, sbi->ll_md_exp, 0, NULL, (void*)arg));
        }
        case OBD_IOC_CHANGELOG_SEND:
        case OBD_IOC_CHANGELOG_CLEAR:
                rc = copy_and_ioctl(cmd, sbi->ll_md_exp, (void *)arg,
                                    sizeof(struct ioc_changelog));
                RETURN(rc);
        case OBD_IOC_FID2PATH:
                RETURN(ll_fid2path(ll_i2mdexp(inode), (void *)arg));
        case LL_IOC_HSM_CT_START:
                rc = copy_and_ioctl(cmd, sbi->ll_md_exp, (void *)arg,
                                    sizeof(struct lustre_kernelcomm));
                RETURN(rc);

        default:
                RETURN(obd_iocontrol(cmd, sbi->ll_dt_exp,0,NULL,(void *)arg));
        }
}

int ll_dir_open(struct inode *inode, struct file *file)
{
        ENTRY;
        RETURN(ll_file_open(inode, file));
}

int ll_dir_release(struct inode *inode, struct file *file)
{
        ENTRY;
        RETURN(ll_file_release(inode, file));
}

struct file_operations ll_dir_operations = {
        .open     = ll_dir_open,
        .release  = ll_dir_release,
        .read     = generic_read_dir,
        .readdir  = ll_readdir,
        .ioctl    = ll_dir_ioctl
};
