// memtest.c -- some simple tests for O2_MALLOC
//
// Roger B. Dannenberg
// June 2023

// What does this test?
// 1. allocate and free 1000 objects of sizes 2 to 1,000,000 in 10% size
//    increments
// 2. allocate sizes 2 to 1,000,000 in 10% size increments and THEN free
//    them
// 3. allocate and free every size from 500 to 3000, which should
//    cross the barriers from linear sizes to a couple of exponential
//    sizes lists.
// 4. allocate 1000 random sizes and free them (100 times)

#include <stdlib.h>
#include <stdio.h>
#include "o2internal.h"
#include "assert.h"

#define FULLO2 1

int main(int argc, const char * argv[])
{
#if FULLO2
    o2_initialize("test");
#else
    static O2_context main_context;
    o2_ctx = &main_context;
    o2_mem_init(NULL, 0);
#endif

    /* Step 1 */
    printf("testing sizes from 2 to 1M bytes...\n");
    for (long bits = 16; bits < 8000000; bits = (long) (bits * 1.1)) {
        int size = bits / 8;
        char *obj = O2_MALLOCNT(size, char);
        for (int i = 0; i < size; i++) obj[i] = 0;
        O2_FREE(obj);
    }

    /* Step 2 */
    printf("allocating all from 2 to 1M bytes, then freeing...\n");
    char *head = NULL;
    char **tail_ptr = &head;
    for (long bits = 16; bits < 8000000; bits = (long) (bits * 1.1)) {
        int size = bits / 8;
        char *obj = O2_MALLOCNT(size, char);
        for (int i = 0; i < size; i++) obj[i] = 0;
        *tail_ptr = obj;
        tail_ptr = (char **) obj;
        *tail_ptr = NULL;
    }
    // now free the list of objects
    while (head) {
        char *next = head;
        head = *((char **) next);
        O2_FREE(next);
    }

    /* Step 3. */
    printf("allocating/freeing 10 of each from 500 to 3000 bytes...\n");
    char *objs[1000];
    for (int i = 500; i < 3000; i++) {
        for (int j = 0; j < 10; j++) {
            objs[j] = O2_MALLOCNT(i, char);
        }
        for (int j = 0; j < 10; j++) {
            O2_FREE(objs[j]);
        }
    }

    /* Step 4. */
    printf("100 cycles of: 1000 random allocations then free all...\n");
    for (int i = 0; i < 100; i++) {
        for (int j = 0; j < 1000; j++) {
            // pick in 2 ranges (small and large)
            #if WIN32
            long r = rand();
            #else
            long r = random();
            #endif
            if (r & 1) { // small
                #if WIN32
                r = rand() & 0x3FF;  // 0 to 1023
                #else 
                r = random() & 0x3FF;  // 0 to 1023
                #endif 
            } else {
                #if WIN32
                r = rand() & 0xFFFFF; // 0 to ~1M
                #else 
                r = random() & 0xFFFFF; // 0 to ~1M
                #endif 
            }
            objs[j] = O2_MALLOCNT(r, char);
        }
        for (int j = 0; j < 1000; j++) {
            O2_FREE(objs[j]);
        }
    }

    printf("DONE\n");
#if FULLO2
    o2_finish();
#else
    o2_mem_finish();  // free memory heap before removing o2_ctx
    o2_ctx = NULL;
#endif
    return 0;
}
