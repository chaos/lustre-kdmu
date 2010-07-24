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
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * Implementation of params_tree.
 *
 * Author: LiuYing <emoly.liu@sun.com>
 */

#ifndef EXPORT_SYMTAB
# define EXPORT_SYMTAB
#endif
#define DEBUG_SUBSYSTEM S_LNET

#include <libcfs/params_tree.h>

#ifdef __KERNEL__
/* for bug 10866, global variable */
cfs_rw_semaphore_t _lprocfs_lock;
EXPORT_SYMBOL(_lprocfs_lock);

static cfs_rw_semaphore_t libcfs_param_sem;
libcfs_param_entry_t *libcfs_param_lnet_root;

static void free_param(libcfs_param_entry_t *lpe)
{
        if (S_ISLNK(lpe->lpe_mode)) {
                LIBCFS_FREE(lpe->lpe_data, strlen(lpe->lpe_data) + 1);
                lpe->lpe_data = NULL;
        }
        if (lpe->lpe_data != NULL && lpe->lpe_cb_sfops == NULL)
                /* seq params use orig data can't be freed */
                LIBCFS_FREE_PARAMDATA(lpe->lpe_data);
        if (lpe->lpe_hash_t != NULL)
                cfs_hash_putref(lpe->lpe_hash_t);
        LIBCFS_FREE(lpe, sizeof(*lpe) + lpe->lpe_name_len + 1);
}

libcfs_param_entry_t *libcfs_param_get(libcfs_param_entry_t *lpe)
{
        LASSERT(lpe != NULL);
        LASSERT(cfs_atomic_read(&lpe->lpe_refcount) > 0);

        cfs_atomic_inc(&lpe->lpe_refcount);

        return lpe;
}

/**
 * derefer the entry and free it automatically if (refcount == 0)
 */
void libcfs_param_put(libcfs_param_entry_t *lpe)
{
        LASSERT(lpe != NULL);
        LASSERTF(cfs_atomic_read(&lpe->lpe_refcount) > 0,
                 "%s has %d ref\n", lpe->lpe_name,
                 cfs_atomic_read(&lpe->lpe_refcount));
        if (cfs_atomic_dec_and_test(&lpe->lpe_refcount))
                free_param(lpe);
}

static void *lpe_hash_get(cfs_hlist_node_t *hnode)
{
        libcfs_param_entry_t *lpe;

        lpe = cfs_hlist_entry(hnode, libcfs_param_entry_t, lpe_hash_n);

        return libcfs_param_get(lpe);
}

static void *lpe_hash_put(cfs_hlist_node_t *hnode)
{
        libcfs_param_entry_t *lpe;

        lpe = cfs_hlist_entry(hnode, libcfs_param_entry_t, lpe_hash_n);
        libcfs_param_put(lpe);

        return lpe;
}

static unsigned lpe_hash(cfs_hash_t *hs, void *key, unsigned mask)
{
        return cfs_hash_djb2_hash(key, strlen((char *)key), mask);
}

static int lpe_hash_compare(void *key, cfs_hlist_node_t *compared_node)
{
        libcfs_param_entry_t *lpe;
        char                      *lpe_name = key;
        int                        rc;

        lpe = cfs_hlist_entry(compared_node, libcfs_param_entry_t,
                              lpe_hash_n);
        rc = strncmp(lpe_name, lpe->lpe_name, lpe->lpe_name_len);

        return (!rc);
}

static void *lpe_hash_key(cfs_hlist_node_t *hnode)
{
        libcfs_param_entry_t *lpe;

        lpe = cfs_hlist_entry(hnode, libcfs_param_entry_t, lpe_hash_n);

        return ((void *)lpe->lpe_name);
}

static cfs_hash_ops_t lpe_hash_ops = {
        .hs_hash = lpe_hash,
        .hs_compare = lpe_hash_compare,
        .hs_key = lpe_hash_key,
        .hs_put = lpe_hash_put,
        .hs_get = lpe_hash_get,
};

static libcfs_param_entry_t libcfs_param_root = {
        .lpe_parent = &libcfs_param_root,
        .lpe_name = "params_root",
        .lpe_name_len = 11,
        .lpe_hash_t = NULL,
        .lpe_data = NULL,
        .lpe_proc = NULL,
};

libcfs_param_entry_t *libcfs_param_get_root(void)
{
        return (&libcfs_param_root);
}

void libcfs_param_root_init(void)
{
        libcfs_param_root.lpe_hash_t = cfs_hash_create("params_root",
                                                       LPE_HASH_CUR_BITS,
                                                       LPE_HASH_MAX_BITS,
                                                       &lpe_hash_ops,
                                                       CFS_HASH_REHASH);
        cfs_init_rwsem(&libcfs_param_sem);
        cfs_init_rwsem(&libcfs_param_root.lpe_rw_sem);
        cfs_atomic_set(&libcfs_param_root.lpe_refcount, 1);
        libcfs_param_lnet_root = NULL;
}

void libcfs_param_root_fini(void)
{
        LASSERTF(cfs_atomic_read(&libcfs_param_root.lpe_hash_t->hs_count) == 0,
                 "params_root hash has %d hnodes\n",
                 cfs_atomic_read(&libcfs_param_root.lpe_hash_t->hs_count));
        LASSERTF(cfs_atomic_read(&libcfs_param_root.lpe_refcount) == 1,
                 "params_root has %d refs\n",
                 cfs_atomic_read(&libcfs_param_root.lpe_refcount));
        if (libcfs_param_root.lpe_hash_t != NULL)
                cfs_hash_putref(libcfs_param_root.lpe_hash_t);
}

struct libcfs_param_cb_data *libcfs_param_cb_data_alloc(void *data, int flag)
{
        struct libcfs_param_cb_data *cb_data = NULL;

        LIBCFS_ALLOC(cb_data, sizeof(*cb_data));
        if (cb_data == NULL) {
                CERROR("No memory for cb data!\n");
                return NULL;
        }
        cb_data->cb_magic = PARAM_DEBUG_MAGIC;
        cb_data->cb_flag = flag;
        cb_data->cb_data = data;

        return cb_data;
}

void libcfs_param_cb_data_free(struct libcfs_param_cb_data *cb_data, int flag)
{
        if (cb_data != NULL && cb_data->cb_magic == PARAM_DEBUG_MAGIC) {
                LASSERTF((cb_data->cb_flag & flag) ||
                          (cb_data->cb_flag == 0 && flag == 0),
                         "cb_flag is %x flag is %x \n", cb_data->cb_flag,
                          flag);
                LIBCFS_FREE(cb_data, sizeof(*cb_data));
        }
}

/**
 * Basic lookup function
 * Look up an entry by its parent entry and its name
 */
static libcfs_param_entry_t *
_lookup_param(const char *name, libcfs_param_entry_t *parent)
{
        libcfs_param_entry_t *lpe = NULL;

        LASSERT(parent != NULL && name != NULL);

        cfs_down_read(&parent->lpe_rw_sem);
        lpe = cfs_hash_lookup(parent->lpe_hash_t, (void *)name);
        cfs_up_read(&parent->lpe_rw_sem);

        return lpe;
}

static libcfs_param_entry_t *
lookup_param_by_path(const char *pathname, libcfs_param_entry_t *entry);
/**
 * Find the target of a symbolic link entry
 */
static libcfs_param_entry_t *
find_symlink_target(libcfs_param_entry_t *lpe)
{
        libcfs_param_entry_t *entry = NULL;
        char                      *path;
        char                      *path_temp = NULL;

        LASSERT(lpe != NULL);
        LASSERT(S_ISLNK(lpe->lpe_mode));

        if (lpe->lpe_data == NULL) {
                CERROR("No symbolic link data found.\n");
                return NULL;
        }

        LIBCFS_ALLOC(path, strlen(lpe->lpe_data) + 1);
        if (path == NULL) {
                CERROR("No memory for lpe_data.\n");
                return NULL;
        }
        strcpy(path, lpe->lpe_data);
        path_temp = path;
        if (path[0] == '/')
                entry = lookup_param_by_path(path++, NULL);
        else
                entry = lookup_param_by_path(path, lpe);
        LIBCFS_FREE(path_temp, strlen(lpe->lpe_data) + 1);

        return entry;
}

/**
 * Lookup child entry according to its parent entry and its name.
 * If it's a symlink, return its target.
 */
static libcfs_param_entry_t *
lookup_param(const char *name, libcfs_param_entry_t *parent)
{
        libcfs_param_entry_t *lpe;
        libcfs_param_entry_t *syml_tgt = NULL;

        LASSERT(name != NULL && parent != NULL);

        lpe = _lookup_param(name, parent);
        if (lpe == NULL)
                return NULL;

        if (S_ISLNK(lpe->lpe_mode)) {
                syml_tgt = find_symlink_target(lpe);
                libcfs_param_put(lpe);
                lpe = syml_tgt;
        }

        return lpe;
}

/**
 * Lookup an entry according to path name.
 * If @entry is null, @path is a full path and we will search from root;
 * otherwise, it's a relative path.
 * If result entry is a symlink, return its target.
 */
static libcfs_param_entry_t *
lookup_param_by_path(const char *pathname, libcfs_param_entry_t *entry)
{
        libcfs_param_entry_t *lpe;
        libcfs_param_entry_t *parent;
        char                      *path = (char *)pathname;
        char                      *temp;

        if (entry == NULL) {
                /* absolute full path */
                lpe = libcfs_param_get(&libcfs_param_root);
                if (!strncmp(lpe->lpe_name, path, lpe->lpe_name_len)) {
                        if (lpe->lpe_name_len == strlen(path))
                                return lpe;
                        path += lpe->lpe_name_len + 1;
                }
        } else {
                /* relative path */
                lpe = libcfs_param_get(entry->lpe_parent);
        }
        /* separate the path by token '/' */
        while ((temp = strsep(&path, "/")) && (lpe != NULL)) {
                parent = lpe;
                if (!strcmp(temp, ".."))
                        lpe = libcfs_param_get(parent->lpe_parent);
                else
                        /* XXX: or lookup_param here ? */
                        lpe = lookup_param(temp, parent);
                libcfs_param_put(parent);
        }

        return lpe;
}

/**
 * lookup interface for external use (lnet and lustre)
 */
libcfs_param_entry_t *
libcfs_param_lookup(const char *name, libcfs_param_entry_t *parent)
{
        libcfs_param_entry_t *lpe;
        libcfs_param_entry_t *syml_tgt = NULL;

        LASSERT(name != NULL && parent != NULL);

        /* parent can't be a symlink */
        if (S_ISLNK(parent->lpe_mode)) {
                syml_tgt = find_symlink_target(parent);
                if (syml_tgt == NULL)
                        return NULL;
                parent = syml_tgt;
        }
        lpe = _lookup_param(name, parent);
        if (syml_tgt != NULL)
                libcfs_param_put(syml_tgt);

        return lpe;
}

/**
 * Add an entry to params tree by it parent and its name.
 */
static libcfs_param_entry_t *
_add_param(const char *name, mode_t mode, libcfs_param_entry_t *parent)
{
        libcfs_param_entry_t *lpe = NULL;
        char                      *ptr;
        int                        len;
        int                        rc = 0;

        LASSERT(parent != NULL && name != NULL);

        CDEBUG(D_INFO, "add param %s \n", name);
        /* parent's refcount should be inc/dec by the caller */
        if ((lpe = _lookup_param(name, parent))) {
                libcfs_param_put(lpe);
                CERROR("The entry %s already exists!\n", name);
                return NULL;
        }

        /* alloc for new entry */
        len = sizeof(*lpe) + strlen(name) + 1;
        LIBCFS_ALLOC(lpe, len);
        if (lpe == NULL) {
                CERROR("No memory for new entry %s!\n", name);
                return NULL;
        }
        ptr = (char *)lpe + sizeof(*lpe);
        strcpy(ptr, name);
        lpe->lpe_name = ptr;
        lpe->lpe_name_len = strlen(name);
        lpe->lpe_hash_t = NULL;

        /* add hash table or node */
        if (S_ISDIR(mode)) {
                /* create a hash table */
                lpe->lpe_hash_t = cfs_hash_create((char *)name,
                                                  LPE_HASH_CUR_BITS,
                                                  LPE_HASH_MAX_BITS,
                                                  &lpe_hash_ops,
                                                  CFS_HASH_REHASH);
                if (lpe->lpe_hash_t == NULL) {
                        CERROR("create lpe_hash_t %s failed", name);
                        GOTO(fail, rc = -ENOMEM);
                }
        }
        lpe->lpe_data = NULL;
        lpe->lpe_cb_sfops = NULL;
        cfs_init_rwsem(&lpe->lpe_rw_sem);
        lpe->lpe_mode = mode;
        /* Init ref count to 1 to avoid deletion */
        cfs_atomic_set(&lpe->lpe_refcount, 1);

        cfs_down_write(&parent->lpe_rw_sem);
        rc = cfs_hash_add_unique(parent->lpe_hash_t, (char *)name,
                                 &lpe->lpe_hash_n);
        if (!rc)
                lpe->lpe_parent = parent;
        cfs_up_write(&parent->lpe_rw_sem);
fail:
        if (rc) {
                free_param(lpe);
                lpe = NULL;
        }

        return lpe;
}

static libcfs_param_entry_t *
add_param(const char *name, mode_t mode, libcfs_param_entry_t *parent)
{
        libcfs_param_entry_t *syml_tgt = NULL;
        libcfs_param_entry_t *lpe;

        LASSERT(name != NULL && parent != NULL);

        if (S_ISLNK(parent->lpe_mode)) {
                syml_tgt = find_symlink_target(parent);
                if (syml_tgt == NULL)
                        return NULL;
                parent = syml_tgt;
        }
        lpe = _add_param(name, mode, parent);
        if (syml_tgt != NULL)
                libcfs_param_put(syml_tgt);

        return lpe;
}

libcfs_param_entry_t *
libcfs_param_create(const char *name, mode_t mode,
                    libcfs_param_entry_t *parent)
{
        if ((mode & S_IFMT) == 0)
                 mode |= S_IFREG;
        if ((mode & S_IALLUGO) == 0)
                 mode |= S_IRUGO;

        return add_param(name, mode, parent);
}

libcfs_param_entry_t *
libcfs_param_mkdir(const char *name, libcfs_param_entry_t *parent)
{
        return add_param(name, S_IFDIR | S_IRUGO | S_IXUGO, parent);
}

static void _remove_param(libcfs_param_entry_t *lpe);
libcfs_param_entry_t *
libcfs_param_symlink(const char *name, libcfs_param_entry_t *parent,
                     const char *dest)
{
        libcfs_param_entry_t *lpe;

        lpe = add_param(name, S_IFLNK | S_IRUGO | S_IWUGO | S_IXUGO,
                               parent);
        if (lpe != NULL) {
                LIBCFS_ALLOC(lpe->lpe_data, strlen(dest) + 1);
                if (lpe->lpe_data != NULL) {
                        strcpy(lpe->lpe_data, dest);
                } else {
                        CERROR("No memory for lpe_data.");
                        _remove_param(lpe);
                        libcfs_param_put(lpe);
                        lpe = NULL;
                }
        }

        return lpe;
}

/**
 * Remove an entry and all of its children from params_tree
 */
static void _remove_param(libcfs_param_entry_t *lpe)
{
        libcfs_param_entry_t *parent;
        libcfs_param_entry_t *rm_lpe;
        libcfs_param_entry_t *temp = NULL;

        LASSERT(lpe != NULL);

        cfs_down_write(&libcfs_param_sem);
        parent = lpe->lpe_parent;
        rm_lpe = libcfs_param_get(lpe);
        while (1) {
                while (rm_lpe->lpe_hash_t != NULL) {
                        temp = rm_lpe;
                        /* find the first hnode */
                        rm_lpe = cfs_hash_lookup(temp->lpe_hash_t, NULL);
                        if (rm_lpe == NULL) {
                                rm_lpe = temp;
                                break;
                        }
                        libcfs_param_put(temp);
                }
                temp = rm_lpe->lpe_parent;
                cfs_down_write(&temp->lpe_rw_sem);
                CDEBUG(D_INFO, "remove %s from %s\n", rm_lpe->lpe_name, temp->lpe_name);
                rm_lpe = cfs_hash_del_key(temp->lpe_hash_t,
                                          (void *)rm_lpe->lpe_name);
                libcfs_param_put(rm_lpe);
                cfs_up_write(&temp->lpe_rw_sem);

                if(temp == parent)
                        break;
                rm_lpe = libcfs_param_get(temp);
        }
        cfs_up_write(&libcfs_param_sem);
}

/**
 * Remove an entry and all of its children from params_tree by
 * its parent and its name.
 * If it's a symlink, just remove it, we don't need to find its target.
 */
static void remove_param(const char *name, libcfs_param_entry_t *parent)
{
        libcfs_param_entry_t *child;

        LASSERT(parent != NULL && name != NULL);

        if ((child = libcfs_param_lookup(name, parent))) {
                _remove_param(child);
                libcfs_param_put(child);
        }
}

/**
 * interfaces for external use
 */
void libcfs_param_remove(const char *name, libcfs_param_entry_t *lpe)
{
        LASSERT(lpe != NULL);

        return ((name == NULL) ? _remove_param(lpe) : remove_param(name, lpe));
}

/**
 * List all the subdirs of an entry by ioctl.
 */
struct list_param_cb_data {
        char *buf;
        int pos;
        int left;
};

static void list_param_cb(void *obj, void *data)
{
        libcfs_param_entry_t *lpe = obj;
        struct list_param_cb_data *lpcb = data;
        libcfs_param_info_t  *lpi;
        int len = sizeof(struct libcfs_param_info);

        LASSERT(lpe != NULL);

        if (lpcb->left < len) {
                CERROR("Have no enough buffer for list_param.\n");
                return;
        }
        lpi = (libcfs_param_info_t *)(lpcb->buf + lpcb->pos);
        /* copy name_len, name, mode */
        lpi->lpi_name_len = lpe->lpe_name_len;
        lpi->lpi_mode = lpe->lpe_mode;
        strncpy(lpi->lpi_name, lpe->lpe_name, lpe->lpe_name_len);
        lpi->lpi_name[lpi->lpi_name_len] = '\0';
        lpcb->pos += len;
        lpcb->left -= len;

        CDEBUG(D_INFO, "copy \"%s\" out, pos=%d\n", lpe->lpe_name, lpcb->pos);
}

static int list_param(libcfs_param_entry_t *parent,
                      char *kern_buf, int kern_buflen)
{
        struct list_param_cb_data lpcd;
        int rc = 0;

        if (parent == NULL)
                return -EINVAL;

        lpcd.buf = kern_buf;
        lpcd.pos = 0;
        lpcd.left = kern_buflen;
        /* we pass kern_buflen here, because we should avoid the real dir size
         * is larger than we have. */
        cfs_down_write(&parent->lpe_rw_sem);
        cfs_hash_for_each(parent->lpe_hash_t, list_param_cb, &lpcd);
        cfs_up_write(&parent->lpe_rw_sem);

        return rc;
}

int libcfs_param_list(const char *parent_path, char *user_buf, int *buflen)
{
        /* In kernel space, do like readdir
         * In user space, match these entries pathname with the pattern */
        libcfs_param_entry_t *parent;
        char *kern_buf = NULL;
        int datalen = 0;
        int num;
        int rc;

        if (user_buf == NULL) {
                CERROR("The buffer is null.\n");
                GOTO(out, rc = -ENOMEM);
        }
        if (parent_path == NULL) {
                CERROR("The full path is null.\n");
                GOTO(out, rc = -EINVAL);
        }

        parent = lookup_param_by_path(parent_path, NULL);
        if (parent == NULL) {
                CERROR("The parent entry %s doesn't exist.\n",
                       parent_path);
                GOTO(out, rc = -EEXIST);
        }
        /* estimate if buflen is big enough */
        num = cfs_atomic_read(&(parent->lpe_hash_t->hs_count));
        if (num == 0)
                GOTO(parent, rc = 0);
        datalen = num * sizeof(struct libcfs_param_info);
        if (datalen > *buflen)
                GOTO(parent, rc = -ENOMEM);
        /* list the entries */
        LIBCFS_ALLOC(kern_buf, datalen);
        if (kern_buf == NULL) {
                CERROR("kernel can't alloc %d bytes.\n", datalen);
                *buflen = 0;
                GOTO(parent, rc = -ENOMEM);
        }
        rc = list_param(parent, kern_buf, datalen);
        if (cfs_copy_to_user(user_buf, kern_buf, datalen))
                rc = -EFAULT;
        LIBCFS_FREE(kern_buf, datalen);
parent:
        libcfs_param_put(parent);
out:
        *buflen = datalen;
        return (rc < 0 ? rc : num);
}

/**
 * These seq operations are help wrappers for using those original
 * seq operation implemented for linux seq file, it should be cleanup
 * when we throw linux /proc totally */
struct libcfs_param_seq_args {
        libcfs_param_dentry_t   *lpsa_pd;
        libcfs_file_t           *lpsa_file;
        libcfs_proc_inode_t     *lpsa_inode;
        char                    *lpsa_buf;
        void                    *lpsa_value;
};

int libcfs_param_seq_print_common(libcfs_seq_file_t *seq,
                                  const char *fmt, ...)
{
        va_list args;
        int len;

        /* Got this code from seq_printf*/
        if (seq->count < seq->size) {
                va_start(args, fmt);
                len = libcfs_vsnprintf(seq->buf + seq->count,
                                       seq->size - seq->count,
                                       fmt, args);
                va_end(args);
                if (seq->count + len < seq->size) {
                        seq->count += len;
                        return 0;
                }
        }
        seq->count = seq->size;

        return -1;
}

void libcfs_seq_release(libcfs_inode_t *inode, libcfs_file_t *file)
{
        libcfs_seq_file_t *seqf = LIBCFS_FILE_PRIVATE(file);

        if (seqf)
                LIBCFS_FREE(seqf, sizeof(libcfs_seq_file_t));
}

int libcfs_param_seq_release_common(libcfs_inode_t *inode, libcfs_file_t *file)
{
        libcfs_seq_file_t *seqf = LIBCFS_FILE_PRIVATE(file);

        LASSERT(seqf != NULL);
        /**
         * Note: it will release seqf->buf in seq_release,
         * but params_tree already set seqf->buf = NULL,
         * In linux, it is ok. Ok for other platform?
         **/
        LIBCFS_SEQ_RELEASE(inode, file);
        LPROCFS_EXIT();
        return 0;
}

int libcfs_param_seq_puts_common(libcfs_seq_file_t *seq,
                           const char *s)
{
        int len = strlen(s);
        if (seq->count + len < seq->size) {
                memcpy(seq->buf + seq->count, s, len);
                seq->count += len;
                return 0;
        }
        seq->count = seq->size;
        return -1;
}

int libcfs_param_seq_putc_common(libcfs_seq_file_t *seq,
                                const char c)
{
        if (seq->count < seq->size) {
                seq->buf[seq->count++] = c;
                return 0;
        }
        return -1;
}

static void
libcfs_param_seq_finiargs(struct libcfs_param_seq_args *args)
{
        if (args->lpsa_inode)
                LIBCFS_FREE(args->lpsa_inode, sizeof(libcfs_proc_inode_t));
        if (args->lpsa_file)
                LIBCFS_FREE(args->lpsa_file, sizeof(libcfs_file_t));
        if (args->lpsa_pd)
                LIBCFS_FREE(args->lpsa_pd, sizeof(libcfs_param_dentry_t));
        if (args->lpsa_buf)
                LIBCFS_FREE(args->lpsa_buf, CFS_PAGE_SIZE);
}

static int
libcfs_param_seq_initargs(struct libcfs_param_seq_args *args)
{
        int rc = 0;

        LIBCFS_ALLOC(args->lpsa_inode, sizeof(libcfs_proc_inode_t));
        if (!args->lpsa_inode)
                GOTO(out, rc = -ENOMEM);

        LIBCFS_ALLOC(args->lpsa_file, sizeof(libcfs_file_t));
        if (!args->lpsa_file)
                GOTO(out, rc = -ENOMEM);

        LIBCFS_ALLOC(args->lpsa_pd, sizeof(libcfs_param_dentry_t));
        if (!args->lpsa_pd)
                GOTO(out, rc = -ENOMEM);

        LIBCFS_ALLOC(args->lpsa_buf, CFS_PAGE_SIZE);
        if (!args->lpsa_buf)
                GOTO(out, rc = -ENOMEM);
out:
        if (rc)
                libcfs_param_seq_finiargs(args);
        RETURN(rc);
}

static int libcfs_param_seq_open(libcfs_param_entry_t *lpe,
                                 struct libcfs_param_seq_args *args,
                                 char *buf, int count)
{
        libcfs_proc_inode_t     *proc_inode = args->lpsa_inode;
        libcfs_inode_t          *inode = &LIBCFS_PROCI_INODE(proc_inode);
        libcfs_file_ops_t       *sfops = lpe->lpe_cb_sfops;
        int                      rc = 0;
        libcfs_seq_file_t       *seqf;

        LIBCFS_DENTRY_DATA(args->lpsa_pd) = lpe->lpe_data;
        LIBCFS_PROC_INODE_PDE(proc_inode) = args->lpsa_pd;

        LASSERT(sfops != NULL);
        rc = sfops->open(inode, args->lpsa_file);
        if (rc)
                RETURN(rc);

        seqf = LIBCFS_FILE_PRIVATE(args->lpsa_file);
        LASSERT(seqf != NULL);
        /* This seqf might be init already in linux system */
        cfs_mutex_init(&seqf->lock);
        seqf->buf = args->lpsa_buf;
        if (buf)
                memcpy(seqf->buf, buf, count);

        /* seqf->size is user buffer size, instead of seqf->buf */
        seqf->size = count;
        RETURN(rc);
}

static void
libcfs_param_seq_release(libcfs_param_entry_t *lpe,
                         struct libcfs_param_seq_args *args)
{
        libcfs_proc_inode_t     *proc_inode = args->lpsa_inode;
        libcfs_inode_t          *inode = &LIBCFS_PROCI_INODE(proc_inode);
        libcfs_file_ops_t       *sfops = lpe->lpe_cb_sfops;
        libcfs_seq_file_t       *seqf;

        seqf = LIBCFS_FILE_PRIVATE(args->lpsa_file);
        LASSERT(seqf != NULL && sfops != NULL);
        seqf->buf = NULL;
        seqf->size = 0;
        sfops->release(inode, args->lpsa_file);

        return;
}

static void *libcfs_param_seq_start(libcfs_param_entry_t *lpe,
                                    struct libcfs_param_seq_args *args,
                                    loff_t *loff)
{
        libcfs_seq_file_t *seqf = LIBCFS_FILE_PRIVATE(args->lpsa_file);
        libcfs_seq_ops_t *seqop = seqf->op;

        LASSERT(seqop != NULL && seqop->start != NULL);
        args->lpsa_value = seqop->start(seqf, loff);

        return args->lpsa_value;
}

static void libcfs_param_seq_stop(libcfs_param_entry_t *lpe,
                                  struct libcfs_param_seq_args *args)
{
        libcfs_seq_file_t *seqf = LIBCFS_FILE_PRIVATE(args->lpsa_file);
        libcfs_seq_ops_t *seqop = seqf->op;

        LASSERT(seqop != NULL && seqop->stop != NULL);
        seqop->stop(seqf, args->lpsa_value);

        return;
}

static int libcfs_param_data_pack(char *buf, int buf_len,
                                  enum libcfs_param_value_type type,
                                  const char *name, const char *unit);
static int libcfs_param_seq_show(libcfs_param_entry_t *lpe,
                                 struct libcfs_param_seq_args *args,
                                 char **output)
{
        libcfs_seq_file_t *seqf = LIBCFS_FILE_PRIVATE(args->lpsa_file);
        libcfs_seq_ops_t *seqop = seqf->op;
        int rc = 0;

        LASSERT(seqop != NULL && seqop->show != NULL);
        seqf->count = 0;
        memset(seqf->buf, 0, CFS_PAGE_SIZE);
        rc = seqop->show(seqf, args->lpsa_value);
        if (rc < 0)
                return rc;

        if (strlen(seqf->buf) > 0) {
                rc = libcfs_param_data_pack(seqf->buf, seqf->size, LP_STR,
                                            NULL, NULL);
                if ( rc < 0)
                        return rc;
        }
        *output = seqf->buf;

        return rc;
}

static void *libcfs_param_seq_next(libcfs_param_entry_t *lpe,
                                   struct libcfs_param_seq_args *args,
                                   loff_t *loff)
{
        libcfs_seq_file_t *seqf = LIBCFS_FILE_PRIVATE(args->lpsa_file);
        libcfs_seq_ops_t *seqop = seqf->op;

        LASSERT(seqop != NULL && seqop->next != NULL);
        args->lpsa_value = seqop->next(seqf, args->lpsa_value, loff);

        return args->lpsa_value;
}

static int libcfs_param_seq_write(libcfs_param_entry_t *lpe,
                                  struct libcfs_param_seq_args *args,
                                  const char *buf, int count, loff_t *off)
{
        libcfs_file_t     *file = args->lpsa_file;
        libcfs_file_ops_t *sfops = lpe->lpe_cb_sfops;

        LASSERT(sfops != NULL);
        if (sfops->write != NULL)
                return sfops->write(file, buf, (unsigned long)count, off);

        return -EIO;
}

static int libcfs_param_seq_read(char *ubuf, loff_t *loff, int count,
                                 int *eof, libcfs_param_entry_t *lpe)
{
        struct libcfs_param_seq_args args = {0};
        int left_bytes = count;
        int bytes;
        char *output;
        int rc;
        void *p;

        rc = libcfs_param_seq_initargs(&args);
        if (rc) {
                CERROR("Init seq failed for %s rc %d\n", lpe->lpe_name, rc);
                RETURN(rc);
        }

        rc = libcfs_param_seq_open(lpe, &args, NULL, count);
        if (rc) {
                CERROR("seq open failed for %s rc %d \n", lpe->lpe_name, rc);
                GOTO(free, rc);
        }

        p = libcfs_param_seq_start(lpe, &args, loff);
        if (p == NULL || IS_ERR(p)) {
                *eof = 1;
                rc = PTR_ERR(p);
                CWARN("seq start failed for %s rc %d \n", lpe->lpe_name, rc);
                GOTO(out, rc);
        }

        /* we need at least one record in buffer */
        bytes = libcfs_param_seq_show(lpe, &args, &output);
        if (bytes < 0)
                GOTO(out, rc = bytes);
        if (bytes > left_bytes)
                GOTO(out, rc = left_bytes - bytes);
        if (left_bytes >= bytes) {
                if (cfs_copy_to_user(ubuf, output, bytes))
                        GOTO(out, rc = -EFAULT);
                left_bytes -= bytes;
                ubuf += bytes;
        }

        /* get more */
        while((left_bytes > 0)) {
                char *format_buf;

                p = libcfs_param_seq_next(lpe, &args, loff);
                if (p == NULL || IS_ERR(p)) {
                        *eof = 1;
                        rc = PTR_ERR(p);
                        break;
                }

                bytes = libcfs_param_seq_show(lpe, &args, &format_buf);
                if (bytes < 0) {
                        rc = bytes;
                        break;
                } else if (bytes == 0) {
                        /* After the beginning, some recodes can be 0.
                         * E.g. lprocfs_stats_seq_show() */
                        continue;
                }
                if (left_bytes >= bytes) {
                        if (cfs_copy_to_user(ubuf, format_buf, bytes)) {
                                rc = -EFAULT;
                                break;
                        }
                } else {
                        break;
                }

                left_bytes -= bytes;
                ubuf += bytes;
        }
out:
        rc = (left_bytes < count) ? (count - left_bytes) : rc;
        libcfs_param_seq_stop(lpe, &args);
        libcfs_param_seq_release(lpe, &args);
free:
        libcfs_param_seq_finiargs(&args);
        return rc;
}

#define IS_SEQ_LPE(lpe) (lpe->lpe_cb_sfops != NULL)
static int libcfs_param_normal_read(char *buf, loff_t *ppos,
                                    int count, int *eof,
                                    libcfs_param_entry_t *entry)
{
        char  *page = NULL;
        char *start = NULL;
        int left_bytes = count;
        int bytes;
        int rc = 0;
        off_t pos = (off_t)*ppos;

        LIBCFS_ALLOC(page, CFS_PAGE_SIZE);
        if (page == NULL)
                return -ENOMEM;
        while((left_bytes > 0) && !(*eof)) {
                /* suppose *eof = 1 by default so that we don't need to
                 * set it in each read_cb function. */
                *eof = 1;
                /* read the param value */
                if (entry->lpe_cb_read != NULL)
                        bytes = entry->lpe_cb_read(page, &start, pos, count,
                                                   eof, entry->lpe_data);
                else
                        break;

                if (bytes <= 0) {
                        rc = bytes;
                        break;
                }
                if (cfs_copy_to_user(buf, page, bytes)) {
                        rc = -EFAULT;
                        break;
                }
                pos += bytes;
                rc += bytes;
                left_bytes -= bytes;
                buf += bytes;
        }

        *ppos = pos;

        LIBCFS_FREE(page, CFS_PAGE_SIZE);
        return rc;
}

int libcfs_param_read(const char *path, char *buf, int nbytes, loff_t *ppos,
                      int *eof)
{
        libcfs_param_entry_t *entry;
        int                        rc = 0;
        int                        count;

        if (path == NULL) {
                CERROR("path is null.\n");
                return -EINVAL;
        }
        /* lookup the entry according to its pathname */
        entry = lookup_param_by_path(path, NULL);
        if (entry == NULL) {
                CERROR("The params entry %s doesn't exist.\n", path);
                return -ENOENT;
        }

        *eof = 0;
        count = (CFS_PAGE_SIZE > nbytes) ? nbytes : CFS_PAGE_SIZE;

        cfs_down_read(&entry->lpe_rw_sem);
        if (IS_SEQ_LPE(entry))
                rc = libcfs_param_seq_read(buf, ppos, count, eof, entry);
        else if (entry->lpe_cb_read != NULL)
                rc = libcfs_param_normal_read(buf, ppos, count, eof, entry);
        cfs_up_read(&entry->lpe_rw_sem);

        libcfs_param_put(entry);
        return rc;
}

static int libcfs_seq_file_write(char *buf, int count,
                                 libcfs_param_entry_t *lpe)
{
        struct libcfs_param_seq_args args = {0};
        loff_t  off = 0;
        int rc;

        rc = libcfs_param_seq_initargs(&args);
        if (rc) {
                CERROR("Init seq failed for %s rc %d\n",
                        lpe->lpe_name, rc);
                RETURN(rc);
        }

        rc = libcfs_param_seq_open(lpe, &args, buf, count);
        if (rc) {
                CERROR("seq open failed for %s rc %d \n",
                        lpe->lpe_name, rc);
                GOTO(free, rc);
        }

        rc = libcfs_param_seq_write(lpe, &args, buf, count, &off);

        libcfs_param_seq_release(lpe, &args);
free:
        libcfs_param_seq_finiargs(&args);
        return rc;

}

int libcfs_param_write(const char *path, char *buf, int count)
{
        libcfs_param_entry_t *entry;
        int                        rc = 0;

        if (path == NULL) {
                CERROR("path is null.\n");
                return -EINVAL;
        }
        entry = lookup_param_by_path(path, NULL);
        if (entry == NULL) {
                CERROR("The params entry %s doesn't exist.\n", path);
                return -EINVAL;
        }

        cfs_down_write(&entry->lpe_rw_sem);
        if (IS_SEQ_LPE(entry))
                rc = libcfs_seq_file_write(buf, count, entry);
        else if (entry->lpe_cb_write != NULL)
                rc = entry->lpe_cb_write(NULL, buf, count, entry->lpe_data);
        else
                GOTO(out, rc = -EIO);
out:
        cfs_up_write(&entry->lpe_rw_sem);
        libcfs_param_put(entry);
        return rc;
}

int libcfs_param_intvec_write(libcfs_file_t *filp, const char *buffer,
                              unsigned long count, void *data)
{
        unsigned long temp;
        const char   *pbuf = buffer;
        int           mult = 1;

        if (*pbuf == '-') {
                mult = -mult;
                pbuf ++;
                temp= simple_strtoul(pbuf, NULL, 10) * mult;
        } else if (pbuf[0] == '0' && toupper(pbuf[1]) == 'X') {
                temp = simple_strtoul(pbuf, NULL, 16);
        } else {
                temp = simple_strtoul(pbuf, NULL, 10);
        }
        memcpy(((lparcb_t *)data)->cb_data, &temp, sizeof(temp));

        return 0;
}

int libcfs_param_intvec_read(char *page, char **start, off_t off, int count,
                             int *eof, void *data)
{
        unsigned long *temp = ((lparcb_t *)data)->cb_data;

        return libcfs_param_snprintf(page, count, data, LP_D32, NULL, *temp);
}

int libcfs_param_string_write(libcfs_file_t *filp, const char *buffer,
                              unsigned long count, void *data)
{
        memcpy(((lparcb_t *)data)->cb_data, buffer, count);
        return 0;
}

int libcfs_param_string_read(char *page, char **start, off_t off, int count,
                             int *eof, void *data)
{
        return libcfs_param_snprintf(page, count, data, LP_STR, "%s",
                                   (char *)(((lparcb_t *)data)->cb_data));
}

void libcfs_param_sysctl_init(char *mod_name,
                              struct libcfs_param_ctl_table *table,
                              struct libcfs_param_entry *parent)
{
        struct libcfs_param_entry *lpe;

        lpe = libcfs_param_lookup(mod_name, parent);
        if (lpe == NULL)
                lpe = libcfs_param_mkdir(mod_name, parent);
        if (lpe != NULL) {
                libcfs_param_sysctl_register(table, lpe);
                libcfs_param_put(lpe);
        } else {
                CERROR("failed to register ctl_table to %s/%s.\n",
                       parent->lpe_name, mod_name);
        }
}

void libcfs_param_sysctl_fini(char *mod_name, libcfs_param_entry_t *parent)
{
        libcfs_param_remove(mod_name, parent);
}

/**
 * add params in sysctl table to params_tree
 */
void libcfs_param_sysctl_register(struct libcfs_param_ctl_table *table,
                                  struct libcfs_param_entry *parent)
{
        struct libcfs_param_entry *lpe = NULL;

        if (parent == NULL)
                return;
        /* create sys subdir */
        for (; table->name; table ++) {
                lpe = libcfs_param_create(table->name, table->mode, parent);
                if (lpe == NULL)
                        continue;
                lpe->lpe_data = LIBCFS_ALLOC_PARAMDATA(table->data);
                if (lpe->lpe_data == NULL) {
                        CERROR("No memory for param cb_data.");
                        return;
                }
                lpe->lpe_cb_read = table->read;
                lpe->lpe_cb_write = table->write;
                /* for lnet write-once params */
                if (table->writeable_before_startup == 1)
                        lpe->lpe_mode |= S_IWUSR;
                libcfs_param_put(lpe);
        }
}

/**
 * For some parameters in lnet, although they're declared as read-only by
 * CFS_MODULE_PARM, they will be written during configuration.
 * So, we provide such a function to meet this requirement.
 */
static void libcfs_param_change_mode(struct libcfs_param_ctl_table *table,
                                     struct libcfs_param_entry *parent)
{
        struct libcfs_param_entry *lpe = NULL;

        LASSERT(parent != NULL);
        for (; table->name; table ++) {
                lpe = libcfs_param_lookup(table->name, parent);
                if (lpe == NULL)
                        continue;
                /* for lnet write-once params */
                cfs_down_write(&lpe->lpe_rw_sem);
                if (table->writeable_before_startup == 1 &&
                    lpe->lpe_cb_write != NULL) {
                        if (lpe->lpe_mode & S_IWUSR)
                                lpe->lpe_mode &= ~S_IWUSR;
                        else
                                lpe->lpe_mode |= S_IWUSR;
                }
                cfs_up_write(&lpe->lpe_rw_sem);
                libcfs_param_put(lpe);
        }
}

void libcfs_param_sysctl_change(char *mod_name,
                                struct libcfs_param_ctl_table *table,
                                struct libcfs_param_entry *parent)
{
        struct libcfs_param_entry *lpe;

        lpe = libcfs_param_lookup(mod_name, parent);
        if (lpe != NULL) {
                libcfs_param_change_mode(table, lpe);
                libcfs_param_put(lpe);
        } else {
                CERROR("Not found module %s under %s.\n",
                       mod_name, parent->lpe_name);
        }

}

/*
void libcfs_param_change_mode(libcfs_param_entry_t *lpe, mode_t mode)
{
        LASSERT(lpe != NULL);

        entry = libcfs_param_get(lpe);

        cfs_down_write(&entry->lpe_rw_sem);
        entry->lpe_mode = mode;
        cfs_up_write(&entry->lpe_rw_sem);

        libcfs_param_put(entry);
}

int libcfs_param_change_mode(const char *name, libcfs_param_entry_t *lpe,
                             int flag)
{
        libcfs_param_entry_t *entry;
        char                 *path = NULL;

        LASSERT(lpe != NULL || name != NULL);

        if (lpe == NULL && name != NULL) {
                LIBCFS_ALLOC(path, strlen(PTREE_ROOT) + strlen(name) + 1);
                entry = lookup_param_by_path(path);
        } else if (lpe != NULL && name != NULL) {
                entry = _lookup_param(name, strlen(name), lpe);
        } else {
                entry = libcfs_param_get(lpe);
        }
        if (path != NULL)
                LIBCFS_FREE(path, strlen(path) + 1);

        if (entry == NULL)
                return -EINVAL;
        cfs_down_write(&entry->lpe_rw_sem);
        if (flag == 1)
                entry->lpe_mode |= S_IWUSR;
        else (flag == 0)
                entry->lpe_mode ~= S_IWUSR;
        cfs_up_write(&entry->lpe_rw_sem);

        libcfs_param_put(entry);

        return 0;
}
*/

/**
 * Since the usr buffer addr has been mapped to the kernel space through
 * libcfs_ioctl_pack(), we don't need copy_from_user.
 * Instead, just call memcpy.
 */
int libcfs_param_copy(int flag, char *dest, const char *src, int count)
{
        if (flag & LIBCFS_PARAM_ACCESS)
                memcpy(dest, src, count);
        else if (cfs_copy_from_user(dest, src, count))
                return -EFAULT;
        return 0;
}

static int get_value_len(enum libcfs_param_value_type type, char *buf)
{
        switch (type) {
                case LP_D16:
                        return 2;
                case LP_D32:
                        return 4;
                case LP_D64:
                        return 8;
                case LP_U8:
                        return 1;
                case LP_U16:
                        return 2;
                case LP_U32:
                        return 4;
                case LP_U64:
                        return 8;
                case LP_DB:
                case LP_STR:
			return strlen(buf);
                default:
                        return 0;
        }
}

/**
 * Since structure va_list is different between 64bit and 32bit platforms,
 * we can't memcpy va_list directly. One possible way is to use va_arg() to
 * pass the specified value type.
 * However, according to the compliation warnings, the types of 'short',
 * '__u8' and '__u16' will be promoted to 'int' when passed through '...',
 * otherwise, if this code is reached, the program will abort.
 * So we have to pass 'int' and '__u32' instead of them respectively here.
 * Currently, the code works on Linux i386 and x86_64 platforms.
 */
#define memcpy_num(page, args, type) do {       \
        type temp = va_arg(args, type);         \
        memcpy(page, &temp, sizeof(temp));      \
} while(0)
static void
print_num(char *page, va_list args, enum libcfs_param_value_type type)
{
        switch (type) {
                case LP_D16:
                case LP_D32:
                        memcpy_num(page, args, int);
                        break;
                case LP_D64:
                        memcpy_num(page, args, long long);
                        break;
                case LP_U8:
                case LP_U16:
                case LP_U32:
                        memcpy_num(page, args, __u32);
                        break;
                case LP_U64:
                        memcpy_num(page, args, __u64);
                        break;
                default:
                        return;
        }
}

int libcfs_param_snprintf_common(char *page, int count, void *cb_data,
                                 enum libcfs_param_value_type type,
                                 const char *name, const char *unit,
                                 const char *format, ...)
{
        va_list args;
        int rc = 0;
        int done = 0;

        LASSERT(((lparcb_t*)cb_data)->cb_magic == PARAM_DEBUG_MAGIC);
        va_start(args, format);
        if (((lparcb_t*)cb_data)->cb_flag & LIBCFS_PARAM_ACCESS) {
                switch (type) {
                case LP_DB:
                        /* currently, we treate LP_DB as LP_STR */
                case LP_STR:
                        if (format == NULL)
                                /* sometimes value has been printed to page */
                                done = 1;
                        else
                                rc = libcfs_vsnprintf(page, count,
                                                      format, args);
                        if (done || rc > 0)
                                rc = libcfs_param_data_pack(page, count,
                                                    LP_STR, name, unit);
                        break;
                default:
                        /* else, they are numbers */
                        print_num(page, args, type);
                        rc = libcfs_param_data_pack(page, count, type,
                                                    name, unit);
                        break;
                }
        } else {
                if (format != NULL) {
                        if (name != NULL)
                                rc = libcfs_snprintf(page, count, "%s", name);
                        rc += libcfs_vsnprintf(page + rc, count - rc,
                                               format, args);
                        if (unit != NULL)
                                rc += libcfs_snprintf(page + rc, count - rc,
                                                      "%s\n", unit);
                } else {
                        rc = get_value_len(type, page);
                }
        }
        va_end(args);

        return rc;
}

/**
 * Validate libcfs_param_data
 */
static int libcfs_param_is_invalid(libcfs_param_data_t *data)
{
        if ((data->param_name != NULL) && !data->param_name_len) {
                CERROR("pve_name pointer but 0 length\n");
                return 1;
        }
        if ((data->param_unit != NULL) && !data->param_unit_len) {
                CERROR("pve_unit pointer but 0 length\n");
                return 1;
        }
        if ((data->param_value != NULL) && !data->param_value_len) {
                CERROR("pve_value pointer but 0 length\n");
                return 1;
        }
        if (data->param_name_len && (data->param_name == NULL)) {
                CERROR("pve_name_len nozero but no name pointer\n");
                return 1;
        }
        if (data->param_unit_len && (data->param_unit == NULL)) {
                CERROR("pve_unit_len nozero but no unit pointer\n");
                return 1;
        }
        if (data->param_value_len && (data->param_value == NULL)) {
                CERROR("pve_value_len nozero but no value pointer\n");
                return 1;
        }
        if (data->param_name_len &&
            data->param_name[data->param_name_len] != '\0') {
                CERROR ("pve_name not 0 terminated\n");
                return 1;
        }
        if (data->param_unit_len &&
            data->param_unit[data->param_unit_len] != '\0') {
                CERROR ("pve_unit not 0 terminated\n");
                return 1;
        }
        if (data->param_type == LP_STR || data->param_type == LP_DB) {
		if (data->param_value_len &&
                    data->param_value[data->param_value_len] != '\0') {
			CERROR ("pve_value(string) not 0 terminated\n");
			return 1;
		}
        }

        return 0;
}

/**
 * Two steps:
 * 1) copy the param_value stored in buffer out
 * 2) organize libcfs_param_data struct and pack it back into the buffer
 */
static int libcfs_param_data_pack(char *buf, int buf_len,
                                  enum libcfs_param_value_type type,
                                  const char *name, const char *unit)
{
        struct libcfs_param_data  *data = NULL;
        char                      *ptr;
        char                      *value = NULL;
        int                        data_len = 0;
        int                        value_len = 0;
	int			   rc = 0;

	/* we store param_value into buf, copy it out first */
        if (buf == NULL) {
                CERROR("buf is null.\n");
                return -ENOMEM;
        }
        value_len = get_value_len(type, buf);
	if (value_len) {
		LIBCFS_ALLOC(value, value_len);
		if (value == NULL)
			return -ENOMEM;
		memcpy(value, buf, value_len);
	}
	/* check buf_len */
        data_len = sizeof(struct libcfs_param_data);
        if (name != NULL)
                data_len += strlen(name) + 1;
        if (unit != NULL)
                data_len += strlen(unit) + 1;
        data_len += value_len;
        if (data_len > buf_len) {
                CERROR("max_buflen(%d) < pvelen (%d)\n", buf_len, data_len);
                GOTO(out, rc = -ENOMEM);
        }
        /* create libcfs_param_data struct */
        memset(buf, 0, buf_len);
        data = (libcfs_param_data_t *)buf;
        ptr = data->param_bulk;
        data->param_type = type;
        if (name != NULL) {
                data->param_name_len = strlen(name);
                strcpy(ptr, name);
                data->param_name = ptr;
                ptr += strlen(name) + 1;
        } else {
                data->param_name = NULL;
                data->param_name_len = 0;
        }
        if (unit != NULL) {
                data->param_unit_len = strlen(unit);
                strcpy(ptr, unit);
                data->param_unit = ptr;
                ptr += strlen(unit) + 1;
        } else {
                data->param_unit = NULL;
                data->param_unit_len = 0;
        }
        data->param_value_len = value_len;
        if (value_len) {
		memcpy(ptr, value, value_len);
                data->param_value = ptr;
	}

        if (libcfs_param_is_invalid(data))
                GOTO(out, rc = -EINVAL);
out:
	if (value_len)
		LIBCFS_FREE(value, value_len);

        return rc < 0 ? rc : data_len;
}

EXPORT_SYMBOL(libcfs_param_get_root);
EXPORT_SYMBOL(libcfs_param_lnet_root);
EXPORT_SYMBOL(libcfs_param_get);
EXPORT_SYMBOL(libcfs_param_put);
EXPORT_SYMBOL(libcfs_param_lookup);
EXPORT_SYMBOL(libcfs_param_create);
EXPORT_SYMBOL(libcfs_param_mkdir);
EXPORT_SYMBOL(libcfs_param_symlink);
EXPORT_SYMBOL(libcfs_param_remove);
EXPORT_SYMBOL(libcfs_param_intvec_read);
EXPORT_SYMBOL(libcfs_param_intvec_write);
EXPORT_SYMBOL(libcfs_param_string_read);
EXPORT_SYMBOL(libcfs_param_string_write);
EXPORT_SYMBOL(libcfs_param_sysctl_init);
EXPORT_SYMBOL(libcfs_param_sysctl_fini);
EXPORT_SYMBOL(libcfs_param_sysctl_register);
EXPORT_SYMBOL(libcfs_param_sysctl_change);
EXPORT_SYMBOL(libcfs_param_snprintf_common);
EXPORT_SYMBOL(libcfs_param_copy);
EXPORT_SYMBOL(libcfs_param_cb_data_alloc);
EXPORT_SYMBOL(libcfs_param_cb_data_free);
EXPORT_SYMBOL(libcfs_param_seq_release_common);
EXPORT_SYMBOL(libcfs_param_seq_print_common);
EXPORT_SYMBOL(libcfs_param_seq_puts_common);
EXPORT_SYMBOL(libcfs_param_seq_putc_common);
#endif /* __KERNEL__ */
