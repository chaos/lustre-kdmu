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
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */
#define DEBUG_SUBSYSTEM S_CLASS

#include <linux/version.h>
#include <lprocfs_status.h>
#include <obd.h>
#include <linux/seq_file.h>
#include <linux/version.h>

#include "filter_internal.h"

static int lprocfs_filter_rd_groups(char *page, char **start, off_t off,
                                    int count, int *eof, void *data)
{
        struct obd_device *obd;

        LIBCFS_PARAM_GET_DATA(obd, data, NULL);

        return libcfs_param_snprintf(page, count, data, LP_D32, "%u\n",
                            obd->u.filter.fo_group_count);
}

static int lprocfs_filter_rd_tot_dirty(char *page, char **start, off_t off,
                                       int count, int *eof, void *data)
{
        struct obd_device *obd;

        LIBCFS_PARAM_GET_DATA(obd, data, NULL);
        LASSERT(obd != NULL);

        return libcfs_param_snprintf(page, count, data, LP_U64, LPU64"\n",
                                     obd->u.filter.fo_tot_dirty);
}

static int lprocfs_filter_rd_tot_granted(char *page, char **start, off_t off,
                                         int count, int *eof, void *data)
{
        struct obd_device *obd;

        LIBCFS_PARAM_GET_DATA(obd, data, NULL);
        LASSERT(obd != NULL);

        return libcfs_param_snprintf(page, count, data, LP_U64, LPU64"\n",
                            obd->u.filter.fo_tot_granted);
}

static int lprocfs_filter_rd_tot_pending(char *page, char **start, off_t off,
                                         int count, int *eof, void *data)
{
        struct obd_device *obd;

        LIBCFS_PARAM_GET_DATA(obd, data, NULL);
        LASSERT(obd != NULL);

        return libcfs_param_snprintf(page, count, data, LP_U64, LPU64"\n",
                            obd->u.filter.fo_tot_pending);
}

static int lprocfs_filter_rd_last_id(char *page, char **start, off_t off,
                                     int count, int *eof, void *data)
{
        struct obd_device *obd;
        struct filter_obd *filter;
        int retval = 0, rc, i;
	__u64 temp;

        LIBCFS_PARAM_GET_DATA(obd, data, NULL);
        filter = &obd->u.filter;
        if (obd == NULL || !obd->obd_set_up || obd->obd_stopping)
                return 0;
	temp = filter_last_id(filter, 0);
        rc = libcfs_param_snprintf(page, count, data, LP_U64, LPU64"\n", temp);
        if (rc < 0)
                return rc;
        page += rc;
        count -= rc;
        retval += rc;

        for (i = FID_SEQ_OST_MDT1; i < filter->fo_group_count; i++) {
		temp = filter_last_id(filter, i);
                rc = libcfs_param_snprintf(page, count, data, LP_U64, LPU64"\n",
				           temp);
                if (rc < 0) {
                        retval = rc;
                        break;
                }
                page += rc;
                count -= rc;
                retval += rc;
        }
        return retval;
}

int lprocfs_filter_rd_readcache(char *page, char **start, off_t off, int count,
                                int *eof, void *data)
{
        struct obd_device *obd;

        LIBCFS_PARAM_GET_DATA(obd, data, NULL);

        return libcfs_param_snprintf(page, count, data, LP_U64, LPU64"\n",
                            obd->u.filter.fo_readcache_max_filesize);
}

int lprocfs_filter_wr_readcache(libcfs_file_t *file, const char *buffer,
                                unsigned long count, void *data)
{
        struct obd_device *obd;
        __u64 val;
        int rc;
        int flag = 0;

        LIBCFS_PARAM_GET_DATA(obd, data, &flag);
        rc = lprocfs_write_u64_helper(buffer, count, &val, flag);
        if (rc)
                return rc;

        obd->u.filter.fo_readcache_max_filesize = val;
        return count;
}

int lprocfs_filter_rd_fmd_max_num(char *page, char **start, off_t off,
                                  int count, int *eof, void *data)
{
        struct obd_device *obd;

        LIBCFS_PARAM_GET_DATA(obd, data, NULL);

        return libcfs_param_snprintf(page, count, data, LP_D32, "%u\n",
                            obd->u.filter.fo_fmd_max_num);
}

int lprocfs_filter_wr_fmd_max_num(libcfs_file_t *file, const char *buffer,
                                  unsigned long count, void *data)
{
        struct obd_device *obd;
        int val;
        int rc;
        int flag = 0;

        LIBCFS_PARAM_GET_DATA(obd, data, &flag);
        rc = lprocfs_write_helper(buffer, count, &val, flag);
        if (rc)
                return rc;

        if (val > 65536 || val < 1)
                return -EINVAL;

        obd->u.filter.fo_fmd_max_num = val;
        return count;
}

int lprocfs_filter_rd_fmd_max_age(char *page, char **start, off_t off,
                                  int count, int *eof, void *data)
{
        struct obd_device *obd;
        int temp;

        LIBCFS_PARAM_GET_DATA(obd, data, NULL);
        temp = obd->u.filter.fo_fmd_max_age / CFS_HZ;

        return libcfs_param_snprintf(page, count, data, LP_D32, "%u\n", temp);
}

int lprocfs_filter_wr_fmd_max_age(libcfs_file_t *file, const char *buffer,
                                  unsigned long count, void *data)
{
        struct obd_device *obd;
        int val;
        int rc;
        int flag = 0;

        LIBCFS_PARAM_GET_DATA(obd, data, &flag);
        rc = lprocfs_write_helper(buffer, count, &val, flag);
        if (rc)
                return rc;

        if (val > 65536 || val < 1)
                return -EINVAL;

        obd->u.filter.fo_fmd_max_age = val * CFS_HZ;
        return count;
}

static int lprocfs_filter_rd_capa(char *page, char **start, off_t off,
                                  int count, int *eof, void *data)
{
        struct obd_device *obd;

        LIBCFS_PARAM_GET_DATA(obd, data, NULL);

        return libcfs_param_snprintf(page, count, data, LP_STR,
                      "capability on: %s\n",
                      obd->u.filter.fo_fl_oss_capa ? "oss" : "");
}

static int lprocfs_filter_wr_capa(libcfs_file_t *file, const char *buffer,
                                  unsigned long count, void *data)
{
        struct obd_device *obd;
        int val, rc, flag = 0;

        LIBCFS_PARAM_GET_DATA(obd, data, &flag);
        rc = lprocfs_write_helper(buffer, count, &val, flag);
        if (rc)
                return rc;

        if (val & ~0x1) {
                CERROR("invalid capability mode, only 0/1 are accepted.\n"
                       " 1: enable oss fid capability\n"
                       " 0: disable oss fid capability\n");
                return -EINVAL;
        }

        obd->u.filter.fo_fl_oss_capa = val;
        LCONSOLE_INFO("OSS %s %s fid capability.\n", obd->obd_name,
                      val ? "enabled" : "disabled");
        return count;
}

static int lprocfs_filter_rd_capa_count(char *page, char **start, off_t off,
                                        int count, int *eof, void *data)
{
        return libcfs_param_snprintf(page, count, data, LP_STR, "%d %d\n",
                      capa_count[CAPA_SITE_CLIENT],
                      capa_count[CAPA_SITE_SERVER]);
}

static int lprocfs_rd_sec_level(char *page, char **start, off_t off,
                                int count, int *eof, void *data)
{
        struct obd_device *obd;

        LIBCFS_PARAM_GET_DATA(obd, data, NULL);

        return libcfs_param_snprintf(page, count, data, LP_D32,
                                     "%d\n", obd->u.filter.fo_sec_level);
}

static int lprocfs_wr_sec_level(libcfs_file_t *file, const char *buffer,
                                unsigned long count, void *data)
{
        struct obd_device *obd;
        int val, rc, flag = 0;

        LIBCFS_PARAM_GET_DATA(obd, data, &flag);
        rc = lprocfs_write_helper(buffer, count, &val, flag);
        if (rc)
                return rc;

        if (val > LUSTRE_SEC_ALL || val < LUSTRE_SEC_NONE)
                return -EINVAL;

        if (val == LUSTRE_SEC_SPECIFY) {
                CWARN("security level %d will be supported in future.\n",
                      LUSTRE_SEC_SPECIFY);
                return -EINVAL;
        }

        obd->u.filter.fo_sec_level = val;
        return count;
}

static int lprocfs_filter_rd_cache(char *page, char **start, off_t off,
                                   int count, int *eof, void *data)
{
        struct obd_device *obd;

        LIBCFS_PARAM_GET_DATA(obd, data, NULL);
        LASSERT(obd != NULL);

        return libcfs_param_snprintf(page, count, data, LP_U32,
                                     "%u\n", obd->u.filter.fo_read_cache);
}

static int lprocfs_filter_wr_cache(libcfs_file_t *file, const char *buffer,
                     unsigned long count, void *data)
{
        struct obd_device *obd;
        int val, rc, flag = 0;

        LIBCFS_PARAM_GET_DATA(obd, data, &flag);
        LASSERT(obd != NULL);
        rc = lprocfs_write_helper(buffer, count, &val, flag);
        if (rc)
                return rc;

        cfs_spin_lock_bh(&obd->obd_processing_task_lock);
        obd->u.filter.fo_read_cache = val;
        cfs_spin_unlock_bh(&obd->obd_processing_task_lock);
        return count;
}

static int lprocfs_filter_rd_wcache(char *page, char **start, off_t off,
                                   int count, int *eof, void *data)
{
        struct obd_device *obd;
        int fo_writethrough_cache;

        LIBCFS_PARAM_GET_DATA(obd, data, NULL);
        LASSERT(obd != NULL);
        fo_writethrough_cache = obd->u.filter.fo_writethrough_cache;

        return libcfs_param_snprintf(page, count, data, LP_U32, "%u\n",
                                     fo_writethrough_cache);
}

static int lprocfs_filter_wr_wcache(libcfs_file_t *file, const char *buffer,
                                    unsigned long count, void *data)
{
        struct obd_device *obd;
        int val, rc, flag = 0;

        LIBCFS_PARAM_GET_DATA(obd, data, &flag);
        LASSERT(obd != NULL);

        rc = lprocfs_write_helper(buffer, count, &val, flag);

        if (rc)
                return rc;

        cfs_spin_lock_bh(&obd->obd_processing_task_lock);
        obd->u.filter.fo_writethrough_cache = val;
        cfs_spin_unlock_bh(&obd->obd_processing_task_lock);
        return count;
}

static int lprocfs_filter_rd_mds_sync(char *page, char **start, off_t off,
                                      int count, int *eof, void *data)
{
        struct obd_device *obd;
        int fo_mds_ost_sync;

        LIBCFS_PARAM_GET_DATA(obd, data, NULL);
        LASSERT(obd != NULL);
        fo_mds_ost_sync = obd->u.filter.fo_mds_ost_sync;

        return libcfs_param_snprintf(page, count, data, LP_U32, "%u\n",
                                     fo_mds_ost_sync);
}

int lprocfs_filter_rd_degraded(char *page, char **start, off_t off,
                               int count, int *eof, void *data)
{
        struct obd_device *obd;

        LIBCFS_PARAM_GET_DATA(obd, data, NULL);

        return libcfs_param_snprintf(page, count, data, LP_U32,
                                     "%u\n", obd->u.filter.fo_raid_degraded);
}

int lprocfs_filter_wr_degraded(libcfs_file_t *file, const char *buffer,
                               unsigned long count, void *data)
{
        struct obd_device *obd;
        int val, rc, flag = 0;

        LIBCFS_PARAM_GET_DATA(obd, data, &flag);
        rc = lprocfs_write_helper(buffer, count, &val, flag);
        if (rc)
                return rc;

        cfs_spin_lock(&obd->obd_osfs_lock);
        obd->u.filter.fo_raid_degraded = !!val;
        cfs_spin_unlock(&obd->obd_osfs_lock);
        return count;
}

int lprocfs_filter_rd_syncjournal(char *page, char **start, off_t off,
                                  int count, int *eof, void *data)
{
        struct obd_device *obd = data;
        int rc;

        rc = snprintf(page, count, "%u\n", obd->u.filter.fo_syncjournal);
        return rc;
}

int lprocfs_filter_wr_syncjournal(struct file *file, const char *buffer,
                                  unsigned long count, void *data)
{
        struct obd_device *obd = data;
        int val;
        int rc;

        rc = lprocfs_write_helper(buffer, count, &val);
        if (rc)
                return rc;

        if (val < 0)
                return -EINVAL;

        obd->u.filter.fo_syncjournal = !!val;
        filter_slc_set(&obd->u.filter);

        return count;
}

static char *sync_on_cancel_states[] = {"never",
                                        "blocking",
                                        "always" };

int lprocfs_filter_rd_sync_lock_cancel(char *page, char **start, off_t off,
                                       int count, int *eof, void *data)
{
        struct obd_device *obd = data;
        int rc;

        rc = snprintf(page, count, "%s\n",
                      sync_on_cancel_states[obd->u.filter.fo_sync_lock_cancel]);
        return rc;
}

int lprocfs_filter_wr_sync_lock_cancel(struct file *file, const char *buffer,
                                          unsigned long count, void *data)
{
        struct obd_device *obd = data;
        int val = -1;
        int i;

        for (i = 0 ; i < NUM_SYNC_ON_CANCEL_STATES; i++) {
                if (memcmp(buffer, sync_on_cancel_states[i],
                    strlen(sync_on_cancel_states[i])) == 0) {
                        val = i;
                        break;
                }
        }
        if (val == -1) {
                int rc;
                rc = lprocfs_write_helper(buffer, count, &val);
                if (rc)
                        return rc;
        }

        if (val < 0 || val > 2)
                return -EINVAL;

        obd->u.filter.fo_sync_lock_cancel = val;
        return count;
}

static struct lprocfs_vars lprocfs_filter_obd_vars[] = {
        { "uuid",         lprocfs_rd_uuid,          0, 0 },
        { "blocksize",    lprocfs_rd_blksize,       0, 0 },
        { "kbytestotal",  lprocfs_rd_kbytestotal,   0, 0 },
        { "kbytesfree",   lprocfs_rd_kbytesfree,    0, 0 },
        { "kbytesavail",  lprocfs_rd_kbytesavail,   0, 0 },
        { "filestotal",   lprocfs_rd_filestotal,    0, 0 },
        { "filesfree",    lprocfs_rd_filesfree,     0, 0 },
        { "filegroups",   lprocfs_filter_rd_groups, 0, 0 },
        { "fstype",       lprocfs_rd_fstype,        0, 0 },
        { "mntdev",       lprocfs_obd_rd_mntdev,    0, 0 },
        { "last_id",      lprocfs_filter_rd_last_id,0, 0 },
        { "tot_dirty",    lprocfs_filter_rd_tot_dirty,   0, 0 },
        { "tot_pending",  lprocfs_filter_rd_tot_pending, 0, 0 },
        { "tot_granted",  lprocfs_filter_rd_tot_granted, 0, 0 },
        { "hash_stats",   lprocfs_obd_rd_hash,      0, 0 },
        { "recovery_status",    lprocfs_obd_rd_recovery_status, 0, 0 },
        { "recovery_time_soft", lprocfs_obd_rd_recovery_time_soft,
                                lprocfs_obd_wr_recovery_time_soft, 0},
        { "recovery_time_hard", lprocfs_obd_rd_recovery_time_hard,
                                lprocfs_obd_wr_recovery_time_hard, 0},
        { "evict_client", 0, lprocfs_wr_evict_client, 0},
                                //&lprocfs_evict_client_fops},
        { "num_exports",  lprocfs_rd_num_exports,   0, 0 },
        { "readcache_max_filesize",
                          lprocfs_filter_rd_readcache,
                          lprocfs_filter_wr_readcache, 0 },
#ifdef HAVE_QUOTA_SUPPORT
        { "quota_type",     lprocfs_quota_rd_type,
                            lprocfs_quota_wr_type, 0},
#endif
        { "client_cache_count", lprocfs_filter_rd_fmd_max_num,
                          lprocfs_filter_wr_fmd_max_num, 0 },
        { "client_cache_seconds", lprocfs_filter_rd_fmd_max_age,
                          lprocfs_filter_wr_fmd_max_age, 0 },
        { "capa",         lprocfs_filter_rd_capa,
                          lprocfs_filter_wr_capa, 0 },
        { "capa_count",   lprocfs_filter_rd_capa_count, 0, 0 },
        { "sec_level",    lprocfs_rd_sec_level,
                          lprocfs_wr_sec_level,            0 },
        { "read_cache_enable", lprocfs_filter_rd_cache, lprocfs_filter_wr_cache, 0},
        { "writethrough_cache_enable", lprocfs_filter_rd_wcache,
                          lprocfs_filter_wr_wcache, 0},
        { "mds_sync",     lprocfs_filter_rd_mds_sync, 0, 0},
        { "degraded",     lprocfs_filter_rd_degraded,
                          lprocfs_filter_wr_degraded, 0 },
        { "sync_journal", lprocfs_filter_rd_syncjournal,
                          lprocfs_filter_wr_syncjournal, 0 },
        { "sync_on_lock_cancel", lprocfs_filter_rd_sync_lock_cancel,
                                 lprocfs_filter_wr_sync_lock_cancel, 0 },
        { 0 }
};

static struct lprocfs_vars lprocfs_filter_module_vars[] = {
        { "num_refs",     lprocfs_rd_numrefs,       0, 0 },
        { 0 }
};

void filter_tally(struct obd_export *exp, struct page **pages, int nr_pages,
                  unsigned long *blocks, int blocks_per_page, int wr)
{
        struct filter_obd *filter = &exp->exp_obd->u.filter;
        struct page *last_page = NULL;
        unsigned long *last_block = NULL;
        unsigned long discont_pages = 0;
        unsigned long discont_blocks = 0;
        int i;

        if (nr_pages == 0)
                return;

        lprocfs_oh_tally_log2(&filter->fo_filter_stats.hist[BRW_R_PAGES + wr],
                              nr_pages);
        if (exp->exp_nid_stats && exp->exp_nid_stats->nid_brw_stats)
                lprocfs_oh_tally_log2(&exp->exp_nid_stats->nid_brw_stats->
                                        hist[BRW_R_PAGES + wr], nr_pages);

        while (nr_pages-- > 0) {
                if (last_page && (*pages)->index != (last_page->index + 1))
                        discont_pages++;
                last_page = *pages;
                pages++;
                for (i = 0; i < blocks_per_page; i++) {
                        if (last_block && *blocks != (*last_block + 1))
                                discont_blocks++;
                        last_block = blocks++;
                }
        }

        lprocfs_oh_tally(&filter->fo_filter_stats.hist[BRW_R_DISCONT_PAGES +wr],
                         discont_pages);
        lprocfs_oh_tally(&filter->fo_filter_stats.hist[BRW_R_DISCONT_BLOCKS+wr],
                         discont_blocks);

        if (exp->exp_nid_stats && exp->exp_nid_stats->nid_brw_stats) {
                lprocfs_oh_tally_log2(&exp->exp_nid_stats->nid_brw_stats->
                                        hist[BRW_R_DISCONT_PAGES + wr],
                                      discont_pages);
                lprocfs_oh_tally_log2(&exp->exp_nid_stats->nid_brw_stats->
                                        hist[BRW_R_DISCONT_BLOCKS + wr],
                                      discont_blocks);
        }
}

#define pct(a,b) (b ? a * 100 / b : 0)

static void display_brw_stats(libcfs_seq_file_t *seq, char *name, char *units,
        struct obd_histogram *read, struct obd_histogram *write, int log2)
{
        unsigned long read_tot, write_tot, r, w, read_cum = 0, write_cum = 0;
        int i;

        LIBCFS_SEQ_PRINTF(seq, "\n%26s read      |     write\n", " ");
        LIBCFS_SEQ_PRINTF(seq, "%-22s %-5s %% cum %% |  %-5s %% cum %%\n",
                   name, units, units);

        read_tot = lprocfs_oh_sum(read);
        write_tot = lprocfs_oh_sum(write);
        for (i = 0; i < OBD_HIST_MAX; i++) {
                r = read->oh_buckets[i];
                w = write->oh_buckets[i];
                read_cum += r;
                write_cum += w;
                if (read_cum == 0 && write_cum == 0)
                        continue;

                if (!log2)
                        LIBCFS_SEQ_PRINTF(seq, "%u", i);
                else if (i < 10)
                        LIBCFS_SEQ_PRINTF(seq, "%u", 1<<i);
                else if (i < 20)
                        LIBCFS_SEQ_PRINTF(seq, "%uK", 1<<(i-10));
                else
                        LIBCFS_SEQ_PRINTF(seq, "%uM", 1<<(i-20));

                LIBCFS_SEQ_PRINTF(seq, ":\t\t%10lu %3lu %3lu   | %4lu %3lu %3lu\n",
                           r, pct(r, read_tot), pct(read_cum, read_tot),
                           w, pct(w, write_tot), pct(write_cum, write_tot));

                if (read_cum == read_tot && write_cum == write_tot)
                        break;
        }
}

static void brw_stats_show(libcfs_seq_file_t *seq, struct brw_stats *brw_stats)
{
        struct timeval now;

        /* this sampling races with updates */
        cfs_gettimeofday(&now);
        LIBCFS_SEQ_PRINTF(seq, "snapshot_time:         %lu.%lu (secs.usecs)\n",
                   now.tv_sec, now.tv_usec);

        display_brw_stats(seq, "pages per bulk r/w", "rpcs",
                          &brw_stats->hist[BRW_R_PAGES],
                          &brw_stats->hist[BRW_W_PAGES], 1);

        display_brw_stats(seq, "discontiguous pages", "rpcs",
                          &brw_stats->hist[BRW_R_DISCONT_PAGES],
                          &brw_stats->hist[BRW_W_DISCONT_PAGES], 0);

        display_brw_stats(seq, "discontiguous blocks", "rpcs",
                          &brw_stats->hist[BRW_R_DISCONT_BLOCKS],
                          &brw_stats->hist[BRW_W_DISCONT_BLOCKS], 0);

        display_brw_stats(seq, "disk fragmented I/Os", "ios",
                          &brw_stats->hist[BRW_R_DIO_FRAGS],
                          &brw_stats->hist[BRW_W_DIO_FRAGS], 0);

        display_brw_stats(seq, "disk I/Os in flight", "ios",
                          &brw_stats->hist[BRW_R_RPC_HIST],
                          &brw_stats->hist[BRW_W_RPC_HIST], 0);

        {
                char title[24];
                sprintf(title, "I/O time (1/%ds)", CFS_HZ);
                display_brw_stats(seq, title, "ios",
                                  &brw_stats->hist[BRW_R_IO_TIME],
                                  &brw_stats->hist[BRW_W_IO_TIME], 1);
        }

        display_brw_stats(seq, "disk I/O size", "ios",
                          &brw_stats->hist[BRW_R_DISK_IOSIZE],
                          &brw_stats->hist[BRW_W_DISK_IOSIZE], 1);
}

#undef pct

static int filter_brw_stats_seq_show(libcfs_seq_file_t *seq, void *v)
{
        struct obd_device *dev = LIBCFS_SEQ_PRIVATE(seq);
        struct filter_obd *filter = &dev->u.filter;

        brw_stats_show(seq, &filter->fo_filter_stats);

        return 0;
}

static ssize_t filter_brw_stats_seq_write(libcfs_file_t *file, const char *buf,
                                       size_t len, loff_t *off)
{
        libcfs_seq_file_t *seq = LIBCFS_FILE_PRIVATE(file);
        struct obd_device *dev = LIBCFS_SEQ_PRIVATE(seq);
        struct filter_obd *filter = &dev->u.filter;
        int i;

        for (i = 0; i < BRW_LAST; i++)
                lprocfs_oh_clear(&filter->fo_filter_stats.hist[i]);

        return len;
}

LPROC_SEQ_FOPS(filter_brw_stats);

int lproc_filter_attach_seqstat(struct obd_device *dev)
{
        return lprocfs_obd_seq_create(dev, "brw_stats", 0644,
                                      &filter_brw_stats_fops, dev);
}

void lprocfs_filter_init_vars(struct lprocfs_static_vars *lvars)
{
    lvars->module_vars  = lprocfs_filter_module_vars;
    lvars->obd_vars     = lprocfs_filter_obd_vars;
}

static int filter_per_nid_stats_seq_show(libcfs_seq_file_t *seq, void *v)
{
        nid_stat_t * stat = LIBCFS_SEQ_PRIVATE(seq);

        if (stat->nid_brw_stats)
                brw_stats_show(seq, stat->nid_brw_stats);

        return 0;
}

static ssize_t filter_per_nid_stats_seq_write(libcfs_file_t *file,
                                              const char *buf, size_t len,
                                              loff_t *off)
{
        libcfs_seq_file_t *seq  = LIBCFS_FILE_PRIVATE(file);
        nid_stat_t      *stat = LIBCFS_SEQ_PRIVATE(seq);
        int i;

        if (stat->nid_brw_stats)
                for (i = 0; i < BRW_LAST; i++)
                        lprocfs_oh_clear(&stat->nid_brw_stats->hist[i]);

        return len;
}

LPROC_SEQ_FOPS(filter_per_nid_stats);
