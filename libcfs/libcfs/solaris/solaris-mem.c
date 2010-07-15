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
 * libcfs/libcfs/solaris/solaris-mem.c
 *
 */

#include <sys/kmem.h>
#include <sys/vmsystm.h>
#include <vm/page.h>
#include <vm/vm_dep.h>
#include <vm/seg_kpm.h>

#define DEBUG_SUBSYSTEM S_LNET

#include <libcfs/libcfs.h>

typedef struct cfs_mem_cache_impl {
        kmem_cache_t *cfsmc_cachep;
        size_t        cfsmc_bufsz;
} cfs_mem_cache_impl_t;

#define CFSPAGE_PHYSPAGE_ALLOCED 0
#define CFSPAGE_PHYSPAGE_WRAPPED 1

typedef struct cfs_page_impl {
        page_t       *cfspg_pp;
        int          cfspg_refcnt;
	int          cfspg_type;	   
} cfs_page_impl_t;

void *
cfs_alloc(size_t nr_bytes, u_int32_t flags)
{
        int     kmflags;
        size_t *addr;

        LASSERT(!cfs_in_interrupt() || flags == CFS_ALLOC_ATOMIC);

        nr_bytes += sizeof (size_t);

        if (flags & CFS_ALLOC_ATOMIC) {
                kmflags = KM_NOSLEEP;
        } else {
                kmflags = KM_SLEEP;
        }
        if (flags & CFS_ALLOC_ZERO) {
                addr = kmem_zalloc(nr_bytes, kmflags);
        } else {
                addr = kmem_alloc(nr_bytes, kmflags);
        }
        
        /* addr is guaranteed to be 8 bytes aligned */

        *addr++ = nr_bytes;
        return (addr);
}

void
cfs_free(void *addr)
{
        size_t *saddr    =    addr;
        size_t  nr_bytes = *--saddr;

        kmem_free(saddr, nr_bytes);
}

cfs_mem_cache_t *
cfs_mem_cache_create(const char *name, size_t size, size_t offset,
                     unsigned long flags)
{
        cfs_mem_cache_impl_t *cp;
        size_t                align = offset;
        
        /*
         * kmem_cache_create() by default doesn't support
         * bigger than PAGESIZE alignment.
         */
        LASSERT(offset <= PAGESIZE);
        LASSERT(!(offset & (offset - 1)));

        if ((flags & CFS_SLAB_HWCACHE_ALIGN) && offset < L2CACHE_ALIGN) {
                align = L2CACHE_ALIGN;
        } else {
                align = offset;
        }

        cp = kmem_alloc(sizeof (cfs_mem_cache_impl_t), KM_SLEEP);

        cp->cfsmc_cachep = kmem_cache_create((char *)name, size, align, NULL,
                                             NULL, NULL, NULL, NULL, 0);
        cp->cfsmc_bufsz = size;

        return ((cfs_mem_cache_t *)cp);
}

int
cfs_mem_cache_destroy(cfs_mem_cache_t *cachep)
{
        cfs_mem_cache_impl_t *cp = (cfs_mem_cache_impl_t *)cachep;

        kmem_cache_destroy((kmem_cache_t *)cp->cfsmc_cachep);
        kmem_free(cp, sizeof (cfs_mem_cache_impl_t));
        return (0);
}

void *
cfs_mem_cache_alloc(cfs_mem_cache_t *cachep, int flags)
{
        cfs_mem_cache_impl_t *cp = (cfs_mem_cache_impl_t *)cachep;
        int                   kmflags;
        void                 *buf;

        if (flags & CFS_ALLOC_ATOMIC) {
                kmflags = KM_NOSLEEP;
        } else {
                kmflags = KM_SLEEP;
        }

        buf = kmem_cache_alloc(cp->cfsmc_cachep, kmflags);
        if (flags & CFS_ALLOC_ZERO) {
                size_t sz = cp->cfsmc_bufsz;
                bzero(buf, sz);
        }
        return (buf);
}

void
cfs_mem_cache_free(cfs_mem_cache_t *cachep, void *objp)
{
        cfs_mem_cache_impl_t *cp = (cfs_mem_cache_impl_t *)cachep;

        kmem_cache_free(cp->cfsmc_cachep, objp);
}

cfs_page_t *
cfs_alloc_page(unsigned int flags)
{
        cfs_page_impl_t       *cfs_pg;
        page_t                *pp;
        int                    kmflags = (flags &
                                          CFS_ALLOC_ATOMIC) ? KM_NOSLEEP :
                                                              KM_SLEEP;
        uint_t                 pcw_flags = (flags &
                                            CFS_ALLOC_ATOMIC) ? 0 : PG_WAIT;
        caddr_t                addr;
        struct seg             kseg;
        size_t                 cfspg_sz;

        if (!page_resv(1, kmflags)) {
                LASSERT(flags & CFS_ALLOC_ATOMIC);
                return (NULL);
        }

        if (!page_create_wait(1, pcw_flags)) {
                LASSERT(flags & CFS_ALLOC_ATOMIC);
                page_unresv(1);
                return (NULL);
        }

        kseg.s_as = &kas;
        addr = (caddr_t)((GETTICK() >> 4) << PAGESHIFT);

        pp = page_get_freelist(&kvp, 0, &kseg, addr, PAGESIZE,
                               PG_NORELOC, NULL);
        if (pp == NULL) {
                pp = page_get_cachelist(&kvp, 0, &kseg, addr,
                                        PG_NORELOC, NULL);
        }
        if (pp == NULL) {
                page_unresv(1);
                page_create_putback(1);
                return (NULL);
        }
        LASSERT(PAGE_EXCL(pp));
        LASSERT(PP_ISFREE(pp));
        LASSERT(pp->p_szc == 0);
        PP_CLRFREE(pp);

        cfspg_sz = sizeof (cfs_page_impl_t);
        cfs_pg = (cfs_page_impl_t *)kmem_alloc(cfspg_sz, kmflags);
        if (cfs_pg == NULL) {
                LASSERT(flags & CFS_ALLOC_ATOMIC);
                goto fail;
        }

        if (pp->p_vnode != NULL) {
                page_hashout(pp, NULL);
        }
        LASSERT(pp->p_vnode == NULL);
        pp->p_vnode = &kvp;
	/*
         * Make p_offset the same as paddr
         * to avoid kpm use of VAC alias range
         * on sun4u.
         */
        pp->p_offset = ptob(pp->p_pagenum);

        if (flags & CFS_ALLOC_ZERO) {
                pagezero(pp, 0, PAGESIZE);
        }

        cfs_pg->cfspg_pp = pp;
        cfs_pg->cfspg_refcnt = 1;
	cfs_pg->cfspg_type = CFSPAGE_PHYSPAGE_ALLOCED;

#ifdef sun4u
	(void) hat_kpm_mapin(pp, NULL);
#endif
	return ((cfs_page_t *)cfs_pg);

fail:
        page_free(pp, 1);
        page_unresv(1);

        return (NULL);
        
}

void
cfs_free_page(cfs_page_t *page)
{
        cfs_page_impl_t       *cfs_pg = (cfs_page_impl_t *)page;
        page_t                *pp = cfs_pg->cfspg_pp;

        LASSERT(pp != NULL);
        LASSERT(PAGE_EXCL(pp));
        LASSERT(!PP_ISFREE(pp));
        LASSERT(pp->p_vnode == &kvp);
        LASSERT(pp->p_offset == ptob(pp->p_pagenum));
        LASSERT(pp->p_szc == 0);
        LASSERT(cfs_pg->cfspg_refcnt > 0);
	ASSERT(cfs_pg->cfspg_type == CFSPAGE_PHYSPAGE_ALLOCED);

        if (atomic_add_int_nv((volatile uint_t *)&cfs_pg->cfspg_refcnt, -1)) {
                return;
        }

#ifdef sun4u
	hat_kpm_mapout(pp, NULL, hat_kpm_page2va(pp, 1));
#endif
	kmem_free(cfs_pg, sizeof (cfs_page_impl_t));

        pp->p_vnode = NULL;
        page_free(pp, 1);
        page_unresv(1);
}

void *
cfs_kmap(cfs_page_t *page)
{
        cfs_page_impl_t       *cfs_pg = (cfs_page_impl_t *)page;
        page_t                *pp = cfs_pg->cfspg_pp;

        LASSERT(pp != NULL);
        LASSERT(cfs_pg->cfspg_refcnt > 0);

	return ((void *)hat_kpm_page2va(pp, 1));
}

void
cfs_kunmap(cfs_page_t *page)
{
        cfs_page_impl_t       *cfs_pg = (cfs_page_impl_t *)page;

        LASSERT(cfs_pg->cfspg_refcnt > 0);

}

void *
cfs_page_address(cfs_page_t *page)
{
        cfs_page_impl_t       *cfs_pg = (cfs_page_impl_t *)page;
        page_t                *pp = cfs_pg->cfspg_pp;

        LASSERT(pp != NULL);
        LASSERT(cfs_pg->cfspg_refcnt > 0);

	return ((void *)hat_kpm_page2va(pp, 1));
}

void
cfs_get_page(cfs_page_t *page)
{
        cfs_page_impl_t *cfs_pg = (cfs_page_impl_t *)page;
        page_t          *pp = cfs_pg->cfspg_pp;

        LASSERT(pp != NULL);
        LASSERT(cfs_pg->cfspg_refcnt > 0);

        atomic_add_int((volatile uint32_t *)&cfs_pg->cfspg_refcnt, 1);

        ASSERT(cfs_pg->cfspg_refcnt > 1);
}

void
cfs_put_page(cfs_page_t *page)
{
	cfs_page_impl_t *cfs_pg = (cfs_page_impl_t *)page;

	LASSERT(cfs_pg->cfspg_refcnt > 0);

	if (cfs_pg->cfspg_type == CFSPAGE_PHYSPAGE_ALLOCED) {
		cfs_free_page(page);
	} else {
		ASSERT(cfs_pg->cfspg_type == CFSPAGE_PHYSPAGE_WRAPPED);
		cfs_unwrap_ospage(page);
	}
}

int
cfs_page_count(cfs_page_t *page)
{
        cfs_page_impl_t *cfs_pg = (cfs_page_impl_t *)page;

        LASSERT(cfs_pg->cfspg_refcnt > 0);

        return(cfs_pg->cfspg_refcnt);
}

page_t *
cfs_page_2_ospage(cfs_page_t *page)
{
        cfs_page_impl_t *cfs_pg = (cfs_page_impl_t *)page;
        
        return (cfs_pg->cfspg_pp);
}

/*
 * cfs_wrap_ospage() can be used to hide Solaris page_t inside cfs_page_t type
 * so that it can be used by common Lustre code that only knows cfs_page type.
 * This may be used when Lustre client obtains a page from FS page cache
 * rather than via cfs_alloc_page().  The caller is responsible to call
 * cfs_unwrap_ospage() when it no longer needs to use returned cfs_page_t type
 * for this page.
 */
cfs_page_t *
cfs_wrap_ospage(page_t *pp)
{
        size_t                 cfspg_sz;
        cfs_page_impl_t       *cfs_pg;

        cfspg_sz = sizeof (cfs_page_impl_t);

        cfs_pg = (cfs_page_impl_t *)kmem_alloc(cfspg_sz, KM_SLEEP);

        cfs_pg->cfspg_pp = pp;
        cfs_pg->cfspg_refcnt = 1;
	cfs_pg->cfspg_type = CFSPAGE_PHYSPAGE_WRAPPED;

#ifdef sun4u
	(void) hat_kpm_mapin(pp, NULL);
#endif
	return ((cfs_page_t *)cfs_pg);
}

void
cfs_unwrap_ospage(cfs_page_t *page)
{
        cfs_page_impl_t       *cfs_pg = (cfs_page_impl_t *)page;
        page_t                *pp = cfs_pg->cfspg_pp;

	LASSERT(cfs_pg->cfspg_refcnt > 0);
	LASSERT(cfs_pg->cfspg_type == CFSPAGE_PHYSPAGE_WRAPPED);
	LASSERT(pp != NULL);

        if (atomic_add_int_nv((volatile uint_t *)&cfs_pg->cfspg_refcnt, -1)) {
                return;
        }

#ifdef sun4u
	hat_kpm_mapout(pp, NULL, hat_kpm_page2va(pp, 1));
#endif
	kmem_free(cfs_pg, sizeof (cfs_page_impl_t));
}
