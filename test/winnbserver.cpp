//  winnbserver.cpp - nonblocking server test
//
//  This program works with winnbclient.cpp. It is a test of
//  my understanding of how to nonblocking sockets should work.
//

#include <stdio.h>
#include <queue>
#include "testassert.h"
#include <windows.h>

const char *dbflags = "";
bool done = false;;
bool failure = false;

SOCKET client_socket = INVALID_SOCKET;
SOCKET server_socket = INVALID_SOCKET;
static struct sockaddr_in server_addr;

FD_SET o2_read_set;
FD_SET o2_write_set;
FD_SET o2_except_set;
struct timeval o2_no_timeout;

std::queue<char *> pending_strings;

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
    }
    else { // a long time has elapsed
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
    getsockopt(socket, SOL_SOCKET, SO_ERROR, (char *)&err, &errlen);
    printf("Socket %ld error %s: %d\n", (long)socket, msg, err);
}


void nbpoll()
{
    int total;
    // accept connection, echo incoming
    // handles at most one client
    FD_ZERO(&o2_read_set);
    FD_ZERO(&o2_write_set);
    FD_ZERO(&o2_except_set);
    FD_SET(server_socket, &o2_read_set); // never write to this
    FD_SET(server_socket, &o2_except_set);
    if (client_socket != INVALID_SOCKET) {
        if (!pending_strings.empty()) {
            FD_SET(client_socket, &o2_write_set);
        }
        FD_SET(client_socket, &o2_read_set);
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
    // check for server_socket
    if (FD_ISSET(server_socket, &o2_read_set)) {
        printf("--read event on socket %ld\n", (long)server_socket);
        SOCKET connection = accept(server_socket, NULL, NULL);
        if (connection == INVALID_SOCKET) {
            printf("tcp_accept_handler failed to accept\n");
            failure = true;
        } else if (client_socket != INVALID_SOCKET) {
            printf("Error: got unexpected new client %ld\n", (long)connection);
            failure = true;
        } else {
            client_socket = connection;
            u_long nonblocking_enabled = TRUE;
            ioctlsocket(client_socket, FIONBIO, &nonblocking_enabled);
        }
    }
    if (FD_ISSET(server_socket, &o2_except_set)) {
        printf("--exception event on socket %ld\n", (long)server_socket);
    }
    // check for client_socket
    if (FD_ISSET(client_socket, &o2_read_set)) {
        char *buf = (char *)malloc(128);
        printf("--read event on socket %ld\n", (long)client_socket);
        int n = (int)recvfrom(client_socket, buf, 127, 0, NULL, NULL);
        if (n < 0) {
            closesocket(client_socket);
            client_socket = INVALID_SOCKET;
            free(buf);
        } else {
            buf[n] = 0;
            o2assert(strlen(buf) == n);  // make sure we can get the length
            pending_strings.push(buf);
        }
    }
    if (FD_ISSET(client_socket, &o2_write_set)) {
        printf("--write event on socket %ld\n", (long)client_socket);
        const char *buf = pending_strings.front();
        pending_strings.pop();
        int flags = 0;
        int err = ::send(client_socket, buf, (int)strlen(buf), flags);
        if (err < 0) {
            printf("send error %d\n", err);
            closesocket(client_socket);
        }
    }
}


int main(int argc, const char * argv[])
{
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    printf("Usage: winnbserver [debugflags]"
        "(no flags defined yet)\n");
    if (argc == 2) {
        dbflags = argv[1];
        printf("debug flags are: %s\n", dbflags);
    }
    if (argc > 2) {
        printf("WARNING: winnbserver ignoring extra command line argments\n");
    }

    server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket < 0) {
        failure = true;
        printf("Could not create server socket\n");
        goto finish;
    }

    u_long nonblocking_enabled = TRUE;
    ioctlsocket(server_socket, FIONBIO, &nonblocking_enabled);
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(44444);
    unsigned int yes = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR,
        (const char *)&yes, sizeof yes) < 0) {
        printf("Error in setsockopt SO_REUSEADDR\n");
        failure = true;
        goto finish;
    }
    if (bind(server_socket, (struct sockaddr *) &server_addr,
        sizeof server_addr)) {
        printf("Error in bind\n");
        failure = true;
        goto finish;
    }
    if (listen(server_socket, 25)) {
        printf("Error in listen\n");
        failure = true;
        goto finish;
    }
    while (!done && !failure) {
        nbpoll();
        o2_sleep(10);
    }
finish:
    if (failure) {
        printf("quit because of error\n");
    }
    else {
        printf("SERVER DONE\n");
    }
    printf("type return to exit: ");
    char in[50];
    gets_s(in);
    return 0;
}
