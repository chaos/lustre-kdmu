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
 */

#ifndef _LUSTRE_SEC_H_
#define _LUSTRE_SEC_H_

/*
 * to avoid include
 */
struct key;
struct obd_import;
struct obd_export;
struct ptlrpc_request;
struct ptlrpc_reply_state;
struct ptlrpc_bulk_desc;
struct brw_page;
struct seq_file;

/*
 * forward declaration
 */
struct ptlrpc_sec_policy;
struct ptlrpc_sec_cops;
struct ptlrpc_sec_sops;
struct ptlrpc_sec;
struct ptlrpc_svc_ctx;
struct ptlrpc_cli_ctx;
struct ptlrpc_ctx_ops;

/*
 * flavor constants
 */
enum sptlrpc_policy {
        SPTLRPC_POLICY_NULL             = 0,
        SPTLRPC_POLICY_PLAIN            = 1,
        SPTLRPC_POLICY_GSS              = 2,
        SPTLRPC_POLICY_MAX,
};

enum sptlrpc_mech_null {
        SPTLRPC_MECH_NULL               = 0,
        SPTLRPC_MECH_NULL_MAX,
};

enum sptlrpc_mech_plain {
        SPTLRPC_MECH_PLAIN              = 0,
        SPTLRPC_MECH_PLAIN_MAX,
};

enum sptlrpc_mech_gss {
        SPTLRPC_MECH_GSS_NULL           = 0,
        SPTLRPC_MECH_GSS_KRB5           = 1,
        SPTLRPC_MECH_GSS_MAX,
};

enum sptlrpc_service_type {
        SPTLRPC_SVC_NULL                = 0,    /* no security */
        SPTLRPC_SVC_AUTH                = 1,    /* auth only */
        SPTLRPC_SVC_INTG                = 2,    /* integrity */
        SPTLRPC_SVC_PRIV                = 3,    /* privacy */
        SPTLRPC_SVC_MAX,
};

enum sptlrpc_bulk_type {
        SPTLRPC_BULK_DEFAULT            = 0,    /* follow rpc flavor */
        SPTLRPC_BULK_HASH               = 1,    /* hash integrity */
        SPTLRPC_BULK_MAX,
};

enum sptlrpc_bulk_service {
        SPTLRPC_BULK_SVC_NULL           = 0,
        SPTLRPC_BULK_SVC_AUTH           = 1,
        SPTLRPC_BULK_SVC_INTG           = 2,
        SPTLRPC_BULK_SVC_PRIV           = 3,
        SPTLRPC_BULK_SVC_MAX,
};

/*
 * rpc flavor compose/extract, represented as 32 bits. currently the
 * high 12 bits are unused, must be set as 0.
 *
 * 4b (bulk svc) | 4b (bulk type) | 4b (svc) | 4b (mech)  | 4b (policy)
 */
#define FLVR_POLICY_OFFSET              (0)
#define FLVR_MECH_OFFSET                (4)
#define FLVR_SVC_OFFSET                 (8)
#define FLVR_BULK_TYPE_OFFSET           (12)
#define FLVR_BULK_SVC_OFFSET            (16)

#define MAKE_FLVR(policy, mech, svc, btype, bsvc)                       \
        (((__u32)(policy) << FLVR_POLICY_OFFSET) |                      \
         ((__u32)(mech) << FLVR_MECH_OFFSET) |                          \
         ((__u32)(svc) << FLVR_SVC_OFFSET) |                            \
         ((__u32)(btype) << FLVR_BULK_TYPE_OFFSET) |                    \
         ((__u32)(bsvc) << FLVR_BULK_SVC_OFFSET))

/*
 * extraction
 */
#define SPTLRPC_FLVR_POLICY(flavor)                                     \
        ((((__u32)(flavor)) >> FLVR_POLICY_OFFSET) & 0xF)
#define SPTLRPC_FLVR_MECH(flavor)                                       \
        ((((__u32)(flavor)) >> FLVR_MECH_OFFSET) & 0xF)
#define SPTLRPC_FLVR_SVC(flavor)                                        \
        ((((__u32)(flavor)) >> FLVR_SVC_OFFSET) & 0xF)
#define SPTLRPC_FLVR_BULK_TYPE(flavor)                                  \
        ((((__u32)(flavor)) >> FLVR_BULK_TYPE_OFFSET) & 0xF)
#define SPTLRPC_FLVR_BULK_SVC(flavor)                                   \
        ((((__u32)(flavor)) >> FLVR_BULK_SVC_OFFSET) & 0xF)

#define SPTLRPC_FLVR_BASE(flavor)                                       \
        ((((__u32)(flavor)) >> FLVR_POLICY_OFFSET) & 0xFFF)
#define SPTLRPC_FLVR_BASE_SUB(flavor)                                   \
        ((((__u32)(flavor)) >> FLVR_MECH_OFFSET) & 0xFF)

/*
 * gss subflavors
 */
#define MAKE_BASE_SUBFLVR(mech, svc)                                    \
        ((__u32)(mech) |                                                \
         ((__u32)(svc) << (FLVR_SVC_OFFSET - FLVR_MECH_OFFSET)))

#define SPTLRPC_SUBFLVR_KRB5N                                           \
        MAKE_BASE_SUBFLVR(SPTLRPC_MECH_GSS_KRB5, SPTLRPC_SVC_NULL)
#define SPTLRPC_SUBFLVR_KRB5A                                           \
        MAKE_BASE_SUBFLVR(SPTLRPC_MECH_GSS_KRB5, SPTLRPC_SVC_AUTH)
#define SPTLRPC_SUBFLVR_KRB5I                                           \
        MAKE_BASE_SUBFLVR(SPTLRPC_MECH_GSS_KRB5, SPTLRPC_SVC_INTG)
#define SPTLRPC_SUBFLVR_KRB5P                                           \
        MAKE_BASE_SUBFLVR(SPTLRPC_MECH_GSS_KRB5, SPTLRPC_SVC_PRIV)

/*
 * "end user" flavors
 */
#define SPTLRPC_FLVR_NULL                               \
        MAKE_FLVR(SPTLRPC_POLICY_NULL,                  \
                  SPTLRPC_MECH_NULL,                    \
                  SPTLRPC_SVC_NULL,                     \
                  SPTLRPC_BULK_DEFAULT,                 \
                  SPTLRPC_BULK_SVC_NULL)
#define SPTLRPC_FLVR_PLAIN                              \
        MAKE_FLVR(SPTLRPC_POLICY_PLAIN,                 \
                  SPTLRPC_MECH_PLAIN,                   \
                  SPTLRPC_SVC_NULL,                     \
                  SPTLRPC_BULK_HASH,                    \
                  SPTLRPC_BULK_SVC_INTG)
#define SPTLRPC_FLVR_KRB5N                              \
        MAKE_FLVR(SPTLRPC_POLICY_GSS,                   \
                  SPTLRPC_MECH_GSS_KRB5,                \
                  SPTLRPC_SVC_NULL,                     \
                  SPTLRPC_BULK_DEFAULT,                 \
                  SPTLRPC_BULK_SVC_NULL)
#define SPTLRPC_FLVR_KRB5A                              \
        MAKE_FLVR(SPTLRPC_POLICY_GSS,                   \
                  SPTLRPC_MECH_GSS_KRB5,                \
                  SPTLRPC_SVC_AUTH,                     \
                  SPTLRPC_BULK_DEFAULT,                 \
                  SPTLRPC_BULK_SVC_NULL)
#define SPTLRPC_FLVR_KRB5I                              \
        MAKE_FLVR(SPTLRPC_POLICY_GSS,                   \
                  SPTLRPC_MECH_GSS_KRB5,                \
                  SPTLRPC_SVC_INTG,                     \
                  SPTLRPC_BULK_DEFAULT,                 \
                  SPTLRPC_BULK_SVC_INTG)
#define SPTLRPC_FLVR_KRB5P                              \
        MAKE_FLVR(SPTLRPC_POLICY_GSS,                   \
                  SPTLRPC_MECH_GSS_KRB5,                \
                  SPTLRPC_SVC_PRIV,                     \
                  SPTLRPC_BULK_DEFAULT,                 \
                  SPTLRPC_BULK_SVC_PRIV)

#define SPTLRPC_FLVR_DEFAULT            SPTLRPC_FLVR_NULL

#define SPTLRPC_FLVR_INVALID            ((__u32) 0xFFFFFFFF)
#define SPTLRPC_FLVR_ANY                ((__u32) 0xFFF00000)

/*
 * extract the useful part from wire flavor
 */
#define WIRE_FLVR(wflvr)                (((__u32) (wflvr)) & 0x000FFFFF)

static inline void flvr_set_svc(__u32 *flvr, __u32 svc)
{
        LASSERT(svc < SPTLRPC_SVC_MAX);
        *flvr = MAKE_FLVR(SPTLRPC_FLVR_POLICY(*flvr),
                          SPTLRPC_FLVR_MECH(*flvr),
                          svc,
                          SPTLRPC_FLVR_BULK_TYPE(*flvr),
                          SPTLRPC_FLVR_BULK_SVC(*flvr));
}

static inline void flvr_set_bulk_svc(__u32 *flvr, __u32 svc)
{
        LASSERT(svc < SPTLRPC_BULK_SVC_MAX);
        *flvr = MAKE_FLVR(SPTLRPC_FLVR_POLICY(*flvr),
                          SPTLRPC_FLVR_MECH(*flvr),
                          SPTLRPC_FLVR_SVC(*flvr),
                          SPTLRPC_FLVR_BULK_TYPE(*flvr),
                          svc);
}

struct bulk_spec_hash {
        __u8    hash_alg;
};

struct sptlrpc_flavor {
        __u32   sf_rpc;         /* wire flavor - should be renamed to sf_wire */
        __u32   sf_flags;       /* general flags */
        /*
         * rpc flavor specification
         */
        union {
                /* nothing for now */
        } u_rpc;
        /*
         * bulk flavor specification
         */
        union {
                struct bulk_spec_hash hash;
        } u_bulk;
};

enum lustre_sec_part {
        LUSTRE_SP_CLI           = 0,
        LUSTRE_SP_MDT,
        LUSTRE_SP_OST,
        LUSTRE_SP_MGC,
        LUSTRE_SP_MGS,
        LUSTRE_SP_ANY           = 0xFF
};

const char *sptlrpc_part2name(enum lustre_sec_part sp);
enum lustre_sec_part sptlrpc_target_sec_part(struct obd_device *obd);

struct sptlrpc_rule {
        __u32                   sr_netid;   /* LNET network ID */
        __u8                    sr_from;    /* sec_part */
        __u8                    sr_to;      /* sec_part */
        __u16                   sr_padding;
        struct sptlrpc_flavor   sr_flvr;
};

struct sptlrpc_rule_set {
        int                     srs_nslot;
        int                     srs_nrule;
        struct sptlrpc_rule    *srs_rules;
};

int sptlrpc_parse_flavor(const char *str, struct sptlrpc_flavor *flvr);
int sptlrpc_flavor_has_bulk(struct sptlrpc_flavor *flvr);

static inline void sptlrpc_rule_set_init(struct sptlrpc_rule_set *set)
{
        memset(set, 0, sizeof(*set));
}

void sptlrpc_rule_set_free(struct sptlrpc_rule_set *set);
int  sptlrpc_rule_set_expand(struct sptlrpc_rule_set *set);
int  sptlrpc_rule_set_merge(struct sptlrpc_rule_set *set,
                            struct sptlrpc_rule *rule);
int sptlrpc_rule_set_choose(struct sptlrpc_rule_set *rset,
                            enum lustre_sec_part from,
                            enum lustre_sec_part to,
                            lnet_nid_t nid,
                            struct sptlrpc_flavor *sf);
void sptlrpc_rule_set_dump(struct sptlrpc_rule_set *set);

int  sptlrpc_process_config(struct lustre_cfg *lcfg);
void sptlrpc_conf_log_start(const char *logname);
void sptlrpc_conf_log_stop(const char *logname);
void sptlrpc_conf_log_update_begin(const char *logname);
void sptlrpc_conf_log_update_end(const char *logname);
void sptlrpc_conf_client_adapt(struct obd_device *obd);
int  sptlrpc_conf_target_get_rules(struct obd_device *obd,
                                   struct sptlrpc_rule_set *rset,
                                   int initial);
void sptlrpc_target_choose_flavor(struct sptlrpc_rule_set *rset,
                                  enum lustre_sec_part from,
                                  lnet_nid_t nid,
                                  struct sptlrpc_flavor *flavor);

/* The maximum length of security payload. 1024 is enough for Kerberos 5,
 * and should be enough for other future mechanisms but not sure.
 * Only used by pre-allocated request/reply pool.
 */
#define SPTLRPC_MAX_PAYLOAD     (1024)


struct vfs_cred {
        uint32_t        vc_uid;
        uint32_t        vc_gid;
};

struct ptlrpc_ctx_ops {
        int     (*match)       (struct ptlrpc_cli_ctx *ctx,
                                struct vfs_cred *vcred);
        int     (*refresh)     (struct ptlrpc_cli_ctx *ctx);
        int     (*validate)    (struct ptlrpc_cli_ctx *ctx);
        void    (*die)         (struct ptlrpc_cli_ctx *ctx,
                                int grace);
        int     (*display)     (struct ptlrpc_cli_ctx *ctx,
                                char *buf, int bufsize);
        /*
         * rpc data transform
         */
        int     (*sign)        (struct ptlrpc_cli_ctx *ctx,
                                struct ptlrpc_request *req);
        int     (*verify)      (struct ptlrpc_cli_ctx *ctx,
                                struct ptlrpc_request *req);
        int     (*seal)        (struct ptlrpc_cli_ctx *ctx,
                                struct ptlrpc_request *req);
        int     (*unseal)      (struct ptlrpc_cli_ctx *ctx,
                                struct ptlrpc_request *req);
        /*
         * bulk transform
         */
        int     (*wrap_bulk)   (struct ptlrpc_cli_ctx *ctx,
                                struct ptlrpc_request *req,
                                struct ptlrpc_bulk_desc *desc);
        int     (*unwrap_bulk) (struct ptlrpc_cli_ctx *ctx,
                                struct ptlrpc_request *req,
                                struct ptlrpc_bulk_desc *desc);
};

#define PTLRPC_CTX_NEW_BIT             (0)  /* newly created */
#define PTLRPC_CTX_UPTODATE_BIT        (1)  /* uptodate */
#define PTLRPC_CTX_DEAD_BIT            (2)  /* mark expired gracefully */
#define PTLRPC_CTX_ERROR_BIT           (3)  /* fatal error (refresh, etc.) */
#define PTLRPC_CTX_CACHED_BIT          (8)  /* in ctx cache (hash etc.) */
#define PTLRPC_CTX_ETERNAL_BIT         (9)  /* always valid */

#define PTLRPC_CTX_NEW                 (1 << PTLRPC_CTX_NEW_BIT)
#define PTLRPC_CTX_UPTODATE            (1 << PTLRPC_CTX_UPTODATE_BIT)
#define PTLRPC_CTX_DEAD                (1 << PTLRPC_CTX_DEAD_BIT)
#define PTLRPC_CTX_ERROR               (1 << PTLRPC_CTX_ERROR_BIT)
#define PTLRPC_CTX_CACHED              (1 << PTLRPC_CTX_CACHED_BIT)
#define PTLRPC_CTX_ETERNAL             (1 << PTLRPC_CTX_ETERNAL_BIT)

#define PTLRPC_CTX_STATUS_MASK         (PTLRPC_CTX_NEW_BIT    |       \
                                        PTLRPC_CTX_UPTODATE   |       \
                                        PTLRPC_CTX_DEAD       |       \
                                        PTLRPC_CTX_ERROR)

struct ptlrpc_cli_ctx {
        cfs_hlist_node_t        cc_cache;      /* linked into ctx cache */
        cfs_atomic_t            cc_refcount;
        struct ptlrpc_sec      *cc_sec;
        struct ptlrpc_ctx_ops  *cc_ops;
        cfs_time_t              cc_expire;     /* in seconds */
        unsigned int            cc_early_expire:1;
        unsigned long           cc_flags;
        struct vfs_cred         cc_vcred;
        cfs_spinlock_t          cc_lock;
        cfs_list_t              cc_req_list;   /* waiting reqs linked here */
        cfs_list_t              cc_gc_chain;   /* linked to gc chain */
};

struct ptlrpc_sec_cops {
        /*
         * ptlrpc_sec constructor/destructor
         */
        struct ptlrpc_sec *     (*create_sec)  (struct obd_import *imp,
                                                struct ptlrpc_svc_ctx *ctx,
                                                struct sptlrpc_flavor *flavor);
        void                    (*destroy_sec) (struct ptlrpc_sec *sec);

        /*
         * notify to-be-dead
         */
        void                    (*kill_sec)    (struct ptlrpc_sec *sec);

        /*
         * context
         */
        struct ptlrpc_cli_ctx * (*lookup_ctx)  (struct ptlrpc_sec *sec,
                                                struct vfs_cred *vcred,
                                                int create,
                                                int remove_dead);
        void                    (*release_ctx) (struct ptlrpc_sec *sec,
                                                struct ptlrpc_cli_ctx *ctx,
                                                int sync);
        int                     (*flush_ctx_cache)
                                               (struct ptlrpc_sec *sec,
                                                uid_t uid,
                                                int grace,
                                                int force);
        void                    (*gc_ctx)      (struct ptlrpc_sec *sec);

        /*
         * reverse context
         */
        int                     (*install_rctx)(struct obd_import *imp,
                                                struct ptlrpc_sec *sec,
                                                struct ptlrpc_cli_ctx *ctx);

        /*
         * request/reply buffer manipulation
         */
        int                     (*alloc_reqbuf)(struct ptlrpc_sec *sec,
                                                struct ptlrpc_request *req,
                                                int lustre_msg_size);
        void                    (*free_reqbuf) (struct ptlrpc_sec *sec,
                                                struct ptlrpc_request *req);
        int                     (*alloc_repbuf)(struct ptlrpc_sec *sec,
                                                struct ptlrpc_request *req,
                                                int lustre_msg_size);
        void                    (*free_repbuf) (struct ptlrpc_sec *sec,
                                                struct ptlrpc_request *req);
        int                     (*enlarge_reqbuf)
                                               (struct ptlrpc_sec *sec,
                                                struct ptlrpc_request *req,
                                                int segment, int newsize);
        /*
         * misc
         */
        int                     (*display)     (struct ptlrpc_sec *sec,
                                                struct seq_file *seq);
};

struct ptlrpc_sec_sops {
        int                     (*accept)      (struct ptlrpc_request *req);
        int                     (*authorize)   (struct ptlrpc_request *req);
        void                    (*invalidate_ctx)
                                               (struct ptlrpc_svc_ctx *ctx);
        /* buffer manipulation */
        int                     (*alloc_rs)    (struct ptlrpc_request *req,
                                                int msgsize);
        void                    (*free_rs)     (struct ptlrpc_reply_state *rs);
        void                    (*free_ctx)    (struct ptlrpc_svc_ctx *ctx);
        /* reverse context */
        int                     (*install_rctx)(struct obd_import *imp,
                                                struct ptlrpc_svc_ctx *ctx);
        /* bulk transform */
        int                     (*prep_bulk)   (struct ptlrpc_request *req,
                                                struct ptlrpc_bulk_desc *desc);
        int                     (*unwrap_bulk) (struct ptlrpc_request *req,
                                                struct ptlrpc_bulk_desc *desc);
        int                     (*wrap_bulk)   (struct ptlrpc_request *req,
                                                struct ptlrpc_bulk_desc *desc);
};

struct ptlrpc_sec_policy {
        cfs_module_t                   *sp_owner;
        char                           *sp_name;
        __u16                           sp_policy; /* policy number */
        struct ptlrpc_sec_cops         *sp_cops;   /* client ops */
        struct ptlrpc_sec_sops         *sp_sops;   /* server ops */
};

#define PTLRPC_SEC_FL_REVERSE           0x0001 /* reverse sec */
#define PTLRPC_SEC_FL_ROOTONLY          0x0002 /* treat everyone as root */
#define PTLRPC_SEC_FL_UDESC             0x0004 /* ship udesc */
#define PTLRPC_SEC_FL_BULK              0x0008 /* intensive bulk i/o expected */
#define PTLRPC_SEC_FL_PAG               0x0010 /* PAG mode */

struct ptlrpc_sec {
        struct ptlrpc_sec_policy       *ps_policy;
        cfs_atomic_t                    ps_refcount;
        cfs_atomic_t                    ps_nctx;        /* statistic only */
        int                             ps_id;          /* unique identifier */
        struct sptlrpc_flavor           ps_flvr;        /* flavor */
        enum lustre_sec_part            ps_part;
        unsigned int                    ps_dying:1;
        struct obd_import              *ps_import;      /* owning import */
        cfs_spinlock_t                  ps_lock;        /* protect ccache */
        /*
         * garbage collection
         */
        cfs_list_t                      ps_gc_list;
        cfs_time_t                      ps_gc_interval; /* in seconds */
        cfs_time_t                      ps_gc_next;     /* in seconds */
};

static inline int sec_is_reverse(struct ptlrpc_sec *sec)
{
        return (sec->ps_flvr.sf_flags & PTLRPC_SEC_FL_REVERSE);
}

static inline int sec_is_rootonly(struct ptlrpc_sec *sec)
{
        return (sec->ps_flvr.sf_flags & PTLRPC_SEC_FL_ROOTONLY);
}


struct ptlrpc_svc_ctx {
        cfs_atomic_t                    sc_refcount;
        struct ptlrpc_sec_policy       *sc_policy;
};

/*
 * user identity descriptor
 */
#define LUSTRE_MAX_GROUPS               (128)

struct ptlrpc_user_desc {
        __u32           pud_uid;
        __u32           pud_gid;
        __u32           pud_fsuid;
        __u32           pud_fsgid;
        __u32           pud_cap;
        __u32           pud_ngroups;
        __u32           pud_groups[0];
};

/*
 * bulk flavors
 */
enum sptlrpc_bulk_hash_alg {
        BULK_HASH_ALG_NULL      = 0,
        BULK_HASH_ALG_ADLER32,
        BULK_HASH_ALG_CRC32,
        BULK_HASH_ALG_MD5,
        BULK_HASH_ALG_SHA1,
        BULK_HASH_ALG_SHA256,
        BULK_HASH_ALG_SHA384,
        BULK_HASH_ALG_SHA512,
        BULK_HASH_ALG_MAX
};

struct sptlrpc_hash_type {
        char           *sht_name;
        char           *sht_tfm_name;
        unsigned int    sht_size;
};

const struct sptlrpc_hash_type *sptlrpc_get_hash_type(__u8 hash_alg);
const char * sptlrpc_get_hash_name(__u8 hash_alg);
__u8 sptlrpc_get_hash_alg(const char *algname);

enum {
        BSD_FL_ERR      = 1,
};

struct ptlrpc_bulk_sec_desc {
        __u8            bsd_version;    /* 0 */
        __u8            bsd_type;       /* SPTLRPC_BULK_XXX */
        __u8            bsd_svc;        /* SPTLRPC_BULK_SVC_XXXX */
        __u8            bsd_flags;      /* flags */
        __u32           bsd_nob;        /* nob of bulk data */
        __u8            bsd_data[0];    /* policy-specific token */
};


/*
 * lprocfs
 */
struct proc_dir_entry;
extern struct proc_dir_entry *sptlrpc_proc_root;

/*
 * round size up to next power of 2, for slab allocation.
 * @size must be sane (can't overflow after round up)
 */
static inline int size_roundup_power2(int size)
{
        size--;
        size |= size >> 1;
        size |= size >> 2;
        size |= size >> 4;
        size |= size >> 8;
        size |= size >> 16;
        size++;
        return size;
}

/*
 * internal support libraries
 */
void _sptlrpc_enlarge_msg_inplace(struct lustre_msg *msg,
                                  int segment, int newsize);

/*
 * security type
 */
int sptlrpc_register_policy(struct ptlrpc_sec_policy *policy);
int sptlrpc_unregister_policy(struct ptlrpc_sec_policy *policy);

__u32 sptlrpc_name2flavor_base(const char *name);
const char *sptlrpc_flavor2name_base(__u32 flvr);
char *sptlrpc_flavor2name_bulk(struct sptlrpc_flavor *sf,
                               char *buf, int bufsize);
char *sptlrpc_flavor2name(struct sptlrpc_flavor *sf, char *buf, int bufsize);
char *sptlrpc_secflags2str(__u32 flags, char *buf, int bufsize);

static inline
struct ptlrpc_sec_policy *sptlrpc_policy_get(struct ptlrpc_sec_policy *policy)
{
        __cfs_module_get(policy->sp_owner);
        return policy;
}

static inline
void sptlrpc_policy_put(struct ptlrpc_sec_policy *policy)
{
        cfs_module_put(policy->sp_owner);
}

/*
 * client credential
 */
static inline
unsigned long cli_ctx_status(struct ptlrpc_cli_ctx *ctx)
{
        return (ctx->cc_flags & PTLRPC_CTX_STATUS_MASK);
}

static inline
int cli_ctx_is_ready(struct ptlrpc_cli_ctx *ctx)
{
        return (cli_ctx_status(ctx) == PTLRPC_CTX_UPTODATE);
}

static inline
int cli_ctx_is_refreshed(struct ptlrpc_cli_ctx *ctx)
{
        return (cli_ctx_status(ctx) != 0);
}

static inline
int cli_ctx_is_uptodate(struct ptlrpc_cli_ctx *ctx)
{
        return ((ctx->cc_flags & PTLRPC_CTX_UPTODATE) != 0);
}

static inline
int cli_ctx_is_error(struct ptlrpc_cli_ctx *ctx)
{
        return ((ctx->cc_flags & PTLRPC_CTX_ERROR) != 0);
}

static inline
int cli_ctx_is_dead(struct ptlrpc_cli_ctx *ctx)
{
        return ((ctx->cc_flags & (PTLRPC_CTX_DEAD | PTLRPC_CTX_ERROR)) != 0);
}

static inline
int cli_ctx_is_eternal(struct ptlrpc_cli_ctx *ctx)
{
        return ((ctx->cc_flags & PTLRPC_CTX_ETERNAL) != 0);
}

/*
 * sec get/put
 */
struct ptlrpc_sec *sptlrpc_sec_get(struct ptlrpc_sec *sec);
void sptlrpc_sec_put(struct ptlrpc_sec *sec);

/*
 * internal apis which only used by policy impelentation
 */
int  sptlrpc_get_next_secid(void);
void sptlrpc_sec_destroy(struct ptlrpc_sec *sec);

/*
 * exported client context api
 */
struct ptlrpc_cli_ctx *sptlrpc_cli_ctx_get(struct ptlrpc_cli_ctx *ctx);
void sptlrpc_cli_ctx_put(struct ptlrpc_cli_ctx *ctx, int sync);
void sptlrpc_cli_ctx_expire(struct ptlrpc_cli_ctx *ctx);
void sptlrpc_cli_ctx_wakeup(struct ptlrpc_cli_ctx *ctx);
int sptlrpc_cli_ctx_display(struct ptlrpc_cli_ctx *ctx, char *buf, int bufsize);

/*
 * exported client context wrap/buffers
 */
int sptlrpc_cli_wrap_request(struct ptlrpc_request *req);
int sptlrpc_cli_unwrap_reply(struct ptlrpc_request *req);
int sptlrpc_cli_alloc_reqbuf(struct ptlrpc_request *req, int msgsize);
void sptlrpc_cli_free_reqbuf(struct ptlrpc_request *req);
int sptlrpc_cli_alloc_repbuf(struct ptlrpc_request *req, int msgsize);
void sptlrpc_cli_free_repbuf(struct ptlrpc_request *req);
int sptlrpc_cli_enlarge_reqbuf(struct ptlrpc_request *req,
                               int segment, int newsize);
int  sptlrpc_cli_unwrap_early_reply(struct ptlrpc_request *req,
                                    struct ptlrpc_request **req_ret);
void sptlrpc_cli_finish_early_reply(struct ptlrpc_request *early_req);

void sptlrpc_request_out_callback(struct ptlrpc_request *req);

/*
 * exported higher interface of import & request
 */
int sptlrpc_import_sec_adapt(struct obd_import *imp,
                             struct ptlrpc_svc_ctx *ctx,
                             struct sptlrpc_flavor *flvr);
struct ptlrpc_sec *sptlrpc_import_sec_ref(struct obd_import *imp);
void sptlrpc_import_sec_put(struct obd_import *imp);

int  sptlrpc_import_check_ctx(struct obd_import *imp);
void sptlrpc_import_flush_root_ctx(struct obd_import *imp);
void sptlrpc_import_flush_my_ctx(struct obd_import *imp);
void sptlrpc_import_flush_all_ctx(struct obd_import *imp);
int  sptlrpc_req_get_ctx(struct ptlrpc_request *req);
void sptlrpc_req_put_ctx(struct ptlrpc_request *req, int sync);
int  sptlrpc_req_refresh_ctx(struct ptlrpc_request *req, long timeout);
int  sptlrpc_req_replace_dead_ctx(struct ptlrpc_request *req);
void sptlrpc_req_set_flavor(struct ptlrpc_request *req, int opcode);

int sptlrpc_parse_rule(char *param, struct sptlrpc_rule *rule);

/* gc */
void sptlrpc_gc_add_sec(struct ptlrpc_sec *sec);
void sptlrpc_gc_del_sec(struct ptlrpc_sec *sec);
void sptlrpc_gc_add_ctx(struct ptlrpc_cli_ctx *ctx);

/* misc */
const char * sec2target_str(struct ptlrpc_sec *sec);
int sptlrpc_lprocfs_cliobd_attach(struct obd_device *dev);

/*
 * server side
 */
enum secsvc_accept_res {
        SECSVC_OK       = 0,
        SECSVC_COMPLETE,
        SECSVC_DROP,
};

int  sptlrpc_svc_unwrap_request(struct ptlrpc_request *req);
int  sptlrpc_svc_alloc_rs(struct ptlrpc_request *req, int msglen);
int  sptlrpc_svc_wrap_reply(struct ptlrpc_request *req);
void sptlrpc_svc_free_rs(struct ptlrpc_reply_state *rs);
void sptlrpc_svc_ctx_addref(struct ptlrpc_request *req);
void sptlrpc_svc_ctx_decref(struct ptlrpc_request *req);
void sptlrpc_svc_ctx_invalidate(struct ptlrpc_request *req);

int  sptlrpc_target_export_check(struct obd_export *exp,
                                 struct ptlrpc_request *req);
void sptlrpc_target_update_exp_flavor(struct obd_device *obd,
                                      struct sptlrpc_rule_set *rset);

/*
 * reverse context
 */
int sptlrpc_svc_install_rvs_ctx(struct obd_import *imp,
                                struct ptlrpc_svc_ctx *ctx);
int sptlrpc_cli_install_rvs_ctx(struct obd_import *imp,
                                struct ptlrpc_cli_ctx *ctx);

/* bulk security api */
int sptlrpc_enc_pool_add_user(void);
int sptlrpc_enc_pool_del_user(void);
int  sptlrpc_enc_pool_get_pages(struct ptlrpc_bulk_desc *desc);
void sptlrpc_enc_pool_put_pages(struct ptlrpc_bulk_desc *desc);

int sptlrpc_cli_wrap_bulk(struct ptlrpc_request *req,
                          struct ptlrpc_bulk_desc *desc);
int sptlrpc_cli_unwrap_bulk_read(struct ptlrpc_request *req,
                                 struct ptlrpc_bulk_desc *desc,
                                 int nob);
int sptlrpc_cli_unwrap_bulk_write(struct ptlrpc_request *req,
                                  struct ptlrpc_bulk_desc *desc);
int sptlrpc_svc_prep_bulk(struct ptlrpc_request *req,
                          struct ptlrpc_bulk_desc *desc);
int sptlrpc_svc_wrap_bulk(struct ptlrpc_request *req,
                          struct ptlrpc_bulk_desc *desc);
int sptlrpc_svc_unwrap_bulk(struct ptlrpc_request *req,
                            struct ptlrpc_bulk_desc *desc);

/* bulk helpers (internal use only by policies) */
int sptlrpc_get_bulk_checksum(struct ptlrpc_bulk_desc *desc, __u8 alg,
                              void *buf, int buflen);

int bulk_sec_desc_unpack(struct lustre_msg *msg, int offset, int swabbed);

/* user descriptor helpers */
static inline int sptlrpc_user_desc_size(int ngroups)
{
        return sizeof(struct ptlrpc_user_desc) + ngroups * sizeof(__u32);
}

int sptlrpc_current_user_desc_size(void);
int sptlrpc_pack_user_desc(struct lustre_msg *msg, int offset);
int sptlrpc_unpack_user_desc(struct lustre_msg *req, int offset, int swabbed);


#define CFS_CAP_CHOWN_MASK (1 << CFS_CAP_CHOWN)
#define CFS_CAP_SYS_RESOURCE_MASK (1 << CFS_CAP_SYS_RESOURCE)

enum {
        LUSTRE_SEC_NONE         = 0,
        LUSTRE_SEC_REMOTE       = 1,
        LUSTRE_SEC_SPECIFY      = 2,
        LUSTRE_SEC_ALL          = 3
};

#endif /* _LUSTRE_SEC_H_ */
