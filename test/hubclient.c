//  hubclient.c - test o2_hub()
//
//  see hubserver.c for details


#include "o2usleep.h"
#include "o2.h"
#include "stdio.h"
#include "string.h"
#include "assert.h"
#include <sys/time.h>

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

int server_hi_count = 0;

void server_hi(o2_msg_data_ptr data, const char *types,
               o2_arg_ptr *argv, int argc, const void *user_data)
{
    assert(streql(argv[0]->s, "hi"));
    server_hi_count++;
}

char server_ip[O2_MAX_PROCNAME_LEN];
int server_port = -1;

void server_ipport(o2_msg_data_ptr data, const char *types,
               o2_arg_ptr *argv, int argc, const void *user_data)
{
    strcpy(server_ip, argv[0]->s);
    server_port = argv[1]->i32;
    printf("# server_ipport handler got %s %d at %ld\n",
           server_ip, server_port, elapsed_time());
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
    o2_service_new("client");
    o2_method_new("/client/server_hi", "s", &server_hi, NULL, false, true);
    o2_method_new("/client/ipport", "si", &server_ipport,
                  NULL, false, true);
    printf("# Started O2: %s at %ld\n", msg, elapsed_time());
}


int main(int argc, const char *argv[])
{
    printf("Usage: hubclient [debugflags]\n"
           "    see o2.h for flags, use a for all, - for none\n");
    if (argc >= 2) {
        o2_debug_flags(argv[1]);
        printf("debug flags are: %s\n", argv[1]);
    }
    if (argc > 2) {
        printf("WARNING: hubclient ignoring extra command line argments\n");
    }
    server_ip[0] = 0;
    startup("START 1: synchronize start");
    // Use ordinary discovery for client to discover this server at time t-0.5.
    printf("# waiting to find server\n");
    while (o2_status("server") < O2_REMOTE) {
        o2_poll();
        usleep(2000); // 2ms
    }
    start_timer();
    for (int i = 0; i < 10; i++) printf("******\n");
    printf("# We discovered the server at %ld\n", elapsed_time());
    delay_for(0.5);
    // at t, client shuts down and delays 1.0s
    printf("# Shutting down at %ld\n", elapsed_time());
    o2_finish();
    printf("# shut down complete at %ld\n", elapsed_time());
    usleep(1000000); //1000ms
    // t+0.5s, server shuts down and restarts
    // t+1.0s, client starts after server. Server sends IP:port to client.
    startup("START 2: client starts after server");
    server_port = -1;
    printf("# waiting to find server and ipport at %ld\n", elapsed_time());
    int count = 0;
    while (o2_status("server") < O2_REMOTE || server_port < 0) {
        o2_poll();
        usleep(2000); // 2ms
        count++;
        if (count % 1000 == 0) {
            printf("    server status %d server_port %d at %ld\n",
                   o2_status("server"), server_port, elapsed_time());
        }
    }
    printf("# got server, ipport: %s %d at %ld\n",
           server_ip, server_port, elapsed_time());
    delay_for(0.5);
    // t+1.5s, client shuts down; server calls o2_hub(NULL) to stop discovery.
    o2_finish();
    printf("# shut down complete at %ld\n", elapsed_time());
    usleep(500000); //500ms
    // t+2.0s, server checks that client is gone.
    usleep(500000); //500ms
    // t+2.5s, client reinitializes and uses o2_hub() to connect to server.
    //     (hub is server which was first to start)
    startup("START 3: client reinitializes to use o2_hub()");
    server_hi_count = 0;
    assert(*server_ip);
    assert(server_port >= 0);
    o2_hub(server_ip, server_port);
    count = 0;
    while (o2_status("server") < O2_REMOTE) {
        o2_poll();
        usleep(2000); // 2ms
        count++;
        if (count % 1000 == 0) {
            printf("    waiting for server service at %ld\n",
                    elapsed_time());
        }
    }
    // Server looks for message from client.
    printf("# sending !server/client_hi at %ld", elapsed_time());
    o2_send_cmd("!server/client_hi", 0, "s", "hi");
    // Server replies to client.
    printf("# waiting to find server and get hi at %ld\n", elapsed_time());
    count = 0;
    while (o2_status("server") < O2_REMOTE || server_hi_count < 1) {
        o2_poll();
        usleep(2000); // 2ms
        count++;
        if (count % 1000 == 0) {
            printf("    waiting for server service at %ld\n",
                    elapsed_time());
        }
    }
    printf("# got server, hi at %ld\n", elapsed_time());
    // Now we want to try making the hub the 2nd
    //     process to start. That would be the client, but the client
    //     has already used o2_hub() to contact the server, so start over:
    delay_for(0.5);
    // t+3.0s, server shuts down, client shuts down.
    o2_finish();
    printf("# shut down complete at %ld\n", elapsed_time());
    usleep(500000); //500ms
    // t+3.5s, server restarts
    usleep(500000); //500ms
    // t+4.0s, client restarts after server. Client sends IP:port to server.
    startup("restarting after server");
    printf("# waiting to find server at %ld\n", elapsed_time());
    count = 0;
    while (o2_status("server") < O2_REMOTE) {
        o2_poll();
        usleep(2000); // 2ms
        count++;
        if (count % 1000 == 0) {
            printf("    waiting for server service at %ld\n",
                    elapsed_time());
        }
    }
    printf("# got server at %ld\n", elapsed_time());
    const char *ipaddress;
    int port;
    o2_get_address(&ipaddress, &port);
    printf("# sending !server/ipport at %ld\n", elapsed_time());
    o2_send_cmd("!server/ipport", 0, "si", ipaddress, port);
    delay_for(0.5);
    // t+4.5s, server shuts down;
    delay_for(0.5);
    // t+5.0s client calls o2_hub(NULL) to stop discovery.
    o2_hub(NULL, 0);
    // t+5.0s, client checks that server is gone.
    assert(o2_status("server") == O2_FAIL);
    server_hi_count = 0;
    delay_for(0.5);
    // t+5.5s, server reinitializes and uses o2_hub() to connect to client.
    //             (hub is client which was second to start)
    // Client looks for message from server. Sends reply.
    printf("# waiting to find server and get hi at %ld\n", elapsed_time());
    count = 0;
    while (o2_status("server") < O2_REMOTE || server_hi_count < 1) {
        o2_poll();
        usleep(2000); // 2ms
        count++;
        if (count % 1000 == 0) {
            printf("    waiting for server service at %ld\n",
                    elapsed_time());
        }
    }
    printf("# sending !server/ hi at %ld\n", elapsed_time());
    o2_send_cmd("!server/client_hi", 0, "s", "hi");
    // t+6.0s, both client and server shut down.
    delay_for(0.5);
    o2_finish();
    printf("HUBCLIENT DONE\n    at %ld\n", elapsed_time());
    return 0;
    // FINISH
}
