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
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pwd.h>
#include <grp.h>

void
usage (char *argv0, int help)
{
	char *progname = strrchr(argv0, '/');

	if (progname == NULL)
		progname = argv0;

	fprintf (help ? stdout : stderr,
		 "Usage: %s [flags] file[s]\n",
		 progname);

	if (!help)
	{
		fprintf (stderr, "   or try '-h' for help\n");
		exit (1);
	}

	printf ("Check given files have...\n");
	printf (" -p    permission       file must have required permissions\n");
	printf (" -t    dir|file|link    file must be of the specified type\n");
	printf (" -l    link_name        file must be a link to the given name\n");
	printf (" -s    size             file must have the given size\n");
	printf (" -u    user             file must be owned by given user\n");
	printf (" -g    group            file must be owned by given group\n");
	printf (" -f                     follow symlinks\n");
	printf (" -a                     file must be absent\n");
	printf (" -v                     increase verbosity\n");
	printf (" -h                     print help\n");
	printf (" Exit status is 0 on success, 1 on failure\n");
}

int
main (int argc, char **argv)
{
	int           c;
	struct stat64 buf;
	int           perms = -1;
	uid_t         uid = (uid_t)-1;
	gid_t         gid = (gid_t)-1;
	char         *type = NULL;
	long          absent = 0;
	char         *checklink = NULL;
	int           verbose = 0;
	long long     size = -1;
	int           follow = 0;
	char         *term;

	while ((c = getopt (argc, argv, "p:t:l:s:u:g:avfh")) != -1)
		switch (c)
		{
		case 'p':
			perms = (int)strtol (optarg, &term, 0);
			if (term == optarg)
			{
				fprintf (stderr, "Can't parse permission %s\n", optarg);
				return (1);
			}
			break;

		case 'l':
			checklink = optarg;
			break;

		case 's':
			size = strtoll (optarg, &term, 0);
			if (term == optarg)
			{
				fprintf (stderr, "Can't parse size %s\n", optarg);
				return (1);
			}
			break;

		case 'u':
			if (*optarg == '#')
			{
				uid = (uid_t)strtol (optarg + 1, &term, 0);
				if (term == optarg + 1)
				{
					fprintf (stderr, "Can't parse numeric uid %s\n", optarg);
					return (1);
				}
			} else {
				struct passwd *pw = getpwnam (optarg);

				if (pw == NULL)
				{
					fprintf (stderr, "Can't find user %s\n", optarg);
					return (1);
				}
				uid = pw->pw_uid;
			}
			break;

		case 'g':
			if (*optarg == '#')
			{
				gid = (gid_t)strtol (optarg + 1, &term, 0);
				if (term == optarg + 1)
				{
					fprintf (stderr, "Can't parse numeric gid %s\n", optarg);
					return (1);
				}
			} else {
				struct group *gr = getgrnam (optarg);

				if (gr == NULL)
				{
					fprintf (stderr, "Can't find group %s\n", optarg);
					return (1);
				}
				uid = gr->gr_gid;
			}
			break;

		case 't':
			type = optarg;
			break;

		case 'a':
			absent = 1;
			break;

		case 'v':
			verbose++;
			break;

		case 'f':
			follow++;
			break;

		case 'h':
			usage (argv[0], 1);
			return (0);

		default:
			usage (argv[0], 0);
		}

	if (optind == argc)
		usage (argv[0], 0);

	do
	{
		char *fname = argv[optind];
		int rc = follow ? stat64 (fname, &buf) : lstat64 (fname, &buf);

		if (rc != 0)
		{
			if (!(absent && errno == ENOENT))
			{
				if (verbose)
					printf ("Can't %sstat %s: %s\n",
						follow ? "" : "l",
						fname, strerror (errno));
				return (1);
			}

			if (verbose)
				printf ("%s: absent OK\n", fname);
			continue;
		}

		if (absent)
		{
			if (verbose)
				printf ("%s exists\n", fname);
			return (1);
		}

		if (type != NULL)
		{
			if (!strcmp (type, "d") ||
			    !strcmp (type, "dir"))
			{
				if (!S_ISDIR (buf.st_mode))
				{
					if (verbose)
						printf ("%s is not a directory\n",
							 fname);
					return (1);
				}
			}
			else if (!strcmp (type, "f") ||
				 !strcmp (type, "file"))
			{
				if (!S_ISREG (buf.st_mode))
				{
					if (verbose)
						printf ("%s is not a regular file\n",
							fname);
					return (1);
				}
			}
			else if (!strcmp (type, "l") ||
				 !strcmp (type, "link"))
			{
				if (!S_ISLNK (buf.st_mode))
				{
					if (verbose)
						printf ("%s is not a link\n",
							fname);
					return (1);
				}
			}
			else
			{
				fprintf (stderr, "Can't parse file type %s\n",
					 type);
				return (1);
			}

			if (verbose)
				printf ("%s has type %s OK\n", fname, type);
		}

		if (perms != -1)
		{
			if ((buf.st_mode & ~S_IFMT) != perms)
			{
				if (verbose)
					printf ("%s has perms 0%o, not 0%o\n",
						fname, (buf.st_mode & ~S_IFMT),
						perms);
				return (1);
			}

			if (verbose)
				printf ("%s has perms 0%o OK\n",
					fname, perms);
		}

		if (size != -1)
		{
			if (buf.st_size != size)
			{
				if (verbose)
					printf ("%s has size %Ld, not %Ld\n",
						fname, (long long)buf.st_size,
						size);
				return (1);
			}

			if (verbose)
				printf ("%s has size %Ld OK\n", fname, size);
		}

		if (checklink != NULL)
		{
			static char lname[4<<10];

			rc = readlink (fname, lname, sizeof (lname) - 1);

			if (rc < 0)
			{
				if (verbose)
					printf ("%s: can't read link: %s\n",
						fname, strerror (errno));
				return (1);
			}

			lname[rc] = 0;
			if (strcmp (checklink, lname))
			{
				if (verbose)
					printf ("%s is a link to %s and not %s\n",
						fname, lname, checklink);
				return (1);
			}

			if (verbose)
				printf ("%s links to %s OK\n", fname, checklink);
		}

		if (uid != (uid_t)-1)
		{
			if (buf.st_uid != uid)
			{
				if (verbose)
					printf ("%s is owned by user #%ld and not #%ld\n",
						fname, (long)buf.st_uid, (long)uid);
				return (1);
			}

			if (verbose)
				printf ("%s is owned by user #%ld OK\n",
					fname, (long)uid);
		}

		if (gid != (gid_t)-1)
		{
			if (buf.st_gid != gid)
			{
				if (verbose)
					printf ("%s is owned by group #%ld and not #%ld\n",
						fname, (long)buf.st_gid, (long)gid);
				return (1);
			}

			if (verbose)
				printf ("%s is owned by group #%ld OK\n",
					fname, (long)gid);
		}
	} while (++optind < argc);

	return (0);
}
