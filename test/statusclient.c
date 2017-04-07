//  statusclient.c - O2 status/discovery test, client side
//
//  see statusserver.c for details


#include "o2.h"
#include "stdio.h"
#include "string.h"
#include "assert.h"

#ifdef WIN32
#include "usleep.h" // special windows implementation of sleep/usleep
#else
#include <unistd.h>
#endif


int running = TRUE;


void stop_handler(o2_msg_data_ptr data, const char *types,
                  o2_arg_ptr *argv, int argc, void *user_data)
{
    printf("client received stop message. Bye.\n");
    running = FALSE;
}


int main(int argc, const char * argv[])
{
    printf("Usage: statusclient\n");
    if (argc > 1) {
        printf("WARNING: tcpclient ignoring extra command line arguments\n");
    }
    o2_initialize("test");
    o2_service_new("client");
    o2_method_new("/client/stop", "", &stop_handler, NULL, FALSE, TRUE);
    
    while (running) {
        o2_poll();
        usleep(2000); // 2ms
    }
    // exit without calling o2_finish() -- this is a test for behavior when
    // the client crashes. Will the server still remove the service?
    // o2_finish();
    printf("CLIENT DONE\n");
    return 0;
}
