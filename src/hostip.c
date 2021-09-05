// hostip.c -- get the host local ip address and related functions
//
// Roger B. Dannenberg
// Aug 2021

// This module is implemented in hostipimpl.h. The implementation appears
// in an include file because o2lite does not have the same memory
// allocation functions. In o2lite, we define some macros to override
// calls to O2 memory allocation, using malloc instead, and we include
// hostipimpl.h directly into o2lite.c to avoid having to compile
// hostip.c in two different configurations.
//
#include "hostipimpl.h"
