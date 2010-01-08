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
 * lustre/utils/req-layout.c
 *
 * User-level tool for printing request layouts
 *
 * Author: Nikita Danilov <nikita@clusterfs.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <liblustre.h>
#include <lustre/lustre_idl.h>

#define __REQ_LAYOUT_USER__ (1)

#define lustre_swab_generic_32s NULL
#define lustre_swab_lu_seq_range NULL
#define lustre_swab_md_fld NULL
#define lustre_swab_mdt_body NULL
#define lustre_swab_mdt_ioepoch NULL
#define lustre_swab_ptlrpc_body NULL
#define lustre_swab_obd_statfs NULL
#define lustre_swab_connect NULL
#define lustre_swab_ldlm_request NULL
#define lustre_swab_ldlm_reply NULL
#define lustre_swab_ldlm_intent NULL
/* #define lustre_swab_lov_mds_md NULL */
#define lustre_swab_mdt_rec_reint NULL
#define lustre_swab_lustre_capa NULL
#define lustre_swab_lustre_capa_key NULL
#define lustre_swab_llogd_conn_body NULL
#define lustre_swab_llog_hdr NULL
#define lustre_swab_llogd_body NULL
#define lustre_swab_obd_quotactl NULL
#define lustre_swab_quota_adjust_qunit NULL
#define lustre_swab_mgs_target_info NULL
#define lustre_swab_niobuf_remote NULL
#define lustre_swab_obd_ioobj NULL
#define lustre_swab_ost_body NULL
#define lustre_swab_ost_last_id NULL
#define lustre_swab_fiemap NULL
#define lustre_swab_qdata NULL
#define lustre_swab_ost_lvb NULL
#define dump_rniobuf NULL
#define dump_ioo NULL
#define dump_obdo NULL
#define dump_ost_body NULL
#define dump_rcs NULL

/*
 * Yes, include .c file.
 */
#include "../ptlrpc/layout.c"

void usage(void)
{
        fprintf(stderr, "req-layout -- prints lustre request layouts\n");
}

void printt_field(const char *prefix, const struct req_msg_field *fld)
{
}

void print_layout(const struct req_format *rf)
{
        int j;
        int k;

        int offset;
        int variable;

        static const char *prefix[RCL_NR] = {
                [RCL_CLIENT] = "C",
                [RCL_SERVER] = "S"
        };

        printf("L %s (%i/%i)\n", rf->rf_name,
               rf->rf_fields[RCL_CLIENT].nr, rf->rf_fields[RCL_SERVER].nr);

        for (j = 0; j < RCL_NR; ++j) {
                offset = 0;
                variable = 0;
                for (k = 0; k < rf->rf_fields[j].nr; ++k) {
                        const struct req_msg_field *fld;

                        fld = rf->rf_fields[j].d[k];

                        printf("        F%s %i [%3.3i%s %-20.20s (",
                               prefix[j], k, offset,
                               variable ? " + ...]" : "]      ",
                               fld->rmf_name);
                        if (fld->rmf_size > 0) {
                                printf("%3.3i) ", fld->rmf_size);
                                offset += fld->rmf_size;
                        } else {
                                printf("var) ");
                                variable = 1;
                        }
                        if (fld->rmf_flags & RMF_F_STRING)
                                printf("string");
                        printf("\n");
                }
                if (k > 0 && j != RCL_NR - 1)
                        printf("        -----------------------------------\n");
        }
}

void print_layouts(void)
{
        int i;

        for (i = 0; i < ARRAY_SIZE(req_formats); ++i) {
                print_layout(req_formats[i]);
                printf("\n");
        }
}

int main(int argc, char **argv)
{
        int opt;
        int verbose;

        verbose = 0;
        do {
                opt = getopt(argc, argv, "hb:k:r:p:v");
                switch (opt) {
                case 'v':
                        verbose++;
                case -1:
                        break;
                case '?':
                default:
                        fprintf(stderr, "Unable to parse options.");
                case 'h':
                        usage();
                        return 0;
                }
        } while (opt != -1);
        print_layouts();
        return 0;
}
