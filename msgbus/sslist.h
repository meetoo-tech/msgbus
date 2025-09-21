#ifndef __SINGLE_LINK_LIST_H__
#define __SINGLE_LINK_LIST_H__

#include <stddef.h>

struct slist_head
{
    struct slist_head *next;
};
#define SLIST_HEAD_INIT() {NULL}
#define SLIST_HEAD(name) struct slist_head name = SLIST_HEAD_INIT()

static inline void SINIT_LIST_HEAD(struct slist_head *slist)
{
    slist->next = NULL;
}

static inline void slist_add(struct slist_head *new_node, struct slist_head *head)
{
    new_node->next = head->next;
    head->next = new_node;
}

static inline void slist_add_tail(struct slist_head *new_node, struct slist_head *head)
{
    struct slist_head *node;

    node = head;
    while (node->next)
        node = node->next;

    /* append the node to the tail */
    node->next = new_node;
    new_node->next = NULL;
}

static inline void slist_del(struct slist_head *entry, struct slist_head *head)
{
    /* remove slist head */
    struct slist_head *node = head;
    while (node->next && node->next != entry)
        node = node->next;

    /* remove node */
    if (node->next != NULL)
        node->next = node->next->next;
}

static inline void slist_replace(struct slist_head *old, struct slist_head *new_node, struct slist_head *head)
{
    struct slist_head *node = head;
    while (node->next && node->next != old)
        node = node->next;

    if (node->next != NULL)
    {
        new_node->next = node->next->next;
        node->next = new_node;
    }
}

static inline int slist_empty(const struct slist_head *head)
{
    return head->next == NULL;
}

#undef container_of
#define container_of(ptr, type, member) ((type *)(((uintptr_t)ptr) - offsetof(type, member)))

#define slist_entry(ptr, type, member) container_of(ptr, type, member)

#define slist_for_each(pos, head) for (pos = (head)->next; pos != NULL; pos = pos->next)

#define slist_for_each_safe(pos, n, head) \
    for (pos = (head)->next, n = pos->next; pos != NULL; pos = n, n = pos->next)

#endif
