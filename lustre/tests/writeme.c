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

#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void usage(char *prog)
{
        printf("usage: %s [-s] filename\n", prog);
}

int main(int argc, char **argv)
{
        int fd, rc;
	int do_sync = 0;
        int i = 0;
	int file_arg = 1;
        char buf[4096];

        memset(buf, 0, 4096);

        if (argc < 2 || argc > 3) {
		usage(argv[0]);
                exit(1);
        }

        if (strcmp(argv[1], "-s") == 0) {
                do_sync = 1;
		file_arg++;
        }

        fd = open(argv[file_arg], O_RDWR | O_CREAT, 0600);
        if (fd == -1) {
                printf("Error opening %s\n", argv[1]);
                exit(1);
        }

        while (1) {
                sprintf(buf, "write %d\n", i);
                rc = write(fd, buf, sizeof(buf));
		if (do_sync)
			sync();
                sleep(1);
        }
        return 0;
}
