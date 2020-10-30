// hubserver.c - test hub discovery
//
// use with hubclient.c
//
// start_timer() is set after first discovery, so times will pretty closely
// match throughout execution even when client and server stop and restart
// o2. This was done to debug the complex interaction sequence.
//
// Use ordinary discovery for client to discover this server at time t-0.5.
// at t, client shuts down and delays 1.5s
// t+0.5s, server shuts down and restarts
// t+1.0s, client starts after server. Server sends IP:port to client.
// t+1.5s, client shuts down and server calls o2_hub(NULL) to stop discovery.
// t+2.0s, server checks that client is gone.
// t+2.5s, client reinitializes and uses o2_hub() to connect to server.
//             (hub is server which was first to start)
// Server looks for message from client.
// Server replies to client. Now we want to try making the hub the 2nd
//             process to start. That would be the client, but the client
//             has already used o2_hub() to contact the server, so start over:
// t+3.0s, server shuts down, client shuts down.
// t+3.5s, server restarts
// t+4.0s, client restarts after server. Client sends IP:port to server.
// t+4.5s, server shuts down and client calls o2_hub(NULL) to stop discovery.
// t+5.0s, client checks that server is gone.
// t+5.5s, server reinitializes and uses o2_hub() to connect to client.
//             (hub is client which was second to start)
// Client looks for message from server. Sends reply.
// t+6.0s, both client and server shut down.
// FINISH

#ifdef __GNUC__
// define usleep:
#define _XOPEN_SOURCE 500
#define _POSIX_C_SOURCE 200112L
#endif

#include "o2.h"
#include "stdio.h"
#include "string.h"
#include "assert.h"
#include <sys/time.h>

#ifdef WIN32
#include "usleep.h" // special windows implementation of sleep/usleep
#else
#include <unistd.h>
#endif

long long current_timestamp() {
    struct timeval te; 
    gettimeofday(&te, NULL); // get current time
    long long milliseconds = te.tv_sec*1000LL + te.tv_usec/1000;
    return milliseconds;
}

long long start_time = 0;
void start_timer()
{
    start_time = current_timestamp();
}

long elapsed_time()
{
    return current_timestamp() - start_time;
}


#define streql(a, b) (strcmp(a, b) == 0)

int client_hi_count = 0;

void client_hi(o2_msg_data_ptr data, const char *types,
               o2_arg_ptr *argv, int argc, const void *user_data)
{
    assert(streql(argv[0]->s, "hi"));
    client_hi_count++;
}

char client_ip[O2_MAX_PROCNAME_LEN];
int client_port = -1;

void client_ipport(o2_msg_data_ptr data, const char *types,
               o2_arg_ptr *argv, int argc, const void *user_data)
{
    strcpy(client_ip, argv[0]->s);
    client_port = argv[1]->i32;
    printf("# client_ipport handler got %s %d at %ld\n",
           client_ip, client_port, elapsed_time());
}


void delay_for(double delay)
{
    // we are not the clock master so o2_time_get() may not always work
    long done = elapsed_time() + (long) (delay * 1000);
    while (elapsed_time() < done) {
        o2_poll();
        usleep(2000);
    }
}

void startup(const char *msg)
{
    o2_initialize("test");
    o2_service_new("server");
    o2_method_new("/server/client_hi", "s", &client_hi, NULL, false, true);
    o2_method_new("/server/ipport", "si", &client_ipport,
                  NULL, false, true);
    o2_clock_set(NULL, NULL);  // always be the master
    printf("# Started O2: %s at %ld\n", msg, elapsed_time());
}    


int main(int argc, const char *argv[])
{
    printf("Usage: hubserver [debugflags]\n"
           "    see o2.h for flags, use a for all, - for none\n");
    if (argc >= 2) {
        o2_debug_flags(argv[1]);
        printf("debug flags are: %s\n", argv[1]);
    }
    if (argc > 2) {
        printf("WARNING: hubserver ignoring extra command line argments\n");
    }
    client_ip[0] = 0;
    startup("START 1: first time to sync up");
    // ordinary discovery for client to discover this server at time t-0.5.
    printf("# sync: waiting for client status at %ld\n", elapsed_time());
    int count = 0;
    while (o2_status("client") < O2_REMOTE) {
        o2_poll();
        usleep(2000); // 2ms
        count++;
        if (count % 1000 == 0) {
            printf("    still waiting at %ld\n", elapsed_time());
        }
    }
    start_timer();
    for (int i = 0; i < 10; i++) printf("******\n");
    printf("# We discovered the client.\n");
    delay_for(0.5);
    // at t, client shuts down and delays 1.5s, but we wait a bit to make
    // sure we are discovered.
    delay_for(0.5);
    // t+0.5s, server shuts down and restarts
    o2_finish();
    printf("# shutdown complete at %ld\n", elapsed_time());
    startup("starting before client");
    printf("# waiting for client status at %ld\n", elapsed_time());
    count = 0;
    while (o2_status("client") < O2_REMOTE) {
        o2_poll();
        usleep(2000); // 2ms
        count++;
        if (count % 1000 == 0) {
            printf("    still waiting at %ld\n", elapsed_time());
        }
    }
    printf("# got client status at %ld\n", elapsed_time());
    // t+1.0s, client starts after server. Server sends IP:port to client.
    const char *ipaddress;
    int port;
    o2_get_address(&ipaddress, &port);
    printf("# sending !client/ipport at %ld\n", elapsed_time());
    o2_send_cmd("!client/ipport", 0, "si", ipaddress, port);
    delay_for(0.5);
    // t+1.5s, client shuts down; server calls o2_hub(NULL) to stop discovery.
    printf("# turning off discovery messages at %ld\n", elapsed_time());
    o2_hub(NULL, 0); // turn off discovery messages
    delay_for(0.5);
    // t+2.0s, server checks that client is gone.
    printf("# testing client is shutdown at %ld\n", elapsed_time());
    assert(o2_status("client") == O2_UNKNOWN);
    // t+2.5s, client reinitializes and uses o2_hub() to connect to server.
    //             (hub is server which was first to start)
    // Server looks for message from client.
    printf("# waiting for client status and get hi at %ld\n", elapsed_time());
    count = 0;
    while (o2_status("client") < O2_REMOTE || client_hi_count < 1)  {
        o2_poll();
        usleep(2000); // 2ms
        count++;
        if (count % 1000 == 0) {
            printf("    still waiting at %ld\n", elapsed_time());
        }
    }
    printf("# sending !client/server_hi at %ld\n", elapsed_time());
    o2_send_cmd("!client/server_hi", 0, "s", "hi");
    // Server replies to client. Now we want to try making the hub the 2nd
    //     process to start. That would be the client, but the client
    //     has already used o2_hub() to contact the server, so start over:
    delay_for(0.5);
    // t+3.0s, server shuts down, client shuts down.
    o2_finish();
    printf("# shutdown complete at %ld\n", elapsed_time());
    usleep(500000); //500ms
    // t+3.5s, server restarts
    startup("START 2: starting to reestablish discovery, get clients IP:port");
    // t+4.0s, client restarts after server. Client sends IP:port to server.
    client_port = -1; // this will be signal that we got client IP:port
    printf("# waiting for client status and ipport at %ld\n", elapsed_time());
    count = 0;
    while (o2_status("client") < O2_REMOTE || client_port < 0)  {
        o2_poll();
        usleep(2000); // 2ms
        count++;
        if (count % 1000 == 0) {
            printf("    still waiting at %ld\n", elapsed_time());
        }
    }
    printf("# got client status and ipport at %ld\n", elapsed_time());
    delay_for(0.5);
    // t+4.5s, server shuts down
    o2_finish();
    printf("# shutdown complete at %ld\n", elapsed_time());
    usleep(500000); //500ms
    // t+5.0s, client calls o2_hub(NULL) to stop discovery.
    // t+5.0s, client checks that server is gone.
    usleep(500000); //500ms
    // t+5.5s, server reinitializes and uses o2_hub() to connect to client.
    //     (hub is client which was second to start)
    startup("START 3: restart to become hub to client");
    client_hi_count = 0;
    // client is up and running before we call o2_hub()
    assert(*client_ip);
    assert(client_port >= 0);
    o2_hub(client_ip, client_port);
    printf("# waiting for client status at %ld\n", elapsed_time());
    count = 0;
    while (o2_status("client") < O2_REMOTE)  {
        o2_poll();
        usleep(2000); // 2ms
        count++;
        if (count % 1000 == 0) {
            printf("    still waiting at %ld\n", elapsed_time());
        }
    }
    // Client looks for message from server. Sends reply.
    printf("# sending !client/server_hi at %ld\n", elapsed_time());
    o2_send_cmd("!client/server_hi", 0, "s", "hi");
    printf("# waiting for client status and get hi at %ld\n", elapsed_time());
    while (o2_status("client") < O2_REMOTE || client_hi_count < 1)  {
        o2_poll();
        usleep(2000); // 2ms
    }
    printf("# got client status and hi at %ld\n", elapsed_time());
    delay_for(0.5);
    // t+6.0s, both client and server shut down.
    o2_finish();
    printf("HUBSERVER DONE\n    at %ld\n", elapsed_time());
    return 0;
    // FINISH
}
