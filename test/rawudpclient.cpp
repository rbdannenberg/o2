/* rawudpclient.c -- performance test for "pure" udp
 *
 * not ported to linux or Windows yet
 */

#include "testassert.h"
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

#include "sys/time.h"
#ifdef __APPLE__
#include "CoreAudio/HostTime.h"
#endif

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#define S_PORT 8000
#define C_PORT 8001

#define max_msgs 1100000

struct pollfd fds[10];
int fds_len = 0;

uint64_t start_time = 0;

void start_clock()
{
#ifdef __APPLE__
    start_time = AudioGetCurrentHostTime();
#elif __linux__
    struct timeval tv;
    gettimeofday(&tv, NULL);
    start_time = tv.tv_sec;
#elif WIN32
    timeBeginPeriod(1); // get 1ms resolution on Windows
    start_time = timeGetTime();
#else
#error o2_clock has no implementation for this system
#endif
}


double the_time()
{
#ifdef __APPLE__
    uint64_t clock_time, nsec_time;
    clock_time = AudioGetCurrentHostTime() - start_time;
    nsec_time = AudioConvertHostTimeToNanos(clock_time);
    return nsec_time * 1.0E-9;
#elif __linux__
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((tv.tv_sec - start_time) + (tv.tv_usec * 0.000001));
#elif WIN32
    return ((timeGetTime() - start_time) * 0.001);
#else
#error o2_clock has no implementation for this system
#endif
}

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

int main(int argc, char **argv)
{
    start_clock();
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == -1) {
        displayError("tcp socket set up error");
    }
    int send_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (send_sock == -1) {
        displayError("udp send socket set up error");
    }
    struct sockaddr_in local_addr, remote_addr;

    memset(&remote_addr, 0, sizeof(struct sockaddr_in));
    remote_addr.sin_family = AF_INET;      //AF_INET means using IPv4
    // remote_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (inet_aton("127.0.0.1", &remote_addr.sin_addr) == 0) {
        displayError("setting IP");
    }
    remote_addr.sin_port = htons(S_PORT);

    memset(&local_addr, 0, sizeof(struct sockaddr_in));
    local_addr.sin_family = AF_INET;      //AF_INET means using IPv4
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = htons(C_PORT);
    /*
    unsigned int yes = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) < 0) {
        displayError("udp setsockopt");
    }
    */
    if (bind(sock, (struct sockaddr *) &local_addr, sizeof local_addr)) {
        displayError("udp bind");
    }

    fds[0].fd = sock;
    fds[0].events = POLLIN;
    fds[0].revents = 0;
    fds_len = 1;

    int32_t count = 0;
    int done = FALSE;
    while (count < max_msgs && !done) {
        int i;

        if (sendto(send_sock, &count, sizeof(int32_t), 0,
                   (struct sockaddr *) &remote_addr,
                   sizeof remote_addr) < 0) {
            displayError("sendto");
            done = TRUE;
        }

        int received = FALSE;
        double start;
        while (!received) {
            poll(fds, fds_len, 0);
            for (i = 0; i < fds_len; i++) {
                if (fds[i].revents & POLLERR) {
                    displayError("poll error");
                } else if (fds[i].revents & POLLHUP) {
                    printf("HUP on %d\n", i);
                    received = TRUE;
                    done = TRUE;
                    break;
                } else if (fds[i].revents) {
                    int32_t msg;
                    int n;
                    n = recvfrom(sock, &msg, sizeof(int32_t), 0, NULL, NULL);
                    if (msg != count) {
                        printf("FAIL!");
                    }
                    received = TRUE;
                    if (n <= 0) done = TRUE;
                    if (count % 10000 == 0) {
                        printf("client received %d messages\n", count);
                        if (count == 50000) {
                            start = the_time();
                        } else if (count == 1050000) {
                            double stop = the_time();
                            printf("TIME: %g\n", stop - start);
                        }
                    }
                }
            }
        }
        count++;
    }
    close(sock);
    return 0;
}
