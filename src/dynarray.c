/* dynarray.c -- generic dynamic arrays */
#include "assert.h"
#include "o2.h"
#include "string.h"
#include "dynarray.h"

// returns void * so that it can be used functionally, as in
//    ((need_room ? o2_da_expand : NULL), get_address)
void *o2_da_expand(dyn_array_ptr array, int siz)
{
    // printf("o2_da_expand called with %d\n", siz);
    if (array->allocated > 0) array->allocated *= 2;
    else array->allocated = 1;
    char *bigger = O2_MALLOCNT(array->allocated * siz, char);
    memcpy(bigger, array->array, array->length * siz);
    if (array->array) O2_FREE(array->array);
    array->array = bigger;
    return NULL;
}
