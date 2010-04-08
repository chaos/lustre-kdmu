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
 * lustre/mdd/mdd_trans.c
 *
 * Lustre Metadata Server (mdd) routines
 *
 * Author: Wang Di <wangdi@clusterfs.com>
 */

#ifndef EXPORT_SYMTAB
# define EXPORT_SYMTAB
#endif
#define DEBUG_SUBSYSTEM S_MDS

#include <linux/module.h>
#include <obd.h>
#include <obd_class.h>
#include <lustre_ver.h>
#include <obd_support.h>
#include <lprocfs_status.h>

#include <lustre_mds.h>
#include <lustre/lustre_idl.h>

#include "mdd_internal.h"

int mdd_txn_start_cb(const struct lu_env *env, struct thandle *txn,
                     void *cookie)
{
        return 0;
}

int mdd_txn_stop_cb(const struct lu_env *env, struct thandle *txn,
                    void *cookie)
{
        return 0;
}

int mdd_txn_commit_cb(const struct lu_env *env, struct thandle *txn,
                      void *cookie)
{
        return 0;
}

struct thandle* mdd_trans_create(const struct lu_env *env,
                                struct mdd_device *mdd)
{
        struct thandle *th;
 
        th = mdd_child_ops(mdd)->dt_trans_create(env, mdd->mdd_child);
        return th;
}

int mdd_trans_start(const struct lu_env *env,
                                struct mdd_device *mdd, struct thandle *handle)
{
                
        return mdd_child_ops(mdd)->dt_trans_start(env, mdd->mdd_child, handle);
}

void mdd_trans_stop(const struct lu_env *env, struct mdd_device *mdd,
                    int result, struct thandle *handle)
{
        handle->th_result = result;
        mdd_child_ops(mdd)->dt_trans_stop(env, handle);
}
