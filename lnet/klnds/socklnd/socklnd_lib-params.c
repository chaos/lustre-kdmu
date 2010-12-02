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
 */

#include "socklnd.h"

/* sysctl table for params_tree */
/* To avoid the error "initializer element is not constant",
 * we use a function to initialize these elements.
 */
static cfs_param_sysctl_table_t lp_ksocknal_ctl_table[30];
static void ksocknal_ctl_table_init(void)
{
        int i = 0;

        lp_ksocknal_ctl_table[i] = (cfs_param_sysctl_table_t)
        {
                .name     = "timeout",
                .data     = ksocknal_tunables.ksnd_timeout,
                .mode     = 0644,
                .read     = cfs_param_intvec_read,
                .write    = cfs_param_intvec_write
        };
        i++;

        lp_ksocknal_ctl_table[i] = (cfs_param_sysctl_table_t)
        {
                .name     = "credits",
                .data     = ksocknal_tunables.ksnd_credits,
                .mode     = 0444,
                .read     = cfs_param_intvec_read
        };
        i++;

        lp_ksocknal_ctl_table[i] = (cfs_param_sysctl_table_t)
        {
                .name     = "peer_credits",
                .data     = ksocknal_tunables.ksnd_peertxcredits,
                .mode     = 0444,
                .read     = cfs_param_intvec_read
        };
        i++;

        lp_ksocknal_ctl_table[i] = (cfs_param_sysctl_table_t)
        {
                .name     = "peer_buffer_credits",
                .data     = ksocknal_tunables.ksnd_peerrtrcredits,
                .mode     = 0444,
                .read     = cfs_param_intvec_read
        };
        i++;

        lp_ksocknal_ctl_table[i] = (cfs_param_sysctl_table_t)
        {
                .name     = "peer_timeout",
                .data     = ksocknal_tunables.ksnd_peertimeout,
                .mode     = 0444,
                .read     = cfs_param_intvec_read
        };
        i++;

        lp_ksocknal_ctl_table[i] = (cfs_param_sysctl_table_t)
        {
                .name     = "nconnds",
                .data     = ksocknal_tunables.ksnd_nconnds,
                .mode     = 0444,
                .read     = cfs_param_intvec_read
        };
        i++;

        lp_ksocknal_ctl_table[i] = (cfs_param_sysctl_table_t)
        {
                .name     = "min_reconnectms",
                .data     = ksocknal_tunables.ksnd_min_reconnectms,
                .mode     = 0444,
                .read     = cfs_param_intvec_read
        };
        i++;

        lp_ksocknal_ctl_table[i] = (cfs_param_sysctl_table_t)
        {
                .name     = "max_reconnectms",
                .data     = ksocknal_tunables.ksnd_max_reconnectms,
                .mode     = 0444,
                .read     = cfs_param_intvec_read
        };
        i++;

        lp_ksocknal_ctl_table[i] = (cfs_param_sysctl_table_t)
        {
                .name     = "eager_ack",
                .data     = ksocknal_tunables.ksnd_eager_ack,
                .mode     = 0644,
                .read     = cfs_param_intvec_read,
                .write    = cfs_param_intvec_write
        };
        i++;

        lp_ksocknal_ctl_table[i] = (cfs_param_sysctl_table_t)
        {
                .name     = "nonblk_zcack",
                .data     = ksocknal_tunables.ksnd_nonblk_zcack,
                .mode     = 0644,
                .read     = cfs_param_intvec_read,
                .write    = cfs_param_intvec_write
        };
        i++;

        lp_ksocknal_ctl_table[i] = (cfs_param_sysctl_table_t)
        {
                .name     = "zero_copy_min_payload",
                .data     = ksocknal_tunables.ksnd_zc_min_payload,
                .mode     = 0644,
                .read     = cfs_param_intvec_read,
                .write    = cfs_param_intvec_write
        };
        i++;

        lp_ksocknal_ctl_table[i] = (cfs_param_sysctl_table_t)
        {
                .name     = "zero_copy_recv",
                .data     = ksocknal_tunables.ksnd_zc_recv,
                .mode     = 0644,
                .read     = cfs_param_intvec_read,
                .write    = cfs_param_intvec_write
        };
        i++;

        lp_ksocknal_ctl_table[i] = (cfs_param_sysctl_table_t)
        {
                .name     = "zero_copy_recv_min_nfrags",
                .data     = ksocknal_tunables.ksnd_zc_recv_min_nfrags,
                .mode     = 0644,
                .read     = cfs_param_intvec_read,
                .write    = cfs_param_intvec_write
        };
        i++;

        lp_ksocknal_ctl_table[i] = (cfs_param_sysctl_table_t)
        {
                .name     = "typed",
                .data     = ksocknal_tunables.ksnd_typed_conns,
                .mode     = 0444,
                .read     = cfs_param_intvec_read
        };
        i++;

        lp_ksocknal_ctl_table[i] = (cfs_param_sysctl_table_t)
        {
                .name     = "min_bulk",
                .data     = ksocknal_tunables.ksnd_min_bulk,
                .mode     = 0644,
                .read     = cfs_param_intvec_read,
                .write    = cfs_param_intvec_write
        };
        i++;

        lp_ksocknal_ctl_table[i] = (cfs_param_sysctl_table_t)
        {
                .name     = "rx_buffer_size",
                .data     = ksocknal_tunables.ksnd_rx_buffer_size,
                .mode     = 0644,
                .read     = cfs_param_intvec_read,
                .write    = cfs_param_intvec_write
        };
        i++;

        lp_ksocknal_ctl_table[i] = (cfs_param_sysctl_table_t)
        {
                .name     = "tx_buffer_size",
                .data     = ksocknal_tunables.ksnd_tx_buffer_size,
                .mode     = 0644,
                .read     = cfs_param_intvec_read,
                .write    = cfs_param_intvec_write
        };
        i++;

        lp_ksocknal_ctl_table[i] = (cfs_param_sysctl_table_t)
        {
                .name     = "nagle",
                .data     = ksocknal_tunables.ksnd_nagle,
                .mode     = 0644,
                .read     = cfs_param_intvec_read,
                .write    = cfs_param_intvec_write
        };
        i++;

#ifdef CPU_AFFINITY
        lp_ksocknal_ctl_table[i] = (cfs_param_sysctl_table_t)
        {
                .name     = "irq_affinity",
                .data     = ksocknal_tunables.ksnd_irq_affinity,
                .mode     = 0644,
                .read     = cfs_param_intvec_read,
                .write    = cfs_param_intvec_write
        };
        i++;
#endif

        lp_ksocknal_ctl_table[i] = (cfs_param_sysctl_table_t)
        {
                .name     = "round_robin",
                .data     = ksocknal_tunables.ksnd_round_robin,
                .mode     = 0644,
                .read     = cfs_param_intvec_read,
                .write    = cfs_param_intvec_write
        };
        i++;

        lp_ksocknal_ctl_table[i] = (cfs_param_sysctl_table_t)
        {
                .name     = "keepalive",
                .data     = ksocknal_tunables.ksnd_keepalive,
                .mode     = 0644,
                .read     = cfs_param_intvec_read,
                .write    = cfs_param_intvec_write
        };
        i++;

        lp_ksocknal_ctl_table[i] = (cfs_param_sysctl_table_t)
        {
                .name     = "keepalive_idle",
                .data     = ksocknal_tunables.ksnd_keepalive_idle,
                .mode     = 0644,
                .read     = cfs_param_intvec_read,
                .write    = cfs_param_intvec_write
        };
        i++;

        lp_ksocknal_ctl_table[i] = (cfs_param_sysctl_table_t)
        {
                .name     = "keepalive_count",
                .data     = ksocknal_tunables.ksnd_keepalive_count,
                .mode     = 0644,
                .read     = cfs_param_intvec_read,
                .write    = cfs_param_intvec_write
        };
        i++;

        lp_ksocknal_ctl_table[i] = (cfs_param_sysctl_table_t)
        {
                .name     = "keepalive_intvl",
                .data     = ksocknal_tunables.ksnd_keepalive_intvl,
                .mode     = 0644,
                .read     = cfs_param_intvec_read,
                .write    = cfs_param_intvec_write
        };
        i++;

#ifdef SOCKNAL_BACKOFF
        lp_ksocknal_ctl_table[i] = (cfs_param_sysctl_table_t)
        {
                .name     = "backoff_init",
                .data     = ksocknal_tunables.ksnd_backoff_init,
                .mode     = 0644,
                .read     = cfs_param_intvec_read,
                .write    = cfs_param_intvec_write
        };
        i++;

        lp_ksocknal_ctl_table[i] = (cfs_param_sysctl_table_t)
        {
                .name     = "backoff_max",
                .data     = ksocknal_tunables.ksnd_backoff_max,
                .mode     = 0644,
                .read     = cfs_param_intvec_read,
                .write    = cfs_param_intvec_write
        };
        i++;
#endif
#if SOCKNAL_VERSION_DEBUG
        lp_ksocknal_ctl_table[i] = (cfs_param_sysctl_table_t)
        {
                .name     = "protocol",
                .data     = ksocknal_tunables.ksnd_protocol,
                .mode     = 0644,
                .read     = cfs_param_intvec_read,
                .write    = cfs_param_intvec_write
        };
        i++;
#endif
        lp_ksocknal_ctl_table[i] = (cfs_param_sysctl_table_t)
        {
                .name     = "enable_csum",
                .data     = ksocknal_tunables.ksnd_enable_csum,
                .mode     = 0644,
                .read     = cfs_param_intvec_read,
                .write    = cfs_param_intvec_write
        };
        i++;
        lp_ksocknal_ctl_table[i] = (cfs_param_sysctl_table_t)
        {
                .name     = "inject_csum_error",
                .data     = ksocknal_tunables.ksnd_inject_csum_error,
                .mode     = 0644,
                .read     = cfs_param_intvec_read,
                .write    = cfs_param_intvec_write
        };
        i++;
}

int
ksocknal_lib_params_init ()
{
        ksocknal_ctl_table_init();
        return cfs_param_sysctl_init("socknal", lp_ksocknal_ctl_table,
                                     cfs_param_get_lnet_root());
}

void
ksocknal_lib_params_fini ()
{
        cfs_param_sysctl_fini("socknal", cfs_param_get_lnet_root());
}
