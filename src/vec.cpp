// vec.cpp -- O2 vector implementation
//
// Roger B. Dannenberg
// November 2020

#include "o2obj.h"
#include "vec.h"

template <typename T>
void Vec<T>::expand_array()
{
    if (allocated > 0) allocated *= 2;
    else allocated = 1;
    T bigger = new T[allocated];
    memcpy(bigger, array, length * sizeof(T));
    if (array) delete array;
    array = bigger;
}

