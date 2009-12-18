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
 * Copyright  2008 Sun Microsystems, Inc. All rights reserved
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/fld/lproc_fld.c
 *
 * FLD (FIDs Location Database)
 *
 * Author: Yury Umanets <umka@clusterfs.com>
 */

#ifndef EXPORT_SYMTAB
# define EXPORT_SYMTAB
#endif
#define DEBUG_SUBSYSTEM S_FLD

#ifdef __KERNEL__
# include <libcfs/libcfs.h>
# include <linux/module.h>
#else /* __KERNEL__ */
# include <liblustre.h>
#endif

#include <obd.h>
#include <obd_class.h>
#include <dt_object.h>
#include <md_object.h>
#include <obd_support.h>
#include <lustre_req_layout.h>
#include <lustre_fld.h>
#include "fld_internal.h"

#ifdef __KERNEL__
static int
fld_proc_read_targets(char *page, char **start, off_t off,
                      int count, int *eof, void *data)
{
        struct lu_client_fld *fld;
        struct lu_fld_target *target;
	int total = 0, rc;
	ENTRY;

        LIBCFS_PARAM_GET_DATA(fld, data, NULL);
        LASSERT(fld != NULL);
        *eof = 1;
        spin_lock(&fld->lcf_lock);
        list_for_each_entry(target,
                            &fld->lcf_targets, ft_chain)
        {
                rc = libcfs_param_snprintf(page, count, data, LP_STR,
                                           "%s\n", fld_target_name(target));
                if (rc > 0) {
                        page += rc;
                        count -= rc;
                        total += rc;
                }
                if (count == 0)
                        break;
        }
        spin_unlock(&fld->lcf_lock);
	RETURN(total);
}

static int
fld_proc_read_hash(char *page, char **start, off_t off,
                   int count, int *eof, void *data)
{
        struct lu_client_fld *fld;
	int rc;
	ENTRY;

        LIBCFS_PARAM_GET_DATA(fld, data, NULL);
        LASSERT(fld != NULL);
        *eof = 1;
        spin_lock(&fld->lcf_lock);
        rc = libcfs_param_snprintf(page, count, data, LP_STR,
                                   "%s\n", fld->lcf_hash->fh_name);
        spin_unlock(&fld->lcf_lock);

	RETURN(rc);
}

static int
fld_proc_write_hash(libcfs_file_t *file, const char *buffer,
                    unsigned long count, void *data)
{
        struct lu_client_fld *fld;
        struct lu_fld_hash *hash = NULL;
        int i;
	ENTRY;

        LIBCFS_PARAM_GET_DATA(fld, data, NULL);
        LASSERT(fld != NULL);

        for (i = 0; fld_hash[i].fh_name != NULL; i++) {
                if (count != strlen(fld_hash[i].fh_name))
                        continue;

                if (!strncmp(fld_hash[i].fh_name, buffer, count)) {
                        hash = &fld_hash[i];
                        break;
                }
        }

        if (hash != NULL) {
                spin_lock(&fld->lcf_lock);
                fld->lcf_hash = hash;
                spin_unlock(&fld->lcf_lock);

                CDEBUG(D_INFO, "%s: Changed hash to \"%s\"\n",
                       fld->lcf_name, hash->fh_name);
        }

        RETURN(count);
}

static int
fld_proc_write_cache_flush(libcfs_file_t *file, const char *buffer,
                           unsigned long count, void *data)
{
        struct lu_client_fld *fld;
	ENTRY;

        LIBCFS_PARAM_GET_DATA(fld, data, NULL);
        LASSERT(fld != NULL);

        fld_cache_flush(fld->lcf_cache);

        CDEBUG(D_INFO, "%s: Lookup cache is flushed\n", fld->lcf_name);

        RETURN(count);
}

struct lprocfs_vars fld_server_proc_list[] = {
	{ NULL }};

struct lprocfs_vars fld_client_proc_list[] = {
	{ "targets",     fld_proc_read_targets, NULL, NULL },
	{ "hash",        fld_proc_read_hash, fld_proc_write_hash, NULL },
	{ "cache_flush", NULL, fld_proc_write_cache_flush, NULL },
	{ NULL }};
#endif /* __KERNEL__ */
