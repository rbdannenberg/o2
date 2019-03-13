#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lo/lo.h"

lo_address client;
char **addresses;
int n_addrs = 20;
int msg_count = 0;


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
           "  n_addrs is number of paths, default is 20\n");
    if (argc == 2) {
        n_addrs = atoi(argv[1]);
        printf("n_addrs is %d\n", n_addrs);
    }

    // create address for client
    client = lo_address_new("localhost", "8001");

    // create server
    lo_server server = lo_server_new("8000", NULL);

    // make addresses and register them with server
    addresses = (char **) malloc(sizeof(char **) * n_addrs);
    for (int i = 0; i < n_addrs; i++) {
        char path[100];
        sprintf(path, "/benchmark/%d", i);
        addresses[i] = (char *) malloc(strlen(path));
        strcpy(addresses[i], path);
        lo_server_add_method(server, path, "i", &handler, NULL);
    }

    // serve port
    while (1) {
        lo_server_recv_noblock(server, 0);
    }
}
