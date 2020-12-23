// O2obj.h -- generic object memory management
//
// Roger B. Dannenberg
// November 2020

#include "o2base.h"
#include "o2mem.h"

class O2obj {
  public:
    void *operator new(size_t size) { return O2_MALLOC(size); }
    void operator delete(void *ptr) { O2_FREE(ptr); }
};

