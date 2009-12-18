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
 */

#include "socklnd.h"

/* sysctl table for params_tree */
/* To avoid the error "initializer element is not constant",
 * we use a function to initialize these elements.
 */
static struct libcfs_param_ctl_table lp_ksocknal_ctl_table[29];
static void libcfs_param_ksocknal_ctl_table_init(void)
{
        int i = 0;

        lp_ksocknal_ctl_table[i] = (struct libcfs_param_ctl_table)
        {
                .name     = "timeout",
                .data     = ksocknal_tunables.ksnd_timeout,
                .mode     = 0644,
                .read     = libcfs_param_intvec_read,
                .write    = libcfs_param_intvec_write
        };
        i++;

        lp_ksocknal_ctl_table[i] = (struct libcfs_param_ctl_table)
        {
                .name     = "credits",
                .data     = ksocknal_tunables.ksnd_credits,
                .mode     = 0444,
                .read     = libcfs_param_intvec_read
        };
        i++;

        lp_ksocknal_ctl_table[i] = (struct libcfs_param_ctl_table)
        {
                .name     = "peer_credits",
                .data     = ksocknal_tunables.ksnd_peertxcredits,
                .mode     = 0444,
                .read     = libcfs_param_intvec_read
        };
        i++;

        lp_ksocknal_ctl_table[i] = (struct libcfs_param_ctl_table)
        {
                .name     = "peer_buffer_credits",
                .data     = ksocknal_tunables.ksnd_peerrtrcredits,
                .mode     = 0444,
                .read     = libcfs_param_intvec_read
        };
        i++;

        lp_ksocknal_ctl_table[i] = (struct libcfs_param_ctl_table)
        {
                .name     = "peer_timeout",
                .data     = ksocknal_tunables.ksnd_peertimeout,
                .mode     = 0444,
                .read     = libcfs_param_intvec_read
        };
        i++;

        lp_ksocknal_ctl_table[i] = (struct libcfs_param_ctl_table)
        {
                .name     = "nconnds",
                .data     = ksocknal_tunables.ksnd_nconnds,
                .mode     = 0444,
                .read     = libcfs_param_intvec_read
        };
        i++;

        lp_ksocknal_ctl_table[i] = (struct libcfs_param_ctl_table)
        {
                .name     = "min_reconnectms",
                .data     = ksocknal_tunables.ksnd_min_reconnectms,
                .mode     = 0444,
                .read     = libcfs_param_intvec_read
        };
        i++;

        lp_ksocknal_ctl_table[i] = (struct libcfs_param_ctl_table)
        {
                .name     = "max_reconnectms",
                .data     = ksocknal_tunables.ksnd_max_reconnectms,
                .mode     = 0444,
                .read     = libcfs_param_intvec_read
        };
        i++;

        lp_ksocknal_ctl_table[i] = (struct libcfs_param_ctl_table)
        {
                .name     = "eager_ack",
                .data     = ksocknal_tunables.ksnd_eager_ack,
                .mode     = 0644,
                .read     = libcfs_param_intvec_read,
                .write    = libcfs_param_intvec_write
        };
        i++;

        lp_ksocknal_ctl_table[i] = (struct libcfs_param_ctl_table)
        {
                .name     = "zero_copy_min_payload",
                .data     = ksocknal_tunables.ksnd_zc_min_payload,
                .mode     = 0644,
                .read     = libcfs_param_intvec_read,
                .write    = libcfs_param_intvec_write
        };
        i++;

        lp_ksocknal_ctl_table[i] = (struct libcfs_param_ctl_table)
        {
                .name     = "zero_copy_recv",
                .data     = ksocknal_tunables.ksnd_zc_recv,
                .mode     = 0644,
                .read     = libcfs_param_intvec_read,
                .write    = libcfs_param_intvec_write
        };
        i++;

        lp_ksocknal_ctl_table[i] = (struct libcfs_param_ctl_table)
        {
                .name     = "zero_copy_recv_min_nfrags",
                .data     = ksocknal_tunables.ksnd_zc_recv_min_nfrags,
                .mode     = 0644,
                .read     = libcfs_param_intvec_read,
                .write    = libcfs_param_intvec_write
        };
        i++;

        lp_ksocknal_ctl_table[i] = (struct libcfs_param_ctl_table)
        {
                .name     = "typed",
                .data     = ksocknal_tunables.ksnd_typed_conns,
                .mode     = 0444,
                .read     = libcfs_param_intvec_read
        };
        i++;

        lp_ksocknal_ctl_table[i] = (struct libcfs_param_ctl_table)
        {
                .name     = "min_bulk",
                .data     = ksocknal_tunables.ksnd_min_bulk,
                .mode     = 0644,
                .read     = libcfs_param_intvec_read,
                .write    = libcfs_param_intvec_write
        };
        i++;

        lp_ksocknal_ctl_table[i] = (struct libcfs_param_ctl_table)
        {
                .name     = "rx_buffer_size",
                .data     = ksocknal_tunables.ksnd_rx_buffer_size,
                .mode     = 0644,
                .read     = libcfs_param_intvec_read,
                .write    = libcfs_param_intvec_write
        };
        i++;

        lp_ksocknal_ctl_table[i] = (struct libcfs_param_ctl_table)
        {
                .name     = "tx_buffer_size",
                .data     = ksocknal_tunables.ksnd_tx_buffer_size,
                .mode     = 0644,
                .read     = libcfs_param_intvec_read,
                .write    = libcfs_param_intvec_write
        };
        i++;

        lp_ksocknal_ctl_table[i] = (struct libcfs_param_ctl_table)
        {
                .name     = "nagle",
                .data     = ksocknal_tunables.ksnd_nagle,
                .mode     = 0644,
                .read     = libcfs_param_intvec_read,
                .write    = libcfs_param_intvec_write
        };
        i++;

#ifdef CPU_AFFINITY
        lp_ksocknal_ctl_table[i] = (struct libcfs_param_ctl_table)
        {
                .name     = "irq_affinity",
                .data     = ksocknal_tunables.ksnd_irq_affinity,
                .mode     = 0644,
                .read     = libcfs_param_intvec_read,
                .write    = libcfs_param_intvec_write
        };
        i++;
#endif

        lp_ksocknal_ctl_table[i] = (struct libcfs_param_ctl_table)
        {
                .name     = "round_robin",
                .data     = ksocknal_tunables.ksnd_round_robin,
                .mode     = 0644,
                .read     = libcfs_param_intvec_read,
                .write    = libcfs_param_intvec_write
        };
        i++;

        lp_ksocknal_ctl_table[i] = (struct libcfs_param_ctl_table)
        {
                .name     = "keepalive",
                .data     = ksocknal_tunables.ksnd_keepalive,
                .mode     = 0644,
                .read     = libcfs_param_intvec_read,
                .write    = libcfs_param_intvec_write
        };
        i++;

        lp_ksocknal_ctl_table[i] = (struct libcfs_param_ctl_table)
        {
                .name     = "keepalive_idle",
                .data     = ksocknal_tunables.ksnd_keepalive_idle,
                .mode     = 0644,
                .read     = libcfs_param_intvec_read,
                .write    = libcfs_param_intvec_write
        };
        i++;

        lp_ksocknal_ctl_table[i] = (struct libcfs_param_ctl_table)
        {
                .name     = "keepalive_count",
                .data     = ksocknal_tunables.ksnd_keepalive_count,
                .mode     = 0644,
                .read     = libcfs_param_intvec_read,
                .write    = libcfs_param_intvec_write
        };
        i++;

        lp_ksocknal_ctl_table[i] = (struct libcfs_param_ctl_table)
        {
                .name     = "keepalive_intvl",
                .data     = ksocknal_tunables.ksnd_keepalive_intvl,
                .mode     = 0644,
                .read     = libcfs_param_intvec_read,
                .write    = libcfs_param_intvec_write
        };
        i++;

#ifdef SOCKNAL_BACKOFF
        lp_ksocknal_ctl_table[i] = (struct libcfs_param_ctl_table)
        {
                .name     = "backoff_init",
                .data     = ksocknal_tunables.ksnd_backoff_init,
                .mode     = 0644,
                .read     = libcfs_param_intvec_read,
                .write    = libcfs_param_intvec_write
        };
        i++;

        lp_ksocknal_ctl_table[i] = (struct libcfs_param_ctl_table)
        {
                .name     = "backoff_max",
                .data     = ksocknal_tunables.ksnd_backoff_max,
                .mode     = 0644,
                .read     = libcfs_param_intvec_read,
                .write    = libcfs_param_intvec_write
        };
        i++;
#endif
#if SOCKNAL_VERSION_DEBUG
        lp_ksocknal_ctl_table[i] = (struct libcfs_param_ctl_table)
        {
                .name     = "protocol",
                .data     = ksocknal_tunables.ksnd_protocol,
                .mode     = 0644,
                .read     = libcfs_param_intvec_read,
                .write    = libcfs_param_intvec_write
        };
        i++;
#endif
        lp_ksocknal_ctl_table[i] = (struct libcfs_param_ctl_table)
        {
                .name     = "enable_csum",
                .data     = ksocknal_tunables.ksnd_enable_csum,
                .mode     = 0644,
                .read     = libcfs_param_intvec_read,
                .write    = libcfs_param_intvec_write
        };
        i++;
        lp_ksocknal_ctl_table[i] = (struct libcfs_param_ctl_table)
        {
                .name     = "inject_csum_error",
                .data     = ksocknal_tunables.ksnd_inject_csum_error,
                .mode     = 0644,
                .read     = libcfs_param_intvec_read,
                .write    = libcfs_param_intvec_write
        };
        i++;
}

int
ksocknal_lib_params_init ()
{
        if (!*ksocknal_tunables.ksnd_typed_conns) {
                int rc = -EINVAL;
#if SOCKNAL_VERSION_DEBUG
                if (*ksocknal_tunables.ksnd_protocol < 3)
                        rc = 0;
#endif
                if (rc != 0) {
                        CERROR("Protocol V3.x MUST have typed connections\n");
                        return rc;
                }
        }

        if (*ksocknal_tunables.ksnd_zc_recv_min_nfrags < 2)
                *ksocknal_tunables.ksnd_zc_recv_min_nfrags = 2;
        if (*ksocknal_tunables.ksnd_zc_recv_min_nfrags > LNET_MAX_IOV)
                *ksocknal_tunables.ksnd_zc_recv_min_nfrags = LNET_MAX_IOV;

        libcfs_param_ksocknal_ctl_table_init();
        libcfs_param_sysctl_init("socknal", lp_ksocknal_ctl_table,
                                 libcfs_param_lnet_root);

        return 0;
}

void
ksocknal_lib_params_fini ()
{
        libcfs_param_sysctl_fini("socknal", libcfs_param_lnet_root);
}
