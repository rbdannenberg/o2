// atomic.c -- cross-platform lock-free atomic operations
//
// based on https://nullprogram.com/blog/2014/09/02
//
// Roger B. Dannenberg
// Oct 2020

#include "stdlib.h"
#ifdef __GNUC__
#endif
#include "stdint.h"
#include "o2atomic.h"
#include "assert.h"


void o2_queue_init(O2queue_atomic_ptr head)
{
    O2queue_na init = O2_QUEUE_INIT;
    atomic_init(head, init);
}


O2list_elem *O2queue::pop()
{
    O2queue_na next_head;
    O2queue_na orig_head = atomic_load(queue());
    do {
        if (orig_head.first == NULL)
            return NULL;  // empty stack
        next_head.aba = orig_head.aba + 1;
        next_head.first = orig_head.first->next;
    } while (!atomic_compare_exchange_weak(queue(), &orig_head, next_head));
    return orig_head.first;
}


void O2queue::push(O2list_elem_ptr elem)
{
    O2queue_na next_head;
    O2queue_na orig_head = atomic_load(queue());
    assert(((uintptr_t) elem & 0x7) == 0);  // 8-byte aligned
    do {
        elem->next = orig_head.first;
        next_head.aba = orig_head.aba + 1;
        next_head.first = elem;
    } while (!atomic_compare_exchange_weak(queue(), &orig_head, next_head));
}


// remove list from src atomically and return it
//
O2list_elem *O2queue::grab()
{
    O2queue_na orig_src = atomic_load(queue());
    O2queue_na next_src;
    do {
        if (orig_src.first == NULL) {
            return NULL;
        }
        next_src.aba = orig_src.aba + 1;
        next_src.first = NULL;
    } while (!atomic_compare_exchange_weak(queue(), &orig_src, next_src));
    return orig_src.first;
}
