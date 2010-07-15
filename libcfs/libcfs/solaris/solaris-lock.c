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
 * libcfs/libcfs/solaris/solaris-lock.c
 *
 */

#define DEBUG_SUBSYSTEM S_LNET

#include <libcfs/libcfs.h>

/*
 * We map lustre spin locks to Solaris adaptive locks. The assumption is that
 * the caller must not call libcfs lock APIs if there's any chance it runs at
 * high interrupt level. Also the caller must not assume that libcfs spin lock
 * implementation disables preemption.
 */
void
cfs_spin_lock_init(cfs_spinlock_t *lock)
{
	mutex_init(&lock->cfssl_lock, NULL, MUTEX_DEFAULT, NULL);
}

void
cfs_spin_lock_done(cfs_spinlock_t *lock)
{
	mutex_destroy(&lock->cfssl_lock);
}

void
cfs_mutex_init(cfs_mutex_t *lock)
{
	mutex_init(&lock->cfsmx_lock, NULL, MUTEX_DEFAULT, NULL);
}

void
cfs_mutex_destroy(cfs_mutex_t *lock)
{
	mutex_destroy(&lock->cfsmx_lock);
}

void
cfs_rwlock_init(cfs_rwlock_t *lock)
{
	rw_init(&lock->cfsrw_lock, NULL, RW_DEFAULT, NULL);
}

void
cfs_rwlock_fini(cfs_rwlock_t *lock)
{
	rw_destroy(&lock->cfsrw_lock);
}

void
cfs_sema_init(cfs_semaphore_t *sem, int val)
{
	LASSERT(val >= 0);
	sema_init(&sem->cfssem_sem, val, NULL, SEMA_DEFAULT, 0);
}

void
cfs_sema_fini(cfs_semaphore_t *sem)
{
	sema_destroy(&sem->cfssem_sem);
}

void
cfs_init_rwsem(struct cfs_rw_semaphore *semrw)
{
        rw_init(&semrw->cfsrwsem_lock, NULL, RW_DEFAULT, NULL);  
}

void
cfs_fini_rwsem(struct cfs_rw_semaphore *semrw)
{
        rw_destroy(&semrw->cfsrwsem_lock);
}

void
cfs_init_completion(cfs_completion_t *cmpl)
{
        sema_init(&cmpl->cfscmpl_sem, 0, NULL, SEMA_DEFAULT, 0);
}

void
cfs_fini_completion(cfs_completion_t *cmpl)
{
        sema_destroy(&cmpl->cfscmpl_sem);
}

void
cfs_complete_and_exit(cfs_completion_t *cmpl, int ecode)
{
        sema_v(&cmpl->cfscmpl_sem);
        thread_exit();
}

int
cfs_atomic_dec_and_lock(cfs_atomic_t *atomic, cfs_spinlock_t *lock)
{
        int ctr = atomic->cfsatm_counter;

        while (ctr != 1) {
                if (cas32((uint32_t *)&atomic->cfsatm_counter, ctr,
                    ctr-1) == ctr) {
                        return (0);
                }
                ctr = atomic->cfsatm_counter;
        }

        mutex_enter(&lock->cfssl_lock);
        if (!atomic_add_32_nv(&atomic->cfsatm_counter, -1)) {
                return (1);
        }
        mutex_exit(&lock->cfssl_lock);
        return (0);
}

int
cfs_atomic_add_unless(cfs_atomic_t *atomic, int a, int u)
{
        int ctr = atomic->cfsatm_counter;

        while (ctr != u) {
                if (cas32((uint32_t *)&atomic->cfsatm_counter, ctr,
                    ctr+a) == ctr) {
                        return (1);
                }
                ctr = atomic->cfsatm_counter;
        }
        return (0);
}
