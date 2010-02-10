/* -*- mode: c; c-basic-offset: 8; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright  2008 Sun Microsystems, Inc. All rights reserved
 *
 *   This file is part of Portals
 *   http://sourceforge.net/projects/sandiaportals/
 *
 *   Portals is free software; you can redistribute it and/or
 *   modify it under the terms of version 2 of the GNU General Public
 *   License as published by the Free Software Foundation.
 *
 *   Portals is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Portals; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#define DEBUG_SUBSYSTEM S_LNET
#include <libcfs/libcfs.h>
#include <lnet/lib-lnet.h>
#include <libcfs/params_tree.h>

#if defined(__KERNEL__) && defined(LNET_ROUTER)

#ifdef CONFIG_PROC_FS
#if defined(__linux__)
#include <linux/seq_file.h>
#endif

/* this is really lnet_proc.c */
#define LNET_PROC_ROOT    "sys/lnet"
#define LNET_PROC_STATS   LNET_PROC_ROOT"/stats"
#define LNET_PROC_ROUTES  LNET_PROC_ROOT"/routes"
#define LNET_PROC_ROUTERS LNET_PROC_ROOT"/routers"
#define LNET_PROC_PEERS   LNET_PROC_ROOT"/peers"
#define LNET_PROC_BUFFERS LNET_PROC_ROOT"/buffers"
#define LNET_PROC_NIS     LNET_PROC_ROOT"/nis"
#endif

#define LNET_PARAM_ROOT    "lnet"
#define LNET_PARAM_STATS   "stats"
#define LNET_PARAM_ROUTES  "routes"
#define LNET_PARAM_ROUTERS "routers"
#define LNET_PARAM_PEERS   "peers"
#define LNET_PARAM_BUFFERS "buffers"
#define LNET_PARAM_NIS     "nis"

static int
lnet_router_proc_stats_read (char *page, char **start, off_t off,
                             int count, int *eof, void *data)
{
        lnet_counters_t *ctrs;
        int              rc;

        *eof = 1;
        if (off != 0)
                return 0;

        LIBCFS_ALLOC(ctrs, sizeof(*ctrs));
        if (ctrs == NULL)
                return -ENOMEM;

        LNET_LOCK();
        *ctrs = the_lnet.ln_counters;
        LNET_UNLOCK();

        rc = libcfs_snprintf(page, count,
                      "%u %u %u %u %u %u %u "LPU64" "LPU64" "LPU64" "LPU64"\n",
                      ctrs->msgs_alloc, ctrs->msgs_max,
                      ctrs->errors,
                      ctrs->send_count, ctrs->recv_count,
                      ctrs->route_count, ctrs->drop_count,
                      ctrs->send_length, ctrs->recv_length,
                      ctrs->route_length, ctrs->drop_length);
        if (data != NULL)
                rc = libcfs_param_snprintf(page, count, data, LP_STR, NULL, NULL);

        LIBCFS_FREE(ctrs, sizeof(*ctrs));
        return rc;
}

static int
lnet_router_proc_stats_write(libcfs_file_t * file, const char *ubuffer,
                             unsigned long count, void *data)
{
        LNET_LOCK();
        memset(&the_lnet.ln_counters, 0, sizeof(the_lnet.ln_counters));
        LNET_UNLOCK();

        return (count);
}

typedef struct {
        __u64                lrsi_version;
        lnet_remotenet_t    *lrsi_net;
        lnet_route_t        *lrsi_route;
        loff_t               lrsi_off;
} lnet_route_seq_iterator_t;

int
lnet_route_seq_seek (lnet_route_seq_iterator_t *lrsi, loff_t off)
{
        cfs_list_t  *n;
        cfs_list_t  *r;
        int          rc;
        loff_t       here;

        if (off == 0) {
                lrsi->lrsi_net = NULL;
                lrsi->lrsi_route = NULL;
                lrsi->lrsi_off = 0;
                return 0;
        }

        LNET_LOCK();

        if (lrsi->lrsi_net != NULL &&
            lrsi->lrsi_version != the_lnet.ln_remote_nets_version) {
                /* tables have changed */
                rc = -ESTALE;
                goto out;
        }

        if (lrsi->lrsi_net == NULL || lrsi->lrsi_off > off) {
                /* search from start */
                n = the_lnet.ln_remote_nets.next;
                r = NULL;
                here = 1;
        } else {
                /* continue search */
                n = &lrsi->lrsi_net->lrn_list;
                r = &lrsi->lrsi_route->lr_list;
                here = lrsi->lrsi_off;
        }

        lrsi->lrsi_version = the_lnet.ln_remote_nets_version;
        lrsi->lrsi_off        = off;

        while (n != &the_lnet.ln_remote_nets) {
                lnet_remotenet_t *rnet =
                        cfs_list_entry(n, lnet_remotenet_t, lrn_list);

                if (r == NULL)
                        r = rnet->lrn_routes.next;

                while (r != &rnet->lrn_routes) {
                        lnet_route_t *re =
                                cfs_list_entry(r, lnet_route_t,
                                           lr_list);

                        if (here == off) {
                                lrsi->lrsi_net = rnet;
                                lrsi->lrsi_route = re;
                                rc = 0;
                                goto out;
                        }

                        r = r->next;
                        here++;
                }

                r = NULL;
                n = n->next;
        }

        lrsi->lrsi_net   = NULL;
        lrsi->lrsi_route = NULL;
        rc             = -ENOENT;
 out:
        LNET_UNLOCK();
        return rc;
}

static void *
lnet_route_seq_start (libcfs_seq_file_t *s, loff_t *pos)
{
        lnet_route_seq_iterator_t *lrsi;
        int                        rc;

        LIBCFS_ALLOC(lrsi, sizeof(*lrsi));
        if (lrsi == NULL)
                return NULL;

        lrsi->lrsi_net = NULL;
        rc = lnet_route_seq_seek(lrsi, *pos);
        if (rc == 0)
                return lrsi;

        LIBCFS_FREE(lrsi, sizeof(*lrsi));
        return NULL;
}

static void
lnet_route_seq_stop (libcfs_seq_file_t *s, void *iter)
{
        lnet_route_seq_iterator_t  *lrsi = iter;

        if (lrsi != NULL)
                LIBCFS_FREE(lrsi, sizeof(*lrsi));
}

static void *
lnet_route_seq_next (libcfs_seq_file_t *s, void *iter, loff_t *pos)
{
        lnet_route_seq_iterator_t *lrsi = iter;
        int                        rc;
        loff_t                     next = *pos + 1;

        rc = lnet_route_seq_seek(lrsi, next);
        if (rc != 0) {
                LIBCFS_FREE(lrsi, sizeof(*lrsi));
                return NULL;
        }

        *pos = next;
        return lrsi;
}

static int
lnet_route_seq_show (libcfs_seq_file_t *s, void *iter)
{
        lnet_route_seq_iterator_t *lrsi = iter;
        __u32                      net;
        unsigned int               hops;
        lnet_nid_t                 nid;
        int                        alive;

        if (lrsi->lrsi_off == 0) {
                LIBCFS_SEQ_PRINTF(s, "Routing %s\n",
                                  the_lnet.ln_routing ? "enabled" : "disabled");
                LIBCFS_SEQ_PRINTF(s, "%-8s %4s %7s %s\n",
                                  "net", "hops", "state", "router");
                return 0;
        }

        LASSERT (lrsi->lrsi_net != NULL);
        LASSERT (lrsi->lrsi_route != NULL);

        LNET_LOCK();

        if (lrsi->lrsi_version != the_lnet.ln_remote_nets_version) {
                LNET_UNLOCK();
                return -ESTALE;
        }

        net   = lrsi->lrsi_net->lrn_net;
        hops  = lrsi->lrsi_route->lr_hops;
        nid   = lrsi->lrsi_route->lr_gateway->lp_nid;
        alive = lrsi->lrsi_route->lr_gateway->lp_alive;

        LNET_UNLOCK();

        LIBCFS_SEQ_PRINTF(s, "%-8s %4u %7s %s\n", libcfs_net2str(net), hops,
                          alive ? "up" : "down", libcfs_nid2str(nid));
        return 0;
}

static libcfs_seq_ops_t lnet_routes_sops = {
        /* start */ lnet_route_seq_start,
        /* stop */  lnet_route_seq_stop,
        /* next */  lnet_route_seq_next,
        /* show */  lnet_route_seq_show,
};

static int
lnet_route_seq_open(libcfs_inode_t *inode, libcfs_file_t *file)
{
        libcfs_param_dentry_t *dp = LIBCFS_PDE(inode);
        libcfs_seq_file_t     *sf;
        int                    rc;

        LPROCFS_ENTRY_AND_CHECK(dp);
        LIBCFS_SEQ_OPEN(file, &lnet_routes_sops, rc);
        if (rc) {
                LPROCFS_EXIT();
                return rc;
        }
        sf = LIBCFS_FILE_PRIVATE(file);
        LIBCFS_SEQ_PRIVATE(sf) = LIBCFS_DENTRY_DATA(dp);

        return 0;
}

static libcfs_file_ops_t lnet_routes_fops = {
        .owner   =  THIS_MODULE,
        .llseek  =  LIBCFS_SEQ_LSEEK_COMMON,
        .read    =  LIBCFS_SEQ_READ_COMMON,
        .open    =  lnet_route_seq_open,
        .release =  libcfs_param_seq_release_common,
};

typedef struct {
        __u64                lrtrsi_version;
        lnet_peer_t         *lrtrsi_router;
        loff_t               lrtrsi_off;
} lnet_router_seq_iterator_t;

int
lnet_router_seq_seek (lnet_router_seq_iterator_t *lrtrsi, loff_t off)
{
        cfs_list_t        *r;
        lnet_peer_t       *lp;
        int                rc;
        loff_t             here;

        if (off == 0) {
                lrtrsi->lrtrsi_router = NULL;
                lrtrsi->lrtrsi_off = 0;
                return 0;
        }

        LNET_LOCK();

        lp = lrtrsi->lrtrsi_router;

        if (lp != NULL &&
            lrtrsi->lrtrsi_version != the_lnet.ln_routers_version) {
                /* tables have changed */
                rc = -ESTALE;
                goto out;
        }

        if (lp == NULL || lrtrsi->lrtrsi_off > off) {
                /* search from start */
                r = the_lnet.ln_routers.next;
                here = 1;
        } else {
                /* continue search */
                r = &lp->lp_rtr_list;
                here = lrtrsi->lrtrsi_off;
        }

        lrtrsi->lrtrsi_version = the_lnet.ln_routers_version;
        lrtrsi->lrtrsi_off     = off;

        while (r != &the_lnet.ln_routers) {
                lnet_peer_t *rtr = cfs_list_entry(r, lnet_peer_t, lp_rtr_list);

                if (here == off) {
                        lrtrsi->lrtrsi_router = rtr;
                        rc = 0;
                        goto out;
                }

                r = r->next;
                here++;
        }

        lrtrsi->lrtrsi_router = NULL;
        rc = -ENOENT;
 out:
        LNET_UNLOCK();
        return rc;
}

static void *
lnet_router_seq_start (libcfs_seq_file_t *s, loff_t *pos)
{
        lnet_router_seq_iterator_t *lrtrsi;
        int                        rc;

        LIBCFS_ALLOC(lrtrsi, sizeof(*lrtrsi));
        if (lrtrsi == NULL)
                return NULL;

        lrtrsi->lrtrsi_router = NULL;
        rc = lnet_router_seq_seek(lrtrsi, *pos);
        if (rc == 0)
                return lrtrsi;

        LIBCFS_FREE(lrtrsi, sizeof(*lrtrsi));
        return NULL;
}

static void
lnet_router_seq_stop (libcfs_seq_file_t *s, void *iter)
{
        lnet_router_seq_iterator_t  *lrtrsi = iter;

        if (lrtrsi != NULL)
                LIBCFS_FREE(lrtrsi, sizeof(*lrtrsi));
}

static void *
lnet_router_seq_next (libcfs_seq_file_t *s, void *iter, loff_t *pos)
{
        lnet_router_seq_iterator_t *lrtrsi = iter;
        int                        rc;
        loff_t                     next = *pos + 1;

        rc = lnet_router_seq_seek(lrtrsi, next);
        if (rc != 0) {
                LIBCFS_FREE(lrtrsi, sizeof(*lrtrsi));
                return NULL;
        }

        *pos = next;
        return lrtrsi;
}

static int
lnet_router_seq_show (libcfs_seq_file_t *s, void *iter)
{
        lnet_router_seq_iterator_t *lrtrsi = iter;
        lnet_peer_t *lp;
        lnet_nid_t   nid;
        cfs_time_t   now;
        cfs_time_t   deadline;
        int          alive;
        int          alive_cnt;
        int          nrefs;
        int          nrtrrefs;
        int          pingsent;
        int          last_ping;
        int          down_ni;

        if (lrtrsi->lrtrsi_off == 0) {
                LIBCFS_SEQ_PRINTF(s, "%-4s %7s %9s %6s %12s %9s %8s %7s %s\n",
                                  "ref", "rtr_ref", "alive_cnt", "state", "last_ping",
                                  "ping_sent", "deadline", "down_ni", "router");
                return 0;
        }

        lp = lrtrsi->lrtrsi_router;
        LASSERT (lp != NULL);

        LNET_LOCK();

        if (lrtrsi->lrtrsi_version != the_lnet.ln_routers_version) {
                LNET_UNLOCK();
                return -ESTALE;
        }

        now       = cfs_time_current();
        deadline  = lp->lp_ping_deadline;
        nid       = lp->lp_nid;
        alive     = lp->lp_alive;
        alive_cnt = lp->lp_alive_count;
        nrefs     = lp->lp_refcount;
        nrtrrefs  = lp->lp_rtr_refcount;
        pingsent  = !lp->lp_ping_notsent;
        last_ping = cfs_duration_sec(cfs_time_sub(now, lp->lp_ping_timestamp));
        down_ni   = lnet_router_down_ni(lp, LNET_NIDNET(LNET_NID_ANY));

        if (deadline == 0)
                LIBCFS_SEQ_PRINTF(s, "%-4d %7d %9d %6s %12d %9d %8s %7d %s\n",
                                  nrefs, nrtrrefs, alive_cnt,
                                  alive ? "up" : "down", last_ping, pingsent,
                                  "NA", down_ni, libcfs_nid2str(nid));
        else
                LIBCFS_SEQ_PRINTF(s, "%-4d %7d %9d %6s %12d %9d %8lu %7d %s\n",
                                  nrefs, nrtrrefs, alive_cnt,
                                  alive ? "up" : "down", last_ping, pingsent,
                                  cfs_duration_sec(cfs_time_sub(deadline, now)),
                                  down_ni, libcfs_nid2str(nid));

        LNET_UNLOCK();

        return 0;
}

static libcfs_seq_ops_t lnet_routers_sops = {
        /* start */ lnet_router_seq_start,
        /* stop */  lnet_router_seq_stop,
        /* next */  lnet_router_seq_next,
        /* show */  lnet_router_seq_show,
};

static int
lnet_router_seq_open(libcfs_inode_t *inode, libcfs_file_t *file)
{
        libcfs_param_dentry_t *dp = LIBCFS_PDE(inode);
        libcfs_seq_file_t     *sf;
        int                    rc;

        LPROCFS_ENTRY_AND_CHECK(dp);
        LIBCFS_SEQ_OPEN(file, &lnet_routers_sops, rc);
        if (rc) {
                LPROCFS_EXIT();
                return rc;
        }
        sf = LIBCFS_FILE_PRIVATE(file);
        LIBCFS_SEQ_PRIVATE(sf) = LIBCFS_DENTRY_DATA(dp);

        return 0;
}

static libcfs_file_ops_t lnet_routers_fops = {
        .owner   =  THIS_MODULE,
        .llseek  =  LIBCFS_SEQ_LSEEK_COMMON,
        .read    =  LIBCFS_SEQ_READ_COMMON,
        .open    =  lnet_router_seq_open,
        .release =  libcfs_param_seq_release_common,
};

typedef struct {
        unsigned long long   lpsi_version;
        int                  lpsi_idx;
        lnet_peer_t         *lpsi_peer;
        loff_t               lpsi_off;
} lnet_peer_seq_iterator_t;

int
lnet_peer_seq_seek (lnet_peer_seq_iterator_t *lpsi, loff_t off)
{
        int                idx;
        cfs_list_t        *p;
        loff_t             here;
        int                rc;

        if (off == 0) {
                lpsi->lpsi_idx = 0;
                lpsi->lpsi_peer = NULL;
                lpsi->lpsi_off = 0;
                return 0;
        }

        LNET_LOCK();

        if (lpsi->lpsi_peer != NULL &&
            lpsi->lpsi_version != the_lnet.ln_peertable_version) {
                /* tables have changed */
                rc = -ESTALE;
                goto out;
        }

        if (lpsi->lpsi_peer == NULL ||
            lpsi->lpsi_off > off) {
                /* search from start */
                idx = 0;
                p = NULL;
                here = 1;
        } else {
                /* continue search */
                idx = lpsi->lpsi_idx;
                p = &lpsi->lpsi_peer->lp_hashlist;
                here = lpsi->lpsi_off;
        }

        lpsi->lpsi_version = the_lnet.ln_peertable_version;
        lpsi->lpsi_off     = off;

        while (idx < LNET_PEER_HASHSIZE) {
                if (p == NULL)
                        p = the_lnet.ln_peer_hash[idx].next;

                while (p != &the_lnet.ln_peer_hash[idx]) {
                        lnet_peer_t *lp = cfs_list_entry(p, lnet_peer_t,
                                                         lp_hashlist);

                        if (here == off) {
                                lpsi->lpsi_idx = idx;
                                lpsi->lpsi_peer = lp;
                                rc = 0;
                                goto out;
                        }

                        here++;
                        p = lp->lp_hashlist.next;
                }

                p = NULL;
                idx++;
        }

        lpsi->lpsi_idx  = 0;
        lpsi->lpsi_peer = NULL;
        rc              = -ENOENT;
 out:
        LNET_UNLOCK();
        return rc;
}

static void *
lnet_peer_seq_start (libcfs_seq_file_t *s, loff_t *pos)
{
        lnet_peer_seq_iterator_t *lpsi;
        int                        rc;

        LIBCFS_ALLOC(lpsi, sizeof(*lpsi));
        if (lpsi == NULL)
                return NULL;

        lpsi->lpsi_idx = 0;
        lpsi->lpsi_peer = NULL;
        rc = lnet_peer_seq_seek(lpsi, *pos);
        if (rc == 0)
                return lpsi;

        LIBCFS_FREE(lpsi, sizeof(*lpsi));
        return NULL;
}

static void
lnet_peer_seq_stop (libcfs_seq_file_t *s, void *iter)
{
        lnet_peer_seq_iterator_t  *lpsi = iter;

        if (lpsi != NULL)
                LIBCFS_FREE(lpsi, sizeof(*lpsi));
}

static void *
lnet_peer_seq_next (libcfs_seq_file_t *s, void *iter, loff_t *pos)
{
        lnet_peer_seq_iterator_t *lpsi = iter;
        int                       rc;
        loff_t                    next = *pos + 1;

        rc = lnet_peer_seq_seek(lpsi, next);
        if (rc != 0) {
                LIBCFS_FREE(lpsi, sizeof(*lpsi));
                return NULL;
        }

        *pos = next;
        return lpsi;
}

static int
lnet_peer_seq_show (libcfs_seq_file_t *s, void *iter)
{
        lnet_peer_seq_iterator_t *lpsi = iter;
        char                     *aliveness = "NA";
        lnet_peer_t              *lp;
        lnet_nid_t                nid;
        int                       maxcr;
        int                       mintxcr;
        int                       txcr;
        int                       minrtrcr;
        int                       rtrcr;
        int                       txqnob;
        int                       nrefs;

        if (lpsi->lpsi_off == 0) {
                LIBCFS_SEQ_PRINTF(s, "%-24s %4s %5s %5s %5s %5s %5s %5s %s\n",
                                  "nid", "refs", "state", "max",
                                  "rtr", "min", "tx", "min", "queue");
                return 0;
        }

        LASSERT (lpsi->lpsi_peer != NULL);

        LNET_LOCK();

        if (lpsi->lpsi_version != the_lnet.ln_peertable_version) {
                LNET_UNLOCK();
                return -ESTALE;
        }

        lp = lpsi->lpsi_peer;

        nid      = lp->lp_nid;
        maxcr    = lp->lp_ni->ni_peertxcredits;
        txcr     = lp->lp_txcredits;
        mintxcr  = lp->lp_mintxcredits;
        rtrcr    = lp->lp_rtrcredits;
        minrtrcr = lp->lp_minrtrcredits;
        txqnob   = lp->lp_txqnob;
        nrefs    = lp->lp_refcount;

        if (lnet_isrouter(lp) || lnet_peer_aliveness_enabled(lp))
                aliveness = lp->lp_alive ? "up" : "down";

        LNET_UNLOCK();

        LIBCFS_SEQ_PRINTF(s, "%-24s %4d %5s %5d %5d %5d %5d %5d %d\n",
                          libcfs_nid2str(nid), nrefs, aliveness,
                          maxcr, rtrcr, minrtrcr, txcr, mintxcr, txqnob);
        return 0;
}

static libcfs_seq_ops_t lnet_peer_sops = {
        /* start */ lnet_peer_seq_start,
        /* stop */  lnet_peer_seq_stop,
        /* next */  lnet_peer_seq_next,
        /* show */  lnet_peer_seq_show,
};

static int
lnet_peer_seq_open(libcfs_inode_t *inode, libcfs_file_t *file)
{
        libcfs_param_dentry_t *dp = LIBCFS_PDE(inode);
        libcfs_seq_file_t     *sf;
        int                    rc;

        LPROCFS_ENTRY_AND_CHECK(dp);
        LIBCFS_SEQ_OPEN(file, &lnet_peer_sops, rc);
        if (rc) {
                LPROCFS_EXIT();
                return rc;
        }
        sf = LIBCFS_FILE_PRIVATE(file);
        LIBCFS_SEQ_PRIVATE(sf) = LIBCFS_DENTRY_DATA(dp);

        return 0;
}

static libcfs_file_ops_t lnet_peer_fops = {
        .owner   =  THIS_MODULE,
        .llseek  =  LIBCFS_SEQ_LSEEK_COMMON,
        .read    =  LIBCFS_SEQ_READ_COMMON,
        .open    =  lnet_peer_seq_open,
        .release =  libcfs_param_seq_release_common,
};

typedef struct {
        int                  lbsi_idx;
        loff_t               lbsi_off;
} lnet_buffer_seq_iterator_t;

int
lnet_buffer_seq_seek (lnet_buffer_seq_iterator_t *lbsi, loff_t off)
{
        int                idx;
        loff_t             here;
        int                rc;

        if (off == 0) {
                lbsi->lbsi_idx = -1;
                lbsi->lbsi_off = 0;
                return 0;
        }

        LNET_LOCK();

        if (lbsi->lbsi_idx < 0 ||
            lbsi->lbsi_off > off) {
                /* search from start */
                idx = 0;
                here = 1;
        } else {
                /* continue search */
                idx = lbsi->lbsi_idx;
                here = lbsi->lbsi_off;
        }

        lbsi->lbsi_off     = off;

        while (idx < LNET_NRBPOOLS) {
                if (here == off) {
                        lbsi->lbsi_idx = idx;
                        rc = 0;
                        goto out;
                }
                here++;
                idx++;
        }

        lbsi->lbsi_idx  = -1;
        rc              = -ENOENT;
 out:
        LNET_UNLOCK();
        return rc;
}

static void *
lnet_buffer_seq_start (libcfs_seq_file_t *s, loff_t *pos)
{
        lnet_buffer_seq_iterator_t *lbsi;
        int                        rc;

        LIBCFS_ALLOC(lbsi, sizeof(*lbsi));
        if (lbsi == NULL)
                return NULL;

        lbsi->lbsi_idx = -1;
        rc = lnet_buffer_seq_seek(lbsi, *pos);
        if (rc == 0)
                return lbsi;

        LIBCFS_FREE(lbsi, sizeof(*lbsi));
        return NULL;
}

static void
lnet_buffer_seq_stop (libcfs_seq_file_t *s, void *iter)
{
        lnet_buffer_seq_iterator_t  *lbsi = iter;

        if (lbsi != NULL)
                LIBCFS_FREE(lbsi, sizeof(*lbsi));
}

static void *
lnet_buffer_seq_next (libcfs_seq_file_t *s, void *iter, loff_t *pos)
{
        lnet_buffer_seq_iterator_t *lbsi = iter;
        int                         rc;
        loff_t                      next = *pos + 1;

        rc = lnet_buffer_seq_seek(lbsi, next);
        if (rc != 0) {
                LIBCFS_FREE(lbsi, sizeof(*lbsi));
                return NULL;
        }

        *pos = next;
        return lbsi;
}

static int
lnet_buffer_seq_show (libcfs_seq_file_t *s, void *iter)
{
        lnet_buffer_seq_iterator_t *lbsi = iter;
        lnet_rtrbufpool_t          *rbp;
        int                         npages;
        int                         nbuf;
        int                         cr;
        int                         mincr;

        if (lbsi->lbsi_off == 0) {
                LIBCFS_SEQ_PRINTF(s, "%5s %5s %7s %7s\n",
                                  "pages", "count", "credits", "min");
                return 0;
        }

        LASSERT (lbsi->lbsi_idx >= 0 && lbsi->lbsi_idx < LNET_NRBPOOLS);

        LNET_LOCK();

        rbp = &the_lnet.ln_rtrpools[lbsi->lbsi_idx];

        npages = rbp->rbp_npages;
        nbuf   = rbp->rbp_nbuffers;
        cr     = rbp->rbp_credits;
        mincr  = rbp->rbp_mincredits;

        LNET_UNLOCK();

        LIBCFS_SEQ_PRINTF(s, "%5d %5d %7d %7d\n", npages, nbuf, cr, mincr);
        return 0;
}

static libcfs_seq_ops_t lnet_buffer_sops = {
        /* start */ lnet_buffer_seq_start,
        /* stop */  lnet_buffer_seq_stop,
        /* next */  lnet_buffer_seq_next,
        /* show */  lnet_buffer_seq_show,
};

static int
lnet_buffer_seq_open(libcfs_inode_t *inode, libcfs_file_t *file)
{
        libcfs_param_dentry_t *dp = LIBCFS_PDE(inode);
        libcfs_seq_file_t     *sf;
        int                    rc;

        LPROCFS_ENTRY_AND_CHECK(dp);
        LIBCFS_SEQ_OPEN(file, &lnet_buffer_sops, rc);
        if (rc) {
                LPROCFS_EXIT();
                return rc;
        }
        sf = LIBCFS_FILE_PRIVATE(file);
        LIBCFS_SEQ_PRIVATE(sf) = LIBCFS_DENTRY_DATA(dp);

        return 0;
}

static libcfs_file_ops_t lnet_buffers_fops = {
        .owner   =  THIS_MODULE,
        .llseek  =  LIBCFS_SEQ_LSEEK_COMMON,
        .read    =  LIBCFS_SEQ_READ_COMMON,
        .open    =  lnet_buffer_seq_open,
        .release =  libcfs_param_seq_release_common,
};

typedef struct {
        lnet_ni_t           *lnsi_ni;
        loff_t               lnsi_off;
} lnet_ni_seq_iterator_t;

int
lnet_ni_seq_seek (lnet_ni_seq_iterator_t *lnsi, loff_t off)
{
        cfs_list_t        *n;
        loff_t             here;
        int                rc;

        if (off == 0) {
                lnsi->lnsi_ni = NULL;
                lnsi->lnsi_off = 0;
                return 0;
        }

        LNET_LOCK();

        if (lnsi->lnsi_ni == NULL ||
            lnsi->lnsi_off > off) {
                /* search from start */
                n = NULL;
                here = 1;
        } else {
                /* continue search */
                n = &lnsi->lnsi_ni->ni_list;
                here = lnsi->lnsi_off;
        }

        lnsi->lnsi_off = off;

        if (n == NULL)
                n = the_lnet.ln_nis.next;

        while (n != &the_lnet.ln_nis) {
                if (here == off) {
                        lnsi->lnsi_ni = cfs_list_entry(n, lnet_ni_t, ni_list);
                        rc = 0;
                        goto out;
                }
                here++;
                n = n->next;
        }

        lnsi->lnsi_ni  = NULL;
        rc             = -ENOENT;
 out:
        LNET_UNLOCK();
        return rc;
}

static void *
lnet_ni_seq_start (libcfs_seq_file_t *s, loff_t *pos)
{
        lnet_ni_seq_iterator_t *lnsi;
        int                     rc;

        LIBCFS_ALLOC(lnsi, sizeof(*lnsi));
        if (lnsi == NULL)
                return NULL;

        lnsi->lnsi_ni = NULL;
        rc = lnet_ni_seq_seek(lnsi, *pos);
        if (rc == 0)
                return lnsi;

        LIBCFS_FREE(lnsi, sizeof(*lnsi));
        return NULL;
}

static void
lnet_ni_seq_stop (libcfs_seq_file_t *s, void *iter)
{
        lnet_ni_seq_iterator_t  *lnsi = iter;

        if (lnsi != NULL)
                LIBCFS_FREE(lnsi, sizeof(*lnsi));
}

static void *
lnet_ni_seq_next (libcfs_seq_file_t *s, void *iter, loff_t *pos)
{
        lnet_ni_seq_iterator_t *lnsi = iter;
        int                     rc;
        loff_t                  next = *pos + 1;

        rc = lnet_ni_seq_seek(lnsi, next);
        if (rc != 0) {
                LIBCFS_FREE(lnsi, sizeof(*lnsi));
                return NULL;
        }

        *pos = next;
        return lnsi;
}

static int
lnet_ni_seq_show (libcfs_seq_file_t *s, void *iter)
{
        lnet_ni_seq_iterator_t *lnsi = iter;
        lnet_ni_t              *ni;
        cfs_time_t              now = cfs_time_current();
        int                     last_alive = -1;
        int                     maxtxcr;
        int                     txcr;
        int                     mintxcr;
        int                     npeertxcr;
        int                     npeerrtrcr;
        lnet_nid_t              nid;
        int                     nref;
        char                   *stat;

        if (lnsi->lnsi_off == 0) {
                LIBCFS_SEQ_PRINTF(s, "%-24s %6s %5s %4s %4s %4s %5s %5s %5s\n",
                                  "nid", "status", "alive", "refs", "peer",
                                  "rtr", "max", "tx", "min");
                return 0;
        }

        LASSERT (lnsi->lnsi_ni != NULL);

        LNET_LOCK();

        ni = lnsi->lnsi_ni;

        if (the_lnet.ln_routing)
                last_alive = cfs_duration_sec(cfs_time_sub(now,
                                              ni->ni_last_alive));
        if (ni->ni_lnd->lnd_type == LOLND) /* @lo forever alive */
                last_alive = 0;

        LASSERT(ni->ni_status != NULL);
        stat = (ni->ni_status->ns_status == LNET_NI_STATUS_UP) ? "up" : "down";

        maxtxcr    = ni->ni_maxtxcredits;
        txcr       = ni->ni_txcredits;
        mintxcr    = ni->ni_mintxcredits;
        npeertxcr  = ni->ni_peertxcredits;
        npeerrtrcr = ni->ni_peerrtrcredits;
        nid        = ni->ni_nid;
        nref       = ni->ni_refcount;

        LIBCFS_SEQ_PRINTF(s, "%-24s %6s %5d %4d %4d %4d %5d %5d %5d\n",
                          libcfs_nid2str(nid), stat, last_alive, nref,
                          npeertxcr, npeerrtrcr, maxtxcr,
                          txcr, mintxcr);

        LNET_UNLOCK();

        return 0;
}

static libcfs_seq_ops_t lnet_ni_sops = {
        /* start */ lnet_ni_seq_start,
        /* stop */  lnet_ni_seq_stop,
        /* next */  lnet_ni_seq_next,
        /* show */  lnet_ni_seq_show,
};

static int
lnet_ni_seq_open(libcfs_inode_t *inode, libcfs_file_t *file)
{
        libcfs_param_dentry_t *dp = LIBCFS_PDE(inode);
        libcfs_seq_file_t     *sf;
        int                    rc;

        LPROCFS_ENTRY_AND_CHECK(dp);
        LIBCFS_SEQ_OPEN(file, &lnet_ni_sops, rc);
        if (rc) {
                LPROCFS_EXIT();
                return rc;
        }
        sf = LIBCFS_FILE_PRIVATE(file);
        LIBCFS_SEQ_PRIVATE(sf) = LIBCFS_DENTRY_DATA(dp);

        return rc;
}

static libcfs_file_ops_t lnet_ni_fops = {
        .owner   =  THIS_MODULE,
        .llseek  =  LIBCFS_SEQ_LSEEK_COMMON,
        .read    =  LIBCFS_SEQ_READ_COMMON,
        .open    =  lnet_ni_seq_open,
        .release =  libcfs_param_seq_release_common,
};
#endif /* __KERNEL__ && LNET_ROUTER */

#if defined(LPROCFS) && defined(LNET_ROUTER)
void
lnet_proc_init(void)
{
        struct proc_dir_entry *pde;

#if 0
        pde = proc_mkdir(LNET_PROC_ROOT, NULL);
        if (pde == NULL) {
                CERROR("couldn't create "LNET_PROC_ROOT"\n");
                return;
        }
#endif
        /* Initialize LNET_PROC_STATS */
        pde = create_proc_entry (LNET_PROC_STATS, 0644, NULL);
        if (pde == NULL) {
                CERROR("couldn't create proc entry %s\n", LNET_PROC_STATS);
                return;
        }
        pde->data = NULL;
        pde->read_proc = lnet_router_proc_stats_read;
        pde->write_proc = lnet_router_proc_stats_write;

        /* Initialize LNET_PROC_ROUTES */
        pde = create_proc_entry (LNET_PROC_ROUTES, 0444, NULL);
        if (pde == NULL) {
                CERROR("couldn't create proc entry %s\n", LNET_PROC_ROUTES);
                return;
        }

        pde->proc_fops = &lnet_routes_fops;
        pde->data = NULL;

        /* Initialize LNET_PROC_ROUTERS */
        pde = create_proc_entry (LNET_PROC_ROUTERS, 0444, NULL);
        if (pde == NULL) {
                CERROR("couldn't create proc entry %s\n", LNET_PROC_ROUTERS);
                return;
        }

        pde->proc_fops = &lnet_routers_fops;
        pde->data = NULL;

        /* Initialize LNET_PROC_PEERS */
        pde = create_proc_entry (LNET_PROC_PEERS, 0444, NULL);
        if (pde == NULL) {
                CERROR("couldn't create proc entry %s\n", LNET_PROC_PEERS);
                return;
        }

        pde->proc_fops = &lnet_peer_fops;
        pde->data = NULL;

        /* Initialize LNET_PROC_BUFFERS */
        pde = create_proc_entry (LNET_PROC_BUFFERS, 0444, NULL);
        if (pde == NULL) {
                CERROR("couldn't create proc entry %s\n", LNET_PROC_BUFFERS);
                return;
        }

        pde->proc_fops = &lnet_buffers_fops;
        pde->data = NULL;

        /* Initialize LNET_PROC_NIS */
        pde = create_proc_entry (LNET_PROC_NIS, 0444, NULL);
        if (pde == NULL) {
                CERROR("couldn't create proc entry %s\n", LNET_PROC_NIS);
                return;
        }

        pde->proc_fops = &lnet_ni_fops;
        pde->data = NULL;
}

void
lnet_proc_fini(void)
{
        remove_proc_entry(LNET_PROC_STATS, 0);
        remove_proc_entry(LNET_PROC_ROUTES, 0);
        remove_proc_entry(LNET_PROC_ROUTERS, 0);
        remove_proc_entry(LNET_PROC_PEERS, 0);
        remove_proc_entry(LNET_PROC_BUFFERS, 0);
        remove_proc_entry(LNET_PROC_NIS, 0);
#if 0   
        remove_proc_entry(LNET_PROC_ROOT, 0);
#endif
}

#else

void
lnet_proc_init(void)
{
}

void
lnet_proc_fini(void)
{
}

#endif

#if defined(__KERNEL__) && defined(LNET_ROUTER)
int
lnet_params_init(void)
{
        struct libcfs_param_entry *lpe;
        int                        rc = 0;

        if (libcfs_param_lnet_root == NULL) {
                libcfs_param_lnet_root = libcfs_param_mkdir(LNET_PARAM_ROOT,
                                                      libcfs_param_get_root());
                if (libcfs_param_lnet_root == NULL) {
                        CERROR("couldn't create "LNET_PARAM_ROOT"\n");
                        return -EINVAL;
                }
        } else {
                libcfs_param_get(libcfs_param_lnet_root);
        }

        /* Initialize LNET_PARAM_STATS */
        lpe = libcfs_param_create(LNET_PARAM_STATS, 0644, libcfs_param_lnet_root);
        if (lpe == NULL) {
                CERROR("couldn't create param entry %s\n", LNET_PARAM_STATS);
                GOTO(out, rc = -EINVAL);
        }

        lpe->lpe_data = LIBCFS_ALLOC_PARAMDATA(NULL);
        lpe->lpe_cb_read = lnet_router_proc_stats_read;
        lpe->lpe_cb_write = lnet_router_proc_stats_write;
        libcfs_param_put(lpe);

        /* Initialize LNET_PARAM_ROUTES */
        lpe = libcfs_param_create(LNET_PARAM_ROUTES, 0444, libcfs_param_lnet_root);
        if (lpe == NULL) {
                CERROR("couldn't create param entry %s\n", LNET_PARAM_ROUTES);
                GOTO(out, rc = -EINVAL);
        }
        lpe->lpe_data = NULL;
        lpe->lpe_cb_sfops = &lnet_routes_fops;
        libcfs_param_put(lpe);

        /* Initialize LNET_PARAM_ROUTERS */
        lpe = libcfs_param_create(LNET_PARAM_ROUTERS, 0444, libcfs_param_lnet_root);
        if (lpe == NULL) {
                CERROR("couldn't create param entry %s\n", LNET_PARAM_ROUTERS);
                GOTO(out, rc = -EINVAL);
        }
        lpe->lpe_data = NULL;
        lpe->lpe_cb_sfops = &lnet_routers_fops;
        libcfs_param_put(lpe);

        /* Initialize LNET_PARAM_PEERS */
        lpe = libcfs_param_create(LNET_PARAM_PEERS, 0444, libcfs_param_lnet_root);
        if (lpe == NULL) {
                CERROR("couldn't create param entry %s\n", LNET_PARAM_PEERS);
                GOTO(out, rc = -EINVAL);
        }
        lpe->lpe_data = NULL;
        lpe->lpe_cb_sfops = &lnet_peer_fops;
        libcfs_param_put(lpe);

        /* Initialize LNET_PARAM_BUFFERS */
        lpe = libcfs_param_create(LNET_PARAM_BUFFERS, 0444, libcfs_param_lnet_root);
        if (lpe == NULL) {
                CERROR("couldn't create param entry %s\n", LNET_PARAM_BUFFERS);
                GOTO(out, rc = -EINVAL);
        }
        lpe->lpe_data = NULL;
        lpe->lpe_cb_sfops = &lnet_buffers_fops;
        libcfs_param_put(lpe);

        /* Initialize LNET_PARAM_NIS */
        lpe = libcfs_param_create(LNET_PARAM_NIS, 0444, libcfs_param_lnet_root);
        if (lpe == NULL) {
                CERROR("couldn't create param entry %s\n", LNET_PARAM_NIS);
                GOTO(out, rc = -EINVAL);
        }
        lpe->lpe_data = NULL;
        lpe->lpe_cb_sfops = &lnet_ni_fops;
        libcfs_param_put(lpe);
out:
        libcfs_param_put(libcfs_param_lnet_root);
        return rc;
}

void
lnet_params_fini(void)
{
        libcfs_param_remove(LNET_PARAM_STATS, libcfs_param_lnet_root);
        libcfs_param_remove(LNET_PARAM_ROUTES, libcfs_param_lnet_root);
        libcfs_param_remove(LNET_PARAM_ROUTERS, libcfs_param_lnet_root);
        libcfs_param_remove(LNET_PARAM_PEERS, libcfs_param_lnet_root);
        libcfs_param_remove(LNET_PARAM_BUFFERS, libcfs_param_lnet_root);
        libcfs_param_remove(LNET_PARAM_NIS, libcfs_param_lnet_root);
}
#else

int
lnet_params_init(void)
{
        return 0;
}

void
lnet_params_fini(void)
{
}

#endif
