#ifndef   	XLAB_LIST_H_
#define   	XLAB_LIST_H_

#include <stddef.h>

#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif

#define container_of(ptr, type, member) ({                      \
      const __typeof( ((type *)0)->member ) *__mptr = (ptr);      \
      (type *)( (char *)__mptr - offsetof(type,member) );})


struct xlab_list
{
    struct xlab_list *prev, *next;
};

static inline void xlab_list_init(struct xlab_list *list)
{
    list->next = list;
    list->prev = list;
}

static inline void __xlab_list_add(struct xlab_list *new, struct xlab_list *prev,
                                 struct xlab_list *next)
{
    next->prev = new;
    new->next = next;
    new->prev = prev;
    prev->next = new;
}

static inline void xlab_list_add(struct xlab_list *new, struct xlab_list *head)
{
    __xlab_list_add(new, head->prev, head);
}

static inline void __xlab_list_del(struct xlab_list *prev, struct xlab_list *next)
{
    prev->next = next;
    next->prev = prev;
}

static inline void xlab_list_del(struct xlab_list *entry)
{
    __xlab_list_del(entry->prev, entry->next);
    entry->prev = NULL;
    entry->next = NULL;
}

static inline int xlab_list_is_empty(struct xlab_list *head)
{
    if (head->next == head) return 0;
    else return -1;
}

#define xlab_list_foreach(curr, head) for( curr = (head)->next; curr != (head); curr = curr->next )
#define xlab_list_foreach_safe(curr, n, head) \
    for (curr = (head)->next, n = curr->next; curr != (head); curr = n, n = curr->next)

#define xlab_list_entry( ptr, type, member ) container_of( ptr, type, member )

/* First node of the list */
#define xlab_list_entry_first(ptr, type, member) container_of(ptr->next, type, member)

/* Last node of the list */
#define xlab_list_entry_last(ptr, type, member) container_of(ptr->prev, type, member)

/* Next node */
#define xlab_list_entry_next(ptr, type, member, head)                     \
    ptr->next == (head) ? container_of((head)->next, type, member) :    \
        container_of(ptr->next, type, member);

#endif /* !XLAB_LIST_H_ */
