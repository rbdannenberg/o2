//  statusclient.c - O2 status/discovery test, client side
//
//  see statusserver.c for details


#ifdef __GNUC__
// define usleep:
#define _XOPEN_SOURCE 500
#define _POSIX_C_SOURCE 200112L
#endif

#include "o2.h"
#include "stdio.h"
#include "string.h"
#include "assert.h"


#ifdef WIN32
#include "usleep.h" // special windows implementation of sleep/usleep
#else
#include <unistd.h>
#endif


bool running = true;


void stop_handler(o2_msg_data_ptr data, const char *types,
                  o2_arg_ptr *argv, int argc, const void *user_data)
{
    printf("client received stop message. Bye.\n");
    running = false;
}


int main(int argc, const char * argv[])
{
    const char *ip = NULL;
    int port = 0;
    printf("Usage: statusclient [debugflags] [ip port] "
           "(see o2.h for flags, use a for all)\n"
           "    last args, if set, specify a hub to use; if only ip is given,\n"
           "    o2_hub(NULL, 0) is called to turn off broadcasting\n"
           "    If port is 0, you will be prompted (allowing you to\n"
           "    start first)\n");
    if (argc >= 2) {
        o2_debug_flags(argv[1]);
        printf("debug flags are: %s\n", argv[1]);
    }
    if (o2_initialize("test")) {
        printf("FAIL\n");
        return -1;
    }
    if (argc == 3) {
        port = 1;
    } else if (argc == 4) {
        ip = argv[2];
        port = atoi(argv[3]);
        while (port == 0) {
            char input[100];
            printf("Port specified as 0, enter new value: ");
            port = atoi((const char *) fgets(input, 100, stdin));
        }
        printf("Using %s:%d as hub.\n", ip, port);
    } else if (argc > 4) {
        printf("WARNING: statusclient ignoring extra command line argments\n");
    }
    o2_service_new("client");
    o2_method_new("/client/stop", "", &stop_handler, NULL, false, true);

    if (port > 0)
        o2_hub(ip, port);

    const char *address;
    int tcp_port;
    o2_get_address(&address, &tcp_port);
    printf("My address is %s:%d\n", address, tcp_port);
    
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
