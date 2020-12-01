// O2obj.cpp -- generic object memory management
//
// Roger B. Dannenberg
// November 2020

#include "o2obj.h"

void *operator O2obj::new(size_t size)
{
    return O2_MALLOC(size);
}


void operator O2obj::delete(void *ptr)
{
    O2_FREE(ptr);
}
