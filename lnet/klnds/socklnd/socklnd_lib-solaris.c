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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lnet/klnds/socklnd/socklnd_lib-solaris.c
 *
 */

#include <sys/xti_inet.h> /* for TCP_NODELAY */
#include <sys/strsubr.h>  /* for desballoca() */

#include "socklnd.h"

int
ksocknal_lib_tunables_init ()
{
        return 0;
}

void
ksocknal_lib_tunables_fini ()
{
}

void
ksocknal_lib_bind_irq (unsigned int irq)
{
}

/* 100% code-duplication of linux version
 * TODO: move it to socklnd_lib-linux-solaris.c */
int
ksocknal_lib_get_conn_addrs (ksock_conn_t *conn)
{
        int rc = libcfs_sock_getaddr(conn->ksnc_sock, 1,
                                     &conn->ksnc_ipaddr,
                                     &conn->ksnc_port);

        /* Didn't need the {get,put}connsock dance to deref ksnc_sock... */
        LASSERT (!conn->ksnc_closing);

        if (rc != 0) {
                CERROR ("Error %d getting sock peer IP\n", rc);
                return rc;
        }

        rc = libcfs_sock_getaddr(conn->ksnc_sock, 0,
                                 &conn->ksnc_myipaddr, NULL);
        if (rc != 0) {
                CERROR ("Error %d getting sock local IP\n", rc);
                return rc;
        }

        return 0;
}

unsigned int
ksocknal_lib_sock_irq (cfs_socket_t *csockp)
{
#ifdef CPU_AFFINITY
/*
 * There is no way on Solaris to query sock about its irq.
 * But we can use cpu_id as irq assuming a ksocket callback
 * has already set it:
 */        
        return csockp->csock_cpu_id;
#endif
        return 0;
}

int
ksocknal_lib_zc_capable(ksock_conn_t *conn)
{
#ifdef sun4u
        return 0; /* not capable */
#else
        int                   rc;
        int                   on;
        int                   len = sizeof(on);
        struct so_snd_bufinfo bi;
        cfs_socket_t         *csockp = conn->ksnc_sock;

        on = 1;
        rc = ksocket_setsockopt(csockp->csock_sock, SOL_SOCKET,
                                SO_SND_COPYAVOID,
                                (void const *)&on, sizeof(on),
                                CRED());

        if (rc != 0)
                return 0; /* not capable */

	len = sizeof(bi);
        rc = ksocket_getsockopt(csockp->csock_sock, SOL_SOCKET,
                                SO_SND_BUFINFO,
                                &bi, &len, CRED());

        if (rc != 0 || bi.sbi_maxpsz < CFS_PAGE_SIZE) {

                CDEBUG(D_NET, "Can't use zc-capablity of csock %p: "
                       "rc=%d (maxblk=%d; maxpsz=%d)\n",
                       csockp, rc,
                       rc == 0 ? bi.sbi_maxblk : 0,
                       rc == 0 ? bi.sbi_maxpsz : 0);

                on = 0; /* turn off zc */
                ksocket_setsockopt(csockp->csock_sock,
                                   SOL_SOCKET, SO_SND_COPYAVOID,
                                   (const void *)&on, sizeof(on),
                                   CRED());
                return 0; /* not capable */
        }

        CDEBUG(D_NET, "csock %p is zc-capable\n", csockp);
        return 1;
#endif /* !sun4u */
}

int
ksocknal_lib_send_iov (ksock_conn_t *conn, ksock_tx_t *tx)
{
        int    rc, i;
        size_t sent;

#if SOCKNAL_SINGLE_FRAG_TX
        struct iovec   scratch;
        struct iovec  *scratchiov = &scratch;
        unsigned int   niov       = 1;
#else
        struct iovec  *scratchiov = conn->ksnc_scheduler->kss_scratch_iov;
        unsigned int   niov       = tx->tx_niov;
#endif
        struct nmsghdr msg = {
                .msg_name       = NULL,
                .msg_namelen    = 0,
                .msg_iov        = scratchiov,
                .msg_iovlen     = niov,
                .msg_control    = NULL,
                .msg_controllen = 0,
                .msg_flags      = 0
        };

        if (*ksocknal_tunables.ksnd_enable_csum        && /* checksum enabled */
            conn->ksnc_proto == &ksocknal_protocol_v2x && /* V2.x connection  */
            tx->tx_nob == tx->tx_resid                 && /* frist sending    */
            tx->tx_msg.ksm_csum == 0)                     /* not checksummed  */
                ksocknal_lib_csum_tx(tx);
        
        for (i = 0; i < niov; i++)
                scratchiov[i] = tx->tx_iov[i];

        rc = ksocket_sendmsg(conn->ksnc_sock->csock_sock,
                             &msg, MSG_DONTWAIT, &sent, CRED());
        LASSERT (rc >= 0);
        
        if (rc != 0)
                return -rc;

        LASSERT (sent > 0);
        return sent;
}

typedef struct {
	frtn_t		kdi_frtn;
        cfs_page_t     *kdi_cfs_page;
} ksock_desbinfo_t;

static void
ksocknal_lib_desbfree(ksock_desbinfo_t *inf)
{
        cfs_kunmap(inf->kdi_cfs_page);
        cfs_put_page(inf->kdi_cfs_page);
	kmem_free(inf, sizeof (*inf));
}

static int
ksocknal_lib_send_kiov_zc (ksock_conn_t *conn, ksock_tx_t *tx)
{
        int          rc, i, len;
        lnet_kiov_t *kiov    = tx->tx_kiov;
        int          nob     = 0;
        int          niov    = tx->tx_nkiov;
        mblk_t      *mp      = NULL;
        mblk_t      *tail_mp = NULL;

        struct nmsghdr msg = {
                .msg_name       = NULL,
                .msg_namelen    = 0,
                .msg_iov        = NULL,
                .msg_iovlen     = 0,
                .msg_control    = NULL,
                .msg_controllen = 0,
                .msg_flags      = 0
        };

        for (i = 0; i < niov; i++, kiov++) {
                mblk_t           *mp1;
                ksock_desbinfo_t *inf  = kmem_alloc(sizeof (*inf), KM_NOSLEEP);
                void             *vaddr;

                if (inf == NULL)
                        return -ENOMEM;

                inf->kdi_frtn.free_func = ksocknal_lib_desbfree;
                inf->kdi_frtn.free_arg  = (caddr_t)inf;
                inf->kdi_cfs_page       = kiov->kiov_page;
                cfs_get_page(kiov->kiov_page);

                vaddr = cfs_kmap(kiov->kiov_page);
                LASSERT (vaddr != NULL);
                        
                mp1 = desballoca((uchar_t *)vaddr + kiov->kiov_offset,
                                 kiov->kiov_len, BPRI_HI, &inf->kdi_frtn);

                if (mp1 == NULL) {
                        if (mp != NULL)
                                freemsg(mp);    
                        cfs_kunmap(kiov->kiov_page);
                        cfs_put_page(kiov->kiov_page);
                        kmem_free(inf, sizeof (*inf));
                        return -ENOMEM;
                }

                nob                         += kiov->kiov_len;
                mp1->b_wptr                 += kiov->kiov_len;
                mp1->b_datap->db_struioflag |= STRUIO_ZC;

                if (mp == NULL)
                        mp = mp1;
                else
                        linkb(tail_mp, mp1);                        

                tail_mp = mp1;
        }

        rc = ksocket_sendmblk(conn->ksnc_sock->csock_sock,
                              &msg, FNONBLOCK, &mp, CRED());
        LASSERT (rc >= 0);

        len = mp != NULL ? msgdsize(mp) : 0;

        LASSERT(rc != 0 || mp == NULL);

        if (rc != 0 && mp != NULL)
                freemsg(mp);
                
        if ((rc != 0 && rc != EAGAIN) ||  /* real error */
            (rc == EAGAIN && len == nob)) /* nothing sent */
                return -rc;

        return nob - len;
}

static int
ksocknal_lib_send_kiov_plain (ksock_conn_t *conn, ksock_tx_t *tx)
{
        lnet_kiov_t *kiov = tx->tx_kiov;
        int          rc, i;
        size_t       sent = 0;

#if SOCKNAL_SINGLE_FRAG_TX
        struct iovec   scratch;
        struct iovec  *scratchiov = &scratch;
        unsigned int   niov       = 1;
#else
        struct iovec  *scratchiov =
                conn->ksnc_scheduler->kss_scratch_iov;
        unsigned int   niov       = tx->tx_nkiov;
#endif
        struct nmsghdr msg = {
                .msg_name       = NULL,
                .msg_namelen    = 0,
                .msg_iov        = scratchiov,
                .msg_iovlen     = niov,
                .msg_control    = NULL,
                .msg_controllen = 0,
                .msg_flags      = 0
        };

        for (i = 0; i < niov; i++) {
                scratchiov[i].iov_base = cfs_kmap(kiov[i].kiov_page) +
                        kiov[i].kiov_offset;
                scratchiov[i].iov_len  = kiov[i].kiov_len;
        }

        rc = ksocket_sendmsg(conn->ksnc_sock->csock_sock,
                             &msg, MSG_DONTWAIT, &sent, CRED());
        LASSERT (rc >= 0);

        for (i = 0; i < niov; i++)
                cfs_kunmap(kiov[i].kiov_page);

        if ((rc != 0 && rc != EAGAIN) ||  /* real error */
            (rc == EAGAIN && sent == 0)) /* nothing sent */
                return -rc;

        return sent;
}

int
ksocknal_lib_send_kiov (ksock_conn_t *conn, ksock_tx_t *tx)
{
        if (tx->tx_msg.ksm_zc_cookies[0] != 0) /* Zero copy is enabled */
                return ksocknal_lib_send_kiov_zc(conn, tx);

        return ksocknal_lib_send_kiov_plain(conn, tx);
}

void
ksocknal_lib_eager_ack (ksock_conn_t *conn)
{
        /* Remind the socket to ACK eagerly.  If I don't, the socket might
         * think I'm about to send something it could piggy-back the ACK
         * on, introducing delay in completing zero-copy sends in my
         * peer. */

        /* Right now, Solaris doesn't support this feature:
         * Maxim: Is there any way to instruct a socket to ACK eagerly?
         * I mean an analog of linux TCP_QUICKACK.
         * Anders: I believe there is a way of disabling that on a system
         * wide basis, but there is not a socket option that I am aware of.
         * But I'll look into it, and if it's not there I'll file a RFE. */

        /* So, we can add an setsockopt(TCP_QUICKACK) here as soon as it
         * becomes available */
}

int
ksocknal_lib_recv_iov (ksock_conn_t *conn)
{
#if SOCKNAL_SINGLE_FRAG_RX
        struct iovec   scratch;
        struct iovec  *scratchiov = &scratch;
        unsigned int   niov       = 1;
#else
        struct iovec  *scratchiov = conn->ksnc_scheduler->kss_scratch_iov;
        unsigned int   niov       = conn->ksnc_rx_niov;
#endif
        struct iovec  *iov        = conn->ksnc_rx_iov;
        struct nmsghdr msg        = {
                .msg_name       = NULL,
                .msg_namelen    = 0,
                .msg_iov        = scratchiov,
                .msg_iovlen     = niov,
                .msg_control    = NULL,
                .msg_controllen = 0,
                .msg_flags      = 0
        };
        int            nob;
        size_t         recv;
        int            i;
        int            rc;
        int            fragnob;
        int            sum;
        __u32          saved_csum;

        /* NB we can't trust socket ops to either consume our iovs
         * or leave them alone. */
        LASSERT (niov > 0);

        for (nob = i = 0; i < niov; i++) {
                scratchiov[i] = iov[i];
                nob += scratchiov[i].iov_len;
        }
        LASSERT (nob <= conn->ksnc_rx_nob_wanted);

        rc = ksocket_recvmsg(conn->ksnc_sock->csock_sock,
                             &msg, MSG_DONTWAIT, &recv, CRED());
        LASSERT (rc >= 0);

        if (rc != 0) {
                rc = -rc;
        } else {
                LASSERT (recv >= 0);
                rc = recv;
        }

        saved_csum = 0;
        if (conn->ksnc_proto == &ksocknal_protocol_v2x) {
                saved_csum = conn->ksnc_msg.ksm_csum;
                conn->ksnc_msg.ksm_csum = 0;
        }

        if (saved_csum != 0) {
                /* accumulate checksum */
                for (i = 0, sum = rc; sum > 0; i++, sum -= fragnob) {
                        LASSERT (i < niov);

                        fragnob = iov[i].iov_len;
                        if (fragnob > sum)
                                fragnob = sum;

                        conn->ksnc_rx_csum = ksocknal_csum(conn->ksnc_rx_csum,
                                                           iov[i].iov_base, fragnob);
                }
                conn->ksnc_msg.ksm_csum = saved_csum;
        }

        return rc;
}

int
ksocknal_lib_recv_kiov (ksock_conn_t *conn)
{
#if SOCKNAL_SINGLE_FRAG_RX
        struct iovec   scratch;
        struct iovec  *scratchiov = &scratch;
        unsigned int   niov = 1;
#else
        struct iovec  *scratchiov = conn->ksnc_scheduler->kss_scratch_iov;
        unsigned int   niov = conn->ksnc_rx_nkiov;
#endif
        lnet_kiov_t   *kiov = conn->ksnc_rx_kiov;
        struct nmsghdr msg  = {
                .msg_name       = NULL,
                .msg_namelen    = 0,
                .msg_iov        = scratchiov,
                .msg_iovlen     = niov,
                .msg_control    = NULL,
                .msg_controllen = 0,
                .msg_flags      = 0
        };
        int            nob;
        size_t         recv;
        int            i;
        int            rc;
        void          *base;
        int            sum;
        int            fragnob;

        /* NB we can't trust socket ops to either consume our iovs
         * or leave them alone. */
        for (nob = i = 0; i < niov; i++) {
                scratchiov[i].iov_base = cfs_kmap(kiov[i].kiov_page)
                        + kiov[i].kiov_offset;
                nob += scratchiov[i].iov_len = kiov[i].kiov_len;
        }
        LASSERT (nob <= conn->ksnc_rx_nob_wanted);

        rc = ksocket_recvmsg(conn->ksnc_sock->csock_sock,
                             &msg, MSG_DONTWAIT, &recv, CRED());
        LASSERT (rc >= 0);

        if (rc != 0) {
                rc = -rc;
        } else {
                LASSERT (recv >= 0);
                rc = recv;
        }

        if (conn->ksnc_msg.ksm_csum != 0) {
                for (i = 0, sum = rc; sum > 0; i++, sum -= fragnob) {
                        LASSERT (i < niov);

                        base = cfs_kmap(kiov[i].kiov_page) + kiov[i].kiov_offset;
                        fragnob = kiov[i].kiov_len;
                        if (fragnob > sum)
                                fragnob = sum;

                        conn->ksnc_rx_csum = ksocknal_csum(conn->ksnc_rx_csum,
                                                           base, fragnob);

                        cfs_kunmap(kiov[i].kiov_page);
                }
        }
        for (i = 0; i < niov; i++)
                cfs_kunmap(kiov[i].kiov_page);

        return (rc);
}

void
ksocknal_lib_csum_tx(ksock_tx_t *tx)
{
        int          i;
        __u32        csum;
        void        *base;

        LASSERT(tx->tx_iov[0].iov_base == (void *)&tx->tx_msg);
        LASSERT(tx->tx_conn != NULL);
        LASSERT(tx->tx_conn->ksnc_proto == &ksocknal_protocol_v2x);

        tx->tx_msg.ksm_csum = 0;

        csum = ksocknal_csum(-1U, (void *)tx->tx_iov[0].iov_base,
                             tx->tx_iov[0].iov_len);

        if (tx->tx_kiov != NULL) {
                for (i = 0; i < tx->tx_nkiov; i++) {
                        base = cfs_kmap(tx->tx_kiov[i].kiov_page) +
                               tx->tx_kiov[i].kiov_offset;

                        csum = ksocknal_csum(csum, base, tx->tx_kiov[i].kiov_len);

                        cfs_kunmap(tx->tx_kiov[i].kiov_page);
                }
        } else {
                for (i = 1; i < tx->tx_niov; i++)
                        csum = ksocknal_csum(csum, tx->tx_iov[i].iov_base,
                                             tx->tx_iov[i].iov_len);
        }

        if (*ksocknal_tunables.ksnd_inject_csum_error) {
                csum++;
                *ksocknal_tunables.ksnd_inject_csum_error = 0;
        }

        tx->tx_msg.ksm_csum = csum;
}

int
ksocknal_lib_get_conn_tunables (ksock_conn_t *conn, int *txmem, int *rxmem, int *nagle)
{
        int           rc;
        cfs_socket_t *csockp = conn->ksnc_sock;

        rc = ksocknal_connsock_addref(conn);
        if (rc != 0) {
                LASSERT (conn->ksnc_closing);
                *txmem = *rxmem = *nagle = 0;
                return (-ESHUTDOWN);
        }

        rc = libcfs_sock_getbuf(csockp, txmem, rxmem);
        if (rc == 0) {
                int len = sizeof(*nagle);
                rc = ksocket_getsockopt(csockp->csock_sock, IPPROTO_TCP,
                                        TCP_NODELAY,
                                        nagle, &len, CRED());
                if (rc != 0)
                        rc = -rc;
        }

        ksocknal_connsock_decref(conn);

        if (rc == 0)
                *nagle = !*nagle;
        else
                *txmem = *rxmem = *nagle = 0;

        return rc;
}

int
ksocknal_lib_setup_sock (cfs_socket_t *csockp)
{
        int           on;
        int           rc;
        struct linger linger;

        /* Ensure this socket aborts active sends immediately when we close
         * it. */

        linger.l_onoff = 0;
        linger.l_linger = 0;

        /* Solaris doesn't support TCP_LINGER2, only SO_LINGER */
        rc = ksocket_setsockopt(csockp->csock_sock, SOL_SOCKET,
                                SO_LINGER,
                                (const void *)&linger, sizeof(linger), CRED());
        if (rc != 0) {
                CERROR ("Can't set SO_LINGER: %d\n", rc);
                return -rc;
        }

        if (!*ksocknal_tunables.ksnd_nagle) {
                on = 1;

                rc = ksocket_setsockopt(csockp->csock_sock, IPPROTO_TCP,
                                        TCP_NODELAY,
                                        (const void *)&on, sizeof(on), CRED());
                if (rc != 0) {
                        CERROR ("Can't disable nagle: %d\n", rc);
                        return -rc;
                }
        }

        rc = libcfs_sock_setbuf(csockp,
                                *ksocknal_tunables.ksnd_tx_buffer_size,
                                *ksocknal_tunables.ksnd_rx_buffer_size);
        if (rc != 0) {
                CERROR ("Can't set buffer tx %d, rx %d buffers: %d\n",
                        *ksocknal_tunables.ksnd_tx_buffer_size,
                        *ksocknal_tunables.ksnd_rx_buffer_size, rc);
                return rc;
        }

/* Solaris doesn't support TCP_BACKOFF feature */
#ifdef SOCKNAL_BACKOFF
#warning Is there SOCKNAL_BACKOFF on Solaris? We did not expect it...
#endif

        /* Solaris doesn't support keepalive tunables like
         * keep_idle, keep_count and keep_intvl. All we can do
         * is turn on KEEPALIVE 'as is' */

        on = (*ksocknal_tunables.ksnd_keepalive_idle  > 0 &&
              *ksocknal_tunables.ksnd_keepalive_count > 0 &&
              *ksocknal_tunables.ksnd_keepalive_intvl > 0) ? 1 : 0;

        rc = ksocket_setsockopt(csockp->csock_sock, SOL_SOCKET,
                                SO_KEEPALIVE,
                                (const void *)&on, sizeof(on),
                                CRED());
        if (rc != 0) {
                CERROR ("Can't set SO_KEEPALIVE: %d\n", rc);
                return -rc;
        }

        return 0;
}

void
ksocknal_lib_push_conn(ksock_conn_t *conn)
{
        CWARN("ksocknal_lib_push_conn() is called but not supported\n");
}

extern void ksocknal_read_callback (ksock_conn_t *conn);
extern void ksocknal_write_callback (ksock_conn_t *conn);

/*
 * socket callbacks in Solaris
 */

static void
ksocknal_data_ready(ksocket_t sock, ksocket_callback_event_t ev,
                    void *arg, uintptr_t evarg)
{
        ksock_conn_t *conn = (ksock_conn_t *)arg;

        /* no races with ksocknal_terminate_conn because
         * we're called with sonode so_lock held */
        ksocknal_read_callback(conn);
}

static void
ksocknal_write_space(ksocket_t sock, ksocket_callback_event_t ev,
                     void *arg, uintptr_t evarg)
{
        ksock_conn_t *conn = (ksock_conn_t *)arg;

        /* no races with ksocknal_terminate_conn because
         * we're called with sonode so_lock held */

        /* we don't check how many write space is available
         * because we rely on ksocket system about calling
         * us only when number of queued data drops below
         * low watermark */
        ksocknal_write_callback(conn);
}

static ksocket_callbacks_t lib_callbacks = {
        .ksock_cb_flags        = KSOCKET_CB_NEWDATA |
                                 KSOCKET_CB_DISCONNECTED |
                                 KSOCKET_CB_CANTRECVMORE |
                                 KSOCKET_CB_CANTSENDMORE |
                                 KSOCKET_CB_CANSEND,
        .ksock_cb_newdata      = ksocknal_data_ready,
        .ksock_cb_disconnected = ksocknal_data_ready,
        .ksock_cb_cantrecvmore = ksocknal_data_ready,
        .ksock_cb_cantsendmore = ksocknal_write_space,
        .ksock_cb_cansend      = ksocknal_write_space,
        
};

void
ksocknal_lib_save_callback(cfs_socket_t *csockp, ksock_conn_t *conn)
{
        /* do nothing */
}

/* ksocknal_create_conn calls us with global_lock held. This is safe
 * (no deadlock is possible) because we're called only once, and
 * callbacks aren't set before the call */
void
ksocknal_lib_set_callback(cfs_socket_t *csockp,  ksock_conn_t *conn)
{
        int rc;

        rc = ksocket_setcallbacks(csockp->csock_sock, &lib_callbacks, conn,
                                  CRED());

        LASSERT (rc == 0);
}

/* ksocknal_terminate_conn calls us with global_lock held. To avoid
 * deadlock we have to release global_lock before calling ksocket API.
 * sonode so_lock protects us from races with data_ready/write_space
 * callbacks */
void
ksocknal_lib_reset_callback(cfs_socket_t *csockp, ksock_conn_t *conn)
{
        int rc;

        cfs_write_unlock_bh (&ksocknal_data.ksnd_global_lock);
        
        rc = ksocket_setcallbacks(csockp->csock_sock, NULL, NULL, CRED());

        cfs_write_lock_bh (&ksocknal_data.ksnd_global_lock);

        LASSERT (rc == 0);
}

int
ksocknal_lib_memory_pressure(ksock_conn_t *conn)
{
        /* Solaris ksocket calls never return EAGAIN due to memory
         * pressure. They do ENOMEM honestly */
        return 0;
}

#if defined(CONFIG_SMP) && defined(CPU_AFFINITY)

/* copy/paste from inet/ip/ip_squeue.c */
#define	CPU_ISON(c) (c != NULL && CPU_ACTIVE(c) && (c->cpu_flags & CPU_EXISTS))

int
ksocknal_lib_bind_thread_to_cpu(int id)
{
        int              i;
        int              rc = 0;
        ksock_irqinfo_t *info;

	for (i = 0; i < NCPU; i++) {
                info = &ksocknal_data.ksnd_irqinfo[i + 1];

                if (info->ksni_valid && info->ksni_sched == id)
                        break;
        }

        LASSERT (i != NCPU);
        
        mutex_enter(&cpu_lock);

        if (CPU_ISON(cpu[i]))
                thread_affinity_set(curthread, i);
        else
                rc = -1;

        mutex_exit(&cpu_lock);

        return rc;
}

void
ksocknal_arch_init ()
{
        int i;
        int id = 0;

        mutex_enter(&cpu_lock);

	for (i = 0; i < NCPU; i++) {
                ksock_irqinfo_t *info;

                if (!CPU_ISON(cpu[i]))
                        continue;

                info = &ksocknal_data.ksnd_irqinfo[i + 1];

                info->ksni_valid = 1;
                info->ksni_sched = id;

                id++;
        }

        mutex_exit(&cpu_lock);

        ksocknal_data.ksnd_nschedulers = id;
}

#else /* CONFIG_SMP && CPU_AFFINITY */

int
ksocknal_lib_bind_thread_to_cpu(int id)
{
        return 0;
}

void
ksocknal_arch_init ()
{
        ksocknal_data.ksnd_nschedulers = ksocknal_nsched();
}
#endif /* CONFIG_SMP && CPU_AFFINITY */
