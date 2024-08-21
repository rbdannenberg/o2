// atomic.h -- atomic list functions for O2
//
// Roger B. Dannenberg
// Oct, 2020

#include <assert.h>
#include <stdio.h>

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
// case, it might make sense to subclass O2list_elem to form O2message.
//
typedef struct O2list_elem {
    union {
        struct O2list_elem *next;
        char data[8];
    };
} O2list_elem;


#ifdef WIN32
/********************** WINDOWS ATOMIC LISTS *****************/

#define WIN32_MEAN_AND_LEAN
#include <windows.h>
#include <interlockedapi.h>

// An atomic queue must be 16-byte aligned, which is guaranteed by
// O2MALLOC (new).
class O2queue {
  public:
    SLIST_HEADER queue_head;

    O2queue() { clear(); }
    
    /* THIS IS NOT ATOMIC - MUST BE PROTECTED BY LOCK - DEBUGGING ONLY */
    /* There's no operator to get the first element, and the structure
       is different for different architectures, so instead we pop the
       whole list and if non-empty, we reinsert it. This requires finding
       the end and counting the elements.
     */
    O2list_elem *first() {
        O2list_elem *result = grab();
        if (result) {
            int count = 1;
            O2list_elem *end = result;
            while (end->next) {
                end = end->next;
                count++;
            }
            InterlockedPushListSListEx(&queue_head, (PSLIST_ENTRY) result,
                                       (PSLIST_ENTRY) end, count);
        }
        return result;
    }

    void clear() {
        InitializeSListHead(&queue_head);
    }

    O2list_elem *pop() {
        return (O2list_elem *) InterlockedPopEntrySList(&queue_head);
    }

    void push(O2list_elem *elem) {
        InterlockedPushEntrySList(&queue_head, (PSLIST_ENTRY) elem);
    }

    O2list_elem *grab() {
        return (O2list_elem *) InterlockedFlushSList(&queue_head);
    }

    void free() {  // empties queue and frees all messages found
        O2list_elem *all = grab();
        while (all) {
            O2list_elem* msg = all;
            all = all->next;
            O2_FREE(msg);
        }
    }
};

#else
/********************** LINUX AND MACOS ATOMIC LISTS ******************/
#ifndef __cplusplus
#  include <stdatomic.h>
#else
#  include <atomic>
#  define _Atomic(X) std::atomic< X >
#endif

typedef struct O2queue_na {
    uintptr_t aba;
    O2list_elem *first;
} O2queue_na;

typedef _Atomic(O2queue_na) O2queue_atomic;
typedef _Atomic(O2queue_na) *O2queue_atomic_ptr;

#define O2_QUEUE_INIT {0, NULL}

#if defined(__clang__)
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Wunused-private-field"
#endif

// An atomic queue must be 16-byte aligned, which is guaranteed by
// O2MALLOC (new).
class O2queue {
  public:
    O2queue_atomic queue_head;

    O2queue() { clear(); }
    
    void clear() {
        O2queue_na init = O2_QUEUE_INIT;
        atomic_init(&queue_head, init);
    }

    O2list_elem *first() {
        return (O2list_elem *) (((O2queue_na) queue_head).first);
    }

    O2list_elem *pop();

    void push(O2list_elem *elem);

    O2list_elem *grab();
    
    void free();  // empties queue and frees all messages found
};

#if defined(__clang__)
  #pragma clang diagnostic pop
#endif
#endif
