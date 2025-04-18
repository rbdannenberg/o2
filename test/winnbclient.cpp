//  winnbclient.cpp - nonblocking client test
//
//  This program works with winnbserver.cpp. It is a test of
//  my understanding of how to nonblocking sockets should work.
//


#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include <queue>
#include "testassert.h"

#define MESSAGE "This is a message to be echoed by the server\n"

const char *dbflags = "";
bool failure = false;
bool recv_flag = false;
bool send_flag = true;
char buf[128];  // only one message received at a time

SOCKET client_socket = INVALID_SOCKET;
static struct sockaddr_in server_addr;

FD_SET o2_read_set;
FD_SET o2_write_set;
FD_SET o2_except_set;
struct timeval o2_no_timeout;

#include <windows.h>
#include <timeapi.h>

static long last_time = 0;
static long implied_wakeup = 0;

void o2_sleep(int n)
{
    long now = timeGetTime();
    if (now - implied_wakeup < 50) {
        // assume the intention is a sequence of short delays
        implied_wakeup += n;
    } else { // a long time has elapsed
        implied_wakeup = now + n;
    }
    if (implied_wakeup > now + 1) {
        Sleep(implied_wakeup - now);
    }
}


void print_socket_error(int err, const char *source)
{
    char errbuf[256];
    errbuf[0] = 0;
    FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                  NULL, err, 0, errbuf, sizeof(errbuf), NULL);
    if (!errbuf[0]) {
        sprintf(errbuf, "%d", err);  // error as number if no string 
    }
    fprintf(stderr, "SOCKET_ERROR in %s: %s\n", source, errbuf);
}


static void report_error(const char *msg, SOCKET socket)
{
    int err;
    int errlen = sizeof(err);
    getsockopt(socket, SOL_SOCKET, SO_ERROR, (char *) &err, &errlen);
    printf("Socket %ld error %s: %d\n", (long) socket, msg, err);
}
    

void nbpoll()
{
    int total;
    // connect to server, send messages, check and count replies
    FD_ZERO(&o2_read_set);
    FD_ZERO(&o2_write_set);
    FD_ZERO(&o2_except_set);
    FD_SET(client_socket, &o2_read_set); // never write to this
    if (client_socket != INVALID_SOCKET) {
        if (send_flag) {  // we want to send a message now
            FD_SET(client_socket, &o2_write_set);
        }
        FD_SET(client_socket, &o2_read_set);
        FD_SET(client_socket, &o2_except_set);
    }
    o2_no_timeout.tv_sec = 0;
    o2_no_timeout.tv_usec = 0;
    if ((total = select(0, &o2_read_set, &o2_write_set, &o2_except_set, 
                        &o2_no_timeout)) == SOCKET_ERROR) {
        int err = WSAGetLastError();
        print_socket_error(err, "nbpoll");
        failure = true;
        return;
    }
    if (total == 0) { /* no messages waiting */
        return;
    }
    // check for client_socket
    if (FD_ISSET(client_socket, &o2_except_set)) {
        printf("--exception event on socket %ld\n", (long) client_socket);
        report_error("exception event", client_socket);
        failure = true;
        return;
    }
    if (FD_ISSET(client_socket, &o2_read_set)) {
        printf("--read event on socket %ld\n", (long) client_socket);
        int n = (int) recvfrom(client_socket, buf, 127, 0, NULL, NULL);
        if (n < 0) {
            closesocket(client_socket);
            client_socket = INVALID_SOCKET;
        } else {
            buf[n] = 0;
            o2assert(strlen(buf) == n);  // make sure we can get the length
            recv_flag = true;
        }
    }
    if (FD_ISSET(client_socket, &o2_write_set)) {
        printf("--write event on socket %ld\n", (long) client_socket);
        int flags = 0;
        int err =  ::send(client_socket, buf, (int) strlen(buf), flags);
        if (err < 0) {
            printf("send error %d\n", err);
            closesocket(client_socket);
        }
        send_flag = false;
        buf[0] = 0;  // modify buf so it does not already contain what
        // we expect to receive, a copy of what we just sent
    }
}


int main(int argc, const char * argv[])
{
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    int count = 0;

    printf("Usage: winnbclient [debugflags]"
           "(no flags defined yet)\n");
    if (argc == 2) {
        dbflags = argv[1];
        printf("debug flags are: %s\n", dbflags);
    }
    if (argc > 2) {
        printf("WARNING: winnbserver ignoring extra command line argments\n");
    }

    client_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client_socket < 0) {
        failure = true;
        printf("Could not create server socket\n");
        goto finish;
    }

    u_long nonblocking_enabled = TRUE;
    ioctlsocket(client_socket, FIONBIO, &nonblocking_enabled);
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = 0x0100007F; // inet_addr("192.168.1.157"); // "127.0.0.1");
    // htonl(0xc044019d);  // 192.168.1.157
    server_addr.sin_port = htons(44444);
    o2assert(server_addr.sin_addr.s_addr != INADDR_NONE);

    if (::connect(client_socket, (const sockaddr *) &server_addr, sizeof(struct sockaddr)) == -1) {
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK) {
            printf("Error in connect\n");
            failure = true;
            goto finish;
        }
    }

    strcpy(buf, MESSAGE);
    send_flag = true;
    while (count < 100 && !failure) {
        nbpoll();
        if (recv_flag) {
            o2assert(!send_flag);
            o2assert(strcmp(buf, MESSAGE) == 0);
            recv_flag = false;  // got it
            send_flag = true;   // send it again
            count++;
        }
        o2_sleep(10);
    }
  finish:
    if (failure) {
        printf("quit because of error\n");
    } else {
        printf("CLIENT DONE\n");
    }
    WSACleanup();
    printf("type return to exit: ");
    char in[50];
    gets_s(in);
    return 0;
}
