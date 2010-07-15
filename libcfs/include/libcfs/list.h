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
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#ifndef __LIBCFS_LIST_H__
#define __LIBCFS_LIST_H__

#if !(defined (__linux__) && defined(__KERNEL__))

#ifndef __WINNT__
#define prefetch(a) ((void)a)
#else
#define prefetch(a) ((void *)a)
#endif

#endif /* !(defined (__linux__) && defined(__KERNEL__)) */

/*
 * Doubly linked lists.
 */

struct cfs_list_head {
        struct cfs_list_head *next;
        struct cfs_list_head *prev;
};

typedef struct cfs_list_head cfs_list_t;

#define CFS_LIST_HEAD_INIT(name) { &(name), &(name) }

#define CFS_LIST_HEAD(name) \
        struct cfs_list_head name = CFS_LIST_HEAD_INIT(name)

#define CFS_INIT_LIST_HEAD(ptr) do {              \
        (ptr)->next = (ptr); (ptr)->prev = (ptr); \
} while (0)

/**
 * Insert an entry at the start of a list.
 * \param entry  new entry to be inserted
 * \param head   list to add it to
 *
 * Insert a new entry after the specified head.
 * This is good for implementing stacks.
 */
static inline void cfs_list_add(struct cfs_list_head *entry,
    struct cfs_list_head *head)
{
        entry->next = head->next;
        entry->prev = head;
        head->next->prev = entry;
        head->next = entry;
}

/**
 * Insert an entry at the end of a list.
 * \param entry  new entry to be inserted
 * \param head   list to add it to
 *
 * Insert a new entry before the specified head.
 * This is useful for implementing queues.
 */
static inline void cfs_list_add_tail(struct cfs_list_head *entry,
    struct cfs_list_head *head)
{
        entry->next = head;
        entry->prev = head->prev;
        head->prev->next = entry;
        head->prev = entry;
}

/**
 * Remove an entry from the list it is currently in.
 * \param entry the entry to remove
 * Note: cfs_list_empty(entry) does not return true after this, the entry is
 * in an undefined state.
 */
static inline void cfs_list_del(struct cfs_list_head *entry)
{
        entry->prev->next = entry->next;
        entry->next->prev = entry->prev;
}

/**
 * Remove an entry from the list it is currently in and reinitialize it.
 * \param entry the entry to remove.
 */
static inline void cfs_list_del_init(struct cfs_list_head *entry)
{
        entry->prev->next = entry->next;
        entry->next->prev = entry->prev;
        CFS_INIT_LIST_HEAD(entry);
}

/**
 * Remove an entry from the list it is currently in and insert it at the start of another list.
 * \param entry the entry to move
 * \param head the list to move it to
 */
static inline void cfs_list_move(struct cfs_list_head *entry,
    struct cfs_list_head *head)
{
        entry->prev->next = entry->next;
        entry->next->prev = entry->prev;

        entry->next = head->next;
        entry->prev = head;
        head->next->prev = entry;
        head->next = entry;

}

/**
 * Remove an entry from the list it is currently in and insert it at the end of another list.
 * \param entry the entry to move
 * \param head the list to move it to
 */
static inline void cfs_list_move_tail(struct cfs_list_head *entry,
    struct cfs_list_head *head)
{
        entry->prev->next = entry->next;
        entry->next->prev = entry->prev;

        entry->next = head;
        entry->prev = head->prev;
        head->prev->next = entry;
        head->prev = entry;
}

/**
 * Test whether a list is empty
 * \param head the list to test.
 */
static inline int cfs_list_empty(struct cfs_list_head *head)
{
        return (head->prev == head);
}

/**
 * Test whether a list is empty and not being modified
 * \param head the list to test
 *
 * Tests whether a list is empty _and_ checks that no other CPU might be
 * in the process of modifying either member (next or prev)
 *
 * NOTE: using cfs_list_empty_careful() without synchronization
 * can only be safe if the only activity that can happen
 * to the list entry is cfs_list_del_init(). Eg. it cannot be used
 * if another CPU could re-cfs_list_add() it.
 */
static inline int cfs_list_empty_careful(const struct cfs_list_head *head)
{
        struct cfs_list_head *next = head->next;
        return (next == head) && (next == head->prev);
}

/**
 * Join two lists
 * \param list the new list to add.
 * \param head the place to add it in the first list.
 *
 * The contents of \a list are added at the start of \a head.  \a list is in an
 * undefined state on return.
 */
static inline void cfs_list_splice(struct cfs_list_head *list,
    struct cfs_list_head *head)
{
        if (list->next != list) {
                list->next->prev = head;
                list->prev->next = head->next;
                head->next->prev = list->prev;
                head->next = list->next;
        }
}

/**
 * Join two lists and reinitialise the emptied list.
 * \param list the new list to add.
 * \param head the place to add it in the first list.
 *
 * The contents of \a list are added at the start of \a head.  \a list is empty
 * on return.
 */
static inline void cfs_list_splice_init(struct cfs_list_head *list,
    struct cfs_list_head *head)
{
        if (list->next != list) {
                list->next->prev = head;
                list->prev->next = head->next;
                head->next->prev = list->prev;
                head->next = list->next;
                CFS_INIT_LIST_HEAD(list);
        }
}

/**
 * Get the container of a list
 * \param ptr    the embedded list.
 * \param type   the type of the struct this is embedded in.
 * \param member the member name of the list within the struct.
 */
#define cfs_list_entry(ptr, type, member)                               \
        ((type *)((char *)(ptr)-(unsigned long)(&((type *)0)->member)))

/**
 * Iterate over a list
 * \param entry the iterator
 * \param head  the list to iterate over
 *
 * Behaviour is undefined if \a pos is removed from the list in the body of the
 * loop.
 */
#define cfs_list_for_each(entry, head)                                     \
        for (entry = (head)->next; prefetch(entry->next), entry != (head); \
             entry = entry->next)

/**
 * iterate over a list safely
 * \param entry   the iterator
 * \param ntmp    temporary storage
 * \param head    the list to iterate over
 *
 * This is safe to use if \a pos could be removed from the list in the body of
 * the loop.
 */
#define cfs_list_for_each_safe(entry, ntmp, head)                       \
        for (entry = (head)->next, ntmp = entry->next;                  \
             prefetch(ntmp), entry != (head);                           \
             entry = ntmp, ntmp = entry->next)

/**
 * Iterate over a list in reverse order
 * \param entry the &struct list_head to use as a loop counter.
 * \param head  the head for your list.
 */
#define cfs_list_for_each_prev(entry, head)                                \
        for (entry = (head)->prev; prefetch(entry->prev), entry != (head); \
             entry = entry->prev)

/**
 * Iterate over a list of given type
 * \param entry      the type * to use as a loop counter.
 * \param head       the head for your list.
 * \param member     the name of the list_struct within the struct.
 */
#define cfs_list_for_each_entry(entry, head, member)                           \
        for (entry = cfs_list_entry((head)->next, typeof(*entry), member);     \
             prefetch(entry->member.next), &entry->member != (head);           \
             entry = cfs_list_entry(entry->member.next, typeof(*entry), member))

/**
 * Continue iterateing over a list of given type 
 * \param entry      the type * to use as a loop counter.
 * \param head       the head for your list.
 * \param member     the name of the list_struct within the struct.
 */
#define cfs_list_for_each_entry_continue(entry, head, member)                    \
        for (entry = cfs_list_entry(entry->member.next, typeof(*entry), member); \
             prefetch(entry->member.next), &entry->member != (head);             \
             entry = cfs_list_entry(entry->member.next, typeof(*entry), member))

/**
 * Iterate over a list of given type safe against removal of list entry
 * \param entry      the type * to use as a loop counter.
 * \param ntmp       another type * to use as temporary storage
 * \param head       the head for your list.
 * \param member     the name of the list_struct within the struct.
 */
#define cfs_list_for_each_entry_safe(entry, ntmp, head, member)            \
        for (entry = cfs_list_entry((head)->next, typeof(*entry), member), \
             ntmp = cfs_list_entry(entry->member.next, typeof(*entry), member); \
             prefetch(ntmp), &entry->member != (head);                     \
             entry = ntmp, ntmp = cfs_list_entry(ntmp->member.next,        \
                 typeof(*ntmp), member))

/**
 * Iterate over a list continuing from an existing point
 * \param entry      the type * to use as a loop cursor.
 * \param ntmp       another type * to use as temporary storage
 * \param head       the head for your list.
 * \param member     the name of the list_struct within the struct.
 *
 * Iterate over list of given type from current point, safe against
 * removal of list entry.
 */
#define cfs_list_for_each_entry_safe_from(entry, ntmp, head, member)       \
        for (ntmp = cfs_list_entry(entry->member.next, typeof(*entry),     \
                                   member);                                \
             prefetch(ntmp), &entry->member != (head);                     \
             entry = ntmp, ntmp = cfs_list_entry(ntmp->member.next,        \
                 typeof(*ntmp), member))

#define cfs_list_for_each_entry_safe_from_typed(entry, ntmp, head, type, \
                                                member)                  \
        for (ntmp = cfs_list_entry(entry->member.next, type, member);    \
             prefetch(ntmp), &entry->member != (head);                   \
             entry = ntmp, ntmp = cfs_list_entry(ntmp->member.next,      \
                 type, member))

#define cfs_list_for_each_entry_typed(entry, head, type, member)        \
        for (entry = cfs_list_entry((head)->next, type, member);        \
             prefetch(entry->member.next), &entry->member != (head);    \
             entry = cfs_list_entry(entry->member.next, type, member))

#define cfs_list_for_each_entry_safe_typed(entry, ntmp, head, type, member)   \
    for (entry = cfs_list_entry((head)->next, type, member),                  \
         ntmp = cfs_list_entry(entry->member.next, type, member);             \
         &entry->member != (head);                                            \
         entry = ntmp, ntmp = cfs_list_entry(ntmp->member.next, type, member))

/**
 * Iterate backwards over a list of given type.
 * \param entry      the type * to use as a loop counter.
 * \param head       the head for your list.
 * \param member     the name of the list_struct within the struct.
 */
#define cfs_list_for_each_entry_reverse(entry, head, member)                   \
        for (entry = cfs_list_entry((head)->prev, typeof(*entry), member);     \
             prefetch(entry->member.prev), &entry->member != (head);           \
             entry = cfs_list_entry(entry->member.prev, typeof(*entry), member))

#define cfs_list_for_each_entry_reverse_typed(entry, head, type, member) \
        for (entry = cfs_list_entry((head)->prev, type, member);         \
             prefetch(entry->member.prev), &entry->member != (head);     \
             entry = cfs_list_entry(entry->member.prev, type, member))

/**
 * \defgroup hlist Hash List
 * Double linked lists with a single pointer list head.
 * Useful for hash tables.
 * @{
 */

typedef struct cfs_hlist_head {
        struct cfs_hlist_node *next;
} cfs_hlist_head_t;

typedef struct cfs_hlist_node {
        struct cfs_hlist_node *next;
        struct cfs_hlist_node *prev;
} cfs_hlist_node_t;

/* @} */

/*
 * "NULL" might not be defined at this point
 */
#ifdef NULL
#define NULL_P NULL
#else
#define NULL_P ((void *)0)
#endif

/**
 * \addtogroup hlist
 * @{
 */

#define CFS_HLIST_HEAD_INIT { .next = NULL_P }
#define CFS_HLIST_HEAD(name) struct cfs_hlist_head name = CFS_HLIST_HEAD_INIT
#define CFS_INIT_HLIST_HEAD(ptr) ((ptr)->next = NULL_P)
#define CFS_INIT_HLIST_NODE(ptr) ((ptr)->next = NULL_P, (ptr)->prev = NULL_P)

static inline int cfs_hlist_unhashed(const struct cfs_hlist_node *entry)
{
        return (entry->prev == NULL_P);
}

static inline int cfs_hlist_empty(const struct cfs_hlist_head *head)
{
        return (head->next == NULL_P);
}

static inline void cfs_hlist_del(struct cfs_hlist_node *entry)
{
        entry->prev->next = entry->next;
        if (entry->next != NULL_P) {
                entry->next->prev = entry->prev;
        }
}

static inline void cfs_hlist_del_init(struct cfs_hlist_node *entry)
{
        if (entry->prev != NULL_P)  {
                entry->prev->next = entry->next;
                if (entry->next != NULL_P) {
                        entry->next->prev = entry->prev;
                }
                CFS_INIT_HLIST_NODE(entry);
        }
}

static inline void cfs_hlist_add_head(struct cfs_hlist_node *entry,
    struct cfs_hlist_head *head)
{
        entry->prev = (struct cfs_hlist_node *)head;
        entry->next = head->next;
        if (head->next != NULL_P) {
                head->next->prev = entry;
        }
        head->next = entry;
}

static inline void cfs_hlist_add_before(struct cfs_hlist_node *entry,
    struct cfs_hlist_node *nexte)
{
        entry->prev = nexte->prev;
        entry->next = nexte;
        nexte->prev->next = entry;
        nexte->prev = entry;
}

static inline void cfs_hlist_add_after(struct cfs_hlist_node *preve,
    struct cfs_hlist_node *entry)
{
        entry->prev = preve;
        entry->next = preve->next;
        if (preve->next != NULL_P) {
                preve->next->prev = entry;
        }
        preve->next = entry;
}

#define cfs_hlist_entry(ptr, type, member) \
        ((type *)((char *)(ptr)-(unsigned long)(&((type *)0)->member)))

#define cfs_hlist_for_each(entry, head) \
        for (entry = (head)->next; entry && (prefetch(entry->next), 1); \
             entry = entry->next)

#define cfs_hlist_for_each_safe(entry, ntmp, head)                      \
        for (entry = (head)->next;                                      \
             entry && (ntmp = entry->next, prefetch(ntmp), 1);          \
             entry = ntmp)

/**
 * Iterate over an hlist of given type
 * \param tentry   the type * to use as a loop counter.
 * \param entry    the &struct hlist_node to use as a loop counter.
 * \param head     the head for your list.
 * \param member   the name of the hlist_node within the struct.
 */
#define cfs_hlist_for_each_entry(tentry, entry, head, member)              \
        for (entry = (head)->next;                                         \
             entry && (prefetch(entry->next),                              \
             tentry = cfs_hlist_entry(entry, typeof(*tentry), member), 1); \
             entry = entry->next)

/**
 * Iterate over an hlist continuing after existing point
 * \param tentry   the type * to use as a loop counter.
 * \param entry    the &struct hlist_node to use as a loop counter.
 * \param member   the name of the hlist_node within the struct.
 */
#define cfs_hlist_for_each_continue(tentry, entry, member)                 \
        for (entry = (entry)->next;                                        \
             entry && (prefetch(entry->next),                              \
             tentry = cfs_hlist_entry(entry, typeof(*tentry), member), 1); \
             entry = entry->next)

/**
 * Iterate over an hlist continuing from an existing point
 * \param tentry   the type * to use as a loop counter.
 * \param entry    the &struct hlist_node to use as a loop counter.
 * \param member   the name of the hlist_node within the struct.
 */
#define cfs_hlist_for_each_from(tentry, entry, member)                       \
        for (; entry && (prefetch(entry->next),                              \
               tentry = cfs_hlist_entry(entry, typeof(*tentry), member), 1); \
             entry = entry->next)

/**
 * Iterate over an hlist of given type safe against removal of list entry
 * \param tentry the type * to use as a loop counter.
 * \param entry  the &struct hlist_node to use as a loop counter.
 * \param ntmp   another &struct hlist_node to use as temporary storage
 * \param head   the head for your list.
 * \param member the name of the hlist_node within the struct.
 */
#define cfs_hlist_for_each_entry_safe(tentry, entry, ntmp, head, member)      \
        for (entry = (head)->next;                                            \
             entry && (ntmp = entry->next, prefetch(ntmp),                    \
             tentry = cfs_hlist_entry(entry, typeof(*tentry), member), 1);    \
             entry = ntmp)

#define cfs_hlist_for_each_entry_typed(tentry, entry, head, type, member) \
        for (entry = (head)->next;                                        \
             entry && (prefetch(entry->next),                             \
             tentry = cfs_hlist_entry(entry, type, member), 1);           \
             entry = entry->next)

#define cfs_hlist_for_each_entry_safe_typed(tentry, entry, ntmp, head, type, \
                                            member)                          \
        for (entry = (head)->next;                                           \
             entry && (ntmp = entry->next, prefetch(ntmp),                   \
             tentry = cfs_hlist_entry(entry, type, member), 1);              \
             entry = ntmp)

/* @} */

#endif /* __LIBCFS_LUSTRE_LIST_H__ */
