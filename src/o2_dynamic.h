/* o2_dynamic.h -- generic dynamic arrays */
#ifndef o2_dynamic_h
#define o2_dynamic_h

typedef struct dyn_array {
    int32_t allocated;
    int32_t length;
    char *array;
} dyn_array, *dyn_array_ptr;

/* initialize a dynamic array. typ is the type of each element,
 siz is the initial space allocated (number of elements). The
 initial length is 0 */
#define DA_INIT(a, typ, siz) { \
        (a).allocated = (siz); \
        (a).length = 0;        \
        (a).array = ((siz) > 0 ? O2_MALLOC((siz) * sizeof(typ)) : NULL); }

/* get a pointer to the index'th item of array. The type of
 each element is typ. */
#define DA_GET(a, typ, index) \
        ((typ *) ((a).array + sizeof(typ) * (index)))

/* get a pointer to the last element. Assumes length > 0. */
#define DA_LAST(a, typ) (DA_GET(a, typ, (a).length - 1))

/* set an array element at index to data. typ is the type of
 each element in array. */
#define DA_SET(a, typ, index, data) \
        (*((typ *) ((a).array + sizeof(typ) * (index))) = (data))

/* return if index of a dynamic array is in bounds */
#define DA_CHECK(a, index) \
        ((index) >= 0 && (index) < (a).length)

/* make sure there is room for at least one more element, and increase
 the length by one. Caller should immediately assign a value to the
 last element */
#define DA_EXPAND(a, typ) { \
        if ((a).length + 1 > (a).allocated) {  \
            o2_da_expand(&(a), sizeof(typ)); } \
        (a).length++; }

/* append data (of type typ) to the dynamic array */
#define DA_APPEND(a, typ, data) { \
        DA_EXPAND(a, typ);        \
        DA_SET(a, typ, (a).length - 1, data); }

#define DA_FINISH(a) O2_FREE((a).array)

void o2_da_expand(dyn_array_ptr array, int siz);

#endif /* o2_dynamic_h */
