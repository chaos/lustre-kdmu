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
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/lvfs/lvfs_lib.c
 *
 * Lustre filesystem abstraction routines
 *
 * Author: Andreas Dilger <adilger@clusterfs.com>
 */
#ifdef __KERNEL__
#include <linux/module.h>
#else
#include <liblustre.h>
#endif
#include <lustre_lib.h>
#include <lprocfs_status.h>

unsigned int obd_fail_val = 0;
unsigned long obd_fail_loc = 0;
unsigned int obd_alloc_fail_rate = 0;

int obd_alloc_fail(const void *ptr, const char *name, const char *type,
                   size_t size, const char *file, int line)
{
        if (ptr == NULL ||
            (cfs_rand() & OBD_ALLOC_FAIL_MASK) < obd_alloc_fail_rate) {
                CERROR("%s%salloc of %s ("LPU64" bytes) failed at %s:%d\n",
                       ptr ? "force " :"", type, name, (__u64)size, file,
                       line);
                CERROR(LPU64" total bytes and "LPU64" total pages "
                       "("LPU64" bytes) allocated by Lustre, "
                       "%d total bytes by LNET\n",
                       obd_memory_sum(),
                       obd_pages_sum() << CFS_PAGE_SHIFT,
                       obd_pages_sum(),
                       cfs_atomic_read(&libcfs_kmemory));
                return 1;
        }
        return 0;
}
EXPORT_SYMBOL(obd_alloc_fail);

int __obd_fail_check_set(__u32 id, __u32 value, int set)
{
        static cfs_atomic_t obd_fail_count = CFS_ATOMIC_INIT(0);

        LASSERT(!(id & OBD_FAIL_ONCE));

        if ((obd_fail_loc & (OBD_FAILED | OBD_FAIL_ONCE)) ==
            (OBD_FAILED | OBD_FAIL_ONCE)) {
                cfs_atomic_set(&obd_fail_count, 0); /* paranoia */
                return 0;
        }

        /* Fail 1/obd_fail_val times */
        if (obd_fail_loc & OBD_FAIL_RAND) {
                if (obd_fail_val < 2 || cfs_rand() % obd_fail_val > 0)
                        return 0;
        }

        /* Skip the first obd_fail_val, then fail */
        if (obd_fail_loc & OBD_FAIL_SKIP) {
                if (cfs_atomic_inc_return(&obd_fail_count) <= obd_fail_val)
                        return 0;
        }

        /* Fail obd_fail_val times, overridden by FAIL_ONCE */
        if (obd_fail_loc & OBD_FAIL_SOME &&
            (!(obd_fail_loc & OBD_FAIL_ONCE) || obd_fail_val <= 1)) {
                int count = cfs_atomic_inc_return(&obd_fail_count);

                if (count >= obd_fail_val) {
                        cfs_set_bit(OBD_FAIL_ONCE_BIT, &obd_fail_loc);
                        cfs_atomic_set(&obd_fail_count, 0);
                        /* we are lost race to increase obd_fail_count */
                        if (count > obd_fail_val)
                                return 0;
                }
        }

        if ((set == OBD_FAIL_LOC_ORSET || set == OBD_FAIL_LOC_RESET) &&
            (value & OBD_FAIL_ONCE))
                cfs_set_bit(OBD_FAIL_ONCE_BIT, &obd_fail_loc);

        /* Lost race to set OBD_FAILED_BIT. */
        if (cfs_test_and_set_bit(OBD_FAILED_BIT, &obd_fail_loc)) {
                /* If OBD_FAIL_ONCE is valid, only one process can fail,
                 * otherwise multi-process can fail at the same time. */
                if (obd_fail_loc & OBD_FAIL_ONCE)
                        return 0;
        }

        switch (set) {
                case OBD_FAIL_LOC_NOSET:
                        break;
                case OBD_FAIL_LOC_ORSET:
                        obd_fail_loc |= value & ~(OBD_FAILED | OBD_FAIL_ONCE);
                        break;
                case OBD_FAIL_LOC_RESET:
                        obd_fail_loc = value;
                        break;
                default:
                        LASSERTF(0, "called with bad set %u\n", set);
                        break;
        }

        return 1;
}
EXPORT_SYMBOL(__obd_fail_check_set);

int __obd_fail_timeout_set(__u32 id, __u32 value, int ms, int set)
{
        int ret = 0;

        ret = __obd_fail_check_set(id, value, set);
        if (ret) {
                CERROR("obd_fail_timeout id %x sleeping for %dms\n",
                       id, ms);
                cfs_schedule_timeout_and_set_state(CFS_TASK_UNINT,
                                                   cfs_time_seconds(ms) / 1000);
                cfs_set_current_state(CFS_TASK_RUNNING);
                CERROR("obd_fail_timeout id %x awake\n", id);
        }
        return ret;
}
EXPORT_SYMBOL(__obd_fail_timeout_set);

#ifdef LPROCFS
void lprocfs_counter_add(struct lprocfs_stats *stats, int idx,
                                       long amount)
{
        struct lprocfs_counter *percpu_cntr;
        int smp_id;

        if (stats == NULL)
                return;

        /* With per-client stats, statistics are allocated only for
         * single CPU area, so the smp_id should be 0 always. */
        smp_id = lprocfs_stats_lock(stats, LPROCFS_GET_SMP_ID);

        percpu_cntr = &(stats->ls_percpu[smp_id]->lp_cntr[idx]);
        if (!(stats->ls_flags & LPROCFS_STATS_FLAG_NOPERCPU))
                cfs_atomic_inc(&percpu_cntr->lc_cntl.la_entry);
        percpu_cntr->lc_count++;

        if (percpu_cntr->lc_config & LPROCFS_CNTR_AVGMINMAX) {
                /* see comment in lprocfs_counter_sub */
                LASSERT(!cfs_in_interrupt());

                percpu_cntr->lc_sum += amount;
                if (percpu_cntr->lc_config & LPROCFS_CNTR_STDDEV)
                        percpu_cntr->lc_sumsquare += (__s64)amount * amount;
                if (amount < percpu_cntr->lc_min)
                        percpu_cntr->lc_min = amount;
                if (amount > percpu_cntr->lc_max)
                        percpu_cntr->lc_max = amount;
        }
        if (!(stats->ls_flags & LPROCFS_STATS_FLAG_NOPERCPU))
                cfs_atomic_inc(&percpu_cntr->lc_cntl.la_exit);
        lprocfs_stats_unlock(stats, LPROCFS_GET_SMP_ID);
}
EXPORT_SYMBOL(lprocfs_counter_add);

void lprocfs_counter_sub(struct lprocfs_stats *stats, int idx,
                                       long amount)
{
        struct lprocfs_counter *percpu_cntr;
        int smp_id;

        if (stats == NULL)
                return;

        /* With per-client stats, statistics are allocated only for
         * single CPU area, so the smp_id should be 0 always. */
        smp_id = lprocfs_stats_lock(stats, LPROCFS_GET_SMP_ID);

        percpu_cntr = &(stats->ls_percpu[smp_id]->lp_cntr[idx]);
        if (!(stats->ls_flags & LPROCFS_STATS_FLAG_NOPERCPU))
                cfs_atomic_inc(&percpu_cntr->lc_cntl.la_entry);
        if (percpu_cntr->lc_config & LPROCFS_CNTR_AVGMINMAX) {
                /*
                 * currently lprocfs_count_add() can only be called in thread
                 * context; sometimes we use RCU callbacks to free memory
                 * which calls lprocfs_counter_sub(), and RCU callbacks may
                 * execute in softirq context - right now that's the only case
                 * we're in softirq context here, use separate counter for that.
                 * bz20650.
                 */
                if (cfs_in_interrupt())
                        percpu_cntr->lc_sum_irq -= amount;
                else
                        percpu_cntr->lc_sum -= amount;
        }
        if (!(stats->ls_flags & LPROCFS_STATS_FLAG_NOPERCPU))
                cfs_atomic_inc(&percpu_cntr->lc_cntl.la_exit);
        lprocfs_stats_unlock(stats, LPROCFS_GET_SMP_ID);
}
EXPORT_SYMBOL(lprocfs_counter_sub);
#endif  /* LPROCFS */

EXPORT_SYMBOL(obd_fail_loc);
EXPORT_SYMBOL(obd_alloc_fail_rate);
EXPORT_SYMBOL(obd_fail_val);
