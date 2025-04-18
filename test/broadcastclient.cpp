/* udp-broadcast-client.c
 * udp datagram client
 * Get datagram stock market quotes from UDP broadcast:
 * see below the step by step explanation
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

int
main(int argc,char **argv) {
    int z;

	#ifdef WIN32
		int x;
	#else
		socklen_t x;
	#endif

    struct sockaddr_in adr;  /* AF_INET */
    int len_inet;            /* length */
    int s;                   /* Socket */
    char dgram[512];         /* Recv buffer */
    
    
    /*
     * Form the broadcast address:
     */
    len_inet = sizeof adr;
    
    adr.sin_family = AF_INET;
    if (inet_pton(AF_INET, "127.0.0.1", &adr.sin_addr.s_addr) != 1) {
        displayError("inet_pton");
    }
    adr.sin_port = htons(8124);
    
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = 0;
    hints.ai_flags = AI_PASSIVE;
/* WORKS
    struct addrinfo *res;
    if (getaddrinfo(NULL, "8124", &hints, &res) < 0) {
        displayError("getaddrinfo");
    }
*/
    /* TEST */
    adr.sin_family = AF_INET;
    adr.sin_addr.s_addr = htonl(INADDR_ANY);
    adr.sin_port = htons(8124);
    /*
     * Create a UDP socket to use:
     */
    s = socket(AF_INET,SOCK_DGRAM,0);
    if (s == -1)
        displayError("socket()");

    
    /*
     * Bind our socket to the broadcast address:
     */
    z = bind(s, /*TEST*/ (struct sockaddr *) &adr, sizeof(adr));
             /* WORKS
             res->ai_addr,
             res->ai_addrlen);
              */
    
    if (z == -1)
        displayError("bind(2)");
    
    for (;;) {
        /*
         * Wait for a broadcast message:
         */
        x = sizeof(adr);
        z = recvfrom(s,      /* Socket */
                     dgram,  /* Receiving buffer */
                     sizeof dgram,/* Max rcv buf size */
                     0,      /* Flags: no options */
                     (struct sockaddr *)&adr, /* Addr */
                     &x);    /* Addr len, in & out */
        
        if (z < 0)
            displayError("recvfrom(2)"); /* else err */
        
        fwrite(dgram, z, 1, stdout);
        putchar('\n');
        
        fflush(stdout);
    }
    
    return 0;
}
