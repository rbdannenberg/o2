/* rawudpserver.c -- performance test for "pure" udp
 */

#undef NDEBUG
#ifdef WIN32
#include <winsock2.h> 
#include <windows.h> 
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <poll.h>
#include <netinet/tcp.h>

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#define S_PORT 8000
#define C_PORT 8001

/*
 * This function reports the error and
 * exits back to the shell:
 */
static void
displayError(const char *on_what) {
    fputs(strerror(errno), stderr);
    fputs(": ", stderr);
    fputs(on_what, stderr);
    fputc('\n', stderr);
    exit(1);
}

struct pollfd fds[10];
int fds_len = 0;

int main(int argc, char **argv)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == -1) {
        displayError("udp socket set up error");
    }
    int send_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (send_sock == -1) {
        displayError("udp send socket set up error");
    }
    struct sockaddr_in local_addr, remote_addr;
    // set up the connection
    memset(&local_addr, 0, sizeof(struct sockaddr_in));
    memset(&remote_addr, 0, sizeof(struct sockaddr_in));
    remote_addr.sin_family = AF_INET;      //AF_INET means using IPv4
    // remote_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (inet_aton("127.0.0.1", &remote_addr.sin_addr) == 0) {
        displayError("setting IP");
    }
    remote_addr.sin_port = htons(C_PORT);
    local_addr.sin_family = AF_INET;      //AF_INET means using IPv4
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = htons(S_PORT);
    if (bind(sock, (struct sockaddr *) &local_addr, sizeof local_addr)) {
        displayError("udp bind");
    }
    fds[0].fd = sock;
    fds[0].events = POLLIN;
    fds[0].revents = 0;
    fds_len = 1;

    int32_t count = 0;
    int done = FALSE;
    while (!done) {
        int i;
        poll(fds, fds_len, 0);
        for (i = 0; i < fds_len; i++) {
            if (fds[i].revents & POLLERR) {
            } else if (fds[i].revents) {
                int32_t msg;
                int n;
                n = recvfrom(sock, &msg, sizeof(int32_t), 0, NULL, NULL);
                if (n <= 0) break;
                if (sendto(send_sock, &msg, sizeof(int32_t), 0,
                           (struct sockaddr *) &remote_addr,
                           sizeof remote_addr) < 0) {
                    displayError("sendto");
                }
                count++;
                if (count % 10000 == 0) {
                    printf("server received %d messages\n", count);
                }
            }
        }
    }
    close(sock);
    return 0;
}
