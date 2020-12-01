// vec.h -- O2 vector implementation
//
// Roger B. Dannenberg
// November 2020

template <typename T> class Vec : public O2obj {
    int32_t allocated;
    int32_t length;
    T *array;
  public:
    Vec(int32_t siz) {
        array = (size > 0 ? O2_MALLOCNT(siz, T) : NULL);
        allocated = siz;
        length = 0;
    }

    Vec(int32_t siz, bool z) {
        array = (size > 0 ? O2_MALLOCNT(siz, T) : NULL);
        allocated = siz;
        length = 0;
        if (z) zero();
    }

    ~Vec() {
        length = 0;
        allocated = 0;
        if (array) delete array;
        array = NULL;
    }
        
    void zero() { memset(array, 0, sizeof(T) * allocated); }

    T  &operator[](int index) { return array[index]; }

    bool check(int32_t index) { return index > 0 && index < length;  }

    T &last() { return array[length - 1]; }

    void expand_array();

    T &expand() {
        if (length + 1 > allocated) {
            expand_array();
        }
        return array[length++];
    }

    void push_back(T data) { expand() = data; }

    T pop_back() { return array[length-- - 1]; }

    
