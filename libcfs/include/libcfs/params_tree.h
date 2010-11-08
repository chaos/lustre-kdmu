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
 * Author: LiuYing <emoly.liu@sun.com>
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
typedef struct file              libcfs_file_t;
typedef struct inode             libcfs_inode_t;
typedef struct proc_inode        libcfs_proc_inode_t;
typedef struct seq_file          libcfs_seq_file_t;
typedef struct seq_operations    libcfs_seq_ops_t;
typedef struct file_operations   libcfs_file_ops_t;
typedef cfs_module_t            *libcfs_module_t;
typedef struct proc_dir_entry    libcfs_param_dentry_t;
typedef struct poll_table_struct libcfs_poll_table_t;
#define LIBCFS_PARAM_MODULE      THIS_MODULE
#define LIBCFS_PDE(value)                       PDE(value)
#define LIBCFS_SEQ_OPEN(file, ops, rc)          (rc = seq_open(file, ops))
#define LIBCFS_FILE_PRIVATE(file)               (file->private_data)
#define LIBCFS_SEQ_READ_COMMON                  seq_read
#define LIBCFS_SEQ_LSEEK_COMMON                 seq_lseek
#define LIBCFS_SEQ_PRIVATE(seq)                 (seq->private)
#define LIBCFS_DENTRY_DATA(dentry)              (dentry->data)
#define LIBCFS_PROC_INODE_PDE(proc_inode)       (proc_inode->pde)
#define LIBCFS_PROCI_INODE(proc_inode)          (proc_inode->vfs_inode)
#define LIBCFS_SEQ_PRINTF(seq, format, ...)     seq_printf(seq, format,  \
                                                           ## __VA_ARGS__)
#define LIBCFS_SEQ_RELEASE(inode, file)         seq_release(inode, file)
#define LIBCFS_SEQ_PUTS(seq, s)                 seq_puts(seq, s)
#define LIBCFS_SEQ_PUTC(seq, s)                 seq_putc(seq, s)
#define LIBCFS_SEQ_READ(file, buf, count, ppos, rc) (rc = seq_read(file, buf, \
                                                            count, ppos))
/* in lprocfs_stat.c, to protect the private data for proc entries */
extern cfs_rw_semaphore_t       _lprocfs_lock;
#define LPROCFS_ENTRY()           do {  \
        cfs_down_read(&_lprocfs_lock);      \
} while(0)
#define LPROCFS_EXIT()            do {  \
        cfs_up_read(&_lprocfs_lock);        \
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

#define LPROCFS_WRITE_ENTRY()     do {  \
        cfs_down_write(&_lprocfs_lock);     \
} while(0)
#define LPROCFS_WRITE_EXIT()      do {  \
        cfs_up_write(&_lprocfs_lock);       \
} while(0)

#else /* !LPROCFS */

typedef struct libcfs_params_file {
        void           *lp_private;
        loff_t          lp_pos;
        unsigned int    lp_flags;
} libcfs_file_t;

typedef struct libcfs_params_inode {
        void    *lp_private;
} libcfs_inode_t;

typedef struct libcfs_proc_dentry {
        void *lp_data;
} libcfs_param_dentry_t;

typedef struct libcfs_proc_inode {
        libcfs_param_dentry_t *lp_pde;
        libcfs_inode_t lp_inode;
} libcfs_proc_inode_t;

struct libcfs_seq_operations;
typedef struct libcfs_seq_file {
        char *buf;
        size_t size;
        size_t from;
        size_t count;
        loff_t index;
        loff_t version;
        cfs_mutex_t lock;
        struct libcfs_seq_operations *op;
        void *private;
} libcfs_seq_file_t;

typedef struct libcfs_seq_operations {
        void *(*start) (libcfs_seq_file_t *m, loff_t *pos);
        void (*stop) (libcfs_seq_file_t *m, void *v);
        void *(*next) (libcfs_seq_file_t *m, void *v, loff_t *pos);
        int (*show) (libcfs_seq_file_t *m, void *v);
} libcfs_seq_ops_t;

typedef void *libcfs_module_t;
typedef void *libcfs_poll_table_t;

typedef struct libcfs_file_ops {
        libcfs_module_t owner;
        int (*open) (libcfs_inode_t *, libcfs_file_t *);
        loff_t (*llseek)(libcfs_file_t *, loff_t, int);
        int (*release) (libcfs_inode_t *, libcfs_file_t *);
        unsigned int (*poll) (libcfs_file_t *, libcfs_poll_table_t *);
        ssize_t (*write) (libcfs_file_t *, const char *, size_t, loff_t *);
        ssize_t (*read)(libcfs_file_t *, char *, size_t, loff_t *);
} libcfs_file_ops_t;
typedef libcfs_file_ops_t *cfs_lproc_filep_t;

static inline libcfs_proc_inode_t *FAKE_PROC_I(const libcfs_inode_t *inode)
{
        return container_of(inode, libcfs_proc_inode_t, lp_inode);
}

static inline libcfs_param_dentry_t *FAKE_PDE(libcfs_inode_t *inode)
{
        return FAKE_PROC_I(inode)->lp_pde;
}

#define LIBCFS_PARAM_MODULE                     NULL
#define LIBCFS_PDE(value)                       FAKE_PDE(value)
#define LIBCFS_PROCI_INODE(proc_inode)          (proc_inode->lp_inode)
#define LIBCFS_FILE_PRIVATE(file)               (file->lp_private)
#define LIBCFS_SEQ_READ_COMMON                  NULL
#define LIBCFS_SEQ_LSEEK_COMMON                 NULL
#define LIBCFS_SEQ_PRIVATE(seq)                 (seq->private)
#define LIBCFS_DENTRY_DATA(dentry)              (dentry->lp_data)
#define LIBCFS_PROC_INODE_PDE(proc_inode)       (proc_inode->lp_pde)
#define LIBCFS_SEQ_PRINTF(seq, format, ...)     libcfs_param_seq_print_common(seq,\
                                                 format, ## __VA_ARGS__)
#define LIBCFS_SEQ_RELEASE(inode, file)         libcfs_seq_release(inode, file)
#define LIBCFS_SEQ_PUTS(seq, s)                 libcfs_param_seq_puts_common(seq, s)
#define LIBCFS_SEQ_PUTC(seq, s)                 libcfs_param_seq_putc_common(seq, s)
#define LIBCFS_SEQ_READ(file, buf, count, ppos, rc) do {} while(0)
#define LIBCFS_SEQ_OPEN(file, ops, rc)                     \
do {                                                       \
         libcfs_seq_file_t *p = LIBCFS_FILE_PRIVATE(file); \
         if (!p) {                                         \
                LIBCFS_ALLOC(p, sizeof(*p));               \
                if (!p) {                                  \
                        rc = -ENOMEM;                      \
                        break;                             \
                }                                          \
                LIBCFS_FILE_PRIVATE(file) = p;             \
        }                                                  \
        memset(p, 0, sizeof(*p));                          \
        p->op = ops;                                       \
        rc = 0;                                            \
} while(0)

#define LPROCFS_ENTRY()             do {} while(0)
#define LPROCFS_EXIT()              do {} while(0)
static inline
int LPROCFS_ENTRY_AND_CHECK(libcfs_param_dentry_t *dp)
{
        LPROCFS_ENTRY();
        return 0;
}
#define LPROCFS_WRITE_ENTRY()       do {} while(0)
#define LPROCFS_WRITE_EXIT()        do {} while(0)

#endif /* LPROCFS */

#define LPE_HASH_CUR_BITS       8
#define LPE_HASH_BKT_BITS       8
#define LPE_HASH_MAX_BITS       24

typedef int (libcfs_param_read_t)(char *page, char **start, off_t off,
                                  int count, int *eof, void *data);
typedef int (libcfs_param_write_t)(libcfs_file_t *filp, const char *buffer,
                                   unsigned long count, void *data);
typedef struct libcfs_param_entry {
        /* hash table members */
        cfs_hash_t                *lpe_hash_t;/* =dir, someone's parent */
        cfs_rw_semaphore_t         lpe_rw_sem;
        /* hash node members */
        cfs_hlist_node_t           lpe_hash_n;/* =leaf_entry, someone's child */
        libcfs_param_read_t       *lpe_cb_read;
        libcfs_param_write_t      *lpe_cb_write;
        libcfs_file_ops_t         *lpe_cb_sfops;/* used only by seq operation */
        void                      *lpe_data;   /* if entry is a symlink, it
                                                  stores the path to the target;
                                                  otherwise, it points to a
                                                  libcfs_param_cb_data */
        /* common members */
        struct libcfs_param_entry *lpe_parent;
        cfs_atomic_t               lpe_refcount;/* lpe reference count */
        const char                *lpe_name;
        int                        lpe_name_len;
        mode_t                     lpe_mode;   /* dir, file or symbol_link, and
                                                  also the access-control mode */
        void                      *lpe_proc;   /* proc entry */
} libcfs_param_entry_t;

#define PARAM_DEBUG_MAGIC 0x01DE01EE
typedef struct libcfs_param_cb_data {
        int             cb_magic;
        int             cb_flag;       /* switch read cb function from proc to params_tree */
        void           *cb_data;     /* the original callback data */
} lparcb_t;

enum libcfs_param_flags {
        /* The dir object has been unlinked */
        LIBCFS_PARAM_ACCESS   = 1 << 0,
};


#define LIBCFS_PARAM_GET_DATA(value, cb_param, flagp)                   \
{                                                                       \
        int *cb_flag = (int *)flagp;                                    \
        LASSERT(((lparcb_t *)cb_param)->cb_magic == PARAM_DEBUG_MAGIC); \
        value = ((lparcb_t *)cb_param)->cb_data;                        \
        if (flagp != NULL)                                              \
                *cb_flag = ((lparcb_t *)cb_param)->cb_flag;             \
}

#define LIBCFS_ALLOC_PROCDATA(data)  libcfs_param_cb_data_alloc(data,0)
#define LIBCFS_ALLOC_PARAMDATA(data) libcfs_param_cb_data_alloc(data,    \
                                     LIBCFS_PARAM_ACCESS)

#define LIBCFS_FREE_PROCDATA(data)  libcfs_param_cb_data_free(data,0)
#define LIBCFS_FREE_PARAMDATA(data) libcfs_param_cb_data_free(data,     \
                                     LIBCFS_PARAM_ACCESS)


extern lparcb_t *libcfs_param_cb_data_alloc(void *data, int flag);
extern void libcfs_param_cb_data_free(lparcb_t *cb_data, int flag);
extern libcfs_param_entry_t *libcfs_param_lnet_root;

extern int libcfs_param_root_init(void);
extern void libcfs_param_root_fini(void);
extern libcfs_param_entry_t *libcfs_param_get_root(void);

extern libcfs_param_entry_t *
libcfs_param_create(const char *name, mode_t mode,
                    libcfs_param_entry_t *parent);
extern libcfs_param_entry_t *
libcfs_param_mkdir(const char *name, libcfs_param_entry_t *parent);
extern libcfs_param_entry_t *
libcfs_param_symlink(const char *name, libcfs_param_entry_t *parent,
                     const char *dest);
extern libcfs_param_entry_t *
libcfs_param_lookup(const char *name, libcfs_param_entry_t *parent);
extern void libcfs_param_remove(const char *name, libcfs_param_entry_t *parent);

extern libcfs_param_entry_t *libcfs_param_get(libcfs_param_entry_t *lpe);
extern void libcfs_param_put(libcfs_param_entry_t *lpe);

extern int libcfs_param_list(const char *parent_path, char *buf, int *buflen);
extern int libcfs_param_read(const char *path, char *buf, int nbytes,
                             loff_t *ppos, int *eof);
extern int libcfs_param_write(const char *path, char *buf, int count);

extern int libcfs_param_seq_release_common(libcfs_inode_t *inode,
                                           libcfs_file_t *file);
extern int libcfs_param_seq_print_common(libcfs_seq_file_t *seq,
                                         const char *f, ...);
extern int libcfs_param_seq_puts_common(libcfs_seq_file_t *seq, const char *s);
extern int libcfs_param_seq_putc_common(libcfs_seq_file_t *seq, const char c);

/* APIs for sysctl table */
typedef struct libcfs_param_sysctl_table {
        const char             *name;
        void                   *data;
        mode_t                  mode;
        libcfs_param_read_t    *read;
        libcfs_param_write_t   *write;
        int                     writeable_before_startup;
} libcfs_param_sysctl_table_t;
extern int libcfs_param_intvec_write(libcfs_file_t *filp, const char *buffer,
                                     unsigned long count, void *data);
extern int libcfs_param_intvec_read(char *page, char **start, off_t off,
                                    int count, int *eof, void *data);
extern int libcfs_param_string_write(libcfs_file_t *filp, const char *buffer,
                                     unsigned long count, void *data);
extern int libcfs_param_string_read(char *page, char **start, off_t off,
                                    int count, int *eof, void *data);

extern void libcfs_param_sysctl_register(libcfs_param_sysctl_table_t *table,
                                         libcfs_param_entry_t *parent);
extern void libcfs_param_sysctl_init(char *mod_name,
                                     libcfs_param_sysctl_table_t *table,
                                     libcfs_param_entry_t *parent);
extern void libcfs_param_sysctl_fini(char *mod_name,
                                     libcfs_param_entry_t *parent);
extern void libcfs_param_sysctl_change(char *mod_name,
                                       libcfs_param_sysctl_table_t *table,
                                       libcfs_param_entry_t *parent);

#define LPE_NAME_MAXLEN         48
typedef struct libcfs_param_info {
        int lpi_name_len;
        int lpi_mode;
        char lpi_name[LPE_NAME_MAXLEN];
} libcfs_param_info_t;

enum libcfs_param_value_type {
        LP_S8           = 0,
        LP_S16          = 1,
        LP_S32          = 2,
        LP_S64          = 3,
        LP_U8           = 4,
        LP_U16          = 5,
        LP_U32          = 6,
        LP_U64          = 7,
        LP_STR          = 8,
        LP_DB           = 9,    /* double:
                                   timeval and lprocfs_read_frac_helper */
};
typedef struct libcfs_param_data {
        enum libcfs_param_value_type    param_type; /* LP_S16, LP_S32, ... */
        char                           *param_name;
        __u32                           param_name_len;
        char                           *param_value;
        __u32                           param_value_len;
        char                           *param_unit;  /* some params have unit */
        __u32                           param_unit_len;
        char                            param_bulk[0];
} libcfs_param_data_t;

extern int
libcfs_param_snprintf_common(char *page, int count, void *cb_data,
                             enum libcfs_param_value_type type,
                             const char *name, const char *unit,
                             const char *format, ...);
extern int libcfs_param_copy(int flag, char *dest, const char *src, int count);
/* in many cases, parameter has no name and unit */
#define libcfs_param_snprintf(page, count, cb_data, type, format, ...)  \
        libcfs_param_snprintf_common(page, count, cb_data, type,        \
                                     NULL, NULL,                        \
                                     format, ## __VA_ARGS__)

#endif  /* __PARAMS_TREE_H__ */
