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
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/utils/lustre_cfg.c
 *
 * Author: Peter J. Braam <braam@clusterfs.com>
 * Author: Phil Schwan <phil@clusterfs.com>
 * Author: Andreas Dilger <adilger@clusterfs.com>
 * Author: Robert Read <rread@clusterfs.com>
 */

#include <stdlib.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include <glob.h>
#include <sys/types.h>

#ifndef __KERNEL__
#include <liblustre.h>
#endif
#include <lustre_lib.h>
#include <lustre_cfg.h>
#include <lustre/lustre_idl.h>
#include <lustre_dlm.h>
#include <obd.h>          /* for struct lov_stripe_md */
#include <obd_lov.h>
#include <lustre/lustre_build_version.h>

#include <unistd.h>
#include <sys/un.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <string.h>


#include "obdctl.h"
#include <lnet/lnetctl.h>
#include <libcfs/libcfsutil.h>
#include <stdio.h>

static char * lcfg_devname;

int lcfg_set_devname(char *name)
{
        if (name) {
                if (lcfg_devname)
                        free(lcfg_devname);
                /* quietly strip the unnecessary '$' */
                if (*name == '$' || *name == '%')
                        name++;
                if (isdigit(*name)) {
                        /* We can't translate from dev # to name */
                        lcfg_devname = NULL;
                } else {
                        lcfg_devname = strdup(name);
                }
        } else {
                lcfg_devname = NULL;
        }
        return 0;
}

char * lcfg_get_devname(void)
{
        return lcfg_devname;
}

int jt_lcfg_device(int argc, char **argv)
{
        return jt_obd_device(argc, argv);
}

int jt_lcfg_attach(int argc, char **argv)
{
        struct lustre_cfg_bufs bufs;
        struct lustre_cfg *lcfg;
        int rc;

        if (argc != 4)
                return CMD_HELP;

        lustre_cfg_bufs_reset(&bufs, NULL);

        lustre_cfg_bufs_set_string(&bufs, 1, argv[1]);
        lustre_cfg_bufs_set_string(&bufs, 0, argv[2]);
        lustre_cfg_bufs_set_string(&bufs, 2, argv[3]);

        lcfg = lustre_cfg_new(LCFG_ATTACH, &bufs);
        rc = lcfg_ioctl(argv[0], OBD_DEV_ID, lcfg);
        lustre_cfg_free(lcfg);
        if (rc < 0) {
                fprintf(stderr, "error: %s: LCFG_ATTACH %s\n",
                        jt_cmdname(argv[0]), strerror(rc = errno));
        } else if (argc == 3) {
                char name[1024];

                lcfg_set_devname(argv[2]);
                if (strlen(argv[2]) > 128) {
                        printf("Name too long to set environment\n");
                        return -EINVAL;
                }
                snprintf(name, 512, "LUSTRE_DEV_%s", argv[2]);
                rc = setenv(name, argv[1], 1);
                if (rc) {
                        printf("error setting env variable %s\n", name);
                }
        } else {
                lcfg_set_devname(argv[2]);
        }

        return rc;
}

int jt_lcfg_setup(int argc, char **argv)
{
        struct lustre_cfg_bufs bufs;
        struct lustre_cfg *lcfg;
        int i;
        int rc;

        if (lcfg_devname == NULL) {
                fprintf(stderr, "%s: please use 'device name' to set the "
                        "device name for config commands.\n",
                        jt_cmdname(argv[0]));
                return -EINVAL;
        }

        lustre_cfg_bufs_reset(&bufs, lcfg_devname);

        if (argc > 6)
                return CMD_HELP;

        for (i = 1; i < argc; i++) {
                lustre_cfg_bufs_set_string(&bufs, i, argv[i]);
        }

        lcfg = lustre_cfg_new(LCFG_SETUP, &bufs);
        rc = lcfg_ioctl(argv[0], OBD_DEV_ID, lcfg);
        lustre_cfg_free(lcfg);
        if (rc < 0)
                fprintf(stderr, "error: %s: %s\n", jt_cmdname(argv[0]),
                        strerror(rc = errno));

        return rc;
}

int jt_obd_detach(int argc, char **argv)
{
        struct lustre_cfg_bufs bufs;
        struct lustre_cfg *lcfg;
        int rc;

        if (lcfg_devname == NULL) {
                fprintf(stderr, "%s: please use 'device name' to set the "
                        "device name for config commands.\n",
                        jt_cmdname(argv[0]));
                return -EINVAL;
        }

        lustre_cfg_bufs_reset(&bufs, lcfg_devname);

        if (argc != 1)
                return CMD_HELP;

        lcfg = lustre_cfg_new(LCFG_DETACH, &bufs);
        rc = lcfg_ioctl(argv[0], OBD_DEV_ID, lcfg);
        lustre_cfg_free(lcfg);
        if (rc < 0)
                fprintf(stderr, "error: %s: %s\n", jt_cmdname(argv[0]),
                        strerror(rc = errno));

        return rc;
}

int jt_obd_cleanup(int argc, char **argv)
{
        struct lustre_cfg_bufs bufs;
        struct lustre_cfg *lcfg;
        char force = 'F';
        char failover = 'A';
        char flags[3] = { 0 };
        int flag_cnt = 0, n;
        int rc;

        if (lcfg_devname == NULL) {
                fprintf(stderr, "%s: please use 'device name' to set the "
                        "device name for config commands.\n",
                        jt_cmdname(argv[0]));
                return -EINVAL;
        }

        lustre_cfg_bufs_reset(&bufs, lcfg_devname);

        if (argc < 1 || argc > 3)
                return CMD_HELP;

        /* we are protected from overflowing our buffer by the argc
         * check above
         */
        for (n = 1; n < argc; n++) {
                if (strcmp(argv[n], "force") == 0) {
                        flags[flag_cnt++] = force;
                } else if (strcmp(argv[n], "failover") == 0) {
                        flags[flag_cnt++] = failover;
                } else {
                        fprintf(stderr, "unknown option: %s", argv[n]);
                        return CMD_HELP;
                }
        }

        if (flag_cnt) {
                lustre_cfg_bufs_set_string(&bufs, 1, flags);
        }

        lcfg = lustre_cfg_new(LCFG_CLEANUP, &bufs);
        rc = lcfg_ioctl(argv[0], OBD_DEV_ID, lcfg);
        lustre_cfg_free(lcfg);
        if (rc < 0)
                fprintf(stderr, "error: %s: %s\n", jt_cmdname(argv[0]),
                        strerror(rc = errno));

        return rc;
}

static
int do_add_uuid(char * func, char *uuid, lnet_nid_t nid)
{
        int rc;
        struct lustre_cfg_bufs bufs;
        struct lustre_cfg *lcfg;

        lustre_cfg_bufs_reset(&bufs, lcfg_devname);
        if (uuid)
                lustre_cfg_bufs_set_string(&bufs, 1, uuid);

        lcfg = lustre_cfg_new(LCFG_ADD_UUID, &bufs);
        lcfg->lcfg_nid = nid;
        /* Poison NAL -- pre 1.4.6 will LASSERT on 0 NAL, this way it
           doesn't work without crashing (bz 10130) */
        lcfg->lcfg_nal = 0x5a;

#if 0
        fprintf(stderr, "adding\tnid: %d\tuuid: %s\n",
               lcfg->lcfg_nid, uuid);
#endif
        rc = lcfg_ioctl(func, OBD_DEV_ID, lcfg);
        lustre_cfg_free(lcfg);
        if (rc) {
                fprintf(stderr, "IOC_PORTAL_ADD_UUID failed: %s\n",
                        strerror(errno));
                return -1;
        }

        printf ("Added uuid %s: %s\n", uuid, libcfs_nid2str(nid));
        return 0;
}

int jt_lcfg_add_uuid(int argc, char **argv)
{
        lnet_nid_t nid;

        if (argc != 3) {
                return CMD_HELP;
        }

        nid = libcfs_str2nid(argv[2]);
        if (nid == LNET_NID_ANY) {
                fprintf (stderr, "Can't parse NID %s\n", argv[2]);
                return (-1);
        }

        return do_add_uuid(argv[0], argv[1], nid);
}

int obd_add_uuid(char *uuid, lnet_nid_t nid)
{
        return do_add_uuid("obd_add_uuid", uuid, nid);
}

int jt_lcfg_del_uuid(int argc, char **argv)
{
        int rc;
        struct lustre_cfg_bufs bufs;
        struct lustre_cfg *lcfg;

        if (argc != 2) {
                fprintf(stderr, "usage: %s <uuid>\n", argv[0]);
                return 0;
        }

        lustre_cfg_bufs_reset(&bufs, lcfg_devname);
        if (strcmp (argv[1], "_all_"))
                lustre_cfg_bufs_set_string(&bufs, 1, argv[1]);

        lcfg = lustre_cfg_new(LCFG_DEL_UUID, &bufs);
        rc = lcfg_ioctl(argv[0], OBD_DEV_ID, lcfg);
        lustre_cfg_free(lcfg);
        if (rc) {
                fprintf(stderr, "IOC_PORTAL_DEL_UUID failed: %s\n",
                        strerror(errno));
                return -1;
        }
        return 0;
}

int jt_lcfg_del_mount_option(int argc, char **argv)
{
        int rc;
        struct lustre_cfg_bufs bufs;
        struct lustre_cfg *lcfg;

        if (argc != 2)
                return CMD_HELP;

        lustre_cfg_bufs_reset(&bufs, lcfg_devname);

        /* profile name */
        lustre_cfg_bufs_set_string(&bufs, 1, argv[1]);

        lcfg = lustre_cfg_new(LCFG_DEL_MOUNTOPT, &bufs);
        rc = lcfg_ioctl(argv[0], OBD_DEV_ID, lcfg);
        lustre_cfg_free(lcfg);
        if (rc < 0) {
                fprintf(stderr, "error: %s: %s\n", jt_cmdname(argv[0]),
                        strerror(rc = errno));
        }
        return rc;
}

int jt_lcfg_set_timeout(int argc, char **argv)
{
        fprintf(stderr, "%s has been deprecated. Use conf_param instead.\n"
                "e.g. conf_param sys.testfs.obd_timeout=50\n",
                jt_cmdname(argv[0]));
        return CMD_HELP;
}

int jt_lcfg_add_conn(int argc, char **argv)
{
        struct lustre_cfg_bufs bufs;
        struct lustre_cfg *lcfg;
        int priority;
        int rc;

        if (argc == 2)
                priority = 0;
        else if (argc == 3)
                priority = 1;
        else
                return CMD_HELP;

        if (lcfg_devname == NULL) {
                fprintf(stderr, "%s: please use 'device name' to set the "
                        "device name for config commands.\n",
                        jt_cmdname(argv[0]));
                return -EINVAL;
        }

        lustre_cfg_bufs_reset(&bufs, lcfg_devname);

        lustre_cfg_bufs_set_string(&bufs, 1, argv[1]);

        lcfg = lustre_cfg_new(LCFG_ADD_CONN, &bufs);
        lcfg->lcfg_num = priority;

        rc = lcfg_ioctl(argv[0], OBD_DEV_ID, lcfg);
        lustre_cfg_free (lcfg);
        if (rc < 0) {
                fprintf(stderr, "error: %s: %s\n", jt_cmdname(argv[0]),
                        strerror(rc = errno));
        }

        return rc;
}

int jt_lcfg_del_conn(int argc, char **argv)
{
        struct lustre_cfg_bufs bufs;
        struct lustre_cfg *lcfg;
        int rc;

        if (argc != 2)
                return CMD_HELP;

        if (lcfg_devname == NULL) {
                fprintf(stderr, "%s: please use 'device name' to set the "
                        "device name for config commands.\n",
                        jt_cmdname(argv[0]));
                return -EINVAL;
        }

        lustre_cfg_bufs_reset(&bufs, lcfg_devname);

        /* connection uuid */
        lustre_cfg_bufs_set_string(&bufs, 1, argv[1]);

        lcfg = lustre_cfg_new(LCFG_DEL_MOUNTOPT, &bufs);

        rc = lcfg_ioctl(argv[0], OBD_DEV_ID, lcfg);
        lustre_cfg_free(lcfg);
        if (rc < 0) {
                fprintf(stderr, "error: %s: %s\n", jt_cmdname(argv[0]),
                        strerror(rc = errno));
        }

        return rc;
}

/* Param set locally, directly on target */
int jt_lcfg_param(int argc, char **argv)
{
        int i, rc;
        struct lustre_cfg_bufs bufs;
        struct lustre_cfg *lcfg;

        if (argc >= LUSTRE_CFG_MAX_BUFCOUNT)
                return CMD_HELP;

        lustre_cfg_bufs_reset(&bufs, NULL);

        for (i = 1; i < argc; i++) {
                lustre_cfg_bufs_set_string(&bufs, i, argv[i]);
        }

        lcfg = lustre_cfg_new(LCFG_PARAM, &bufs);

        rc = lcfg_ioctl(argv[0], OBD_DEV_ID, lcfg);
        lustre_cfg_free(lcfg);
        if (rc < 0) {
                fprintf(stderr, "error: %s: %s\n", jt_cmdname(argv[0]),
                        strerror(rc = errno));
        }
        return rc;
}

/* Could this element of a parameter be an obd type?
 * returns boolean
 */
static int element_could_be_obd(char *el)
{
        char *ptr = el;

        /* Rather than try to enumerate known obd types and risk
         * becoming stale, I'm just going to check for no wacky chars */
        while ((*ptr != '\0') && (*ptr != '.')) {
                if (!isalpha(*ptr++))
                        return 0;
        }
        return 1;
}

/* Convert set_param into conf_param format.  Examples of differences:
 * conf_param testfs.sys.at_max=1200
 * set_param at_max=1200   -- no fsname, but conf_param needs a valid one
 * conf_param lustre.llite.max_read_ahead_mb=16
 * set_param llite.lustre-ffff81003f157000.max_read_ahead_mb=16
 * conf_param lustre-MDT0000.lov.stripesize=2M
 * set_param lov.lustre-MDT0000-mdtlov.stripesize=2M
 * set_param lov.lustre-clilov-ffff81003f157000.stripesize=2M  --  clilov
 * conf_param lustre-OST0001.osc.active=0
 * set_param osc.lustre-OST0000-osc-ffff81003f157000.active=0
 * conf_param lustre-OST0000.osc.max_dirty_mb=29.15
 * set_param osc.lustre-OST0000-osc-ffff81003f157000.max_dirty_mb=16
 * conf_param lustre-OST0001.ost.client_cache_seconds=15
 * set_param obdfilter.lustre-OST0001.client_cache_seconds=15  --  obdfilter/ost
 * conf_param testfs-OST0000.failover.node=1.2.3.4@tcp1
 * no proc, but osc.testfs-OST0000.failover.node -- would be appropriate
 */
static int rearrange_setparam_syntax(char *in)
{
        char buf[MGS_PARAM_MAXLEN];
        char *element[3];
        int elements = 0;
        int dev, obd;
        char *ptr, *value;
        __u32 index;
        int type;
        int rc;

        value = strchr(in, '=');
        if (!value)
                return -EINVAL;
        *value = '\0';

        /* Separate elements 0.1.all_the_rest */
        element[elements++] = in;
        for (ptr = in; *ptr != '\0' && (elements < 3); ptr++) {
                if (*ptr == '.') {
                        *ptr = '\0';
                        element[elements++] = ++ptr;
                }
        }
        if (elements != 3) {
                fprintf(stderr, "error: Parameter format is "
                        "<obd>.<fsname|devname>.<param>.\n"
                        "Wildcards are not supported. Examples:\n"
                        "sys.testfs.at_max=1200\n"
                        "llite.testfs.max_read_ahead_mb=16\n"
                        "lov.testfs-MDT0000.qos_threshold_rr=30\n"
                        "mdc.testfs-MDT0000.max_rpcs_in_flight=6\n"
                        "osc.testfs-OST0000.active=0\n"
                        "osc.testfs-OST0000.max_dirty_mb=16\n"
                        "obdfilter.testfs-OST0001.client_cache_seconds=15\n"
                        "osc.testfs-OST0000.failover.node=1.2.3.4@tcp\n\n"
                        );
                return -EINVAL;
        }

        /* e.g. testfs-OST003f-junk.ost.param */
        rc = libcfs_str2server(element[0], &type, &index, &ptr);
        if (rc == 0) {
                *ptr = '\0'; /* trunc the junk */
                goto out0;
        }
        /* e.g. ost.testfs-OST003f-junk.param */
        rc = libcfs_str2server(element[1], &type, &index, &ptr);
        if (rc == 0) {
                *ptr = '\0';
                goto out1;
        }

        /* llite.fsname.param or fsname.obd.param */
        if (!element_could_be_obd(element[0]) &&
            element_could_be_obd(element[1]))
                /* fsname-junk.obd.param */
                goto out0;
        if (element_could_be_obd(element[0]) &&
            !element_could_be_obd(element[1]))
                /* obd.fsname-junk.param */
                goto out1;
        if (!element_could_be_obd(element[0]) &&
            !element_could_be_obd(element[1])) {
                fprintf(stderr, "error: Parameter format is "
                        "<obd>.<fsname|devname>.<param>\n");
                return -EINVAL;
        }
        /* Either element could be obd.  Assume set_param syntax
         * (obd.fsname.param) */
        goto out1;

out0:
        dev = 0;
        obd = 1;
        goto out;
out1:
        dev = 1;
        obd = 0;
out:
        /* Don't worry Mom, we'll check it out */
        if (strncmp(element[2], "failover", 8) != 0) { /* no proc for this */
                char *argt[3];

                if (strcmp(element[obd], "sys") == 0)
                        sprintf(buf, "%s", element[2]);
                else
                        sprintf(buf, "%s.%s*.%s", element[obd], element[dev],
                                element[2]);
                argt[1] = "-q";
                argt[2] = buf;
                rc = jt_lcfg_listparam(3, argt);
                if (rc)
                        fprintf(stderr, "warning: can't find local param '%s'\n"
                                "(but that service may not be running locally)."
                                "\n", buf);
        }

        /* s/obdfilter/ost/ */
        if (strcmp(element[obd], "obdfilter") == 0)
                sprintf(element[obd], "ost");

        sprintf(buf, "%s.%s.%s=%s", element[dev], element[obd],
                element[2], value + 1);
        strcpy(in, buf);

        return 0;
}

/* Param set in config log on MGS */
/* conf_param key=value */
/* Note we can actually send mgc conf_params from clients, but currently
 * that's only done for default file striping (see ll_send_mgc_param),
 * and not here. */
/* After removal of a parameter (-d) Lustre will use the default
 * AT NEXT REBOOT, not immediately. */
int jt_lcfg_mgsparam(int argc, char **argv)
{
        int rc;
        int del = 0;
        struct lustre_cfg_bufs bufs;
        struct lustre_cfg *lcfg;
        char buf[MGS_PARAM_MAXLEN];

        /* mgs_setparam processes only lctl buf #1 */
        if ((argc > 3) || (argc <= 1))
                return CMD_HELP;

        while ((rc = getopt(argc, argv, "d")) != -1) {
                switch (rc) {
                        case 'd':
                                del = 1;
                                break;
                        default:
                                return CMD_HELP;
                }
        }

        if (del) {
                char *ptr;

                /* for delete, make it "<param>=\0" */
                /* put an '=' on the end in case it doesn't have one */
                sprintf(buf, "%s=", argv[optind]);
                /* then truncate after the first '=' */
                ptr = strchr(buf, '=');
                *(++ptr) = '\0';
        } else {
                sprintf(buf, "%s", argv[optind]);
        }

        rc = rearrange_setparam_syntax(buf);
        if (rc)
                return CMD_HELP;

        lustre_cfg_bufs_reset(&bufs, NULL);
        lustre_cfg_bufs_set_string(&bufs, 1, buf);

        /* We could put other opcodes here. */
        lcfg = lustre_cfg_new(LCFG_PARAM, &bufs);

        rc = lcfg_mgs_ioctl(argv[0], OBD_DEV_ID, lcfg);
        lustre_cfg_free(lcfg);
        if (rc < 0) {
                fprintf(stderr, "error: %s: %s\n", jt_cmdname(argv[0]),
                        strerror(rc = errno));
                if (rc == ENOENT) {
                        char *argt[3];
                        fprintf(stderr, "Does this filesystem/target exist on "
                                "the MGS?\n");
                        printf("Known targets:\n");
                        sprintf(buf, "mgs.MGS.live.*");
                        argt[1] = "-n";
                        argt[2] = buf;
                        jt_lcfg_getparam(3, argt);
                }
        }

        return rc;
}

struct params_opts{
        int show_path:1;
        int only_path:1;
        int show_type:1;
        int recursive:1;
};

static void strrpl(char *str, char src, char dst)
{
        if (str == NULL)
                return;
        while (*str != '\0') {
                if (*str == src)
                        *str = dst;
                str ++;
        }
}

static void append_filetype(char *filename, mode_t mode)
{
        /* append the indicator to entries
         * /: directory
         * @: symlink
         * =: writable file
         */
        if (S_ISDIR(mode))
                filename[strlen(filename)] = '/';
        else if (S_ISLNK(mode))
                filename[strlen(filename)] = '@';
        else if (mode & S_IWUSR)
                filename[strlen(filename)] = '=';
}


/* Display the path in the same format as sysctl
 * For eg. obdfilter.lustre-OST0000.stats */
static char *display_name(char *filename, mode_t mode, int show_type)
{
        char *tmp;

        if (strncmp(filename, PTREE_PREFIX, PTREE_PRELEN) == 0) {
                filename += PTREE_PRELEN;
        }
        if (strncmp(filename, "lustre/", strlen("lustre/")) == 0)
                filename += strlen("lustre/");
        else if (strncmp(filename, "lnet/", strlen("lnet/")) == 0)
                filename += strlen("lnet/");

        /* replace '/' with '.' to match conf_param and sysctl */
        tmp = filename;
        strrpl(tmp, '/', '.');

        if (show_type)
                append_filetype(filename, mode);

        return filename;
}

static void params_show(int show_path, char *buf)
{
        char outbuf[CFS_PAGE_SIZE];
        int rc = 0, pos = 0;

        memset(outbuf, 0, CFS_PAGE_SIZE);
        while ((rc = params_unpack(buf + pos, outbuf, sizeof(outbuf)))) {
                /* start from a new line if there are multi-lines */
                if (show_path &&
                    strstr(outbuf, "\n") != (outbuf + strlen(outbuf) - 1))
                        printf("\n%s", outbuf);
                else
                        printf("%s", outbuf);
                pos += rc;
        }
}

static int listparam_display(struct params_opts *popt, char *pattern)
{
        int rc = 0;
        char filename[PATH_MAX + 1];    /* extra 1 byte for file type */
        struct params_entry_list *pel = NULL;
        struct params_entry_list *pel_head = NULL;

        if ((rc = params_list(pattern, &pel)) < 0)
                goto out;
        pel_head = pel;
        pel = pel_head->pel_next;
        while (pel) {
                char *valuename = NULL;

                memset(filename, 0, PATH_MAX + 1);
                strcpy(filename, pel->pel_name);
                if ((valuename = display_name(filename, pel->pel_mode,
                                              popt->show_type))) {
                        printf("%s\n", valuename);
                        if (popt->recursive && S_ISDIR(pel->pel_mode)) {
                                /* Here, we have to convert '.' to '/' again,
                                 * same as what we did in get_patter().
                                 * Because, to list param recursively,
                                 * the matched params will be added "/*",
                                 * and then passed again.
                                 * For example,
                                 * $lctl list_param -R ost.*
                                 * Round1. get ost.num_refs and ost.OSS;
                                 * Round2. try to list ost.OSS/*, but this '.'
                                 * will be treated as a wildcard, not a separator.
                                 */
                                strrpl(valuename, '.', '/');
                                strcat(valuename, "/*");
                                listparam_display(popt, valuename);
                        }
                }
                pel = pel->pel_next;
        }
out:
        if (pel_head)
                params_free_entrylist(pel_head);

        return rc;
}

static int getparam_display(struct params_opts *popt, char *pattern)
{
        int rc = 0;
        char *buf = NULL;
        char filename[PATH_MAX + 1];    /* extra 1 byte for file type */
        long long offset = 0;
        int eof = 0;
        int params_count = 0;
        struct params_entry_list *pel = NULL;
        struct params_entry_list *pel_head = NULL;

        if ((rc = params_list(pattern, &pel)) < 0)
                goto out;
        pel_head = pel;
        pel = pel_head->pel_next;
        buf = malloc(CFS_PAGE_SIZE);
        while (pel) {
                char *valuename = NULL;
                mode_t mode = pel->pel_mode;

                memset(filename, 0, PATH_MAX + 1);
                strcpy(filename, pel->pel_name);
                if (popt->only_path) {
                        valuename = display_name(filename, mode,
                                                 popt->show_type);
                        if (valuename)
                                printf("%s\n", valuename);
                        goto next;
                }
                if (!(mode & S_IRUSR)) {
                        fprintf(stderr, "No read permission.\n");
                        goto next;
                }
                if (!(valuename = display_name(filename, mode, 0)))
                        goto next;
                if (S_ISDIR(mode) || S_ISLNK(mode)) {
                        fprintf(stderr, "error: get_param: "
                                "parameter '%s' is a directory.\n", valuename);
                        goto next;
                }
                eof = 0;
                offset = 0;
                if (popt->show_path)
                        printf("%s=", valuename);
                while (!eof) {
                        memset(buf, 0, CFS_PAGE_SIZE);
                        params_count =
                                params_read(pel->pel_name + PTREE_PRELEN,
                                            pel->pel_name_len - PTREE_PRELEN,
                                            buf, CFS_PAGE_SIZE, &offset, &eof);
                        if (params_count > 0) {
                                params_show(popt->show_path, buf);
                                /* usually, offset is set only in seq_read,
                                 * but not in common read cb. */
                        } else if (params_count == 0) {
                                if (popt->show_path)
                                        printf("\n");
                                break;
                        } else if (params_count < 0){
                                fprintf(stderr, "error: get_param: read value"
                                        "failed (%d).\n", params_count);
                                break;
                        }
                }
next:
                pel = pel->pel_next;
        }
out:
        if (buf)
                free(buf);
        if (pel_head)
                params_free_entrylist(pel_head);
        return rc;
}

static int setparam_display(struct params_opts *popt, char *pattern,
                            char *value)
{
        int rc = 0;
        char filename[PATH_MAX + 1];    /* extra 1 byte for file type */
        int offset = 0;
        struct params_entry_list *pel = NULL;
        struct params_entry_list *pel_head = NULL;

        if ((rc = params_list(pattern, &pel)) < 0)
                goto out;
        pel_head = pel;
        pel = pel_head->pel_next;
        while (pel) {
                char *valuename = NULL;
                mode_t mode = pel->pel_mode;

                memset(filename, 0, PATH_MAX + 1);
                strcpy(filename, pel->pel_name);
                valuename = display_name(filename, mode, 0);
                if (valuename == NULL)
                        goto next;
                if (S_ISDIR(mode)) {
                        fprintf(stderr, "error: set_param: "
                                "parameter '%s' is a directory.\n", valuename);
                        goto next;
                }
                if (!(mode & S_IWUSR)) {
                        fprintf(stderr, "No write permission.\n");
                        goto next;
                }
                offset = 0;
                rc = params_write(pel->pel_name + PTREE_PRELEN,
                                  pel->pel_name_len - PTREE_PRELEN,
                                  value, strlen(value), offset);
                if (rc >= 0 && popt->show_path)
                        printf("%s=%s\n", valuename, value);
next:
                pel = pel->pel_next;
        }
out:
        rc = rc > 0 ? 0 : rc;
        if (pel_head)
                params_free_entrylist(pel_head);

        return rc;
}

static int params_cmdline(int argc, char **argv, char *options,
                         struct params_opts *popt)
{
        int ch;

        popt->show_path = 1;
        popt->only_path = 0;
        popt->show_type = 0;
        popt->recursive = 0;

        while ((ch = getopt(argc, argv, options)) != -1) {
                switch(ch) {
                        case 'N':
                                popt->only_path = 1;
                                break;
                        case 'n':
                                popt->show_path = 0;
                        case 'F':
                                popt->show_type = 1;
                                break;
                        case 'R':
                                popt->recursive = 1;
                                break;
                        default:
                                return -1;
                }
        }

        return optind;
}

static void get_pattern(char *path, char *pattern)
{
        char *tmp;

        /* If the input is in form Eg. obdfilter.*.stats */
        /* We use '.' as the directory separator.
         * But sometimes '.' is part of the parameter name, e.g.
         * "ldlm.*.MGC10.10.121.1@tcp", which will cause error.
         * XXX: can we use other separator, or avoid '.' as part of the name?
         */
        if (strchr(path, '.')) {
                tmp = path;
                strrpl(tmp, '.', '/');
        }
        if (strchr(path, '#')) {
                tmp = path;
                strrpl(tmp, '#', '.');
        }
        strcpy(pattern, path);
}

int jt_lcfg_listparam(int argc, char **argv)
{
        int rc = 0, i;
        struct params_opts popt;
        char pattern[PATH_MAX];

        rc = params_cmdline(argc, argv, "FR", &popt);
        if (rc == argc && popt.recursive) {
                /* if '-R' is given without a path, list all params.
                 * Let's overwrite  the last param with '*' and
                 * use that for a path. */
                rc --;  /* we know at least "-R" is a parameter */
                argv[rc] = "*";
        } else if (rc < 0 || rc >= argc) {
                return CMD_HELP;
        }

        for (i = rc; i < argc; i++) {
                get_pattern(argv[i], pattern);
                rc = listparam_display(&popt, pattern);
                if (rc < 0)
                        return rc;
        }

        return 0;
}

int jt_lcfg_getparam(int argc, char **argv)
{
        int rc = 0, i;
        struct params_opts popt;
        char pattern[PATH_MAX];

        rc = params_cmdline(argc, argv, "nNF", &popt);
        if (rc < 0 || rc >= argc)
                return CMD_HELP;

        for (i = rc; i < argc; i++) {
                get_pattern(argv[i], pattern);
                rc = getparam_display(&popt, pattern);
                if (rc < 0)
                        return rc;
        }

        return 0;
}

int jt_lcfg_setparam(int argc, char **argv)
{
        int rc = 0, i;
        struct params_opts popt;
        char pattern[PATH_MAX];
        char *path = NULL, *value = NULL;

        rc = params_cmdline(argc, argv, "n", &popt);
        if (rc < 0 || rc >= argc)
                return CMD_HELP;

        for (i = rc; i < argc; i++) {
                if ((value = strchr(argv[i], '=')) != NULL) {
                        /* format: set_param a=b */
                        *value = '\0';
                        value ++;
                        path = argv[i];
                } else {
                        /* format: set_param a b */
                        if (path == NULL) {
                                path = argv[i];
                                continue;
                        } else {
                                value = argv[i];
                        }
                }
                get_pattern(path, pattern);
                rc = setparam_display(&popt, pattern, value);
                path = NULL;
                value = NULL;
                if (rc < 0)
                        return rc;
        }

        return 0;
}
