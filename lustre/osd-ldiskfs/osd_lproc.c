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
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/osd/osd_lproc.c
 *
 * Author: Mikhail Pershin <tappro@sun.com>
 * Author: Alex Zhuravlev <bzzz@sun.com>
 */

#define DEBUG_SUBSYSTEM S_CLASS

#include <obd.h>
#include <lprocfs_status.h>
#include <lu_time.h>

#include <lustre/lustre_idl.h>

#ifdef DMU_OSD_BUILD
#include "udmu.h"
#endif

#include "osd_internal.h"

static int osd_stats_init(struct osd_device *osd)
{
        int result;
        ENTRY;

        osd->od_stats = lprocfs_alloc_stats(LPROC_OSD_LAST, 0);
        if (osd->od_stats != NULL) {
                result = lprocfs_register_stats(osd->od_proc_entry, "stats",
                                                osd->od_stats);
                if (result)
                        GOTO(out, result);

                lprocfs_counter_init(osd->od_stats, LPROC_OSD_GET_PAGE,
                                     LPROCFS_CNTR_AVGMINMAX|LPROCFS_CNTR_STDDEV,
                                     "get_page", "usec");
                lprocfs_counter_init(osd->od_stats, LPROC_OSD_NO_PAGE,
                                     LPROCFS_CNTR_AVGMINMAX,
                                     "get_page_failures", "num");
                lprocfs_counter_init(osd->od_stats, LPROC_OSD_CACHE_ACCESS,
                                     LPROCFS_CNTR_AVGMINMAX,
                                     "cache_access", "pages");
                lprocfs_counter_init(osd->od_stats, LPROC_OSD_CACHE_HIT,
                                     LPROCFS_CNTR_AVGMINMAX,
                                     "cache_hit", "pages");
                lprocfs_counter_init(osd->od_stats, LPROC_OSD_CACHE_MISS,
                                     LPROCFS_CNTR_AVGMINMAX,
                                     "cache_miss", "pages");
        } else
                result = -ENOMEM;

out:
        RETURN(result);
}

int osd_procfs_init(struct osd_device *osd, const char *name)
{
        struct lprocfs_static_vars lvars;
        struct lu_device    *ld = &osd->od_dt_dev.dd_lu_dev;
        struct obd_type     *type;
        int                  rc;
        ENTRY;

        type = ld->ld_type->ldt_obd_type;

        LASSERT(name != NULL);
        LASSERT(type != NULL);

        /* Find the type procroot and add the proc entry for this device */
        lprocfs_osd_init_vars(&lvars);
        osd->od_proc_entry = lprocfs_register(name, type->typ_procroot,
                                              lvars.obd_vars, osd);
        if (IS_ERR(osd->od_proc_entry)) {
                rc = PTR_ERR(osd->od_proc_entry);
                CERROR("Error %d setting up lprocfs for %s\n", rc, name);
                osd->od_proc_entry = NULL;
                GOTO(out, rc);
        }

        osd_quota_procfs_init(osd);

        rc = osd_stats_init(osd);
        lprocfs_put_lperef(osd->od_proc_entry);

        EXIT;
out:
        if (rc)
               osd_procfs_fini(osd);
        return rc;
}

int osd_procfs_fini(struct osd_device *osd)
{
        ENTRY;

        if (osd->od_stats)
                lprocfs_free_stats(&osd->od_stats);

        if (osd->od_proc_entry) {
                lprocfs_remove(&osd->od_proc_entry);
                osd->od_proc_entry = NULL;
        }

        RETURN(0);
}

void osd_lprocfs_time_start(const struct lu_env *env)
{
        lu_lprocfs_time_start(env);
}

void osd_lprocfs_time_end(const struct lu_env *env, struct osd_device *osd,
                          int idx)
{
        lu_lprocfs_time_end(env, osd->od_stats, idx);
}

int lprocfs_osd_rd_blksize(char *page, char **start, off_t off, int count,
                           int *eof, void *data)
{
        struct osd_device *osd;
        int rc;

        LIBCFS_PARAM_GET_DATA(osd, data, NULL);
        rc = osd_statfs(NULL, &osd->od_dt_dev, &osd->od_osfs);
        if (!rc)
                rc = libcfs_param_snprintf(page, count, data, LP_U32, "%d\n",
					   (unsigned) osd->od_osfs.os_bsize);
        return rc;
}

int lprocfs_osd_rd_kbytestotal(char *page, char **start, off_t off, int count,
                               int *eof, void *data)
{
        struct osd_device *osd;
        int rc;

        LIBCFS_PARAM_GET_DATA(osd, data, NULL);
        rc = osd_statfs(NULL, &osd->od_dt_dev, &osd->od_osfs);
        if (!rc) {
                __u32 blk_size = osd->od_osfs.os_bsize >> 10;
                __u64 result = osd->od_osfs.os_blocks;

                while (blk_size >>= 1)
                        result <<= 1;

                rc = libcfs_param_snprintf(page, count, data, LP_U64,
                                           LPU64"\n", result);
        }
        return rc;
}

int lprocfs_osd_rd_kbytesfree(char *page, char **start, off_t off, int count,
                              int *eof, void *data)
{
        struct osd_device *osd;
        int rc;

        LIBCFS_PARAM_GET_DATA(osd, data, NULL);
        rc = osd_statfs(NULL, &osd->od_dt_dev, &osd->od_osfs);
        if (!rc) {
                __u32 blk_size = osd->od_osfs.os_bsize >> 10;
                __u64 result = osd->od_osfs.os_bfree;

                while (blk_size >>= 1)
                        result <<= 1;

                rc = libcfs_param_snprintf(page, count, data, LP_U64,
                                           LPU64"\n", result);
        }
        return rc;
}

int lprocfs_osd_rd_kbytesavail(char *page, char **start, off_t off, int count,
                               int *eof, void *data)
{
        struct osd_device *osd;
        int rc;

        LIBCFS_PARAM_GET_DATA(osd, data, NULL);
        rc = osd_statfs(NULL, &osd->od_dt_dev, &osd->od_osfs);
        if (!rc) {
                __u32 blk_size = osd->od_osfs.os_bsize >> 10;
                __u64 result = osd->od_osfs.os_bavail;

                while (blk_size >>= 1)
                        result <<= 1;

                rc = libcfs_param_snprintf(page, count, data, LP_U64,
                                           LPU64"\n", result);
        }
        return rc;
}

int lprocfs_osd_rd_filestotal(char *page, char **start, off_t off, int count,
                              int *eof, void *data)
{
        struct osd_device *osd;
        int rc;

        LIBCFS_PARAM_GET_DATA(osd, data, NULL);
        rc = osd_statfs(NULL, &osd->od_dt_dev, &osd->od_osfs);
        if (!rc)
                rc = libcfs_param_snprintf(page, count, data, LP_U64, LPU64"\n",
                                    osd->od_osfs.os_files);

        return rc;
}

int lprocfs_osd_rd_filesfree(char *page, char **start, off_t off, int count,
                             int *eof, void *data)
{
        struct osd_device *osd;
        int rc;

        LIBCFS_PARAM_GET_DATA(osd, data, NULL);
        rc = osd_statfs(NULL, &osd->od_dt_dev, &osd->od_osfs);
        if (!rc)
                rc = libcfs_param_snprintf(page, count, data, LP_U64, LPU64"\n",
                                    osd->od_osfs.os_ffree);
        return rc;
}

int lprocfs_osd_rd_fstype(char *page, char **start, off_t off, int count,
                          int *eof, void *data)
{
        struct obd_device *osd;

        LIBCFS_PARAM_GET_DATA(osd, data, NULL);
        LASSERT(osd != NULL);
#ifdef DMU_OSD_BUILD
        return libcfs_param_snprintf(page, count, data, LP_STR, "%s\n", "zfs");
#else
        return libcfs_param_snprintf(page, count, data, LP_STR,
                                     "%s\n", "ldiskfs");
#endif
}

static int lprocfs_osd_rd_mntdev(char *page, char **start, off_t off, int count,
                                 int *eof, void *data)
{
        struct osd_device *osd;

        LIBCFS_PARAM_GET_DATA(osd, data, NULL);
        LASSERT(osd != NULL);

        return libcfs_param_snprintf(page, count, data, LP_STR,
                                     "%s\n", osd->od_mntdev);
}

#ifndef DMU_OSD_BUILD
/* FIXME enabling/disabling read cache is not supported in the
 * DMU-OSD yet. We should get/set the primarycache property here */
static int lprocfs_osd_rd_cache(char *page, char **start, off_t off,
                                int count, int *eof, void *data)
{
        struct osd_device *osd;

        LIBCFS_PARAM_GET_DATA(osd, data, NULL);
        LASSERT(osd != NULL);

        return libcfs_param_snprintf(page, count, data, LP_U32,
                                     "%u\n", osd->od_read_cache);
}

static int lprocfs_osd_wr_cache(libcfs_file_t *file, const char *buffer,
                                unsigned long count, void *data)
{
        struct osd_device *osd;
        int val, rc, flag;

        LIBCFS_PARAM_GET_DATA(osd, data, &flag);
        LASSERT(osd != NULL);
        rc = lprocfs_write_helper(buffer, count, &val, flag);

        if (rc)
                return rc;

        osd->od_read_cache = !!val;
        return count;
}


static int lprocfs_osd_rd_wcache(char *page, char **start, off_t off,
                                   int count, int *eof, void *data)
{
        struct osd_device *osd;

        LIBCFS_PARAM_GET_DATA(osd, data, NULL);
        LASSERT(osd != NULL);

        return libcfs_param_snprintf(page, count, data, LP_U32,
                                     "%u\n", osd->od_writethrough_cache);
}

static int lprocfs_osd_wr_wcache(libcfs_file_t *file, const char *buffer,
                                 unsigned long count, void *data)
{
        struct osd_device *osd;
        int val, rc, flag;

        LIBCFS_PARAM_GET_DATA(osd, data, &flag);
        LASSERT(osd != NULL);
        rc = lprocfs_write_helper(buffer, count, &val, flag);

        if (rc)
                return rc;

        osd->od_writethrough_cache = !!val;
        return count;
}
#endif

#ifdef DMU_OSD_BUILD
static int lprocfs_osd_rd_reserved(char *page, char **start, off_t off,
                                   int count, int *eof, void *data)
{
        struct osd_device *osd;
        int rc;

        LIBCFS_PARAM_GET_DATA(osd, data, NULL);
        rc = libcfs_param_snprintf(page, count, data, LP_U32,
			           "%u\n", osd->od_reserved_fraction);
        return rc;
}

static int lprocfs_osd_wr_reserved(libcfs_file_t *file, const char *buffer,
                                   unsigned long count, void *data)
{
        struct osd_device *osd;
        int                val, rc, flag;

        LIBCFS_PARAM_GET_DATA(osd, data, &flag);
        rc = lprocfs_write_helper(buffer, count, &val, flag);
        if (rc)
                return rc;
        if (val < 0)
                return -EINVAL;

        osd->od_reserved_fraction = val;
        return count;
}
#endif

struct lprocfs_vars lprocfs_osd_obd_vars[] = {
        { "blocksize",       lprocfs_osd_rd_blksize,     0, 0 },
        { "kbytestotal",     lprocfs_osd_rd_kbytestotal, 0, 0 },
        { "kbytesfree",      lprocfs_osd_rd_kbytesfree,  0, 0 },
        { "kbytesavail",     lprocfs_osd_rd_kbytesavail, 0, 0 },
        { "filestotal",      lprocfs_osd_rd_filestotal,  0, 0 },
        { "filesfree",       lprocfs_osd_rd_filesfree,   0, 0 },
        { "fstype",          lprocfs_osd_rd_fstype,      0, 0 },
        { "mntdev",          lprocfs_osd_rd_mntdev,      0, 0 },
#ifndef DMU_OSD_BUILD
        { "read_cache_enable",lprocfs_osd_rd_cache,
                             lprocfs_osd_wr_cache,          0 },
        { "writethrough_cache_enable",lprocfs_osd_rd_wcache,
                             lprocfs_osd_wr_wcache,         0 },
#endif
#ifdef DMU_OSD_BUILD
        { "reserved_space",  lprocfs_osd_rd_reserved,
                             lprocfs_osd_wr_reserved,       0 },
#endif
        { 0 }
};

struct lprocfs_vars lprocfs_osd_module_vars[] = {
        { "num_refs",        lprocfs_rd_numrefs,     0, 0 },
        { 0 }
};

void lprocfs_osd_init_vars(struct lprocfs_static_vars *lvars)
{
        lvars->module_vars = lprocfs_osd_module_vars;
        lvars->obd_vars = lprocfs_osd_obd_vars;
}
