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
 * libcfs/include/libcfs/solaris/solaris-utils.h
 *
 */

#ifndef __LIBCFS_SOLARIS_SOLARIS_UTILS_H__
#define __LIBCFS_SOLARIS_SOLARIS_UTILS_H__

#ifndef __LIBCFS_LIBCFS_H__
#error Do not #include this file directly. #include <libcfs/libcfs.h> instead
#endif

#ifdef __KERNEL__

/*
 * It's weird but there is no common includes defining strsep,
 * see for example smbsrv/string.h.
 */
extern char *strsep(char **, const char *);

extern char *strnchr(const char *, size_t, int);

extern int sscanf(const char *, const char *, ...);
extern int vsscanf(const char *, const char *, va_list);


/*
 *  printk flags
 */
#define CFS_KERN_EMERG      "<0>"   /* system is unusable                   */
#define CFS_KERN_ALERT      "<1>"   /* action must be taken immediately     */
#define CFS_KERN_CRIT       "<2>"   /* critical conditions                  */
#define CFS_KERN_ERR        "<3>"   /* error conditions                     */
#define CFS_KERN_WARNING    "<4>"   /* warning conditions                   */
#define CFS_KERN_NOTICE     "<5>"   /* normal but significant condition     */
#define CFS_KERN_INFO       "<6>"   /* informational                        */
#define CFS_KERN_DEBUG      "<7>"   /* debug-level messages                 */

#define printk(format, args...) cmn_err(CE_NOTE, format, ## args)
#define vprintk(format, arg)    vcmn_err(CE_NOTE, format, arg)
#define printk_ratelimit()      0

/*
 * randomize
 */
#define cfs_get_random_bytes_prim(buf, size) \
        random_get_pseudo_bytes((uint8_t *)(buf), (size_t)(size))

/*
 * arithmetic
 */
#define do_div(a,b)                     \
        ({                              \
                unsigned long remainder;\
                remainder = (a) % (b);  \
                (a) = (a) / (b);        \
                (remainder);            \
        })

/*
 * strings
 */
extern long               simple_strtol  (const char *,char **,unsigned int);
extern long long          simple_strtoll (const char *,char **,unsigned int);
extern unsigned long      simple_strtoul (const char *,char **,unsigned int);
extern unsigned long long simple_strtoull(const char *,char **,unsigned int);

#define	tolower(C)	(((C) >= 'A' && (C) <= 'Z') ? (C) - 'A' + 'a' : (C))
#define	toupper(C)	(((C) >= 'a' && (C) <= 'z') ? (C) - 'a' + 'A' : (C))

#endif /* __KERNEL__ */

#define min_t(type,x,y)                                                 \
	({ type __x = (x); type __y = (y); __x < __y ? __x: __y; })
#define max_t(type,x,y) \
	({ type __x = (x); type __y = (y); __x > __y ? __x: __y; })

/*
 * USERMODHELPER support
 */

typedef struct cfs_cfsd_arg {
        int  cmd;
        int  len;
        int  argc;
        int  envc;
        char data[0];
} cfs_cfsd_arg_t;

/* values for cfsd_arg cmd */
#define CFS_CFSD_UMH            1

/* values for cfsd_res status */
#define CFS_CFSD_DR_OK          0
#define CFS_CFSD_DR_BADARG      1
#define CFS_CFSD_DR_BADCMD      2
#define CFS_CFSD_DR_NOMEM       3
#define CFS_CFSD_DR_NOFORK      4
#define CFS_CFSD_DR_NOEXEC      5

typedef struct cfs_cfsd_res {
        int err;
        int status;
} cfs_cfsd_res_t;


#ifdef __KERNEL__

extern int cfs_set_door(int door_fd);
extern int cfs_user_mode_helper(char *path, char *argv[], char *envp[]);

#define USERMODEHELPER(path, argv, envp) cfs_user_mode_helper(path, argv, envp)

#endif /* __KERNEL__ */

#endif /* __LIBCFS_SOLARIS_SOLARIS_UTILS_H__ */
