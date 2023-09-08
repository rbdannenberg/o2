// vec.h -- O2 vector implementation
//
// Roger B. Dannenberg
// November 2020

template <typename T> class Vec : public O2obj {
  protected:
    int allocated;
    int length;
    T *array;
  public:
    Vec(int siz) { init(siz); }

    // initialization with optional fill with zeros
    // if not z(erofill), the initial length is zero.
    // if z(erofill), vector initial length will be
    // at least size and filled with zero. It may be
    // larger than size if the memory allocator returns
    // more space than requested.
    Vec(int siz, bool z) { init(siz, z); }

    // move vector to new one, initialize src vector to empty
    Vec(Vec &v) { allocated = v.allocated;
            length = v.length; array = v.array;
            v.array = NULL; v.allocated = 0; v.length = 0; }

    // default constructor does not allocate any memory
    Vec() { init(0); }

    ~Vec() { finish(); }

    // explicitly initialize the vector and array storage
    // with option to zero fill all the entries. Call this ONLY if
    // initialization was not already performed by a constructor OR
    // if array is NULL, e.g. when size was initialized to zero or
    // after a call to Vec::finish().
    void init(int siz, bool z = false) {
        array = (siz > 0 ? O2_MALLOCNT(siz, T) : NULL);
        allocated = siz;
        length = 0;
        if (z) {
            zero();
            length = allocated;
        }
    }

    // explicitly free the associated storage. No destructors called
    // on elements.
    void finish() {
        length = 0;
        allocated = 0;
        // note that delete would call system delete, so call O2_FREE.
        if (array) O2_FREE(array);
        array = NULL;
    }

    // clear all allocated space
    void zero() { memset((void *) array, 0, sizeof(T) * allocated); }

    T  &operator[](int index) { return array[index]; }

    // return the last element
    T &last() { return array[length - 1]; }

    // remove all elements. No destructors are called.
    void clear() { length = 0; }

    // remove an element quickly: fill the "hole" with the last element,
    //   which reorders the array but leaves no holes.
    void remove(int index) {
        length--;
        if (length > index) {
            array[index] = array[length];
        }
    }

    // push one element to the end of the array.
    void push_back(T data) { *append_space(1) = data; }

    // append a C array of count elements to the end of the array.
    void append(const T *data, int count) {
        memcpy(append_space(count), data, count * sizeof(T));
    }
    
    // make room for an additional count of elements, return address of first
    //   the new elements are uninitialized
    T *append_space(int count) {
        if (length + count > allocated) {
            expand_array(length + count);
        }
        length += count;
        return &array[length - count];
    }

    // remove elements from index first to index last, [first, last)
    //    No destructors are callled. Element order is maintained.
    void erase(int first, int last) {
        if (first >= 0 && first <= last && last <= length) {
            int n = last - first;
            memmove(array + first, array + last, (length - last) * sizeof(T));
            length -= n;
        }
    }

    // remove one element. Element order is maintained.
    void erase(int i) { erase(i, i + 1); }

    // remove n elements from beginning if size is at least n.
    //    No destructors are callled.
    void drop_front(int n) { erase(0, n); }
    
    void insert(int i, T data) {
        if (i >= 0 && i <= length) {
            append_space(1);
            memmove(array + i + 1, array + i, (length - (i + 1)) * sizeof(T));
            array[i] = data;
        }
    }

    // copy all size() elements to C array:
    void retrieve(T *data) { memcpy(data, array, length * sizeof(T)); }

    // remove the last element
    T pop_back() { return array[length-- - 1]; }

    // return true if index i is in-bounds
    bool bounds_check(int i) { return i >= 0 && i < length; }

    // return the number of elements in use
    int size() { return length; }
    
    // set the size - CAUTION! content is uninitialized if z is false
    // storage is reallocated if needed
    void set_size(int n, bool z = true) {
        if (allocated < n) expand_array(n);
        length = n;
        if (z) zero();
    }
    
    // return the maximum count of elements that could be stored
    //   without reallocating the array.
    int get_allocated() { return allocated; }
    
private:
    void expand_array(int new_size);
};

    
template <typename T>
void Vec<T>::expand_array(int newsize)
{
    if (allocated > 0) allocated *= 2;
    else allocated = 1;
    if (allocated < newsize) allocated = newsize;
    T *bigger = O2_MALLOCNT(allocated, T);
    memcpy(bigger, array, length * sizeof(T));
    // Maybe we got more memory than requested. Make use of it:
    allocated = o2_allocation_size(bigger, allocated * sizeof(T)) /
                sizeof(T);
    if (array) O2_FREE(array);
    array = bigger;
}

