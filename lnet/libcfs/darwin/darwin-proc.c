/* -*- mode: c; c-basic-offset: 8; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2001, 2002 Cluster File Systems, Inc.
 *
 *   This file is part of Lustre, http://www.lustre.org.
 *
 *   Lustre is free software; you can redistribute it and/or
 *   modify it under the terms of version 2 of the GNU General Public
 *   License as published by the Free Software Foundation.
 *
 *   Lustre is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Lustre; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/proc.h>
#include <sys/unistd.h>
#include <mach/mach_types.h>

#define DEBUG_SUBSYSTEM S_LNET

#include <libcfs/libcfs.h>

#define LIBCFS_SYSCTL           "libcfs"
#define LIBCFS_SYSCTL_SPRITE    "sprite"
#define LIBCFS_SYSCTL_MAGIC     0xbabeface

static struct libcfs_sysctl_sprite {
        int                     ss_magic;
        struct sysctl_oid_list  *ss_link;
} libcfs_sysctl_sprite = { 0, NULL };

static cfs_sysctl_table_header_t *libcfs_table_header = NULL;
extern unsigned int libcfs_debug;
extern unsigned int libcfs_subsystem_debug;
extern unsigned int libcfs_printk;
extern unsigned int libcfs_console_ratelimit;
extern unsigned int libcfs_catastrophe;
extern atomic_t libcfs_kmemory;

extern long max_debug_mb;
extern int cfs_trace_daemon SYSCTL_HANDLER_ARGS;
extern int cfs_debug_mb SYSCTL_HANDLER_ARGS;
/*
 * sysctl table for lnet
 */

SYSCTL_NODE (,		        OID_AUTO,	lnet,	CTLFLAG_RW,
	     0,			"lnet sysctl top");

SYSCTL_INT(_lnet,		        OID_AUTO,	debug,
	     CTLTYPE_INT | CTLFLAG_RW ,			&libcfs_debug,
	     0,		"debug");
SYSCTL_INT(_lnet,		        OID_AUTO,	subsystem_debug,
	     CTLTYPE_INT | CTLFLAG_RW,			&libcfs_subsystem_debug,
	     0,		"subsystem debug");
SYSCTL_INT(_lnet,		        OID_AUTO,	printk,
	     CTLTYPE_INT | CTLFLAG_RW,			&libcfs_printk,
	     0,		"printk");
SYSCTL_INT(_lnet,		        OID_AUTO,	console_ratelimit,
	     CTLTYPE_INT | CTLFLAG_RW,			&libcfs_console_ratelimit,
	     0,		"console_ratelimit");
SYSCTL_STRING(_lnet,		        OID_AUTO,	debug_path,
	     CTLTYPE_STRING | CTLFLAG_RW,		debug_file_path,
	     1024,	"debug path");
SYSCTL_INT(_lnet,		        OID_AUTO,	memused,
	     CTLTYPE_INT | CTLFLAG_RW,			(int *)&libcfs_kmemory.counter,
	     0,		"memused");
SYSCTL_INT(_lnet,		        OID_AUTO,	catastrophe,
	     CTLTYPE_INT | CTLFLAG_RW,			(int *)&libcfs_catastrophe,
	     0,		"catastrophe");
SYSCTL_PROC(_lnet,		        OID_AUTO,	trace_daemon,
	     CTLTYPE_STRING | CTLFLAG_RW,		0,
	     0,		&cfs_trace_daemon,		"A",	"trace daemon");
SYSCTL_PROC(_lnet,		        OID_AUTO,	debug_mb,
	     CTLTYPE_INT | CTLFLAG_RW,		        &max_debug_mb,
	     0,		&cfs_debug_mb,		        "L",	"max debug size");


static cfs_sysctl_table_t	top_table[] = {
	&sysctl__lnet,
	&sysctl__lnet_debug,
	&sysctl__lnet_subsystem_debug,
	&sysctl__lnet_printk,
	&sysctl__lnet_console_ratelimit,
	&sysctl__lnet_debug_path,
	&sysctl__lnet_memused,
	&sysctl__lnet_catastrophe,
	&sysctl__lnet_trace_daemon,
	&sysctl__lnet_debug_mb,
	NULL
};

/*
 * Register sysctl table
 */
cfs_sysctl_table_header_t *
cfs_register_sysctl_table (cfs_sysctl_table_t *table, int arg)
{
        cfs_sysctl_table_t      item;
        int i = 0;

        while ((item = table[i++]) != NULL) 
                sysctl_register_oid(item);
        return table;
}

/*
 * Unregister sysctl table
 */
void
cfs_unregister_sysctl_table (cfs_sysctl_table_header_t *table) {
        int i = 0;
        cfs_sysctl_table_t      item;

        while ((item = table[i++]) != NULL) 
                sysctl_unregister_oid(item);
        return;
}

/*
 * Allocate a sysctl oid. 
 */
static struct sysctl_oid *
cfs_alloc_sysctl(struct sysctl_oid_list *parent, int nbr, int access,
                 const char *name, void *arg1, int arg2, const char *fmt,
                 int (*handler) SYSCTL_HANDLER_ARGS)
{
        struct sysctl_oid *oid;
        char    *sname = NULL;
        char    *sfmt = NULL;

        if (strlen(name) + 1 > CTL_MAXNAME) {
                printf("libcfs: sysctl name: %s is too long.\n", name);
                return NULL;
        }
        oid = (struct sysctl_oid*)_MALLOC(sizeof(struct sysctl_oid), 
                                          M_TEMP, M_WAITOK | M_ZERO);
        if (oid == NULL) 
                return NULL;

        sname = (char *)_MALLOC(sizeof(CTL_MAXNAME), 
                                M_TEMP, M_WAITOK | M_ZERO);
        if (sname == NULL) 
                goto error;
        strcpy(sname, name);

        sfmt = (char *)_MALLOC(4, M_TEMP, M_WAITOK | M_ZERO);
        if (sfmt == NULL) 
                goto error;
        strcpy(sfmt, fmt);

        if (parent == NULL)
                oid->oid_parent = &sysctl__children;
        else
                oid->oid_parent = parent;
        oid->oid_number = nbr;
        oid->oid_kind = access;
        oid->oid_name = sname;
        oid->oid_handler = handler;
        oid->oid_fmt = sfmt;

        if ((access & CTLTYPE) == CTLTYPE_NODE){
                /* It's a sysctl node */
                struct sysctl_oid_list *link;

                link = (struct sysctl_oid_list *)_MALLOC(sizeof(struct sysctl_oid_list), 
                                                         M_TEMP, M_WAITOK | M_ZERO);
                if (link == NULL)
                        goto error;
                oid->oid_arg1 = link;
                oid->oid_arg2 = 0;
        } else {
                oid->oid_arg1 = arg1;
                oid->oid_arg2 = arg2;
        }

        return oid;
error:
        if (sfmt != NULL)
                _FREE(sfmt, M_TEMP);
        if (sname != NULL)
                _FREE(sname, M_TEMP);
        if (oid != NULL)
                _FREE(oid, M_TEMP);
        return NULL;
}

void cfs_free_sysctl(struct sysctl_oid *oid)
{
        if (oid->oid_name != NULL)
                _FREE((void *)oid->oid_name, M_TEMP);
        if (oid->oid_fmt != NULL)
                _FREE((void *)oid->oid_fmt, M_TEMP);
        if ((oid->oid_kind & CTLTYPE_NODE != 0) && oid->oid_arg1)
                /* XXX Liang: need to assert the list is empty */
                _FREE(oid->oid_arg1, M_TEMP);
        _FREE(oid, M_TEMP);
}

#define CFS_SYSCTL_ISVALID ((libcfs_sysctl_sprite.ss_magic == LIBCFS_SYSCTL_MAGIC) && \
                            (libcfs_sysctl_sprite.ss_link != NULL))       

int
cfs_sysctl_isvalid(void)
{
        return CFS_SYSCTL_ISVALID;
}

struct sysctl_oid *
cfs_alloc_sysctl_node(struct sysctl_oid_list *parent, int nbr, int access,
                      const char *name, int (*handler) SYSCTL_HANDLER_ARGS)
{
        if (parent == NULL && CFS_SYSCTL_ISVALID)
                parent = libcfs_sysctl_sprite.ss_link;
        return cfs_alloc_sysctl(parent, nbr, CTLTYPE_NODE | access, name,
                                NULL, 0, "N", handler);
}

struct sysctl_oid *
cfs_alloc_sysctl_int(struct sysctl_oid_list *parent, int nbr, int access,
                     const char *name, int *ptr, int val)
{
        if (parent == NULL && CFS_SYSCTL_ISVALID)
                parent = libcfs_sysctl_sprite.ss_link;
        return cfs_alloc_sysctl(parent, nbr, CTLTYPE_INT | access, name, 
                                ptr, val, "I", sysctl_handle_int);
}

struct sysctl_oid *
cfs_alloc_sysctl_long(struct sysctl_oid_list *parent, int nbr, int access,
                      const char *name, int *ptr, int val)
{
        if (parent == NULL && CFS_SYSCTL_ISVALID)
                parent = libcfs_sysctl_sprite.ss_link;
        return cfs_alloc_sysctl(parent, nbr, CTLTYPE_INT | access, name, 
                                ptr, val, "L", sysctl_handle_long);
}

struct sysctl_oid *
cfs_alloc_sysctl_string(struct sysctl_oid_list *parent, int nbr, int access,
                        const char *name, char *ptr, int len)
{
        if (parent == NULL && CFS_SYSCTL_ISVALID)
                parent = libcfs_sysctl_sprite.ss_link;
        return cfs_alloc_sysctl(parent, nbr, CTLTYPE_STRING | access, name, 
                                ptr, len, "A", sysctl_handle_string);
}

struct sysctl_oid *
cfs_alloc_sysctl_struct(struct sysctl_oid_list *parent, int nbr, int access,
                        const char *name, void *ptr, int size)
{
        if (parent == NULL && CFS_SYSCTL_ISVALID)
                parent = libcfs_sysctl_sprite.ss_link;
        return cfs_alloc_sysctl(parent, nbr, CTLTYPE_OPAQUE | access, name,
                                ptr, size, "S", sysctl_handle_opaque);
}

/* no proc in osx */
cfs_proc_dir_entry_t *
cfs_create_proc_entry(char *name, int mod, cfs_proc_dir_entry_t *parent)
{
	cfs_proc_dir_entry_t *entry;
	MALLOC(entry, cfs_proc_dir_entry_t *, sizeof(cfs_proc_dir_entry_t), M_TEMP, M_WAITOK|M_ZERO);

	return  entry;
}

void
cfs_free_proc_entry(cfs_proc_dir_entry_t *de){
	FREE(de, M_TEMP);
	return;
};

void
cfs_remove_proc_entry(char *name, cfs_proc_dir_entry_t *entry)
{
	cfs_free_proc_entry(entry);
	return;
}

int
insert_proc(void)
{
#if 1
        if (!libcfs_table_header) 
                libcfs_table_header = cfs_register_sysctl_table(top_table, 0);
#endif
	return 0;
}

void
remove_proc(void)
{
#if 1
        if (libcfs_table_header != NULL) 
                cfs_unregister_sysctl_table(libcfs_table_header); 
        libcfs_table_header = NULL;
#endif
	return;
}

int
cfs_sysctl_init(void)
{
        struct sysctl_oid               *oid_root;
        struct sysctl_oid               *oid_sprite;
        struct libcfs_sysctl_sprite     *sprite;
        size_t  len; 
        int     rc;

        len = sizeof(struct libcfs_sysctl_sprite);
        rc = sysctlbyname("libcfs.sprite", 
                          (void *)&libcfs_sysctl_sprite, &len, NULL, 0);
        if (rc == 0) {
                /* 
                 * XXX Liang: assert (rc == 0 || rc == ENOENT)
                 *
                 * libcfs.sprite has been registered by previous 
                 * loading of libcfs 
                 */
                if (libcfs_sysctl_sprite.ss_magic != LIBCFS_SYSCTL_MAGIC) {
                        printf("libcfs: magic number of libcfs.sprite "
                               "is not right (%lx, %lx)\n", 
                               libcfs_sysctl_sprite.ss_magic,
                               LIBCFS_SYSCTL_MAGIC);
                        return -1;
                }
                assert(libcfs_sysctl_sprite.ss_link != NULL);
                printf("libcfs: registered libcfs.sprite found.\n");
                return 0;
        }
        oid_root = cfs_alloc_sysctl_node(NULL, OID_AUTO, CTLFLAG_RD | CTLFLAG_KERN,
                                         LIBCFS_SYSCTL, 0);
        if (oid_root == NULL)
                return -1;
        sysctl_register_oid(oid_root);

        sprite = (struct libcfs_sysctl_sprite *)_MALLOC(sizeof(struct libcfs_sysctl_sprite), 
                                                        M_TEMP, M_WAITOK | M_ZERO);
        if (sprite == NULL) {
                sysctl_unregister_oid(oid_root);
                cfs_free_sysctl(oid_root);
                return -1;
        }
        sprite->ss_magic = LIBCFS_SYSCTL_MAGIC;
        sprite->ss_link = (struct sysctl_oid_list *)oid_root->oid_arg1;
        oid_sprite = cfs_alloc_sysctl_struct((struct sysctl_oid_list *)oid_root->oid_arg1, 
                                             OID_AUTO, CTLFLAG_RD | CTLFLAG_KERN, 
                                             LIBCFS_SYSCTL_SPRITE, sprite, 
                                             sizeof(struct libcfs_sysctl_sprite));
        if (oid_sprite == NULL) {
                cfs_free_sysctl(oid_sprite);
                sysctl_unregister_oid(oid_root);
                cfs_free_sysctl(oid_root);
                return -1;
        }
        sysctl_register_oid(oid_sprite);

        libcfs_sysctl_sprite.ss_magic = sprite->ss_magic;
        libcfs_sysctl_sprite.ss_link = sprite->ss_link;

        return 0;
}

void
cfs_sysctl_fini(void)
{
        libcfs_sysctl_sprite.ss_magic = 0;
        libcfs_sysctl_sprite.ss_link = NULL;
}

