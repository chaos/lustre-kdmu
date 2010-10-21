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
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * libcfs/include/libcfs/util/params_tree_util.h
 *
 * params_tree userspace APIs.
 *
 * Author: LiuYing <emoly.liu@sun.com>
 *
 */

#ifndef _PARAMS_TREE_UTIL_H_
#define _PARAMS_TREE_UTIL_H_

#include <libcfs/params_tree.h>

/* parameter entry list */
struct params_entry_list {
        int pel_name_len;
        char *pel_name;  /* full pathname of the entry */
        int pel_mode;    /* entry mode */
        struct params_entry_list *pel_next;
};
int params_list(const char *pattern, struct params_entry_list **pel_ptr);
int params_read(char *path, int path_len, char *read_buf, int buf_len,
                long long *offset, int *eof);
int params_write(char *path, int path_len, char *write_buf, int buf_len);
int params_unpack(char *inbuf, char *outbuf, int outbuf_len);
int params_value_output(struct libcfs_param_data *data, char *outbuf);
void params_free_entrylist(struct params_entry_list *entry_list);

#endif
