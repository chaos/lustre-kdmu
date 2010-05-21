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
 * Implementation of params_tree userspace APIs.
 *
 * Author: LiuYing <emoly.liu@sun.com>
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
 * 4. copy all the matched parameters information into params_entry_list,
 *    including full pathname, mode.
 * Till now, list_param is done.
 *
 * 5. If get/set_param value is needed, continue to lookup the parameters by
 *    their full pathname, then call read/write callback functions.
 */

/* regular expression list */
struct params_preg_list {
        regex_t preg;
        struct params_preg_list *next;
};

/* Free preg_list */
static void params_free_preglist(struct params_preg_list *pr_list) {
        struct params_preg_list *temp;

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
static struct params_preg_list *preg_list_create(const char *pattern)
{
#define REGEXP_LEN      64
        int rc = 0;
        char *path_pt = NULL;
        char *temp_path = NULL;
        char *dir_pt;
        char regexp[REGEXP_LEN];
        struct params_preg_list *preg_head = NULL;
        struct params_preg_list *preg_list = NULL;
        struct params_preg_list *preg_temp;

        /* compile GNU regexp filter list */
        path_pt = strdup(pattern);
        temp_path = path_pt;
        preg_head = malloc(sizeof(*preg_head));
        if (!preg_head) {
                fprintf(stderr, "error: %s: no memory for regexp_list!\n",
                        __FUNCTION__);
                rc = -ENOMEM;
                goto out;
        }
        memset(preg_head, 0, sizeof(*preg_head));
        /* add '*' because we need to match {lustre,lnet} first by default */
        pattern2regexp("*", regexp);
        if (regcomp(&(preg_head->preg), regexp, 0)) {
                fprintf(stderr, "error: %s: regcomp failed!\n", __FUNCTION__);
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
                                __FUNCTION__);
                        rc = -ENOMEM;
                        goto out;
                }
                memset(preg_temp, 0, sizeof(*preg_temp));
                if (regcomp(&(preg_temp->preg), regexp, 0)) {
                        fprintf(stderr, "error: %s: regcomp failed!\n",
                                __FUNCTION__);
                        rc = -EINVAL;
                        goto out;
                }
                preg_list->next = preg_temp;
                preg_list = preg_list->next;
        }
out:
        if (temp_path)
                free(temp_path);
        if (rc < 0)
                if (preg_head)
                        params_free_preglist(preg_head);

        return preg_head;
}

/* fill entry list */
static int params_fill_list(void *buf, struct params_entry_list **pel)
{
        struct libcfs_param_info *lpi = buf;
        struct params_entry_list *temp = NULL;
        char *ptr;
        int rc = 0;

        temp = malloc(sizeof(*temp) + lpi->lpi_name_len + 1);
        if (temp == NULL) {
                fprintf(stderr, "error: %s: No memory for pel.\n",__FUNCTION__);
                return -ENOMEM;
        }
        temp->pel_next = NULL;
        temp->pel_name_len = lpi->lpi_name_len;
        temp->pel_mode = lpi->lpi_mode;
        ptr = (char *)temp + sizeof(struct params_entry_list);
        strcpy(ptr, lpi->lpi_name);
        temp->pel_name = ptr;
        *pel = temp;

        return rc;
}

/* client sends req{path, buf, buflen} to kernel by ioctl,
 * if buflen is not big enough, kernel will send a likely size back;
 * otherwise, send data back directly */
static int send_req_to_kernel(char *path, char *list_buf, int *buflen)
{
        struct libcfs_ioctl_data data = { 0 };
        int ioc_data_buflen = 0;
        char *ioc_data_buf = NULL;
        char *buf;
        int rc;

        /* pack the parameters to ioc_data */
        data.ioc_inllen1 = strlen(path) + 1;
        data.ioc_inlbuf1 = path;
        data.ioc_plen1 = *buflen;
        data.ioc_pbuf1 = list_buf;

        /* if PARAMS_BUFLEN_DEFAULT is not large enough,
         * we should avoid buflen < packlen */
        ioc_data_buflen = libcfs_ioctl_packlen(&data);
        if (ioc_data_buflen <= PARAMS_BUFLEN_DEFAULT)
                ioc_data_buflen = PARAMS_BUFLEN_DEFAULT;
        ioc_data_buf = malloc(ioc_data_buflen);
        if (ioc_data_buf == NULL) {
                fprintf(stderr,
                        "error: %s: No memory for ioc_data.\n", __FUNCTION__);
                *buflen = 0;
                return -ENOMEM;
        }
        memset(ioc_data_buf, 0, ioc_data_buflen);
        /* list params through libcfs_ioctl */
        buf = ioc_data_buf;
        rc = libcfs_ioctl_pack(&data, &buf, ioc_data_buflen);
        if (rc) {
                fprintf(stderr,
                        "error: %s: Failed to pack libcfs_ioctl data (%d).\n",
                        __FUNCTION__, rc);
                GOTO(out, rc < 0 ? rc : -rc);
        }
        /* XXX: lreplicate can't recognize LNET_DEV_ID */
        register_ioc_dev(LNET_DEV_ID, LNET_DEV_PATH,
                         LNET_DEV_MAJOR, LNET_DEV_MINOR);
        rc = l_ioctl(LNET_DEV_ID, IOC_LIBCFS_LIST_PARAM, buf);
        if (rc) {
                fprintf(stderr, "error: %s: IOC_LIBCFS_LIST_PARAM failed.\n",
                        __FUNCTION__);
                *buflen = 0;
                GOTO(out, rc < 0 ? rc : -rc);
        }
        rc = ((struct libcfs_ioctl_data *)buf)->ioc_u32[0];
        if (rc < 0) {
                /* if not big enough, tell what kernel needs */
                *buflen = ((struct libcfs_ioctl_data *)buf)->ioc_plen1;
                goto out;
        }
out:
        if (ioc_data_buf)
                free(ioc_data_buf);
        return rc;
}

/* list params entry as proc_readdir does. */
static int params_readdir(char *parent_path, struct params_entry_list **pel)
{
        struct params_entry_list *temp = NULL;
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
                                __FUNCTION__);
                        return -ENOMEM;
                }
                memset(list_buf, 0, buflen);
                rc = send_req_to_kernel(parent_path, list_buf, &buflen);
                if (rc < 0) {
                        free(list_buf);
                        if (buflen == 0)
                                return rc;
                }
        } while (rc == -ENOMEM);
        /* receive params from kernel */
        len = sizeof(struct libcfs_param_info);
        temp_buf = list_buf;
        num = rc;
        for (i = 0; i < num; i++) {
                rc = params_fill_list(temp_buf, &temp);
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
static int params_match(char *parent_path, struct params_preg_list *pregl,
                        struct params_entry_list **pel)
{
        int pel_name_len = 0;
        int rc = 0;
        char *pel_name = NULL;
        struct params_entry_list *curdir = NULL;
        struct params_entry_list *curdir_list = NULL;
        struct params_entry_list *new_pel;

        curdir = malloc(sizeof(*curdir));
        if (!curdir) {
                fprintf(stderr,
                        "error: %s: No memory for curdir.\n", __FUNCTION__);
                rc = -ENOMEM;
                goto out;
        }
        memset(curdir, 0, sizeof(*curdir));
        curdir_list = curdir;
        rc = params_readdir(parent_path, &curdir);
        if (rc < 0)
                goto out;
        curdir = curdir_list->pel_next;
        while (curdir) {
                /* unmatched */
                if (regexec(&(pregl->preg), curdir->pel_name, 0, 0, 0)) {
                        curdir = curdir->pel_next;
                        continue;
                }
                /* matched: copy full path */
                pel_name_len = strlen(parent_path) + 1 + curdir->pel_name_len;
                pel_name = malloc(pel_name_len + 1);
                if (!pel_name) {
                        fprintf(stderr, "error: %s: No memory for pel_name.\n",
                                __FUNCTION__);
                        rc = -ENOMEM;
                        goto out;
                }
                memset(pel_name, 0, pel_name_len + 1);
                strncpy(pel_name, parent_path, strlen(parent_path));
                pel_name[strlen(parent_path)] = '/';
                strncpy(pel_name + strlen(parent_path) + 1,
                        curdir->pel_name, curdir->pel_name_len);
                /* if reach the end of preg list, copy the matched results;
                 * otherwise, if the current entry is a dir or symlink,
                 * read through its children. */
                if (!pregl->next) {
                        new_pel = malloc(sizeof(*new_pel));
                        if (!new_pel) {
                                fprintf(stderr, "error: %s: No memory for pel.\n",
                                        __FUNCTION__);
                                rc = -ENOMEM;
                                goto out;
                        }
                        memset(new_pel, 0, sizeof(*new_pel));
                        new_pel->pel_name_len = pel_name_len;
                        new_pel->pel_name = pel_name;
                        new_pel->pel_mode = curdir->pel_mode;
                        new_pel->pel_next = NULL;
                        if (*pel != NULL) {
                                (*pel)->pel_next = new_pel;
                                *pel = new_pel;
                        } else {
                                *pel = new_pel;
                        }
                } else if (S_ISDIR(curdir->pel_mode) || S_ISLNK(curdir->pel_mode)){
                        params_match(pel_name, pregl->next, pel);
                        free(pel_name);
                        pel_name = NULL;
                }
                curdir = curdir->pel_next;
        }
out:
        if (curdir_list)
                params_free_entrylist(curdir_list);

        return rc;
}

/* list the matched entries */
int params_list(const char *pattern, struct params_entry_list **pel_ptr)
{
        /* 1. create regular expression list according to the pattern
         * 2. read the params entry back
         * 3. match the entries with regexp and return
         */
        int rc = 0;
        struct params_preg_list *preg_head = NULL;
        struct params_entry_list *pel = NULL;

        if (pattern == NULL) {
                fprintf(stderr, "error: %s: Null path pattern.\n", __FUNCTION__);
                return -EINVAL;
        }

        *pel_ptr = malloc(sizeof(struct params_entry_list));
        if (*pel_ptr == NULL) {
                fprintf(stderr,
                        "error: %s: No memory for pel_list.\n", __FUNCTION__);
                return -ENOMEM;
        }
        memset(*pel_ptr, 0, sizeof(struct params_entry_list));
        pel = *pel_ptr;

        preg_head = preg_list_create(pattern);
        if (preg_head) {
                rc = params_match("params_root", preg_head, &pel);
                params_free_preglist(preg_head);
                if ((*pel_ptr)->pel_next == NULL) {
                        fprintf(stderr, "param \"%s\" not found!\n", pattern);
                        rc = -ESRCH;
                }
        }

        return rc;
}

/* get parameters value */
int params_read(char *path, int path_len, char *read_buf,
                      int buf_len, long long *offset, int *eof)
{
        struct libcfs_ioctl_data data = { 0 };
        int rc = 0;
        int ioc_data_buflen = 0;
        char *ioc_data_buf = NULL;
        char *pathname = NULL;
        char *buf;

        if (!path || path_len <= 0) {
                fprintf(stderr, "error: %s: Path is null.\n", __FUNCTION__);
                rc = -EINVAL;
                goto out;
        }
        memset(read_buf, 0, buf_len);
        /* pack the parameters to ioc_data */
        data.ioc_inllen1 = path_len + 1;
        pathname = malloc(path_len + 1);
        if (!pathname) {
                fprintf(stderr,
                        "error: %s: No memory for path name.\n", __FUNCTION__);
                rc = -ENOMEM;
                goto out;
        }
        /* can't remove this memset because it will cause
         * libcfs_ioctl_is_invalid() failure:inlbuf1 not 0 terminated*/
        memset(pathname, 0, path_len + 1);
        strncpy(pathname, path, path_len);
        data.ioc_inlbuf1 = pathname;
        data.ioc_u64[0] = *offset;
        data.ioc_plen1 = buf_len;
        data.ioc_pbuf1 = read_buf;
        /* avoid buflen < packlen */
        ioc_data_buflen = libcfs_ioctl_packlen(&data);
        if (ioc_data_buflen <= PARAMS_BUFLEN_DEFAULT)
                ioc_data_buflen = PARAMS_BUFLEN_DEFAULT;
        ioc_data_buf = malloc(ioc_data_buflen);
        if (ioc_data_buf == NULL) {
                fprintf(stderr,
                        "error: %s: No memory for ioc_data.\n", __FUNCTION__);
                rc = -ENOMEM;
                goto out;
        }
        memset(ioc_data_buf, 0, ioc_data_buflen);
        /* read params values through libcfs_ioctl */
        buf = ioc_data_buf;
        rc = libcfs_ioctl_pack(&data, &buf, ioc_data_buflen);
        if (rc) {
                fprintf(stderr,
                        "error: %s: Failed to pack libcfs_ioctl data (%d).\n",
                        __FUNCTION__, rc);
                rc = -rc;
                goto out;
        }
        register_ioc_dev(LNET_DEV_ID, LNET_DEV_PATH,
                         LNET_DEV_MAJOR, LNET_DEV_MINOR);
        rc = l_ioctl(LNET_DEV_ID, IOC_LIBCFS_GET_PARAM, buf);
        if (rc) {
                fprintf(stderr, "error: %s: IOC_LIBCFS_GET_PARAM failed.\n",
                        __FUNCTION__);
                goto out;
        }
        *offset = ((struct libcfs_ioctl_data*)buf)->ioc_u64[0];
        *eof = ((struct libcfs_ioctl_data*)buf)->ioc_u32[1];
        rc = ((struct libcfs_ioctl_data*)buf)->ioc_u32[0];
out:
        if (pathname)
                free(pathname);
        if (ioc_data_buf)
                free(ioc_data_buf);

        return rc;
}

/* set parameters value */
int params_write(char *path, int path_len, char *write_buf, int buf_len,
                       int offset)
{
        struct libcfs_ioctl_data data = { 0 };
        int rc = 0;
        int ioc_data_buflen = 0;
        char *ioc_data_buf = NULL;
        char *pathname = NULL;
        char *buf;

        if (!path || path_len <= 0) {
                fprintf(stderr, "error: %s: Path is null.\n", __FUNCTION__);
                rc = -EINVAL;
                goto out;
        }

        /* pack the parameters to data first */
        data.ioc_inllen1 = path_len + 1;
        pathname = malloc(path_len + 1);
        if (!pathname) {
                fprintf(stderr,
                        "error: %s: No memory for path name.\n", __FUNCTION__);
                rc = -ENOMEM;
                goto out;
        }
        /* can't remove this memset because it will cause
         * libcfs_ioctl_is_invalid() failure:inlbuf1 not 0 terminated*/
        memset(pathname, 0, path_len + 1);
        strncpy(pathname, path, path_len);
        data.ioc_inlbuf1 = pathname;
        data.ioc_inlbuf2 = write_buf;
        data.ioc_inllen2 = buf_len + 1;
        /* avoid buflen < packlen */
        ioc_data_buflen = libcfs_ioctl_packlen(&data);
        if (ioc_data_buflen <= PARAMS_BUFLEN_DEFAULT)
                ioc_data_buflen = PARAMS_BUFLEN_DEFAULT;
        ioc_data_buf = malloc(ioc_data_buflen);
        if (ioc_data_buf == NULL) {
                fprintf(stderr,
                        "error: %s: No memory for ioc_data.\n", __FUNCTION__);
                rc = -ENOMEM;
                goto out;
        }
        memset(ioc_data_buf, 0, ioc_data_buflen);
        /* write params values through libcfs_ioctl */
        buf = ioc_data_buf;
        rc = libcfs_ioctl_pack(&data, &buf, ioc_data_buflen);
        if (rc) {
                fprintf(stderr,
                        "error: %s: Failed to pack libcfs_ioctl data (%d).\n",
                        __FUNCTION__, rc);
                rc = -rc;
                goto out;
        }
        /* XXX: l_getidentity can't recognize LNET_DEV_ID */
        register_ioc_dev(LNET_DEV_ID, LNET_DEV_PATH,
                         LNET_DEV_MAJOR, LNET_DEV_MINOR);
        rc = l_ioctl(LNET_DEV_ID, IOC_LIBCFS_SET_PARAM, buf);
        if (rc) {
                fprintf(stderr, "error: %s: IOC_LIBCFS_SET_PARAM failed.\n",
                        __FUNCTION__);
                goto out;
        }
        rc = ((struct libcfs_ioctl_data*)buf)->ioc_u32[0];
out:
        if (pathname)
                free(pathname);
        if (ioc_data_buf)
                free(ioc_data_buf);

        return rc;
}

int params_value_output(struct libcfs_param_data data, char *outbuf)
{
        switch (data.param_type) {
                case LP_D16: {
                        short temp;
                        memcpy(&temp, data.param_value, data.param_value_len);
                        sprintf(outbuf, "%d", temp);
                        break; }
                case LP_D32: {
                        int temp;
                        memcpy(&temp, data.param_value, data.param_value_len);
                        sprintf(outbuf, "%d", temp);
                        break; }
                case LP_D64: {
                        long long temp;
                        memcpy(&temp, data.param_value, data.param_value_len);
                        sprintf(outbuf, "%lld", temp);
                        break; }
                case LP_U8: {
                        __u8 temp;
                        memcpy(&temp, data.param_value, data.param_value_len);
                        sprintf(outbuf, "%u", temp);
                        break; }
                case LP_U16: {
                        __u16 temp;
                        memcpy(&temp, data.param_value, data.param_value_len);
                        sprintf(outbuf, "%u", temp);
                        break; }
                case LP_U32: {
                        __u32 temp;
                        memcpy(&temp, data.param_value, data.param_value_len);
                        sprintf(outbuf, "%u", temp);
                        break; }
                case LP_U64: {
                        __u64 temp;
                        memcpy(&temp, data.param_value, data.param_value_len);
                        sprintf(outbuf, "%llu", temp);
                        break; }
                case LP_DB:
                case LP_STR:
                        if (data.param_value[data.param_value_len - 1] == '\n')
                                data.param_value[data.param_value_len - 1]='\0';
                        sprintf(outbuf, "%s", data.param_value);
                        break;
                default:
                        fprintf(stderr,
                                "warning: %s: unknown libcfs_param_data_type"
                                " (%d).\n", __FUNCTION__, data.param_type);
                        return 0;
        }

        return strlen(outbuf);
}

/* one record each time */
int params_unpack(char *inbuf, char *outbuf, int outbuf_len)
{
        struct libcfs_param_data data;

        if (*inbuf == '\0')
                return 0;

        if (!libcfs_param_unpack(&data, inbuf)) {
                if (data.param_name) {
                        sprintf(outbuf, "%s\t", data.param_name);
                        outbuf += data.param_name_len + 1;
                }
                if (data.param_value)
                        outbuf += params_value_output(data, outbuf);
                if (data.param_unit) {
                        sprintf(outbuf, "%s\n", data.param_unit);
                        outbuf += data.param_unit_len + 1;
                } else {
                        sprintf(outbuf, "%s", "\n");
                        outbuf += 1;
                }
                libcfs_param_free_value(&data);
        }

        return libcfs_param_packlen(&data);
}

/* Free params_entry_list */
void params_free_entrylist(struct params_entry_list *entry_list)
{
        struct params_entry_list *pel;

        while (entry_list != NULL) {
                pel = entry_list;
                entry_list = entry_list->pel_next;
                free(pel);
        }
}

