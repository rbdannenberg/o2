// vectorserver.c -- test array/vector messages
//

// What does this test?
// 1. send vector messages of type double, int64 and float

#include <stdio.h>
#include "o2.h"
#include "assert.h"
#include "string.h"

#ifdef WIN32
#define snprintf _snprintf
#endif

int got_the_message = FALSE;


void send_the_message()
{
    for (int i = 0; i < 1000000; i++) {
        if (got_the_message) break;
        o2_poll();
    }
    //assert(got_the_message);
    got_the_message = FALSE;
}



int main(int argc, const char *argv[])
{
    printf("Usage: o2server [debugflags] "
           "(see o2.h for flags, use a for all)\n");
    if (argc == 2) {
        o2_debug_flags(argv[1]);
        printf("debug flags are: %s\n", argv[1]);
    }
    if (argc > 2) {
        printf("WARNING: o2server ignoring extra command line argments\n");
    }

    o2_initialize("test");
    o2_clock_set(NULL, NULL);

    // wait for client service to be discovered
    while (o2_status("vectortest") < O2_REMOTE) {
        o2_poll();
        usleep(2000); // 2ms
    }

    printf("We discovered the client at time %g.\n", o2_time_get());

    // delay 1 second
    double now = o2_time_get();
    while (o2_time_get() < now + 1) {
        o2_poll();
        usleep(2000);
    }

    printf("Here we go! ...\ntime is %g.\n", o2_time_get());
    
    //send blob data
    o2_blob_ptr a_blob;
    a_blob = malloc(20);
    a_blob->size = 15;
    memcpy(a_blob->data, "This", 5);
    o2_send("/one/b", 0, "b", a_blob);
    printf("Sent blob data..");

    //add vector of type int64
    long dvec[102];
    for (int j = 0; j < 102; j++) {
        dvec[j] = 12345 + j;
    }
    for (int i = 0; i < 102; i++) {
        o2_send_start();
        o2_add_vector('h', i, dvec);
        o2_send_finish(0, "/vectortest/service_vh", TRUE);
    }
     printf("DONE sending vh, size 0 through 100\n");

    //add vector of type double
    double dvec2[102];
    for (int j = 0; j < 102; j++) {
        dvec2[j] = 12345.67 + j;
    }
    for (int i = 0; i < 102; i++) {
        o2_send_start();
        o2_add_vector('d', i, dvec2);
        o2_send_finish(0, "/vectortest/service_vd", TRUE);
    }
     printf("DONE sending vd, size 0 through 100\n");

    //add vector of type float
    float dvec3[102];
    for (int j = 0; j < 102; j++) {
        dvec3[j] = 12345.67 + j;
    }
    for (int i = 0; i < 102; i++) {
        o2_send_start();
        o2_add_vector('f', i, dvec3);
        o2_send_finish(0, "/vectortest/service_vf", TRUE);
    }
     printf("DONE sending vf, size 0 through 100\n");
     o2_finish();
    printf("SERVER DONE\n");
    return 0;
}
