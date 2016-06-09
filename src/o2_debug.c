/* o2_debug.c -- some debugging code. This is not used, but if
 *    if you need a quick re-implementation of malloc or free, 
 *    you could start here.
 */

#include "o2.h"
#include <stdlib.h>


void *dbg_malloc(size_t size)
{
    void *ptr = malloc(size);
    /* code used for debug */
    if ((long)ptr == 0x0100100d68) {
        printf("malloc %p\n", ptr);
    } /* */
    return ptr;
}


void dbg_free(void *ptr)
{
    /* code used for debug */
    if ((long)ptr == 0x0100100d68) {
        printf("free %p\n", ptr);
    } /* */
    free(ptr);
}
