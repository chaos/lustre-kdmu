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
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * Implementation of params_tree userspace APIs.
 *
 * Author: LiuYing <emoly.liu@oracle.com>
 */

#include <regex.h>
#include <libcfs/libcfsutil.h>
#include <lnet/lnetctl.h>

#define PARAMS_BUFLEN_DEFAULT       8192

/* Parameters Tree Userspace APIs */
/* Introduction to how to list/get/set_param from params_tree without glob.
 *
 * Since params_tree is platform independent structure, we can't access it
 * as procfs with posix system calls(open/close/read/write/glob).
 * In order to access it in lctl, we implement param pattern matching with
 * GNU regular expression lib.
 * The main steps include:
 * 1. analyse the input pattern (x.y.z) and create its compiled regexp list
 *    with some necessary semantic convertion (a->b->c).
 * 2. read parameter entry names from params_tree as readdir does, one level
 *    each time.
 * 3. match all parameter names in the same level with the corresponding regexp.
 *    if (matched && regexp_list_end), return this parameter;
 *    if (matched && !regexp_list_end), keep reading and matching;
 *    if (!matched), go out.
 * 4. copy all the matched parameters information into param_entry_list,
 *    including full pathname, mode.
 * Till now, list_param is done.
 *
 * 5. If get/set_param value is needed, continue to lookup the parameters by
 *    their full pathname, then call read/write callback functions.
 */

/* regular expression list */
struct param_preg_list {
        regex_t preg;
        struct param_preg_list *next;
};

/* Free preg_list */
static void param_free_preglist(struct param_preg_list *pr_list) {
        struct param_preg_list *temp;

        while (pr_list != NULL) {
                temp = pr_list;
                pr_list = pr_list->next;
                regfree(&(temp->preg));
                free(temp);
        }
}

/* Wildcards in shell have different meanings from regular expression,
 * so we have to translate them to keep the semantic correctness. */
static void pattern2regexp(const char *pattern, char *regexp)
{
        /* apply GNU regular expression lib to parse the path pattern
         * wildcard translation:
         *      *                       -> .*
         *      ?                       -> .
         * beginning of the string      -> ^
         * end of the string            -> $
         */
        char *temp = NULL;

        if (!regexp)
                return;
        temp = regexp;
        *temp = '^';
        temp ++;
        while (*pattern != '\0') {
                switch (*pattern) {
                        case '*':
                                *temp = '.';
                                temp ++;
                                *temp = '*';
                                break;
                        case '?':
                                *temp = '.';
                                break;
                        default:
                                *temp = *pattern;
                                break;
                }
                temp ++;
                pattern ++;
        }
        *temp = '$';
        temp ++;
        *temp = '\0';
}

/* create the regular expression list according to the path pattern */
static struct param_preg_list *preg_list_create(const char *pattern)
{
#define REGEXP_LEN      64
        int rc = 0;
        char *path_pt = NULL;
        char *temp_path = NULL;
        char *dir_pt;
        char regexp[REGEXP_LEN];
        struct param_preg_list *preg_head = NULL;
        struct param_preg_list *preg_list = NULL;
        struct param_preg_list *preg_temp;

        /* compile GNU regexp filter list */
        path_pt = strdup(pattern);
        temp_path = path_pt;
        preg_head = calloc(1, sizeof(*preg_head));
        if (!preg_head) {
                fprintf(stderr, "error: %s: no memory for regexp_list!\n",
                        __func__);
                rc = -ENOMEM;
                goto out;
        }
        /* add '*' because we need to match {lustre,lnet} first by default */
        pattern2regexp("*", regexp);
        if (regcomp(&(preg_head->preg), regexp, 0)) {
                fprintf(stderr, "error: %s: regcomp failed!\n", __func__);
                rc = -EINVAL;
                goto out;
        }
        preg_list = preg_head;
        while ((dir_pt = strsep(&path_pt, "/"))) {
                if (!strcmp(dir_pt, ""))
                        continue;
                pattern2regexp(dir_pt, regexp);
                preg_temp = malloc(sizeof(*preg_temp));
                if (!preg_temp) {
                        fprintf(stderr, "error: %s: no memory for preg_list!\n",
                                __func__);
                        rc = -ENOMEM;
                        goto out;
                }
                if (regcomp(&(preg_temp->preg), regexp, 0)) {
                        fprintf(stderr, "error: %s: regcomp failed!\n",
                                __func__);
                        rc = -EINVAL;
                        goto out;
                }
                preg_temp->next = NULL;
                preg_list->next = preg_temp;
                preg_list = preg_list->next;
        }
out:
        if (temp_path)
                free(temp_path);
        if (rc < 0)
                if (preg_head)
                        param_free_preglist(preg_head);

        return preg_head;
}

/* fill entry list */
static int param_fill_list(void *buf, struct param_entry_list **pel)
{
        cfs_param_info_t      *pi = buf;
        struct param_entry_list *temp = NULL;
        char *ptr;
        int rc = 0;

        temp = malloc(sizeof(*temp) + pi->pi_name_len + 1);
        if (temp == NULL) {
                fprintf(stderr, "error: %s: No memory for pel.\n", __func__);
                return -ENOMEM;
        }
        temp->pel_next = NULL;
        temp->pel_name_len = pi->pi_name_len;
        temp->pel_mode = pi->pi_mode;
        ptr = (char *)temp + sizeof(struct param_entry_list);
        strcpy(ptr, pi->pi_name);
        temp->pel_name = ptr;
        *pel = temp;

        return rc;
}

static int param_ioctl(struct libcfs_ioctl_data *data, char **buf_ptr,
                        unsigned int opc)
{
        char *buf = NULL;
        int buflen = 0;
        int rc = 0;

        /* if PARAMS_BUFLEN_DEFAULT is not large enough,
         * we should avoid buflen < packlen */
        buflen = libcfs_ioctl_packlen(data);
        if (buflen <= PARAMS_BUFLEN_DEFAULT)
                buflen = PARAMS_BUFLEN_DEFAULT;
        buf = malloc(buflen);
        if (buf == NULL) {
                fprintf(stderr,
                        "error: %s: No memory for ioc_data.\n", __func__);
                GOTO(out, rc = -ENOMEM);
        }
        memset(buf, 0, buflen);
        /* list params through libcfs_ioctl */
        rc = libcfs_ioctl_pack(data, &buf, buflen);
        if (rc) {
                fprintf(stderr,
                        "error: %s: Failed to pack libcfs_ioctl data (%d).\n",
                        __func__, rc);
                GOTO(out, rc < 0 ? rc : -rc);
        }
        /* XXX: in case some tools can't recognize LNET_DEV_ID */
        register_ioc_dev(LNET_DEV_ID, LNET_DEV_PATH,
                         LNET_DEV_MAJOR, LNET_DEV_MINOR);
        rc = l_ioctl(LNET_DEV_ID, opc, buf);
        if (rc) {
                fprintf(stderr, "error: %s: IOC_LIBCFS_LIST_PARAM failed.\n",
                        __func__);
                GOTO(out, rc);
        }
out:
        if (rc < 0 && buf != NULL) {
                free(buf);
                buf = NULL;
        }
        *buf_ptr = buf;
        return rc;
}

/* client sends req{path, buf, buflen} to kernel by ioctl,
 * if buflen is not big enough, kernel will send a likely size back;
 * otherwise, send data back directly */
static int send_req_to_kernel(char *path, char *list_buf, int *list_buflen)
{
        struct libcfs_ioctl_data data = { 0 };
        struct libcfs_ioctl_data *data_ptr;
        char *buf;
        int rc;

        /* pack the parameters to ioc_data */
        (path == NULL) ? (data.ioc_inllen1 = 0) : /* list from the root */
                         (data.ioc_inllen1 = strlen(path) + 1);
        data.ioc_inlbuf1 = path;
        data.ioc_pbuf1 = list_buf;
        data.ioc_plen1 = *list_buflen;
        rc = param_ioctl(&data, &buf, IOC_LIBCFS_LIST_PARAM);
        if (buf == NULL) {
                *list_buflen = 0;
        } else {
                data_ptr = (struct libcfs_ioctl_data *)buf;
                rc = data_ptr->ioc_u32[0];
                if (rc < 0)
                        /* if not big enough, tell what kernel needs */
                        *list_buflen = data_ptr->ioc_plen1;
                free(buf);
        }

        return rc;
}

/* list params entry as proc_readdir does. */
static int cfs_param_ureaddir(char *parent_path, struct param_entry_list **pel)
{
        struct param_entry_list *temp = NULL;
        char *list_buf = NULL;
        char *temp_buf = NULL;
        int buflen = PARAMS_BUFLEN_DEFAULT;
        int num;
        int len;
        int i;
        int rc = 0;

        /* send {path, buf, buflen} to kernel */
        do {
                list_buf = malloc(buflen);
                if (list_buf == NULL) {
                        fprintf(stderr, "error: %s: No memory for list_buf.",
                                __func__);
                        return -ENOMEM;
                }
                rc = send_req_to_kernel(parent_path, list_buf, &buflen);
                if (rc < 0) {
                        free(list_buf);
                        if (buflen == 0)
                                return rc;
                }
        } while (rc == -ENOMEM);
        /* receive params from kernel */
        len = sizeof(struct cfs_param_info);
        temp_buf = list_buf;
        num = rc;
        for (i = 0; i < num; i++) {
                rc = param_fill_list(temp_buf, &temp);
                if (rc < 0) {
                        fprintf(stderr,
                                "error: Message receive: %s\n", strerror(-rc));
                        break;
                }
                temp->pel_next = NULL;
                (*pel)->pel_next = temp;
                (*pel) = (*pel)->pel_next;
                temp_buf += len;
        }
        if (list_buf)
                free(list_buf);

        return rc;
}

/* match the entry list with the regular expression list */
static int param_match(char *parent_path, struct param_preg_list *pregl,
                        struct param_entry_list **pel_ptr)
{
        int pel_name_len = 0;
        int rc = 0;
        char *pel_name = NULL;
        struct param_entry_list *curdir = NULL;
        struct param_entry_list *curdir_list = NULL;
        struct param_entry_list *new_pel;

        curdir = calloc(1, sizeof(*curdir));
        if (!curdir) {
                fprintf(stderr,
                        "error: %s: No memory for curdir.\n", __func__);
                rc = -ENOMEM;
                goto out;
        }
        curdir_list = curdir;
        rc = cfs_param_ureaddir(parent_path, &curdir);
        if (rc < 0)
                goto out;
        curdir = curdir_list->pel_next;
        while (curdir != NULL) {
                /* unmatched */
                if (regexec(&(pregl->preg), curdir->pel_name, 0, 0, 0)) {
                        curdir = curdir->pel_next;
                        continue;
                }
                /* matched: copy full path */
                if (parent_path == NULL)
                        pel_name_len = curdir->pel_name_len;
                else
                        pel_name_len = strlen(parent_path) + 1 +
                                       curdir->pel_name_len;
                pel_name = malloc(pel_name_len + 1);
                if (pel_name == NULL) {
                        fprintf(stderr, "error: %s: No memory for pel_name.\n",
                                __func__);
                        rc = -ENOMEM;
                        goto out;
                }
                if (parent_path == NULL) {
                        strncpy(pel_name, curdir->pel_name, curdir->pel_name_len);
                } else {
                        strncpy(pel_name, parent_path, strlen(parent_path));
                        pel_name[strlen(parent_path)] = '/';
                        strncpy(pel_name + strlen(parent_path) + 1,
                                curdir->pel_name, curdir->pel_name_len);
                }
                pel_name[pel_name_len] = '\0';
                /* if reach the end of preg list, copy the matched results;
                 * otherwise, if the current entry is a dir or symlink,
                 * read through its children. */
                if (!pregl->next) {
                        new_pel = malloc(sizeof(*new_pel));
                        if (!new_pel) {
                                fprintf(stderr, "error: %s: No memory for pel.\n",
                                        __func__);
                                rc = -ENOMEM;
                                goto out;
                        }
                        new_pel->pel_name_len = pel_name_len;
                        new_pel->pel_name = pel_name;
                        new_pel->pel_mode = curdir->pel_mode;
                        new_pel->pel_next = NULL;
                        if (*pel_ptr != NULL) {
                                (*pel_ptr)->pel_next = new_pel;
                                *pel_ptr = new_pel;
                        } else {
                                *pel_ptr = new_pel;
                        }
                } else if (S_ISDIR(curdir->pel_mode) || S_ISLNK(curdir->pel_mode)){
                        param_match(pel_name, pregl->next, pel_ptr);
                        free(pel_name);
                        pel_name = NULL;
                }
                curdir = curdir->pel_next;
        }
out:
        if (curdir_list)
                cfs_param_free_entrylist(curdir_list);

        return rc;
}

/* list the matched entries
 * 1. create regular expression list according to the pattern
 * 2. read the params entry back
 * 3. match the entries with regexp and return
 */
int cfs_param_ulist(const char *pattern, struct param_entry_list **pel_ptr)
{
        int rc = 0;
        struct param_preg_list *preg_head = NULL;
        struct param_entry_list *pel_head = NULL;

        if (pattern == NULL) {
                fprintf(stderr, "error: %s: Null path pattern.\n", __func__);
                GOTO(out, rc = -EINVAL);
        }

        *pel_ptr = calloc(1, sizeof(struct param_entry_list));
        if (*pel_ptr == NULL) {
                fprintf(stderr,
                        "error: %s: No memory for pel_list.\n", __func__);
                GOTO(out, rc = -ENOMEM);
        }
        pel_head = *pel_ptr;

        preg_head = preg_list_create(pattern);
        if (preg_head) {
                /* match from the root */
                rc = param_match(NULL, preg_head, &pel_head);
                param_free_preglist(preg_head);
                if ((*pel_ptr)->pel_next == NULL)
                        GOTO(out, rc = -ESRCH);
        }

        return rc;
out:
        if (*pel_ptr != NULL)
                cfs_param_free_entrylist(*pel_ptr);
        return rc;
}

/**
 * Get parameters value from params_tree according to @path by ioctl,
 * and copy the value out to @read_buf. If @buf_len is not enough,
 * @offset is used to remember the position, read until @eof is set.
 */
int cfs_param_uread(char *path, int path_len, char *read_buf,
                int buf_len, long long *offset, int *eof)
{
        struct libcfs_ioctl_data data = { 0 };
        struct libcfs_ioctl_data *data_ptr;
        char *pathname = NULL;
        char *buf;
        int rc = 0;

        if (!path || path_len <= 0) {
                fprintf(stderr, "error: %s: Path is null.\n", __func__);
                rc = -EINVAL;
                goto out;
        }
        memset(read_buf, 0, buf_len);
        /* pack the parameters to ioc_data */
        data.ioc_inllen1 = path_len + 1;
        pathname = malloc(path_len + 1);
        if (!pathname) {
                fprintf(stderr,
                        "error: %s: No memory for path name.\n", __func__);
                rc = -ENOMEM;
                goto out;
        }
        strncpy(pathname, path, path_len);
        pathname[path_len] = '\0';
        data.ioc_inlbuf1 = pathname;
        data.ioc_u64[0] = *offset;
        data.ioc_plen1 = buf_len;
        data.ioc_pbuf1 = read_buf;
        rc = param_ioctl(&data, &buf, IOC_LIBCFS_GET_PARAM);
        if (buf != NULL) {
                data_ptr = (struct libcfs_ioctl_data *)buf;
                *offset = data_ptr->ioc_u64[0];
                *eof = data_ptr->ioc_u32[1];
                rc = data_ptr->ioc_u32[0];
                free(buf);
        }
out:
        if (pathname)
                free(pathname);

        return rc;
}

/**
 * Set value in @write_buf to params_tree parameters.
 * Lookup the corresponding parameters according to @path by ioctl.
 */
int cfs_param_uwrite(char *path, int path_len, char *write_buf, int buf_len)
{
        struct libcfs_ioctl_data data = { 0 };
        struct libcfs_ioctl_data *data_ptr;
        char *pathname = NULL;
        char *buf;
        int rc = 0;

        if (!path || path_len <= 0) {
                fprintf(stderr, "error: %s: Path is null.\n", __func__);
                rc = -EINVAL;
                goto out;
        }

        /* pack the parameters to data first */
        pathname = malloc(path_len + 1);
        if (!pathname) {
                fprintf(stderr,
                        "error: %s: No memory for path name.\n", __func__);
                rc = -ENOMEM;
                goto out;
        }
        strncpy(pathname, path, path_len);
        pathname[path_len] = '\0';
        data.ioc_inlbuf1 = pathname;
        data.ioc_inllen1 = path_len + 1;
        data.ioc_inlbuf2 = write_buf;
        data.ioc_inllen2 = buf_len + 1;
        rc = param_ioctl(&data, &buf, IOC_LIBCFS_SET_PARAM);
        if (buf != NULL) {
                data_ptr = (struct libcfs_ioctl_data *)buf;
                rc = data_ptr->ioc_u32[0];
                free(buf);
        }
out:
        if (pathname)
                free(pathname);

        return rc;
}


/**
 * The following APIs are used to unpack cfs_param_data
 */
/* Count cfs_param_data len */
static int param_data_packlen(struct cfs_param_data *data)
{
        int len = sizeof(*data);

        if (data->pd_name)
                len += data->pd_name_len + 1;
        if (data->pd_unit)
                len += data->pd_unit_len + 1;
        len += data->pd_value_len;

        return len;
}

/* Unpack cfs_param_data from the input buf */
static int param_data_unpack(cfs_param_data_t **data_ptr, char *buf)
{
        cfs_param_data_t *data;
        char *ptr;

        if (!buf)
                return 1;

        data = (struct cfs_param_data *)buf;
        ptr = data->pd_bulk;

        if (data->pd_name_len) {
                data->pd_name = ptr;
                ptr += data->pd_name_len + 1;
        } else {
                data->pd_name = NULL;
        }
        if (data->pd_unit_len) {
                data->pd_unit = ptr;
                ptr += data->pd_unit_len + 1;
        } else {
                data->pd_unit = NULL;
        }
        if (data->pd_value_len)
                data->pd_value = ptr;
        else
                data->pd_value = NULL;

        *data_ptr = data;

        return 0;
}

static int param_value_output(cfs_param_data_t *data, char *outbuf)
{
        switch (data->pd_type) {
                case CFS_PARAM_S16: {
                        short temp;
                        memcpy(&temp, data->pd_value, data->pd_value_len);
                        sprintf(outbuf, "%d", temp);
                        break; }
                case CFS_PARAM_S32: {
                        int temp;
                        memcpy(&temp, data->pd_value, data->pd_value_len);
                        sprintf(outbuf, "%d", temp);
                        break; }
                case CFS_PARAM_S64: {
                        long long temp;
                        memcpy(&temp, data->pd_value, data->pd_value_len);
                        sprintf(outbuf, "%lld", temp);
                        break; }
                case CFS_PARAM_U8: {
                        __u8 temp;
                        memcpy(&temp, data->pd_value, data->pd_value_len);
                        sprintf(outbuf, "%u", temp);
                        break; }
                case CFS_PARAM_U16: {
                        __u16 temp;
                        memcpy(&temp, data->pd_value, data->pd_value_len);
                        sprintf(outbuf, "%u", temp);
                        break; }
                case CFS_PARAM_U32: {
                        __u32 temp;
                        memcpy(&temp, data->pd_value, data->pd_value_len);
                        sprintf(outbuf, "%u", temp);
                        break; }
                case CFS_PARAM_U64: {
                        __u64 temp;
                        memcpy(&temp, data->pd_value, data->pd_value_len);
                        sprintf(outbuf, "%llu", temp);
                        break; }
                case CFS_PARAM_DB:
                case CFS_PARAM_STR: {
                        if (data->pd_value[data->pd_value_len - 1] == '\n')
                                data->pd_value[data->pd_value_len - 1]='\0';
                        sprintf(outbuf, "%s", data->pd_value);
                        break; }
                default:
                        fprintf(stderr,
                                "warning: %s: unknown cfs_param_data_type"
                                " (%d).\n", __func__, data->pd_type);
                        return 0;
        }

        return strlen(outbuf);
}

/* one record each time */
int cfs_param_unpack(char *inbuf, char *outbuf, int outbuf_len)
{
        cfs_param_data_t *data;

        if (*inbuf == '\0')
                return 0;
        if (!param_data_unpack(&data, inbuf)) {
                if (data->pd_name != NULL) {
                        sprintf(outbuf, "%s\t", data->pd_name);
                        outbuf += data->pd_name_len + 1;
                }
                if (data->pd_value != NULL)
                        outbuf += param_value_output(data, outbuf);
                if (data->pd_unit != NULL) {
                        sprintf(outbuf, "%s\n", data->pd_unit);
                        outbuf += data->pd_unit_len + 1;
                } else {
                        sprintf(outbuf, "%s", "\n");
                        outbuf += 1;
                }
        }

        return param_data_packlen(data);
}

/* Free param_entry_list */
void cfs_param_free_entrylist(struct param_entry_list *entry_list)
{
        struct param_entry_list *pel;

        while (entry_list != NULL) {
                pel = entry_list;
                entry_list = entry_list->pel_next;
                free(pel);
        }
}
