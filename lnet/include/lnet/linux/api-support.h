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
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#ifndef __LINUX_API_SUPPORT_H__
#define __LINUX_API_SUPPORT_H__

#ifndef __LNET_API_SUPPORT_H__
#error Do not #include this file directly. #include <lnet /api-support.h> instead
#endif

#ifndef __KERNEL__
# include <stdio.h>
# include <stdlib.h>
# include <unistd.h>
# include <time.h>

/* Lots of POSIX dependencies to support PtlEQWait_timeout */
# include <signal.h>
# include <setjmp.h>
# include <time.h>

#ifdef HAVE_LIBREADLINE
#define READLINE_LIBRARY
#include <readline/readline.h>

/* readline.h pulls in a #define that conflicts with one in libcfs.h */
#undef RETURN

/* completion_matches() is #if 0-ed out in modern glibc */
#ifndef completion_matches
#  define completion_matches rl_completion_matches
#endif

#endif /* HAVE_LIBREADLINE */

extern void using_history(void);
extern void stifle_history(int);
extern void add_history(char *);

#endif /* !__KERNEL__ */

#endif
