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
 * libcfs/include/libcfs/solaris/solaris-time.h
 *
 */

#ifndef __LIBCFS_SOLARIS_SOLARIS_TIME_H__
#define __LIBCFS_SOLARIS_SOLARIS_TIME_H__

#ifndef __LIBCFS_LIBCFS_H__
#error Do not #include this file directly. #include <libcfs/libcfs.h> instead
#endif

#ifndef __KERNEL__
#error This include is only for kernel use.
#endif

static inline void cfs_gettimeofday(struct timeval *tv)
{
        timestruc_t ts;

        gethrestime(&ts);

        tv->tv_sec = ts.tv_sec;
        tv->tv_usec = ts.tv_nsec / 1000;
}

/* Portable time API */

/*
 * Platform provides three opaque data-types:
 *
 *  cfs_time_t        represents point in time. This is internal kernel
 *                    time rather than "wall clock". This time bears no
 *                    relation to gettimeofday().
 *
 *  cfs_duration_t    represents time interval with resolution of internal
 *                    platform clock
 *
 *  cfs_fs_time_t     represents instance in world-visible time. This is
 *                    used in file-system time-stamps
 *
 *  cfs_time_t     cfs_time_current(void);
 *  cfs_time_t     cfs_time_add    (cfs_time_t, cfs_duration_t);
 *  cfs_duration_t cfs_time_sub    (cfs_time_t, cfs_time_t);
 *  int            cfs_time_before (cfs_time_t, cfs_time_t);
 *  int            cfs_time_beforeq(cfs_time_t, cfs_time_t);
 *
 *  cfs_duration_t cfs_duration_build(int64_t);
 *
 *  time_t         cfs_duration_sec (cfs_duration_t);
 *  void           cfs_duration_usec(cfs_duration_t, struct timeval *);
 *  void           cfs_duration_nsec(cfs_duration_t, struct timespec *);
 *
 *  void           cfs_fs_time_current(cfs_fs_time_t *);
 *  time_t         cfs_fs_time_sec    (cfs_fs_time_t *);
 *  void           cfs_fs_time_usec   (cfs_fs_time_t *, struct timeval *);
 *  void           cfs_fs_time_nsec   (cfs_fs_time_t *, struct timespec *);
 *  int            cfs_fs_time_before (cfs_fs_time_t *, cfs_fs_time_t *);
 *  int            cfs_fs_time_beforeq(cfs_fs_time_t *, cfs_fs_time_t *);
 *
 *  CFS_TIME_FORMAT
 *  CFS_DURATION_FORMAT
 *
 */

#define ONE_BILLION ((uint64_t)1000000000)
#define ONE_MILLION 1000000

#define CFS_HZ hz

typedef struct timespec cfs_fs_time_t;

static inline void cfs_fs_time_usec(cfs_fs_time_t *t, struct timeval *v)
{
        v->tv_sec  = t->tv_sec;
        v->tv_usec = t->tv_nsec / 1000;
}

static inline void cfs_fs_time_nsec(cfs_fs_time_t *t, struct timespec *s)
{
        *s = *t;
}

/*
 * internal helper function used by cfs_fs_time_before*()
 */
static inline unsigned long long __cfs_fs_time_flat(cfs_fs_time_t *t)
{
        return (unsigned long long)t->tv_sec * ONE_BILLION + t->tv_nsec;
}

/*
 * Generic kernel stuff
 */

typedef clock_t cfs_time_t;      /* lbolt */
typedef long cfs_duration_t;


static inline cfs_time_t cfs_time_current(void)
{
        return lbolt;
}

static inline time_t cfs_time_current_sec(void)
{
        timestruc_t ts;

        gethrestime(&ts);

        return ts.tv_sec;
}

static inline void cfs_fs_time_current(cfs_fs_time_t *t)
{
        gethrestime(t);
}

static inline time_t cfs_fs_time_sec(cfs_fs_time_t *t)
{
        return t->tv_sec;
}

static inline int cfs_fs_time_before(cfs_fs_time_t *t1, cfs_fs_time_t *t2)
{
        return __cfs_fs_time_flat(t1) < __cfs_fs_time_flat(t2);
}

static inline int cfs_fs_time_beforeq(cfs_fs_time_t *t1, cfs_fs_time_t *t2)
{
        return __cfs_fs_time_flat(t1) <= __cfs_fs_time_flat(t2);
}

static inline cfs_duration_t cfs_time_seconds(int seconds)
{
        return ((cfs_duration_t)seconds) * hz;
}

static inline time_t cfs_duration_sec(cfs_duration_t d)
{
        return d / hz;
}

static inline void cfs_duration_usec(cfs_duration_t d, struct timeval *s)
{
        s->tv_sec = d / hz;
        s->tv_usec = (d - (cfs_duration_t)s->tv_sec * hz) * (ONE_MILLION / hz);
}

static inline void cfs_duration_nsec(cfs_duration_t d, struct timespec *s)
{
        s->tv_sec = d / hz;
        s->tv_nsec = (d - (cfs_duration_t)s->tv_sec * hz) * (ONE_BILLION / hz);
}

#define cfs_time_before(t1, t2)    ((long)(t2) - (long)(t1) > 0)
#define cfs_time_beforeq(t1, t2)   ((long)(t2) - (long)(t1) >= 0)

static inline __u64 cfs_time_current_64(void)
{
#if defined(_LP64)
        return (__u64)lbolt64;
#else
        __u64 t;

        t = lbolt64;
        t = atomic_cas_64((volatile uint64_t *)&lbolt64, t, t);
        return (t);
#endif /* _LP64 */
}

static inline __u64 cfs_time_add_64(__u64 t, __u64 d)
{
        return t + d;
}

static inline __u64 cfs_time_shift_64(int seconds)
{
        return cfs_time_add_64(cfs_time_current_64(),
                               cfs_time_seconds(seconds));
}

static inline int cfs_time_before_64(__u64 t1, __u64 t2)
{
        return (__s64)t2 - (__s64)t1 > 0;
}

static inline int cfs_time_beforeq_64(__u64 t1, __u64 t2)
{
        return (__s64)t2 - (__s64)t1 >= 0;
}

/*
 * One jiffy
 */
#define CFS_TICK                (1)

#define CFS_TIME_T              "%ld"
#define CFS_DURATION_T          "%ld"

#endif /* __LIBCFS_SOLARIS_SOLARIS_TIME_H__ */
