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
 * lustre/lod/lod_lov.c
 *
 * Lustre Multi-oBject Device
 *
 */

#ifndef EXPORT_SYMTAB
# define EXPORT_SYMTAB
#endif
#define DEBUG_SUBSYSTEM S_LOV

#ifdef __KERNEL__
#include <libcfs/libcfs.h>
#else
#include <liblustre.h>
#endif

#include <obd_class.h>
#include <obd_lov.h>
#include <lustre/lustre_idl.h>
#include "lod_internal.h"

#define QOS_DEBUG 1 
#define D_QOS D_OTHER

#define TGT_BAVAIL(i) (lov->lov_tgts[i]->ltd_exp->exp_obd->obd_osfs.os_bavail *\
                       lov->lov_tgts[i]->ltd_exp->exp_obd->obd_osfs.os_bsize)


int qos_add_tgt(struct obd_device *obd, __u32 index)
{
        struct lov_obd *lov = &obd->u.lov;
        struct lov_qos_oss *oss, *temposs;
        struct obd_export *exp = lov->lov_tgts[index]->ltd_exp;
        int rc = 0, found = 0;
        ENTRY;

        /* We only need this QOS struct on MDT, not clients - but we may not
         * have registered the LOV's observer yet, so there's no way to know */
        if (!exp || !exp->exp_connection) {
                CERROR("Missing connection\n");
                RETURN(-ENOTCONN);
        }

        down_write(&lov->lov_qos.lq_rw_sem);
        mutex_down(&lov->lov_lock);
        list_for_each_entry(oss, &lov->lov_qos.lq_oss_list, lqo_oss_list) {
                if (obd_uuid_equals(&oss->lqo_uuid,
                                    &exp->exp_connection->c_remote_uuid)) {
                        found++;
                        break;
                }
        }

        if (!found) {
                OBD_ALLOC_PTR(oss);
                if (!oss)
                        GOTO(out, rc = -ENOMEM);
                memcpy(&oss->lqo_uuid,
                       &exp->exp_connection->c_remote_uuid,
                       sizeof(oss->lqo_uuid));
        } else {
                /* Assume we have to move this one */
                list_del(&oss->lqo_oss_list);
        }

        oss->lqo_ost_count++;
        lov->lov_tgts[index]->ltd_qos.ltq_oss = oss;

        /* Add sorted by # of OSTs.  Find the first entry that we're
           bigger than... */
        list_for_each_entry(temposs, &lov->lov_qos.lq_oss_list, lqo_oss_list) {
                if (oss->lqo_ost_count > temposs->lqo_ost_count)
                        break;
        }
        /* ...and add before it.  If we're the first or smallest, temposs
           points to the list head, and we add to the end. */
        list_add_tail(&oss->lqo_oss_list, &temposs->lqo_oss_list);

        lov->lov_qos.lq_dirty = 1;
        lov->lov_qos.lq_rr.lqr_dirty = 1;

        CDEBUG(D_QOS, "add tgt %s to OSS %s (%d OSTs)\n",
               obd_uuid2str(&lov->lov_tgts[index]->ltd_uuid),
               obd_uuid2str(&oss->lqo_uuid),
               oss->lqo_ost_count);

out:
        mutex_up(&lov->lov_lock);
        up_write(&lov->lov_qos.lq_rw_sem);
        RETURN(rc);
}

int qos_del_tgt(struct obd_device *obd, struct lov_tgt_desc *tgt)
{
        struct lov_obd *lov = &obd->u.lov;
        struct lov_qos_oss *oss;
        int rc = 0;
        ENTRY;

        down_write(&lov->lov_qos.lq_rw_sem);

        oss = tgt->ltd_qos.ltq_oss;
        if (!oss)
                GOTO(out, rc = -ENOENT);

        oss->lqo_ost_count--;
        if (oss->lqo_ost_count == 0) {
                CDEBUG(D_QOS, "removing OSS %s\n",
                       obd_uuid2str(&oss->lqo_uuid));
                list_del(&oss->lqo_oss_list);
                OBD_FREE_PTR(oss);
        }

        lov->lov_qos.lq_dirty = 1;
        lov->lov_qos.lq_rr.lqr_dirty = 1;
out:
        up_write(&lov->lov_qos.lq_rw_sem);
        RETURN(rc);
}

/*
 * Update statfs data if the current osfs age is older than max_age.
 * If wait is not set, it means that we are called from lov_create()
 * and we should just issue the rpcs without waiting for them to complete.
 * If wait is set, we are called from alloc_qos() and we just have
 * to wait for the request set to complete.
 */
static void lod_qos_statfs_update(const struct lu_env *env,
                                   struct lod_device *m,
                                   __u64 max_age)
{
        struct obd_device *obd = m->mbd_obd;
        struct lov_obd    *lov = &obd->u.lov;
        struct ost_pool   *osts = &(lov->lov_packed);
        int                i, idx, rc = 0;
        struct kstatfs     sfs;
        ENTRY;

        if (cfs_time_beforeq_64(max_age, obd->obd_osfs_age))
                /* statfs data are quite recent, don't need to refresh it */
                RETURN_EXIT;

        down_write(&lov->lov_qos.lq_rw_sem);
        if (cfs_time_beforeq_64(max_age, obd->obd_osfs_age))
                GOTO(out, rc = 0);

        for (i = 0; i < osts->op_count; i++) {
                idx = osts->op_array[i];
                rc = dt_statfs(env, m->mbd_ost[i], &sfs);
                if (rc) {
                        /* XXX: disable this OST till next refresh? */
                        CERROR("can't refresh statfs: %d\n", rc);
                        break;
                }
                if (lov->lov_tgts[idx]->ltd_statfs.os_bavail != sfs.f_bavail) {
                        /* recalculate weigths */
                        lov->lov_qos.lq_dirty = 1;
                }
                statfs_pack(&lov->lov_tgts[idx]->ltd_statfs, &sfs);
        }

        obd->obd_osfs_age = cfs_time_current_64();

out:
        up_write(&lov->lov_qos.lq_rw_sem);
}

/* Recalculate per-object penalties for OSSs and OSTs,
   depends on size of each ost in an oss */
static int lod_qos_calc_ppo(struct lov_obd *lov)
{
        struct lov_qos_oss *oss;
        __u64 ba_max, ba_min, temp;
        __u32 num_active;
        int rc, i, prio_wide;
        time_t now, age;
        ENTRY;

        if (!lov->lov_qos.lq_dirty)
                GOTO(out, rc = 0);

        num_active = lov->desc.ld_active_tgt_count - 1;
        if (num_active < 1)
                GOTO(out, rc = -EAGAIN);

        /* find bavail on each OSS */
        list_for_each_entry(oss, &lov->lov_qos.lq_oss_list, lqo_oss_list) {
                oss->lqo_bavail = 0;
        }
        lov->lov_qos.lq_active_oss_count = 0;

        /* How badly user wants to select osts "widely" (not recently chosen
           and not on recent oss's).  As opposed to "freely" (free space
           avail.) 0-256. */
        prio_wide = 256 - lov->lov_qos.lq_prio_free;

        ba_min = (__u64)(-1);
        ba_max = 0;
        now = cfs_time_current_sec();
        /* Calculate OST penalty per object */
        /* (lov ref taken in alloc_qos) */
        for (i = 0; i < lov->desc.ld_tgt_count; i++) {
                if (!lov->lov_tgts[i] || !lov->lov_tgts[i]->ltd_active)
                        continue;
                temp = TGT_BAVAIL(i);
                if (!temp)
                        continue;
                ba_min = min(temp, ba_min);
                ba_max = max(temp, ba_max);

                /* Count the number of usable OSS's */
                if (lov->lov_tgts[i]->ltd_qos.ltq_oss->lqo_bavail == 0)
                        lov->lov_qos.lq_active_oss_count++;
                lov->lov_tgts[i]->ltd_qos.ltq_oss->lqo_bavail += temp;

                /* per-OST penalty is prio * TGT_bavail / (num_ost - 1) / 2 */
                temp >>= 1;
                do_div(temp, num_active);
                lov->lov_tgts[i]->ltd_qos.ltq_penalty_per_obj =
                        (temp * prio_wide) >> 8;

                age = (now - lov->lov_tgts[i]->ltd_qos.ltq_used) >> 3;
                if (lov->lov_qos.lq_reset || age > 32 * lov->desc.ld_qos_maxage)
                        lov->lov_tgts[i]->ltd_qos.ltq_penalty = 0;
                else if (age > lov->desc.ld_qos_maxage)
                        /* Decay the penalty by half for every 8x the update
                         * interval that the device has been idle.  That gives
                         * lots of time for the statfs information to be
                         * updated (which the penalty is only a proxy for),
                         * and avoids penalizing OSS/OSTs under light load. */
                        lov->lov_tgts[i]->ltd_qos.ltq_penalty >>=
                                (age / lov->desc.ld_qos_maxage);
        }

        num_active = lov->lov_qos.lq_active_oss_count - 1;
        if (num_active < 1) {
                /* If there's only 1 OSS, we can't penalize it, so instead
                   we have to double the OST penalty */
                num_active = 1;
                for (i = 0; i < lov->desc.ld_tgt_count; i++) {
                        if (lov->lov_tgts[i] == NULL)
                                continue;
                        lov->lov_tgts[i]->ltd_qos.ltq_penalty_per_obj <<= 1;
                }
        }

        /* Per-OSS penalty is prio * oss_avail / oss_osts / (num_oss - 1) / 2 */
        list_for_each_entry(oss, &lov->lov_qos.lq_oss_list, lqo_oss_list) {
                temp = oss->lqo_bavail >> 1;
                do_div(temp, oss->lqo_ost_count * num_active);
                oss->lqo_penalty_per_obj = (temp * prio_wide) >> 8;

                age = (now - oss->lqo_used) >> 3;
                if (lov->lov_qos.lq_reset || age > 32 * lov->desc.ld_qos_maxage)
                        oss->lqo_penalty = 0;
                else if (age > lov->desc.ld_qos_maxage)
                        /* Decay the penalty by half for every 8x the update
                         * interval that the device has been idle.  That gives
                         * lots of time for the statfs information to be
                         * updated (which the penalty is only a proxy for),
                         * and avoids penalizing OSS/OSTs under light load. */
                        oss->lqo_penalty >>= (age / lov->desc.ld_qos_maxage);
        }

        lov->lov_qos.lq_dirty = 0;
        lov->lov_qos.lq_reset = 0;

        /* If each ost has almost same free space,
         * do rr allocation for better creation performance */
        lov->lov_qos.lq_same_space = 0;
        if ((ba_max * (256 - lov->lov_qos.lq_threshold_rr)) >> 8 < ba_min) {
                lov->lov_qos.lq_same_space = 1;
                /* Reset weights for the next time we enter qos mode */
                lov->lov_qos.lq_reset = 1;
        }
        rc = 0;

out:
        if (!rc && lov->lov_qos.lq_same_space)
                RETURN(-EAGAIN);
        RETURN(rc);
}

static int lod_qos_calc_weight(struct lov_obd *lov, int i)
{
        __u64 temp, temp2;

        /* Final ost weight = TGT_BAVAIL - ost_penalty - oss_penalty */
        temp = TGT_BAVAIL(i);
        temp2 = lov->lov_tgts[i]->ltd_qos.ltq_penalty +
                lov->lov_tgts[i]->ltd_qos.ltq_oss->lqo_penalty;
        if (temp < temp2)
                lov->lov_tgts[i]->ltd_qos.ltq_weight = 0;
        else
                lov->lov_tgts[i]->ltd_qos.ltq_weight = temp - temp2;
        return 0;
}

/* We just used this index for a stripe; adjust everyone's weights */
static int lod_qos_used(struct lov_obd *lov, struct ost_pool *osts,
                         __u32 index, __u64 *total_wt)
{
        struct lov_qos_oss *oss;
        int j;
        ENTRY;

        /* Don't allocate from this stripe anymore, until the next alloc_qos */
        lov->lov_tgts[index]->ltd_qos.ltq_usable = 0;

        oss = lov->lov_tgts[index]->ltd_qos.ltq_oss;

        /* Decay old penalty by half (we're adding max penalty, and don't
           want it to run away.) */
        lov->lov_tgts[index]->ltd_qos.ltq_penalty >>= 1;
        oss->lqo_penalty >>= 1;

        /* mark the OSS and OST as recently used */
        lov->lov_tgts[index]->ltd_qos.ltq_used =
                oss->lqo_used = cfs_time_current_sec();

        /* Set max penalties for this OST and OSS */
        lov->lov_tgts[index]->ltd_qos.ltq_penalty +=
                lov->lov_tgts[index]->ltd_qos.ltq_penalty_per_obj *
                lov->desc.ld_active_tgt_count;
        oss->lqo_penalty += oss->lqo_penalty_per_obj *
                lov->lov_qos.lq_active_oss_count;

        /* Decrease all OSS penalties */
        list_for_each_entry(oss, &lov->lov_qos.lq_oss_list, lqo_oss_list) {
                if (oss->lqo_penalty < oss->lqo_penalty_per_obj)
                        oss->lqo_penalty = 0;
                else
                        oss->lqo_penalty -= oss->lqo_penalty_per_obj;
        }

        *total_wt = 0;
        /* Decrease all OST penalties */
        for (j = 0; j < osts->op_count; j++) {
                int i;

                i = osts->op_array[j];
                if (!lov->lov_tgts[i] || !lov->lov_tgts[i]->ltd_active)
                        continue;
                if (lov->lov_tgts[i]->ltd_qos.ltq_penalty <
                    lov->lov_tgts[i]->ltd_qos.ltq_penalty_per_obj)
                        lov->lov_tgts[i]->ltd_qos.ltq_penalty = 0;
                else
                        lov->lov_tgts[i]->ltd_qos.ltq_penalty -=
                        lov->lov_tgts[i]->ltd_qos.ltq_penalty_per_obj;

                lod_qos_calc_weight(lov, i);

                /* Recalc the total weight of usable osts */
                if (lov->lov_tgts[i]->ltd_qos.ltq_usable)
                        *total_wt += lov->lov_tgts[i]->ltd_qos.ltq_weight;

#ifdef QOS_DEBUG
                CDEBUG(D_QOS, "recalc tgt %d usable=%d avail="LPU64
                       " ostppo="LPU64" ostp="LPU64" ossppo="LPU64
                       " ossp="LPU64" wt="LPU64"\n",
                       i, lov->lov_tgts[i]->ltd_qos.ltq_usable,
                       TGT_BAVAIL(i) >> 10,
                       lov->lov_tgts[i]->ltd_qos.ltq_penalty_per_obj >> 10,
                       lov->lov_tgts[i]->ltd_qos.ltq_penalty >> 10,
                       lov->lov_tgts[i]->ltd_qos.ltq_oss->lqo_penalty_per_obj>>10,
                       lov->lov_tgts[i]->ltd_qos.ltq_oss->lqo_penalty >> 10,
                       lov->lov_tgts[i]->ltd_qos.ltq_weight >> 10);
#endif
        }

        RETURN(0);
}

#define LOV_QOS_EMPTY ((__u32)-1)
/* compute optimal round-robin order, based on OSTs per OSS */
static int lod_qos_calc_rr(struct lov_obd *lov, struct ost_pool *src_pool,
                            struct lov_qos_rr *lqr)
{
        struct lov_qos_oss *oss;
        unsigned placed, real_count;
        int i, rc;
        ENTRY;

        if (!lqr->lqr_dirty) {
                LASSERT(lqr->lqr_pool.op_size);
                RETURN(0);
        }

        /* Do actual allocation. */
        down_write(&lov->lov_qos.lq_rw_sem);

        /*
         * Check again. While we were sleeping on @lq_rw_sem something could
         * change.
         */
        if (!lqr->lqr_dirty) {
                LASSERT(lqr->lqr_pool.op_size);
                up_write(&lov->lov_qos.lq_rw_sem);
                RETURN(0);
        }

        real_count = src_pool->op_count;

        /* Zero the pool array */
        /* alloc_rr is holding a read lock on the pool, so nobody is adding/
           deleting from the pool. The lq_rw_sem insures that nobody else
           is reading. */
        lqr->lqr_pool.op_count = real_count;
        rc = lov_ost_pool_extend(&lqr->lqr_pool, real_count);
        if (rc) {
                up_write(&lov->lov_qos.lq_rw_sem);
                RETURN(rc);
        }
        for (i = 0; i < lqr->lqr_pool.op_count; i++)
                lqr->lqr_pool.op_array[i] = LOV_QOS_EMPTY;

        /* Place all the OSTs from 1 OSS at the same time. */
        placed = 0;
        list_for_each_entry(oss, &lov->lov_qos.lq_oss_list, lqo_oss_list) {
                int j = 0;
                for (i = 0; i < lqr->lqr_pool.op_count; i++) {
                        if (lov->lov_tgts[src_pool->op_array[i]] &&
                            (lov->lov_tgts[src_pool->op_array[i]]->ltd_qos.ltq_oss == oss)) {
                              /* Evenly space these OSTs across arrayspace */
                              int next = j * lqr->lqr_pool.op_count / oss->lqo_ost_count;
                              while (lqr->lqr_pool.op_array[next] !=
                                     LOV_QOS_EMPTY)
                                        next = (next + 1) % lqr->lqr_pool.op_count;
                              lqr->lqr_pool.op_array[next] = src_pool->op_array[i];
                              j++;
                              placed++;
                        }
                }
        }

        lqr->lqr_dirty = 0;
        up_write(&lov->lov_qos.lq_rw_sem);

        if (placed != real_count) {
                /* This should never happen */
                LCONSOLE_ERROR_MSG(0x14e, "Failed to place all OSTs in the "
                                   "round-robin list (%d of %d).\n",
                                   placed, real_count);
                for (i = 0; i < lqr->lqr_pool.op_count; i++) {
                        LCONSOLE(D_WARNING, "rr #%d ost idx=%d\n", i,
                                 lqr->lqr_pool.op_array[i]);
                }
                lqr->lqr_dirty = 1;
                RETURN(-EAGAIN);
        }

#ifdef QOS_DEBUG
        for (i = 0; i < lqr->lqr_pool.op_count; i++) {
                LCONSOLE(D_QOS, "rr #%d ost idx=%d\n", i,
                         lqr->lqr_pool.op_array[i]);
        }
#endif

        RETURN(0);
}


struct lov_request_set;
struct lov_request;

void lod_qos_shrink_lsm(struct lov_request_set *set)
{
        LBUG();
#if 0
        struct lov_stripe_md *lsm = set->set_oi->oi_md, *lsm_new;
        /* XXX LOV STACKING call into osc for sizes */
        unsigned oldsize, newsize;

        if (set->set_oti && set->set_cookies && set->set_cookie_sent) {
                struct llog_cookie *cookies;
                oldsize = lsm->lsm_stripe_count * sizeof(*cookies);
                newsize = set->set_count * sizeof(*cookies);

                cookies = set->set_cookies;
                oti_alloc_cookies(set->set_oti, set->set_count);
                if (set->set_oti->oti_logcookies) {
                        memcpy(set->set_oti->oti_logcookies, cookies, newsize);
                        OBD_FREE(cookies, oldsize);
                        set->set_cookies = set->set_oti->oti_logcookies;
                } else {
                        CWARN("'leaking' %d bytes\n", oldsize - newsize);
                }
        }

        CWARN("using fewer stripes for object "LPU64": old %u new %u\n",
              lsm->lsm_object_id, lsm->lsm_stripe_count, set->set_count);
        LASSERT(lsm->lsm_stripe_count >= set->set_count);

        newsize = lov_stripe_md_size(set->set_count);
        OBD_ALLOC(lsm_new, newsize);
        if (lsm_new != NULL) {
                int i;
                memcpy(lsm_new, lsm, sizeof(*lsm));
                for (i = 0; i < lsm->lsm_stripe_count; i++) {
                        if (i < set->set_count) {
                                lsm_new->lsm_oinfo[i] = lsm->lsm_oinfo[i];
                                continue;
                        }
                        OBD_SLAB_FREE(lsm->lsm_oinfo[i], lov_oinfo_slab,
                                      sizeof(struct lov_oinfo));
                }
                lsm_new->lsm_stripe_count = set->set_count;
                OBD_FREE(lsm, sizeof(struct lov_stripe_md) +
                         lsm->lsm_stripe_count * sizeof(struct lov_oinfo *));
                set->set_oi->oi_md = lsm_new;
        } else {
                CWARN("'leaking' few bytes\n");
        }
#endif
}

int qos_remedy_create(struct lov_request_set *set, struct lov_request *req)
{
        LBUG();
        return 0;
#if 0
        struct lov_stripe_md *lsm = set->set_oi->oi_md;
        struct lov_obd *lov = &set->set_exp->exp_obd->u.lov;
        unsigned ost_idx, ost_count = lov->desc.ld_tgt_count;
        int stripe, i, rc = -EIO;
        ENTRY;

        ost_idx = (req->rq_idx + lsm->lsm_stripe_count) % ost_count;
        for (i = 0; i < ost_count; i++, ost_idx = (ost_idx + 1) % ost_count) {
                if (!lov->lov_tgts[ost_idx] ||
                    !lov->lov_tgts[ost_idx]->ltd_active)
                        continue;
                /* check if objects has been created on this ost */
                for (stripe = 0; stripe < lsm->lsm_stripe_count; stripe++) {
                        /* we try send create to this ost but he is failed */
                        if (stripe == req->rq_stripe)
                                continue;
                        /* already have object at this stripe */
                        if (ost_idx == lsm->lsm_oinfo[stripe]->loi_ost_idx)
                                break;
                }
                if (stripe >= lsm->lsm_stripe_count) {
                        req->rq_idx = ost_idx;
                        rc = obd_create(lov->lov_tgts[ost_idx]->ltd_exp,
                                        req->rq_oi.oi_oa, &req->rq_oi.oi_md,
                                        set->set_oti);
                        if (!rc)
                                break;
                }
        }
        RETURN(rc);
#endif
}

static int min_stripe_count(int stripe_cnt, int flags)
{
        return (flags & LOV_USES_DEFAULT_STRIPE ?
                stripe_cnt - (stripe_cnt / 4) : stripe_cnt);
}

#define LOV_CREATE_RESEED_MULT 4
#define LOV_CREATE_RESEED_MIN  1000
/* Allocate objects on osts with round-robin algorithm */
static int lod_alloc_rr(const struct lu_env *env, struct lod_device *m,
                         int *idx_arr, int *stripe_cnt, char *poolname, int flags)
{
        struct lov_obd *lov = &m->mbd_obd->u.lov;
        unsigned array_idx;
        int i, rc, *idx_pos;
        __u32 ost_idx;
        int ost_start_idx_temp;
        int speed = 0;
        int stripe_cnt_min = min_stripe_count(*stripe_cnt, flags);
        struct pool_desc *pool;
        struct ost_pool *osts;
        struct lov_qos_rr *lqr;
        ENTRY;

        pool = lov_find_pool(lov, poolname);
        if (pool == NULL) {
                osts = &(lov->lov_packed);
                lqr = &(lov->lov_qos.lq_rr);
        } else {
                down_read(&pool_tgt_rw_sem(pool));
                osts = &(pool->pool_obds);
                lqr = &(pool->pool_rr);
        }

        rc = lod_qos_calc_rr(lov, osts, lqr);
        if (rc)
                GOTO(out, rc);

        if (--lqr->lqr_start_count <= 0) {
                lqr->lqr_start_idx = ll_rand() % osts->op_count;
                lqr->lqr_start_count =
                        (LOV_CREATE_RESEED_MIN / max(osts->op_count, 1U) +
                         LOV_CREATE_RESEED_MULT) * max(osts->op_count, 1U);
        } else if (stripe_cnt_min >= osts->op_count ||
                   lqr->lqr_start_idx > osts->op_count) {
                /* If we have allocated from all of the OSTs, slowly
                 * precess the next start if the OST/stripe count isn't
                 * already doing this for us. */
                lqr->lqr_start_idx %= osts->op_count;
                if (*stripe_cnt > 1 && (osts->op_count % (*stripe_cnt)) != 1)
                        ++lqr->lqr_offset_idx;
        }
        down_read(&lov->lov_qos.lq_rw_sem);
        ost_start_idx_temp = lqr->lqr_start_idx;

repeat_find:
        array_idx = (lqr->lqr_start_idx + lqr->lqr_offset_idx) % osts->op_count;
        idx_pos = idx_arr;
#ifdef QOS_DEBUG
        CDEBUG(D_QOS, "pool '%s' want %d startidx %d startcnt %d offset %d "
               "active %d count %d arrayidx %d\n", poolname,
               *stripe_cnt, lqr->lqr_start_idx, lqr->lqr_start_count,
               lqr->lqr_offset_idx, osts->op_count, osts->op_count, array_idx);
#endif

        for (i = 0; i < osts->op_count;
                    i++, array_idx=(array_idx + 1) % osts->op_count) {
                ++lqr->lqr_start_idx;
                ost_idx = lqr->lqr_pool.op_array[array_idx];
#ifdef QOS_DEBUG
                CDEBUG(D_QOS, "#%d strt %d act %d strp %d ary %d idx %d\n",
                       i, lqr->lqr_start_idx,
                       ((ost_idx != LOV_QOS_EMPTY) && lov->lov_tgts[ost_idx]) ?
                       lov->lov_tgts[ost_idx]->ltd_active : 0,
                       idx_pos - idx_arr, array_idx, ost_idx);
#endif
                if ((ost_idx == LOV_QOS_EMPTY) || !lov->lov_tgts[ost_idx] ||
                    !lov->lov_tgts[ost_idx]->ltd_active)
                        continue;

                /* Fail Check before osc_precreate() is called
                   so we can only 'fail' single OSC. */
                if (OBD_FAIL_CHECK(OBD_FAIL_MDS_OSC_PRECREATE) && ost_idx == 0)
                        continue;

                /*
                 * XXX: how to skip slow OST w/o obd_precreate() ?
                 */
                /* Drop slow OSCs if we can */
                /*if (obd_precreate(lov->lov_tgts[ost_idx]->ltd_exp) > speed)
                        continue;*/

                *idx_pos = ost_idx;
                idx_pos++;
                /* We have enough stripes */
                if (idx_pos - idx_arr == *stripe_cnt)
                        break;
        }
        if ((speed < 2) && (idx_pos - idx_arr < stripe_cnt_min)) {
                /* Try again, allowing slower OSCs */
                speed++;
                lqr->lqr_start_idx = ost_start_idx_temp;
                goto repeat_find;
        }

        up_read(&lov->lov_qos.lq_rw_sem);

        *stripe_cnt = idx_pos - idx_arr;
out:
        if (pool != NULL) {
                up_read(&pool_tgt_rw_sem(pool));
                /* put back ref got by lov_find_pool() */
                lov_pool_putref(pool);
        }

        RETURN(rc);
}

#if 0
/*
 * XXX: not implemented yet!
 */
/* alloc objects on osts with specific stripe offset */
static int lod_alloc_specific(const struct lu_env *env, struct lov_obd *lov,
                               struct lov_stripe_md *lsm, int *idx_arr)
{
        unsigned ost_idx, array_idx, ost_count;
        int i, rc, *idx_pos;
        int speed = 0;
        struct pool_desc *pool;
        struct ost_pool *osts;
        ENTRY;

        pool = lov_find_pool(lov, lsm->lsm_pool_name);
        if (pool == NULL) {
                osts = &(lov->lov_packed);
        } else {
                down_read(&pool_tgt_rw_sem(pool));
                osts = &(pool->pool_obds);
        }

        ost_count = osts->op_count;

repeat_find:
        /* search loi_ost_idx in ost array */
        array_idx = 0;
        for (i = 0; i < ost_count; i++) {
                if (osts->op_array[i] == lsm->lsm_oinfo[0]->loi_ost_idx) {
                        array_idx = i;
                        break;
                }
        }
        if (i == ost_count) {
                CERROR("Start index %d not found in pool '%s'\n",
                       lsm->lsm_oinfo[0]->loi_ost_idx, lsm->lsm_pool_name);
                GOTO(out, rc = -EINVAL);
        }

        idx_pos = idx_arr;
        for (i = 0; i < ost_count;
             i++, array_idx = (array_idx + 1) % ost_count) {
                ost_idx = osts->op_array[array_idx];

                if (!lov->lov_tgts[ost_idx] ||
                    !lov->lov_tgts[ost_idx]->ltd_active) {
                        continue;
                }

                /* Fail Check before osc_precreate() is called
                   so we can only 'fail' single OSC. */
                if (OBD_FAIL_CHECK(OBD_FAIL_MDS_OSC_PRECREATE) && ost_idx == 0)
                        continue;

                /* Drop slow OSCs if we can, but not for requested start idx.
                 *
                 * This means "if OSC is slow and it is not the requested
                 * start OST, then it can be skipped, otherwise skip it only
                 * if it is inactive/recovering/out-of-space." */
                if ((obd_precreate(lov->lov_tgts[ost_idx]->ltd_exp) > speed) &&
                    (i != 0 || speed >= 2))
                        continue;

                *idx_pos = ost_idx;
                idx_pos++;
                /* We have enough stripes */
                if (idx_pos - idx_arr == lsm->lsm_stripe_count)
                        GOTO(out, rc = 0);
        }
        if (speed < 2) {
                /* Try again, allowing slower OSCs */
                speed++;
                goto repeat_find;
        }

        /* If we were passed specific striping params, then a failure to
         * meet those requirements is an error, since we can't reallocate
         * that memory (it might be part of a larger array or something).
         *
         * We can only get here if lsm_stripe_count was originally > 1.
         */
        CERROR("can't lstripe objid "LPX64": have %d want %u\n",
               lsm->lsm_object_id, (int)(idx_pos - idx_arr),
               lsm->lsm_stripe_count);
        rc = -EFBIG;
out:
        if (pool != NULL) {
                up_read(&pool_tgt_rw_sem(pool));
                /* put back ref got by lov_find_pool() */
                lov_pool_putref(pool);
        }

        RETURN(rc);
}
#endif

/* Alloc objects on osts with optimization based on:
   - free space
   - network resources (shared OSS's)
*/
static int lod_alloc_qos(const struct lu_env *env, struct lod_device *m,
                          int *idx_arr, int *stripe_cnt, char *poolname, int flags)
{
        struct lov_obd *lov = &m->mbd_obd->u.lov;
        __u64 total_weight = 0;
        int nfound, good_osts, i, rc = 0;
        int stripe_cnt_min = min_stripe_count(*stripe_cnt, flags);
        struct pool_desc *pool;
        struct ost_pool *osts;
        struct lov_qos_rr *lqr;
        ENTRY;

        if (stripe_cnt_min < 1)
                RETURN(-EINVAL);

        pool = lov_find_pool(lov, poolname);
        if (pool == NULL) {
                osts = &(lov->lov_packed);
                lqr = &(lov->lov_qos.lq_rr);
        } else {
                down_read(&pool_tgt_rw_sem(pool));
                osts = &(pool->pool_obds);
                lqr = &(pool->pool_rr);
        }

        /* XXXX: obd_getref(exp->exp_obd);*/

        lod_qos_statfs_update(env, m, cfs_time_shift_64(-2*lov->desc.ld_qos_maxage));

        /* Detect -EAGAIN early, before expensive lock is taken. */
        if (!lov->lov_qos.lq_dirty && lov->lov_qos.lq_same_space)
                GOTO(out_nolock, rc = -EAGAIN);

        /* Do actual allocation, use write lock here. */
        down_write(&lov->lov_qos.lq_rw_sem);

        /*
         * Check again, while we were sleeping on @lq_rw_sem things could
         * change.
         */
        if (!lov->lov_qos.lq_dirty && lov->lov_qos.lq_same_space)
                GOTO(out, rc = -EAGAIN);

        if (lov->desc.ld_active_tgt_count < 2)
                GOTO(out, rc = -EAGAIN);

        rc = lod_qos_calc_ppo(lov);
        if (rc)
                GOTO(out, rc);

        good_osts = 0;
        /* Find all the OSTs that are valid stripe candidates */
        for (i = 0; i < osts->op_count; i++) {
                if (!lov->lov_tgts[osts->op_array[i]] ||
                    !lov->lov_tgts[osts->op_array[i]]->ltd_active)
                        continue;

                /* Fail Check before osc_precreate() is called
                   so we can only 'fail' single OSC. */
                if (OBD_FAIL_CHECK(OBD_FAIL_MDS_OSC_PRECREATE) && osts->op_array[i] == 0)
                        continue;

                /*
                 * XXX: how to ignore slow OSTs
                 */
                /*if (obd_precreate(lov->lov_tgts[osts->op_array[i]]->ltd_exp) > 2)
                        continue;*/

                lov->lov_tgts[osts->op_array[i]]->ltd_qos.ltq_usable = 1;
                lod_qos_calc_weight(lov, osts->op_array[i]);
                total_weight += lov->lov_tgts[osts->op_array[i]]->ltd_qos.ltq_weight;

                good_osts++;
        }

#ifdef QOS_DEBUG
        CDEBUG(D_QOS, "found %d good osts\n", good_osts);
#endif

        if (good_osts < stripe_cnt_min)
                GOTO(out, rc = -EAGAIN);

        /* We have enough osts */
        if (good_osts < *stripe_cnt)
                *stripe_cnt = good_osts;

        if (!*stripe_cnt)
                GOTO(out, rc = -EAGAIN);

        /* Find enough OSTs with weighted random allocation. */
        nfound = 0;
        while (nfound < *stripe_cnt) {
                __u64 rand, cur_weight;

                cur_weight = 0;
                rc = -ENODEV;

                if (total_weight) {
#if BITS_PER_LONG == 32
                        rand = ll_rand() % (unsigned)total_weight;
                        /* If total_weight > 32-bit, first generate the high
                         * 32 bits of the random number, then add in the low
                         * 32 bits (truncated to the upper limit, if needed) */
                        if (total_weight > 0xffffffffULL)
                                rand = (__u64)(ll_rand() %
                                          (unsigned)(total_weight >> 32)) << 32;
                        else
                                rand = 0;

                        if (rand == (total_weight & 0xffffffff00000000ULL))
                                rand |= ll_rand() % (unsigned)total_weight;
                        else
                                rand |= ll_rand();

#else
                        rand = ((__u64)ll_rand() << 32 | ll_rand()) %
                                total_weight;
#endif
                } else {
                        rand = 0;
                }

                /* On average, this will hit larger-weighted osts more often.
                   0-weight osts will always get used last (only when rand=0).*/
                for (i = 0; i < osts->op_count; i++) {
                        if (!lov->lov_tgts[osts->op_array[i]] ||
                            !lov->lov_tgts[osts->op_array[i]]->ltd_qos.ltq_usable)
                                continue;

                        cur_weight += lov->lov_tgts[osts->op_array[i]]->ltd_qos.ltq_weight;
#ifdef QOS_DEBUG
                        CDEBUG(D_QOS, "stripe_cnt=%d nfound=%d cur_weight="LPU64
                                      " rand="LPU64" total_weight="LPU64"\n",
                               *stripe_cnt, nfound, cur_weight, rand, total_weight);
#endif
                        if (cur_weight >= rand) {
#ifdef QOS_DEBUG
                                CDEBUG(D_QOS, "assigned stripe=%d to idx=%d\n",
                                       nfound, osts->op_array[i]);
#endif
                                idx_arr[nfound++] = osts->op_array[i];
                                lod_qos_used(lov, osts, osts->op_array[i],
                                              &total_weight);
                                rc = 0;
                                break;
                        }
                }
                /* should never satisfy below condition */
                if (rc) {
                        CERROR("Didn't find any OSTs?\n");
                        break;
                }
        }
        LASSERT(nfound == *stripe_cnt);

out:
        up_write(&lov->lov_qos.lq_rw_sem);

out_nolock:
        if (pool != NULL) {
                up_read(&pool_tgt_rw_sem(pool));
                /* put back ref got by lov_find_pool() */
                lov_pool_putref(pool);
        }

        if (rc == -EAGAIN)
                rc = lod_alloc_rr(env, m, idx_arr, stripe_cnt, poolname, flags);

        /* XXX: obd_putref(exp->exp_obd); */
        RETURN(rc);
}

/* return new alloced stripe count on success */
static int lod_alloc_idx_array(const struct lu_env *env, struct lod_device *m,
                                int stripe_cnt, char *poolname, int flags)
{
        struct lod_thread_info *mti = lod_mti_get(env);
        int i, rc = 0;
        int *tmp_arr;
        ENTRY;

        LASSERT(mti);
        LASSERT(stripe_cnt < LOD_MAX_OSTNR);

        tmp_arr = mti->ost_arr;
        for (i = 0; i < stripe_cnt; i++)
                tmp_arr[i] = -1;

        /* XXX: support for allocation on specified OSTs
         *      rc = alloc_specific(lov, lsm, tmp_arr); */
        rc = lod_alloc_qos(env, m, tmp_arr, &stripe_cnt, poolname, flags);

        RETURN(rc);
}

int lod_qos_prep_create(const struct lu_env *env, struct lod_device *m,
                         int stripes)
{
        struct lod_thread_info *mti = lod_mti_get(env);
        int flag = LOV_USES_ASSIGNED_STRIPE;
        int i, rc = 0;
        int *idx_arr;
        ENTRY;

        /* XXX: support for non-0 files w/o objects */
        /* XXX: support for pools */
        i = lod_alloc_idx_array(env, m, stripes, NULL, flag);
        if (i <= 0)
                GOTO(out_err, rc = i ? i : -EIO);
        LASSERTF(i <= stripes, "requested %d allocated %d\n", i, stripes);

        idx_arr = mti->ost_arr;

        /* XXX ? */
        /*if (OBD_FAIL_CHECK(OBD_FAIL_MDS_LOV_PREP_CREATE)) {
                lod_qos_shrink_lsm(set);
                rc = -EIO;
        }*/

out_err:
        EXIT;
        return rc;
}

