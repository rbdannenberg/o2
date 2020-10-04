// stuniptest.c -- get public IP address using stun server
//
// Roger B. Dannenberg
// August 2020

#include "stdio.h"
#include "o2.h"
#include "stun.h"

#ifdef WIN32
#include "usleep.h" // special windows implementation of sleep/usleep
#else
#include <unistd.h>
#endif

int main()
{
    o2_initialize("test");
    o2_get_public_ip();
    for (int i = 0; i < 1000; i++) { // run for 2 seconds
        o2_poll();
        usleep(2000); // 2ms
    }
    if (o2_public_ip[0]) {
        printf("Public IP: %s\nDONE\n", o2_public_ip);
    } else {
        printf("FAILED\n");
    }
}
