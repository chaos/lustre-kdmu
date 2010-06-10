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
 */

#define DEBUG_SUBSYSTEM S_CLASS

#include <obd_support.h>
#include <lprocfs_status.h>

static int libcfs_param_fail_loc_write(libcfs_file_t * filp,
                                       const char *buffer,
                                       unsigned long count, void *data)
{
        int rc;
        long old_fail_loc = obd_fail_loc;

        rc = libcfs_param_intvec_write(filp, buffer, count, data);
        if (old_fail_loc != obd_fail_loc)
                cfs_waitq_signal(&obd_race_waitq);

        return rc;
}

static int libcfs_param_fail_loc_read(char *page, char **start, off_t off,
                                      int count, int *eof, void *data)
{
        int rc;
        long old_fail_loc = obd_fail_loc;

        rc = libcfs_param_intvec_read(page, start, off, count, eof, data);
        if (old_fail_loc != obd_fail_loc)
                cfs_waitq_signal(&obd_race_waitq);

        return rc;
}

static int libcfs_param_timeout_write(libcfs_file_t * filp, const char *buffer,
                                      unsigned long count, void *data)
{
        int rc;

        rc = libcfs_param_intvec_write(filp, buffer, count, data);
        if (ldlm_timeout >= obd_timeout)
                ldlm_timeout = max(obd_timeout / 3, 1U);

        return rc;
}

static int libcfs_param_timeout_read(char *page, char **start, off_t off,
                                     int count, int *eof, void *data)
{
        int rc;

        rc = libcfs_param_intvec_read(page, start, off, count, eof, data);
        if (ldlm_timeout >= obd_timeout)
                ldlm_timeout = max(obd_timeout / 3, 1U);

        return rc;
}

static int libcfs_param_memory_alloc_read(char *page, char **start, off_t off,
                                    int count, int *eof, void *data)
{
        return libcfs_param_snprintf(page, count, data, LP_U64,
                                     LPU64"\n", obd_memory_sum());
}

static int libcfs_param_pages_alloc_read(char *page, char **start, off_t off,
                                         int count, int *eof, void *data)
{
        return libcfs_param_snprintf(page, count, data, LP_U64,
                                     LPU64"\n", obd_pages_sum());
}

static int libcfs_param_mem_max_read(char *page, char **start, off_t off,
                               int count, int *eof, void *data)
{
        return libcfs_param_snprintf(page, count, data, LP_U64,
                                     LPU64"\n", obd_memory_max());
}

static int libcfs_param_pages_max_read(char *page, char **start, off_t off,
                                       int count, int *eof, void *data)
{
        return libcfs_param_snprintf(page, count, data, LP_U64,
                                     LPU64"\n", obd_pages_max());
}

static int
libcfs_param_max_dirty_pages_in_mb_read(char *page, char **start, off_t off,
                                        int count, int *eof, void *data)
{
        unsigned int *value;
        int mult;
        int rc;

        LIBCFS_PARAM_GET_DATA(value, data, NULL);
        mult = 1 << (20 - CFS_PAGE_SHIFT);
        rc = lprocfs_read_frac_helper(page, count, *value, mult);
        if (rc > 0)
                rc = libcfs_param_snprintf(page, count, data, LP_DB,
                                           NULL, NULL);

        return rc;
}

static int
libcfs_param_max_dirty_pages_in_mb_write(libcfs_file_t * filp,
                                         const char *buffer,
                                         unsigned long count, void *data)
{
        unsigned int *value;
        int rc, flag = 0;

        LIBCFS_PARAM_GET_DATA(value, data, &flag);
        rc = lprocfs_write_frac_helper(buffer, count, value,
                                       1 << (20 - CFS_PAGE_SHIFT), flag);
        /* Don't allow them to let dirty pages exceed 90% of system memory,
         * and set a hard minimum of 4MB. */
        if (obd_max_dirty_pages > ((cfs_num_physpages / 10) * 9)) {
                CERROR("Refusing to set max dirty pages to %u, which "
                       "is more than 90%% of available RAM; setting to %lu\n",
                       obd_max_dirty_pages, ((cfs_num_physpages / 10) * 9));
                obd_max_dirty_pages = ((cfs_num_physpages / 10) * 9);
        } else if (obd_max_dirty_pages < 4 << (20 - CFS_PAGE_SHIFT)) {
                obd_max_dirty_pages = 4 << (20 - CFS_PAGE_SHIFT);
        }

        return rc;
}

#ifdef RANDOM_FAIL_ALLOC
static int
libcfs_param_alloc_fail_rate_read(char *page, char **start, off_t off,
                                  int count, int *eof, void *data)
{
        unsigned int *value;
        int rc;

        LIBCFS_PARAM_GET_DATA(value, data, NULL);
        rc = lprocfs_read_frac_helper(page, count, *value, OBD_ALLOC_FAIL_MULT);
        if (rc > 0)
                rc = libcfs_param_snprintf(page, count, data, LP_DB,
                                           NULL, NULL);

        return rc;
}

static int libcfs_param_alloc_fail_rate_write(libcfs_file_t * filp,
                                              const char *buffer,
                                              unsigned long count, void *data)
{
        unsigned int *value;
        int flag = 0;

        LIBCFS_PARAM_GET_DATA(value, data, &flag);
        return lprocfs_write_frac_helper(buffer, count, value,
                                         OBD_ALLOC_FAIL_MULT, flag);
}
#endif

static struct libcfs_param_ctl_table libcfs_param_obd_table[] = {
        {
                .name   = "fail_loc",
                .data   = &obd_fail_loc,
                .mode   = 0644,
                .read   = libcfs_param_fail_loc_read,
                .write  = libcfs_param_fail_loc_write
        },
        {
                .name   = "fail_val",
                .data   = &obd_fail_val,
                .mode   = 0644,
                .read   = libcfs_param_intvec_read,
                .write  = libcfs_param_intvec_write
        },
        {
                .name   = "timeout",
                .data   = &obd_timeout,
                .mode   = 0644,
                .read   = libcfs_param_timeout_read,
                .write  = libcfs_param_timeout_write,
        },
        {
                .name   = "debug_peer_on_timeout",
                .data   = &obd_debug_peer_on_timeout,
                .mode   = 0644,
                .read   = libcfs_param_intvec_read,
                .write  = libcfs_param_intvec_write
        },
        {
                .name   = "dump_on_timeout",
                .data   = &obd_dump_on_timeout,
                .mode   = 0644,
                .read   = libcfs_param_intvec_read,
                .write  = libcfs_param_intvec_write
        },
        {
                .name   = "dump_on_eviction",
                .data   = &obd_dump_on_eviction,
                .mode   = 0644,
                .read   = libcfs_param_intvec_read,
                .write  = libcfs_param_intvec_write
        },
        {
                .name   = "memused",
                .data   = NULL,
                .mode   = 0444,
                .read   = libcfs_param_memory_alloc_read
        },
        {
                .name   = "pagesused",
                .data   = NULL,
                .mode   = 0444,
                .read   = libcfs_param_pages_alloc_read
        },
        {
                .name   = "memused_max",
                .data   = NULL,
                .mode   = 0444,
                .read   = libcfs_param_mem_max_read
        },
        {
                .name   = "pagesused_max",
                .data   = NULL,
                .mode   = 0444,
                .read   = libcfs_param_pages_max_read
        },
        {
                .name   = "ldlm_timeout",
                .data   = &ldlm_timeout,
                .mode   = 0644,
                .read   = libcfs_param_timeout_read,
                .write  = libcfs_param_timeout_write
        },
#ifdef RANDOM_FAIL_ALLOC
        {
                .name   = "alloc_fail_rate",
                .data   = &obd_alloc_fail_rate,
                .mode   = 0644,
                .read   = libcfs_param_alloc_fail_rate_read,
                .write  = libcfs_param_alloc_fail_rate_write
        },
#endif
        {
                .name   = "max_dirty_mb",
                .data   = &obd_max_dirty_pages,
                .mode   = 0644,
                .read   = libcfs_param_max_dirty_pages_in_mb_read,
                .write  = libcfs_param_max_dirty_pages_in_mb_write
        },
        {
                .name   = "at_min",
                .data   = &at_min,
                .mode   = 0644,
                .read   = libcfs_param_intvec_read,
                .write  = libcfs_param_intvec_write
        },
        {
                .name   = "at_max",
                .data   = &at_max,
                .mode   = 0644,
                .read   = libcfs_param_intvec_read,
                .write  = libcfs_param_intvec_write
        },
        {
                .name   = "at_extra",
                .data   = &at_extra,
                .mode   = 0644,
                .read   = libcfs_param_intvec_read,
                .write  = libcfs_param_intvec_write
        },
        {
                .name   = "at_early_margin",
                .data   = &at_early_margin,
                .mode   = 0644,
                .read   = libcfs_param_intvec_read,
                .write  = libcfs_param_intvec_write
        },
        {
                .name   = "at_history",
                .data   = &at_history,
                .mode   = 0644,
                .read   = libcfs_param_intvec_read,
                .write  = libcfs_param_intvec_write
        },
        { 0 }
};

void obd_params_init (void)
{
        /* register sysctl_table into libcfs_param_tree */
        libcfs_param_sysctl_init("lustre", libcfs_param_obd_table,
                                 libcfs_param_get_root());
}
