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
 *
 * lnet/klnds/o2iblnd/o2iblnd_modparams.c
 *
 * Author: Eric Barton <eric@bartonsoftware.com>
 */

#include "o2iblnd.h"

static int service = 987;
CFS_MODULE_PARM(service, "i", int, 0444,
                "service number (within RDMA_PS_TCP)");

static int cksum = 0;
CFS_MODULE_PARM(cksum, "i", int, 0644,
                "set non-zero to enable message (not RDMA) checksums");

static int nettimeout = 50;
CFS_MODULE_PARM(nettimeout, "i", int, 0644,
                "timeout (seconds)");

static int ntx = 256;
CFS_MODULE_PARM(ntx, "i", int, 0444,
                "# of message descriptors");

static int credits = 64;
CFS_MODULE_PARM(credits, "i", int, 0444,
                "# concurrent sends");

static int peer_credits = 8;
CFS_MODULE_PARM(peer_credits, "i", int, 0444,
                "# concurrent sends to 1 peer");

static int peer_credits_hiw = 0;
CFS_MODULE_PARM(peer_credits_hiw, "i", int, 0444,
                "when eagerly to return credits");

static int peer_buffer_credits = 0;
CFS_MODULE_PARM(peer_buffer_credits, "i", int, 0444,
                "# per-peer router buffer credits");

static int peer_timeout = 180;
CFS_MODULE_PARM(peer_timeout, "i", int, 0444,
                "Seconds without aliveness news to declare peer dead (<=0 to disable)");

static char *ipif_name = "ib0";
CFS_MODULE_PARM(ipif_name, "s", charp, 0444,
                "IPoIB interface name");

static int retry_count = 5;
CFS_MODULE_PARM(retry_count, "i", int, 0644,
                "Retransmissions when no ACK received");

static int rnr_retry_count = 6;
CFS_MODULE_PARM(rnr_retry_count, "i", int, 0644,
                "RNR retransmissions");

static int keepalive = 100;
CFS_MODULE_PARM(keepalive, "i", int, 0644,
                "Idle time in seconds before sending a keepalive");

static int ib_mtu = 0;
CFS_MODULE_PARM(ib_mtu, "i", int, 0444,
                "IB MTU 256/512/1024/2048/4096");

static int concurrent_sends = 0;
CFS_MODULE_PARM(concurrent_sends, "i", int, 0444,
                "send work-queue sizing");

static int map_on_demand = 0;
CFS_MODULE_PARM(map_on_demand, "i", int, 0444,
                "map on demand");

kib_tunables_t kiblnd_tunables = {
        .kib_service                = &service,
        .kib_cksum                  = &cksum,
        .kib_timeout                = &nettimeout,
        .kib_keepalive              = &keepalive,
        .kib_ntx                    = &ntx,
        .kib_credits                = &credits,
        .kib_peertxcredits          = &peer_credits,
        .kib_peercredits_hiw        = &peer_credits_hiw,
        .kib_peerrtrcredits         = &peer_buffer_credits,
        .kib_peertimeout            = &peer_timeout,
        .kib_default_ipif           = &ipif_name,
        .kib_retry_count            = &retry_count,
        .kib_rnr_retry_count        = &rnr_retry_count,
        .kib_concurrent_sends       = &concurrent_sends,
        .kib_ib_mtu                 = &ib_mtu,
        .kib_map_on_demand          = &map_on_demand,
        /* .kib_plat_tunes to be inited in kiblnd_plat_modparams_init() */
};

static char ipif_basename_space[32];

#if defined(CONFIG_SYSCTL) && !CFS_SYSFS_MODULE_PARM

static cfs_sysctl_table_t kiblnd_ctl_table[] = {
        {
                .ctl_name = O2IBLND_SERVICE,
                .procname = "service",
                .data     = &service,
                .maxlen   = sizeof(int),
                .mode     = 0444,
                .proc_handler = &proc_dointvec
        },
        {
                .ctl_name = O2IBLND_CKSUM,
                .procname = "cksum",
                .data     = &cksum,
                .maxlen   = sizeof(int),
                .mode     = 0644,
                .proc_handler = &proc_dointvec
        },
        {
                .ctl_name = O2IBLND_TIMEOUT,
                .procname = "timeout",
                .data     = &nettimeout,
                .maxlen   = sizeof(int),
                .mode     = 0644,
                .proc_handler = &proc_dointvec
        },
        {
                .ctl_name = O2IBLND_NTX,
                .procname = "ntx",
                .data     = &ntx,
                .maxlen   = sizeof(int),
                .mode     = 0444,
                .proc_handler = &proc_dointvec
        },
        {
                .ctl_name = O2IBLND_CREDITS,
                .procname = "credits",
                .data     = &credits,
                .maxlen   = sizeof(int),
                .mode     = 0444,
                .proc_handler = &proc_dointvec
        },
        {
                .ctl_name = O2IBLND_PEER_TXCREDITS,
                .procname = "peer_credits",
                .data     = &peer_credits,
                .maxlen   = sizeof(int),
                .mode     = 0444,
                .proc_handler = &proc_dointvec
        },
        {
                .ctl_name = O2IBLND_PEER_CREDITS_HIW,
                .procname = "peer_credits_hiw",
                .data     = &peer_credits_hiw,
                .maxlen   = sizeof(int),
                .mode     = 0444,
                .proc_handler = &proc_dointvec
        },
        {
                .ctl_name = O2IBLND_PEER_RTRCREDITS,
                .procname = "peer_buffer_credits",
                .data     = &peer_buffer_credits,
                .maxlen   = sizeof(int),
                .mode     = 0444,
                .proc_handler = &proc_dointvec
        },
        {
                .ctl_name = O2IBLND_PEER_TIMEOUT,
                .procname = "peer_timeout",
                .data     = &peer_timeout,
                .maxlen   = sizeof(int),
                .mode     = 0444,
                .proc_handler = &proc_dointvec
        },
        {
                .ctl_name = O2IBLND_IPIF_BASENAME,
                .procname = "ipif_name",
                .data     = ipif_basename_space,
                .maxlen   = sizeof(ipif_basename_space),
                .mode     = 0444,
                .proc_handler = &proc_dostring
        },
        {
                .ctl_name = O2IBLND_RETRY_COUNT,
                .procname = "retry_count",
                .data     = &retry_count,
                .maxlen   = sizeof(int),
                .mode     = 0644,
                .proc_handler = &proc_dointvec
        },
        {
                .ctl_name = O2IBLND_RNR_RETRY_COUNT,
                .procname = "rnr_retry_count",
                .data     = &rnr_retry_count,
                .maxlen   = sizeof(int),
                .mode     = 0644,
                .proc_handler = &proc_dointvec
        },
        {
                .ctl_name = O2IBLND_KEEPALIVE,
                .procname = "keepalive",
                .data     = &keepalive,
                .maxlen   = sizeof(int),
                .mode     = 0644,
                .proc_handler = &proc_dointvec
        },
        {
                .ctl_name = O2IBLND_CONCURRENT_SENDS,
                .procname = "concurrent_sends",
                .data     = &concurrent_sends,
                .maxlen   = sizeof(int),
                .mode     = 0444,
                .proc_handler = &proc_dointvec
        },
        {
                .ctl_name = O2IBLND_IB_MTU,
                .procname = "ib_mtu",
                .data     = &ib_mtu,
                .maxlen   = sizeof(int),
                .mode     = 0444,
                .proc_handler = &proc_dointvec
        },
        {
                .ctl_name = O2IBLND_MAP_ON_DEMAND,
                .procname = "map_on_demand",
                .data     = &map_on_demand,
                .maxlen   = sizeof(int),
                .mode     = 0444,
                .proc_handler = &proc_dointvec
        },
        {0}
};

static cfs_sysctl_table_t kiblnd_top_ctl_table[] = {
        {
                .ctl_name = CTL_O2IBLND,
                .procname = "o2iblnd",
                .data     = NULL,
                .maxlen   = 0,
                .mode     = 0555,
                .child    = kiblnd_ctl_table
        },
        {0}
};

#endif /* defined(CONFIG_SYSCTL) && !CFS_SYSFS_MODULE_PARM */

static struct libcfs_param_ctl_table libcfs_param_kiblnd_ctl_table[] = {
        {
                .name     = "service",
                .data     = &service,
                .mode     = 0444,
                .read     = libcfs_param_intvec_read
        },
        {
                .name     = "cksum",
                .data     = &cksum,
                .mode     = 0644,
                .read     = libcfs_param_intvec_read,
                .write    = libcfs_param_intvec_write
        },
        {
                .name     = "timeout",
                .data     = &nettimeout,
                .mode     = 0644,
                .read     = libcfs_param_intvec_read,
                .write    = libcfs_param_intvec_write
        },
        {
                .name     = "ntx",
                .data     = &ntx,
                .mode     = 0444,
                .read     = libcfs_param_intvec_read
        },
        {
                .name     = "credits",
                .data     = &credits,
                .mode     = 0444,
                .read     = libcfs_param_intvec_read
        },
        {
                .name     = "peer_credits",
                .data     = &peer_credits,
                .mode     = 0444,
                .read     = libcfs_param_intvec_read
        },
        {
                .name     = "peer_credits_hiw",
                .data     = &peer_credits_hiw,
                .mode     = 0444,
                .read     = libcfs_param_intvec_read
        },
        {
                .name     = "peer_buffer_credits",
                .data     = &peer_buffer_credits,
                .mode     = 0444,
                .read     = libcfs_param_intvec_read
        },
        {
                .name     = "peer_timeout",
                .data     = &peer_timeout,
                .mode     = 0444,
                .read     = libcfs_param_intvec_read
        },
        {
                .name     = "ipif_name",
                .data     = ipif_basename_space,
                .mode     = 0444,
                .read     = libcfs_param_string_read
        },
        {
                .name     = "retry_count",
                .data     = &retry_count,
                .mode     = 0644,
                .read     = libcfs_param_intvec_read,
                .write    = libcfs_param_intvec_write
        },
        {
                .name     = "rnr_retry_count",
                .data     = &rnr_retry_count,
                .mode     = 0644,
                .read     = libcfs_param_intvec_read,
                .write    = libcfs_param_intvec_write
        },
        {
                .name     = "keepalive",
                .data     = &keepalive,
                .mode     = 0644,
                .read     = libcfs_param_intvec_read,
                .write    = libcfs_param_intvec_write
        },
        {
                .name     = "concurrent_sends",
                .data     = &concurrent_sends,
                .mode     = 0444,
                .read     = libcfs_param_intvec_read
        },
        {
                .name     = "ib_mtu",
                .data     = &ib_mtu,
                .mode     = 0444,
                .read     = libcfs_param_intvec_read
        },
        {
                .name     = "map_on_demand",
                .data     = &map_on_demand,
                .mode     = 0444,
                .read     = libcfs_param_intvec_read,
                .write    = libcfs_param_intvec_write,
        },
        {0}
};

void
kiblnd_initstrtunable(char *space, char *str, int size)
{
        strncpy(space, str, size);
        space[size-1] = 0;
}

void
kiblnd_modparams_init(void)
{
        kiblnd_initstrtunable(ipif_basename_space, ipif_name,
                              sizeof(ipif_basename_space));

        libcfs_param_sysctl_init("o2iblnd", libcfs_param_kiblnd_ctl_table,
                                 libcfs_param_lnet_root);

#if defined(CONFIG_SYSCTL) && !CFS_SYSFS_MODULE_PARM
        kiblnd_tunables.kib_sysctl =
                cfs_register_sysctl_table(kiblnd_top_ctl_table, 0);

        if (kiblnd_tunables.kib_sysctl == NULL)
                CWARN("Can't setup /proc tunables\n");
#endif

        kiblnd_plat_modparams_init();
}

void
kiblnd_modparams_fini(void)
{
        kiblnd_plat_modparams_fini();

        libcfs_param_sysctl_fini("o2iblnd", libcfs_param_lnet_root);

#if defined(CONFIG_SYSCTL) && !CFS_SYSFS_MODULE_PARM
        if (kiblnd_tunables.kib_sysctl != NULL)
                cfs_unregister_sysctl_table(kiblnd_tunables.kib_sysctl);
#endif
}

int
kiblnd_tunables_init (void)
{
#if !defined(__sun__)
        kiblnd_modparams_init();
#endif

        if (kiblnd_translate_mtu(*kiblnd_tunables.kib_ib_mtu) < 0) {
                CERROR("Invalid ib_mtu %d, expected 256/512/1024/2048/4096\n",
                       *kiblnd_tunables.kib_ib_mtu);
                return -EINVAL;
        }

        if (*kiblnd_tunables.kib_peertxcredits < IBLND_CREDITS_DEFAULT)
                *kiblnd_tunables.kib_peertxcredits = IBLND_CREDITS_DEFAULT;

        if (*kiblnd_tunables.kib_peertxcredits > IBLND_CREDITS_MAX)
                *kiblnd_tunables.kib_peertxcredits = IBLND_CREDITS_MAX;

        if (*kiblnd_tunables.kib_peertxcredits > *kiblnd_tunables.kib_credits)
                *kiblnd_tunables.kib_peertxcredits = *kiblnd_tunables.kib_credits;

        if (*kiblnd_tunables.kib_peercredits_hiw < *kiblnd_tunables.kib_peertxcredits / 2)
                *kiblnd_tunables.kib_peercredits_hiw = *kiblnd_tunables.kib_peertxcredits / 2;

        if (*kiblnd_tunables.kib_peercredits_hiw >= *kiblnd_tunables.kib_peertxcredits)
                *kiblnd_tunables.kib_peercredits_hiw = *kiblnd_tunables.kib_peertxcredits - 1;

        if (*kiblnd_tunables.kib_map_on_demand < 0 ||
            *kiblnd_tunables.kib_map_on_demand > IBLND_MAX_RDMA_FRAGS)
                *kiblnd_tunables.kib_map_on_demand = 0; /* disable map-on-demand */

        if (*kiblnd_tunables.kib_map_on_demand == 1)
                *kiblnd_tunables.kib_map_on_demand = 2; /* don't make sense to create map if only one fragment */

        if (*kiblnd_tunables.kib_concurrent_sends == 0) {
                if (*kiblnd_tunables.kib_map_on_demand > 0 &&
                    *kiblnd_tunables.kib_map_on_demand <= IBLND_MAX_RDMA_FRAGS / 8)
                        *kiblnd_tunables.kib_concurrent_sends = (*kiblnd_tunables.kib_peertxcredits) * 2;
                else
                        *kiblnd_tunables.kib_concurrent_sends = (*kiblnd_tunables.kib_peertxcredits);
        }

        if (*kiblnd_tunables.kib_concurrent_sends > *kiblnd_tunables.kib_peertxcredits * 2)
                *kiblnd_tunables.kib_concurrent_sends = *kiblnd_tunables.kib_peertxcredits * 2;

        if (*kiblnd_tunables.kib_concurrent_sends < *kiblnd_tunables.kib_peertxcredits / 2)
                *kiblnd_tunables.kib_concurrent_sends = *kiblnd_tunables.kib_peertxcredits / 2;

        if (*kiblnd_tunables.kib_concurrent_sends < *kiblnd_tunables.kib_peertxcredits) {
                CWARN("Concurrent sends %d is lower than message queue size: %d, "
                      "performance may drop slightly.\n",
                      *kiblnd_tunables.kib_concurrent_sends, *kiblnd_tunables.kib_peertxcredits);
        }

        return 0;
}

void
kiblnd_tunables_fini (void)
{
#if !defined(__sun__)
        kiblnd_modparams_fini();
#endif
}
