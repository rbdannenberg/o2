//  statusclient.c - O2 status/discovery test, client side
//
//  see statusserver.c for details


#include "o2.h"
#include "stdio.h"
#include "string.h"
#include "assert.h"

/* added for special async_test */
#include "o2_internal.h"
#include "o2_send.h"
void o2_context_init(o2_context_ptr context);
extern o2_context_t main_context;

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


int async_test_handler(o2n_info_ptr info)
{
    o2_message_ptr msg = info->in_message;
    printf("Client got message: %s\n", msg->data.address);
    // send the message back to the sender
    assert(info);
    o2_send_by_tcp(info, FALSE, msg);
    return O2_SUCCESS;
}


void async_test(const char *ip, const char *port_string)
{
    o2_context_init(&main_context);
    o2_ensemble_name = "async"; // pretend initialized
    o2_debug_flags("A");
    int port = atoi(port_string);
    printf("Calling o2n_initialize()\n");
    o2n_initialize();
    o2n_send_by_tcp = &async_test_handler;
    printf("My address: %s\n", o2_context->info->proc.name);
    printf("Connecting to %s:%d\n", ip, port);
    int rslt = o2n_connect(ip, port, INFO_TCP_NOCLOCK);
    printf("Connection result: %d (%d is SUCCESS)\n", rslt, O2_SUCCESS);
    assert(rslt == O2_SUCCESS);
    o2n_info_ptr info = *DA_LAST(o2_context->fds_info, o2n_info_ptr);
    int fds_len = o2_context->fds_info.length;
    for (int i = 0; i < 5000; i++) {
        o2n_recv();
        if (info->net_tag != NET_TCP_CONNECTING) {
            goto connected;
        }
        if (i % 100 == 0) { // print every 200ms
            printf("Waiting for connection to complete, net_tag %d\n",
                   info->net_tag);
        }
        usleep(2000); // 2ms
    }
    printf("Connection did not complete, timed out, calling o2n_finish().\n");
    // ~10s have elapsed
    o2n_finish();
    return;
  connected:
    for (int i = 0; i < 5000; i++) {
        o2n_recv();
        if (i % 100 == 0) { // print every 200ms
            if (o2_context->fds_info.length < fds_len) {
                printf("Socket seems to have closed\n");
                break;
            }
            printf("Waiting for message, net_tag %d\n",
                   info->net_tag);
        }
        usleep(2000); // 2ms
    }
    printf("Calling o2n_finish()\n");
    o2n_finish();
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
           "    start first)\n"
           "If debugflags is xxx, this code runs a special test of\n"
           "    low-level asynchronous TCP connections. This is for\n"
           "    debugging and not intended for real regression testing\n"
           "    In this case, ip port should be given for the instance\n"
           "    of statusserver. Start \"statusserver xxx\" and copy the\n"
           "    ip and port number to the command line: \n"
           "        \"statusclient xxx ipaddress portno\"\n");
    if (argc >= 2) {
        if (streql(argv[1], "xxx") && argc >= 4) {
            printf("Special asynchronous tcp connection code test...\n");
            async_test(argv[2], argv[3]);
            return 0;
        } else {
            o2_debug_flags(argv[1]);
            printf("debug flags are: %s\n", argv[1]);
        }
    }
    o2_initialize("test");
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
    o2_method_new("/client/stop", "", &stop_handler, NULL, FALSE, TRUE);

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
