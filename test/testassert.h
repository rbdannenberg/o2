// testassert.h - custom always-enabled assert so tests can run in release mode
//
// Roger B. Dannenberg
// April 2025

#include "stdio.h"  // for stderr, fprintf, fflush

// __assert is not defined, so make a substitute
void custom_test_assert(const char *msg, const char *file, int line)
{
    fprintf(stderr, "o2assert: %s is false in %s:%d\n", msg, file, line);
    fflush(stderr);
}

#define o2assert(EX) (void)((EX) || \
                            (custom_test_assert(#EX, __FILE__, __LINE__), 0))
