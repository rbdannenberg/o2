/* udp-broadcast-server.c:
 * udp broadcast server example 
 * Example Stock Index Broadcast:
 */

#undef NDEBUG
#ifdef WIN32
#include <winsock2.h> 
#include <windows.h> 
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#endif

#include <stdio.h>
#include <windows.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#define MAXQ 4

static struct {
    char *index;
    int start;
    int volit;
    int current;
} quotes[] = {
    { "DJIA", 1030330, 375 },
    { "NASDAQ", 276175, 125 },
    { "S&P 500", 128331, 50 },
    { "TSE 300", 689572, 75 },
};

/*
 * Initialize:
 */
static void initialize(void) {
    short x;
    time_t td;
	
    /*
     * Seed the random number generator:
     */
    time(&td);
    srand((int)td);
	
    for ( x=0; x < MAXQ; ++x )
        quotes[x].current =
            quotes[x].start;
}

/*
 * Randomly change one index quotation:
 */
static void gen_quote(void) {
    short x;    /* Index */
    short v;    /* Volatility of index */
    short h;    /* Half of v */
    short r;    /* Random change */
	
    x = rand() % MAXQ;
    v = quotes[x].volit;
    h = (v / 2) - 2;
    r = rand() % v;
    if ( r < h )
		r = -r;
    quotes[x].current += r;
}

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
    short x;    /* index of Stock Indexes */
    double I0;  /* Initial index value */
    double I;   /* Index value */
    char bcbuf[512], *bp;/* Buffer and ptr */
    int z;      /* Status return code */
    int broadcast_sock;      /* Socket */
    int local_send_sock;      /* Socket */
    struct sockaddr_in broadcast_to_addr; /* AF_INET */
    struct sockaddr_in local_to_addr;  /* AF_INET */
    static int so_broadcast = TRUE;
    /*
     * Form the dest address:
     */
    memset(&broadcast_to_addr, 0, sizeof(broadcast_to_addr));

#ifndef WIN32
	broadcast_to_addr.sin_len = sizeof(broadcast_to_addr);
#endif

    broadcast_to_addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, "255.255.255.255",
                  &(broadcast_to_addr.sin_addr.s_addr)) != 1) {
        displayError("inet_pton");
    }
    broadcast_to_addr.sin_port = htons(8124);
    
    /*
     * Create a UDP socket to use:
     */
    broadcast_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (broadcast_sock == -1)
		displayError("socket()");
    
    /*
     * Allow broadcasts:
     */
    z = setsockopt(broadcast_sock,
                   SOL_SOCKET,
                   SO_BROADCAST,
                   (const char *) &so_broadcast,
                   sizeof so_broadcast);
    
    if (z == -1)
		displayError("setsockopt(SO_BROADCAST)");
    
    /*
     * Form the local dest address:
     */

#ifndef WIN32
	local_to_addr.sin_len = sizeof(broadcast_to_addr);
#endif

    local_to_addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, "127.0.0.1",
                  &(local_to_addr.sin_addr.s_addr)) != 1) {
        displayError("inet_pton");
    }
    local_to_addr.sin_port = htons(8123);
    
    /*
     * Create a UDP socket to use:
     */
    local_send_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (local_send_sock == -1)
        displayError("socket()");
    
    /*
     * Now start serving quotes:
     */
    initialize();
	
    for (;;) {
        /*
         * Update one quote in the list:
         */
        gen_quote();
		
        /*
         * Form a packet to send out:
         */
        bp = bcbuf;
        for (x = 0; x < MAXQ; ++x) {
            I0 = quotes[x].start / 100.0;
            I = quotes[x].current / 100.0;
            sprintf(bp,
                    "%-7.7s %8.2f %+.2f\n",
                    quotes[x].index,
                    I,
                    I - I0);
            bp += strlen(bp);
        }
        
        /*
         * Broadcast the updated info:
         */
        z = sendto(broadcast_sock,
                   bcbuf,
                   strlen(bcbuf),
                   0,
                   (struct sockaddr *)&broadcast_to_addr,
                   sizeof(broadcast_to_addr)); 
        
        if (z == -1)
			displayError("broadcast sendto()");
        
        z = sendto(local_send_sock,
                   bcbuf,
                   strlen(bcbuf),
                   0,
                   (struct sockaddr *)&local_to_addr,
                   sizeof(broadcast_to_addr)); 
        
        if (z == -1)
			displayError("local sendto()");
        
#ifdef WIN32
		Sleep(4);
#else
		sleep(4)
#endif
    }
    
    return 0;
}
