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
#include "assert.h"
#include "o2atomic.h"

#ifdef WIN32
/********************** WINDOWS ATOMIC LISTS *****************/

// nothing to implement here - see o2atomic.h

#else
/********************** LINUX AND MACOS ATOMIC LISTS ******************/

O2list_elem *O2queue::pop()
{
    O2queue_na next_head;
    O2queue_na orig_head = atomic_load(&queue_head);
    assert(((uintptr_t) &queue_head & 0xF) == 0);
    do {
        if (orig_head.first == NULL)
            return NULL;  // empty stack
        next_head.aba = orig_head.aba + 1;
        next_head.first = orig_head.first->next;
    } while (!atomic_compare_exchange_weak(&queue_head, &orig_head, next_head));
    return orig_head.first;
}

O2list_elem *O2queue::pop()
{
    O2queue_na next_head;
    O2queue_na orig_head = atomic_load(&queue_head);
    assert(((uintptr_t) &queue_head & 0xF) == 0);
    do {
        if (orig_head.first == NULL)
            return NULL;  // empty stack
        next_head.aba = orig_head.aba + 1;
        next_head.first = orig_head.first->next;
    } while (!atomic_compare_exchange_weak(&queue_head, &orig_head, next_head));
    return orig_head.first;
}


void O2queue::push(O2list_elem *elem)
{
    O2queue_na next_head;
    O2queue_na orig_head = atomic_load(&queue_head);
    assert(((uintptr_t) elem & 0x7) == 0);  // 8-byte aligned
    do {
        elem->next = orig_head.first;
        next_head.aba = orig_head.aba + 1;
        next_head.first = elem;
    } while (!atomic_compare_exchange_weak(&queue_head, &orig_head, next_head));
}


// remove list from src atomically and return it
//
O2list_elem *O2queue::grab()
{
    O2queue_na orig_src = atomic_load(&queue_head);
    O2queue_na next_src;
    do {
        if (orig_src.first == NULL) {
            return NULL;
        }
        next_src.aba = orig_src.aba + 1;
        next_src.first = NULL;
    } while (!atomic_compare_exchange_weak(&queue_head, &orig_src, next_src));
    return orig_src.first;
}

#endif
