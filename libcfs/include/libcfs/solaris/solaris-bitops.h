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
 * libcfs/include/libcfs/solaris/solaris-bitops.h
 *
 */

#ifndef __LIBCFS_SOLARIS_SOLARIS_BITOPS_H__
#define __LIBCFS_SOLARIS_SOLARIS_BITOPS_H__

#ifndef __LIBCFS_LIBCFS_H__
#error Do not #include this file directly. #include <libcfs/libcfs.h> instead
#endif

#ifndef __KERNEL__
#error This include is only for kernel use.
#endif

unsigned long cfs_find_next_zero_bit(unsigned long *addr, unsigned long size,
                                     unsigned long offset);
unsigned long cfs_find_first_zero_bit(unsigned long *addr, unsigned long size);
unsigned long cfs_find_next_bit(unsigned long *addr, unsigned long size,
                                unsigned long offset); 
unsigned long cfs_find_first_bit(unsigned long *addr, unsigned long size);

#define cfs_set_bit(nr, addr) BT_ATOMIC_SET((ulong_t *)addr, nr)
#define cfs_clear_bit(nr, addr) BT_ATOMIC_CLEAR((ulong_t *)addr, nr)
#define cfs_test_bit(nr, addr) BT_TEST((ulong_t *)addr, nr)

static inline int
cfs_test_and_set_bit(int nr, volatile void *addr)
{
        int rc;

        /*
         * rc == -1 if bit was already set, 0 otherwise
         */
        BT_ATOMIC_SET_EXCL((ulong_t *)addr, nr, rc);
        return (((rc == 0) ? rc : 1));
}

static inline int
cfs_test_and_clear_bit(int nr, volatile void *addr)
{
        int rc;

        /*
         * rc == -1 if bit was already set, 0 otherwise
         */
        BT_ATOMIC_CLEAR_EXCL((ulong_t *)addr, nr, rc);
        return (((rc == 0) ? rc : 1));
}

static inline int
cfs_ffs(int x)
{
        int r = 1;

        if (!x)
                return 0;
        if (!(x & 0xffff)) {
                x >>= 16;
                r += 16;
        }
        if (!(x & 0xff)) {
                x >>= 8;
                r += 8;
        }
        if (!(x & 0xf)) {
                x >>= 4;
                r += 4;
        }
        if (!(x & 3)) {
                x >>= 2;
                r += 2;
        }
        if (!(x & 1)) {
                x >>= 1;
                r += 1;
        }
        return r;
}
static inline int
cfs_fls(int x)
{
        int r = 32;

        if (!x)
                return 0;
        if (!(x & 0xffff0000u)) {
                x <<= 16;
                r -= 16;
        }
        if (!(x & 0xff000000u)) {
                x <<= 8;
                r -= 8;
        }
        if (!(x & 0xf0000000u)) {
                x <<= 4;
                r -= 4;
        }
        if (!(x & 0xc0000000u)) {
                x <<= 2;
                r -= 2;
        }
        if (!(x & 0x80000000u)) {
                x <<= 1;
                r -= 1;
        }
        return r;
}

#define cfs_ffz(x) cfs_ffs(~(x))
#define cfs_flz(x) cfs_fls(~(x))

static inline unsigned long
__cfs_ffs(unsigned long data)
{
        int pos = 0;

#if BT_NBIPUL == 64
        if ((data & 0xFFFFFFFF) == 0) {
        	pos += 32;
        	data >>= 32;
        }
#endif
        if ((data & 0xFFFF) == 0) {
        	pos += 16;
        	data >>= 16;
        }
        if ((data & 0xFF) == 0) {
        	pos += 8;
        	data >>= 8;
        }
        if ((data & 0xF) == 0) {
        	pos += 4;
        	data >>= 4;
        }
        if ((data & 0x3) == 0) {
        	pos += 2;
        	data >>= 2;
        }
        if ((data & 0x1) == 0)
        	pos += 1;

        return (pos);
}

static inline unsigned long
__cfs_fls(unsigned long data)
{
        int pos = 31;

#if BT_NBIPUL == 64

        if (data & 0xFFFFFFFF00000000) {
                data >>= 32;
                pos += 32;
        }
#endif

        if (!(data & 0xFFFF0000u)) {
                data <<= 16;
                pos -= 16;
        }
        if (!(data & 0xFF000000u)) {
                data <<= 8;
                pos -= 8;
        }
        if (!(data & 0xF0000000u)) {
                data <<= 4;
                pos -= 4;
        }
        if (!(data & 0xC0000000u)) {
                data <<= 2;
                pos -= 2;
        }
        if (!(data & 0x80000000u)) {
                data <<= 1;
                pos -= 1;
        }
        return pos;
}

#define __cfs_ffz(x) __cfs_ffs(~(x))
#define __cfs_flz(x) __cfs_fls(~(x))

/*
 * non atomic versions of set/clear/test bit for little endian layout.
 */
   
static inline int
ext2_set_bit(int nr, __u32 *bm)
{
        int oldbit;
        ulong_t *addr = (ulong_t *)bm;

#if defined(_BIG_ENDIAN)
        nr = nr ^ ((BITS_PER_LONG-1) & ~0x7);
#endif

        oldbit = BT_TEST(addr, nr);
        BT_SET(addr, nr);
        return (oldbit);
}

static inline int
ext2_clear_bit(int nr, __u32 *bm)
{
        int oldbit;
        ulong_t *addr = (ulong_t *)bm;

#if defined(_BIG_ENDIAN)
        nr = nr ^ ((BITS_PER_LONG-1) & ~0x7);
#endif

        oldbit = BT_TEST(addr, nr);
        BT_CLEAR(addr, nr);
        return (oldbit);
}

static inline int
ext2_test_bit(int nr, __u32 *bm)
{
        int oldbit;
        ulong_t *addr = (ulong_t *)bm;

#if defined(_BIG_ENDIAN)
        nr = nr ^ ((BITS_PER_LONG-1) & ~0x7);
#endif

        oldbit = BT_TEST(addr, nr);
        return (oldbit);
}

#endif /* __LIBCFS_SOLARIS_SOLARIS_BITOPS_H__ */
