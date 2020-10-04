/* dynarray.h -- generic dynamic arrays */
#ifndef dynarray_h
#define dynarray_h

#ifdef __cplusplus
extern "C" {
#endif

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
    (a).array = ((siz) > 0 ? (char *) O2_MALLOCNT((siz), typ) : NULL); }

#define DA_ZERO(a, typ) \
    memset((a).array, 0, sizeof(typ) * (a).allocated)


#define DA_INIT_ZERO(a, typ, siz) { \
    DA_INIT(a, typ, siz); \
    o2_mem_check((a).array); \
    DA_ZERO(a, typ); }


/* get a pointer to the first item of array. The type of
 each element is typ. */
#define DA(a, typ) (o2_mem_check((a).array), (typ *) ((a).array))

/* get an element from a dynamic array */
#define DA_GET(a, typ, index) \
    (assert(DA_CHECK(a, index)), DA(a, typ)[index])

/* get address of element in a dynamic array
 * I would like to write &DA_GET(...), but with bounds checking,
 * DA_GET() does not return an rvalue, i.e. you cannot take the
 * address of (assert(in_bounds), DA(...)[index]), so we have
 * this variant of the macro that takes the address directly:
 */
#define DA_GET_ADDR(a, typ, index) \
    (assert(DA_CHECK(a, index)), &DA(a, typ)[index])

/* get the last element. Assumes length > 0. */
#define DA_LAST(a, typ) DA_GET(a, typ, (a).length - 1)

/* get the address of the last element */
#define DA_LAST_ADDR(a, typ) DA_GET_ADDR(a, typ, (a).length - 1)

/* set an array element at index to data. typ is the type of
 each element in array. */
#define DA_SET(a, typ, index, data) \
        (*DA_GET_ADDR(a, typ, index) = (data))
    
/* return if index of a dynamic array is in bounds */
#define DA_CHECK(a, index) \
        ((index) >= 0 && (index) < (a).length)

/* make sure there is room for at least one more element, and increase
 the length by one. Caller should immediately assign a value to the
 last element */
#define DA_EXPAND(a, typ) \
        ((((a).length + 1 > (a).allocated) ?  \
             o2_da_expand(&(a), sizeof(typ)) : NULL), \
         &DA(a, typ)[(a).length++]) // return new element address

/* append data (of type typ) to the dynamic array */
#define DA_APPEND(a, typ, data) { \
        *(DA_EXPAND(a, typ)) = (data); }

/* remove an element at index i, replacing with last */
#define DA_REMOVE(a, typ, i) { \
        DA_SET(a, typ, i, DA_LAST(a, typ));      \
        (a).length--; }


#define DA_FINISH(a) { (a).length = (a).allocated = 0; \
        if ((a).array) O2_FREE((a).array); (a).array = NULL; }

void o2_da_expand(dyn_array_ptr array, int siz);

#ifdef __cplusplus
}
#endif

#endif /* dynarray_h */
