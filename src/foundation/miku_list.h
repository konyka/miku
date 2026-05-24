#ifndef MIKU_LIST_H
#define MIKU_LIST_H

#include "miku_common.h"

typedef struct miku_list_node_s {
    struct miku_list_node_s *prev;
    struct miku_list_node_s *next;
} miku_list_node_t;

static inline void miku_list_init(miku_list_node_t *head) {
    head->prev = head;
    head->next = head;
}

static inline bool miku_list_empty(const miku_list_node_t *head) {
    return head->next == head;
}

static inline void miku_list_insert(miku_list_node_t *after, miku_list_node_t *node) {
    node->prev = after;
    node->next = after->next;
    after->next->prev = node;
    after->next = node;
}

static inline void miku_list_push_front(miku_list_node_t *head, miku_list_node_t *node) {
    miku_list_insert(head, node);
}

static inline void miku_list_push_back(miku_list_node_t *head, miku_list_node_t *node) {
    miku_list_insert(head->prev, node);
}

static inline void miku_list_remove(miku_list_node_t *node) {
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->prev = node;
    node->next = node;
}

#define miku_list_foreach(head, node) \
    for ((node) = (head)->next; (node) != (head); (node) = (node)->next)

#define miku_list_foreach_safe(head, node, tmp) \
    for ((node) = (head)->next, (tmp) = (node)->next; \
         (node) != (head); \
         (node) = (tmp), (tmp) = (node)->next)

#endif
