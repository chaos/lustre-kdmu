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
 * libcfs/libcfs/solaris/solaris-utils.c
 *
 */

#define DEBUG_SUBSYSTEM S_LNET

#include <libcfs/libcfs.h>

#include <sys/door.h> /* for USERMODEHELPER */

/* XXX: remove these two functions as soon as params_tree landed.
   XXX: params_tree should be fixed to call them only #ifdef LPROCFS */
int insert_proc(void) { return 0; }
void remove_proc(void) {}

/*
 * strings
 */

/* NB:
 * 1) it's safe to convert 'unsigned int base' to 'int'
 * below, because if base is out of range, ddi_* will
 * just return EINVAL and *nptr won't be updated
 * 2) it's safe to discard rc returned by ddi_* because
 * users of simple_* checks *nptr after calling us
 */
long
simple_strtol(const char *str, char **nptr, unsigned int base)
{
        long result = 0;       
        (void) ddi_strtol(str, nptr, (int)base, &result);
        return result;
}

long long
simple_strtoll(const char *str, char **nptr, unsigned int base)
{
        long long result = 0;
        (void) ddi_strtoll(str, nptr, (int)base, &result);
        return result;
}

unsigned long
simple_strtoul(const char *str, char **nptr, unsigned int base)
{
        unsigned long result = 0;
        (void) ddi_strtoul(str, nptr, (int)base, &result);
        return result;
}

unsigned long long
simple_strtoull(const char *str, char **nptr, unsigned int base)
{
        unsigned long long result = 0;
        (void) ddi_strtoull(str, nptr, (int)base, &result);
        return result;
}

/*
 * USERMODEHELPER
 *
 * convert argv, envp to flat buffer to pass up to user space via a door
 * connected to cfsd.
 */

/* door handle to communicate with cfsd */
static door_handle_t cfs_doorh;
kmutex_t cfsd_lock;

/* Returns:
 *  0 for success,
 * -1 if failed
*/
int
cfs_set_door(int door_fd)
{
        mutex_enter(&cfsd_lock);
        if (cfs_doorh)
                door_ki_rele(cfs_doorh);
        cfs_doorh = door_ki_lookup(door_fd);
        mutex_exit(&cfsd_lock);

        if (cfs_doorh == NULL)
                return (-1);

        return (0);
}

int
cfs_user_mode_helper(char *path, char *argv[], char *envp[])
{
        door_arg_t      dargs;
        cfs_cfsd_arg_t *argp;
        cfs_cfsd_res_t  res;
        int             i;
        int             err;
        int             len;
        int             size = 0;
        int             argc;
        int             envc;
        char           *datap;
        door_handle_t   dh;

        mutex_enter(&cfsd_lock);
        dh = cfs_doorh;
        if (dh)
                door_ki_hold(dh);
        mutex_exit(&cfsd_lock);

        if ((void *)dh == NULL) {
                CERROR("can't invoke %s: door handle is not set;"
                       "is cfsd running?\n", argv[0]);
                return (-EINVAL);
        }

        /* calc size of args. ignore path, its just argv[0] */
        for (i = 0; argv[i] != NULL; i++) {
                size += strlen(argv[i]);
                size++; /* add one for null */
        }
        argc = i;
        for (i = 0; envp[i] != NULL; i++) {
                size += strlen(envp[i]);
                size++; /* add one for null */
        }
        envc = i;

        size += sizeof (cfs_cfsd_arg_t);
        argp = (cfs_cfsd_arg_t *)kmem_alloc(size, KM_SLEEP);

        argp->cmd  = CFS_CFSD_UMH;
        argp->len  = size;
        argp->argc = argc;
        argp->envc = envc;

        datap = argp->data;
        for (i = 0; i < argc; i++) {
                len = strlen(argv[i]) + 1;      /* add 1 for null */
                strcpy(datap, argv[i]);
                datap += len;
        }
        for (i = 0; i < envc; i++) {
                len = strlen(envp[i]) + 1;      /* add 1 for null */
                strcpy(datap, envp[i]);
                datap += len;
        }

        dargs.data_ptr  = (char *)argp;
        dargs.data_size = size;
        dargs.desc_ptr  = NULL;
        dargs.desc_num  = 0;
        dargs.rbuf      = (char *)&res;
        dargs.rsize     = sizeof (cfs_cfsd_res_t);

        err = door_ki_upcall(dh, &dargs);

        door_ki_rele(dh);
        kmem_free(argp, size);

        if (err) {
                /* the upcall itself has failed */
                return (-err);
        }

        /*
         * if res.err != 0, res.status could give us a reason for the failure
         * perhaps we should put a CERROR or dtrace probe here.
         */
        return (-res.err);
}
