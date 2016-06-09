/* udp-broadcast-client.c
 * udp datagram client
 * Get datagram stock market quotes from UDP broadcast:
 * see below the step by step explanation
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

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

int main(int argc,char **argv)
{
    FILE *inf = fopen("port.dat", "r");
    if (!inf) displayError("Could not find port.dat");
    char ip[100];
    int tcp_port;
    if (fscanf(inf, "%s %d", ip, &tcp_port) != 2) {
        displayError("could not scanf");
    }
//    strcpy(ip, "localhost");
//    printf("ip %s tcp %d\n", ip, tcp_port);
    fclose(inf);
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        displayError("tcp socket set up error");
    }
    struct sockaddr_in remote_addr;
    // set up the connection
    memset(&remote_addr, 0, sizeof(struct sockaddr_in));
    remote_addr.sin_family = AF_INET;      //AF_INET means using IPv4
//    inet_pton(AF_INET, ip, &(remote_addr.sin_addr));
    struct hostent *hostp = gethostbyname(ip);
    if (!hostp) displayError("gethostbyname failed");
    memcpy(&remote_addr.sin_addr, hostp->h_addr, sizeof(remote_addr.sin_addr));
//
    remote_addr.sin_port = htons(tcp_port);
    printf("*** connecting to %s:%d\n", ip, tcp_port);
    if (connect(sock, (struct sockaddr *) &remote_addr, sizeof(remote_addr)) < 0) {
        displayError("Connect Error!");
    }
    while (TRUE) {
        char *msg = "This is a test\n";
        if (write(sock, msg, strlen(msg) + 1) < 0) {
            displayError("send");
        }
        sleep(4);
    }
    return 0;
}
