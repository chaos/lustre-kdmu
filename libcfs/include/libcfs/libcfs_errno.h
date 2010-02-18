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
 * libcfs/include/libcfs/libcfs_errno.h
 *
 */

#ifndef __LIBCFS_LIBCFS_ERRNO_H__
#define __LIBCFS_LIBCFS_ERRNO_H__

/*
 * Canonical errno values based on Linux ones
 */

/* from Linux asm-generic/errno-base.h */

#define CFS_EPERM            1      /* Operation not permitted */
#define CFS_ENOENT           2      /* No such file or directory */
#define CFS_ESRCH            3      /* No such process */
#define CFS_EINTR            4      /* Interrupted system call */
#define CFS_EIO              5      /* I/O error */
#define CFS_ENXIO            6      /* No such device or address */
#define CFS_E2BIG            7      /* Argument list too long */
#define CFS_ENOEXEC          8      /* Exec format error */
#define CFS_EBADF            9      /* Bad file number */
#define CFS_ECHILD          10      /* No child processes */
#define CFS_EAGAIN          11      /* Try again */
#define CFS_ENOMEM          12      /* Out of memory */
#define CFS_EACCES          13      /* Permission denied */
#define CFS_EFAULT          14      /* Bad address */
#define CFS_ENOTBLK         15      /* Block device required */
#define CFS_EBUSY           16      /* Device or resource busy */
#define CFS_EEXIST          17      /* File exists */
#define CFS_EXDEV           18      /* Cross-device link */
#define CFS_ENODEV          19      /* No such device */
#define CFS_ENOTDIR         20      /* Not a directory */
#define CFS_EISDIR          21      /* Is a directory */
#define CFS_EINVAL          22      /* Invalid argument */
#define CFS_ENFILE          23      /* File table overflow */
#define CFS_EMFILE          24      /* Too many open files */
#define CFS_ENOTTY          25      /* Not a typewriter */
#define CFS_ETXTBSY         26      /* Text file busy */
#define CFS_EFBIG           27      /* File too large */
#define CFS_ENOSPC          28      /* No space left on device */
#define CFS_ESPIPE          29      /* Illegal seek */
#define CFS_EROFS           30      /* Read-only file system */
#define CFS_EMLINK          31      /* Too many links */
#define CFS_EPIPE           32      /* Broken pipe */
#define CFS_EDOM            33      /* Math argument out of domain of func */
#define CFS_ERANGE          34      /* Math result not representable */

/* from Linux asm-generic/errno.h */

#define CFS_EDEADLK         35      /* Resource deadlock would occur */
#define CFS_ENAMETOOLONG    36      /* File name too long */
#define CFS_ENOLCK          37      /* No record locks available */
#define CFS_ENOSYS          38      /* Function not implemented */
#define CFS_ENOTEMPTY       39      /* Directory not empty */
#define CFS_ELOOP           40      /* Too many symbolic links encountered */
#define CFS_EWOULDBLOCK     11      /* Operation would block */
#define CFS_ENOMSG          42      /* No message of desired type */
#define CFS_EIDRM           43      /* Identifier removed */
#define CFS_ECHRNG          44      /* Channel number out of range */
#define CFS_EL2NSYNC        45      /* Level 2 not synchronized */
#define CFS_EL3HLT          46      /* Level 3 halted */
#define CFS_EL3RST          47      /* Level 3 reset */
#define CFS_ELNRNG          48      /* Link number out of range */
#define CFS_EUNATCH         49      /* Protocol driver not attached */
#define CFS_ENOCSI          50      /* No CSI structure available */
#define CFS_EL2HLT          51      /* Level 2 halted */
#define CFS_EBADE           52      /* Invalid exchange */
#define CFS_EBADR           53      /* Invalid request descriptor */
#define CFS_EXFULL          54      /* Exchange full */
#define CFS_ENOANO          55      /* No anode */
#define CFS_EBADRQC         56      /* Invalid request code */
#define CFS_EBADSLT         57      /* Invalid slot */
#define CFS_EDEADLOCK       35
#define CFS_EBFONT          59      /* Bad font file format */
#define CFS_ENOSTR          60      /* Device not a stream */
#define CFS_ENODATA         61      /* No data available */
#define CFS_ETIME           62      /* Timer expired */
#define CFS_ENOSR           63      /* Out of streams resources */
#define CFS_ENONET          64      /* Machine is not on the network */
#define CFS_ENOPKG          65      /* Package not installed */
#define CFS_EREMOTE         66      /* Object is remote */
#define CFS_ENOLINK         67      /* Link has been severed */
#define CFS_EADV            68      /* Advertise error */
#define CFS_ESRMNT          69      /* Srmount error */
#define CFS_ECOMM           70      /* Communication error on send */
#define CFS_EPROTO          71      /* Protocol error */
#define CFS_EMULTIHOP       72      /* Multihop attempted */
#define CFS_EDOTDOT         73      /* RFS specific error */
#define CFS_EBADMSG         74      /* Not a data message */
#define CFS_EOVERFLOW       75      /* Value too large for defined data type */
#define CFS_ENOTUNIQ        76      /* Name not unique on network */
#define CFS_EBADFD          77      /* File descriptor in bad state */
#define CFS_EREMCHG         78      /* Remote address changed */
#define CFS_ELIBACC         79      /* Can not access a needed shared library */
#define CFS_ELIBBAD         80      /* Accessing a corrupted shared library */
#define CFS_ELIBSCN         81      /* .lib section in a.out corrupted */
#define CFS_ELIBMAX         82      /* Attempting to link in too many shared libraries */
#define CFS_ELIBEXEC        83      /* Cannot exec a shared library directly */
#define CFS_EILSEQ          84      /* Illegal byte sequence */
#define CFS_ERESTART        85      /* Interrupted system call should be restarted */
#define CFS_ESTRPIPE        86      /* Streams pipe error */
#define CFS_EUSERS          87      /* Too many users */
#define CFS_ENOTSOCK        88      /* Socket operation on non-socket */
#define CFS_EDESTADDRREQ    89      /* Destination address required */
#define CFS_EMSGSIZE        90      /* Message too long */
#define CFS_EPROTOTYPE      91      /* Protocol wrong type for socket */
#define CFS_ENOPROTOOPT     92      /* Protocol not available */
#define CFS_EPROTONOSUPPORT 93      /* Protocol not supported */
#define CFS_ESOCKTNOSUPPORT 94      /* Socket type not supported */
#define CFS_EOPNOTSUPP      95      /* Operation not supported on transport endpoint */
#define CFS_EPFNOSUPPORT    96      /* Protocol family not supported */
#define CFS_EAFNOSUPPORT    97      /* Address family not supported by protocol */
#define CFS_EADDRINUSE      98      /* Address already in use */
#define CFS_EADDRNOTAVAIL   99      /* Cannot assign requested address */
#define CFS_ENETDOWN        100     /* Network is down */
#define CFS_ENETUNREACH     101     /* Network is unreachable */
#define CFS_ENETRESET       102     /* Network dropped connection because of reset */
#define CFS_ECONNABORTED    103     /* Software caused connection abort */
#define CFS_ECONNRESET      104     /* Connection reset by peer */
#define CFS_ENOBUFS         105     /* No buffer space available */
#define CFS_EISCONN         106     /* Transport endpoint is already connected */
#define CFS_ENOTCONN        107     /* Transport endpoint is not connected */
#define CFS_ESHUTDOWN       108     /* Cannot send after transport endpoint shutdown */
#define CFS_ETOOMANYREFS    109     /* Too many references: cannot splice */
#define CFS_ETIMEDOUT       110     /* Connection timed out */
#define CFS_ECONNREFUSED    111     /* Connection refused */
#define CFS_EHOSTDOWN       112     /* Host is down */
#define CFS_EHOSTUNREACH    113     /* No route to host */
#define CFS_EALREADY        114     /* Operation already in progress */
#define CFS_EINPROGRESS     115     /* Operation now in progress */
#define CFS_ESTALE          116     /* Stale NFS file handle */
#define CFS_EUCLEAN         117     /* Structure needs cleaning */
#define CFS_ENOTNAM         118     /* Not a XENIX named type file */
#define CFS_ENAVAIL         119     /* No XENIX semaphores available */
#define CFS_EISNAM          120     /* Is a named type file */
#define CFS_EREMOTEIO       121     /* Remote I/O error */
#define CFS_EDQUOT          122     /* Quota exceeded */
#define CFS_ENOMEDIUM       123     /* No medium found */
#define CFS_EMEDIUMTYPE     124     /* Wrong medium type */
#define CFS_ECANCELED       125     /* Operation Canceled */
#define CFS_ENOKEY          126     /* Required key not available */
#define CFS_EKEYEXPIRED     127     /* Key has expired */
#define CFS_EKEYREVOKED     128     /* Key has been revoked */
#define CFS_EKEYREJECTED    129     /* Key was rejected by service */
#define CFS_EOWNERDEAD      130     /* Owner died */
#define CFS_ENOTRECOVERABLE 131     /* State not recoverable */

/* from Linux errno.h */

#define CFS_ERESTARTSYS     512
#define CFS_ERESTARTNOINTR  513
#define CFS_ERESTARTNOHAND  514     /* restart if no handler.. */
#define CFS_ENOIOCTLCMD     515     /* No ioctl command */
#define CFS_ERESTART_RESTARTBLOCK 516 /* restart by calling sys_restart_syscall */
#define CFS_EBADHANDLE      521     /* Illegal NFS file handle */
#define CFS_ENOTSYNC        522     /* Update synchronization mismatch */
#define CFS_EBADCOOKIE      523     /* Cookie is stale */
#define CFS_ENOTSUPP        524     /* Operation is not supported */
#define CFS_ETOOSMALL       525     /* Buffer or request is too small */
#define CFS_ESERVERFAULT    526     /* An untranslatable error occurred */
#define CFS_EBADTYPE        527     /* Type not supported by server */
#define CFS_EJUKEBOX        528     /* Request initiated, but will not complete before timeout */
#define CFS_EIOCBQUEUED     529     /* iocb queued, will get completion event */
#define CFS_EIOCBRETRY      530     /* iocb queued, will trigger a retry */
#define CFS_EWOULDBLOCKIO   530     /* Would block due to block-IO */

#define CFS_MAX_ERRNO       1024

#define CFS_EUNKNOWN        (CFS_MAX_ERRNO - 1)

/*
 * The following error code definitions are not defined by Solaris OS
 * but are used by Lustre code. Map them to unique codes for Solaris to
 * distinguish them from all other error types during c2p conversion.
 */
#ifndef ENAVAIL
#define ENAVAIL             900
#endif

#ifndef ENOMEDIUM
#define ENOMEDIUM           901
#endif

#ifndef ERESTARTSYS
#define ERESTARTSYS         902
#endif

#ifndef EREMOTEIO
#define EREMOTEIO           903
#endif

#ifndef ENOTSUPP
# ifdef ENOTSUP
#  define ENOTSUPP          ENOTSUP
# else
#  error Unsupported operating system
# endif
#endif

/*
 * errno conversion: all lustre modules MUST ensure that only
 * canonical errno traverses through the wire.
 * 
 * p2c stands for platform2canonical
 * c2p stands for canonical2platform
 */

extern int cfs_errno_c2p_table[CFS_MAX_ERRNO];
extern int cfs_errno_p2c_table[CFS_MAX_ERRNO];

static inline int
cfs_errno_p2c(int err)
{
        int rc;
        
        LASSERT (err >= 0 && err < CFS_MAX_ERRNO);

        rc = cfs_errno_p2c_table[err];

        LASSERT (err == 0 || rc != 0);
        LASSERT (err != 0 || rc == 0);

        return rc;
}

static inline int
cfs_errno_c2p(int err)
{
        int rc;
        
        LASSERT (err >= 0 && err < CFS_MAX_ERRNO);

        rc = cfs_errno_c2p_table[err];

        LASSERT (err == 0 || rc != 0);
        LASSERT (err != 0 || rc == 0);

        return rc;
}

/*
 * Mapping unknown errors to CFS_EUNKNOWN
 */
static inline void
cfs_errno_tables_init(void)
{
        int i;
        for (i = 1; i < CFS_MAX_ERRNO; i++) {

                if (cfs_errno_c2p_table[i] == 0)
                        cfs_errno_c2p_table[i] = CFS_EUNKNOWN;

                if (cfs_errno_p2c_table[i] == 0)
                        cfs_errno_p2c_table[i] = CFS_EUNKNOWN;
        }
}

#endif /* _LIBCFS_ERRNO_H */
