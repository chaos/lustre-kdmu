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
 * Author: LiuYing <emoly.liu@oracle.com>
 */

#ifndef EXPORT_SYMTAB
# define EXPORT_SYMTAB
#endif
#define DEBUG_SUBSYSTEM S_LNET

#include <libcfs/params_tree.h>

#ifdef __KERNEL__

#ifdef LPROCFS
/* for bug 10866, global variable, moved from lprocfs_status.c */
CFS_DECLARE_RWSEM(_lprocfs_lock);
EXPORT_SYMBOL(_lprocfs_lock);
#endif /* LPROCFS */

static cfs_rw_semaphore_t cfs_param_sem;

static void
param_free(cfs_param_entry_t *pe)
{
        if (S_ISLNK(pe->pe_mode))
                LIBCFS_FREE(pe->pe_data, strlen(pe->pe_data) + 1);
        else if (pe->pe_data != NULL && pe->pe_cb_sfops == NULL)
                /* seq params use orig data can't be freed */
                CFS_FREE_PARAMDATA(pe->pe_data);
        if (pe->pe_hs != NULL)
                cfs_hash_putref(pe->pe_hs);
        LIBCFS_FREE(pe, sizeof(*pe) + pe->pe_name_len + 1);
}

cfs_param_entry_t *
cfs_param_get(cfs_param_entry_t *pe)
{
        LASSERT(pe != NULL);
        LASSERT(cfs_atomic_read(&pe->pe_refcount) > 0);

        cfs_atomic_inc(&pe->pe_refcount);

        return pe;
}
EXPORT_SYMBOL(cfs_param_get);

/**
 * derefer the entry and free it automatically if (refcount == 0)
 */
void
cfs_param_put(cfs_param_entry_t *pe)
{
        LASSERT(pe != NULL);
        LASSERTF(cfs_atomic_read(&pe->pe_refcount) > 0,
                 "%s has %d ref\n", pe->pe_name,
                 cfs_atomic_read(&pe->pe_refcount));
        if (cfs_atomic_dec_and_test(&pe->pe_refcount))
                param_free(pe);
}
EXPORT_SYMBOL(cfs_param_put);

static void *
pe_hash_get(cfs_hlist_node_t *hnode)
{
        cfs_param_entry_t *pe;

        pe = cfs_hlist_entry(hnode, cfs_param_entry_t, pe_hnode);

        return cfs_param_get(pe);
}

static void *
pe_hash_put(cfs_hlist_node_t *hnode)
{
        cfs_param_entry_t *pe;

        pe = cfs_hlist_entry(hnode, cfs_param_entry_t, pe_hnode);
        cfs_param_put(pe);

        return pe;
}

static unsigned
pe_hash(cfs_hash_t *hs, void *key, unsigned mask)
{
        return cfs_hash_djb2_hash(key, strlen((char *)key), mask);
}

static int
pe_hash_keycmp(void *key, cfs_hlist_node_t *compared_node)
{
        cfs_param_entry_t *pe;
        char              *pe_name = key;
        int                   rc;

        pe = cfs_hlist_entry(compared_node, cfs_param_entry_t,
                             pe_hnode);
        rc = strncmp(pe_name, pe->pe_name, pe->pe_name_len);

        return (!rc);
}

static void *
pe_hash_key(cfs_hlist_node_t *hnode)
{
        cfs_param_entry_t *pe;

        pe = cfs_hlist_entry(hnode, cfs_param_entry_t, pe_hnode);

        return ((void *)pe->pe_name);
}

static void *
pe_hash_object(cfs_hlist_node_t *hnode)
{
        return cfs_hlist_entry(hnode, cfs_param_entry_t, pe_hnode);
}

static cfs_hash_ops_t pe_hash_ops = {
        .hs_hash       = pe_hash,
        .hs_key        = pe_hash_key,
        .hs_keycmp     = pe_hash_keycmp,
        .hs_object     = pe_hash_object,
        .hs_get        = pe_hash_get,
        .hs_put        = pe_hash_put,
        .hs_put_locked = pe_hash_put,
};

static cfs_param_entry_t cfs_param_root = {
        .pe_parent   = &cfs_param_root,
        .pe_name     = "params_root",
        .pe_name_len = 11,
        .pe_hs       = NULL,
        .pe_data     = NULL,
        .pe_proc     = NULL,
};

cfs_param_entry_t *
cfs_param_get_root(void)
{
        return (&cfs_param_root);
}
EXPORT_SYMBOL(cfs_param_get_root);

static cfs_param_entry_t *cfs_param_lnet_root;
cfs_param_entry_t *
cfs_param_get_lnet_root(void)
{
        return cfs_param_lnet_root;
}
EXPORT_SYMBOL(cfs_param_get_lnet_root);

int
cfs_param_root_init(void)
{
        cfs_param_root.pe_hs = cfs_hash_create("params_root",
                                               PE_HASH_CUR_BITS,
                                               PE_HASH_MAX_BITS,
                                               PE_HASH_BKT_BITS, 0,
                                               CFS_HASH_MIN_THETA,
                                               CFS_HASH_MAX_THETA,
                                               &pe_hash_ops,
                                               CFS_HASH_DEFAULT |
                                               CFS_HASH_BIGNAME);
        if (cfs_param_root.pe_hs == NULL)
                return -ENOMEM;
        cfs_init_rwsem(&cfs_param_sem);
        cfs_init_rwsem(&cfs_param_root.pe_rw_sem);
        cfs_atomic_set(&cfs_param_root.pe_refcount, 1);
        /* Since lnet_root is used in many places, so create it directly. */
        cfs_param_lnet_root = cfs_param_mkdir("lnet", &cfs_param_root);
        if (cfs_param_lnet_root == NULL)
                return -ENOMEM;

        return 0;
}

void
cfs_param_root_fini(void)
{
        cfs_param_put(cfs_param_lnet_root);
        LASSERTF(cfs_atomic_read(&cfs_param_root.pe_hs->hs_count) == 0,
                 "params_root hash has %d hnodes\n",
                 cfs_atomic_read(&cfs_param_root.pe_hs->hs_count));
        LASSERTF(cfs_atomic_read(&cfs_param_root.pe_refcount) == 1,
                 "params_root has %d refs\n",
                 cfs_atomic_read(&cfs_param_root.pe_refcount));
        if (cfs_param_root.pe_hs != NULL)
                cfs_hash_putref(cfs_param_root.pe_hs);
        cfs_fini_rwsem(&cfs_param_sem);
        cfs_fini_rwsem(&cfs_param_root.pe_rw_sem);
}

cfs_param_cb_data_t *cfs_param_cb_data_alloc(void *data, int flag)
{
        cfs_param_cb_data_t *cb_data = NULL;

        LIBCFS_ALLOC(cb_data, sizeof(*cb_data));
        if (cb_data == NULL) {
                CERROR("No memory for cb data!\n");
                return NULL;
        }
        cb_data->cb_magic = CFS_PARAM_DEBUG_MAGIC;
        cb_data->cb_flag = flag;
        cb_data->cb_data = data;

        return cb_data;
}
EXPORT_SYMBOL(cfs_param_cb_data_alloc);

void cfs_param_cb_data_free(cfs_param_cb_data_t *cb_data, int flag)
{
        if (cb_data != NULL) {
                LASSERTF(cb_data->cb_magic == CFS_PARAM_DEBUG_MAGIC,
                         "cb_magic is %x \n", cb_data->cb_magic);
                LASSERTF((cb_data->cb_flag & flag) ||
                         (cb_data->cb_flag == 0 && flag == 0),
                         "cb_flag is %x flag is %x \n", cb_data->cb_flag,
                          flag);
                LIBCFS_FREE(cb_data, sizeof(*cb_data));
        }
}
EXPORT_SYMBOL(cfs_param_cb_data_free);

/**
 * Basic lookup function
 * Look up an entry by its parent entry and its name
 */
static cfs_param_entry_t *
_param_lookup(const char *name, cfs_param_entry_t *parent)
{
        cfs_param_entry_t *pe = NULL;

        LASSERT(parent != NULL && name != NULL);

        cfs_down_read(&parent->pe_rw_sem);
        pe = cfs_hash_lookup(parent->pe_hs, (void *)name);
        cfs_up_read(&parent->pe_rw_sem);

        return pe;
}

static cfs_param_entry_t *
param_lookup_by_path(const char *pathname, cfs_param_entry_t *entry);
/**
 * Find the target of a symbolic link entry
 */
static cfs_param_entry_t *
find_symlink_target(cfs_param_entry_t *pe)
{
        cfs_param_entry_t *entry = NULL;
        char                      *path;
        char                      *path_temp = NULL;

        LASSERT(pe != NULL);
        LASSERT(S_ISLNK(pe->pe_mode));

        if (pe->pe_data == NULL) {
                CERROR("No symbolic link data found.\n");
                return NULL;
        }

        LIBCFS_ALLOC(path, strlen(pe->pe_data) + 1);
        if (path == NULL) {
                CERROR("No memory for pe_data.\n");
                return NULL;
        }
        strcpy(path, pe->pe_data);
        path_temp = path;
        if (path[0] == '/')
                entry = param_lookup_by_path(++path, NULL);
        else
                entry = param_lookup_by_path(path, pe);
        LIBCFS_FREE(path_temp, strlen(pe->pe_data) + 1);

        return entry;
}

/**
 * Lookup child entry according to its parent entry and its name.
 * If it's a symlink, return its target.
 */
static cfs_param_entry_t *
param_lookup(const char *name, cfs_param_entry_t *parent)
{
        cfs_param_entry_t *pe;
        cfs_param_entry_t *syml_tgt = NULL;

        LASSERT(name != NULL && parent != NULL);

        pe = _param_lookup(name, parent);
        if (pe == NULL)
                return NULL;

        if (S_ISLNK(pe->pe_mode)) {
                syml_tgt = find_symlink_target(pe);
                cfs_param_put(pe);
                pe = syml_tgt;
        }

        return pe;
}

/**
 * Lookup an entry according to path name.
 * If @entry is null, @path is a full path and we will search from root;
 * otherwise, it's a relative path.
 * If result entry is a symlink, return its target.
 */
static cfs_param_entry_t *
param_lookup_by_path(const char *pathname, cfs_param_entry_t *entry)
{
        cfs_param_entry_t *pe;
        cfs_param_entry_t *parent;
        char              *path = (char *)pathname;
        char              *temp;

        if (entry == NULL) {
                /* absolute full path */
                pe = cfs_param_get(&cfs_param_root);
                if (!strncmp(pe->pe_name, path, pe->pe_name_len)) {
                        if (pe->pe_name_len == strlen(path))
                                return pe;
                        path += pe->pe_name_len + 1;
                }
        } else {
                /* relative path */
                pe = cfs_param_get(entry->pe_parent);
        }
        /* separate the path by token '/' */
        while ((temp = strsep(&path, "/")) && (pe != NULL)) {
                parent = pe;
                if (!strcmp(temp, ".."))
                        pe = cfs_param_get(parent->pe_parent);
                else
                        pe = param_lookup(temp, parent);
                cfs_param_put(parent);
        }

        return pe;
}

/**
 * lookup interface for external use (lnet and lustre)
 */
cfs_param_entry_t *
cfs_param_lookup(const char *name, cfs_param_entry_t *parent)
{
        cfs_param_entry_t *pe;
        cfs_param_entry_t *syml_tgt = NULL;

        LASSERT(name != NULL && parent != NULL);

        /* parent can't be a symlink */
        if (S_ISLNK(parent->pe_mode)) {
                syml_tgt = find_symlink_target(parent);
                if (syml_tgt == NULL)
                        return NULL;
                parent = syml_tgt;
        }
        pe = _param_lookup(name, parent);
        if (syml_tgt != NULL)
                cfs_param_put(syml_tgt);

        return pe;
}
EXPORT_SYMBOL(cfs_param_lookup);

/**
 * Add an entry to params tree by it parent and its name.
 */
static cfs_param_entry_t *
_param_add(const char *name, mode_t mode, cfs_param_entry_t *parent)
{
        cfs_param_entry_t *pe = NULL;
        char              *ptr;
        int                len;
        int                rc = 0;

        LASSERT(parent != NULL && name != NULL);

        CDEBUG(D_INFO, "add param %s \n", name);
        /* parent's refcount should be inc/dec by the caller */
        if ((pe = _param_lookup(name, parent))) {
                cfs_param_put(pe);
                CERROR("The entry %s already exists!\n", name);
                return NULL;
        }

        /* alloc for new entry */
        len = sizeof(*pe) + strlen(name) + 1;
        LIBCFS_ALLOC(pe, len);
        if (pe == NULL) {
                CERROR("No memory for new entry %s!\n", name);
                return NULL;
        }
        ptr = (char *)pe + sizeof(*pe);
        strcpy(ptr, name);
        pe->pe_name = ptr;
        pe->pe_name_len = strlen(name);
        pe->pe_hs = NULL;

        /* add hash table or node */
        if (S_ISDIR(mode)) {
                /* create a hash table */
                pe->pe_hs = cfs_hash_create((char *)name,
                                            PE_HASH_CUR_BITS,
                                            PE_HASH_MAX_BITS,
                                            PE_HASH_BKT_BITS, 0,
                                            CFS_HASH_MIN_THETA,
                                            CFS_HASH_MAX_THETA,
                                            &pe_hash_ops,
                                            CFS_HASH_DEFAULT |
                                            CFS_HASH_BIGNAME);
                if (pe->pe_hs == NULL) {
                        CERROR("create pe_hs %s failed", name);
                        GOTO(fail, rc = -ENOMEM);
                }
        }
        pe->pe_data = NULL;
        pe->pe_cb_sfops = NULL;
        cfs_init_rwsem(&pe->pe_rw_sem);
        pe->pe_mode = mode;
        /* Init ref count to 1 to avoid deletion */
        cfs_atomic_set(&pe->pe_refcount, 1);

        cfs_down_write(&parent->pe_rw_sem);
        rc = cfs_hash_add_unique(parent->pe_hs, (char *)name,
                                 &pe->pe_hnode);
        if (!rc)
                pe->pe_parent = parent;
        cfs_up_write(&parent->pe_rw_sem);
fail:
        if (rc) {
                param_free(pe);
                pe = NULL;
        }

        return pe;
}

static cfs_param_entry_t *
param_add(const char *name, mode_t mode, cfs_param_entry_t *parent)
{
        cfs_param_entry_t *syml_tgt = NULL;
        cfs_param_entry_t *pe;

        LASSERT(name != NULL && parent != NULL);

        if (S_ISLNK(parent->pe_mode)) {
                syml_tgt = find_symlink_target(parent);
                if (syml_tgt == NULL)
                        return NULL;
                parent = syml_tgt;
        }
        pe = _param_add(name, mode, parent);
        if (syml_tgt != NULL)
                cfs_param_put(syml_tgt);

        return pe;
}

cfs_param_entry_t *
cfs_param_create(const char *name, mode_t mode, cfs_param_entry_t *parent)
{
        if ((mode & S_IFMT) == 0)
                 mode |= S_IFREG;
        if ((mode & S_IALLUGO) == 0)
                 mode |= S_IRUGO;

        return param_add(name, mode, parent);
}
EXPORT_SYMBOL(cfs_param_create);

cfs_param_entry_t *
cfs_param_mkdir(const char *name, cfs_param_entry_t *parent)
{
        return param_add(name, S_IFDIR | S_IRUGO | S_IXUGO, parent);
}
EXPORT_SYMBOL(cfs_param_mkdir);

static void _param_remove(cfs_param_entry_t *pe);
cfs_param_entry_t *
cfs_param_symlink(const char *name, cfs_param_entry_t *parent,
                  const char *dest)
{
        cfs_param_entry_t *pe;

        pe = param_add(name, S_IFLNK | S_IRUGO | S_IWUGO | S_IXUGO, parent);
        if (pe != NULL) {
                LIBCFS_ALLOC(pe->pe_data, strlen(dest) + 1);
                if (pe->pe_data != NULL) {
                        strcpy(pe->pe_data, dest);
                } else {
                        CERROR("No memory for pe_data.");
                        _param_remove(pe);
                        cfs_param_put(pe);
                        pe = NULL;
                }
        }

        return pe;
}
EXPORT_SYMBOL(cfs_param_symlink);

static int
find_firsthnode_cb(cfs_hash_t *hs, cfs_hash_bd_t *bd, cfs_hlist_node_t *hnode,
                   void *data)
{
        *(cfs_param_entry_t **)data = cfs_hash_get(hs, hnode);

        /* return and break the loop */
        return 1;
}

/**
 * Remove an entry and all of its children from params_tree
 */
static void
_param_remove(cfs_param_entry_t *pe)
{
        cfs_param_entry_t *parent;
        cfs_param_entry_t *rm_pe;
        cfs_param_entry_t *temp = NULL;

        LASSERT(pe != NULL);

        cfs_down_write(&cfs_param_sem);
        parent = pe->pe_parent;
        rm_pe = cfs_param_get(pe);
        while (1) {
                while (rm_pe->pe_hs != NULL) {
                        temp = rm_pe;
                        /* Use cfs_hash_for_each to get the first hnode:
                         * Since in params_tree each hash table is probably
                         * other table's hnode, when we remove one entry,
                         * we have to find its bottom leaf node, and remove
                         * them one by one.
                         */
                        cfs_hash_for_each(temp->pe_hs,
                                          find_firsthnode_cb, &rm_pe);
                        if (rm_pe == temp)
                                break;
                        cfs_param_put(temp);
                }
                temp = rm_pe->pe_parent;
                cfs_down_write(&temp->pe_rw_sem);
                CDEBUG(D_INFO,
                       "remove %s from %s\n", rm_pe->pe_name, temp->pe_name);
                rm_pe = cfs_hash_del_key(temp->pe_hs,
                                          (void *)rm_pe->pe_name);
                cfs_param_put(rm_pe);
                cfs_up_write(&temp->pe_rw_sem);

                if(temp == parent)
                        break;
                rm_pe = cfs_param_get(temp);
        }
        cfs_up_write(&cfs_param_sem);
}

/**
 * Remove an entry and all of its children from params_tree by
 * its parent and its name.
 * If it's a symlink, just remove it, we don't need to find its target.
 */
static void
param_remove(const char *name, cfs_param_entry_t *parent)
{
        cfs_param_entry_t *child;

        LASSERT(parent != NULL && name != NULL);

        if ((child = cfs_param_lookup(name, parent))) {
                _param_remove(child);
                cfs_param_put(child);
        }
}

/**
 * interfaces for external use
 */
void
cfs_param_remove(const char *name, cfs_param_entry_t *pe)
{
        LASSERT(pe != NULL);

        return ((name == NULL) ? _param_remove(pe) : param_remove(name, pe));
}
EXPORT_SYMBOL(cfs_param_remove);

/**
 * These seq operations are help wrappers for using those original
 * seq operation implemented for linux seq file, it should be cleanup
 * when we throw linux /proc totally */
struct param_seq_args {
        cfs_param_dentry_t *psa_pd;
        cfs_param_file_t   *psa_file;
        cfs_proc_inode_t   *psa_inode;
        char               *psa_buf;
        void               *psa_value;
};

static void
param_seq_finiargs(struct param_seq_args *args)
{
        if (args->psa_inode)
                LIBCFS_FREE(args->psa_inode, sizeof(cfs_proc_inode_t));
        if (args->psa_file)
                LIBCFS_FREE(args->psa_file, sizeof(cfs_param_file_t));
        if (args->psa_pd)
                LIBCFS_FREE(args->psa_pd, sizeof(cfs_param_dentry_t));
        if (args->psa_buf)
                LIBCFS_FREE(args->psa_buf, CFS_PAGE_SIZE);
}

static int
param_seq_initargs(struct param_seq_args *args)
{
        int rc = 0;

        LIBCFS_ALLOC(args->psa_inode, sizeof(cfs_proc_inode_t));
        if (!args->psa_inode)
                GOTO(out, rc = -ENOMEM);

        LIBCFS_ALLOC(args->psa_file, sizeof(cfs_param_file_t));
        if (!args->psa_file)
                GOTO(out, rc = -ENOMEM);

        LIBCFS_ALLOC(args->psa_pd, sizeof(cfs_param_dentry_t));
        if (!args->psa_pd)
                GOTO(out, rc = -ENOMEM);

        LIBCFS_ALLOC(args->psa_buf, CFS_PAGE_SIZE);
        if (!args->psa_buf)
                GOTO(out, rc = -ENOMEM);
out:
        if (rc)
                param_seq_finiargs(args);
        RETURN(rc);
}

static void *
param_seq_start(cfs_param_entry_t *pe, struct param_seq_args *args,
                loff_t *loff)
{
        cfs_seq_file_t *seqf = cfs_file_private(args->psa_file);
        cfs_seq_ops_t *seqop = seqf->op;

        LASSERT(seqop != NULL && seqop->start != NULL);
        args->psa_value = seqop->start(seqf, loff);

        return args->psa_value;
}

static void
param_seq_stop(cfs_param_entry_t *pe, struct param_seq_args *args)
{
        cfs_seq_file_t *seqf = cfs_file_private(args->psa_file);
        cfs_seq_ops_t *seqop = seqf->op;

        LASSERT(seqop != NULL && seqop->stop != NULL);
        seqop->stop(seqf, args->psa_value);

        return;
}

static int
param_data_pack(char *buf, int buf_len, enum cfs_param_value_type type,
                const char *name, const char *unit);
static int
param_seq_show(cfs_param_entry_t *pe, struct param_seq_args *args,
               char **output)
{
        cfs_seq_file_t *seqf = cfs_file_private(args->psa_file);
        cfs_seq_ops_t *seqop = seqf->op;
        int rc = 0;

        LASSERT(seqop != NULL && seqop->show != NULL);
        seqf->count = 0;
        memset(seqf->buf, 0, CFS_PAGE_SIZE);
        rc = seqop->show(seqf, args->psa_value);
        if (rc < 0)
                return rc;

        if (strlen(seqf->buf) > 0) {
                rc = param_data_pack(seqf->buf, seqf->size, CFS_PARAM_STR,
                                            NULL, NULL);
                if ( rc < 0)
                        return rc;
        }
        *output = seqf->buf;

        return rc;
}

static void *
param_seq_next(cfs_param_entry_t *pe, struct param_seq_args *args,
               loff_t *loff)
{
        cfs_seq_file_t *seqf = cfs_file_private(args->psa_file);
        cfs_seq_ops_t *seqop = seqf->op;

        LASSERT(seqop != NULL && seqop->next != NULL);
        args->psa_value = seqop->next(seqf, args->psa_value, loff);

        return args->psa_value;
}

static int
param_seq_open(cfs_param_entry_t *pe, struct param_seq_args *args,
               char *buf, int count)
{
        cfs_proc_inode_t     *proc_inode = args->psa_inode;
        cfs_inode_t          *inode = &cfs_proci_inode(proc_inode);
        cfs_param_file_ops_t *sfops = pe->pe_cb_sfops;
        cfs_seq_file_t       *seqf;
        int                   rc = 0;

        cfs_dentry_data(args->psa_pd) = pe->pe_data;
        cfs_proc_inode_pde(proc_inode) = args->psa_pd;

        LASSERT(sfops != NULL);
        rc = sfops->open(inode, args->psa_file);
        if (rc)
                RETURN(rc);

        seqf = cfs_file_private(args->psa_file);
        LASSERT(seqf != NULL);
        seqf->buf = args->psa_buf;
        if (buf)
                memcpy(seqf->buf, buf, count);

        /* seqf->size is user buffer size, instead of seqf->buf */
        seqf->size = count;
        RETURN(rc);
}

int
cfs_param_seq_open(cfs_param_file_t *file, cfs_seq_ops_t *op)
{
        cfs_seq_file_t *p = cfs_file_private(file);

        if (!p) {
                LIBCFS_ALLOC(p, sizeof(*p));
                if (!p)
                        return -ENOMEM;
                cfs_file_private(file) = p;
        }
        memset(p, 0, sizeof(*p));
        p->op = op;

        return 0;
}
EXPORT_SYMBOL(cfs_param_seq_open);

int
cfs_param_seq_fopen(cfs_inode_t *inode, cfs_param_file_t *file, cfs_seq_ops_t *op)
{
        cfs_param_dentry_t *dp = CFS_PDE(inode);
        cfs_seq_file_t     *sf;
        int                 rc;

        LPROCFS_ENTRY_AND_CHECK(dp);
        rc = cfs_seq_open(file, op);
        if (rc) {
                LPROCFS_EXIT();
        } else {
                sf = cfs_file_private(file);
                cfs_seq_private(sf) = cfs_dentry_data(dp);
        }

        return rc;
}
EXPORT_SYMBOL(cfs_param_seq_fopen);

static void
param_seq_release(cfs_param_entry_t *pe, struct param_seq_args *args)
{
        cfs_proc_inode_t     *proc_inode = args->psa_inode;
        cfs_inode_t          *inode = &cfs_proci_inode(proc_inode);
        cfs_param_file_ops_t *sfops = pe->pe_cb_sfops;
        cfs_seq_file_t       *seqf;

        seqf = cfs_file_private(args->psa_file);
        LASSERT(seqf != NULL && sfops != NULL);
        seqf->buf = NULL;
        seqf->size = 0;
        sfops->release(inode, args->psa_file);

        return;
}

int
cfs_param_seq_release(cfs_inode_t *inode, cfs_param_file_t *file)
{
        cfs_seq_file_t *seqf = cfs_file_private(file);

        LASSERT(seqf != NULL);
        /**
         * Note: it will release seqf->buf in seq_release,
         * but params_tree already set seqf->buf = NULL,
         * In linux, it is ok. Ok for other platform?
         **/
#ifdef LPROCFS
        seq_release(inode, file);
#else
        LIBCFS_FREE(seqf, sizeof(cfs_seq_file_t));
#endif
        LPROCFS_EXIT();
        return 0;
}
EXPORT_SYMBOL(cfs_param_seq_release);

int
cfs_param_seq_puts(cfs_seq_file_t *seq, const char *s)
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
EXPORT_SYMBOL(cfs_param_seq_puts);

int
cfs_param_seq_putc(cfs_seq_file_t *seq, const char c)
{
        if (seq->count < seq->size) {
                seq->buf[seq->count++] = c;
                return 0;
        }
        return -1;
}
EXPORT_SYMBOL(cfs_param_seq_putc);

int
cfs_param_seq_printf(cfs_seq_file_t *seq, const char *fmt, ...)
{
        va_list args;
        int     len;

        /* Got this code from seq_printf*/
        if (seq->count < seq->size) {
                va_start(args, fmt);
                len = cfs_vsnprintf(seq->buf + seq->count,
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
EXPORT_SYMBOL(cfs_param_seq_printf);

static int
param_seq_read(char *ubuf, loff_t *loff, int count, int *eof,
               cfs_param_entry_t *pe)
{
        struct param_seq_args args = {0};
        int left_bytes = count;
        int bytes;
        char *output;
        int rc;
        void *p;

        rc = param_seq_initargs(&args);
        if (rc) {
                CERROR("Init seq failed for %s rc %d\n", pe->pe_name, rc);
                RETURN(rc);
        }

        rc = param_seq_open(pe, &args, NULL, count);
        if (rc) {
                CERROR("seq open failed for %s rc %d \n", pe->pe_name, rc);
                GOTO(free, rc);
        }

        p = param_seq_start(pe, &args, loff);
        if (p == NULL || IS_ERR(p)) {
                *eof = 1;
                rc = PTR_ERR(p);
                GOTO(out, rc);
        }

        /* we need at least one record in buffer */
        bytes = param_seq_show(pe, &args, &output);
        if (bytes < 0)
                GOTO(out, rc = bytes);
        if (bytes > left_bytes)
                GOTO(out, rc = -ENOSPC);
        if (cfs_copy_to_user(ubuf, output, bytes))
                GOTO(out, rc = -EFAULT);
        left_bytes -= bytes;
        ubuf += bytes;

        /* get more */
        while((left_bytes > 0)) {
                char *format_buf;

                p = param_seq_next(pe, &args, loff);
                if (p == NULL || IS_ERR(p)) {
                        *eof = 1;
                        rc = PTR_ERR(p);
                        break;
                }

                bytes = param_seq_show(pe, &args, &format_buf);
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
        rc = (rc < 0) ? rc : (count - left_bytes);
        param_seq_stop(pe, &args);
        param_seq_release(pe, &args);
free:
        param_seq_finiargs(&args);
        return rc;
}

static int
_param_seq_write(cfs_param_entry_t *pe, struct param_seq_args *args,
                 const char *buf, int count, loff_t *off)
{
        cfs_param_file_t     *file = args->psa_file;
        cfs_param_file_ops_t *sfops = pe->pe_cb_sfops;

        LASSERT(sfops != NULL);
        if (sfops->write != NULL)
                return sfops->write(file, buf, (unsigned long)count, off);

        return -EIO;
}

static int
param_seq_write(char *buf, int count, cfs_param_entry_t *pe)
{
        struct param_seq_args args = {0};
        loff_t  off = 0;
        int rc;

        rc = param_seq_initargs(&args);
        if (rc)
                RETURN(rc);

        rc = param_seq_open(pe, &args, buf, count);
        if (rc) {
                CERROR("seq open failed for %s rc %d \n",
                        pe->pe_name, rc);
                GOTO(free, rc);
        }

        rc = _param_seq_write(pe, &args, buf, count, &off);

        param_seq_release(pe, &args);
free:
        param_seq_finiargs(&args);
        return rc;

}

static int
param_normal_read(char *buf, loff_t *ppos, int count, int *eof,
                  cfs_param_entry_t *entry)
{
        char *page = NULL;
        char *start = NULL;
        int   left_bytes = count;
        int   bytes;
        int   rc = 0;
        off_t pos = (off_t)*ppos;

        LIBCFS_ALLOC(page, CFS_PAGE_SIZE);
        if (page == NULL)
                return -ENOMEM;
        while((left_bytes > 0) && !(*eof)) {
                /* suppose *eof = 1 by default so that we don't need to
                 * set it in each read_cb function. */
                *eof = 1;
                /* read the param value */
                if (entry->pe_cb_read != NULL)
                        bytes = entry->pe_cb_read(page, &start, pos, count,
                                                  eof, entry->pe_data);
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

#define IS_SEQ_LPE(pe) (pe->pe_cb_sfops != NULL)
int
cfs_param_kread(const char *path, char *buf, int nbytes, loff_t *ppos,
                int *eof)
{
        cfs_param_entry_t *entry;
        int                rc = 0;
        int                count;

        if (path == NULL) {
                CERROR("path is null.\n");
                return -EINVAL;
        }
        /* lookup the entry according to its pathname */
        entry = param_lookup_by_path(path, NULL);
        if (entry == NULL)
                return -ENOENT;

        *eof = 0;
        count = (CFS_PAGE_SIZE > nbytes) ? nbytes : CFS_PAGE_SIZE;

        cfs_down_read(&entry->pe_rw_sem);
        if (entry->pe_mode & S_IRUSR) {
                if (IS_SEQ_LPE(entry))
                        rc = param_seq_read(buf, ppos, count, eof, entry);
                else if (entry->pe_cb_read != NULL)
                        rc = param_normal_read(buf, ppos, count, eof, entry);
                else
                        rc = -EINVAL;
        } else {
                rc = -EPERM;
        }
        cfs_up_read(&entry->pe_rw_sem);

        cfs_param_put(entry);
        return rc;
}

int
cfs_param_kwrite(const char *path, char *buf, int count, int force_write)
{
        cfs_param_entry_t *entry;
        int                rc = 0;

        if (path == NULL) {
                CERROR("path is null.\n");
                return -EINVAL;
        }
        entry = param_lookup_by_path(path, NULL);
        if (entry == NULL)
                return -ENOENT;

        cfs_down_write(&entry->pe_rw_sem);
        if (entry->pe_mode & S_IWUSR || force_write) {
                if (IS_SEQ_LPE(entry))
                        rc = param_seq_write(buf, count, entry);
                else if (entry->pe_cb_write != NULL)
                        rc = entry->pe_cb_write(NULL, buf, count,
                                                entry->pe_data);
                else
                        rc = -EINVAL;
        } else {
                rc = -EPERM;
        }
        cfs_up_write(&entry->pe_rw_sem);

        cfs_param_put(entry);
        return rc;
}

/**
 * List all the subdirs of an entry by ioctl.
 */
struct param_list_cb_data {
        char *buf;
        int   pos;
        int   left;
};

static int
param_list_cb(cfs_hash_t *hs, cfs_hash_bd_t *bd, cfs_hlist_node_t *hnode,
              void *data)
{
        struct param_list_cb_data *cb_data = data;
        cfs_param_entry_t *pe = cfs_hash_object(hs, hnode);
        cfs_param_info_t  *pi;
        int len = sizeof(struct cfs_param_info);

        LASSERT(pe != NULL);

        if (cb_data->left < len) {
                CERROR("Have no enough buffer for list_param.\n");
                return -ENOMEM;
        }
        pi = (cfs_param_info_t *)(cb_data->buf + cb_data->pos);
        /* copy name_len, name, mode */
        pi->pi_name_len = pe->pe_name_len;
        pi->pi_mode = pe->pe_mode;
        strncpy(pi->pi_name, pe->pe_name, pe->pe_name_len);
        pi->pi_name[pi->pi_name_len] = '\0';
        cb_data->pos += len;
        cb_data->left -= len;

        CDEBUG(D_INFO, "copy \"%s\" out, pos=%d\n", pe->pe_name, cb_data->pos);
        return 0;
}

static int
param_list(cfs_param_entry_t *parent, char *kern_buf, int kern_buflen)
{
        struct param_list_cb_data cb_data;
        int                       rc = 0;

        if (parent == NULL)
                return -EINVAL;

        cb_data.buf = kern_buf;
        cb_data.pos = 0;
        cb_data.left = kern_buflen;
        /* we pass kern_buflen here, because we should avoid the real dir size
         * is larger than we have. */
        cfs_down_read(&parent->pe_rw_sem);
        cfs_hash_for_each(parent->pe_hs, param_list_cb, &cb_data);
        cfs_up_read(&parent->pe_rw_sem);

        return rc;
}

int cfs_param_klist(const char *parent_path, char *user_buf, int *buflen)
{
        /* In kernel space, do like readdir
         * In user space, match these entries pathname with the pattern */
        cfs_param_entry_t *parent;
        char *kern_buf = NULL;
        int datalen = 0;
        int num;
        int rc;

        if (user_buf == NULL) {
                CERROR("The buffer is null.\n");
                GOTO(out, rc = -EINVAL);
        }
        if (parent_path == NULL)
                /* list the entries under params_root */
                parent = cfs_param_get(&cfs_param_root);
        else
                parent = param_lookup_by_path(parent_path, NULL);
        if (parent == NULL) {
                CERROR("The parent entry %s doesn't exist.\n",
                       parent_path);
                GOTO(out, rc = -ENOENT);
        }
        /* estimate if buflen is big enough */
        num = cfs_atomic_read(&(parent->pe_hs->hs_count));
        if (num == 0)
                GOTO(parent, rc = 0);
        datalen = num * sizeof(struct cfs_param_info);
        if (datalen > *buflen)
                GOTO(parent, rc = -ENOMEM);
        /* list the entries */
        LIBCFS_ALLOC(kern_buf, datalen);
        if (kern_buf == NULL) {
                CERROR("kernel can't alloc %d bytes.\n", datalen);
                *buflen = 0;
                GOTO(parent, rc = -ENOMEM);
        }
        rc = param_list(parent, kern_buf, datalen);
        if (cfs_copy_to_user(user_buf, kern_buf, datalen))
                rc = -EFAULT;
        LIBCFS_FREE(kern_buf, datalen);
parent:
        cfs_param_put(parent);
out:
        *buflen = datalen;
        return (rc < 0 ? rc : num);
}

int
cfs_param_intvec_write(cfs_param_file_t *filp, const char *buffer,
                       unsigned long count, void *data)
{
        unsigned int  temp;
        const char   *pbuf = buffer;

        if (*pbuf == '-') {
                pbuf ++;
                temp = -simple_strtoul(pbuf, NULL, 10);
        } else if (pbuf[0] == '0' && toupper(pbuf[1]) == 'X') {
                temp = simple_strtoul(pbuf, NULL, 16);
        } else {
                temp = simple_strtoul(pbuf, NULL, 10);
        }
        memcpy(((cfs_param_cb_data_t *)data)->cb_data, &temp, sizeof(temp));

        return count;
}
EXPORT_SYMBOL(cfs_param_intvec_write);

int
cfs_param_intvec_read(char *page, char **start, off_t off, int count,
                      int *eof, void *data)
{
        unsigned int *temp = ((cfs_param_cb_data_t *)data)->cb_data;

        return cfs_param_snprintf(page, count, data, CFS_PARAM_S32, NULL, *temp);
}
EXPORT_SYMBOL(cfs_param_intvec_read);

int
cfs_param_longvec_write(cfs_param_file_t *filp, const char *buffer,
                        unsigned long count, void *data)
{
        unsigned long temp;
        const char   *pbuf = buffer;

        if (*pbuf == '-') {
                pbuf ++;
                temp = -simple_strtoul(pbuf, NULL, 10);
        } else if (pbuf[0] == '0' && toupper(pbuf[1]) == 'X') {
                temp = simple_strtoul(pbuf, NULL, 16);
        } else {
                temp = simple_strtoul(pbuf, NULL, 10);
        }
        memcpy(((cfs_param_cb_data_t *)data)->cb_data, &temp, sizeof(temp));

        return count;
}
EXPORT_SYMBOL(cfs_param_longvec_write);

int
cfs_param_longvec_read(char *page, char **start, off_t off, int count,
                       int *eof, void *data)
{
        unsigned long *temp = ((cfs_param_cb_data_t *)data)->cb_data;

        return cfs_param_snprintf(page, count, data, CFS_PARAM_S64,
                                  NULL, *temp);
}
EXPORT_SYMBOL(cfs_param_longvec_read);

int
cfs_param_string_write(cfs_param_file_t *filp, const char *buffer,
                       unsigned long count, void *data)
{
        memcpy(((cfs_param_cb_data_t *)data)->cb_data, buffer, count);
        return count;
}
EXPORT_SYMBOL(cfs_param_string_write);

int
cfs_param_string_read(char *page, char **start, off_t off, int count,
                      int *eof, void *data)
{
        return cfs_param_snprintf(page, count, data, CFS_PARAM_STR, "%s",
                                  ((cfs_param_cb_data_t *)data)->cb_data);
}
EXPORT_SYMBOL(cfs_param_string_read);

/**
 * add params in sysctl table to params_tree
 */
static void
param_sysctl_unregister(cfs_param_sysctl_table_t *table,
                        cfs_param_entry_t *parent)
{
        if (parent == NULL)
                return;
        for (; table->name != NULL; table ++)
                param_remove(table->name, parent);
}

static int
param_sysctl_register(cfs_param_sysctl_table_t *table,
                      cfs_param_entry_t *parent)
{
        cfs_param_entry_t *pe = NULL;
        int                rc = 0;

        if (parent == NULL)
                return -EINVAL;
        for (; table->name != NULL; table ++) {
                pe = cfs_param_create(table->name, table->mode, parent);
                if (pe == NULL)
                        GOTO(out, rc = -ENOMEM);
                pe->pe_data = CFS_ALLOC_PARAMDATA(table->data);
                if (pe->pe_data == NULL)
                        GOTO(out, rc = -ENOMEM);
                pe->pe_cb_read = table->read;
                pe->pe_cb_write = table->write;
                cfs_param_put(pe);
        }
out:
        if (rc != 0)
                param_sysctl_unregister(table, parent);
        return 0;
}

int
cfs_param_sysctl_init(char *mod_name, cfs_param_sysctl_table_t *table,
                      cfs_param_entry_t *parent)
{
        cfs_param_entry_t *pe;
        int                rc = 0;

        pe = cfs_param_lookup(mod_name, parent);
        if (pe == NULL)
                pe = cfs_param_mkdir(mod_name, parent);
        if (pe != NULL) {
                rc = param_sysctl_register(table, pe);
                cfs_param_put(pe);
        } else {
                CERROR("failed to register ctl_table to %s/%s.\n",
                       parent->pe_name, mod_name);
                rc = -ENOMEM;
        }

        return rc;
}
EXPORT_SYMBOL(cfs_param_sysctl_init);

void
cfs_param_sysctl_fini(char *mod_name, cfs_param_entry_t *parent)
{
        cfs_param_remove(mod_name, parent);
}
EXPORT_SYMBOL(cfs_param_sysctl_fini);

/**
 * Since the usr buffer addr has been mapped to the kernel space through
 * libcfs_ioctl_pack(), we don't need cfs_copy_from_user.
 * Instead, just call memcpy.
 */
int
cfs_param_copy(int flag, char *dest, const char *src, int count)
{
        if (flag & CFS_PARAM_ACCESS)
                memcpy(dest, src, count);
        else if (cfs_copy_from_user(dest, src, count))
                return -EFAULT;
        return 0;
}
EXPORT_SYMBOL(cfs_param_copy);

static int
get_value_len(enum cfs_param_value_type type, char *buf)
{
        switch (type) {
                case CFS_PARAM_S8:
                        return 1;
                case CFS_PARAM_S16:
                        return 2;
                case CFS_PARAM_S32:
                        return 4;
                case CFS_PARAM_S64:
                        return 8;
                case CFS_PARAM_U8:
                        return 1;
                case CFS_PARAM_U16:
                        return 2;
                case CFS_PARAM_U32:
                        return 4;
                case CFS_PARAM_U64:
                        return 8;
                case CFS_PARAM_DB:
                case CFS_PARAM_STR:
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
print_num(char *page, va_list args, enum cfs_param_value_type type)
{
        switch (type) {
                case CFS_PARAM_S8:
                case CFS_PARAM_S16:
                case CFS_PARAM_S32:
                        memcpy_num(page, args, int);
                        break;
                case CFS_PARAM_S64:
                        memcpy_num(page, args, long long);
                        break;
                case CFS_PARAM_U8:
                case CFS_PARAM_U16:
                case CFS_PARAM_U32:
                        memcpy_num(page, args, __u32);
                        break;
                case CFS_PARAM_U64:
                        memcpy_num(page, args, __u64);
                        break;
                default:
                        return;
        }
}

int
cfs_param_snprintf_common(char *page, int count, void *cb_data,
                          enum cfs_param_value_type type,
                          const char *name, const char *unit,
                          const char *format, ...)
{
        va_list args;
        int rc = 0;
        int done = 0;

        LASSERT(((cfs_param_cb_data_t*)cb_data)->cb_magic ==
                CFS_PARAM_DEBUG_MAGIC);
        va_start(args, format);
        if (((cfs_param_cb_data_t*)cb_data)->cb_flag & CFS_PARAM_ACCESS) {
                switch (type) {
                case CFS_PARAM_DB:
                        /* currently, we treate CFS_PARAM_DB as CFS_PARAM_STR */
                case CFS_PARAM_STR:
                        if (format == NULL)
                                /* sometimes value has been printed to page */
                                done = 1;
                        else
                                rc = cfs_vsnprintf(page, count, format, args);
                        if (done || rc > 0)
                                rc = param_data_pack(page, count, CFS_PARAM_STR,
                                                     name, unit);
                        break;
                default:
                        /* else, they are numbers */
                        print_num(page, args, type);
                        rc = param_data_pack(page, count, type, name, unit);
                        break;
                }
        } else {
                if (format != NULL) {
                        if (name != NULL)
                                rc = cfs_snprintf(page, count, "%s", name);
                        rc += cfs_vsnprintf(page + rc, count - rc,
                                            format, args);
                        if (unit != NULL)
                                rc += cfs_snprintf(page + rc, count - rc,
                                                   "%s", unit);
                } else {
                        rc = get_value_len(type, page);
                }
        }
        va_end(args);

        return rc;
}
EXPORT_SYMBOL(cfs_param_snprintf_common);

/**
 * Validate param_data
 */
static int
param_is_invalid(cfs_param_data_t *data)
{
        if (data->pd_name != NULL && data->pd_name_len == 0) {
                CERROR("pve_name pointer but 0 length\n");
                return 1;
        }
        if (data->pd_unit != NULL && data->pd_unit_len == 0) {
                CERROR("pve_unit pointer but 0 length\n");
                return 1;
        }
        if (data->pd_value != NULL && data->pd_value_len == 0) {
                CERROR("pve_value pointer but 0 length\n");
                return 1;
        }
        if (data->pd_name_len > 0 && data->pd_name == NULL) {
                CERROR("pve_name_len nozero but no name pointer\n");
                return 1;
        }
        if (data->pd_unit_len > 0 && data->pd_unit == NULL) {
                CERROR("pve_unit_len nozero but no unit pointer\n");
                return 1;
        }
        if (data->pd_value_len > 0 && data->pd_value == NULL) {
                CERROR("pve_value_len nozero but no value pointer\n");
                return 1;
        }
        if (data->pd_name_len > 0 &&
            data->pd_name[data->pd_name_len] != '\0') {
                CERROR ("pve_name not 0 terminated\n");
                return 1;
        }
        if (data->pd_unit_len > 0 &&
            data->pd_unit[data->pd_unit_len] != '\0') {
                CERROR ("pve_unit not 0 terminated\n");
                return 1;
        }
        if (data->pd_type == CFS_PARAM_STR || data->pd_type == CFS_PARAM_DB) {
                if (data->pd_value_len > 0 &&
                    data->pd_value[data->pd_value_len] != '\0') {
                        CERROR ("pve_value(string) not 0 terminated\n");
                        return 1;
                }
        }

        return 0;
}

/**
 * Two steps:
 * 1) copy the param_value stored in buffer out
 * 2) organize cfs_param_data struct and pack it back into the buffer
 */
static int
param_data_pack(char *buf, int buf_len,
                enum cfs_param_value_type type,
                const char *name, const char *unit)
{
        cfs_param_data_t    *data = NULL;
        char                *ptr;
        char                *value = NULL;
        int                  data_len = 0;
        int                  value_len = 0;
        int                  rc = 0;

        /* we store param_value into buf, copy it out first */
        if (buf == NULL) {
                CERROR("buf is null.\n");
                return -ENOMEM;
        }
        value_len = get_value_len(type, buf);
        if (value_len > 0) {
                LIBCFS_ALLOC(value, value_len);
                if (value == NULL)
                        return -ENOMEM;
                memcpy(value, buf, value_len);
        }
        /* check buf_len */
        data_len = sizeof(struct cfs_param_data);
        if (name != NULL)
                data_len += strlen(name) + 1;
        if (unit != NULL)
                data_len += strlen(unit) + 1;
        data_len += value_len;
        if (data_len >= buf_len) {
                CERROR("max_buflen(%d) < pvelen (%d)\n", buf_len, data_len);
                GOTO(out, rc = -ENOMEM);
        }
        /* create cfs_param_data struct */
        memset(buf, 0, buf_len);
        data = (cfs_param_data_t *)buf;
        ptr = data->pd_bulk;
        data->pd_type = type;
        if (name != NULL) {
                data->pd_name_len = strlen(name);
                strcpy(ptr, name);
                data->pd_name = ptr;
                ptr += strlen(name) + 1;
        } else {
                data->pd_name = NULL;
                data->pd_name_len = 0;
        }
        if (unit != NULL) {
                data->pd_unit_len = strlen(unit);
                strcpy(ptr, unit);
                data->pd_unit = ptr;
                ptr += strlen(unit) + 1;
        } else {
                data->pd_unit = NULL;
                data->pd_unit_len = 0;
        }
        data->pd_value_len = value_len;
        if (value_len > 0) {
                memcpy(ptr, value, value_len);
                data->pd_value = ptr;
        }

        if (param_is_invalid(data))
                rc = -EINVAL;
out:
        if (value_len > 0)
                LIBCFS_FREE(value, value_len);

        return rc < 0 ? rc : data_len;
}

#endif /* __KERNEL__ */
