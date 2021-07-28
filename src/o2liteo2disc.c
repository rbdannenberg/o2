// o2liteavahi.c -- discovery using Avahi for o2lite
//
// Roger B. Dannenberg
// July 2021

#include "o2lite.h"

#ifndef O2_NO_O2DISCOVERY
// !O2_NO_O2DISCOVERY -> use O2 Discovery (this is not standard)

#ifndef O2_NO_ZEROCONF
// O2 ensembles should adopt one of two discovery methods.
#error O2lite supporte either ZeroConf or built-in discovery, but not both.
#error One of O2_NO_ZEROCONF or O2_NO_O2DISCOVERY must be defined
#endif  // end of O2DISCOVERY

#ifndef O2L_NO_BROADCAST
// o2discovery + broadcast requires a broadcast socket:
SOCKET broadcast_sock = INVALID_SOCKET;
struct sockaddr_in broadcast_to_addr;
#endif

void o2ldisc_init(const char *ensemble)
{
#define inet_pton InetPton

    o2l_ensemble = ensemble;
    time_for_discovery_send = o2l_local_time();
    o2l_method_new("!_o2/dy", "sissiii", true, &o2l_dy_handler, NULL);

#ifndef O2L_NO_BROADCAST
    // Initialize addr for broadcasting
    broadcast_to_addr.sin_family = AF_INET;
    inet_pton(AF_INET, "255.255.255.255",
              &broadcast_to_addr.sin_addr.s_addr);

    // create UDP broadcast socket
    if ((broadcast_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("allocating udp broadcast socket");
        return O2L_FAIL;
    }
    // Set the socket's option to broadcast
    int optval = true; // type is correct: int, not bool
    if (setsockopt(broadcast_sock, SOL_SOCKET, SO_BROADCAST,
                   (const char *) &optval, sizeof optval) == -1) {
        perror("Set socket to broadcast");
        return O2L_FAIL;
    }
#endif

    // original O2 discovery opens first available from multiple ports:
    for (int i = 0; i < PORT_MAX; i++) {
        udp_recv_port = o2_port_map[i];
        if (o2l_bind_recv_socket(udp_recv_sock, &udp_recv_port) != 0) {
            O2LDB printf("o2lite: could not allocate a udp recv port\n");
            return O2L_FAIL;
        }
    }

    return O2L_SUCCESS;
}


void o2ldisc_poll()
{
    
}


void o2ldisc_events(fd_set *read_set_ptr)
{
}

#endif // o2discovery (!defined(O2_NO_O2DISCOVERY))


