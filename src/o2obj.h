// O2obj.h -- generic object memory management
//
// Roger B. Dannenberg
// November 2020

class O2obj {
  public:
    O2obj *next;
    void *operator new(size_t size);
    void operator delete(void *ptr);
    O2obj() { next = NULL; }
};
