// statusserver.c - O2 status/discovery test, client side
//
// This program works with statusclient.c. It checks for 
// discovery of statusclient's service, sends it a message
// to exit, then checks that the status of the service
// reverts to "does not exist".

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

o2_message_ptr make_message()
{
    o2_message_ptr m = o2_malloc(MESSAGE_DEFAULT_SIZE);
    m->next = NULL;
    m->tcp_flag = TRUE;
    m->allocated = MESSAGE_ALLOCATED_FROM_SIZE(MESSAGE_DEFAULT_SIZE);
    m->length = 40; // timestamp 8, address 28, typestring 4
    m->data.timestamp = 0.0;
    strncpy(m->data.address, "/This_is_from_make_message.\000,\000\000\000",
            40 - 8);
    return m;
}


int async_test_handler(o2n_info_ptr info)
{
    o2_message_ptr msg = info->in_message;
    printf("Client got message: %s\n", msg->data.address);
    o2_message_free(msg);
    // close the connection
    assert(info);
    o2_socket_remove(info);
    return O2_SUCCESS;
}


void async_test()
{
    o2_context_init(&main_context);
    o2_ensemble_name = "async"; // pretend initialized
    o2_debug_flags("A");
    printf("Calling o2n_initialize()\n");
    o2n_initialize();
    o2n_send_by_tcp = &async_test_handler;
    printf("My address: %s\n", o2_context->info->proc.name);
    int connection_index = o2_context->fds_info.length;
    for (int i = 0; i < 15000; i++) { // 30s
        o2n_recv();
        if (o2_context->fds_info.length > connection_index) {
            goto accepted;
        }
        if (i % 1000 == 0) { // print every 2s
            printf("Waiting for connection\n");
        }
        usleep(2000); // 2ms
    }
    // up to ~30s have elapsed
    printf("Connection never came, timed out, calling o2n_finish().\n");
    o2n_finish();
    return;
  accepted:
    printf("Accepted a connection. Sending a message.\n");
    o2_message_ptr msg = make_message();
    o2n_info_ptr connection = GET_PROCESS(connection_index);
    o2_send_by_tcp(connection, FALSE, msg);
    for (int i = 0; i < 15000; i++) { // 30s
        o2n_recv();
        if (o2_context->fds_info.length == connection_index) {
            printf("Connection was closed.\n");
            goto sent_message;
        }
        if (i % 1000 == 0) { // print every 2s
            printf("Waiting for send and close\n");
        }
        usleep(2000); // 2ms
    }
    // up to ~30s have elapsed
    printf("Connection WAS NOT CLOSED: timeout\n");
  sent_message:
    printf("Calling o2n_finish()\n");
    o2n_finish();
}



int main(int argc, const char * argv[])
{
    const char *ip = NULL;
    int port = 0;
    printf("Usage: statusserver [debugflags] [ip port] "
           "(see o2.h for flags, use a for all)\n"
           "    last args, if set, specify a hub to use; if only ip is given,\n"
           "    o2_hub(NULL, 0) is called to turn off broadcasting\n"
           "If debugflags is xxx, this code runs a special test of\n"
           "    low-level asynchronous TCP connections. This is for\n"
           "    debugging and not intended for real regression testing\n");
    if (argc >= 2) {
        if (streql(argv[1], "xxx")) {
            printf("Special asynchronous tcp connection code test...\n");
            async_test();
            return 0;
        } else {
            o2_debug_flags(argv[1]);
            printf("debug flags are: %s\n", argv[1]);
        }
    }
    if (argc == 3) {
        port = 1;
    } else if (argc == 4) {
        ip = argv[2];
        port = atoi(argv[3]);
        printf("Using %s:%d as hub.\n", ip, port);
    } else if (argc > 4) {
        printf("WARNING: statusserver ignoring extra command line argments\n");
    }
    o2_initialize("test");
    
    // we are the master clock
    o2_clock_set(NULL, NULL);
    
    if (port > 0)
        o2_hub(ip, port);

    const char *address;
    int tcp_port;
    o2_get_address(&address, &tcp_port);
    printf("My address is %s:%d\n", address, tcp_port);

    // wait for client service to be discovered
    while (o2_status("client") < O2_LOCAL) {
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
    
    double start_time = o2_time_get();
    printf("Here we go! ...\ntime is %g.\n", start_time);
    o2_send_cmd("!client/stop", 0.0, "");
    // allow 3s for client to shut down and detect it
    while (running && o2_time_get() < start_time + 3 &&
           o2_status("client") >= 0) {
        o2_poll();
    }
    if (o2_status("client") < 0) {
        printf("SERVER DONE\n");
    } else {
        printf("FAIL: client service status is %d\n", o2_status("client"));
	o2_info_show((o2n_info_ptr) &o2_context->path_tree, 2);
    }
    o2_finish();
    sleep(1); // clean up sockets
    return 0;
}
