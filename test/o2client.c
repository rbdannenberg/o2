//  o2client.c - part of performance benchmark
//
//  see o2server.c for details


#include "o2.h"
#include "stdio.h"
#include "string.h"

#ifdef WIN32
#include <windows.h> 
#else
#include <unistd.h>
#endif

#pragma comment(lib,"o2_static.lib")

#define N_ADDRS 20

char *server_addresses[N_ADDRS];
int msg_count = 0;

int client_test(o2_message_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_send(server_addresses[msg_count % N_ADDRS], 0, "i", msg_count);
    if (msg_count % 10000 == 0) {
        printf("client received %d messages\n", msg_count);
    }
    msg_count++;
    return O2_SUCCESS;
}


int main(int argc, const char * argv[]) {
    o2_initialize("test");
    o2_add_service("client");
    
    for (int i = 0; i < N_ADDRS; i++) {
        char path[100];
        sprintf(path, "/client/benchmark/%d", i);
        o2_add_method(path, "i", &client_test, NULL, FALSE, FALSE);
    }
    
    for (int i = 0; i < N_ADDRS; i++) {
        char path[100];
        sprintf(path, "!server/benchmark/%d", i);
        server_addresses[i] = (char *) O2_MALLOC(strlen(path));
        strcpy(server_addresses[i], path);
    }

    while (o2_status("server") < O2_LOCAL) {
        o2_poll();
        usleep(2000); // 2ms
    }
    printf("We discovered the server.\ntime is %g.\n", o2_get_time());
    
    double now = o2_get_time();
    while (o2_get_time() < now + 5) {
        o2_poll();
        usleep(2000);
    }
    
    printf("Here we go! ...\ntime is %g.\n", o2_get_time());
    
    o2_send("!server/benchmark/0", 0, "i", 0);
    
    while (1) {
        o2_poll();
        //usleep(2000); // 2ms // as fast as possible
    }

    o2_finish();
    return 0;
}
