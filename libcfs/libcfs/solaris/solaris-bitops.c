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
 * Copyright  2009 Sun Microsystems, Inc. All rights reserved
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * libcfs/libcfs/solaris/solaris-bitops.c
 *
 */

#include <libcfs/libcfs.h>

#define OFF_BY_START(start) ((start)/BT_NBIPUL)

unsigned long
cfs_find_next_zero_bit(unsigned long *addr, unsigned long size,
                       unsigned long offset)
{
        unsigned long *word, *last;
        unsigned long first_bit, bit, base;

        word = addr + OFF_BY_START(offset);
        last = addr + OFF_BY_START(size-1);
        first_bit = offset % BT_NBIPUL;
        base = offset - first_bit;

        if (offset >= size)
        	return (size);
        if (first_bit != 0) {
        	unsigned long tmp = *word;
        	bit = first_bit + __cfs_ffz(tmp >> first_bit);
        	if (bit < BT_NBIPUL)
        		goto found;
        	word++;
        	base += BT_NBIPUL;
        }
        while (word <= last) {
        	if (*word != ~0UL) {
        		bit = __cfs_ffz(*word);
        		goto found;
        	}
        	word++;
        	base += BT_NBIPUL;
        }
        return (size);
found:
        return (base + bit);
}

unsigned long
cfs_find_first_zero_bit(unsigned long *addr, unsigned long size)
{
        return (cfs_find_next_zero_bit(addr, size, 0));
}

unsigned long
cfs_find_next_bit(unsigned long *addr, unsigned long size, unsigned long offset)
{
        unsigned long *word, *last;
        unsigned long first_bit, bit, base;

        word = addr + OFF_BY_START(offset);
        last = addr + OFF_BY_START(size-1);
        first_bit = offset % BT_NBIPUL;
        base = offset - first_bit;

        if (offset >= size)
        	return (size);
        if (first_bit != 0) {
        	unsigned long tmp = (*word) & (~0UL << first_bit);
        	if (tmp != 0) {
        		bit = __cfs_ffs(tmp);
        		if (bit < BT_NBIPUL)
        			goto found;
        	}
        	word++;
        	base += BT_NBIPUL;
        }
        while (word <= last) {
        	if (*word != 0UL) {
        		bit = __cfs_ffs(*word);
        		goto found;
        	}
        	word++;
        	base += BT_NBIPUL;
        }
        return (size);
found:
        return (base + bit);
}

unsigned long
cfs_find_first_bit(unsigned long *addr, unsigned long size)
{
        return (cfs_find_next_bit(addr, size, 0));
}
