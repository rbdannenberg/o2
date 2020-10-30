// atomic.h -- atomic list functions for O2
//
// Roger B. Dannenberg
// Oct, 2020

#include <stdatomic.h>

// o2_obj is a generic chunk of memory. The actual size of the object
// is a multiple of 8 bytes and stored as an int64_t in the 8 bytes
// *before* the address.
//
typedef struct o2_obj {
    union {
        struct o2_obj *next;
        char data[8];
    };
} o2_obj, *o2_obj_ptr;

// to deal with cross-platform issues, 32-bits is plenty for length
// and long (and %ld) are standard, so we'll store 64 bits for
// alignment but return a long, whatever that is...
#define O2_OBJ_SIZE(obj) ((long) (((int64_t *) ((obj)->data))[-1]))


typedef struct o2_queue_na {
    uintptr_t aba;
    o2_obj_ptr first;
} o2_queue_na;

typedef _Atomic(o2_queue_na) o2_queue;
typedef _Atomic o2_queue_na *o2_queue_ptr;


void o2_queue_init(o2_queue_ptr head);

o2_obj_ptr o2_queue_pop(o2_queue_ptr head);

void o2_queue_push(o2_queue_ptr head, o2_obj_ptr elem);

o2_obj_ptr o2_queue_grab(o2_queue_ptr src);
