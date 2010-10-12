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
 * libcfs/include/libcfs/solaris/solaris-prim.h
 *
 */

#ifndef __LIBCFS_SOLARIS_SOLARIS_PRIM_H__
#define __LIBCFS_SOLARIS_SOLARIS_PRIM_H__

#ifndef __LIBCFS_LIBCFS_H__
#error Do not #include this file directly. #include <libcfs/libcfs.h> instead
#endif

#ifndef __KERNEL__
#error This include is only for kernel use.
#endif

#include <libcfs/list.h>

#ifndef abs
static inline int abs(int x)
{
        return (x < 0) ? -x : x;
}
#endif

#ifndef min
#define min(x,y) ((x)<(y) ? (x) : (y))
#endif

#ifndef max
#define max(x,y) ((x)>(y) ? (x) : (y))
#endif

#define __init
#define __exit

/*
 * errno to 'void *' conversion and vice versa
 */
#define ERR_PTR(error) ((void *)(long_ptr_t)(error))
#define PTR_ERR(ptr)   ((long)(long_ptr_t) (ptr))
#define IS_ERR(ptr)    ((long)(((ulong_ptr_t) (ptr)) > (ulong_ptr_t)(-1000L)))

/*
 * Wait Queue
 */
#define CFS_TASK_INTERRUPTIBLE          0x1
#define CFS_TASK_UNINT                  0x2

typedef struct cfs_waitq {
        cfs_list_t              cfswq_list;
        kmutex_t                cfswq_lock;
} cfs_waitq_t;

typedef struct cfs_waitlink {
        cfs_list_t             cfswl_list;
        kmutex_t               cfswl_lock;
        kcondvar_t             cfswl_cv;
        uchar_t                cfswl_evhit;
        uchar_t                cfswl_flag;
} cfs_waitlink_t;

#define CFS_WAITQ_EXCLUSIVE     (0x1)

typedef long                            cfs_task_state_t;

#define cfs_set_current_state(s)        do {} while (0)

#define cfs_waitq_forward(l, w)         do {} while(0)

extern void     cfs_waitq_init(struct cfs_waitq *);
extern void     cfs_waitlink_fini(struct cfs_waitlink *);
extern void     cfs_waitlink_init(struct cfs_waitlink *);
extern void     cfs_waitq_add(struct cfs_waitq *, struct cfs_waitlink *);
extern void     cfs_waitq_add_exclusive(struct cfs_waitq *,
                                        struct cfs_waitlink *);
extern void     cfs_waitq_del(struct cfs_waitq *, struct cfs_waitlink *);
extern int      cfs_waitq_active(struct cfs_waitq *);
extern void     cfs_waitq_wakeup(struct cfs_waitq *, int);
extern void     cfs_waitq_wait(struct cfs_waitlink *, cfs_task_state_t);

extern void     cfs_pause(cfs_duration_t);


/* Kernel thread */

typedef kthread_t            cfs_task_t;

typedef int (*cfs_thread_t)(void *);

extern int cfs_kernel_thread(int (*fn)(void *), void *arg,
                             unsigned long flags);

extern cfs_task_t *cfs_kthread_run(int (*fn)(void *), void *arg,
                                   const char *namefmt, ...);

#define cfs_in_interrupt() servicing_interrupt()

#define cfs_might_sleep() do {} while(0)
#define cfs_smp_processor_id() (CPU->cpu_id)

#define cfs_lock_kernel()       do {} while(0)
#define cfs_unlock_kernel()     do {} while(0)

/*
 * thread creation flags from Linux - values of those flags
 * don't matter, but definitions are needed as they are used from
 * common code.
 */
#define CLONE_VM        0x00000100      /* set if VM shared between processes */
#define CLONE_FS        0x00000200      /* set if fs info shared between processes */
#define CLONE_FILES     0x00000400      /* set if open files shared between processes */
#define CLONE_SIGHAND   0x00000800      /* set if signal handlers and blocked signals shared */
#define CLONE_PID       0x00001000      /* set if pid shared */
#define CLONE_PTRACE    0x00002000      /* set if we want to let tracing continue on the child too */
#define CLONE_VFORK     0x00004000      /* set if the parent wants the child to wake it up on mm_release */
#define CLONE_PARENT    0x00008000      /* set if we want to have the same parent as the cloner */
#define CLONE_THREAD    0x00010000      /* Same thread group? */
#define CLONE_NEWNS     0x00020000      /* New namespace group? */

#define CLONE_SIGNAL    (CLONE_SIGHAND | CLONE_THREAD)

/*
 * group_info: from linux linux/sched.h
 */

#define CFS_NGROUPS_SMALL       32

typedef struct cfs_group_info {
        int          ngroups;
        cfs_atomic_t usage;
        gid_t        small_block[CFS_NGROUPS_SMALL];
        int          nblocks;
        gid_t       *blocks[0];
} cfs_group_info_t;

struct cfs_group_info *cfs_groups_alloc(int gidsetsize);
void cfs_groups_free(struct cfs_group_info *group_info);

#define cfs_get_group_info(group_info) do { \
        cfs_atomic_inc(&(group_info)->usage); \
} while (0)

#define cfs_put_group_info(group_info) do { \
        if (cfs_atomic_dec_and_test(&(group_info)->usage)) \
                cfs_groups_free(group_info); \
} while (0)

static inline int cfs_cleanup_group_info(void)
{
        return 0;
}

static inline int cfs_set_current_groups(cfs_group_info_t *ginfo)
{
        return 0;
}

/*
 * Task struct
 */
#define cfs_current()               ((cfs_task_t *)curthread)

#define CFS_DECL_JOURNAL_DATA   
#define CFS_PUSH_JOURNAL            do {} while(0)
#define CFS_POP_JOURNAL             do {} while(0)

/*
 * some extension to stat.h
 */

#define S_IRWXUGO	(S_IRWXU|S_IRWXG|S_IRWXO)
#define S_IALLUGO	(S_ISUID|S_ISGID|S_ISVTX|S_IRWXUGO)
#define S_IRUGO		(S_IRUSR|S_IRGRP|S_IROTH)
#define S_IWUGO		(S_IWUSR|S_IWGRP|S_IWOTH)
#define S_IXUGO		(S_IXUSR|S_IXGRP|S_IXOTH)

typedef struct {
        int foo;
} cfs_psdev_t;

typedef struct {
        const char *mdesc_name;
        const char *mdesc_version;
        int       (*mdesc_init)(void);
        void      (*mdesc_fini)(void);
} cfs_lumodule_desc_t;

/* this define is to be monolithic module */
#define cfs_module(name, version, init_func, fini_func)                       \
cfs_lumodule_desc_t name##_module_desc = {                                    \
        #name,      /* mdesc_name */                                          \
        version,    /* mdesc_version */                                       \
        &init_func, /* mdesc_init */                                          \
        &fini_func, /* mdesc_fini */                                          \
}

extern int cfs_psdev_register(cfs_psdev_t *);
extern int cfs_psdev_deregister(cfs_psdev_t *);

/*
 * Signal
 */

#define LUSTRE_FATAL_SIGS (sigmask(SIGKILL) | sigmask(SIGINT) |               \
                           sigmask(SIGTERM) | sigmask(SIGQUIT) |              \
                           sigmask(SIGALRM))

typedef k_sigset_t                        cfs_sigset_t;

#define SIGNAL_MASK_ASSERT()  do {} while(0)

/*
 * Timer
 */

typedef struct cfs_timer {
        kmutex_t                cfstim_lock;
        volatile cfs_time_t     cfstim_deadline;
        void                  (*cfstim_func)(unsigned long arg);
        unsigned long           cfstim_arg;
        volatile callout_id_t   cfstim_cid;
        uint64_t                cfstim_count;
} cfs_timer_t;

#define CFS_MAX_SCHEDULE_TIMEOUT LONG_MAX

#define __cfs_wait_event(wq, condition)                         \
do {                                                            \
        struct cfs_waitlink __wait;                             \
                                                                \
        cfs_waitlink_init(&__wait);                             \
        cfs_waitq_add(&(wq), &__wait);                          \
        for (;;) {                                              \
                if (condition)                                  \
                        break;                                  \
                                                                \
                mutex_enter(&__wait.cfswl_lock);                \
                if (!__wait.cfswl_evhit)                        \
                        cv_wait(&__wait.cfswl_cv,               \
                                &__wait.cfswl_lock);            \
                __wait.cfswl_evhit = 0;                         \
                mutex_exit(&__wait.cfswl_lock);                 \
        }                                                       \
        cfs_waitq_del(&(wq), &__wait);                          \
        cfs_waitlink_fini(&__wait);                             \
} while (0)

#define cfs_wait_event(wq, condition)                           \
do {                                                            \
        if (condition)                                          \
                break;                                          \
        __cfs_wait_event(wq, condition);                        \
} while (0)

#define __cfs_wait_event_interruptible(wq, condition, ex, ret)  \
do {                                                            \
        struct cfs_waitlink __wait;                             \
        int                 __wret = 1;                         \
                                                                \
        cfs_waitlink_init(&__wait);                             \
        if (!ex) {                                              \
                cfs_waitq_add(&(wq), &__wait);                  \
        }                                                       \
        for (;;) {                                              \
                if (ex) {                                       \
                        cfs_waitq_add_exclusive(&(wq), &__wait);\
                }                                               \
                if (condition)  {                               \
                        break;                                  \
                }                                               \
                mutex_enter(&__wait.cfswl_lock);                \
                if (!__wait.cfswl_evhit)                        \
                        __wret = cv_wait_sig(&__wait.cfswl_cv,  \
                                             &__wait.cfswl_lock);\
                __wait.cfswl_evhit = 0;                         \
                mutex_exit(&__wait.cfswl_lock);                 \
                if (__wret == 0) {                              \
                        (ret) = -ERESTARTSYS;                   \
                        break;                                  \
                }                                               \
                if (condition) {                                \
                        break;                                  \
                }                                               \
                if (ex) {                                       \
                        cfs_waitq_del(&(wq), &__wait);          \
                }                                               \
        }                                                       \
        cfs_waitq_del(&(wq), &__wait);                          \
        cfs_waitlink_fini(&__wait);                             \
} while (0)


#define cfs_wait_event_interruptible(wq, condition, rc)         \
do {                                                            \
        rc = 0;                                                 \
        if (!condition)                                         \
                __cfs_wait_event_interruptible(wq, condition,   \
                                               0, rc);          \
} while (0)

#define cfs_wait_event_interruptible_exclusive(wq, condition, rc) \
do {                                                            \
        rc = 0;                                                 \
        if (!condition)                                         \
                __cfs_wait_event_interruptible(wq, condition,   \
                                               1, rc);          \
} while (0)

#define __cfs_wait_event_timeout(wq, condition, ret)            \
do {                                                            \
        struct cfs_waitlink __wait;                             \
                                                                \
        cfs_waitlink_init(&__wait);                             \
        cfs_waitq_add(&(wq), &__wait);                          \
        for (;;) {                                              \
                if (condition)  {                               \
                        break;                                  \
                }                                               \
                mutex_enter(&__wait.cfswl_lock);                \
                if (!__wait.cfswl_evhit)                        \
                        (ret) = cv_reltimedwait(&__wait.cfswl_cv,\
                                                 &__wait.cfswl_lock,\
                                                 (ret),         \
                                                 TR_CLOCK_TICK);\
                __wait.cfswl_evhit = 0;                         \
                mutex_exit(&__wait.cfswl_lock);                 \
                if (condition) {                                \
                        break;                                  \
                }                                               \
                if ((ret) == -1) {                              \
                        (ret) = 0;                              \
                        break;                                  \
                }                                               \
        }                                                       \
        cfs_waitq_del(&(wq), &__wait);                          \
        cfs_waitlink_fini(&__wait);                             \
} while (0)

/*
 * retval > 0; condition met; we're good.
 * retval == 0; timed out.
*/
#define cfs_waitq_wait_event_timeout(wq, condition, timeout, rc)     \
do {                                                                 \
        typecheck(clock_t, rc);                                      \
        rc = timeout;                                                \
        if (!(condition))                                            \
                __cfs_wait_event_timeout(wq, condition, rc);         \
} while (0)

#define __cfs_wait_event_interruptible_timeout(wq, condition, ret) \
do {                                                            \
        struct cfs_waitlink __wait;                             \
                                                                \
        cfs_waitlink_init(&__wait);                             \
        cfs_waitq_add(&(wq), &__wait);                          \
        for (;;) {                                              \
                if (condition)  {                               \
                        break;                                  \
                }                                               \
                mutex_enter(&__wait.cfswl_lock);                \
                if (!__wait.cfswl_evhit)                        \
                        (ret) = cv_reltimedwait_sig(&__wait.cfswl_cv,\
                                                     &__wait.cfswl_lock,\
                                                     (ret),     \
                                                     TR_CLOCK_TICK);\
                __wait.cfswl_evhit = 0;                         \
                mutex_exit(&__wait.cfswl_lock);                 \
                if (condition) {                                \
                        break;                                  \
                }                                               \
                if ((ret) == 0) {                               \
                        (ret) = -ERESTARTSYS;                   \
                        break;                                  \
                } else if ((ret) == -1) {                       \
                        (ret) = 0;                              \
                        break;                                  \
                }                                               \
        }                                                       \
        cfs_waitq_del(&(wq), &__wait);                          \
        cfs_waitlink_fini(&__wait);                             \
} while (0)

/*
 * retval > 0; condition met; we're good.
 * retval < 0; interrupted by signal.
 * retval == 0; timed out.
*/
#define cfs_waitq_wait_event_interruptible_timeout(wq, condition, timeout, rc)\
do {                                                                          \
        typecheck(clock_t, rc);                                               \
        rc = timeout;                                                         \
        if (!(condition))                                                     \
                __cfs_wait_event_interruptible_timeout(wq, condition, rc);    \
} while (0)

#define cfs_request_module(name, ...)        0

#define __cfs_module_get(mod)           do {} while (0)
#define cfs_try_module_get(mod)         1
#define cfs_module_put(mod)             do {} while (0)
#define cfs_module_refcount(x)          1
#define cfs_module_name(x)              ""

typedef struct cfs_module {
        char *name;
} cfs_module_t;

#define THIS_MODULE (void *)0x11111111

#define EXPORT_SYMBOL(s)
#define MODULE_AUTHOR(s)
#define MODULE_DESCRIPTION(s)
#define MODULE_LICENSE(s)

#define LUSTREFS_DRIVER "lustrefs"

#define isxdigit(c)     (('0' <= (c) && (c) <= '9') \
                         || ('a' <= (c) && (c) <= 'f') \
                         || ('A' <= (c) && (c) <= 'F'))

#define isalnum(c)      (('0' <= (c) && (c) <= '9') \
                         || ('a' <= (c) && (c) <= 'z') \
                         || ('A' <= (c) && (c) <= 'Z'))

#define isspace(ch) (((ch) == ' ') || ((ch) == '\r') || ((ch) == '\n') || \
                     ((ch) == '\t') || ((ch) == '\f'))

/*
 *  cache alignment size
 */
#define CFS_L1_CACHE_ALIGN(x) P2ROUNDUP_TYPED(x, 128, typeof(x))
#define __cacheline_aligned   __attribute__((__aligned__(128)))

/*
 * SMP ...
 */
#define CFS_NR_CPUS             NCPU
#define cfs_num_possible_cpus() CFS_NR_CPUS
#define cfs_num_online_cpus()   ncpus_online

static inline int
cfs_cpu_online(int i)
{
        int    rc = 0;
        cpu_t *c;

        mutex_enter(&cpu_lock);

        c  = cpu_get(i);

        if (c != NULL && cpu_is_online(c) &&
            ((c->cpu_flags & (CPU_QUIESCED | CPU_READY)) == CPU_READY))
                rc = 1;

        mutex_exit(&cpu_lock);
        
        return rc;
}

#endif /* __LIBCFS_SOLARIS_SOLARIS_PRIM_H__ */
