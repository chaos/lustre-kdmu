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
 * libcfs/include/libcfs/solaris/solaris-mem.h
 *
 */

#ifndef __LIBCFS_SOLARIS_SOLARIS_MEM_H__
#define __LIBCFS_SOLARIS_SOLARIS_MEM_H__

#ifndef __LIBCFS_LIBCFS_H__
#error Do not #include this file directly. #include <libcfs/libcfs.h> instead
#endif

#define CFS_ALLOC_ATOMIC_TRY   CFS_ALLOC_ATOMIC

#define cfs_alloc_large(size)           cfs_alloc(size, 0)
#define cfs_free_large(addr)            cfs_free(addr)

typedef void *cfs_page_t;
typedef void *cfs_mem_cache_t;

#define CFS_PAGE_SIZE                   PAGESIZE
#define CFS_PAGE_SHIFT                  PAGESHIFT
#define CFS_PAGE_MASK                   PAGEMASK

#define cfs_num_physpages               physinstalled

#define CFS_NUM_CACHEPAGES              physinstalled

#define CFS_SLAB_HWCACHE_ALIGN          1
#define CFS_SLAB_DESTROY_BY_RCU         0

extern void *cfs_alloc(size_t nr_bytes, uint32_t flags);
extern void  cfs_free(void *addr);

extern cfs_mem_cache_t *cfs_mem_cache_create(const char *name, size_t size,
    size_t offset, unsigned long flags);
extern int cfs_mem_cache_destroy(cfs_mem_cache_t *cachep);
extern void *cfs_mem_cache_alloc(cfs_mem_cache_t *cachep, int flags);
extern void cfs_mem_cache_free(cfs_mem_cache_t *cachep, void *objp);

extern cfs_page_t *cfs_alloc_page(unsigned int flags);
extern void cfs_free_page(cfs_page_t *page);
extern void cfs_get_page(cfs_page_t *page);
extern void cfs_put_page(cfs_page_t *page);
extern int  cfs_page_count(cfs_page_t *page);
extern void *cfs_kmap(cfs_page_t *page);
extern void cfs_kunmap(cfs_page_t *page);
extern void *cfs_page_address(cfs_page_t *page);
extern page_t *cfs_page_2_ospage(cfs_page_t *page);
extern cfs_page_t *cfs_wrap_ospage(page_t *pp);
extern void cfs_unwrap_ospage(cfs_page_t *page);

/*
 * XXX below definitions needed by common code shared with linux.
 */
#define cfs_page_index(p)   (0)
#define CFS_DECL_MMSPACE
#define CFS_MMSPACE_OPEN
#define CFS_MMSPACE_CLOSE

#define cfs_copy_from_user(kaddr, uaddr, size) ddi_copyin(uaddr, kaddr, size, 0)
#define cfs_copy_to_user(uaddr, kaddr, size)   ddi_copyout(kaddr, uaddr, size, 0)

/*
 * Linux kernel slab shrinker emulation. Currently just stubs.
 */
struct cfs_shrinker {
        ;
};

#define CFS_DEFAULT_SEEKS (0)
#define __GFP_FS (1)

typedef int (*cfs_shrinker_t)(int, unsigned int);

static inline struct cfs_shrinker *cfs_set_shrinker(int seeks,
                                                    cfs_shrinker_t shrinkert)
{
        return (struct cfs_shrinker *)0xdeadbea1; // Cannot return NULL here
}

static inline void cfs_remove_shrinker(struct cfs_shrinker *shrinker)
{
}

#define libcfs_memory_pressure_set() do {} while (0)
#define libcfs_memory_pressure_clr() do {} while (0)

#endif /* __LIBCFS_SOLARIS_SOLARIS_MEM_H__ */
