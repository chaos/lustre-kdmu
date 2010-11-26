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
 * Author: Nathan Rutman <nathan.rutman@sun.com>
 *
 * Kernel <-> userspace communication routines.
 * Using pipes for all arches.
 */

#define DEBUG_SUBSYSTEM S_CLASS
#define D_KUC D_OTHER

#include <libcfs/libcfs.h>

#ifdef LUSTRE_UTILS
/* This is the userspace side. */

/** Start the userspace side of a KUC pipe.
 * @param link Private descriptor for pipe/socket.
 * @param groups KUC broadcast group to listen to
 *          (can be null for unicast to this pid)
 */
int libcfs_ukuc_start(lustre_kernelcomm *link, int group)
{
        int pfd[2];

        if (pipe(pfd) < 0)
                return -errno;

        memset(link, 0, sizeof(*link));
        link->lk_rfd = pfd[0];
        link->lk_wfd = pfd[1];
        link->lk_group = group;
        link->lk_uid = getpid();
        return 0;
}

int libcfs_ukuc_stop(lustre_kernelcomm *link)
{
        if (link->lk_wfd > 0)
                close(link->lk_wfd);
        return close(link->lk_rfd);
}

#define lhsz sizeof(*kuch)

/** Read a message from the link.
 * Allocates memory, returns handle
 *
 * @param link Private descriptor for pipe/socket.
 * @param buf Buffer to read into, must include size for kuc_hdr
 * @param maxsize Maximum message size allowed
 * @param transport Only listen to messages on this transport
 *      (and the generic transport)
 */
int libcfs_ukuc_msg_get(lustre_kernelcomm *link, char *buf, int maxsize,
                        int transport)
{
        struct kuc_hdr *kuch;
        int rc = 0;

        memset(buf, 0, maxsize);

        CDEBUG(D_KUC, "Waiting for message from kernel on fd %d\n",
               link->lk_rfd);

        while (1) {
                /* Read header first to get message size */
                rc = read(link->lk_rfd, buf, lhsz);
                if (rc <= 0) {
                        rc = -errno;
                        break;
                }
                kuch = (struct kuc_hdr *)buf;

                CDEBUG(D_KUC, "Received message mg=%x t=%d m=%d l=%d\n",
                       kuch->kuc_magic, kuch->kuc_transport, kuch->kuc_msgtype,
                       kuch->kuc_msglen);

                if (kuch->kuc_magic != KUC_MAGIC) {
                        CERROR("bad message magic %x != %x\n",
                               kuch->kuc_magic, KUC_MAGIC);
                        rc = -EPROTO;
                        break;
                }

                if (kuch->kuc_msglen > maxsize) {
                        rc = -EMSGSIZE;
                        break;
                }

                /* Read payload */
                rc = read(link->lk_rfd, buf + lhsz, kuch->kuc_msglen - lhsz);
                if (rc < 0) {
                        rc = -errno;
                        break;
                }
                if (rc < (kuch->kuc_msglen - lhsz)) {
                        CERROR("short read: got %d of %d bytes\n",
                               rc, kuch->kuc_msglen);
                        rc = -EPROTO;
                        break;
                }

                if (kuch->kuc_transport == transport ||
                    kuch->kuc_transport == KUC_TRANSPORT_GENERIC) {
                        return 0;
                }
                /* Drop messages for other transports */
        }
        return rc;
}

#else /* LUSTRE_UTILS */
/* This is the kernel side (liblustre as well). */

/**
 * libcfs_kkuc_msg_put - send an message from kernel to userspace
 * @param fp to send the message to
 * @param payload Payload data.  First field of payload is always
 *   struct kuc_hdr
 */
int libcfs_kkuc_msg_put(cfs_file_t *filp, void *payload)
{
        struct kuc_hdr *kuch = (struct kuc_hdr *)payload;
        int rc = -ENOSYS;

        if (filp == NULL || IS_ERR(filp))
                return -EBADF;

        if (kuch->kuc_magic != KUC_MAGIC) {
                CERROR("KernelComm: bad magic %x\n", kuch->kuc_magic);
                return -ENOSYS;
        }

#ifdef __KERNEL__
        {
                loff_t offset = 0;
                rc = cfs_user_write(filp, (char *)payload, kuch->kuc_msglen,
                                    &offset);
        }
#endif

        if (rc < 0)
                CWARN("message send failed (%d)\n", rc);
        else
                CDEBUG(D_KUC, "Sent message rc=%d, fp=%p\n", rc, filp);

        return rc;
}
CFS_EXPORT_SYMBOL(libcfs_kkuc_msg_put);

/* Broadcast groups are global across all mounted filesystems;
 * i.e. registering for a group on 1 fs will get messages for that
 * group from any fs */
/** A single group reigstration has a uid and a file pointer */
struct kkuc_reg {
        cfs_list_t  kr_chain;
        int         kr_uid;
        cfs_file_t *kr_fp;
        __u32       kr_data;
};
static cfs_list_t kkuc_groups[KUC_GRP_MAX+1] = {};
/* Protect message sending against remove and adds */
static CFS_DECLARE_RWSEM(kg_sem);

/** Add a receiver to a broadcast group
 * @param filp pipe to write into
 * @param uid identidier for this receiver
 * @param group group number
 */
int libcfs_kkuc_group_add(cfs_file_t *filp, int uid, int group, __u32 data)
{
        struct kkuc_reg *reg;

        if (group > KUC_GRP_MAX) {
                CDEBUG(D_WARNING, "Kernelcomm: bad group %d\n", group);
                return -EINVAL;
        }

        /* fput in group_rem */
        if (filp == NULL)
                return -EBADF;

        /* freed in group_rem */
        reg = cfs_alloc(sizeof(*reg), 0);
        if (reg == NULL)
                return -ENOMEM;

        reg->kr_fp = filp;
        reg->kr_uid = uid;
        reg->kr_data = data;

        cfs_down_write(&kg_sem);
        if (kkuc_groups[group].next == NULL)
                CFS_INIT_LIST_HEAD(&kkuc_groups[group]);
        cfs_list_add(&reg->kr_chain, &kkuc_groups[group]);
        cfs_up_write(&kg_sem);

        CDEBUG(D_KUC, "Added uid=%d fp=%p to group %d\n", uid, filp, group);

        return 0;
}
CFS_EXPORT_SYMBOL(libcfs_kkuc_group_add);

int libcfs_kkuc_group_rem(int uid, int group)
{
        struct kkuc_reg *reg, *next;
        ENTRY;

        if (kkuc_groups[group].next == NULL)
                RETURN(0);

        if (uid == 0) {
                /* Broadcast a shutdown message */
                struct kuc_hdr lh;

                lh.kuc_magic = KUC_MAGIC;
                lh.kuc_transport = KUC_TRANSPORT_GENERIC;
                lh.kuc_msgtype = KUC_MSG_SHUTDOWN;
                lh.kuc_msglen = sizeof(lh);
                libcfs_kkuc_group_put(group, &lh);
        }

        cfs_down_write(&kg_sem);
        cfs_list_for_each_entry_safe(reg, next, &kkuc_groups[group], kr_chain) {
                if ((uid == 0) || (uid == reg->kr_uid)) {
                        cfs_list_del(&reg->kr_chain);
                        CDEBUG(D_KUC, "Removed uid=%d fp=%p from group %d\n",
                               reg->kr_uid, reg->kr_fp, group);
                        if (reg->kr_fp != NULL)
                                cfs_put_file(reg->kr_fp);
                        cfs_free(reg);
                }
        }
        cfs_up_write(&kg_sem);

        RETURN(0);
}
CFS_EXPORT_SYMBOL(libcfs_kkuc_group_rem);

int libcfs_kkuc_group_put(int group, void *payload)
{
        struct kkuc_reg *reg;
        int rc = 0;
        ENTRY;

        cfs_down_read(&kg_sem);
        cfs_list_for_each_entry(reg, &kkuc_groups[group], kr_chain) {
                if (reg->kr_fp != NULL) {
                rc = libcfs_kkuc_msg_put(reg->kr_fp, payload);
                        if (rc == -EPIPE) {
                                cfs_put_file(reg->kr_fp);
                                reg->kr_fp = NULL;
                        }
                }
        }
        cfs_up_read(&kg_sem);

        RETURN(rc);
}
CFS_EXPORT_SYMBOL(libcfs_kkuc_group_put);

/**
 * Calls a callback function for each link of the given kuc group.
 * @param group the group to call the function on.
 * @param cb_func the function to be called.
 * @param cb_arg iextra argument to be passed to the callback function.
 */
int libcfs_kkuc_group_foreach(int group, libcfs_kkuc_cb_t cb_func,
                              void *cb_arg)
{
        struct kkuc_reg *reg;
        int rc = 0;
        ENTRY;

        if (group > KUC_GRP_MAX) {
                CDEBUG(D_WARNING, "Kernelcomm: bad group %d\n", group);
                RETURN(-EINVAL);
        }

        /* no link for this group */
        if (kkuc_groups[group].next == NULL)
                RETURN(0);

        cfs_down_read(&kg_sem);
        cfs_list_for_each_entry(reg, &kkuc_groups[group], kr_chain) {
                if (reg->kr_fp != NULL) {
                        rc = cb_func(reg->kr_data, cb_arg);
                }
        }
        cfs_up_read(&kg_sem);

        RETURN(rc);
}
CFS_EXPORT_SYMBOL(libcfs_kkuc_group_foreach);

#endif /* LUSTRE_UTILS */

