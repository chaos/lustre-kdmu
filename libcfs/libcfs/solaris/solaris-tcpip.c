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
 * Copyright  2009 Sun Microsystems, Inc. All rights reserved
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * libcfs/libcfs/solaris/solaris-tcpip.c
 *
 */

#define DEBUG_SUBSYSTEM S_LNET

#include <libcfs/libcfs.h>

#include <sys/ksynch.h>
#include <sys/sockio.h> /* SIOCGLIFFLAGS, etc. */
#include <sys/filio.h> /* FIONBIO */
#include <net/if.h> /* struct lifreq */

/* Internal function for ipif_query and ipif_enumerate */
static int
libcfs_sock_ioctl(int cmd, intptr_t arg)
{
        ksocket_t sock;
        int       rc;
        int       rval;
        
        rc = ksocket_socket(&sock,
                            AF_INET, SOCK_STREAM, 0, KSOCKET_SLEEP, CRED());
        if (rc != 0) {
                CERROR ("Can't create socket: %d\n", rc);
                return -rc;
        }

        /* Looking into opensolaris sources, it seems that rval
         * doesn't matter: if all is OK, it's 0; and if an error
         * occured, it comes in iocbp->ioc_error and we'll get it
         * as rc
         */
        rc = ksocket_ioctl(sock, cmd, arg, &rval, CRED());

        ksocket_close(sock, CRED());

        return -rc;
}

int
libcfs_ipif_query(char *name, int *up, __u32 *ip, __u32 *mask)
{
        struct lifreq lifr;
        int           nob;
        int           rc;
        __u32         val;

        nob = strnlen(name, LIFNAMSIZ);
        if (nob == LIFNAMSIZ) {
                CERROR("Interface name %s too long\n", name);
                return -EINVAL;
        }

        CLASSERT (sizeof(lifr.lifr_name) >= LIFNAMSIZ);

        memset(&lifr, 0, sizeof(lifr));
        strcpy(lifr.lifr_name, name);
        rc = libcfs_sock_ioctl(SIOCGLIFFLAGS, (intptr_t)&lifr);

        if (rc != 0) {
                CERROR("Can't get flags for interface %s\n", name);
                return rc;
        }

        if ((lifr.lifr_flags & IFF_UP) == 0) {
                CDEBUG(D_NET, "Interface %s down\n", name);
                *up = 0;
                *ip = *mask = 0;
                return 0;
        }

        *up = 1;

        strcpy(lifr.lifr_name, name);
        rc = libcfs_sock_ioctl(SIOCGLIFADDR, (intptr_t)&lifr);

        if (rc != 0) {
                CERROR("Can't get IP address for interface %s\n", name);
                return rc;
        }

        val = ((struct sockaddr_in *)&lifr.lifr_addr)->sin_addr.s_addr;
        *ip = ntohl(val);

        strcpy(lifr.lifr_name, name);
        rc = libcfs_sock_ioctl(SIOCGLIFNETMASK, (intptr_t)&lifr);

        if (rc != 0) {
                CERROR("Can't get netmask for interface %s\n", name);
                return rc;
        }

        val = ((struct sockaddr_in *)&lifr.lifr_addr)->sin_addr.s_addr;
        *mask = ntohl(val);

        return 0;
}

int
libcfs_ipif_enumerate(char ***namesp)
{
        /* Allocate and fill in 'names', returning # interfaces/error */
        char          **names;
        int             nalloc;
        int             nfound;
        struct lifreq  *lifr;
        struct lifnum   lifn;
        struct lifconf  lifc;
        int             rc;
        int             nob;
        int             i;
        int64_t         lflags = LIFC_NOXMIT | LIFC_TEMPORARY | LIFC_ALLZONES;

        lifn.lifn_family = AF_INET;
        lifn.lifn_flags  = lflags;

        rc = libcfs_sock_ioctl(SIOCGLIFNUM, (unsigned long)&lifn);

        if (rc < 0) {
                CERROR ("Error %d getting number of interfaces\n", rc);
                return rc;
        }

        nalloc = lifn.lifn_count;
        LASSERT (nalloc >= 0);
        if (nalloc == 0)
                return 0;

        LIBCFS_ALLOC(lifr, nalloc * sizeof(*lifr));
        if (lifr == NULL) {
                CERROR ("ENOMEM enumerating up to %d interfaces\n", nalloc);
                rc = -ENOMEM;
                goto out0;
        }

        lifc.lifc_family = AF_INET;
        lifc.lifc_flags  = lflags;
        lifc.lifc_buf = (char *)lifr;
        lifc.lifc_len = nalloc * sizeof(*lifr);

        rc = libcfs_sock_ioctl(SIOCGLIFCONF, (intptr_t)&lifc);

        if (rc < 0) {
                CERROR ("Error %d enumerating interfaces\n", rc);
                goto out1;
        }

        LASSERT (rc == 0);

        nfound = lifc.lifc_len/sizeof(*lifr);
        LASSERT (nfound <= nalloc);

        if (nfound == 0)
                goto out1;

        LIBCFS_ALLOC(names, nfound * sizeof(*names));
        if (names == NULL) {
                rc = -ENOMEM;
                goto out1;
        }
        /* NULL out all names[i] */
        memset (names, 0, nfound * sizeof(*names));

        for (i = 0; i < nfound; i++) {

                nob = strnlen (lifr[i].lifr_name, LIFNAMSIZ);
                if (nob == LIFNAMSIZ) {
                        /* no space for terminating NULL */
                        CERROR("interface name %.*s too long (%d max)\n",
                               nob, lifr[i].lifr_name, LIFNAMSIZ);
                        rc = -ENAMETOOLONG;
                        goto out2;
                }

                LIBCFS_ALLOC(names[i], LIFNAMSIZ);
                if (names[i] == NULL) {
                        rc = -ENOMEM;
                        goto out2;
                }

                memcpy(names[i], lifr[i].lifr_name, nob);
                names[i][nob] = 0;
        }

        *namesp = names;
        rc = nfound;

 out2:
        if (rc < 0)
                libcfs_ipif_free_enumeration(names, nfound);
 out1:
        LIBCFS_FREE(lifr, nalloc * sizeof(*lifr));
 out0:
        return rc;
}

void
libcfs_ipif_free_enumeration(char **names, int n)
{
        int      i;

        LASSERT (n > 0);

        for (i = 0; i < n && names[i] != NULL; i++)
                LIBCFS_FREE(names[i], LIFNAMSIZ);

        LIBCFS_FREE(names, n * sizeof(*names));
}

static inline void
libcfs_sock_notifier_init(cfs_sock_notifier_t *ntf)
{
        mutex_init(&ntf->sntf_lock, NULL, MUTEX_DRIVER, NULL);
        cv_init(&ntf->sntf_cv, NULL, CV_DRIVER, NULL);
        ntf->sntf_state = CFS_SNTF_EMPTY;
        ntf->sntf_cpuid = 0;
}

static inline void
libcfs_sock_notifier_fini(cfs_sock_notifier_t *ntf)
{
        mutex_destroy(&ntf->sntf_lock);
        cv_destroy(&ntf->sntf_cv);
}

static inline void
libcfs_sock_notifier_twait(cfs_sock_notifier_t *ntf, int timeout, int *bflag_p)
{
        int     rc          = 0;
        clock_t finish_time = lbolt + timeout * hz;

        if (finish_time <= 0)
                finish_time = CFS_MAX_SCHEDULE_TIMEOUT;
        
        mutex_enter(&ntf->sntf_lock);
        while (ntf->sntf_state == CFS_SNTF_EMPTY && rc != -1)
                rc = cv_timedwait(&ntf->sntf_cv, &ntf->sntf_lock, finish_time);

        *bflag_p = ntf->sntf_state & CFS_SNTF_BREAK;
        
        ntf->sntf_state = CFS_SNTF_EMPTY;
        mutex_exit(&ntf->sntf_lock);
}

static inline void
libcfs_sock_notifier_wait(cfs_sock_notifier_t *ntf, int *bflag_p)
{
        mutex_enter(&ntf->sntf_lock);
        while (ntf->sntf_state == CFS_SNTF_EMPTY)
                cv_wait(&ntf->sntf_cv, &ntf->sntf_lock);
        
        *bflag_p = ntf->sntf_state & CFS_SNTF_BREAK;
        
        ntf->sntf_state = CFS_SNTF_EMPTY;
        mutex_exit(&ntf->sntf_lock);
}

static void
read_cb_handler(ksocket_t sock, ksocket_callback_event_t ev, void *arg,
                uintptr_t evarg)
{
        cfs_sock_notifier_t *ntf = (cfs_sock_notifier_t *)arg;

        mutex_enter(&ntf->sntf_lock);
        
        switch (ev) {
        case KSOCKET_EV_NEWDATA:
                ntf->sntf_state |= CFS_SNTF_READY;
                break;

        case KSOCKET_EV_DISCONNECTED:
        case KSOCKET_EV_CANTRECVMORE:
                ntf->sntf_state |= CFS_SNTF_BREAK;
                break;

        default:
                LBUG();
        }

        cv_broadcast(&ntf->sntf_cv);
        mutex_exit(&ntf->sntf_lock);
}

static ksocket_callbacks_t read_callbacks = {
        .ksock_cb_flags        = KSOCKET_CB_NEWDATA |
                                 KSOCKET_CB_DISCONNECTED |
                                 KSOCKET_CB_CANTRECVMORE,
        .ksock_cb_newdata      = read_cb_handler,
        .ksock_cb_disconnected = read_cb_handler,
        .ksock_cb_cantrecvmore = read_cb_handler,
};

int
libcfs_sock_read(cfs_socket_t *csockp, void *buffer, int nob, int timeout)
{
        cfs_time_t          start_time = cfs_time_current();
        int                 break_flag = 0;
        int                 rc;
        size_t              recvd;
        cfs_sock_notifier_t ntf;

        libcfs_sock_notifier_init(&ntf);

        rc = ksocket_setcallbacks(csockp->csock_sock, &read_callbacks, &ntf,
                                  CRED());
        if (rc != 0) {
                rc = -rc;
                goto sock_read_done;
        }        

        while (nob != 0 && timeout > 0 && !break_flag) {

                rc = ksocket_recv(csockp->csock_sock, buffer, nob,
                                  MSG_DONTWAIT, &recvd, CRED());

                if (rc == 0) { /* probably read something */
                        if (recvd == 0) {
                                rc = -EIO;
                                goto sock_read_done;
                        }

                        buffer = ((char *)buffer) + recvd;
                        nob -= recvd;

                        if (nob == 0) /* nothing to wait */
                                break;

                } else if (rc != EWOULDBLOCK) { /* real error occured */
                        rc = -rc;
                        goto sock_read_done;
                }

                /* rc == EWOULDBLOCK */
                libcfs_sock_notifier_twait(&ntf, timeout, &break_flag);

                timeout -= cfs_duration_sec(cfs_time_sub(cfs_time_current(),
                                                         start_time));
                start_time = cfs_time_current();
        }

        if (nob != 0) {
                if (break_flag)
                        rc = -EIO;
                else if (timeout <= 0)
                        rc = -ETIMEDOUT;
                else
                        LBUG();
        }

  sock_read_done:
        ksocket_setcallbacks(csockp->csock_sock, NULL, NULL, CRED());
        libcfs_sock_notifier_fini(&ntf);
        return rc;
}

static void
write_cb_handler(ksocket_t sock, ksocket_callback_event_t ev, void *arg,
                 uintptr_t evarg)
{
        cfs_sock_notifier_t *ntf = (cfs_sock_notifier_t *)arg;

        mutex_enter(&ntf->sntf_lock);
        
        switch (ev) {
        case KSOCKET_EV_CANSEND:
                ntf->sntf_state |= CFS_SNTF_READY;
                break;

        case KSOCKET_EV_DISCONNECTED:
        case KSOCKET_EV_CANTSENDMORE:
                ntf->sntf_state |= CFS_SNTF_BREAK;
                break;

        default:
                LBUG();
        }

        cv_broadcast(&ntf->sntf_cv);
        mutex_exit(&ntf->sntf_lock);
}

static ksocket_callbacks_t write_callbacks = {
        .ksock_cb_flags        = KSOCKET_CB_CANSEND |
                                 KSOCKET_CB_DISCONNECTED |
                                 KSOCKET_CB_CANTSENDMORE,
        .ksock_cb_cansend      = write_cb_handler,
        .ksock_cb_disconnected = write_cb_handler,
        .ksock_cb_cantsendmore = write_cb_handler,
};

int
libcfs_sock_write(cfs_socket_t *csockp, void *buffer, int nob, int timeout)
{
        cfs_time_t          start_time = cfs_time_current();
        int                 break_flag = 0;
        int                 rc;
        size_t              sent;
        cfs_sock_notifier_t ntf;

        libcfs_sock_notifier_init(&ntf);
        
        rc = ksocket_setcallbacks(csockp->csock_sock, &write_callbacks, &ntf,
                                  CRED());
        if (rc != 0) {
                rc = -rc;
                goto sock_write_done;
        }        

        while (nob != 0 && timeout > 0 && !break_flag) {
                
                rc = ksocket_send(csockp->csock_sock, buffer, nob,
                                  MSG_DONTWAIT, &sent, CRED());
                if (rc == 0) { /* sent something */
                        LASSERT (sent != 0);

                        buffer = ((char *)buffer) + sent;
                        nob -= sent;

                        if (nob == 0) /* nothing to wait */
                                goto sock_write_done;
                        
                } else if (rc != EWOULDBLOCK) { /* real error occured */
                        rc = -rc;
                        goto sock_write_done;
                }

                /* rc == EWOULDBLOCK */
                libcfs_sock_notifier_twait(&ntf, timeout, &break_flag);

                timeout -= cfs_duration_sec(cfs_time_sub(cfs_time_current(),
                                                         start_time));
                start_time = cfs_time_current();
        }
        
        if (nob != 0) {
                if (break_flag)
                        rc = -EIO;
                else if (timeout <= 0)
                        rc = -ETIMEDOUT;
                else
                        LBUG();
        }

  sock_write_done:
        ksocket_setcallbacks(csockp->csock_sock, NULL, NULL, CRED());
        libcfs_sock_notifier_fini(&ntf);
        return rc;
}

static void
accept_cb_handler(ksocket_t sock, ksocket_callback_event_t ev, void *arg,
                 uintptr_t evarg)
{
        cfs_sock_notifier_t *ntf = (cfs_sock_notifier_t *)arg;

        mutex_enter(&ntf->sntf_lock);
        
        switch (ev) {
        case KSOCKET_EV_NEWCONN:
                ntf->sntf_cpuid  = CPU->cpu_id + 1;
                ntf->sntf_state |= CFS_SNTF_READY;
                break;

        default:
                LBUG();
        }

        cv_broadcast(&ntf->sntf_cv);
        mutex_exit(&ntf->sntf_lock);
}

static ksocket_callbacks_t accept_callbacks = {
        .ksock_cb_flags        = KSOCKET_CB_NEWCONN,
        .ksock_cb_newconn      = accept_cb_handler,
};

/* We assume that sock was set as nonblocking with ksocket_ioctl(FIONBIO) */
int
libcfs_sock_accept(cfs_socket_t **newcsockpp, cfs_socket_t *csockp)
{
        int                  break_flag = 0;
        int                  rc;
        cfs_sock_notifier_t *ntf = &csockp->csock_ntf;
        ksocket_t            newsock;
        cfs_socket_t        *newcsockp;

        LIBCFS_ALLOC(newcsockp, sizeof(*newcsockp));
        if (newcsockp == NULL) {
                CERROR ("Can't allocate memory for cfs_socket_t");
                return -ENOMEM;
        }

        rc = ksocket_setcallbacks(csockp->csock_sock, &accept_callbacks, ntf,
                                  CRED());
        if (rc != 0)
                goto sock_accept_done;

        while (!break_flag) {

                rc = ksocket_accept(csockp->csock_sock,
                                    NULL, NULL, &newsock, CRED());
                if (rc == 0) { /* got it! */
                        newcsockp->csock_sock   = newsock;
                        newcsockp->csock_cpu_id = ntf->sntf_cpuid;
                        break;
                } else if (rc != EWOULDBLOCK) { /* real error */
                        break;
                }

                /* rc == EWOULDBLOCK */
                libcfs_sock_notifier_wait(ntf, &break_flag);

                if (break_flag) /* somebody interrupted us with abort_accept */
                        rc = EAGAIN; /* not EINTR to make our caller happy */
        }

  sock_accept_done:
        ksocket_setcallbacks(csockp->csock_sock, NULL, NULL, CRED());

        if (rc != 0)
                LIBCFS_FREE(newcsockp, sizeof(*newcsockp));
        else
                *newcsockpp = newcsockp;

        return -rc;
}

void
libcfs_sock_abort_accept(cfs_socket_t *csockp)
{
        mutex_enter(&csockp->csock_ntf.sntf_lock);

        csockp->csock_ntf.sntf_state |= CFS_SNTF_BREAK;

        cv_broadcast(&csockp->csock_ntf.sntf_cv);
        mutex_exit(&csockp->csock_ntf.sntf_lock);
}

/* Internal function to release socket w/o shutting down */
static inline void
libcfs_sock_close(cfs_socket_t *csockp)
{
        ksocket_close(csockp->csock_sock, CRED());

        libcfs_sock_notifier_fini(&csockp->csock_ntf);

        LIBCFS_FREE(csockp, sizeof(*csockp));
}


/* Internal function for sock_connect and sock_listen */
static int
libcfs_sock_create(cfs_socket_t **csockpp,
                   int *fatal, __u32 local_ip, int local_port)
{
        ksocket_t           sock;
        cfs_socket_t       *csockp;
        int                 rc;
	int32_t	            on  = 1;
        struct sockaddr_in  locaddr;
        
        /* All errors are fatal except bind failure if the port is in use */
        *fatal = 1;

        LIBCFS_ALLOC(csockp, sizeof(*csockp));
        if (csockp == NULL) {
                CERROR ("Can't allocate memory for cfs_socket_t");
                return -ENOMEM;
        }
        
        rc = ksocket_socket(&sock,
                            AF_INET, SOCK_STREAM, 0, KSOCKET_SLEEP, CRED());
        if (rc != 0) {
                CERROR ("Can't create socket: %d\n", rc);
                LIBCFS_FREE(csockp, sizeof(*csockp));
                return -rc;
        }

        csockp->csock_sock = sock;
        libcfs_sock_notifier_init(&csockp->csock_ntf);

        rc = ksocket_setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
                                (const void *)&on, sizeof(on), CRED());
        if (rc != 0) {
                CERROR("Can't set SO_REUSEADDR for socket: %d\n", rc);
                goto failed;
        }

        if (local_ip != 0 || local_port != 0) {
                memset(&locaddr, 0, sizeof(locaddr));
                locaddr.sin_family = AF_INET;
                locaddr.sin_port = htons(local_port);
                locaddr.sin_addr.s_addr = (local_ip == 0) ?
                                          INADDR_ANY : htonl(local_ip);

                rc = ksocket_bind(sock, (struct sockaddr *)&locaddr,
                                  sizeof(locaddr), CRED());
                if (rc == EADDRINUSE) {
                        CDEBUG(D_NET, "Port %d already in use\n", local_port);
                        *fatal = 0;
                        goto failed;
                }
                if (rc != 0) {
                        CERROR("Error trying to bind to port %d: %d\n",
                               local_port, rc);
                        goto failed;
                }
        }

        *csockpp = csockp;
        return 0;

  failed:
        libcfs_sock_close(csockp);
        return -rc;
}

int
libcfs_sock_setbuf(cfs_socket_t *csockp, int txbufsize, int rxbufsize)
{
        int                 rc;

        if (txbufsize != 0) {
                rc = ksocket_setsockopt(csockp->csock_sock, SOL_SOCKET,
                                        SO_SNDBUF,
                                        (const void *)&txbufsize,
                                        sizeof(txbufsize), CRED());
                if (rc != 0) {
                        CERROR ("Can't set send buffer %d: %d\n",
                                txbufsize, rc);
                        return -rc;
                }
        }

        if (rxbufsize != 0) {
                rc = ksocket_setsockopt(csockp->csock_sock, SOL_SOCKET,
                                        SO_RCVBUF,
                                        (const void *)&rxbufsize,
                                        sizeof(rxbufsize), CRED());
                if (rc != 0) {
                        CERROR ("Can't set recv buffer %d: %d\n",
                                rxbufsize, rc);
                        return -rc;
                }
        }

        return 0;
}

int
libcfs_sock_getaddr(cfs_socket_t *csockp, int remote, __u32 *ip, int *port)
{
        struct sockaddr_in sin;
        socklen_t	   len = sizeof(sin);
        int                rc;

        if (remote)
                rc = ksocket_getpeername(csockp->csock_sock,
                                         (struct sockaddr *)&sin,
                                         &len, CRED());
        else
                rc = ksocket_getsockname(csockp->csock_sock,
                                         (struct sockaddr *)&sin,
                                         &len, CRED());

        if (rc != 0) {
                CERROR ("Error %d getting sock %s IP/port\n",
                        rc, remote ? "peer" : "local");
                return -rc;
        }

        if (ip != NULL)
                *ip = ntohl(sin.sin_addr.s_addr);

        if (port != NULL)
                *port = ntohs(sin.sin_port);

        return 0;
}

int
libcfs_sock_getbuf(cfs_socket_t *csockp, int *txbufsize, int *rxbufsize)
{
        int rc;
        int len = sizeof(int);

        if (txbufsize != NULL) {                
                rc = ksocket_getsockopt(csockp->csock_sock, SOL_SOCKET,
                                        SO_SNDBUF,
                                        txbufsize, &len, CRED());
                if (rc != 0) {
                        CERROR ("Can't get send buffer: %d\n", rc);
                        return -rc;
                }
                LASSERT (len == sizeof(int));
        }

        if (rxbufsize != NULL) {
                rc = ksocket_getsockopt(csockp->csock_sock, SOL_SOCKET,
                                        SO_RCVBUF,
                                        rxbufsize, &len, CRED());
                if (rc != 0) {
                        CERROR ("Can't get recv buffer: %d\n", rc);
                        return -rc;
                }
                LASSERT (len == sizeof(int));
        }

        return 0;
}

/* Right now, it is called only from lnet/acceptor.c::lnet_acceptor()
 *
 * Implementation: just create socket, start listening and
 * switch to non-blocking mode.
 */
int
libcfs_sock_listen(cfs_socket_t **csockpp,
                   __u32 local_ip, int local_port, int backlog)
{
        int      fatal;
        int      rc;
        int32_t  on  = 1;
        int      rval;

        rc = libcfs_sock_create(csockpp, &fatal, local_ip, local_port);
        if (rc != 0) {
                if (!fatal)
                        CERROR("Can't create socket: port %d already in use\n",
                               local_port);
                return rc;
        }

        rc = ksocket_listen((*csockpp)->csock_sock, backlog, CRED());
        if (rc != 0) {
                CERROR("Can't set listen backlog %d: %d\n", backlog, rc);
                libcfs_sock_close(*csockpp);
                return -rc;
        }

        rc = ksocket_ioctl((*csockpp)->csock_sock, FIONBIO, (intptr_t)&on,
                           &rval, CRED());
        if (rc == 0)
                return 0;
        
        CERROR("Can't switch socket to NONBLOCK mode: %d\n", rc);
        libcfs_sock_release(*csockpp);
        return -rc;
}

static void
connect_cb_handler(ksocket_t sock, ksocket_callback_event_t ev, void *arg,
                 uintptr_t evarg)
{
        *(int *)arg = CPU->cpu_id + 1;
}

static ksocket_callbacks_t connect_callbacks = {
        .ksock_cb_flags       = KSOCKET_CB_CONNECTED,
        .ksock_cb_connected   = connect_cb_handler,
};

/* Right now, there is exactly one call-path:
 * LND --> lnet_connect() from lnet/acceptor.c --> libcfs_sock_connect()
 *
 * Implementation: just create socket, bind it and connect to remote peer
 */
int
libcfs_sock_connect(cfs_socket_t **csockpp, int *fatal,
                    __u32 local_ip, int local_port,
                    __u32 peer_ip, int peer_port)
{
        struct sockaddr_in srvaddr;
        int                rc;

        rc = libcfs_sock_create(csockpp, fatal, local_ip, local_port);
        if (rc != 0)
                return rc;

        memset (&srvaddr, 0, sizeof (srvaddr));
        srvaddr.sin_family = AF_INET;
        srvaddr.sin_port = htons(peer_port);
        srvaddr.sin_addr.s_addr = htonl(peer_ip);

        rc = ksocket_setcallbacks((*csockpp)->csock_sock, &connect_callbacks,
                                  &(*csockpp)->csock_cpu_id, CRED());

        /* Here we assume that socket hasn't been switched to
         * NONBLOCK-mode yet */
        rc = ksocket_connect((*csockpp)->csock_sock,
                             (struct sockaddr *)&srvaddr, sizeof(srvaddr),
                             CRED());

        ksocket_setcallbacks((*csockpp)->csock_sock, NULL, NULL, CRED());
        
        if (rc == 0)
                return 0;
        else
                rc = -rc;

        /* EADDRNOTAVAIL probably means we're already connected to the same
         * peer/port on the same local port on a differently typed
         * connection.  Let our caller retry with a different local
         * port...
         * Solaris-specific: EADDRINUSE is also possible, see comments
         * in inet/ip/ipclassifier.c */
        *fatal = !(rc == -EADDRNOTAVAIL || rc == -EADDRINUSE);

        CDEBUG(*fatal ? D_NETERROR : D_NET,
               "Error %d connecting %u.%u.%u.%u/%d -> %u.%u.%u.%u/%d\n", rc,
               HIPQUAD(local_ip), local_port, HIPQUAD(peer_ip), peer_port);

        libcfs_sock_release(*csockpp);
        return rc;
}

void
libcfs_sock_release(cfs_socket_t *csockp)
{
        ksocket_shutdown(csockp->csock_sock, SHUT_RDWR, CRED());

        libcfs_sock_close(csockp);
}

int
libcfs_sock_error(cfs_socket_t *csockp)
{
        int rc;
        int err = 0;
        int len = sizeof(err);

        rc = ksocket_getsockopt(csockp->csock_sock, SOL_SOCKET,
                                SO_ERROR,
                                &err, &len, CRED());
        if (rc != 0) {
                CERROR ("Can't get sock error: %d\n", rc);
                return EINVAL;
        }
        LASSERT (len == sizeof(int));

        return err;        
}
