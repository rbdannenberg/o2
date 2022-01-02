// atomic.h -- atomic list functions for O2
//
// Roger B. Dannenberg
// Oct, 2020

#ifndef __cplusplus
#  include <stdatomic.h>
#else
#  include <atomic>
#  define _Atomic(X) std::atomic< X >
#endif

#include <assert.h>
#include <stdio.h>
#include "o2base.h"

// O2list_elem is a generic linked list element. Although we could use
// templates to make a generic atomic list, the "C++" way would 
// require either separate list elements that point to objects, or all
// list elements would have to subclass List_elem and add the overhead
// of a "next" pointer.
//
// Instead, we just cast memory blocks to O2list_elem and overwrite the
// first 8 bytes with the "next" pointer. This works for freed objects
// because, well, they are free memory, and because they have a length
// count stored in the previous 8 bytes before the object (the standard
// trick used by malloc()), so we don't lose size information when we
// cast to a different type.
// 
// Some objects, notably O2message objects, use atomic lists for 
// shared memory, inter-thread communication. These objects have a
// "next" pointer allocated as their first member variable so that
// using them in an atomic list does not overwrite anything. In this
// case, it might make sense to subclass O2list_elem to form O2message,
// but that would require introducing C++ into the O2 API, which is
// simpler and more portable if we keep it in C.
//
typedef struct O2list_elem {
    union {
        struct O2list_elem *next;
        char data[8];
    };
} O2list_elem, *O2list_elem_ptr;


typedef struct O2queue_na {
    uintptr_t aba;
    O2list_elem_ptr first;
} O2queue_na;

typedef _Atomic(O2queue_na) o2_queue;
typedef _Atomic(O2queue_na) *o2_queue_ptr;

#define O2_QUEUE_INIT {0, NULL}

#if defined(__clang__)
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Wunused-private-field"
#endif

// An atomic queue must be 16-byte aligned, but malloc and O2MALLOC (new)
// only align to 8 bytes, so the atomic queue here has some padding and 
// we treat it as being whereever the 16-byte boundary occurs. We use a
// method to find it.
class O2queue {
  private:
    int64_t padding;
    o2_queue space_for_queue_head;
  public:
    o2_queue *queue() { 
        return ((o2_queue *) (((intptr_t) &space_for_queue_head) & ~0xF)); }

    O2queue() { clear(); }
    
    void clear() {
        O2queue_na init = O2_QUEUE_INIT;
        o2_sleep(1); // pause for a ms
        atomic_init(queue(), init);
    }

    O2list_elem *pop();

    void push(O2list_elem *elem);

    O2list_elem *grab();
};

#if defined(__clang__)
  #pragma clang diagnostic pop
#endif
