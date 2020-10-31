// o2usleep.h -- cross-platform definition of usleep()
//
// Roger B. Dannenberg
// Oct 2020
//
// This file should be included first because if someone includes <unistd.h>
// before this file sets _POSIX_C_SOURCE, then usleep() will not be defined.
// link with o2usleep.c so that usleep() will be implemented for Windows

#ifdef WIN32
void usleep(long usec);
#else
#  if defined(__LINUX__) && defined(__GNUC__)
#    define _POSIX_C_SOURCE 200112L
#  endif
#  include <unistd.h>
#endif
