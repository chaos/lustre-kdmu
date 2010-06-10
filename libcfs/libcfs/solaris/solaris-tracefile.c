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
 * libcfs/libcfs/solaris/solaris-tracefile.c
 *
 */

#define DEBUG_SUBSYSTEM S_LNET
#define LUSTRE_TRACEFILE_PRIVATE

#include <libcfs/libcfs.h>
#include "../tracefile.h"

/* percents to share the total debug memory for each type */
static unsigned int pages_factor[CFS_TCD_TYPE_MAX] = {
        90,  /* 90% pages for CFS_TCD_TYPE_PROC */
        10   /* 10% pages for CFS_TCD_TYPE_INTERRUPT */
};

char *cfs_trace_console_buffers[CFS_NR_CPUS][CFS_TCD_TYPE_MAX];

struct cfs_rw_semaphore cfs_tracefile_sem;

int cfs_tracefile_init_arch()
{
	int                        i, j;
	struct cfs_trace_cpu_data *tcd;

	/* initialize trace_data */
	memset(cfs_trace_data, 0, sizeof(cfs_trace_data));
	for (i = 0; i < CFS_TCD_TYPE_MAX; i++) {
		cfs_trace_data[i] = kmem_alloc(
                        sizeof(union cfs_trace_data_union) * CFS_NR_CPUS,
                        KM_NOSLEEP);
		if (cfs_trace_data[i] == NULL)
			goto out;
	}

	/* arch related info initialized */
	cfs_tcd_for_each(tcd, i, j) {
		tcd->tcd_pages_factor = pages_factor[i];
		tcd->tcd_type = i;
		tcd->tcd_cpu = j;
                cfs_spin_lock_init(&tcd->tcd_lock);
	}

	for (i = 0; i < cfs_num_possible_cpus(); i++)
		for (j = 0; j < CFS_TCD_TYPE_MAX; j++) {
			cfs_trace_console_buffers[i][j] =
				kmem_alloc(CFS_TRACE_CONSOLE_BUFFER_SIZE,
                                           KM_NOSLEEP);

			if (cfs_trace_console_buffers[i][j] == NULL)
				goto out;
		}

	return 0;

out:
	cfs_tracefile_fini_arch();
	printk(CFS_KERN_ERR "lnet: No enough memory\n");
	return -ENOMEM;

}

void cfs_tracefile_fini_arch()
{
	int                        i, j;
	struct cfs_trace_cpu_data *tcd;

	for (i = 0; i < cfs_num_possible_cpus(); i++) {
		for (j = 0; j < CFS_TCD_TYPE_MAX; j++) {
			if (cfs_trace_console_buffers[i][j] != NULL) {
                                kmem_free(cfs_trace_console_buffers[i][j],
                                          CFS_TRACE_CONSOLE_BUFFER_SIZE);
				cfs_trace_console_buffers[i][j] = NULL;
			}
		}
	}

	cfs_tcd_for_each(tcd, i, j) {
                cfs_spin_lock_done(&tcd->tcd_lock);
	}

	for (i = 0; cfs_trace_data[i] != NULL; i++) {
		kmem_free(cfs_trace_data[i],
                          sizeof(union cfs_trace_data_union) * CFS_NR_CPUS);
		cfs_trace_data[i] = NULL;
	}
}

void cfs_tracefile_read_lock()
{
        cfs_down_read(&cfs_tracefile_sem);
}

void cfs_tracefile_read_unlock()
{
	cfs_up_read(&cfs_tracefile_sem);
}

void cfs_tracefile_write_lock()
{
        cfs_down_write(&cfs_tracefile_sem);
}

void cfs_tracefile_write_unlock()
{
	cfs_up_write(&cfs_tracefile_sem);
}

cfs_trace_buf_type_t
cfs_trace_buf_idx_get()
{
        if (servicing_interrupt())
                return CFS_TCD_TYPE_INTERRUPT;
        else
                return CFS_TCD_TYPE_PROC;
}

int cfs_trace_lock_tcd(struct cfs_trace_cpu_data *tcd)
{
	__LASSERT(tcd->tcd_type < CFS_TCD_TYPE_MAX);

	cfs_spin_lock_irqsave(&tcd->tcd_lock, tcd->tcd_lock_flags);

	return 1;
}

void cfs_trace_unlock_tcd(struct cfs_trace_cpu_data *tcd)
{
	__LASSERT(tcd->tcd_type < CFS_TCD_TYPE_MAX);

	cfs_spin_unlock_irqrestore(&tcd->tcd_lock, tcd->tcd_lock_flags);
}

int tcd_owns_tage(struct cfs_trace_cpu_data *tcd, struct cfs_trace_page *tage)
{
	/*
	 * XXX nikita: do NOT call portals_debug_msg() (CDEBUG/ENTRY/EXIT)
	 * from here: this will lead to infinite recursion.
	 */
	return tcd->tcd_cpu == tage->cpu;
}

void
cfs_set_ptldebug_header(struct ptldebug_header *header, int subsys, int mask,
                        const int line, unsigned long stack)
{
	struct timeval tv;

	cfs_gettimeofday(&tv);

	header->ph_subsys = subsys;
	header->ph_mask = mask;
	header->ph_cpu_id = CPU->cpu_id;
	header->ph_sec = (__u32)tv.tv_sec;
	header->ph_usec = tv.tv_usec;
	header->ph_stack = stack;
	header->ph_line_num = line;
	header->ph_extern_pid = 0;

        /* In Solaris, 'pid' is an attribute of user-process not
         * kernel thread. curthread->t_tid is useless becasue always
         * zero for threads spawned with thread_create(). Though,
         * t_did seems to be unique system-wide */
	header->ph_pid = (__u32)curthread->t_did;

        return;
}

void cfs_print_to_console(struct ptldebug_header *hdr, int mask,
                          const char *buf, int len, const char *file,
                          const char *fn)
{
	char *prefix = "Lustre", *ptype = NULL;

	if ((mask & D_EMERG) != 0) {
		prefix = "LustreError";
		ptype = CFS_KERN_EMERG;
	} else if ((mask & D_ERROR) != 0) {
		prefix = "LustreError";
		ptype = CFS_KERN_ERR;
	} else if ((mask & D_WARNING) != 0) {
		prefix = "Lustre";
		ptype = CFS_KERN_WARNING;
	} else if ((mask & (D_CONSOLE | libcfs_printk)) != 0) {
		prefix = "Lustre";
		ptype = CFS_KERN_INFO;
	}

	if ((mask & D_CONSOLE) != 0) {
		printk("%s%s: %.*s", ptype, prefix, len, buf);
	} else {
		printk("%s%s: %d:%d:(%s:%d:%s()) %.*s", ptype, prefix, hdr->ph_pid,
		       hdr->ph_extern_pid, file, hdr->ph_line_num, fn, len, buf);
	}
	return;
}

int cfs_trace_max_debug_mb(void)
{
	int  total_mb = (physinstalled >> (20 - CFS_PAGE_SHIFT));
	
	return MAX(512, (total_mb * 80)/100);
}
