/* o2_dynamic.c -- generic dynamic arrays */
#include "o2_dynamic.h"
#include "assert.h"
#include "o2.h"
#include "string.h"

void o2_da_expand(dyn_array_ptr array, int siz)
{
    if (array->allocated > 0) array->allocated *= 2;
    else array->allocated = 1;
    void *bigger = O2_MALLOC(array->allocated * siz);
    assert(bigger);
    memcpy(bigger, array->array, array->length * siz);
    if (array-> array) O2_FREE(array->array);
    array->array = bigger;
}
