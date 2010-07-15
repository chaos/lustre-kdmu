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
 */

#ifndef __SOLARIS_OBD_H__
#define __SOLARIS_OBD_H__

#ifndef __OBD_H
#error Do not #include this file directly. #include <obd.h> instead
#endif

#include <obd_support.h>

typedef struct {
        cfs_spinlock_t      lock;

#ifdef CLIENT_OBD_LIST_LOCK_DEBUG
        unsigned long       time;
        cfs_task_t         *task;
        const char         *func;
        int                 line;
#endif

} client_obd_lock_t;

#ifdef CLIENT_OBD_LIST_LOCK_DEBUG
static inline void __client_obd_list_lock(client_obd_lock_t *lock,
                                          const char *func,
                                          int line)
{
        unsigned long cur = cfs_time_current();
        while (1) {
                if (cfs_spin_trylock(&lock->lock)) {
                        LASSERT(lock->task == NULL);
                        lock->task = cfs_current();
                        lock->func = func;
                        lock->line = line;
                        lock->time = cfs_time_current();
                        break;
                }

                if ((cfs_time_current() - cur > 5 * CFS_HZ) &&
                    (cfs_time_current() - lock->time > 5 * CFS_HZ)) {
                        LCONSOLE_WARN("LOCK UP! the lock %p was acquired"
                                      " by <%p:%s:%d> %lu time\n",
                                      lock, lock->task,
                                      lock->func, lock->line,
                                      (cfs_time_current() - lock->time));
                        LCONSOLE_WARN("====== for process holding the "
                                      "lock =====\n");
                        libcfs_debug_dumpstack(lock->task);
                        LCONSOLE_WARN("====== for current process =====\n");
                        libcfs_debug_dumpstack(NULL);
                        LCONSOLE_WARN("====== end =======\n");
                        cfs_pause(1000 * CFS_HZ);
                }
        }
}

#define client_obd_list_lock(lock) \
        __client_obd_list_lock(lock, __FUNCTION__, __LINE__)

static inline void client_obd_list_unlock(client_obd_lock_t *lock)
{
        LASSERT(lock->task != NULL);
        lock->task = NULL;
        lock->time = cfs_time_current();
        cfs_spin_unlock(&lock->lock);
}

#else /* ifdef CLIEBT_OBD_LIST_LOCK_DEBUG */
static inline void client_obd_list_lock(client_obd_lock_t *lock)
{
	cfs_spin_lock(&lock->lock);
}

static inline void client_obd_list_unlock(client_obd_lock_t *lock)
{
        cfs_spin_unlock(&lock->lock);
}

#endif /* ifdef CLIEBT_OBD_LIST_LOCK_DEBUG */

static inline void client_obd_list_lock_init(client_obd_lock_t *lock)
{
        cfs_spin_lock_init(&lock->lock);
}

static inline void client_obd_list_lock_done(client_obd_lock_t *lock)
{}


static inline int client_obd_list_is_locked(client_obd_lock_t *lock)
{
        return cfs_spin_is_locked(&lock->lock);
}

#endif /* __SOLARIS_OBD_H__ */
