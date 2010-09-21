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
 * lustre/osd/osd_internal.h
 * Shared definitions and declarations for zfs/dmu osd
 *
 * Author: Nikita Danilov <nikita@clusterfs.com>
 */

#ifndef _OSD_INTERNAL_H
#define _OSD_INTERNAL_H

#include <dt_object.h>
#include <osd_quota.h>

struct inode;

#define OSD_COUNTERS (0)

#define DMU_RESERVED_FRACTION   25      /* default reserved fraction 1/25 = 4% */
/**
 * Storage representation for fids.
 *
 * Variable size, first byte contains the length of the whole record.
 */
struct osd_fid_pack {
        unsigned char fp_len;
        char fp_area[sizeof(struct lu_fid)];
};

struct osd_thread_info {
        const struct lu_env   *oti_env;

        struct lu_fid          oti_fid;
        /*
         * XXX temporary: for ->i_op calls.
         */
        struct timespec        oti_time;
        /*
         * XXX temporary: for capa operations.
         */
        struct lustre_capa_key oti_capa_key;
        struct lustre_capa     oti_capa;

        struct osd_fid_pack    oti_fid_pack;
};

typedef struct osd_quota_context {
        struct dt_object *qc_slave_uid_dto;
        struct dt_object *qc_slave_gid_dto;
} osd_quota_context_t;

/*
 * osd device.
 */
struct osd_device {
        /* super-class */
        struct dt_device          od_dt_dev;
        /* information about underlying file system */
        udmu_objset_t             od_objset;

        /* Environment for transaction commit callback.
         * Currently, OSD is based on ext3/JBD. Transaction commit in ext3/JBD
         * is serialized, that is there is no more than one transaction commit
         * at a time (JBD journal_commit_transaction() is serialized).
         * This means that it's enough to have _one_ lu_context.
         */
        struct lu_env             od_env_for_commit;

        /*
         * Fid Capability
         */
        unsigned int              od_fl_capa:1;
        unsigned long             od_capa_timeout;
        __u32                     od_capa_alg;
        struct lustre_capa_key   *od_capa_keys;
        cfs_hlist_head_t         *od_capa_hash;

        /*
         * statfs optimization: we cache a bit.
         */
        struct obd_statfs         od_osfs;
        cfs_time_t                od_osfs_age;
        cfs_spinlock_t            od_osfs_lock;

        cfs_proc_dir_entry_t     *od_proc_entry;
        struct lprocfs_stats     *od_stats;

        dmu_buf_t                *od_root_db;
        dmu_buf_t                *od_objdir_db;

        unsigned int              od_rdonly:1;
        char                      od_mntdev[128];
        char                      od_label[MAXNAMELEN];

        int                       od_reserved_fraction;
        osd_quota_context_t       od_qctxt;
};

struct osd_object {
        struct dt_object       oo_dt;
        /*
         * Inode for file system object represented by this osd_object. This
         * inode is pinned for the whole duration of lu_object life.
         *
         * Not modified concurrently (either setup early during object
         * creation, or assigned by osd_object_create() under write lock).
         */
        dmu_buf_t               *oo_db;

        /* protects inode attributes. */
        cfs_semaphore_t         oo_guard;
        cfs_rw_semaphore_t      oo_sem;

        uint64_t                oo_mode;
        uint64_t                oo_type;
};

#define IT_REC_SIZE 256

struct osd_zap_it {
        zap_cursor_t            *ozi_zc;
        struct osd_object       *ozi_obj;
        struct lustre_capa      *ozi_capa;
        unsigned                 ozi_reset:1;     /* 1 -- no need to advance */
        char                     ozi_name[NAME_MAX + 1];
        char                     ozi_rec[IT_REC_SIZE];
};
#define DT_IT2DT(it) (&((struct osd_zap_it *)it)->ozi_obj->oo_dt)

int osd_statfs(const struct lu_env *env, struct dt_device *d, struct obd_statfs *osfs);

#ifdef LPROCFS
enum {
        LPROC_OSD_READ_BYTES = 0,
        LPROC_OSD_WRITE_BYTES = 1,
        LPROC_OSD_GET_PAGE = 2,
        LPROC_OSD_NO_PAGE = 3,
        LPROC_OSD_CACHE_ACCESS = 4,
        LPROC_OSD_CACHE_HIT = 5,
        LPROC_OSD_CACHE_MISS = 6,
        LPROC_OSD_LAST,
};

/* osd_lproc.c */
void lprocfs_osd_init_vars(struct lprocfs_static_vars *lvars);
int osd_procfs_init(struct osd_device *osd, const char *name);
int osd_procfs_fini(struct osd_device *osd);
void osd_lprocfs_time_start(const struct lu_env *env);
void osd_lprocfs_time_end(const struct lu_env *env,
                          struct osd_device *osd, int op);
#endif

/* osd_quota.c */
void osd_set_quota_index_ops(struct dt_object *dt);
int  osd_qctxt_init(const struct lu_env *env, struct lu_device *lu_dev);
void osd_qctxt_finish(const struct lu_env *env, struct lu_device *d);
#ifdef LPROCFS
void osd_quota_procfs_init(struct osd_device *osd);
#endif

/* osd_handler.c */
struct osd_object *osd_dt_obj(const struct dt_object *d);
struct osd_device  *osd_obj2dev (const struct osd_object *o);
void osd_zap_it_fini(const struct lu_env *env, struct dt_it *di);
int osd_zap_it_next(const struct lu_env *env, struct dt_it *di);
struct osd_device *osd_dev(const struct lu_device *d);

#endif /* _OSD_INTERNAL_H */
