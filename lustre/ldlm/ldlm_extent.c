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
 * lustre/ldlm/ldlm_extent.c
 *
 * Author: Peter Braam <braam@clusterfs.com>
 * Author: Phil Schwan <phil@clusterfs.com>
 */

#define DEBUG_SUBSYSTEM S_LDLM
#ifndef __KERNEL__
# include <liblustre.h>
#else
# include <libcfs/libcfs.h>
#endif

#include <lustre_dlm.h>
#include <obd_support.h>
#include <obd.h>
#include <obd_class.h>
#include <lustre_lib.h>

#include "ldlm_internal.h"

#define LDLM_MAX_GROWN_EXTENT (32 * 1024 * 1024 - 1)

/* fixup the ldlm_extent after expanding */
static void ldlm_extent_internal_policy_fixup(struct ldlm_lock *req,
                                              struct ldlm_extent *new_ex,
                                              int conflicting)
{
        ldlm_mode_t req_mode = req->l_req_mode;
        __u64 req_start = req->l_req_extent.start;
        __u64 req_end = req->l_req_extent.end;
        __u64 req_align, mask;

        if (conflicting > 32 && (req_mode == LCK_PW || req_mode == LCK_CW)) {
                if (req_end < req_start + LDLM_MAX_GROWN_EXTENT)
                        new_ex->end = min(req_start + LDLM_MAX_GROWN_EXTENT,
                                          new_ex->end);
        }

        if (new_ex->start == 0 && new_ex->end == OBD_OBJECT_EOF) {
                EXIT;
                return;
        }

        /* we need to ensure that the lock extent is properly aligned to what
         * the client requested.  We align it to the lowest-common denominator
         * of the clients requested lock start and end alignment. */
        mask = 0x1000ULL;
        req_align = (req_end + 1) | req_start;
        if (req_align != 0) {
                while ((req_align & mask) == 0)
                        mask <<= 1;
        }
        mask -= 1;
        /* We can only shrink the lock, not grow it.
         * This should never cause lock to be smaller than requested,
         * since requested lock was already aligned on these boundaries. */
        new_ex->start = ((new_ex->start - 1) | mask) + 1;
        new_ex->end = ((new_ex->end + 1) & ~mask) - 1;
        LASSERTF(new_ex->start <= req_start,
                 "mask "LPX64" grant start "LPU64" req start "LPU64"\n",
                 mask, new_ex->start, req_start);
        LASSERTF(new_ex->end >= req_end,
                 "mask "LPX64" grant end "LPU64" req end "LPU64"\n",
                 mask, new_ex->end, req_end);
}


static int ldlm_check_contention(struct ldlm_lock *lock, int contended_locks)
{
        struct ldlm_resource *res = lock->l_resource;
        cfs_time_t now = cfs_time_current();

        if (OBD_FAIL_CHECK(OBD_FAIL_LDLM_SET_CONTENTION))
                return 1;

        CDEBUG(D_DLMTRACE, "contended locks = %d\n", contended_locks);
        if (contended_locks > res->lr_namespace->ns_contended_locks)
                res->lr_contention_time = now;
        return cfs_time_before(now, cfs_time_add(res->lr_contention_time,
                cfs_time_seconds(res->lr_namespace->ns_contention_time)));
}

struct ldlm_extent_compat_args {
        cfs_list_t *work_list;
        struct ldlm_lock *lock;
        ldlm_mode_t mode;
        int *locks;
        int *compat;
        int *conflicts;
};

static enum interval_iter ldlm_extent_compat_cb(struct interval_node *n,
                                                void *data)
{
        struct ldlm_extent_compat_args *priv = data;
        struct ldlm_interval *node = to_ldlm_interval(n);
        struct ldlm_extent *extent;
        cfs_list_t *work_list = priv->work_list;
        struct ldlm_lock *lock, *enq = priv->lock;
        ldlm_mode_t mode = priv->mode;
        int count = 0;
        ENTRY;

        LASSERT(!cfs_list_empty(&node->li_group));

        cfs_list_for_each_entry(lock, &node->li_group, l_sl_policy) {
                /* interval tree is for granted lock */
                LASSERTF(mode == lock->l_granted_mode,
                         "mode = %s, lock->l_granted_mode = %s\n",
                         ldlm_lockname[mode],
                         ldlm_lockname[lock->l_granted_mode]);

                /* only count _requested_ region overlapped locks as contended
                 * locks */
                if (lock->l_req_extent.end >= enq->l_req_extent.start &&
                    lock->l_req_extent.start <= enq->l_req_extent.end) {
                        count++;
                        (*priv->conflicts)++;
                }
                if (lock->l_blocking_ast)
                        ldlm_add_ast_work_item(lock, enq, work_list);
        }

        /* don't count conflicting glimpse locks */
        extent = ldlm_interval_extent(node);
        if (!(mode == LCK_PR &&
            extent->start == 0 && extent->end == OBD_OBJECT_EOF))
                *priv->locks += count;

        if (priv->compat)
                *priv->compat = 0;

        RETURN(INTERVAL_ITER_CONT);
}

static int
ldlm_extent_compat_granted_queue(cfs_list_t *queue, struct ldlm_lock *req,
                                 int *flags, ldlm_error_t *err,
                                 cfs_list_t *work_list, int *contended_locks)
{
        struct ldlm_resource *res = req->l_resource;
        ldlm_mode_t req_mode = req->l_req_mode;
        __u64 req_start = req->l_req_extent.start;
        __u64 req_end = req->l_req_extent.end;
        int compat = 1, conflicts;
        /* Using interval tree for granted lock */
        struct ldlm_interval_tree *tree;
        struct ldlm_extent_compat_args data = {.work_list = work_list,
                                       .lock = req,
                                       .locks = contended_locks,
                                       .compat = &compat,
                                       .conflicts = &conflicts };
        struct interval_node_extent ex = { .start = req_start,
                                           .end = req_end };
        int idx, rc;
        ENTRY;


        for (idx = 0; idx < LCK_MODE_NUM; idx++) {
                tree = &res->lr_itree[idx];
                if (tree->lit_root == NULL) /* empty tree, skipped */
                        continue;

                data.mode = tree->lit_mode;
                if (lockmode_compat(req_mode, tree->lit_mode)) {
                        struct ldlm_interval *node;
                        struct ldlm_extent *extent;

                        if (req_mode != LCK_GROUP)
                                continue;

                        /* group lock, grant it immediately if
                         * compatible */
                        node = to_ldlm_interval(tree->lit_root);
                        extent = ldlm_interval_extent(node);
                        if (req->l_policy_data.l_extent.gid ==
                            extent->gid)
                                RETURN(2);
                }

                if (tree->lit_mode == LCK_GROUP) {
                        if (*flags & LDLM_FL_BLOCK_NOWAIT) {
                                compat = -EWOULDBLOCK;
                                goto destroylock;
                        }

                        *flags |= LDLM_FL_NO_TIMEOUT;
                        if (!work_list)
                                RETURN(0);

                        /* if work list is not NULL,add all
                           locks in the tree to work list */
                        compat = 0;
                        interval_iterate(tree->lit_root,
                                         ldlm_extent_compat_cb, &data);
                        continue;
                }


                if (!work_list) {
                        rc = interval_is_overlapped(tree->lit_root, &ex);
                        if (rc)
                                RETURN(0);
                } else {
                        struct interval_node_extent result_ext = {
                                .start = req->l_policy_data.l_extent.start,
                                .end = req->l_policy_data.l_extent.end };

                        conflicts = 0;
                        interval_search_expand_extent(tree->lit_root, &ex,
                                                      &result_ext,
                                                      ldlm_extent_compat_cb,
                                                      &data);
                        req->l_policy_data.l_extent.start = result_ext.start;
                        req->l_policy_data.l_extent.end = result_ext.end;
                        /* for granted locks, count non-compatible not overlapping
                         * locks in traffic index */
                        req->l_traffic += tree->lit_size - conflicts;

                        if (!cfs_list_empty(work_list)) {
                                if (compat)
                                        compat = 0;
                                /* if there is at least 1 conflicting lock, we
                                 * do not expand to the left, since we often
                                 * continue writing to the right.
                                 */
                                req->l_policy_data.l_extent.start = req_start;
                        }
                }
        }

        RETURN(compat);
destroylock:
        cfs_list_del_init(&req->l_res_link);
        ldlm_lock_destroy_nolock(req);
        *err = compat;
        RETURN(compat);
}

static int
ldlm_extent_compat_waiting_queue(cfs_list_t *queue, struct ldlm_lock *req,
                                 int *flags, ldlm_error_t *err,
                                 cfs_list_t *work_list, int *contended_locks)
{
        cfs_list_t *tmp;
        struct ldlm_lock *lock;
        ldlm_mode_t req_mode = req->l_req_mode;
        __u64 req_start = req->l_req_extent.start;
        __u64 req_end = req->l_req_extent.end;
        int compat = 1;
        int scan = 0;
        int check_contention;
        ENTRY;

        cfs_list_for_each(tmp, queue) {
                check_contention = 1;

                lock = cfs_list_entry(tmp, struct ldlm_lock, l_res_link);

                if (req == lock)
                        break;

                if (unlikely(scan)) {
                        /* We only get here if we are queuing GROUP lock
                           and met some incompatible one. The main idea of this
                           code is to insert GROUP lock past compatible GROUP
                           lock in the waiting queue or if there is not any,
                           then in front of first non-GROUP lock */
                        if (lock->l_req_mode != LCK_GROUP) {
                                /* Ok, we hit non-GROUP lock, there should be no
                                   more GROUP locks later on, queue in front of
                                   first non-GROUP lock */

                                ldlm_resource_insert_lock_after(lock, req);
                                cfs_list_del_init(&lock->l_res_link);
                                ldlm_resource_insert_lock_after(req, lock);
                                compat = 0;
                                break;
                        }
                        if (req->l_policy_data.l_extent.gid ==
                            lock->l_policy_data.l_extent.gid) {
                                /* found it */
                                ldlm_resource_insert_lock_after(lock, req);
                                compat = 0;
                                break;
                        }
                        continue;
                }

                /* locks are compatible, overlap doesn't matter */
                if (lockmode_compat(lock->l_req_mode, req_mode)) {
                        if (req_mode == LCK_PR &&
                            ((lock->l_policy_data.l_extent.start <=
                              req->l_policy_data.l_extent.start) &&
                             (lock->l_policy_data.l_extent.end >=
                              req->l_policy_data.l_extent.end))) {
                                /* If we met a PR lock just like us or wider,
                                   and nobody down the list conflicted with
                                   it, that means we can skip processing of
                                   the rest of the list and safely place
                                   ourselves at the end of the list, or grant
                                   (dependent if we met an conflicting locks
                                   before in the list).
                                   In case of 1st enqueue only we continue
                                   traversing if there is something conflicting
                                   down the list because we need to make sure
                                   that something is marked as AST_SENT as well,
                                   in cse of empy worklist we would exit on
                                   first conflict met. */
                                /* There IS a case where such flag is
                                   not set for a lock, yet it blocks
                                   something. Luckily for us this is
                                   only during destroy, so lock is
                                   exclusive. So here we are safe */
                                if (!(lock->l_flags & LDLM_FL_AST_SENT)) {
                                        RETURN(compat);
                                }
                        }

                        /* non-group locks are compatible, overlap doesn't
                           matter */
                        if (likely(req_mode != LCK_GROUP))
                                continue;

                        /* If we are trying to get a GROUP lock and there is
                           another one of this kind, we need to compare gid */
                        if (req->l_policy_data.l_extent.gid ==
                            lock->l_policy_data.l_extent.gid) {
                                /* We are scanning queue of waiting
                                 * locks and it means current request would
                                 * block along with existing lock (that is
                                 * already blocked.
                                 * If we are in nonblocking mode - return
                                 * immediately */
                                if (*flags & LDLM_FL_BLOCK_NOWAIT) {
                                        compat = -EWOULDBLOCK;
                                        goto destroylock;
                                }
                                /* If this group lock is compatible with another
                                 * group lock on the waiting list, they must be
                                 * together in the list, so they can be granted
                                 * at the same time.  Otherwise the later lock
                                 * can get stuck behind another, incompatible,
                                 * lock. */
                                ldlm_resource_insert_lock_after(lock, req);
                                /* Because 'lock' is not granted, we can stop
                                 * processing this queue and return immediately.
                                 * There is no need to check the rest of the
                                 * list. */
                                RETURN(0);
                        }
                }

                if (unlikely(req_mode == LCK_GROUP &&
                             (lock->l_req_mode != lock->l_granted_mode))) {
                        scan = 1;
                        compat = 0;
                        if (lock->l_req_mode != LCK_GROUP) {
                                /* Ok, we hit non-GROUP lock, there should
                                 * be no more GROUP locks later on, queue in
                                 * front of first non-GROUP lock */

                                ldlm_resource_insert_lock_after(lock, req);
                                cfs_list_del_init(&lock->l_res_link);
                                ldlm_resource_insert_lock_after(req, lock);
                                break;
                        }
                        if (req->l_policy_data.l_extent.gid ==
                            lock->l_policy_data.l_extent.gid) {
                                /* found it */
                                ldlm_resource_insert_lock_after(lock, req);
                                break;
                        }
                        continue;
                }

                if (unlikely(lock->l_req_mode == LCK_GROUP)) {
                        /* If compared lock is GROUP, then requested is PR/PW/
                         * so this is not compatible; extent range does not
                         * matter */
                        if (*flags & LDLM_FL_BLOCK_NOWAIT) {
                                compat = -EWOULDBLOCK;
                                goto destroylock;
                        } else {
                                *flags |= LDLM_FL_NO_TIMEOUT;
                        }
                } else if (!work_list) {
                        if (lock->l_policy_data.l_extent.end < req_start ||
                            lock->l_policy_data.l_extent.start > req_end)
                                /* if a non group lock doesn't overlap skip it */
                                continue;
                        RETURN(0);
                } else {
                        /* for waiting locks, count all non-compatible locks in
                         * traffic index */
                        ++req->l_traffic;
                        ++lock->l_traffic;

                        /* adjust policy */
                        if (lock->l_policy_data.l_extent.end < req_start) {
                                /*     lock            req
                                 * ------------+
                                 * ++++++      |   +++++++
                                 *      +      |   +
                                 * ++++++      |   +++++++
                                 * ------------+
                                 */
                                if (lock->l_policy_data.l_extent.end >
                                    req->l_policy_data.l_extent.start)
                                        req->l_policy_data.l_extent.start =
                                             lock->l_policy_data.l_extent.end+1;
                                continue;
                        } else if (lock->l_req_extent.end < req_start) {
                                /*     lock            req
                                 * ------------------+
                                 * ++++++          +++++++
                                 *      +          + |
                                 * ++++++          +++++++
                                 * ------------------+
                                 */
                                lock->l_policy_data.l_extent.end =
                                                          req_start - 1;
                                req->l_policy_data.l_extent.start =
                                                              req_start;
                                continue;
                        } else if (lock->l_policy_data.l_extent.start >
                                   req_end) {
                                /*  req              lock
                                 *              +--------------
                                 *  +++++++     |    +++++++
                                 *        +     |    +
                                 *  +++++++     |    +++++++
                                 *              +--------------
                                 */
                                if (lock->l_policy_data.l_extent.start <
                                    req->l_policy_data.l_extent.end)
                                        req->l_policy_data.l_extent.end =
                                           lock->l_policy_data.l_extent.start-1;
                                continue;
                        } else if (lock->l_req_extent.start > req_end) {
                                /*  req              lock
                                 *      +----------------------
                                 *  +++++++          +++++++
                                 *      | +          +
                                 *  +++++++          +++++++
                                 *      +----------------------
                                 */
                                lock->l_policy_data.l_extent.start =
                                                            req_end + 1;
                                req->l_policy_data.l_extent.end=req_end;
                                continue;
                        }
                } /* policy_adj */

                compat = 0;
                if (work_list) {
                        /* don't count conflicting glimpse locks */
                        if (lock->l_flags & LDLM_FL_HAS_INTENT)
                                check_contention = 0;

                        *contended_locks += check_contention;

                        if (lock->l_blocking_ast)
                                ldlm_add_ast_work_item(lock, req, work_list);
                }
        }

        RETURN(compat);
destroylock:
        cfs_list_del_init(&req->l_res_link);
        ldlm_lock_destroy_nolock(req);
        *err = compat;
        RETURN(compat);
}
/* Determine if the lock is compatible with all locks on the queue.
 * We stop walking the queue if we hit ourselves so we don't take
 * conflicting locks enqueued after us into accound, or we'd wait forever.
 *
 * 0 if the lock is not compatible
 * 1 if the lock is compatible
 * 2 if this group lock is compatible and requires no further checking
 * negative error, such as EWOULDBLOCK for group locks
 *
 * Note: policy adjustment only happends during the 1st lock enqueue procedure
 */
static int
ldlm_extent_compat_queue(cfs_list_t *queue, struct ldlm_lock *req,
                         int *flags, ldlm_error_t *err,
                         cfs_list_t *work_list, int *contended_locks)
{
        struct ldlm_resource *res = req->l_resource;
        ldlm_mode_t req_mode = req->l_req_mode;
        __u64 req_start = req->l_req_extent.start;
        __u64 req_end = req->l_req_extent.end;
        int compat = 1;
        ENTRY;

        lockmode_verify(req_mode);

        if (queue == &res->lr_granted)
                compat = ldlm_extent_compat_granted_queue(queue, req, flags,
                                                          err, work_list,
                                                          contended_locks);
        else
                compat = ldlm_extent_compat_waiting_queue(queue, req, flags,
                                                          err, work_list,
                                                          contended_locks);


        if (ldlm_check_contention(req, *contended_locks) &&
            compat == 0 &&
            (*flags & LDLM_FL_DENY_ON_CONTENTION) &&
            req->l_req_mode != LCK_GROUP &&
            req_end - req_start <=
            req->l_resource->lr_namespace->ns_max_nolock_size)
                GOTO(destroylock, compat = -EUSERS);

        RETURN(compat);
destroylock:
        cfs_list_del_init(&req->l_res_link);
        ldlm_lock_destroy_nolock(req);
        *err = compat;
        RETURN(compat);
}

static void discard_bl_list(cfs_list_t *bl_list)
{
        cfs_list_t *tmp, *pos;
        ENTRY;

        cfs_list_for_each_safe(pos, tmp, bl_list) {
                struct ldlm_lock *lock =
                        cfs_list_entry(pos, struct ldlm_lock, l_bl_ast);

                cfs_list_del_init(&lock->l_bl_ast);
                LASSERT(lock->l_flags & LDLM_FL_AST_SENT);
                lock->l_flags &= ~LDLM_FL_AST_SENT;
                LASSERT(lock->l_bl_ast_run == 0);
                LASSERT(lock->l_blocking_lock);
                LDLM_LOCK_RELEASE(lock->l_blocking_lock);
                lock->l_blocking_lock = NULL;
                LDLM_LOCK_RELEASE(lock);
        }
        EXIT;
}

static inline void ldlm_process_extent_init(struct ldlm_lock *lock)
{
        lock->l_policy_data.l_extent.start = 0;
        lock->l_policy_data.l_extent.end = OBD_OBJECT_EOF;
}

static inline void ldlm_process_extent_fini(struct ldlm_lock *lock, int *flags)
{
        if (lock->l_traffic > 4)
                lock->l_policy_data.l_extent.start = lock->l_req_extent.start;
        ldlm_extent_internal_policy_fixup(lock,
                                          &lock->l_policy_data.l_extent,
                                          lock->l_traffic);
        if (lock->l_req_extent.start != lock->l_policy_data.l_extent.start ||
            lock->l_req_extent.end   != lock->l_policy_data.l_extent.end)
                *flags |= LDLM_FL_LOCK_CHANGED;
}

/* If first_enq is 0 (ie, called from ldlm_reprocess_queue):
  *   - blocking ASTs have already been sent
  *   - must call this function with the ns lock held
  *
  * If first_enq is 1 (ie, called from ldlm_lock_enqueue):
  *   - blocking ASTs have not been sent
  *   - must call this function with the ns lock held once */
int ldlm_process_extent_lock(struct ldlm_lock *lock, int *flags, int first_enq,
                             ldlm_error_t *err, cfs_list_t *work_list)
{
        struct ldlm_resource *res = lock->l_resource;
        CFS_LIST_HEAD(rpc_list);
        int rc, rc2;
        int contended_locks = 0;
        ENTRY;

        LASSERT(cfs_list_empty(&res->lr_converting));
        LASSERT(!(*flags & LDLM_FL_DENY_ON_CONTENTION) ||
                !(lock->l_flags & LDLM_AST_DISCARD_DATA));
        check_res_locked(res);
        *err = ELDLM_OK;

        if (!first_enq) {
                /* Careful observers will note that we don't handle -EWOULDBLOCK
                 * here, but it's ok for a non-obvious reason -- compat_queue
                 * can only return -EWOULDBLOCK if (flags & BLOCK_NOWAIT).
                 * flags should always be zero here, and if that ever stops
                 * being true, we want to find out. */
                LASSERT(*flags == 0);
                rc = ldlm_extent_compat_queue(&res->lr_granted, lock, flags,
                                              err, NULL, &contended_locks);
                if (rc == 1) {
                        rc = ldlm_extent_compat_queue(&res->lr_waiting, lock,
                                                      flags, err, NULL,
                                                      &contended_locks);
                }
                if (rc == 0)
                        RETURN(LDLM_ITER_STOP);

                ldlm_resource_unlink_lock(lock);

                if (OBD_FAIL_CHECK(OBD_FAIL_LDLM_CANCEL_EVICT_RACE)) {
                        lock->l_policy_data.l_extent.start =
                                lock->l_req_extent.start;
                        lock->l_policy_data.l_extent.end =
                                lock->l_req_extent.end;
                } else {
                        ldlm_process_extent_fini(lock, flags);
                }

                ldlm_grant_lock(lock, work_list);
                RETURN(LDLM_ITER_CONTINUE);
        }

 restart:
        contended_locks = 0;

        ldlm_process_extent_init(lock);

        rc = ldlm_extent_compat_queue(&res->lr_granted, lock, flags, err,
                                      &rpc_list, &contended_locks);
        if (rc < 0)
                GOTO(out, rc); /* lock was destroyed */
        if (rc == 2)
                goto grant;

        rc2 = ldlm_extent_compat_queue(&res->lr_waiting, lock, flags, err,
                                       &rpc_list, &contended_locks);
        if (rc2 < 0)
                GOTO(out, rc = rc2); /* lock was destroyed */

        if (rc + rc2 == 2) {
        grant:
                ldlm_resource_unlink_lock(lock);
                ldlm_process_extent_fini(lock, flags);
                ldlm_grant_lock(lock, NULL);
        } else {
                /* If either of the compat_queue()s returned failure, then we
                 * have ASTs to send and must go onto the waiting list.
                 *
                 * bug 2322: we used to unlink and re-add here, which was a
                 * terrible folly -- if we goto restart, we could get
                 * re-ordered!  Causes deadlock, because ASTs aren't sent! */
                if (cfs_list_empty(&lock->l_res_link))
                        ldlm_resource_add_lock(res, &res->lr_waiting, lock);
                unlock_res(res);
                rc = ldlm_run_ast_work(&rpc_list, LDLM_WORK_BL_AST);

                if (OBD_FAIL_CHECK(OBD_FAIL_LDLM_OST_FAIL_RACE) &&
                    !ns_is_client(res->lr_namespace))
                        class_fail_export(lock->l_export);
 
                lock_res(res);
                if (rc == -ERESTART) {

                        /* 15715: The lock was granted and destroyed after
                         * resource lock was dropped. Interval node was freed
                         * in ldlm_lock_destroy. Anyway, this always happens
                         * when a client is being evicted. So it would be
                         * ok to return an error. -jay */
                        if (lock->l_destroyed) {
                                *err = -EAGAIN;
                                GOTO(out, rc = -EAGAIN);
                        }

                        /* lock was granted while resource was unlocked. */
                        if (lock->l_granted_mode == lock->l_req_mode) {
                                /* bug 11300: if the lock has been granted,
                                 * break earlier because otherwise, we will go
                                 * to restart and ldlm_resource_unlink will be
                                 * called and it causes the interval node to be
                                 * freed. Then we will fail at
                                 * ldlm_extent_add_lock() */
                                *flags &= ~(LDLM_FL_BLOCK_GRANTED | LDLM_FL_BLOCK_CONV |
                                            LDLM_FL_BLOCK_WAIT);
                                GOTO(out, rc = 0);
                        }

                        GOTO(restart, -ERESTART);
                }

                *flags |= LDLM_FL_BLOCK_GRANTED;
                /* this way we force client to wait for the lock
                 * endlessly once the lock is enqueued -bzzz */
                *flags |= LDLM_FL_NO_TIMEOUT;

        }
        RETURN(0);
out:
        if (!cfs_list_empty(&rpc_list)) {
                LASSERT(!(lock->l_flags & LDLM_AST_DISCARD_DATA));
                discard_bl_list(&rpc_list);
        }
        RETURN(rc);
}

/* When a lock is cancelled by a client, the KMS may undergo change if this
 * is the "highest lock".  This function returns the new KMS value.
 * Caller must hold ns_lock already.
 *
 * NB: A lock on [x,y] protects a KMS of up to y + 1 bytes! */
__u64 ldlm_extent_shift_kms(struct ldlm_lock *lock, __u64 old_kms)
{
        struct ldlm_resource *res = lock->l_resource;
        cfs_list_t *tmp;
        struct ldlm_lock *lck;
        __u64 kms = 0;
        ENTRY;

        /* don't let another thread in ldlm_extent_shift_kms race in
         * just after we finish and take our lock into account in its
         * calculation of the kms */
        lock->l_flags |= LDLM_FL_KMS_IGNORE;

        cfs_list_for_each(tmp, &res->lr_granted) {
                lck = cfs_list_entry(tmp, struct ldlm_lock, l_res_link);

                if (lck->l_flags & LDLM_FL_KMS_IGNORE)
                        continue;

                if (lck->l_policy_data.l_extent.end >= old_kms)
                        RETURN(old_kms);

                /* This extent _has_ to be smaller than old_kms (checked above)
                 * so kms can only ever be smaller or the same as old_kms. */
                if (lck->l_policy_data.l_extent.end + 1 > kms)
                        kms = lck->l_policy_data.l_extent.end + 1;
        }
        LASSERTF(kms <= old_kms, "kms "LPU64" old_kms "LPU64"\n", kms, old_kms);

        RETURN(kms);
}

cfs_mem_cache_t *ldlm_interval_slab;
struct ldlm_interval *ldlm_interval_alloc(struct ldlm_lock *lock)
{
        struct ldlm_interval *node;
        ENTRY;

        LASSERT(lock->l_resource->lr_type == LDLM_EXTENT);
        OBD_SLAB_ALLOC_PTR_GFP(node, ldlm_interval_slab, CFS_ALLOC_IO);
        if (node == NULL)
                RETURN(NULL);

        CFS_INIT_LIST_HEAD(&node->li_group);
        ldlm_interval_attach(node, lock);
        RETURN(node);
}

void ldlm_interval_free(struct ldlm_interval *node)
{
        if (node) {
                LASSERT(cfs_list_empty(&node->li_group));
                LASSERT(!interval_is_intree(&node->li_node));
                OBD_SLAB_FREE(node, ldlm_interval_slab, sizeof(*node));
        }
}

/* interval tree, for LDLM_EXTENT. */
void ldlm_interval_attach(struct ldlm_interval *n,
                          struct ldlm_lock *l)
{
        LASSERT(l->l_tree_node == NULL);
        LASSERT(l->l_resource->lr_type == LDLM_EXTENT);

        cfs_list_add_tail(&l->l_sl_policy, &n->li_group);
        l->l_tree_node = n;
}

struct ldlm_interval *ldlm_interval_detach(struct ldlm_lock *l)
{
        struct ldlm_interval *n = l->l_tree_node;

        if (n == NULL)
                return NULL;

        LASSERT(!cfs_list_empty(&n->li_group));
        l->l_tree_node = NULL;
        cfs_list_del_init(&l->l_sl_policy);

        return (cfs_list_empty(&n->li_group) ? n : NULL);
}

static inline int lock_mode_to_index(ldlm_mode_t mode)
{
        int index;

        LASSERT(mode != 0);
        LASSERT(IS_PO2(mode));
        for (index = -1; mode; index++, mode >>= 1) ;
        LASSERT(index < LCK_MODE_NUM);
        return index;
}

void ldlm_extent_add_lock(struct ldlm_resource *res,
                          struct ldlm_lock *lock)
{
        struct interval_node *found, **root;
        struct ldlm_interval *node;
        struct ldlm_extent *extent;
        int idx;

        LASSERT(lock->l_granted_mode == lock->l_req_mode);

        node = lock->l_tree_node;
        LASSERT(node != NULL);
        LASSERT(!interval_is_intree(&node->li_node));

        idx = lock_mode_to_index(lock->l_granted_mode);
        LASSERT(lock->l_granted_mode == 1 << idx);
        LASSERT(lock->l_granted_mode == res->lr_itree[idx].lit_mode);

        /* node extent initialize */
        extent = &lock->l_policy_data.l_extent;
        interval_set(&node->li_node, extent->start, extent->end);

        root = &res->lr_itree[idx].lit_root;
        found = interval_insert(&node->li_node, root);
        if (found) { /* The policy group found. */
                struct ldlm_interval *tmp = ldlm_interval_detach(lock);
                LASSERT(tmp != NULL);
                ldlm_interval_free(tmp);
                ldlm_interval_attach(to_ldlm_interval(found), lock);
        }
        res->lr_itree[idx].lit_size++;

        /* even though we use interval tree to manage the extent lock, we also
         * add the locks into grant list, for debug purpose, .. */
        ldlm_resource_add_lock(res, &res->lr_granted, lock);
}

void ldlm_extent_unlink_lock(struct ldlm_lock *lock)
{
        struct ldlm_resource *res = lock->l_resource;
        struct ldlm_interval *node = lock->l_tree_node;
        struct ldlm_interval_tree *tree;
        int idx;

        if (!node || !interval_is_intree(&node->li_node)) /* duplicate unlink */
                return;

        idx = lock_mode_to_index(lock->l_granted_mode);
        LASSERT(lock->l_granted_mode == 1 << idx);
        tree = &res->lr_itree[idx];

        LASSERT(tree->lit_root != NULL); /* assure the tree is not null */

        tree->lit_size--;
        node = ldlm_interval_detach(lock);
        if (node) {
                interval_erase(&node->li_node, &tree->lit_root);
                ldlm_interval_free(node);
        }
}
