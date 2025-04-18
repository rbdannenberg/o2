#include "testassert.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lo/lo.h"

lo_address client;
char **addresses;
int n_addrs = 20;
int msg_count = 0;
int use_tcp = 0;

int handler(const char *path, const char *types,
            lo_arg **argv, int argc, lo_message msg, void *user_data)
{
    // keep count and send a reply
    msg_count++;
    lo_send(client, addresses[msg_count % n_addrs], "i", msg_count);
    if (msg_count % 10000 == 0) {
        printf("server received %d messages\n", msg_count);
    }
    return 1;
}


int main(int argc, const char *argv[])
{
    printf("Usage: lo_benchmk_server [n_addrs]\n"
           "  n_addrs is number of paths, default is 20\n"
           "  end n_addrs with t for TCP, e.g. 20t\n");
    if (argc == 2) {
        n_addrs = atoi(argv[1]);
        printf("n_addrs is %d\n", n_addrs);
        use_tcp = (strchr(argv[1], 't') != NULL);
    }

    // create address for client
    client = lo_address_new_with_proto(use_tcp ? LO_TCP : LO_UDP,
                                       "localhost", "8001");

    // create server
    lo_server server = lo_server_new_with_proto("8000",
                                use_tcp ? LO_TCP : LO_UDP, NULL);

    // make addresses and register them with server
    addresses = (char **) malloc(sizeof(char **) * n_addrs);
    for (int i = 0; i < n_addrs; i++) {
        char path[100];
        sprintf(path, "/benchmark/%d", i);
        addresses[i] = (char *) malloc(strlen(path) + 1);
        strcpy(addresses[i], path);
        lo_server_add_method(server, path, "i", &handler, NULL);
    }

    // serve port
    while (1) {
        lo_server_recv_noblock(server, 0);
    }
}
