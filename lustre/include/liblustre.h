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
 * lustre/include/liblustre.h
 *
 * User-space Lustre headers.
 */

#ifndef LIBLUSTRE_H__
#define LIBLUSTRE_H__

/** \defgroup liblustre liblustre
 *
 * @{
 */
#include <fcntl.h>
#include <sys/queue.h>

#ifdef __KERNEL__
#error Kernel files should not #include <liblustre.h>
#endif

#include <libcfs/libcfs.h>
#include <lnet/lnet.h>

/* definitions for liblustre */

#ifdef __CYGWIN__

#define loff_t long long
#define ERESTART 2001
typedef unsigned short cfs_umode_t;

#endif

/* always adopt 2.5 definitions */
#define KERNEL_VERSION(a,b,c) ((a)*100+(b)*10+c)
#define LINUX_VERSION_CODE KERNEL_VERSION(2,6,5)

#ifndef page_private
#define page_private(page) ((page)->private)
#define set_page_private(page, v) ((page)->private = (v))
#endif


/*
 * The inter_module_get implementation is specific to liblustre, so this needs
 * to stay here for now.
 */
static inline void inter_module_put(void *a)
{
        return;
}
void *inter_module_get(char *arg);

/* bits ops */

/* a long can be more than 32 bits, so use BITS_PER_LONG
 * to allow the compiler to adjust the bit shifting accordingly
 */

static __inline__ int ext2_set_bit(int nr, void *addr)
{
#ifdef __BIG_ENDIAN
        return cfs_set_bit((nr ^ ((BITS_PER_LONG-1) & ~0x7)), addr);
#else
        return cfs_set_bit(nr, addr);
#endif
}

static __inline__ int ext2_clear_bit(int nr, void *addr)
{
#ifdef __BIG_ENDIAN
        return cfs_clear_bit((nr ^ ((BITS_PER_LONG-1) & ~0x7)), addr);
#else
        return cfs_clear_bit(nr, addr);
#endif
}

static __inline__ int ext2_test_bit(int nr, void *addr)
{
#ifdef __BIG_ENDIAN
        __const__ unsigned char *tmp = (__const__ unsigned char *) addr;
        return (tmp[nr >> 3] >> (nr & 7)) & 1;
#else
        return cfs_test_bit(nr, addr);
#endif
}


/* module initialization */
extern int init_obdclass(void);
extern int ptlrpc_init(void);
extern int ldlm_init(void);
extern int osc_init(void);
extern int lov_init(void);
extern int mdc_init(void);
extern int lmv_init(void);
extern int mgc_init(void);
extern int echo_client_init(void);



/* general stuff */

#define EXPORT_SYMBOL(S)

typedef __u64 kdev_t;

#ifndef min
#define min(x,y) ((x)<(y) ? (x) : (y))
#endif

#ifndef max
#define max(x,y) ((x)>(y) ? (x) : (y))
#endif

#ifndef min_t
#define min_t(type,x,y) \
        ({ type __x = (x); type __y = (y); __x < __y ? __x: __y; })
#endif
#ifndef max_t
#define max_t(type,x,y) \
        ({ type __x = (x); type __y = (y); __x > __y ? __x: __y; })
#endif

#define simple_strtol strtol

/* registering symbols */
#ifndef ERESTARTSYS
#define ERESTARTSYS ERESTART
#endif
#define CFS_HZ 1

/* random */

void cfs_get_random_bytes(void *ptr, int size);

/* memory */

/* memory size: used for some client tunables */
#define cfs_num_physpages  (256 * 1024) /* 1GB */
#define CFS_NUM_CACHEPAGES cfs_num_physpages


/* VFS stuff */
#define ATTR_MODE       0x0001
#define ATTR_UID        0x0002
#define ATTR_GID        0x0004
#define ATTR_SIZE       0x0008
#define ATTR_ATIME      0x0010
#define ATTR_MTIME      0x0020
#define ATTR_CTIME      0x0040
#define ATTR_ATIME_SET  0x0080
#define ATTR_MTIME_SET  0x0100
#define ATTR_FORCE      0x0200  /* Not a change, but a change it */
#define ATTR_ATTR_FLAG  0x0400
#define ATTR_RAW        0x0800  /* file system, not vfs will massage attrs */
#define ATTR_FROM_OPEN  0x1000  /* called from open path, ie O_TRUNC */
#define ATTR_CTIME_SET  0x2000
#define ATTR_BLOCKS     0x4000
#define ATTR_KILL_SUID  0
#define ATTR_KILL_SGID  0

struct iattr {
        unsigned int    ia_valid;
        cfs_umode_t     ia_mode;
        uid_t           ia_uid;
        gid_t           ia_gid;
        loff_t          ia_size;
        time_t          ia_atime;
        time_t          ia_mtime;
        time_t          ia_ctime;
        unsigned int    ia_attr_flags;
};

#define ll_iattr iattr

#define IT_OPEN     0x0001
#define IT_CREAT    0x0002
#define IT_READDIR  0x0004
#define IT_GETATTR  0x0008
#define IT_LOOKUP   0x0010
#define IT_UNLINK   0x0020
#define IT_GETXATTR 0x0040
#define IT_EXEC     0x0080
#define IT_PIN      0x0100

#define IT_FL_LOCKED   0x0001
#define IT_FL_FOLLOWED 0x0002 /* set by vfs_follow_link */

#define INTENT_MAGIC 0x19620323

struct lustre_intent_data {
        int       it_disposition;
        int       it_status;
        __u64     it_lock_handle;
        void     *it_data;
        int       it_lock_mode;
        int it_int_flags;
};
struct lookup_intent {
        int     it_magic;
        void    (*it_op_release)(struct lookup_intent *);
        int     it_op;
        int     it_flags;
        int     it_create_mode;
        union {
                struct lustre_intent_data lustre;
        } d;
};

static inline void intent_init(struct lookup_intent *it, int op, int flags)
{
        memset(it, 0, sizeof(*it));
        it->it_magic = INTENT_MAGIC;
        it->it_op = op;
        it->it_flags = flags;
}

struct dentry {
        int d_count;
};

struct vfsmount {
        void *pwd;
};

struct signal {
        int signal;
};

struct task_struct {
        int state;
        struct signal pending;
        char comm[32];
        int uid;
        int gid;
        int pid;
        int fsuid;
        int fsgid;
        int max_groups;
        int ngroups;
        gid_t *groups;
        __u32 cap_effective;
};


typedef struct task_struct cfs_task_t;
#define cfs_current()           current
#define cfs_curproc_pid()       (current->pid)
#define cfs_curproc_comm()      (current->comm)
#define cfs_curproc_fsuid()     (current->fsuid)
#define cfs_curproc_fsgid()     (current->fsgid)

extern struct task_struct *current;
int cfs_curproc_is_in_groups(gid_t gid);

#define cfs_set_current_state(foo) do { current->state = foo; } while (0)

#define cfs_wait_event_interruptible(wq, condition, ret)                \
{                                                                       \
        struct l_wait_info lwi;                                         \
        int timeout = 100000000;/* for ever */                          \
        int ret;                                                        \
                                                                        \
        lwi = LWI_TIMEOUT(timeout, NULL, NULL);                         \
        ret = l_wait_event(NULL, condition, &lwi);                      \
                                                                        \
        ret;                                                            \
}

#define cfs_lock_kernel() do {} while (0)
#define cfs_unlock_kernel() do {} while (0)
#define daemonize(l) do {} while (0)
#define sigfillset(l) do {} while (0)
#define recalc_sigpending(l) do {} while (0)

#define USERMODEHELPER(path, argv, envp) (0)
#define SIGNAL_MASK_ASSERT()
#define CFS_KERN_INFO

#if CFS_HZ != 1
#error "liblustre's jiffies currently expects HZ to be 1"
#endif
#define jiffies                                 \
({                                              \
        unsigned long _ret = 0;                 \
        struct timeval tv;                      \
        if (gettimeofday(&tv, NULL) == 0)       \
                _ret = tv.tv_sec;               \
        _ret;                                   \
})
#define get_jiffies_64()  (__u64)jiffies

#ifndef likely
#define likely(exp) (exp)
#endif
#ifndef unlikely
#define unlikely(exp) (exp)
#endif

#define cfs_might_sleep()
#define might_sleep_if(c)
#define smp_mb()

/* FIXME sys/capability will finally included linux/fs.h thus
 * cause numerous trouble on x86-64. as temporary solution for
 * build broken at Cray, we copy definition we need from capability.h
 * FIXME
 */
struct _cap_struct;
typedef struct _cap_struct *cap_t;
typedef int cap_value_t;
typedef enum {
    CAP_EFFECTIVE=0,
    CAP_PERMITTED=1,
    CAP_INHERITABLE=2
} cap_flag_t;
typedef enum {
    CAP_CLEAR=0,
    CAP_SET=1
} cap_flag_value_t;

cap_t   cap_get_proc(void);
int     cap_get_flag(cap_t, cap_value_t, cap_flag_t, cap_flag_value_t *);

static inline void libcfs_run_lbug_upcall(char *file, const char *fn,
                                           const int l){}


struct liblustre_wait_callback {
        cfs_list_t              llwc_list;
        const char             *llwc_name;
        int                   (*llwc_fn)(void *arg);
        void                   *llwc_arg;
};

void *liblustre_register_wait_callback(const char *name,
                                       int (*fn)(void *arg), void *arg);
void liblustre_deregister_wait_callback(void *notifier);
int liblustre_wait_event(int timeout);

void *liblustre_register_idle_callback(const char *name,
                                       int (*fn)(void *arg), void *arg);
void liblustre_deregister_idle_callback(void *notifier);
void liblustre_wait_idle(void);

/* flock related */
struct nfs_lock_info {
        __u32             state;
        __u32             flags;
        void            *host;
};

typedef struct file_lock {
        struct file_lock *fl_next;  /* singly linked list for this inode  */
        cfs_list_t fl_link;   /* doubly linked list of all locks */
        cfs_list_t fl_block;  /* circular list of blocked processes */
        void *fl_owner;
        unsigned int fl_pid;
        cfs_waitq_t fl_wait;
        struct file *fl_file;
        unsigned char fl_flags;
        unsigned char fl_type;
        loff_t fl_start;
        loff_t fl_end;

        void (*fl_notify)(struct file_lock *);  /* unblock callback */
        void (*fl_insert)(struct file_lock *);  /* lock insertion callback */
        void (*fl_remove)(struct file_lock *);  /* lock removal callback */

        void *fl_fasync; /* for lease break notifications */
        unsigned long fl_break_time;    /* for nonblocking lease breaks */

        union {
                struct nfs_lock_info    nfs_fl;
        } fl_u;
} cfs_flock_t;

#define cfs_flock_type(fl)                  ((fl)->fl_type)
#define cfs_flock_set_type(fl, type)        do { (fl)->fl_type = (type); } while(0)
#define cfs_flock_pid(fl)                   ((fl)->fl_pid)
#define cfs_flock_set_pid(fl, pid)          do { (fl)->fl_pid = (pid); } while(0)
#define cfs_flock_start(fl)                 ((fl)->fl_start)
#define cfs_flock_set_start(fl, start)      do { (fl)->fl_start = (start); } while(0)
#define cfs_flock_end(fl)                   ((fl)->fl_end)
#define cfs_flock_set_end(fl, end)          do { (fl)->fl_end = (end); } while(0)

#ifndef OFFSET_MAX
#define INT_LIMIT(x)    (~((x)1 << (sizeof(x)*8 - 1)))
#define OFFSET_MAX      INT_LIMIT(loff_t)
#endif

#define i_atime                     i_stbuf.st_atime
#define i_mtime                     i_stbuf.st_mtime
#define i_ctime                     i_stbuf.st_ctime
#define i_size                      i_stbuf.st_size
#define i_blocks                    i_stbuf.st_blocks
#define i_blksize                   i_stbuf.st_blksize
#define i_mode                      i_stbuf.st_mode
#define i_uid                       i_stbuf.st_uid
#define i_gid                       i_stbuf.st_gid

/* XXX: defined in kernel */
#define FL_POSIX        1
#define FL_SLEEP        128

/* quota */
#define QUOTA_OK 0
#define NO_QUOTA 1

/* ACL */
struct posix_acl_entry {
        short                   e_tag;
        unsigned short          e_perm;
        unsigned int            e_id;
};

struct posix_acl {
        cfs_atomic_t            a_refcount;
        unsigned int            a_count;
        struct posix_acl_entry  a_entries[0];
};

typedef struct {
        __u16           e_tag;
        __u16           e_perm;
        __u32           e_id;
} xattr_acl_entry;

typedef struct {
        __u32           a_version;
        xattr_acl_entry a_entries[0];
} xattr_acl_header;

static inline size_t xattr_acl_size(int count)
{
        return sizeof(xattr_acl_header) + count * sizeof(xattr_acl_entry);
}

static inline
struct posix_acl * posix_acl_from_xattr(const void *value, size_t size)
{
        return NULL;
}

static inline
int posix_acl_valid(const struct posix_acl *acl)
{
        return 0;
}

static inline
void posix_acl_release(struct posix_acl *acl)
{
}

#ifdef LIBLUSTRE_POSIX_ACL
# ifndef posix_acl_xattr_entry
#  define posix_acl_xattr_entry xattr_acl_entry
# endif
# ifndef posix_acl_xattr_header
#  define posix_acl_xattr_header xattr_acl_header
# endif
# ifndef posix_acl_xattr_size
#  define posix_acl_xattr_size(entry) xattr_acl_size(entry)
# endif
# ifndef CONFIG_FS_POSIX_ACL
#  define CONFIG_FS_POSIX_ACL 1
# endif
#endif

#ifndef ENOTSUPP
#define ENOTSUPP ENOTSUP
#endif

typedef int mm_segment_t;
enum {
        KERNEL_DS,
        USER_DS
};
static inline mm_segment_t get_fs(void)
{
        return USER_DS;
}

static inline void set_fs(mm_segment_t seg)
{
}

#define S_IRWXUGO       (S_IRWXU|S_IRWXG|S_IRWXO)
#define S_IALLUGO       (S_ISUID|S_ISGID|S_ISVTX|S_IRWXUGO)

#include <obd_support.h>
#include <lustre/lustre_idl.h>
#include <lustre_lib.h>
#include <lustre_import.h>
#include <lustre_export.h>
#include <lustre_net.h>

/** @} liblustre */

#endif
