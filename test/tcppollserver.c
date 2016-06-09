/* udp-broadcast-server.c:
 * udp broadcast server example 
 * Example Stock Index Broadcast:
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/poll.h>
#include <ifaddrs.h>
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

static struct sockaddr_in o2_serv_addr;
char o2_local_ip[24];

struct pollfd pfd[2];
int pfd_len = 0;

/*
 * This function reports the error and
 * exits back to the shell:
 */
static void
displayError(const char *on_what) {
    fputs(strerror(errno),stderr);
    fputs(": ",stderr);
    fputs(on_what,stderr);
    fputc('\n',stderr);
    exit(1);
}

int main(int argc, char **argv) {
    int server_sock;      /* Socket */

    /*
     * Create a TCP server socket:
     */
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock == -1)
		displayError("socket()");
    
    /*
     * bind it
     */
    memset((char *) &o2_serv_addr, 0, sizeof(o2_serv_addr));
    o2_serv_addr.sin_family = AF_INET;
    o2_serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); // local IP address
    o2_serv_addr.sin_port = 0;
    if (bind(server_sock, (struct sockaddr *) &o2_serv_addr, sizeof(o2_serv_addr))) {
        displayError("Bind receive socket");
    }
    // find the port that was (possibly) allocated
    socklen_t addr_len = sizeof(o2_serv_addr);
    if (getsockname(server_sock, (struct sockaddr *) &o2_serv_addr,
                    &addr_len)) {
        displayError("getsockname call to get port number");
    }
    int server_port = ntohs(o2_serv_addr.sin_port);  // set actual port used
    printf("server_port %d\n", server_port);
    printf("server ip? %x\n", o2_serv_addr.sin_addr.s_addr);

    o2_local_ip[0] = 0;
    struct ifaddrs *ifap, *ifa;
    struct sockaddr_in *sa;
    
    if (getifaddrs (&ifap)) {
        displayError("getting IP address");
    }
    for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr->sa_family==AF_INET) {
            sa = (struct sockaddr_in *) ifa->ifa_addr;
            if (!inet_ntop(AF_INET, &sa->sin_addr, o2_local_ip,
                           sizeof(o2_local_ip))) {
                displayError("converting local ip to string");
            }
            if (strcmp(o2_local_ip, "127.0.0.1")) {
                printf("%s %d\n", o2_local_ip, server_port);
                break;
            }
        }
    }
    freeifaddrs(ifap);
    if (!o2_local_ip[0]) {
        printf("NO IP!\n");
        return -1;
    }
    FILE *outf = fopen("port.dat", "w");
    fprintf(outf, "%s %d\n", o2_local_ip, server_port);
    fclose(outf);

    /* listen */
    if (listen(server_sock, 10) < 0) {
        displayError("listen failed");
    }

    /* poll */

    pfd[0].fd = server_sock;
    pfd[0].events = POLLIN;
    pfd_len = 1;
    while (TRUE) {
        poll(pfd, pfd_len, 0);
        for (int i = 0; i < pfd_len; i++) {
            if ((pfd[i].revents & POLLERR) || (pfd[i].revents & POLLHUP)) {
                printf("pfd error\n");
            } else if (pfd[i].revents) {
                printf("poll got %d on %d\n", pfd[i].revents, i);
                if (i == 0) { // connection request
                    int connection = accept(pfd[i].fd, NULL, NULL);
                    if (connection <= 0) {
                        displayError("accept failed");
                    }
                    pfd[pfd_len].events = POLLIN;
                    pfd[pfd_len++].fd = connection;
                } else {
                    char buffer[1001];
                    int len;
                    if ((len = recvfrom(pfd[i].fd, buffer, 1000, 0, NULL, NULL)) <= 0) {
                        displayError("recvfrom tcp failed");
                    }
                    buffer[len] = 0;
                    printf("GOT %d: %s\n", len, buffer);
                }
            }
        }
        sleep(1);
    }
    return 0;
}
