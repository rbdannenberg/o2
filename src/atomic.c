// atomic.c -- cross-platform lock-free atomic operations
//
// based on https://nullprogram.com/blog/2014/09/02
//
// Roger B. Dannenberg
// Oct 2020

#include "stdlib.h"
#include "atomic.h"

#define O2_QUEUE_HEAD_INIT {0, NULL}


/*
struct lstack_node {
    void *value;
    struct lstack_node *next;
};


typedef struct {
    struct lstack_node *node_buffer;
    _Atomic struct lstack_head head, free;
    _Atomic size_t size;
} lstack_t;

int
lstack_init(lstack_t *lstack, size_t max_size)
{
    struct lstack_head head_init = {0, NULL};
    lstack->head = ATOMIC_VAR_INIT(head_init);
    lstack->size = ATOMIC_VAR_INIT(0);

    // Pre-allocate all nodes:
    lstack->node_buffer = malloc(max_size * sizeof(struct lstack_node));
    if (lstack->node_buffer == NULL)
        return ENOMEM;
    for (size_t i = 0; i < max_size - 1; i++)
        lstack->node_buffer[i].next = lstack->node_buffer + i + 1;
    lstack->node_buffer[max_size - 1].next = NULL;
    struct lstack_head free_init = {0, lstack->node_buffer};
    lstack->free = ATOMIC_VAR_INIT(free_init);
    return 0;
}


static inline void
stack_free(lstack_t *lstack)
{
    free(lstack->node_buffer);
}

static inline size_t
lstack_size(lstack_t *lstack)
{
    return atomic_load(&lstack->size);
}
*/

void o2_queue_init(o2_queue_head_ptr head)
{
    o2_queue_head_na init = O2_QUEUE_HEAD_INIT;
    atomic_init(head, init);
}


o2_obj_ptr o2_queue_pop(o2_queue_head_ptr head)
{
    o2_queue_head_na next_head;
    o2_queue_head_na orig_head = atomic_load(head);
    do {
        if (orig_head.first == NULL)
            return NULL;  // empty stack
        next_head.aba = orig_head.aba + 1;
        next_head.first = orig_head.first->next;
    } while (!atomic_compare_exchange_weak(head, &orig_head, next_head));
    return orig_head.first;
}


void o2_queue_push(o2_queue_head_ptr head, o2_obj_ptr elem)
{
    o2_queue_head_na next_head;
    o2_queue_head_na orig_head = atomic_load(head);
    do {
        elem->next = orig_head.first;
        next_head.aba = orig_head.aba + 1;
        next_head.first = elem;
    } while (!atomic_compare_exchange_weak(head, &orig_head, next_head));
}


/*
int
lstack_push(lstack_t *lstack, void *value)
{
    struct lstack_node *node = pop(&lstack->free);
    if (node == NULL)
        return ENOMEM;
    node->value = value;
    push(&lstack->head, node);
    atomic_fetch_add(&lstack->size, 1);
    return 0;
}

void *
lstack_pop(lstack_t *lstack)
{
    struct lstack_node *node = pop(&lstack->head);
    if (node == NULL)
        return NULL;
    atomic_fetch_sub(&lstack->size, 1);
    void *value = node->value;
    push(&lstack->free, node);
    return value;
}
*/

// remove list from src atomically and return it
//
o2_obj_ptr o2_queue_grab(o2_queue_head_ptr src)
{
    o2_queue_head_na orig_src = atomic_load(src);
    o2_queue_head_na next_src;
    do {
        if (orig_src.first == NULL) {
            return NULL;
        }
        next_src.aba = orig_src.aba + 1;
        next_src.first = NULL;
    } while (!atomic_compare_exchange_weak(src, &orig_src, next_src));
    return orig_src.first;
}
