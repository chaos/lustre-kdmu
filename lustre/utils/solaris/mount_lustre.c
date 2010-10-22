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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <libcfs/libcfs.h>
#include <lustre_ver.h>
#include <lustre_param.h>

#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <strings.h>
#include <sys/mount.h>
#include <sys/mntent.h>

#define MAX_RETRIES 99

int          fake = 0;
int          verbose = 0;
int          retry = 0;
char         *progname = NULL;
int          have_mgsnid = 0;

static void usage(FILE *out)
{
        fprintf(out, "%s v"LUSTRE_VERSION_STRING"\n", progname);
        fprintf(out, "\nThis mount helper should only be invoked via the "
                "mount (1M) command,\n mount -F lustrefs [-m] [-o mntopt] "
                "<dataset> <mountpoint>\n\n");
        fprintf(out,
                "\t<dataset>: ZFS dataset name\n"
                "\t<mountpoint>: filesystem mountpoint (e.g. /mnt/ost1)\n"
                "\t-m: do not update /etc/mnttab after mount\n"
                "\t<mntopt>: one or more comma separated of:\n"
                "\t\tmgsnode=<mgsnid>\n"
                "\t\t(no)flock,(no)user_xattr,(no)acl\n"
                "\t\tabort_recov: abort server recovery handling\n"
                "\t\tnosvc: only start MGC/MGS obds\n"
                "\t\tnomgs: only start target obds, using existing MGS\n"
                "\t\texclude=<ostname>[:<ostname>] : colon-separated list of "
                "inactive OSTs (e.g. lustre-OST0001)\n"
                "\t\tretry=<num>: number of times mount is retried by client\n"
                "\t\tverbose: print verbose config settings\n"
                "\t\tfake: dry-run, don't mount\n"
                );
        exit((out != stdout) ? EINVAL : 0);
}

static void append_option(char *options, const char *one)
{
        if (*options)
                strcat(options, ",");
        strcat(options, one);
}

static void append_mgsnid(char *options, const char *val)
{
        append_option(options, PARAM_MGSNODE);
        strcat(options, val);
        have_mgsnid++;
}

/* Replace options with subset of Lustre-specific options */
static int parse_options(char *orig_options)
{
        char *options, *opt, *nextopt, *arg, *val;
        char *mgsopt;

        options = calloc(strlen(orig_options) + 1, 1);
        if (options == NULL) {
                fprintf(stderr, "%s: parse_options: can't allocate "
                        "options\n", progname);
                return ENOMEM;
        }

        nextopt = orig_options;
        while ((opt = strsep(&nextopt, ","))) {
                if (!*opt)
                        /* empty option */
                        continue;

                /* Handle retries in a slightly different
                 * manner */
                arg = opt;
                val = strchr(opt, '=');
                if (val && strncmp(arg, "retry", 5) == 0) {
                        retry = atoi(val + 1);
                        if (retry > MAX_RETRIES)
                                retry = MAX_RETRIES;
                        else if (retry < 0)
                                retry = 0;
                } else if (strncmp(opt, "verbose", 7) == 0) {
                        verbose++;
                } else if (strncmp(opt, "fake", 4) == 0) {
                        fake++;
                } else if (val && strncmp(opt, PARAM_MGSNODE,
                                          sizeof(PARAM_MGSNODE)-1) == 0) {
                        append_mgsnid(options, val + 1);
                } else {
                         append_option(options, opt);
                }
        }

        strcpy(orig_options, options);
        free(options);

        return 0;
}

int main(int argc, char *const argv[])
{
        int flags = MS_OPTIONSTR;
        char *source;
        char target[PATH_MAX] = {'\0'};
        char *ptr;
        char *options, *optcopy, *orig_options = NULL;
        int i, opt, rc, optlen;

        progname = strrchr(argv[0], '/');
        progname = progname ? progname + 1 : argv[0];

        while ((opt = getopt(argc, argv, "?mo:")) != EOF){
                switch (opt) {
                case '?':
                        usage(stderr);
                        break;
                case 'm':
                        flags |= MS_NOMNTTAB;
                        break;
                case 'o':
                        orig_options = optarg;
                        break;
                default:
                        fprintf(stderr, "%s: unknown option '%c'\n",
                                progname, opt);
                        usage(stderr);
                        break;
                }
        }

        if (orig_options == NULL) {
                fprintf(stderr, "%s: -o mgsnode=<mgsnid> is missing\n", progname);
                usage(stderr);
        }

        if (optind + 2 != argc) {
                fprintf(stderr, "%s: too few arguments\n", progname);
                usage(stderr);
        }

        source = argv[optind];

        if (realpath(argv[optind + 1], target) == NULL) {
                rc = errno;
                fprintf(stderr, "warning: %s: cannot resolve: %s\n",
                        argv[optind + 1], strerror(errno));
                return rc;
        }

        options = malloc(strlen(orig_options) + 1);
        if (options == NULL) {
                fprintf(stderr, "can't allocate memory for options\n");
                return ENOMEM;
        }
        strcpy(options, orig_options);
        if (rc = parse_options(options))
                return rc;
        if (verbose) {
                for (i = 0; i < argc; i++)
                        printf("arg[%d] = %s\n", i, argv[i]);
                printf("source = %s, target = %s\n", source, target);
                printf("options = %s\n", orig_options);
        }
        if (!have_mgsnid) {
                fprintf(stderr, "%s: -o mgsnode=<mgsnid> is missing\n", progname);
                usage(stderr);
        }

        rc = access(target, F_OK);
        if (rc) {
                rc = errno;
                fprintf(stderr, "%s: %s inaccessible: %s\n", progname, target,
                        strerror(errno));
                return rc;
        }

        optlen = strlen(options) + strlen(",device=") + strlen(source) + 1;
        optcopy = malloc(optlen);
        if (optcopy == NULL) {
                fprintf(stderr, "can't allocate memory to optcopy\n");
                return ENOMEM;
        }
        strcpy(optcopy, options);
        if (*optcopy)
                strcat(optcopy, ",");
        strcat(optcopy, "device=");
        strcat(optcopy, source);

        if (verbose)
                printf("mounting device %s at %s, flags=%#x options=%s\n",
                       source, target, flags, optcopy);

        if (!fake) {
                for (i = 0, rc = EAGAIN; i <= retry && rc != 0; i++) {
                        rc = mount(source, target, flags, "lustrefs",
                                   NULL, 0, optcopy, optlen);
                        if (rc) {
                                rc = errno;
                                if (verbose) {
                                        fprintf(stderr, "%s: mount %s at %s "
                                                "failed: %s retries left: "
                                                "%d\n", progname,
                                                source, target,
                                                strerror(rc), retry-i);
                                }
                                if (retry)
                                        sleep(1 << max((i/2), 5));
                        }
                }
        }

        if (rc) {
                fprintf(stderr, "%s: mount %s at %s failed: %s\n", progname,
                        source, target, strerror(errno));
                if (errno == ENODEV)
                        fprintf(stderr, "Does ZFS dataset %s exist?\n", source);
                if (errno == ENOENT) {
                        fprintf(stderr, "Is the MGS specification correct?\n");
                        fprintf(stderr, "Is the filesystem name correct?\n");
                }
                if (errno == EALREADY)
                        fprintf(stderr, "The target service is already running."
                                " (%s)\n", source);
                if (errno == ENXIO)
                        fprintf(stderr, "The target service failed to start "
                                "See /var/adm/messages.\n");
                if (errno == EIO)
                        fprintf(stderr, "Is the MGS running?\n");
                if (errno == EADDRINUSE)
                        fprintf(stderr, "The target service's index is already "
                                "in use. (%s)\n", source);
                if (errno == EINVAL) {
                        fprintf(stderr, "This may have multiple causes.\n");
                        fprintf(stderr, "Are the mount options correct?\n");
                        fprintf(stderr, "Check the syslog for more info.\n");
                }

        }

        free(optcopy);
        return rc;
}
