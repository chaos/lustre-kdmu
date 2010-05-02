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
 *
 * lustre/include/lustre/lustre_user.h
 *
 * Lustre public user-space interface definitions.
 */

#ifndef _LUSTRE_USER_H
#define _LUSTRE_USER_H

/** \defgroup lustreuser lustreuser
 *
 * @{
 */

#include <lustre/ll_fiemap.h>
#if defined(__linux__)
#include <linux/lustre_user.h>
#elif defined(__APPLE__)
#include <darwin/lustre_user.h>
#elif defined(__WINNT__)
#include <winnt/lustre_user.h>
#else
#error Unsupported operating system.
#endif

/* for statfs() */
#define LL_SUPER_MAGIC 0x0BD00BD0

#ifndef FSFILT_IOC_GETFLAGS
#define FSFILT_IOC_GETFLAGS               _IOR('f', 1, long)
#define FSFILT_IOC_SETFLAGS               _IOW('f', 2, long)
#define FSFILT_IOC_GETVERSION             _IOR('f', 3, long)
#define FSFILT_IOC_SETVERSION             _IOW('f', 4, long)
#define FSFILT_IOC_GETVERSION_OLD         _IOR('v', 1, long)
#define FSFILT_IOC_SETVERSION_OLD         _IOW('v', 2, long)
#define FSFILT_IOC_FIEMAP                 _IOWR('f', 11, struct ll_user_fiemap)
#endif

/* FIEMAP flags supported by Lustre */
#define LUSTRE_FIEMAP_FLAGS_COMPAT (FIEMAP_FLAG_SYNC | FIEMAP_FLAG_DEVICE_ORDER)

enum obd_statfs_state {
        OS_STATE_DEGRADED       = 0x00000001, /**< RAID degraded/rebuilding */
        OS_STATE_READONLY       = 0x00000002, /**< filesystem is read-only */
        OS_STATE_RDONLY_1       = 0x00000004, /**< obsolete 1.6, was EROFS=30 */
        OS_STATE_RDONLY_2       = 0x00000008, /**< obsolete 1.6, was EROFS=30 */
        OS_STATE_RDONLY_3       = 0x00000010, /**< obsolete 1.6, was EROFS=30 */
};

struct obd_statfs {
        __u64           os_type;
        __u64           os_blocks;
        __u64           os_bfree;
        __u64           os_bavail;
        __u64           os_files;
        __u64           os_ffree;
        __u8            os_fsid[40];
        __u32           os_bsize;
        __u32           os_namelen;
        __u64           os_maxbytes;
        __u32           os_state;       /**< obd_statfs_state OS_STATE_* flag */
        __u32           os_spare1;
        __u32           os_spare2;
        __u32           os_spare3;
        __u32           os_spare4;
        __u32           os_spare5;
        __u32           os_spare6;
        __u32           os_spare7;
        __u32           os_spare8;
        __u32           os_spare9;
};


/*
 * The ioctl naming rules:
 * LL_*     - works on the currently opened filehandle instead of parent dir
 * *_OBD_*  - gets data for both OSC or MDC (LOV, LMV indirectly)
 * *_MDC_*  - gets/sets data related to MDC
 * *_LOV_*  - gets/sets data related to OSC/LOV
 * *FILE*   - called on parent dir and passes in a filename
 * *STRIPE* - set/get lov_user_md
 * *INFO    - set/get lov_user_mds_data
 */
#define LL_IOC_GETFLAGS                 _IOR ('f', 151, long)
#define LL_IOC_SETFLAGS                 _IOW ('f', 152, long)
#define LL_IOC_CLRFLAGS                 _IOW ('f', 153, long)
#define LL_IOC_LOV_SETSTRIPE            _IOW ('f', 154, long)
#define LL_IOC_LOV_GETSTRIPE            _IOW ('f', 155, long)
#define LL_IOC_LOV_SETEA                _IOW ('f', 156, long)
#define LL_IOC_RECREATE_OBJ             _IOW ('f', 157, long)
#define LL_IOC_GROUP_LOCK               _IOW ('f', 158, long)
#define LL_IOC_GROUP_UNLOCK             _IOW ('f', 159, long)
#define LL_IOC_QUOTACHECK               _IOW ('f', 160, int)
#define LL_IOC_POLL_QUOTACHECK          _IOR ('f', 161, struct if_quotacheck *)
#define LL_IOC_QUOTACTL                 _IOWR('f', 162, struct if_quotactl *)
#define IOC_OBD_STATFS                  _IOWR('f', 164, struct obd_statfs *)
#define IOC_LOV_GETINFO                 _IOWR('f', 165, struct lov_user_mds_data *)
#define LL_IOC_FLUSHCTX                 _IOW ('f', 166, long)
#define LL_IOC_RMTACL                   _IOW ('f', 167, long)

#define LL_IOC_GETOBDCOUNT              _IOR ('f', 168, long)

#define LL_IOC_LLOOP_ATTACH             _IOWR('f', 169, long)
#define LL_IOC_LLOOP_DETACH             _IOWR('f', 170, long)
#define LL_IOC_LLOOP_INFO               _IOWR('f', 171, long)
#define LL_IOC_LLOOP_DETACH_BYDEV       _IOWR('f', 172, long)
#define LL_IOC_PATH2FID                 _IOR ('f', 173, long)
#define LL_IOC_GET_CONNECT_FLAGS        _IOWR('f', 174, __u64 *)
#define LL_IOC_GET_MDTIDX               _IOR ('f', 175, int)

#define LL_IOC_HSM_CT_START             _IOW ('f', 178, struct lustre_kernelcomm *)

#define LL_STATFS_MDC           1
#define LL_STATFS_LOV           2

#define IOC_MDC_TYPE            'i'
#define IOC_MDC_LOOKUP          _IOWR(IOC_MDC_TYPE, 20, struct obd_device *)
#define IOC_MDC_GETFILESTRIPE   _IOWR(IOC_MDC_TYPE, 21, struct lov_user_md *)
#define IOC_MDC_GETFILEINFO     _IOWR(IOC_MDC_TYPE, 22, struct lov_user_mds_data *)
#define LL_IOC_MDC_GETINFO      _IOWR(IOC_MDC_TYPE, 23, struct lov_user_mds_data *)

/* Keep these for backward compartability. */
#define LL_IOC_OBD_STATFS       IOC_OBD_STATFS
#define IOC_MDC_GETSTRIPE       IOC_MDC_GETFILESTRIPE

#define O_LOV_DELAY_CREATE 0100000000  /* hopefully this does not conflict */

#define LL_FILE_IGNORE_LOCK             0x00000001
#define LL_FILE_GROUP_LOCKED            0x00000002
#define LL_FILE_READAHEAD               0x00000004
#define LL_FILE_RMTACL                  0x00000008

#define LOV_USER_MAGIC_V1 0x0BD10BD0
#define LOV_USER_MAGIC    LOV_USER_MAGIC_V1
#define LOV_USER_MAGIC_JOIN_V1 0x0BD20BD0
#define LOV_USER_MAGIC_V3 0x0BD30BD0

#define LOV_PATTERN_RAID0 0x001
#define LOV_PATTERN_RAID1 0x002
#define LOV_PATTERN_FIRST 0x100

#define LOV_MAXPOOLNAME 16
#define LOV_POOLNAMEF "%.16s"

#define lov_user_ost_data lov_user_ost_data_v1
struct lov_user_ost_data_v1 {     /* per-stripe data structure */
        __u64 l_object_id;        /* OST object ID */
        __u64 l_object_gr;        /* OST object group (creating MDS number) */
        __u32 l_ost_gen;          /* generation of this OST index */
        __u32 l_ost_idx;          /* OST index in LOV */
} __attribute__((packed));

#define lov_user_md lov_user_md_v1
struct lov_user_md_v1 {           /* LOV EA user data (host-endian) */
        __u32 lmm_magic;          /* magic number = LOV_USER_MAGIC_V1 */
        __u32 lmm_pattern;        /* LOV_PATTERN_RAID0, LOV_PATTERN_RAID1 */
        __u64 lmm_object_id;      /* LOV object ID */
        __u64 lmm_object_gr;      /* LOV object group */
        __u32 lmm_stripe_size;    /* size of stripe in bytes */
        __u16 lmm_stripe_count;   /* num stripes in use for this object */
        __u16 lmm_stripe_offset;  /* starting stripe offset in lmm_objects */
        struct lov_user_ost_data_v1 lmm_objects[0]; /* per-stripe data */
} __attribute__((packed));

struct lov_user_md_v3 {           /* LOV EA user data (host-endian) */
        __u32 lmm_magic;          /* magic number = LOV_USER_MAGIC_V3 */
        __u32 lmm_pattern;        /* LOV_PATTERN_RAID0, LOV_PATTERN_RAID1 */
        __u64 lmm_object_id;      /* LOV object ID */
        __u64 lmm_object_gr;      /* LOV object group */
        __u32 lmm_stripe_size;    /* size of stripe in bytes */
        __u16 lmm_stripe_count;   /* num stripes in use for this object */
        __u16 lmm_stripe_offset;  /* starting stripe offset in lmm_objects */
        char  lmm_pool_name[LOV_MAXPOOLNAME]; /* pool name */
        struct lov_user_ost_data_v1 lmm_objects[0]; /* per-stripe data */
} __attribute__((packed));

/* Compile with -D_LARGEFILE64_SOURCE or -D_GNU_SOURCE (or #define) to
 * use this.  It is unsafe to #define those values in this header as it
 * is possible the application has already #included <sys/stat.h>. */
#ifdef HAVE_LOV_USER_MDS_DATA
#define lov_user_mds_data lov_user_mds_data_v1
struct lov_user_mds_data_v1 {
        lstat_t lmd_st;                 /* MDS stat struct */
        struct lov_user_md_v1 lmd_lmm;  /* LOV EA V1 user data */
} __attribute__((packed));

struct lov_user_mds_data_v3 {
        lstat_t lmd_st;                 /* MDS stat struct */
        struct lov_user_md_v3 lmd_lmm;  /* LOV EA V3 user data */
} __attribute__((packed));
#endif

struct ll_recreate_obj {
        __u64 lrc_id;
        __u64 lrc_group;
        __u32 lrc_ost_idx;
};

struct ll_fid {
        __u64 id;         /* holds object id */
        __u32 generation; /* holds object generation */
        __u32 f_type;     /* holds object type or stripe idx when passing it to
                           * OST for saving into EA. */
};

struct filter_fid {
        struct ll_fid   ff_fid;  /* ff_fid.f_type == file stripe number */
        __u64           ff_objid;
        __u64           ff_group;
};

struct obd_uuid {
        char uuid[40];
};

static inline int obd_uuid_equals(struct obd_uuid *u1, struct obd_uuid *u2)
{
        return strcmp((char *)u1->uuid, (char *)u2->uuid) == 0;
}

static inline int obd_uuid_empty(struct obd_uuid *uuid)
{
        return uuid->uuid[0] == '\0';
}

static inline void obd_str2uuid(struct obd_uuid *uuid, const char *tmp)
{
        strncpy((char *)uuid->uuid, tmp, sizeof(*uuid));
        uuid->uuid[sizeof(*uuid) - 1] = '\0';
}

/* For printf's only, make sure uuid is terminated */
static inline char *obd_uuid2str(struct obd_uuid *uuid)
{
        if (uuid->uuid[sizeof(*uuid) - 1] != '\0') {
                /* Obviously not safe, but for printfs, no real harm done...
                   we're always null-terminated, even in a race. */
                static char temp[sizeof(*uuid)];
                memcpy(temp, uuid->uuid, sizeof(*uuid) - 1);
                temp[sizeof(*uuid) - 1] = '\0';
                return temp;
        }
        return (char *)(uuid->uuid);
}

/* Extract fsname from uuid (or target name) of a target
   e.g. (myfs-OST0007_UUID -> myfs)
   see also deuuidify. */
static inline void obd_uuid2fsname(char *buf, char *uuid, int buflen)
{
        char *p;

        strncpy(buf, uuid, buflen - 1);
        buf[buflen - 1] = '\0';
        p = strrchr(buf, '-');
        if (p)
           *p = '\0';
}

/**
 * File IDentifier.
 *
 * FID is a cluster-wide unique identifier of a file or an object (stripe).
 * FIDs are never reused.
 */
struct lu_fid {
        /**
         * FID sequence. Sequence is a unit of migration: all files (objects)
         * with FIDs from a given sequence are stored on the same server.
         * Lustre should support 2^64 objects, so even if each sequence
         * has only a single object we can still enumerate 2^64 objects.
         */
        __u64 f_seq;
        /** FID number within sequence. */
        __u32 f_oid;
        /**
         * FID version, used to distinguish different versions (in the sense
         * of snapshots, etc.) of the same file system object. Not currently
         * used.
         */
        __u32 f_ver;
};

/* Userspace should treat lu_fid as opaque, and only use the following methods
   to print or parse them.  Other functions (e.g. compare, swab) could be moved
   here from lustre_idl.h if needed. */
typedef struct lu_fid lustre_fid;

/* printf display format
   e.g. printf("file FID is "DFID"\n", PFID(fid)); */
#define DFID "["LPX64":0x%x:0x%x]"
#define PFID(fid)     \
        (fid)->f_seq, \
        (fid)->f_oid, \
        (fid)->f_ver

/* scanf input parse format -- strip '[' first.
   e.g. sscanf(fidstr, SFID, RFID(&fid)); */
/* #define SFID "0x"LPX64i":0x"LPSZX":0x"LPSZX""
liblustreapi.c:2893: warning: format '%lx' expects type 'long unsigned int *', but argument 4 has type 'unsigned int *'
liblustreapi.c:2893: warning: format '%lx' expects type 'long unsigned int *', but argument 5 has type 'unsigned int *'
*/
#define SFID "0x"LPX64i":0x%x:0x%x"
#define RFID(fid)     \
        &((fid)->f_seq), \
        &((fid)->f_oid), \
        &((fid)->f_ver)


/********* Quotas **********/

/* these must be explicitly translated into linux Q_* in ll_dir_ioctl */
#define LUSTRE_Q_QUOTAON    0x800002     /* turn quotas on */
#define LUSTRE_Q_QUOTAOFF   0x800003     /* turn quotas off */
#define LUSTRE_Q_GETINFO    0x800005     /* get information about quota files */
#define LUSTRE_Q_SETINFO    0x800006     /* set information about quota files */
#define LUSTRE_Q_GETQUOTA   0x800007     /* get user quota structure */
#define LUSTRE_Q_SETQUOTA   0x800008     /* set user quota structure */
/* lustre-specific control commands */
#define LUSTRE_Q_INVALIDATE  0x80000b     /* invalidate quota data */
#define LUSTRE_Q_FINVALIDATE 0x80000c     /* invalidate filter quota data */

#define UGQUOTA 2       /* set both USRQUOTA and GRPQUOTA */

struct if_quotacheck {
        char                    obd_type[16];
        struct obd_uuid         obd_uuid;
};

#define IDENTITY_DOWNCALL_MAGIC 0x6d6dd620

/* permission */
#define N_PERMS_MAX      64

struct perm_downcall_data {
        __u64 pdd_nid;
        __u32 pdd_perm;
};

struct identity_downcall_data {
        __u32                            idd_magic;
        __u32                            idd_err;
        __u32                            idd_uid;
        __u32                            idd_gid;
        __u32                            idd_nperms;
        struct perm_downcall_data idd_perms[N_PERMS_MAX];
        __u32                            idd_ngroups;
        __u32                            idd_groups[0];
};

/* for non-mapped uid/gid */
#define NOBODY_UID      99
#define NOBODY_GID      99

#define INVALID_ID      (-1)

enum {
        RMT_LSETFACL    = 1,
        RMT_LGETFACL    = 2,
        RMT_RSETFACL    = 3,
        RMT_RGETFACL    = 4
};

#ifdef NEED_QUOTA_DEFS
#ifndef QIF_BLIMITS
#define QIF_BLIMITS     1
#define QIF_SPACE       2
#define QIF_ILIMITS     4
#define QIF_INODES      8
#define QIF_BTIME       16
#define QIF_ITIME       32
#define QIF_LIMITS      (QIF_BLIMITS | QIF_ILIMITS)
#define QIF_USAGE       (QIF_SPACE | QIF_INODES)
#define QIF_TIMES       (QIF_BTIME | QIF_ITIME)
#define QIF_ALL         (QIF_LIMITS | QIF_USAGE | QIF_TIMES)
#endif

#endif /* !__KERNEL__ */

typedef enum lustre_quota_version {
        LUSTRE_QUOTA_V2 = 1
} lustre_quota_version_t;

/* XXX: same as if_dqinfo struct in kernel */
struct obd_dqinfo {
        __u64 dqi_bgrace;
        __u64 dqi_igrace;
        __u32 dqi_flags;
        __u32 dqi_valid;
};

/* XXX: same as if_dqblk struct in kernel, plus one padding */
struct obd_dqblk {
        __u64 dqb_bhardlimit;
        __u64 dqb_bsoftlimit;
        __u64 dqb_curspace;
        __u64 dqb_ihardlimit;
        __u64 dqb_isoftlimit;
        __u64 dqb_curinodes;
        __u64 dqb_btime;
        __u64 dqb_itime;
        __u32 dqb_valid;
        __u32 padding;
};

enum {
        QC_GENERAL      = 0,
        QC_MDTIDX       = 1,
        QC_OSTIDX       = 2,
        QC_UUID         = 3
};

struct if_quotactl {
        __u32                   qc_cmd;
        __u32                   qc_type;
        __u32                   qc_id;
        __u32                   qc_stat;
        __u32                   qc_valid;
        __u32                   qc_idx;
        struct obd_dqinfo       qc_dqinfo;
        struct obd_dqblk        qc_dqblk;
        char                    obd_type[16];
        struct obd_uuid         obd_uuid;
};


/********* Changelogs **********/
/** Changelog record types */
enum changelog_rec_type {
        CL_MARK     = 0,
        CL_CREATE   = 1,  /* namespace */
        CL_MKDIR    = 2,  /* namespace */
        CL_HARDLINK = 3,  /* namespace */
        CL_SOFTLINK = 4,  /* namespace */
        CL_MKNOD    = 5,  /* namespace */
        CL_UNLINK   = 6,  /* namespace */
        CL_RMDIR    = 7,  /* namespace */
        CL_RENAME   = 8,  /* namespace */
        CL_EXT      = 9,  /* namespace extended record (2nd half of rename) */
        CL_OPEN     = 10, /* not currently used */
        CL_CLOSE    = 11, /* may be written to log only with mtime change */
        CL_IOCTL    = 12,
        CL_TRUNC    = 13,
        CL_SETATTR  = 14,
        CL_XATTR    = 15,
        CL_HSM      = 16, /* HSM specific events, see flags */
        CL_TIME     = 17, /* mtime, atime, ctime change only */
        CL_LAST
};

static inline const char *changelog_type2str(int type) {
        static const char *changelog_str[] = {
                "MARK",  "CREAT", "MKDIR", "HLINK", "SLINK", "MKNOD", "UNLNK",
                "RMDIR", "RNMFM", "RNMTO", "OPEN",  "CLOSE", "IOCTL", "TRUNC",
                "SATTR", "XATTR", "HSM",   "TIME"  };
        if (type >= 0 && type < CL_LAST)
                return changelog_str[type];
        return NULL;
}

/* per-record flags */
#define CLF_VERSION  0x1000
#define CLF_FLAGMASK 0x0FFF
/* Anything under the flagmask may be per-type (if desired) */

#define CR_MAXSIZE (PATH_MAX + sizeof(struct changelog_rec))
struct changelog_rec {
        __u16                 cr_namelen;
        __u16                 cr_flags; /**< (flags&CLF_FLAGMASK)|CLF_VERSION */
        __u32                 cr_type;  /**< \a changelog_rec_type */
        __u64                 cr_index; /**< changelog record number */
        __u64                 cr_prev;  /**< last index for this target fid */
        __u64                 cr_time;
        union {
                lustre_fid    cr_tfid;        /**< target fid */
                __u32         cr_markerflags; /**< CL_MARK flags */
        };
        lustre_fid            cr_pfid;        /**< parent fid */
        char                  cr_name[0];     /**< last element */
} __attribute__((packed));

struct ioc_changelog {
        __u64 icc_recno;
        __u32 icc_mdtindex;
        __u32 icc_id;
        __u32 icc_flags;
};

enum changelog_message_type {
        CL_RECORD = 10, /* message is a changelog_rec */
        CL_EOF    = 11, /* at end of current changelog */
};

/********* Misc **********/

#ifndef offsetof
# define offsetof(typ,memb)     ((unsigned long)((char *)&(((typ *)0)->memb)))
#endif

#define dot_lustre_name ".lustre"


/********* HSM **********/


#define HSM_FLAGS_MASK  0


enum hsm_message_type {
        HMT_ACTION_LIST = 100, /* message is a hsm_action_list */
};

/* User-generated (ioctl) request types */
enum hsm_request {
        HSMR_ARCHIVE = 10, /* copy to hsm */
        HSMR_RESTORE = 11, /* prestage */
        HSMR_RELEASE = 12, /* drop ost objects */
        HSMR_REMOVE  = 13, /* remove from archive */
        HSMR_CANCEL  = 14
};

/* Copytool commands */
enum hsm_action {
        HSMA_ARCHIVE = 20, /* arbitrary offset */
        HSMA_RESTORE = 21,
        HSMA_REMOVE  = 22,
        HSMA_CANCEL  = 23
};

/* Copytool item action description */
struct hsm_action_item {
        __u32      hai_len;     /* valid size of this struct */
        __u32      hai_action;  /* enum actually, but use known size */
        lustre_fid hai_fid;     /* Lustre FID to operated on */
        __u64      hai_cookie;  /* action cookie from coordinator */
        __u64      hai_extent_start;  /* byte range to operate on */
        __u64      hai_extent_end;
        __u64      hai_gid;     /* grouplock id */
        char       hai_data[0]; /* variable length */
} __attribute__((packed));

/* Copytool action list */
#define HAL_VERSION 1
#define HAL_MAXSIZE 4096 /* bytes, used in userspace only */
struct hsm_action_list {
        __u32 hal_version;
        __u32 hal_count;       /* number of hai's to follow */
        __u32 hal_archive_num; /* which archive backend */
        __u32 padding1;
        char  hal_fsname[0];   /* null-terminated */
        /* struct hsm_action_item[hal_count] follows, aligned on 8-byte
           boundaries. See hai_zero */
} __attribute__((packed));

/* Return pointer to first hai in action list */
static __inline__ struct hsm_action_item * hai_zero(struct hsm_action_list *hal)
{
        return (struct hsm_action_item *)(hal->hal_fsname +
                                          cfs_size_round(strlen(hal-> \
                                                                hal_fsname)));
}
/* Return pointer to next hai */
static __inline__ struct hsm_action_item * hai_next(struct hsm_action_item *hai)
{
        return (struct hsm_action_item *)((char *)hai +
                                          cfs_size_round(hai->hai_len));
}

/** @} lustreuser */

#endif /* _LUSTRE_USER_H */
