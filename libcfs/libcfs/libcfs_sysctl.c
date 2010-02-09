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
 * libcfs/libcfs/linux/linux-proc.c
 *
 * Author: Zach Brown <zab@zabbo.net>
 * Author: Peter J. Braam <braam@clusterfs.com>
 * Author: Phil Schwan <phil@clusterfs.com>
 */

#ifndef EXPORT_SYMTAB
# define EXPORT_SYMTAB
#endif

# define DEBUG_SUBSYSTEM S_LNET

#include <libcfs/libcfs.h>
#include <libcfs/params_tree.h>
#include "tracefile.h"

extern char lnet_upcall[1024];
/**
 * The path of debug log dump upcall script.
 */
extern char lnet_debug_log_upcall[1024];

static int libcfs_param_bitmasks_read(char *page, char **start, off_t off,
                                      int count, int *eof, void *data)
{
        const int     tmpstrlen = 512;
        int           rc;
        unsigned int *mask = ((lparcb_t *)data)->cb_data;
        int           is_subsys = (mask == &libcfs_subsystem_debug) ? 1 : 0;

        *eof = 1;
        libcfs_debug_mask2str(page, tmpstrlen, *mask, is_subsys);
        rc = strlen(page);
        if (rc > 0)
                rc = libcfs_param_snprintf(page, count, data, LP_STR,
                                           NULL, NULL);

        return rc;
}

static int libcfs_param_bitmasks_write(libcfs_file_t *filp,
                                       const char *buffer,
                                       unsigned long count, void *data)
{
        int           rc;
        unsigned int *mask = ((lparcb_t *)data)->cb_data;
        int           is_subsys = (mask == &libcfs_subsystem_debug) ? 1 : 0;
        int           is_printk = (mask == &libcfs_printk) ? 1 : 0;

        rc = libcfs_debug_str2mask(mask, buffer, is_subsys);
        /* Always print LBUG/LASSERT to console, so keep this mask */
        if (is_printk)
                *mask |= D_EMERG;

        return count;

}

static int min_watchdog_ratelimit = 0;          /* disable ratelimiting */
static int max_watchdog_ratelimit = (24*60*60); /* limit to once per day */

static int libcfs_param_dump_kernel_write(libcfs_file_t *filp,
                                          const char *buffer,
                                          unsigned long count, void *data)
{
        int rc = cfs_trace_dump_debug_buffer_usrstr((void *)buffer, count,
                                                ((lparcb_t *)data)->cb_flag);
        return (rc < 0 ? rc : count);
}

static int libcfs_param_daemon_file_read(char *page, char **start, off_t off,
                                         int count, int *eof, void *data)
{
        *eof = 1;
        if (off >= strlen(cfs_tracefile))
                return 0;
        return libcfs_param_snprintf(page, count, data, LP_STR,
                                     "%s", cfs_tracefile + off);
}

static int libcfs_param_daemon_file_write(libcfs_file_t *filp,
                                          const char *buffer,
                                          unsigned long count, void *data)
{
        struct libcfs_param_cb_data *cb_data = data;
        int rc = cfs_trace_daemon_command_usrstr((void *)buffer, count,
                                             cb_data->cb_flag);
        return (rc < 0 ? rc : count);
}

static int libcfs_param_debug_mb_read(char *page, char **start, off_t off,
                                      int count, int *eof, void *data)
{
        int  temp;

        *eof = 1;
        temp = cfs_trace_get_debug_mb();

        return libcfs_param_snprintf(page, count, data, LP_D32, NULL, temp);
}

static int libcfs_param_debug_mb_write(libcfs_file_t *filp,
                                       const char *buffer,
                                       unsigned long count, void *data)
{
        int rc = cfs_trace_set_debug_mb_usrstr((void *)buffer, count,
                                           ((lparcb_t *)data)->cb_flag);
        return (rc < 0 ? rc : count);
}

static int
libcfs_param_console_max_delay_cs_read(char *page, char **start, off_t off,
                                       int count,int *eof, void *data)
{
        int max_delay_cs;

        *eof = 1;
        max_delay_cs = cfs_duration_sec(libcfs_console_max_delay * 100);

        return libcfs_param_snprintf(page, count, data, LP_D32,
                                     NULL, max_delay_cs);
}

static int
libcfs_param_console_max_delay_cs_write(libcfs_file_t *filp,
                                        const char *buffer,
                                        unsigned long count, void *data)
{
        int max_delay_cs;
        cfs_duration_t d;
        void *cb_data = LIBCFS_ALLOC_PARAMDATA(&max_delay_cs);

        libcfs_param_intvec_write(filp, buffer, count, cb_data);
        LIBCFS_FREE_PARAMDATA(cb_data);
        if (max_delay_cs <= 0)
                return -EINVAL;
        d = cfs_time_seconds(max_delay_cs) / 100;
        if (d == 0 || d < libcfs_console_min_delay)
                return -EINVAL;
        libcfs_console_max_delay = d;

        return count;
}

static int
libcfs_param_console_min_delay_cs_read(char *page, char **start, off_t off,
                                       int count,int *eof, void *data)
{
        int min_delay_cs;

        *eof = 1;
        min_delay_cs = cfs_duration_sec(libcfs_console_min_delay * 100);

        return libcfs_param_snprintf(page, count, data, LP_D32,
                                     NULL, min_delay_cs);
}

static int
libcfs_param_console_min_delay_cs_write(libcfs_file_t *filp,
                                        const char *buffer,
                                        unsigned long count, void *data)
{
        int min_delay_cs;
        cfs_duration_t d;
        void *cb_data = LIBCFS_ALLOC_PARAMDATA(&min_delay_cs);

        libcfs_param_intvec_write(filp, buffer, count, cb_data);
        LIBCFS_FREE_PARAMDATA(cb_data);
        if (min_delay_cs <= 0)
                return -EINVAL;
        d = cfs_time_seconds(min_delay_cs) / 100;
        if (d == 0 || d > libcfs_console_max_delay)
                return -EINVAL;
        libcfs_console_min_delay = d;

        return count;
}

static int libcfs_param_console_backoff_read(char *page, char **start,off_t off,
                                             int count,int *eof, void *data)
{
        *eof = 1;
        return libcfs_param_snprintf(page, count, data, LP_U32,
                                     NULL, libcfs_console_backoff);
}

static int
libcfs_param_console_backoff_write(libcfs_file_t *filp, const char *buffer,
                                   unsigned long count, void *data)
{
        int backoff;
        void *cb_data = LIBCFS_ALLOC_PARAMDATA(&backoff);

        libcfs_param_intvec_write(filp, buffer, count, cb_data);
        LIBCFS_FREE_PARAMDATA(cb_data);
        if (backoff <= 0)
                return -EINVAL;
        libcfs_console_backoff = backoff;

        return count;
}

static int libcfs_param_watchdog_write(libcfs_file_t *filp,
                                       const char *buffer,
                                       unsigned long count, void *data)
{
        unsigned long temp;
        void *cb_data = LIBCFS_ALLOC_PARAMDATA(&temp);

        libcfs_param_intvec_write(filp, buffer, count, cb_data);
        LIBCFS_FREE_PARAMDATA(cb_data);
        if (temp < min_watchdog_ratelimit || temp > max_watchdog_ratelimit)
                return -EINVAL;

        return libcfs_param_intvec_write(filp, buffer, count, data);
}

static int libcfs_param_memused_read(char *page, char **start, off_t off,
                                     int count, int *eof, void *data)
{
        int temp;
        cfs_atomic_t *memused = ((lparcb_t *)data)->cb_data;

        *eof = 1;
        temp = cfs_atomic_read(memused);

        return libcfs_param_snprintf(page, count, data, LP_D32, NULL, temp);
}

static struct libcfs_param_ctl_table libcfs_param_lnet_table[] = {
        {
                .name   = "debug",
                .data   = &libcfs_debug,
                .mode   = 0644,
                .read   = libcfs_param_bitmasks_read,
                .write  = libcfs_param_bitmasks_write
        },
        {
                .name   = "subsystem_debug",
                .data   = &libcfs_subsystem_debug,
                .mode   = 0644,
                .read   = libcfs_param_bitmasks_read,
                .write  = libcfs_param_bitmasks_write
        },
        {
                .name   = "printk",
                .data   = &libcfs_printk,
                .mode   = 0644,
                .read   = libcfs_param_bitmasks_read,
                .write  = libcfs_param_bitmasks_write
        },
        {
                .name   = "console_ratelimit",
                .data   = &libcfs_console_ratelimit,
                .mode   = 0644,
                .read   = libcfs_param_intvec_read,
                .write  = libcfs_param_intvec_write
        },
        {
                .name   = "console_max_delay_centisecs",
                .mode   = 0644,
                .read   = libcfs_param_console_max_delay_cs_read,
                .write  = libcfs_param_console_max_delay_cs_write
        },
        {
                .name   = "console_min_delay_centisecs",
                .mode   = 0644,
                .read   = libcfs_param_console_min_delay_cs_read,
                .write  = libcfs_param_console_min_delay_cs_write
        },
        {
                .name   = "console_backoff",
                .mode   = 0644,
                .read   = libcfs_param_console_backoff_read,
                .write  = libcfs_param_console_backoff_write
        },
        {
                .name   = "debug_path",
                .data   = libcfs_debug_file_path_arr,
                .mode   = 0644,
                .read   = libcfs_param_string_read,
                .write  = libcfs_param_string_write
        },
        {
                .name   = "upcall",
                .data   = lnet_upcall,
                .mode   = 0644,
                .read   = libcfs_param_string_read,
                .write  = libcfs_param_string_write
        },
        {
                .name   = "debug_log_upcall",
                .data   = lnet_debug_log_upcall,
                .mode   = 0644,
                .read   = libcfs_param_string_read,
                .write  = libcfs_param_string_write
        },
        {
                .name   = "memused",
                .data   = &libcfs_kmemory,
                .mode   = 0444,
                .read   = libcfs_param_memused_read,
        },
        {
                .name   = "catastrophe",
                .data   = &libcfs_catastrophe,
                .mode   = 0444,
                .read   = libcfs_param_intvec_read,
        },
        {
                .name   = "panic_on_lbug",
                .data   = &libcfs_panic_on_lbug,
                .mode   = 0644,
                .read   = libcfs_param_intvec_read,
                .write  = libcfs_param_intvec_write
        },
        {
                .name   = "dump_kernel",
                .mode   = 0200,
                .write  = libcfs_param_dump_kernel_write
        },
        {
                .name   = "daemon_file",
                .mode   = 0644,
                .read   = libcfs_param_daemon_file_read,
                .write  = libcfs_param_daemon_file_write
        },
        {
                .name   = "debug_mb",
                .mode   = 0644,
                .read   = libcfs_param_debug_mb_read,
                .write  = libcfs_param_debug_mb_write
        },
        {
                .name   = "watchdog_ratelimit",
                .data   = &libcfs_watchdog_ratelimit,
                .mode   = 0644,
                .read   = libcfs_param_intvec_read,
                .write  = libcfs_param_watchdog_write
        },
        {0}
};

int insert_params(void)
{
        /* register sysctl_table into libcfs_param_tree */
        /* we don't use libcfs_param_sysctl_init(),
         * because we will get libcfs_param_lnet_root here.*/
        if (libcfs_param_lnet_root == NULL) {
                libcfs_param_lnet_root = libcfs_param_mkdir("lnet",
                                                     libcfs_param_get_root());
        }
        if (libcfs_param_lnet_root != NULL) {
                libcfs_param_sysctl_register(libcfs_param_lnet_table,
                                             libcfs_param_lnet_root);
                libcfs_param_put(libcfs_param_lnet_root);
        } else
                return -EINVAL;
        return 0;
}

void remove_params(void)
{
        libcfs_param_remove("lnet", libcfs_param_get_root());
}
