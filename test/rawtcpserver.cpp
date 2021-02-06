/* rawtcpserver.c -- performance test for "pure" tcp
 */

#ifdef WIN32
#include <winsock2.h> 
#include <windows.h> 
#include <stdint.h>
#include <assert.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <netinet/tcp.h>
typedef int SOCKET;  // In O2, we'll use SOCKET to denote the type of a socket
#define INVALID_SOCKET -1
#define closesocket close
#endif

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <signal.h>

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#define PORT 8000

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
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        displayError("tcp socket set up error");
    }
    struct sockaddr_in server_addr, client;
    // set up the connection
    memset(&server_addr, 0, sizeof(struct sockaddr_in));
    server_addr.sin_family = AF_INET;      //AF_INET means using IPv4
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(PORT);
    printf("*** binding to port %d\n", PORT);
    if (bind(sock, (struct sockaddr *) &server_addr,
             sizeof server_addr) < 0) {
        displayError("bind error");
    }
    if ((listen(sock, 5)) != 0) {
        displayError("listen error");
    }

    fds[0].fd = sock;
    fds[0].events = POLLIN;
    fds[0].revents = 0;
    fds_len = 1;

#ifndef WIN32
    unsigned
#endif
    int client_size = sizeof client;
    SOCKET conn = ::accept(sock, (struct sockaddr *) &client, &client_size);
    if (conn < 0) {
        printf("server acccept failed...\n");
        exit(0);
    }
    else
        printf("server acccept the client...\n");

    int option = 1;
    setsockopt(conn, IPPROTO_TCP, TCP_NODELAY, (const char *) &option,
               sizeof option);

    fds[1].fd = conn;
    fds[1].events = POLLIN;
    fds[1].revents = 0;
    fds_len = 2;

    int32_t count = 0;
    int done = FALSE;
    while (!done) {
#ifdef WIN32
        assert(false); // no windows select() implementation
#else
        int i;
        poll(fds, fds_len, 0);
        for (i = 0; i < fds_len; i++) {
            if (fds[i].revents & POLLERR) {
            } else if (fds[i].revents & POLLHUP) {
                printf("HUP on %d\n", i);
                done = TRUE;
                break;
            } else if (fds[i].revents) {
                int32_t msg;
                int n;
                n = read(conn, &msg, sizeof(int32_t));
                if (n <= 0) break;
                if (write(conn, &count, sizeof(int32_t)) < 0) {
                    displayError("send");
                }
                count++;
                if (count % 10000 == 0) {
                    printf("server received %d messages\n", count);
                }
            }
        }
#endif
    }
    closesocket(conn);
    closesocket(sock);
    return 0;
}
