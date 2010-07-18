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
 * lnet/selftest/workitem.c
 *
 * Author: Isaac Huang <isaac@clusterfs.com>
 */
#define DEBUG_SUBSYSTEM S_LNET

#include "selftest.h"


struct smoketest_workitem {
        cfs_list_t       wi_runq;         /* concurrent workitems */
        cfs_list_t       wi_serial_runq;  /* serialised workitems */
        cfs_waitq_t      wi_waitq;        /* where schedulers sleep */
        cfs_waitq_t      wi_serial_waitq; /* where serial scheduler sleep */
        cfs_spinlock_t   wi_lock;         /* serialize */
        int              wi_shuttingdown;
        int              wi_nthreads;
} swi_data;

static inline int
swi_sched_cansleep (cfs_list_t *q)
{
        int rc;

        cfs_spin_lock(&swi_data.wi_lock);

        rc = !swi_data.wi_shuttingdown && cfs_list_empty(q);

        cfs_spin_unlock(&swi_data.wi_lock);
        return rc;
}

/* XXX: 
 * 0. it only works when called from wi->wi_action.
 * 1. when it returns no one shall try to schedule the workitem.
 */
void
swi_kill_workitem (swi_workitem_t *wi)
{
        LASSERT (!cfs_in_interrupt()); /* because we use plain spinlock */
        LASSERT (!swi_data.wi_shuttingdown);

        cfs_spin_lock(&swi_data.wi_lock);

#ifdef __KERNEL__
        LASSERT (wi->wi_running);
#endif

        if (wi->wi_scheduled) { /* cancel pending schedules */
                LASSERT (!cfs_list_empty(&wi->wi_list));
                cfs_list_del_init(&wi->wi_list);
        }

        LASSERT (cfs_list_empty(&wi->wi_list));
        wi->wi_scheduled = 1; /* LBUG future schedule attempts */

        cfs_spin_unlock(&swi_data.wi_lock);
        return;
}

void
swi_schedule_workitem (swi_workitem_t *wi)
{
        LASSERT (!cfs_in_interrupt()); /* because we use plain spinlock */
        LASSERT (!swi_data.wi_shuttingdown);

        cfs_spin_lock(&swi_data.wi_lock);

        if (!wi->wi_scheduled) {
                LASSERT (cfs_list_empty(&wi->wi_list));

                wi->wi_scheduled = 1;
                cfs_list_add_tail(&wi->wi_list, &swi_data.wi_runq);
                cfs_waitq_signal(&swi_data.wi_waitq);
        }

        LASSERT (!cfs_list_empty(&wi->wi_list));
        cfs_spin_unlock(&swi_data.wi_lock);
        return;
}

/*
 * Workitem scheduled by this function is strictly serialised not only with
 * itself, but also with others scheduled this way.
 *
 * Now there's only one static serialised queue, but in the future more might
 * be added, and even dynamic creation of serialised queues might be supported.
 */
void
swi_schedule_serial_workitem (swi_workitem_t *wi)
{
        LASSERT (!cfs_in_interrupt()); /* because we use plain spinlock */
        LASSERT (!swi_data.wi_shuttingdown);

        cfs_spin_lock(&swi_data.wi_lock);

        if (!wi->wi_scheduled) {
                LASSERT (cfs_list_empty(&wi->wi_list));

                wi->wi_scheduled = 1;
                cfs_list_add_tail(&wi->wi_list, &swi_data.wi_serial_runq);
                cfs_waitq_signal(&swi_data.wi_serial_waitq);
        }

        LASSERT (!cfs_list_empty(&wi->wi_list));
        cfs_spin_unlock(&swi_data.wi_lock);
        return;
}

#ifdef __KERNEL__

int
swi_scheduler_main (void *arg)
{
        int  id = (int)(long_ptr_t) arg;
        char name[16];

        snprintf(name, sizeof(name), "swi_sd%03d", id);
        cfs_daemonize(name);
        cfs_block_allsigs();

        cfs_spin_lock(&swi_data.wi_lock);

        while (!swi_data.wi_shuttingdown) {
                int             nloops = 0;
                int             rc;
                swi_workitem_t *wi;

                while (!cfs_list_empty(&swi_data.wi_runq) &&
                       nloops < SWI_RESCHED) {
                        wi = cfs_list_entry(swi_data.wi_runq.next,
                                            swi_workitem_t, wi_list);
                        cfs_list_del_init(&wi->wi_list);

                        LASSERT (wi->wi_scheduled);

                        nloops++;
                        if (wi->wi_running) {
                                cfs_list_add_tail(&wi->wi_list,
                                                  &swi_data.wi_runq);
                                continue;
                        }

                        wi->wi_running   = 1;
                        wi->wi_scheduled = 0;
                        cfs_spin_unlock(&swi_data.wi_lock);

                        rc = (*wi->wi_action) (wi);

                        cfs_spin_lock(&swi_data.wi_lock);
                        if (rc == 0) /* wi still active */
                                wi->wi_running = 0;
                }

                cfs_spin_unlock(&swi_data.wi_lock);

                if (nloops < SWI_RESCHED)
                        cfs_wait_event_interruptible_exclusive(
                                swi_data.wi_waitq,
                                !swi_sched_cansleep(&swi_data.wi_runq), rc);
                else
                        cfs_cond_resched();

                cfs_spin_lock(&swi_data.wi_lock);
        }

        swi_data.wi_nthreads--;
        cfs_spin_unlock(&swi_data.wi_lock);
        return 0;
}

int
swi_serial_scheduler_main (void *arg)
{
        UNUSED (arg);

        cfs_daemonize("swi_serial_sd");
        cfs_block_allsigs();

        cfs_spin_lock(&swi_data.wi_lock);

        while (!swi_data.wi_shuttingdown) {
                int             nloops = 0;
                int             rc;
                swi_workitem_t *wi;

                while (!cfs_list_empty(&swi_data.wi_serial_runq) &&
                       nloops < SWI_RESCHED) {
                        wi = cfs_list_entry(swi_data.wi_serial_runq.next,
                                            swi_workitem_t, wi_list);
                        cfs_list_del_init(&wi->wi_list);

                        LASSERTF (!wi->wi_running && wi->wi_scheduled,
                                  "wi %p running %d scheduled %d\n",
                                  wi, wi->wi_running, wi->wi_scheduled);

                        nloops++;
                        wi->wi_running   = 1;
                        wi->wi_scheduled = 0;
                        cfs_spin_unlock(&swi_data.wi_lock);

                        rc = (*wi->wi_action) (wi);

                        cfs_spin_lock(&swi_data.wi_lock);
                        if (rc == 0) /* wi still active */
                                wi->wi_running = 0;
                }

                cfs_spin_unlock(&swi_data.wi_lock);

                if (nloops < SWI_RESCHED)
                        cfs_wait_event_interruptible_exclusive(
                                swi_data.wi_serial_waitq,
                                !swi_sched_cansleep(&swi_data.wi_serial_runq),
                                rc);
                else
                        cfs_cond_resched();

                cfs_spin_lock(&swi_data.wi_lock);
        }

        swi_data.wi_nthreads--;
        cfs_spin_unlock(&swi_data.wi_lock);
        return 0;
}

int
swi_start_thread (int (*func) (void*), void *arg)
{
        long pid;

        LASSERT (!swi_data.wi_shuttingdown);

        pid = cfs_kernel_thread(func, arg, 0);
        if (pid < 0)
                return (int)pid;

        cfs_spin_lock(&swi_data.wi_lock);
        swi_data.wi_nthreads++;
        cfs_spin_unlock(&swi_data.wi_lock);
        return 0;
}

#else /* __KERNEL__ */

int
swi_check_events (void)
{
        int               n = 0;
        swi_workitem_t   *wi;
        cfs_list_t       *q;

        cfs_spin_lock(&swi_data.wi_lock);

        for (;;) {
                if (!cfs_list_empty(&swi_data.wi_serial_runq))
                        q = &swi_data.wi_serial_runq;
                else if (!cfs_list_empty(&swi_data.wi_runq))
                        q = &swi_data.wi_runq;
                else
                        break;

                wi = cfs_list_entry(q->next, swi_workitem_t, wi_list);
                cfs_list_del_init(&wi->wi_list);

                LASSERT (wi->wi_scheduled);
                wi->wi_scheduled = 0;
                cfs_spin_unlock(&swi_data.wi_lock);

                n++;
                (*wi->wi_action) (wi);

                cfs_spin_lock(&swi_data.wi_lock);
        }

        cfs_spin_unlock(&swi_data.wi_lock);
        return n;
}

#endif

int
swi_startup (void)
{
        int i;
        int rc;

        swi_data.wi_nthreads = 0;
        swi_data.wi_shuttingdown = 0;
        cfs_spin_lock_init(&swi_data.wi_lock);
        cfs_waitq_init(&swi_data.wi_waitq);
        cfs_waitq_init(&swi_data.wi_serial_waitq);
        CFS_INIT_LIST_HEAD(&swi_data.wi_runq);
        CFS_INIT_LIST_HEAD(&swi_data.wi_serial_runq);

#ifdef __KERNEL__
        rc = swi_start_thread(swi_serial_scheduler_main, NULL);
        if (rc != 0) {
                LASSERT (swi_data.wi_nthreads == 0);
                CERROR ("Can't spawn serial workitem scheduler: %d\n", rc);
                return rc;
        }

        for (i = 0; i < cfs_num_online_cpus(); i++) {
                rc = swi_start_thread(swi_scheduler_main,
                                      (void *) (long_ptr_t) i);
                if (rc != 0) {
                        CERROR ("Can't spawn workitem scheduler: %d\n", rc);
                        swi_shutdown();
                        return rc;
                }
        }
#else
        UNUSED(i);
        UNUSED(rc);
#endif

        return 0;
}

void
swi_shutdown (void)
{
        cfs_spin_lock(&swi_data.wi_lock);

        LASSERT (cfs_list_empty(&swi_data.wi_runq));
        LASSERT (cfs_list_empty(&swi_data.wi_serial_runq));

        swi_data.wi_shuttingdown = 1;

#ifdef __KERNEL__
        cfs_waitq_broadcast(&swi_data.wi_waitq);
        cfs_waitq_broadcast(&swi_data.wi_serial_waitq);
        lst_wait_until(swi_data.wi_nthreads == 0, swi_data.wi_lock,
                       "waiting for %d threads to terminate\n",
                       swi_data.wi_nthreads);
#endif

        cfs_spin_unlock(&swi_data.wi_lock);
        return;
}
