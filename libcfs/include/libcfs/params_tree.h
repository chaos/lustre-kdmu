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
 * API and structure definitions for params_tree.
 *
 * Author: LiuYing <emoly.liu@oracle.com>
 */
#ifndef __PARAMS_TREE_H__
#define __PARAMS_TREE_H__

#ifndef EXPORT_SYMTAB
# define EXPORT_SYMTAB
#endif

#include <libcfs/libcfs.h>

#undef LPROCFS
#if (defined(__KERNEL__) && defined(CONFIG_PROC_FS))
# define LPROCFS
#endif

#ifdef LPROCFS
typedef cfs_module_t                           *cfs_param_module_t;
typedef struct file                             cfs_param_file_t;
typedef struct inode                            cfs_inode_t;
typedef struct proc_inode                       cfs_proc_inode_t;
typedef struct seq_file                         cfs_seq_file_t;
typedef struct seq_operations                   cfs_seq_ops_t;
typedef struct file_operations                  cfs_param_file_ops_t;
typedef struct proc_dir_entry                   cfs_param_dentry_t;
typedef struct poll_table_struct                cfs_poll_table_t;
#define CFS_PARAM_MODULE                        THIS_MODULE
#define CFS_PDE(value)                          PDE(value)
#define cfs_file_private(file)                  (file->private_data)
#define cfs_inode_private(inode)                (inode->i_private)
#define cfs_seq_private(seq)                    (seq->private)
#define cfs_dentry_data(dentry)                 (dentry->data)
#define cfs_proc_inode_pde(proc_inode)          (proc_inode->pde)
#define cfs_proci_inode(proc_inode)             (proc_inode->vfs_inode)
#define cfs_seq_open(file, op)                  seq_open(file, op)
#define cfs_seq_read                            seq_read
#define cfs_seq_lseek                           seq_lseek
#define cfs_seq_printf(seq, format, ...)        seq_printf(seq, format,  \
                                                           ## __VA_ARGS__)
#define cfs_seq_puts(seq, s)                    seq_puts(seq, s)
#define cfs_seq_putc(seq, c)                    seq_putc(seq, c)

/* in lprocfs_stat.c, to protect the private data for proc entries */
extern cfs_rw_semaphore_t       _lprocfs_lock;
#define LPROCFS_ENTRY()                 \
do {                                    \
        cfs_down_read(&_lprocfs_lock);  \
} while(0)
#define LPROCFS_EXIT()                  \
do {                                    \
        cfs_up_read(&_lprocfs_lock);    \
} while(0)

#ifdef HAVE_PROCFS_DELETED
static inline
int LPROCFS_ENTRY_AND_CHECK(struct proc_dir_entry *dp)
{
        LPROCFS_ENTRY();
        if ((dp)->deleted) {
                LPROCFS_EXIT();
                return -ENODEV;
        }
        return 0;
}
#else /* !HAVE_PROCFS_DELETED*/
static inline
int LPROCFS_ENTRY_AND_CHECK(struct proc_dir_entry *dp)
{
        LPROCFS_ENTRY();
        return 0;
}
#endif /* HAVE_PROCFS_DELETED */

#define LPROCFS_WRITE_ENTRY()           \
do {                                    \
        cfs_down_write(&_lprocfs_lock); \
} while(0)
#define LPROCFS_WRITE_EXIT()            \
do {                                    \
        cfs_up_write(&_lprocfs_lock);   \
} while(0)

#else /* !LPROCFS */

struct cfs_seq_operations;
typedef struct cfs_seq_file {
        char                      *buf;
        size_t                     size;
        size_t                     from;
        size_t                     count;
        loff_t                     index;
        loff_t                     version;
        cfs_mutex_t                lock;
        struct cfs_seq_operations *op;
        void                      *private;
} cfs_seq_file_t;

typedef struct cfs_seq_operations {
        void *(*start) (cfs_seq_file_t *m, loff_t *pos);
        void  (*stop) (cfs_seq_file_t *m, void *v);
        void *(*next) (cfs_seq_file_t *m, void *v, loff_t *pos);
        int   (*show) (cfs_seq_file_t *m, void *v);
} cfs_seq_ops_t;

typedef struct cfs_param_inode {
        void                   *param_private;
} cfs_inode_t;

typedef struct cfs_param_file {
        void                   *param_private;
        loff_t                  param_pos;
        unsigned int            param_flags;
} cfs_param_file_t;

typedef struct cfs_param_dentry {
        void                   *param_data;
} cfs_param_dentry_t;

typedef struct cfs_proc_inode {
        cfs_param_dentry_t     *param_pde;
        cfs_inode_t             param_inode;
} cfs_proc_inode_t;

typedef void *cfs_param_module_t;
typedef void *cfs_poll_table_t;

typedef struct cfs_param_file_ops {
        cfs_param_module_t owner;
        int (*open) (cfs_inode_t *, cfs_param_file_t *);
        loff_t (*llseek)(cfs_param_file_t *, loff_t, int);
        int (*release) (cfs_inode_t *, cfs_param_file_t *);
        unsigned int (*poll) (cfs_param_file_t *, cfs_poll_table_t *);
        ssize_t (*write) (cfs_param_file_t *, const char *, size_t, loff_t *);
        ssize_t (*read)(cfs_param_file_t *, char *, size_t, loff_t *);
} cfs_param_file_ops_t;
typedef cfs_param_file_ops_t *cfs_lproc_filep_t;

#define LPROCFS_ENTRY()             do {} while(0)
#define LPROCFS_EXIT()              do {} while(0)
static inline
int LPROCFS_ENTRY_AND_CHECK(cfs_param_dentry_t *dp)
{
        LPROCFS_ENTRY();
        return 0;
}
#define LPROCFS_WRITE_ENTRY()       do {} while(0)
#define LPROCFS_WRITE_EXIT()        do {} while(0)

static inline
cfs_proc_inode_t *FAKE_PROC_I(const cfs_inode_t *inode)
{
        return container_of(inode, cfs_proc_inode_t, param_inode);
}

static inline
cfs_param_dentry_t *FAKE_PDE(cfs_inode_t *inode)
{
        return FAKE_PROC_I(inode)->param_pde;
}

#define CFS_PARAM_MODULE                     NULL
#define CFS_PDE(value)                       FAKE_PDE(value)
#define cfs_proci_inode(proc_inode)          (proc_inode->param_inode)
#define cfs_file_private(file)               (file->param_private)
#define cfs_seq_private(seq)                 (seq->private)
#define cfs_inode_private(inode)             (inode->param_private)
#define cfs_dentry_data(dentry)              (dentry->param_data)
#define cfs_proc_inode_pde(proc_inode)       (proc_inode->param_pde)
#define cfs_seq_open(file, op)               cfs_param_seq_open(file, op)
#define cfs_seq_read                         NULL
#define cfs_seq_lseek                        NULL
#define cfs_seq_printf(seq, format, ...)     cfs_param_seq_printf(seq, \
                                             format, ## __VA_ARGS__)
#define cfs_seq_puts(seq, s)                 cfs_param_seq_puts(seq, s)
#define cfs_seq_putc(seq, c)                 cfs_param_seq_putc(seq, c)

#endif /* LPROCFS */

/* params_tree definitions */
#define PE_HASH_CUR_BITS       8
#define PE_HASH_BKT_BITS       8
#define PE_HASH_MAX_BITS       24

typedef int (cfs_param_read_t)(char *page, char **start, off_t off,
                               int count, int *eof, void *data);
typedef int (cfs_param_write_t)(cfs_param_file_t *filp, const char *buffer,
                                unsigned long count, void *data);
typedef struct cfs_param_entry {
        /* hash table members */
        cfs_hash_t             *pe_hs;      /* =dir, someone's parent */
        cfs_rw_semaphore_t      pe_rw_sem;
        /* hash node members */
        cfs_hlist_node_t        pe_hnode;   /* =leaf_entry, someone's child */
        cfs_param_read_t       *pe_cb_read;
        cfs_param_write_t      *pe_cb_write;
        cfs_param_file_ops_t   *pe_cb_sfops;/* used only by seq operation */
        void                   *pe_data;    /* if entry is a symlink, it
                                               stores the path to the target;
                                               otherwise, it points to a
                                               libcfs_param_cb_data */
        /* common members */
        struct cfs_param_entry *pe_parent;
        cfs_atomic_t            pe_refcount;/* pe reference count */
        const char             *pe_name;
        int                     pe_name_len;
        mode_t                  pe_mode;    /* dir, file or symbol_link, and
                                               also the access-control mode */
        void                   *pe_proc;    /* proc entry */
} cfs_param_entry_t;

#define CFS_PARAM_DEBUG_MAGIC 0x01DE01EE
typedef struct cfs_param_cb_data {
        int             cb_magic;
        int             cb_flag;  /* switch read cb function from proc to params_tree */
        void           *cb_data;  /* the original callback data */
} cfs_param_cb_data_t;

enum cfs_param_flags {
        /* The dir object has been unlinked */
        CFS_PARAM_ACCESS   = 1 << 0,
};

#define cfs_param_get_data(value, data, flagp)                          \
{                                                                       \
        int *cb_flag = (int *)flagp;                                    \
        LASSERT(((cfs_param_cb_data_t *)data)->cb_magic ==              \
                CFS_PARAM_DEBUG_MAGIC);                                 \
        value = ((cfs_param_cb_data_t *)data)->cb_data;                 \
        if (flagp != NULL)                                              \
                *cb_flag = ((cfs_param_cb_data_t *)data)->cb_flag;      \
}

#define CFS_ALLOC_PROCDATA(data)  cfs_param_cb_data_alloc(data,0)
#define CFS_ALLOC_PARAMDATA(data) cfs_param_cb_data_alloc(data,         \
                                                CFS_PARAM_ACCESS)

#define CFS_FREE_PROCDATA(data)  cfs_param_cb_data_free(data,0)
#define CFS_FREE_PARAMDATA(data) cfs_param_cb_data_free(data,           \
                                                CFS_PARAM_ACCESS)

extern cfs_param_cb_data_t *cfs_param_cb_data_alloc(void *data, int flag);
extern void cfs_param_cb_data_free(cfs_param_cb_data_t *cb_data, int flag);

extern cfs_param_entry_t *cfs_param_lnet_root;
extern int cfs_param_root_init(void);
extern void cfs_param_root_fini(void);
extern cfs_param_entry_t *cfs_param_get_root(void);

extern cfs_param_entry_t *
cfs_param_create(const char *name, mode_t mode, cfs_param_entry_t *parent);
extern cfs_param_entry_t *
cfs_param_mkdir(const char *name, cfs_param_entry_t *parent);
extern cfs_param_entry_t *
cfs_param_symlink(const char *name, cfs_param_entry_t *parent,
                  const char *dest);
extern cfs_param_entry_t *
cfs_param_lookup(const char *name, cfs_param_entry_t *parent);
extern void cfs_param_remove(const char *name, cfs_param_entry_t *parent);

extern cfs_param_entry_t *cfs_param_get(cfs_param_entry_t *pe);
extern void cfs_param_put(cfs_param_entry_t *pe);

extern int cfs_param_seq_open(cfs_param_file_t *file, cfs_seq_ops_t *op);
extern int cfs_param_seq_fopen(cfs_inode_t *inode, cfs_param_file_t *file,
                               cfs_seq_ops_t *op);
extern int cfs_param_seq_release(cfs_inode_t *inode, cfs_param_file_t *file);
static inline int
cfs_seq_release(cfs_inode_t *inode, cfs_param_file_t *file)
{
        return cfs_param_seq_release(inode, file);
}

extern int cfs_param_seq_printf(cfs_seq_file_t *seq, const char *f, ...);
extern int cfs_param_seq_puts(cfs_seq_file_t *seq, const char *s);
extern int cfs_param_seq_putc(cfs_seq_file_t *seq, const char c);

extern int cfs_param_klist(const char *parent_path, char *buf, int *buflen);
extern int cfs_param_kread(const char *path, char *buf, int nbytes,
                           loff_t *ppos, int *eof);
extern int cfs_param_kwrite(const char *path, char *buf, int count);

/* APIs for sysctl table */
typedef struct cfs_param_sysctl_table {
        const char             *name;
        void                   *data;
        mode_t                  mode;
        cfs_param_read_t       *read;
        cfs_param_write_t      *write;
        int                     writeable_before_startup;
} cfs_param_sysctl_table_t;
extern int cfs_param_intvec_write(cfs_param_file_t *filp, const char *buffer,
                                  unsigned long count, void *data);
extern int cfs_param_intvec_read(char *page, char **start, off_t off,
                                 int count, int *eof, void *data);
extern int cfs_param_string_write(cfs_param_file_t *filp, const char *buffer,
                                     unsigned long count, void *data);
extern int cfs_param_string_read(char *page, char **start, off_t off,
                                 int count, int *eof, void *data);

extern void cfs_param_sysctl_register(cfs_param_sysctl_table_t *table,
                                      cfs_param_entry_t *parent);
extern void cfs_param_sysctl_init(char *mod_name,
                                  cfs_param_sysctl_table_t *table,
                                  cfs_param_entry_t *parent);
extern void cfs_param_sysctl_fini(char *mod_name,
                                  cfs_param_entry_t *parent);
extern void cfs_param_sysctl_change_mode(char *mod_name,
                                         cfs_param_sysctl_table_t *table,
                                         cfs_param_entry_t *parent);

#define PE_NAME_MAXLEN         48
typedef struct cfs_param_info {
        int  pi_name_len;
        int  pi_mode;
        char pi_name[PE_NAME_MAXLEN];
} cfs_param_info_t;

enum cfs_param_value_type {
        CFS_PARAM_S8           = 0,
        CFS_PARAM_S16          = 1,
        CFS_PARAM_S32          = 2,
        CFS_PARAM_S64          = 3,
        CFS_PARAM_U8           = 4,
        CFS_PARAM_U16          = 5,
        CFS_PARAM_U32          = 6,
        CFS_PARAM_U64          = 7,
        CFS_PARAM_STR          = 8,
        CFS_PARAM_DB           = 9, /* double:
                                       timeval and lprocfs_read_frac_helper */
};
typedef struct cfs_param_data {
        enum cfs_param_value_type    pd_type;  /* CFS_PARAM_S16, ... */
        char                        *pd_name;
        __u32                        pd_name_len;
        char                        *pd_value;
        __u32                        pd_value_len;
        char                        *pd_unit;  /* some params have unit */
        __u32                        pd_unit_len;
        char                         pd_bulk[0];
} cfs_param_data_t;

extern int
cfs_param_snprintf_common(char *page, int count, void *cb_data,
                          enum cfs_param_value_type type,
                          const char *name, const char *unit,
                          const char *format, ...);
extern int cfs_param_copy(int flag, char *dest, const char *src, int count);
/* in many cases, parameter has no name and unit */
#define cfs_param_snprintf(page, count, cb_data, type, format, ...)  \
        cfs_param_snprintf_common(page, count, cb_data, type, NULL, NULL, \
                                  format, ## __VA_ARGS__)

#endif  /* __PARAMS_TREE_H__ */
