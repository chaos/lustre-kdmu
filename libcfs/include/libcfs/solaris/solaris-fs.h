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
 * Copyright  2009 Sun Microsystems, Inc. All rights reserved
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * libcfs/include/libcfs/solaris/solaris-fs.h
 *
 */

#ifndef __LIBCFS_SOLARIS_SOLARIS_FS_H__
#define __LIBCFS_SOLARIS_SOLARIS_FS_H__

#ifndef __LIBCFS_LIBCFS_H__
#error Do not #include this file directly. #include <libcfs/libcfs.h> instead
#endif

#ifndef __KERNEL__
#error This include is only for kernel use.
#endif

#define PATH_MAX MAXPATHLEN

/* linux/fs.h */
#define MAY_EXEC 1
#define MAY_WRITE 2
#define MAY_READ 4
#define MAY_APPEND 8

/* from linux asm-x86_64/posix-types.h */
typedef struct {
	int	val[2];
} cfs_fsid_t;

/* linux/statfs.h */
typedef struct cfs_kstatfs {
        long       f_type;
        long       f_bsize;
        u64        f_blocks;
        u64        f_bfree;
        u64        f_bavail;
        u64        f_files;
        u64        f_ffree;
        cfs_fsid_t f_fsid;
        long       f_namelen;
        long       f_frsize;
        long       f_spare[5];
} cfs_kstatfs_t;

/*
 * cfs_file_t and related routines
 */

typedef struct {
        vnode_t *cfl_vp;
        file_t  *cfl_fp;
        int      cfl_oflag;
        loff_t   cfl_pos;
} cfs_file_t;

#define cfs_filp_poff(f)                (&(f)->cfl_pos)

cfs_file_t *cfs_filp_open(const char *name, int flags, int mode, int *err);
int cfs_filp_close(cfs_file_t *fp);
int cfs_filp_write(cfs_file_t *fp, void *buf, size_t nbytes, loff_t *pos_p);
int cfs_filp_fsync(cfs_file_t *fp);
loff_t cfs_filp_size(cfs_file_t *fp);

cfs_file_t *cfs_get_fd(int fd);
void cfs_put_file(cfs_file_t *fp);
ssize_t cfs_user_write (cfs_file_t *cfp, const char *buf, size_t count,
                        loff_t *offset);

/* see sys/fcntl.h and sys/file.h */
static inline int
cfs_filp_fcntl2file(int flags)
{
        int f = 0;

        f |= (flags & O_CREAT)     ? FCREAT           : 0;
        f |= (flags & O_TRUNC)     ? FTRUNC           : 0;
        f |= (flags & O_EXCL)      ? FEXCL            : 0;
        f |= (flags & O_LARGEFILE) ? FOFFMAX          : 0;
        f |= (flags & O_RDONLY)    ? FREAD            : 0;
        f |= (flags & O_WRONLY)    ? FWRITE           : 0;
        f |= (flags & O_RDWR)      ? (FREAD | FWRITE) : 0;

	return f;
}

/*
 * CFS_FLOCK routines
 */

typedef struct cfs_file_lock {
    int         fl_type;
    pid_t       fl_pid;
    size_t      fl_len;
    off_t       fl_start;
    off_t       fl_end;
} cfs_flock_t; 

#define cfs_flock_type(fl)                  ((fl)->fl_type)
#define cfs_flock_set_type(fl, type)        do { (fl)->fl_type = (type); } while(0)
#define cfs_flock_pid(fl)                   ((fl)->fl_pid)
#define cfs_flock_set_pid(fl, pid)          do { (fl)->fl_pid = (pid); } while(0)
#define cfs_flock_start(fl)                 ((fl)->fl_start)
#define cfs_flock_set_start(fl, start)      do { (fl)->fl_start = (start); } while(0)
#define cfs_flock_end(fl)                   ((fl)->fl_end)
#define cfs_flock_set_end(fl, end)          do { (fl)->fl_end = (end); } while(0)

/*
 * Linux inode flags - the definitions added here to be able to
 * compile common lustre_idl.h
 */

#define S_SYNC		1	/* Writes are synced at once */
#define S_NOATIME	2	/* Do not update access times */
#define S_APPEND	4	/* Append-only file */
#define S_IMMUTABLE	8	/* Immutable file */
#define S_DEAD		16	/* removed, but still open directory */
#define S_NOQUOTA	32	/* Inode is not counted to quota */
#define S_DIRSYNC	64	/* Directory modifications are synchronous */
#define S_NOCMTIME	128	/* Do not update file c/mtime */
#define S_SWAPFILE	256	/* Do not truncate: swapon got its bmaps */
#define S_PRIVATE	512	/* Inode is fs-internal */

/* stub it out for now */
struct super_block {
        ;
};

typedef struct {
        ;
} cfs_dentry_t;

/* Linux ACL structures and definitions */

#define ACL_USER_OBJ            (0x01)
#define ACL_USER                (0x02)
#define ACL_GROUP_OBJ           (0x04)
#define ACL_GROUP               (0x08)
#define ACL_MASK                (0x10)
#define ACL_OTHER               (0x20)

#define ACL_UNDEFINED_ID        (-1)

#define POSIX_ACL_XATTR_VERSION (0x0002)

typedef struct {
        __u16                   e_tag;
        __u16                   e_perm;
        __u32                   e_id;
} posix_acl_xattr_entry;

typedef struct {
        __u32                   a_version;
        posix_acl_xattr_entry   a_entries[0];
} posix_acl_xattr_header;

#define xattr_acl_entry  posix_acl_xattr_entry
#define xattr_acl_header posix_acl_xattr_header

#endif /* __LIBCFS_SOLARIS_SOLARIS_FS_H__ */
