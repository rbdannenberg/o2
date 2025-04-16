/* udp-broadcast-server.c:
 * udp broadcast server example 
 * Example Stock Index Broadcast:
 */

#undef NDEBUG
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <time.h>


#ifdef WIN32
#include <winsock2.h> 
#include <windows.h>   
#include <ws2tcpip.h> 
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/poll.h>
#include <ifaddrs.h>
#endif

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#ifdef WIN32
typedef struct ifaddrs
{
	struct ifaddrs  *ifa_next;    /* Next item in list */
	char            *ifa_name;    /* Name of interface */
	unsigned int     ifa_flags;   /* Flags from SIOCGIFFLAGS */
	struct sockaddr *ifa_addr;    /* Address of interface */
	struct sockaddr *ifa_netmask; /* Netmask of interface */
	union
	{
		struct sockaddr *ifu_broadaddr; /* Broadcast address of interface */
		struct sockaddr *ifu_dstaddr; /* Point-to-point destination address */
	} ifa_ifu;
#define              ifa_broadaddr ifa_ifu.ifu_broadaddr
#define              ifa_dstaddr   ifa_ifu.ifu_dstaddr
	void            *ifa_data;    /* Address-specific data */
} ifaddrs;

int getifaddrs(struct ifaddrs **ifpp);
void freeifaddrs(struct ifaddrs *ifp);
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

	#ifdef WIN32
		int addr_len = sizeof(o2_serv_addr);
	#else
		socklen_t addr_len = sizeof(o2_serv_addr);
	#endif

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
#ifdef WIN32
	pfd[0].fd = server_sock;
	pfd[0].events = POLLIN;
	pfd_len = 1;

	FD_SET o2_read_set;
	struct timeval o2_no_timeout;
	int total;
	FD_ZERO(&o2_read_set);
	for (int i = 0; i < pfd_len; i++) {
		struct pollfd *d = pfd + i;
		FD_SET(d->fd, &o2_read_set);
	}
	o2_no_timeout.tv_sec = 0;
	o2_no_timeout.tv_usec = 0;
	while (TRUE) {
		if ((total = select(0, &o2_read_set, NULL, NULL, &o2_no_timeout)) == SOCKET_ERROR) {
			printf("read_set error\n");
		} else if (total == 0) {
			printf("No message is waiting!");
		} else {
			for (int i = 0; i < pfd_len; i++) {
				struct pollfd *d = pfd + i;
				if (FD_ISSET(d->fd, &o2_read_set)) {
					if (i == 0) { // connection request
						int connection = accept(d->fd, NULL, NULL);
						if (connection <= 0) {
							displayError("accept failed");
						}
						pfd[pfd_len].events = POLLIN;
						pfd[pfd_len++].fd = connection;
					}
					else {
						char buffer[1001];
						int len;
						if ((len = recvfrom(d->fd, buffer, 1000, 0, NULL, NULL)) <= 0) {
							displayError("recvfrom tcp failed");
						}
						buffer[len] = 0;
						printf("GOT %d: %s\n", len, buffer);
					}
				}
			}
		}
		Sleep(1);
#else
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
		sleep(1)
#endif

    }

    return 0;
}


#ifdef _WIN32

static struct sockaddr * dupaddr(const sockaddr_gen * src)
{
	sockaddr_gen * d = malloc(sizeof(*d));

	if (d) {
		memcpy(d, src, sizeof(*d));
	}

	return (struct sockaddr *) d;
}

int getifaddrs(struct ifaddrs **ifpp)
{
	SOCKET s = INVALID_SOCKET;
	size_t il_len = 8192;
	int ret = -1;
	INTERFACE_INFO *il = NULL;

	*ifpp = NULL;

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s == INVALID_SOCKET)
		return -1;

	for (;;) {
		DWORD cbret = 0;

		il = malloc(il_len);
		if (!il)
			break;

		ZeroMemory(il, il_len);

		if (WSAIoctl(s, SIO_GET_INTERFACE_LIST, NULL, 0,
			(LPVOID)il, (DWORD)il_len, &cbret,
			NULL, NULL) == 0) {
			il_len = cbret;
			break;
		}

		free(il);
		il = NULL;

		if (WSAGetLastError() == WSAEFAULT && cbret > il_len) {
			il_len = cbret;
		}
		else {
			break;
		}
	}

	if (!il)
		goto _exit;

	/* il is an array of INTERFACE_INFO structures.  il_len has the
	actual size of the buffer.  The number of elements is
	il_len/sizeof(INTERFACE_INFO) */

	{
		size_t n = il_len / sizeof(INTERFACE_INFO);
		size_t i;

		for (i = 0; i < n; i++) {
			struct ifaddrs *ifp;

			ifp = malloc(sizeof(*ifp));
			if (ifp == NULL)
				break;

			ZeroMemory(ifp, sizeof(*ifp));

			ifp->ifa_next = NULL;
			ifp->ifa_name = NULL;
			ifp->ifa_flags = il[i].iiFlags;
			ifp->ifa_addr = dupaddr(&il[i].iiAddress);
			ifp->ifa_netmask = dupaddr(&il[i].iiNetmask);
			ifp->ifa_broadaddr = dupaddr(&il[i].iiBroadcastAddress);
			ifp->ifa_data = NULL;

			*ifpp = ifp;
			ifpp = &ifp->ifa_next;
		}

		if (i == n)
			ret = 0;
	}

_exit:

	if (s != INVALID_SOCKET)
		closesocket(s);

	if (il)
		free(il);

	return ret;
}

void freeifaddrs(struct ifaddrs *ifp)
{
	free(ifp);
}

#endif
