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
 */

#ifndef EXPORT_SYMTAB
# define EXPORT_SYMTAB
#endif
#define DEBUG_SUBSYSTEM S_SEC
#ifdef __KERNEL__
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/dcache.h>
#include <linux/fs.h>
#include <linux/random.h>
#include <linux/mutex.h>
#else
#include <liblustre.h>
#endif

#include <obd.h>
#include <obd_class.h>
#include <obd_support.h>
#include <lustre/lustre_idl.h>
#include <lustre_net.h>
#include <lustre_import.h>
#include <lprocfs_status.h>
#include <lustre_sec.h>

#include "gss_err.h"
#include "gss_internal.h"
#include "gss_api.h"

static struct proc_dir_entry *gss_proc_root = NULL;
static struct proc_dir_entry *gss_proc_lk = NULL;

/*
 * statistic of "out-of-sequence-window"
 */
static struct {
        cfs_spinlock_t  oos_lock;
        cfs_atomic_t    oos_cli_count;       /* client occurrence */
        int             oos_cli_behind;      /* client max seqs behind */
        cfs_atomic_t    oos_svc_replay[3];   /* server replay detected */
        cfs_atomic_t    oos_svc_pass[3];     /* server verified ok */
} gss_stat_oos = {
        .oos_cli_count  = CFS_ATOMIC_INIT(0),
        .oos_cli_behind = 0,
        .oos_svc_replay = { CFS_ATOMIC_INIT(0), },
        .oos_svc_pass   = { CFS_ATOMIC_INIT(0), },
};

void gss_stat_oos_record_cli(int behind)
{
        cfs_atomic_inc(&gss_stat_oos.oos_cli_count);

        cfs_spin_lock(&gss_stat_oos.oos_lock);
        if (behind > gss_stat_oos.oos_cli_behind)
                gss_stat_oos.oos_cli_behind = behind;
        cfs_spin_unlock(&gss_stat_oos.oos_lock);
}

void gss_stat_oos_record_svc(int phase, int replay)
{
        LASSERT(phase >= 0 && phase <= 2);

        if (replay)
                cfs_atomic_inc(&gss_stat_oos.oos_svc_replay[phase]);
        else
                cfs_atomic_inc(&gss_stat_oos.oos_svc_pass[phase]);
}

static int gss_proc_read_oos(char *page, char **start, off_t off, int count,
                             int *eof, void *data)
{
        int written;

        written = snprintf(page, count,
                        "seqwin:                %u\n"
                        "backwin:               %u\n"
                        "client fall behind seqwin\n"
                        "  occurrence:          %d\n"
                        "  max seq behind:      %d\n"
                        "server replay detected:\n"
                        "  phase 0:             %d\n"
                        "  phase 1:             %d\n"
                        "  phase 2:             %d\n"
                        "server verify ok:\n"
                        "  phase 2:             %d\n",
                        GSS_SEQ_WIN_MAIN,
                        GSS_SEQ_WIN_BACK,
                        cfs_atomic_read(&gss_stat_oos.oos_cli_count),
                        gss_stat_oos.oos_cli_behind,
                        cfs_atomic_read(&gss_stat_oos.oos_svc_replay[0]),
                        cfs_atomic_read(&gss_stat_oos.oos_svc_replay[1]),
                        cfs_atomic_read(&gss_stat_oos.oos_svc_replay[2]),
                        cfs_atomic_read(&gss_stat_oos.oos_svc_pass[2]));

        return written;
}

static int gss_proc_write_secinit(struct file *file, const char *buffer,
                                  unsigned long count, void *data)
{
        int rc;

        rc = gss_do_ctx_init_rpc((char *) buffer, count);
        if (rc) {
                LASSERT(rc < 0);
                return rc;
        }

        return ((int) count);
}

static struct lprocfs_vars gss_lprocfs_vars[] = {
        { "replays", gss_proc_read_oos, NULL },
        { "init_channel", NULL, gss_proc_write_secinit, NULL, NULL, 0222 },
        { NULL }
};

/*
 * for userspace helper lgss_keyring.
 *
 * debug_level: [0, 4], defined in utils/gss/lgss_utils.h
 */
static int gss_lk_debug_level = 1;

static int gss_lk_proc_read_dl(char *page, char **start, off_t off,
                               int count, int *eof, void *data)
{
        return snprintf(page, count, "%u\n", gss_lk_debug_level);
}

static int gss_lk_proc_write_dl(struct file *file, const char *buffer,
                                unsigned long count, void *data)
{
        int     val, rc;

        rc = lprocfs_write_helper(buffer, count, &val);
        if (rc < 0)
                return rc;

        if (val < 0 || val > 4)
                return -ERANGE;

        gss_lk_debug_level = val;
        return count;
}

static struct lprocfs_vars gss_lk_lprocfs_vars[] = {
        { "debug_level", gss_lk_proc_read_dl, gss_lk_proc_write_dl, NULL },
        { NULL }
};

void gss_exit_lproc(void)
{
        if (gss_proc_lk) {
                lprocfs_remove(&gss_proc_lk);
                gss_proc_lk = NULL;
        }

        if (gss_proc_root) {
                lprocfs_remove(&gss_proc_root);
                gss_proc_root = NULL;
        }
}

int gss_init_lproc(void)
{
        int     rc;

        cfs_spin_lock_init(&gss_stat_oos.oos_lock);

        gss_proc_root = lprocfs_register("gss", sptlrpc_proc_root,
                                         gss_lprocfs_vars, NULL);
        if (IS_ERR(gss_proc_root)) {
                gss_proc_root = NULL;
                GOTO(err_out, rc = PTR_ERR(gss_proc_root));
        }

        gss_proc_lk = lprocfs_register("lgss_keyring", gss_proc_root,
                                       gss_lk_lprocfs_vars, NULL);
        if (IS_ERR(gss_proc_lk)) {
                gss_proc_lk = NULL;
                GOTO(err_out, rc = PTR_ERR(gss_proc_root));
        }

        return 0;

err_out:
        CERROR("failed to initialize gss lproc entries: %d\n", rc);
        gss_exit_lproc();
        return rc;
}
