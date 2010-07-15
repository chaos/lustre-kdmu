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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * libcfs/libcfs/solaris/solaris-prim.c
 *
 */

#define DEBUG_SUBSYSTEM S_LNET

/* more #include-s to be here? */
#include <libcfs/libcfs.h>
#include <sys/cpuvar.h>

void cfs_enter_debugger()
{
        panic("Entering debugger ...");
}

/* Nothing to daemonize on Solaris. At least on server side. */
void cfs_daemonize(char *str)
{
}
int cfs_daemonize_ctxt(char *str)
{
        return 0;
}

int cfs_need_resched()
{
        return 0;
}

void
cfs_cond_resched(void)
{
        delay(1);
}

cfs_sigset_t
cfs_block_allsigs(void)
{
        cfs_sigset_t        old;
        cfs_sigset_t        new;

        if (ttolwp(curthread) == NULL) {
                sigfillset(&old);
                return (old);
        }

        sigfillset(&new);
        sigreplace(&new, &old);

        return (old);
}

cfs_sigset_t
cfs_block_sigs(cfs_sigset_t bits)
{
        cfs_sigset_t        old;

        if (ttolwp(curthread) == NULL) {
                sigfillset(&old);
                return (old);
        }

        sigreplace(&bits, &old);
        
        return (old);
}

void
cfs_restore_sigs(cfs_sigset_t old)
{
        if (ttolwp(curthread) == NULL) {
                return;
        }

        sigreplace(&old, NULL);
        
        return;
}

int
cfs_signal_pending(void)
{
        kthread_t *t = curthread;

        if (ttolwp(t) == NULL) {
                return (0);
        }

        return (ISSIG(t, JUSTLOOKING));

}

/*
 * Filled with the inverse of LUSTRE_FATAL_SIGS on startup
 */
k_sigset_t cfs_invlfatalsigs;

inline void
cfs_int_to_invsigset(int sigs, cfs_sigset_t *invsigset)
{
        int i;
        
        sigfillset(invsigset);
        for (i = 0; i <= 31; i++) {
                if (sigs & (1 << i)) {
                        sigdelset(invsigset, i + 1);
                }
        }
}

void
cfs_clear_sigpending(void)
{
        return;
}

extern kmutex_t cfsd_lock;

int
libcfs_arch_init(void)
{
        libcfs_panic_on_lbug = 1;
        cfs_int_to_invsigset(LUSTRE_FATAL_SIGS, &cfs_invlfatalsigs);
        mutex_init(&cfsd_lock, NULL, MUTEX_DEFAULT, NULL);

        return 0;
}

void
libcfs_arch_cleanup(void)
{
        mutex_destroy(&cfsd_lock);
}

void
cfs_waitq_init(struct cfs_waitq *waitq)
{
        CFS_INIT_LIST_HEAD(&waitq->cfswq_list);
        mutex_init(&waitq->cfswq_lock, NULL, MUTEX_DEFAULT, NULL);
}

void
cfs_waitlink_init(struct cfs_waitlink *link)
{
        mutex_init(&link->cfswl_lock, NULL, MUTEX_DEFAULT, NULL);
        cv_init(&link->cfswl_cv, NULL, CV_DEFAULT, NULL);
        CFS_INIT_LIST_HEAD(&link->cfswl_list);
}

void
cfs_waitlink_fini(struct cfs_waitlink *link)
{
        mutex_destroy(&link->cfswl_lock);
        cv_destroy(&link->cfswl_cv);
        CFS_INIT_LIST_HEAD(&link->cfswl_list);
}

void
cfs_waitq_add(struct cfs_waitq *waitq, struct cfs_waitlink *link)
{
        link->cfswl_evhit = 0;
        link->cfswl_flag = 0;

        mutex_enter(&waitq->cfswq_lock);
        cfs_list_add(&link->cfswl_list, &waitq->cfswq_list);
        mutex_exit(&waitq->cfswq_lock);
}

void
cfs_waitq_add_exclusive(struct cfs_waitq *waitq, struct cfs_waitlink *link)
{
        link->cfswl_evhit = 0;
        link->cfswl_flag = CFS_WAITQ_EXCLUSIVE;

        mutex_enter(&waitq->cfswq_lock);
        cfs_list_add_tail(&link->cfswl_list, &waitq->cfswq_list);
        mutex_exit(&waitq->cfswq_lock);
}

void
cfs_waitq_del(struct cfs_waitq *waitq, struct cfs_waitlink *link)
{
        LASSERT(!cfs_list_empty(&link->cfswl_list));

	mutex_enter(&waitq->cfswq_lock);
        cfs_list_del(&link->cfswl_list);
        mutex_exit(&waitq->cfswq_lock);
}

int
cfs_waitq_active(struct cfs_waitq *waitq)
{
        return (!cfs_list_empty(&waitq->cfswq_list));
}

void
cfs_waitq_wakeup(struct cfs_waitq *waitq, int nr)
{
        struct cfs_list_head *llink;
	/*
	 * XXX nikita: do NOT call libcfs_debug_msg() (CDEBUG/ENTRY/EXIT)
	 * from here: this will lead to infinite recursion.
	 */

        mutex_enter(&waitq->cfswq_lock);
        cfs_list_for_each(llink, &waitq->cfswq_list) {
                cfs_waitlink_t *wl;
                wl = cfs_list_entry(llink, cfs_waitlink_t, cfswl_list);
                mutex_enter(&wl->cfswl_lock);
                cv_signal(&wl->cfswl_cv);
                wl->cfswl_evhit = 1;
                mutex_exit(&wl->cfswl_lock);
                if ((wl->cfswl_flag & CFS_WAITQ_EXCLUSIVE) && --nr == 0) {
                        break;
                }
        }
        mutex_exit(&waitq->cfswq_lock);
}

void
cfs_waitq_wait(struct cfs_waitlink *link, cfs_task_state_t state)
{
        mutex_enter(&link->cfswl_lock);
        if (link->cfswl_evhit == 0) {
                if (state == CFS_TASK_INTERRUPTIBLE) {
                        cv_wait_sig(&link->cfswl_cv, &link->cfswl_lock);
                } else {
                        cv_wait(&link->cfswl_cv, &link->cfswl_lock);
                }
        }
        link->cfswl_evhit = 0;
        mutex_exit(&link->cfswl_lock);
}

static inline int64_t
cfs_waitq_timedwait_internal(struct cfs_waitlink *link,
                             cfs_task_state_t state,
                             int64_t timeout)
{
        clock_t expire = lbolt + timeout;
        clock_t ret;

        /* the following check is only needed because Solaris's
         * cv_timedwait currently does:
         *         if (tim <= lbolt) return (-1); */
        if (expire <= 0)
                expire = CFS_MAX_SCHEDULE_TIMEOUT;
        
        mutex_enter(&link->cfswl_lock);
        if (link->cfswl_evhit == 0) {
                if (state == CFS_TASK_INTERRUPTIBLE) {
                        cv_timedwait_sig(&link->cfswl_cv, &link->cfswl_lock,
                            expire);
                } else {
                        cv_timedwait(&link->cfswl_cv, &link->cfswl_lock,
                            expire);
                }
        }
        link->cfswl_evhit = 0;
        mutex_exit(&link->cfswl_lock);

        ret = expire - lbolt;

        return (ret < 0 ? 0 : ret);
}

int64_t
cfs_waitq_timedwait(struct cfs_waitlink *link,
                    cfs_task_state_t state,
                    int64_t timeout)
{
        if (timeout == CFS_MAX_SCHEDULE_TIMEOUT) {
                cfs_waitq_wait(link, state);
                return (0); /* caller shouldn't care for rc */
        }

        return (cfs_waitq_timedwait_internal(link, state, timeout));
}

void
cfs_waitq_signal(cfs_waitq_t *waitq)
{
        cfs_waitq_wakeup(waitq, 1);
}

void
cfs_waitq_signal_nr(cfs_waitq_t *waitq, int nr)
{
        cfs_waitq_wakeup(waitq, nr);
}

void
cfs_waitq_broadcast(cfs_waitq_t *waitq)
{
        cfs_waitq_wakeup(waitq, 0);
}


void
cfs_schedule_timeout_and_set_state(cfs_task_state_t state, int64_t timeout)
{
        if (state == CFS_TASK_INTERRUPTIBLE) {
                delay_sig(timeout);
        } else {
                delay(timeout);
        }
}

/* This should be OK for server-side code. Client-side
 * needs more thinking */
void
cfs_schedule_timeout(int64_t timeout)
{
        cfs_schedule_timeout_and_set_state(CFS_TASK_UNINT, timeout);
}

void
cfs_schedule(void)
{
        cfs_schedule_timeout_and_set_state(CFS_TASK_UNINT, CFS_TICK);
}

void
cfs_pause(cfs_duration_t ticks)
{
        delay(ticks);
}

int
cfs_kernel_thread(int (*fn)(void *), void *arg, unsigned long flags)
{
        if (thread_create(NULL, 0, (void (*)())fn, arg, 0, &p0,
            TS_RUN, minclsyspri) == NULL) {
                return (-1);
        }
        return (0);
}


cfs_task_t *
cfs_kthread_run(int (*fn)(void *), void *arg, const char *namefmt, ...)
{
        return (thread_create(NULL, 0, (void (*)())fn, arg, 0, &p0, TS_RUN,
                              minclsyspri));
}

/*
 * group_info routines
 */

struct cfs_group_info *
cfs_groups_alloc(int gidsetsize)
{
        cfs_group_info_t *ginfo;

        ginfo = (cfs_group_info_t *)cfs_alloc(sizeof(struct cfs_group_info),
                                              CFS_ALLOC_ZERO);
        cfs_atomic_set(&ginfo->usage, 1);

        return (ginfo);
}

void
cfs_groups_free(struct cfs_group_info *group_info)
{
        cfs_free(group_info);
}

static
void cfs_timer_handler(void *arg)
{
        cfs_timer_t *t = (cfs_timer_t *)arg;

        mutex_enter(&t->cfstim_lock);
        LASSERT(t->cfstim_count);
        if (--t->cfstim_count == 0) {
                t->cfstim_cid = 0;
        }
        mutex_exit(&t->cfstim_lock);

        t->cfstim_func(t->cfstim_arg);
}

void
cfs_timer_init(cfs_timer_t *t, cfs_timer_func_t *func, void *arg)
{
        mutex_init(&t->cfstim_lock, NULL, MUTEX_DEFAULT, NULL);
        t->cfstim_func = func;
        t->cfstim_arg = (unsigned long)arg;
        t->cfstim_cid = 0;
        t->cfstim_count = 0;
}

void
cfs_timer_done(cfs_timer_t *t)
{
        mutex_destroy(&t->cfstim_lock);
}

void
cfs_timer_arm(cfs_timer_t *t, cfs_time_t deadline)
{
        callout_id_t cid;
        clock_t delta;

        mutex_enter(&t->cfstim_lock);
        if ((cid = t->cfstim_cid)) {
                if (untimeout_default(cid, 1) >= 0) {
                        LASSERT(t->cfstim_count);
                        t->cfstim_count--;
                }
        }
        delta = deadline - lbolt;
        t->cfstim_deadline = deadline;
        t->cfstim_count++;
        LASSERT(t->cfstim_count);
        t->cfstim_cid = timeout_default(cfs_timer_handler, t, delta);
        mutex_exit(&t->cfstim_lock);
}

void
cfs_timer_disarm(cfs_timer_t *t)
{
        callout_id_t cid;

        mutex_enter(&t->cfstim_lock);
        if ((cid = t->cfstim_cid)) {
                if (untimeout_default(cid, 1) >= 0) {
                        LASSERT(t->cfstim_count);
                        t->cfstim_count--;
                }
                t->cfstim_cid = 0;
        }
        mutex_exit(&t->cfstim_lock);
}

int
cfs_timer_is_armed(cfs_timer_t *t)
{
        return (t->cfstim_cid != 0);
}

cfs_time_t
cfs_timer_deadline(cfs_timer_t *t)
{
        return (t->cfstim_deadline);
}
