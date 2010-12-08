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
#include <lustre_disk.h>
#include <lustre_ver.h>

#include <stdio.h>
#include <errno.h>
#include <strings.h>
#include <getopt.h>

#include <sys/fs/zfs.h>
#include <libzfs.h>

#define CMD_MAX 4096

#define INDEX_UNASSIGNED 0xFFFF
#define MO_FORCEFORMAT 0x02

/* used to describe the options to format the lustre disk, not persistent */
struct mkfs_opts {
        struct lustre_disk_data mo_ldd; /* to be written in MOUNT_DATA_FILE */
        char  mo_device[256];           /* disk device name or ZFS objset name */
        char  **mo_pool_vdevs;          /* list of pool vdevs */
        char  mo_mkfsopts[128];         /* options to the backing-store mkfs */
        __u64 mo_device_sz;             /* in KB */
        int   mo_flags;
};

char *progname;
int verbose = 1;
static int print_only = 0;
static int force_zpool = 0;

static void fatal(void)
{
        verbose = 0;
        fprintf(stderr, "\n%s FATAL: ", progname);
}

static void usage(FILE *out)
{
        fprintf(out, "%s v"LUSTRE_VERSION_STRING"\n", progname);
        fprintf(out, "usage: %s <target types> [options] "
                "<pool name>/<dataset name> [[<vdev type>] <device> "
                "[<device> ...] [[vdev type>] ...]]\n", progname);
        fprintf(out,
                "\t<device>:block device or file (e.g /dev/dsk/c0t0d0 or /tmp/ost1)\n"
                "\t<pool name>: name of the ZFS pool where to create the "
                "target (e.g. tank)\n"
                "\t<dataset name>: name of the new dataset (e.g. ost1). The "
                "dataset name must be unique within the ZFS pool\n"
                "\t<vdev type>: type of vdev (mirror, raidz, raidz2, spare, "
                "cache, log)\n"
                "\n"
                "\ttarget types:\n"
                "\t\t--ost: object storage, mutually exclusive with mdt,mgs\n"
                "\t\t--mdt: metadata storage, mutually exclusive with ost\n"
                "\t\t--mgs: configuration management service - one per site\n"
                "\toptions (in order of popularity):\n"
                "\t\t--fsname=<filesystem_name> : default is 'lustre'\n"
                "\t\t--device-size=#N(KB) : device size\n"
                "\t\t--reformat: overwrite an existing dataset\n"
                "\t\t--index=<target index> : default is unassigned\n"
                "\t\t--force-create : force the creation of a ZFS pool\n"
                "\t\t--dryrun: just report what we would do; "
                "don't write to disk\n"
                "\t\t--verbose\n"
                "\t\t--quiet\n"
                "\t\t--mkfsoptions options for zfs create -o\n"
                "\t\t--help\n");
        return;
}

#define vprint if (verbose > 0) printf
#define verrprint if (verbose >= 0) printf

static char *strscat(char *dst, char *src, int buflen) {
        dst[buflen - 1] = 0;
        if (strlen(dst) + strlen(src) >= buflen) {
                fprintf(stderr, "string buffer overflow (max %d): '%s' + '%s'"
                        "\n", buflen, dst, src);
                exit(EOVERFLOW);
        }
        return strcat(dst, src);

}

static char *strscpy(char *dst, char *src, int buflen) {
        dst[0] = 0;
        return strscat(dst, src, buflen);
}

static int prop2nvlist(nvlist_t *props, char *propname)
{
	char *propval;

	if ((propval = strchr(propname, '=')) == NULL) {
		return;
	}
	*propval++ = '\0';
	if (nvlist_add_string(props, propname, propval) != 0) {
                fatal();
		(void) fprintf(stderr, "prop2nvlist: out of memory\n");
		return (1);
	}
	return (0);
}

/* Build fs according to type */
static int make_lustre_backfs(struct mkfs_opts *mop)
{
        char mkfs_cmd[CMD_MAX];
        int ret = 1;
        libzfs_handle_t *g_zfs;
        nvlist_t *props = NULL;

	if ((g_zfs = libzfs_init()) == NULL) {
                fatal();
		(void) fprintf(stderr, "failed to initialize libzfs\n");
		return 1;
	}

        if (mop->mo_pool_vdevs != NULL) {
                char pool_name[128];
                char *sep;

                strncpy(pool_name, mop->mo_device, sizeof(pool_name));
                pool_name[sizeof(pool_name) - 1] = '\0';
                sep = strchr(pool_name, '/');
                if (sep == NULL) {
                        fatal();
                        fprintf(stderr, "Pool name too long: %s...\n",
                            pool_name);
                        goto out;
                }
                sep[0] = '\0';

                /* We are creating a new ZFS pool */
                snprintf(mkfs_cmd, sizeof(mkfs_cmd),
                         "zpool create %s -m legacy %s",
                         force_zpool ? "-f " : "", pool_name);

                /* Add the vdevs to the cmd line */
                while (*mop->mo_pool_vdevs != NULL) {
                        strscat(mkfs_cmd, " ", sizeof(mkfs_cmd));
                        strscat(mkfs_cmd, *mop->mo_pool_vdevs,
                                sizeof(mkfs_cmd));
                        mop->mo_pool_vdevs++; /* point to next vdev */
                }

                vprint("\ncreating ZFS pool '%s'...\n", pool_name);
                vprint("zpool_cmd = '%s'\n", mkfs_cmd);

                ret = system(mkfs_cmd);
                if (ret) {
                        fatal();
                        fprintf(stderr, "Unable to create pool '%s' "
                                "(%d)\n", pool_name, ret);
                        goto out;
                }
        } else if (mop->mo_flags & MO_FORCEFORMAT) {
                zfs_handle_t *zhp;

                vprint("\nDestroying previous filesystem if it exists"
                       " (--reformat was given)...\n");

                zhp = zfs_open(g_zfs, (const char *)mop->mo_device,
                               ZFS_TYPE_FILESYSTEM);
                if (zhp == NULL && (libzfs_errno(g_zfs) != EZFS_NOENT)) {
                        fatal();
                        fprintf(stderr, "Unable to open dataset %s: %s\n",
                                mop->mo_device,
                                libzfs_error_description(g_zfs));
                        goto out;
                }
                
                if (zhp != NULL) {
                        if (zfs_destroy(zhp, B_FALSE)) {
                                fatal();
                                fprintf(stderr,
                                        "Unable to destroy dataset %s: %s\n",
                                        mop->mo_device,
                                        libzfs_error_description(g_zfs));
                                zfs_close(zhp);
                                goto out;
                        }
                        zfs_close(zhp);
                }
        }

        if (nvlist_alloc(&props, NV_UNIQUE_NAME, 0) != 0) {
		(void) fprintf(stderr, "nvlist_alloc failed\n");
                goto out;
	}

        if (mop->mo_mkfsopts[0]) {
                char *s1 = mop->mo_mkfsopts;
                char *s2;
                while (s2 = strchr(s1, ',')) {
                        *s2 = '\0';
                        if (prop2nvlist(props, s1))
                                goto out;
                        s1 = s2 + 1;
                }
                if (*s1) {
                        if (prop2nvlist(props, s1))
                                goto out;
                }
        }

        /* Set the label */
        snprintf(mkfs_cmd, sizeof(mkfs_cmd),
                 "com.sun.lustre:label=%s:%s%04x",
                 mop->mo_ldd.ldd_fsname,
                 mop->mo_ldd.ldd_flags & LDD_F_SV_TYPE_MDT ? "MDT":"OST",
                 mop->mo_ldd.ldd_svindex);

        if (prop2nvlist(props, mkfs_cmd))
            goto out;

        /* Set refquota if --device-size was given */
        if (mop->mo_device_sz != 0) {
                snprintf(mkfs_cmd, sizeof(mkfs_cmd),
                         "refquota="LPU64"K", mop->mo_device_sz);
                if (prop2nvlist(props, mkfs_cmd))
                    goto out;

        }
        snprintf(mkfs_cmd, sizeof(mkfs_cmd), "mountpoint=legacy");
        if (prop2nvlist(props, mkfs_cmd))
                goto out;
        
        vprint("\ncreating ZFS filesystem %s\n"
               "\nThe following nvlist is passed as options\n",
               mop->mo_device);
        if (verbose > 0)
                nvlist_print(stdout, props);
        
        if (zfs_create(g_zfs, mop->mo_device, ZFS_TYPE_FILESYSTEM,
                       props) != 0) {
                fatal();
                fprintf(stderr, "Unable to create filesystem %s: %s\n",
                        mop->mo_device,
                        libzfs_error_description(g_zfs));
                goto out;
        }
        
        ret = 0;

 out:
        nvlist_free(props);
        libzfs_fini(g_zfs);

        return ret;
}

static void set_defaults(struct mkfs_opts *mop)
{
        strcpy(mop->mo_ldd.ldd_fsname, "lustre");
        mop->mo_ldd.ldd_svindex = INDEX_UNASSIGNED;
}

static inline void badopt(const char *opt, char *type)
{
        fprintf(stderr, "%s: '--%s' only valid for %s\n",
                progname, opt, type);
        usage(stderr);
}

static int parse_opts(int argc, char *const argv[], struct mkfs_opts *mop)
{
        static struct option long_opt[] = {
                {"device-size", 1, 0, 'd'},
                {"dryrun", 0, 0, 'n'},
                {"noformat", 0, 0, 'n'},
                {"print", 0, 0, 'n'},
                {"force-create", 0, 0, 'F'},
                {"mgs", 0, 0, 'G'},
                {"mdt", 0, 0, 'M'},
                {"ost", 0, 0, 'O'},
                {"help", 0, 0, 'h'},
                {"index", 1, 0, 'i'},
                {"mkfsoptions", 1, 0, 'k'},
                {"fsname",1, 0, 'L'},
                {"quiet", 0, 0, 'q'},
                {"reformat", 0, 0, 'r'},
                {"verbose", 0, 0, 'v'},
                {0, 0, 0, 0}
        };
        char *optstring = "d:nFGMOhi:k:L:qrv";
        int opt;
        int rc, longidx;

        while ((opt = getopt_long(argc, argv, optstring, long_opt, &longidx)) !=
               EOF) {
                switch (opt) {
                case 'd':
                        mop->mo_device_sz = atol(optarg);
                        break;
                case 'n':
                        print_only++;
                        break;
                case 'F':
                        force_zpool = 1;
                        break;
                case 'G':
                        mop->mo_ldd.ldd_flags |= LDD_F_SV_TYPE_MGS;
                        break;
                case 'M':
                        mop->mo_ldd.ldd_flags |= LDD_F_SV_TYPE_MDT;
                        break;
                case 'O':
                        mop->mo_ldd.ldd_flags |= LDD_F_SV_TYPE_OST;
                        break;
                case 'h':
                        usage(stdout);
                        return 1;
                case 'i':
                        if (IS_MDT(&mop->mo_ldd) || IS_OST(&mop->mo_ldd)) {
                                mop->mo_ldd.ldd_svindex = atol(optarg);
                        } else {
                                badopt(long_opt[longidx].name, "MDT,OST");
                                return 1;
                        }
                        break;
                case 'k':
                        strscpy(mop->mo_mkfsopts, optarg,
                                sizeof(mop->mo_mkfsopts));
                        break;
                case 'L': {
                        char *tmp;
                        if ((strlen(optarg) < 1) || (strlen(optarg) > 8)) {
                                fprintf(stderr, "%s: filesystem name must be "
                                        "1-8 chars\n", progname);
                                return 1;
                        }
                        if ((tmp = strpbrk(optarg, "/:"))) {
                                fprintf(stderr, "%s: char '%c' not allowed in "
                                        "filesystem name\n", progname, *tmp);
                                return 1;
                        }
                        strscpy(mop->mo_ldd.ldd_fsname, optarg,
                                sizeof(mop->mo_ldd.ldd_fsname));
                        break;
                }
                case 'q':
                        verbose--;
                        break;
                case 'r':
                        mop->mo_flags |= MO_FORCEFORMAT;
                        break;
                case 'v':
                        verbose++;
                        break;
                default:
                        if (opt != '?') {
                                fatal();
                                fprintf(stderr, "Unknown option '%c'\n", opt);
                        }
                        return EINVAL;
                }
        }

        if (optind == argc) {
                /* The user didn't specify device name */
                fatal();
                fprintf(stderr, "Not enough arguments - device name or "
                        "pool/dataset name not specified.\n");
                return EINVAL;
        } else {
                /* optind points to device or pool/filesystem name */
                strscpy(mop->mo_device, argv[optind], sizeof(mop->mo_device));
        }

        /* Common mistake: user gave device name instead of pool name */
        if (mop->mo_device[0] == '/') {
                fatal();
                fprintf(stderr, "Pool name cannot start with '/': '%s'"
                        "\nPlease run '%s --help' for syntax help.\n",
                        mop->mo_device, progname);
                return EINVAL;
        }
        if (strchr(mop->mo_device, '/') == NULL) {
                fatal();
                fprintf(stderr, "Incomplete ZFS dataset name: '%s'\n"
                        "Please run '%s --help' for syntax help.\n",
                        mop->mo_device, progname);
                return EINVAL;
        }
        if (strchr(mop->mo_device, '@') != NULL) {
                fatal();
                fprintf(stderr, "Primary dataset name %s cannot include @\n"
                        "@ in the name is reserved for snapshots\n",
                        mop->mo_device);
                return EINVAL;
        }
        /* next index (if existent) points to vdevs */
        if (optind < argc - 1)
                mop->mo_pool_vdevs = (char **) &argv[optind + 1];

        /* single argument: <device> */
        if (argc == 2)
                ++print_only;

        return 0;
}

int main(int argc, char *const argv[])
{
        struct mkfs_opts mop;
        struct lustre_disk_data *ldd;
        int ret;

        if ((progname = strrchr(argv[0], '/')) != NULL)
                progname++;
        else
                progname = argv[0];

        if ((argc < 2) || (argv[argc - 1][0] == '-')) {
                usage(stderr);
                return(EINVAL);
        }

        memset(&mop, 0, sizeof(mop));
        set_defaults(&mop);

        ret = parse_opts(argc, argv, &mop);
        if (ret)
                goto out;

        ldd = &mop.mo_ldd;

        if (!(IS_MDT(ldd) || IS_OST(ldd) || IS_MGS(ldd))) {
                fatal();
                fprintf(stderr, "must set target type: MDT,OST,MGS\n");
                ret = EINVAL;
                goto out;
        }

        if (((IS_MDT(ldd) || IS_MGS(ldd))) && IS_OST(ldd)) {
                fatal();
                fprintf(stderr, "OST type is exclusive with MDT,MGS\n");
                ret = EINVAL;
                goto out;
        }

        server_make_name(ldd->ldd_flags, ldd->ldd_svindex,
                         ldd->ldd_fsname, ldd->ldd_svname);

        if (verbose >= 0)
                printf("Target:     %s\n", ldd->ldd_svname);

        if (print_only) {
                printf("exiting before disk write.\n");
                goto out;
        }

        /* Format the backing filesystem */
        ret = make_lustre_backfs(&mop);
        if (ret != 0) {
                fatal();
                fprintf(stderr, "make_lustre_backfs failed\n");
                return ret;
        }
        return 0;

out:
        verrprint("%s: exiting with %d (%s)\n",
                  progname, ret, strerror(ret));
        return ret;
}
