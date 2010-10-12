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
 * libcfs/include/libcfs/solaris/solaris-lock.h
 *
 */

#ifndef __LIBCFS_SOLARIS_SOLARIS_LOCK_H__
#define __LIBCFS_SOLARIS_SOLARIS_LOCK_H__

#ifndef __LIBCFS_LIBCFS_H__
#error Do not #include this file directly. #include <libcfs/libcfs.h> instead
#endif

typedef struct cfs_spinlock {
        kmutex_t        cfssl_lock;
} cfs_spinlock_t;

typedef struct cfs_mutex {
        kmutex_t        cfsmx_lock;
} cfs_mutex_t;

typedef struct cfs_rwlock {
        krwlock_t        cfsrw_lock;
} cfs_rwlock_t;

typedef struct cfs_semaphore {
        ksema_t cfssem_sem;
} cfs_semaphore_t;

/*
 * rw semaphore is blocking rw lock.
 */

typedef struct cfs_rw_semaphore {
        krwlock_t   cfsrwsem_lock;    
} cfs_rw_semaphore_t;

typedef struct cfs_completion {
        ksema_t cfscmpl_sem;
} cfs_completion_t;

typedef struct cfs_atomic {
        volatile int cfsatm_counter;
} cfs_atomic_t;

extern void cfs_spin_lock_init(cfs_spinlock_t *lock);
extern void cfs_spin_lock_done(cfs_spinlock_t *lock);
extern void cfs_mutex_init(cfs_mutex_t *lock);
extern void cfs_mutex_destroy(cfs_mutex_t *lock);
extern void cfs_rwlock_init(cfs_rwlock_t *lock);
extern void cfs_rwlock_fini(cfs_rwlock_t *lock);
extern void cfs_sema_init(cfs_semaphore_t *sem, int val);
extern void cfs_sema_fini(cfs_semaphore_t *sem);
extern void cfs_init_rwsem(struct cfs_rw_semaphore *semrw);
extern void cfs_fini_rwsem(struct cfs_rw_semaphore *semrw);
extern void cfs_init_completion(struct cfs_completion *cmpl);
extern void cfs_fini_completion(struct cfs_completion *cmpl);
extern void cfs_complete_and_exit(cfs_completion_t *cmpl, int ecode);

#define cfs_spin_lock(lock)        mutex_enter(&(lock)->cfssl_lock)
#define cfs_spin_unlock(lock)      mutex_exit(&(lock)->cfssl_lock)
#define cfs_spin_trylock(lock)     mutex_tryenter(&(lock)->cfssl_lock)
#define cfs_spin_is_locked(lock)   mutex_owned(&(lock)->cfssl_lock)

#define cfs_spin_lock_bh_init(lock)   cfs_spin_lock_init(lock)
#define cfs_spin_lock_bh_done(lock)   cfs_spin_lock_done(lock)

#define cfs_spin_lock_bh(lock)     mutex_enter(&(lock)->cfssl_lock)  
#define cfs_spin_unlock_bh(lock)   mutex_exit(&(lock)->cfssl_lock)

#define cfs_spin_lock_irq(l)                    mutex_enter(&(l)->cfssl_lock)
#define cfs_spin_unlock_irq(l)                  mutex_exit(&(l)->cfssl_lock)

#define cfs_spin_lock_irqsave(l, f)             mutex_enter(&(l)->cfssl_lock)
#define cfs_spin_unlock_irqrestore(l, f)        mutex_exit(&(l)->cfssl_lock)

#define cfs_mutex_lock(lock)        mutex_enter(&(lock)->cfsmx_lock)
#define cfs_mutex_unlock(lock)      mutex_exit(&(lock)->cfsmx_lock)
#define cfs_mutex_trylock(lock)     mutex_tryenter(&(lock)->cfsmx_lock)
#define cfs_mutex_is_locked(lock)   mutex_owned(&(lock)->cfsmx_lock)

#define cfs_read_lock(lock)             rw_enter(&(lock)->cfsrw_lock, RW_READER)
#define cfs_read_unlock(lock)           rw_exit(&(lock)->cfsrw_lock)
#define cfs_write_lock(lock)            rw_enter(&(lock)->cfsrw_lock, RW_WRITER)
#define cfs_write_unlock(lock)          rw_exit(&(lock)->cfsrw_lock)

#define cfs_read_lock_bh(lock)          rw_enter(&(lock)->cfsrw_lock, RW_READER)
#define cfs_read_unlock_bh(lock)        rw_exit(&(lock)->cfsrw_lock)
#define cfs_write_lock_bh(lock)         rw_enter(&(lock)->cfsrw_lock, RW_WRITER)
#define cfs_write_unlock_bh(lock)       rw_exit(&(lock)->cfsrw_lock)

#define cfs_read_lock_irq(l)                    rw_enter(&(l)->cfsrw_lock, \
                                                    RW_READER)
#define cfs_read_unlock_irq(l)                  rw_exit(&(l)->cfsrw_lock)
#define cfs_write_lock_irq(l)                   rw_enter(&(l)->cfsrw_lock, \
                                                    RW_WRITER)
#define cfs_write_unlock_irq(l)                 rw_exit(&(l)->cfsrw_lock)

#define cfs_read_lock_irqsave(l, f)             rw_enter(&(l)->cfsrw_lock, \
                                                    RW_READER)
#define cfs_read_unlock_irqrestore(l, f)        rw_exit(&(l)->cfsrw_lock)

#define cfs_write_lock_irqsave(l, f)            rw_enter(&(l)->cfsrw_lock, \
                                                    RW_WRITER)
#define cfs_write_unlock_irqrestore(l, f)       rw_exit(&(l)->cfsrw_lock)

#define cfs_down(sem)                   sema_p(&(sem)->cfssem_sem)
#define cfs_up(sem)                     sema_v(&(sem)->cfssem_sem)
#define cfs_down_interruptible(sem)     \
        (sema_p_sig(&(sem)->cfssem_sem) ? -EINTR : 0)
#define cfs_down_trylock(sem)           (!sema_tryp(&(sem)->cfssem_sem))
#define cfs_sem_is_locked(sem)          (sema_held(&(sem)->cfssem_sem))

#define cfs_init_mutex(s)               cfs_sema_init(s, 1)
#define cfs_init_mutex_locked(s)        cfs_sema_init(s, 0)
#define cfs_mutex_up(s)                 cfs_up(s)
#define cfs_mutex_down(s)               cfs_down(s)

#define cfs_down_read(s)                \
        rw_enter(&(s)->cfsrwsem_lock, RW_READER)
#define cfs_down_read_trylock(s)        \
        rw_tryenter(&(s)->cfsrwsem_lock, RW_READER)
#define cfs_down_write(s)               \
        rw_enter(&(s)->cfsrwsem_lock, RW_WRITER)
#define cfs_down_write_trylock(s)        \
        rw_tryenter(&(s)->cfsrwsem_lock, RW_WRITER)

#define cfs_up_read(s)  rw_exit(&(s)->cfsrwsem_lock)
#define cfs_up_write(s) rw_exit(&(s)->cfsrwsem_lock)

#define cfs_complete(c)                 sema_v(&(c)->cfscmpl_sem)
#define cfs_wait_for_completion(c)      sema_p(&(c)->cfscmpl_sem)


#define CFS_ATOMIC_INIT(v)              { (v) }
#define cfs_atomic_read(a)              ((a)->cfsatm_counter)
#define cfs_atomic_set(a, v)            ((a)->cfsatm_counter = (v))

#define cfs_atomic_add(v, a)            \
        atomic_add_32((uint32_t *)&(a)->cfsatm_counter, (v))
#define cfs_atomic_sub(v, a)            \
        atomic_add_32((uint32_t *)&(a)->cfsatm_counter, (-v))

#define cfs_atomic_inc(a)               cfs_atomic_add(1, (a))
#define cfs_atomic_dec(a)               cfs_atomic_sub(1, (a))

#define cfs_atomic_sub_and_test(v, a)   \
        (atomic_add_32_nv((uint32_t *)&(a)->cfsatm_counter, -v) == 0)

#define cfs_atomic_dec_and_test(a)      cfs_atomic_sub_and_test(1, a)

#define cfs_atomic_add_return(v, a)        \
        atomic_add_32_nv((uint32_t *)&(a)->cfsatm_counter, v)
#define cfs_atomic_sub_return(v, a)        \
        atomic_add_32_nv((uint32_t *)&(a)->cfsatm_counter, -v)

#define cfs_atomic_inc_return(a)        cfs_atomic_add_return(1, a)
#define cfs_atomic_dec_return(a)        cfs_atomic_sub_return(1, a)
#define cfs_atomic_inc_not_zero(v)      cfs_atomic_add_unless((v), 1, 0)

extern int cfs_atomic_dec_and_lock(cfs_atomic_t *, cfs_spinlock_t *);
extern int cfs_atomic_add_unless(cfs_atomic_t *, int, int);

/*
 * Lockdep "implementation"
 */

typedef struct cfs_lock_class_key {
        int foo;
} cfs_lock_class_key_t;

static inline void cfs_lockdep_set_class(void *lock, struct cfs_lock_class_key *key)
{
}

static inline void cfs_lockdep_off(void)
{
}

static inline void cfs_lockdep_on(void)
{
}

/*
 * "implementation" of nested privitives
 */

#define cfs_mutex_lock_nested(mutex, subclass) cfs_mutex_lock(mutex)
#define cfs_spin_lock_nested(lock, subclass)   cfs_spin_lock(lock)
#define cfs_down_read_nested(lock, subclass)   cfs_down_read(lock)
#define cfs_down_write_nested(lock, subclass)  cfs_down_write(lock)

/*
 * membars
 */

/* for RMO machines cfs_mb() should also call
 * membar_exit() + membar_consumer() */
#define cfs_mb()  membar_enter()
#define cfs_rmb() membar_consumer()

#endif /* __LIBCFS_SOLARIS_SOLARIS_LOCK_H__ */
