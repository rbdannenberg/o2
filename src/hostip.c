// hostip.c -- get the host ip address
//
// Roger B. Dannenberg, 2020
//

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#ifdef WIN32
#include <stdlib.h>
#include <Winsock2.h>
#include <iphlpapi.h>

#else

#include <sys/socket.h>
#include <unistd.h>    // define close()
#include <netdb.h>
#include <sys/time.h>

#include "sys/ioctl.h"
#include <ifaddrs.h>

#endif
#include <assert.h>
#include <stdio.h>
#include "o2base.h"
#include "hostip.h"

// this can be turned off before calling o2n_initialize():
bool o2n_network_enabled = true;
// this will be turned on if we find an internal IP address, but
// it stays false if o2n_network_enabled is false:
bool o2n_network_found = false;
char o2n_internal_ip[O2N_IP_LEN] = "";

void o2n_get_internal_ip(void)
{
    if (o2n_internal_ip[0]) {  // already known
        return;
    }
    assert(!o2n_network_found);  // not yet found
    assert(o2n_network_enabled); // otherwise, we should not be called
    // look for AF_INET interface. If you find one, copy it
    // to name. If you find one that is not 127.0.0.1, then
    // stop looking.
#ifdef WIN32
    ULONG outbuflen = 15000;
    DWORD rslt;
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                  GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_FRIENDLY_NAME;
    ULONG iterations = 0;
    PIP_ADAPTER_ADDRESSES addresses;
    do {
        addresses = (PIP_ADAPTER_ADDRESSES) O2_MALLOC(outbuflen);
        if (addresses == NULL) {
            return;
        }
        rslt = GetAdaptersAddresses(AF_INET, flags, NULL, 
                                    addresses, &outbuflen);
        if (rslt == ERROR_BUFFER_OVERFLOW) {
            O2_FREE(addresses);
            addresses = NULL;
        }
    } while (rslt == ERROR_BUFFER_OVERFLOW && iterations++ < 3);
    if (rslt != NO_ERROR) {
        return;
    }
    PIP_ADAPTER_ADDRESSES cur = addresses;
    while (cur) {
        if (cur->IfType != IF_TYPE_SOFTWARE_LOOPBACK &&
            cur->OperStatus == IfOperStatusUp) {
            PIP_ADAPTER_UNICAST_ADDRESS addr;
            for (addr = cur->FirstUnicastAddress; addr != NULL;
                addr = addr->Next) {
                SOCKADDR_IN *saddr = (SOCKADDR_IN *)addr->Address.lpSockaddr;
                snprintf(o2n_internal_ip, O2N_IP_LEN, "%08x",
                    ntohl(saddr->sin_addr.S_un.S_addr));
                if (!(streql(o2n_internal_ip, "7f000001"))) {
                    o2n_network_found = true;
                    break;
                }
            }
        }
        cur = cur->Next;
    }
    O2_FREE(addresses);
#else
    struct ifaddrs *ifap, *ifa;
    if (getifaddrs(&ifap)) {
        perror("getting IP address");
        return;
    }
    for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr->sa_family == AF_INET) {
	    struct sockaddr_in *sa = (struct sockaddr_in *) ifa->ifa_addr;
            snprintf(o2n_internal_ip, O2N_IP_LEN, "%08x",
                     ntohl(sa->sin_addr.s_addr));
            if (!streql(o2n_internal_ip, "7f000001")) {
                o2n_network_found = true;
                break;
            }
        }
    }
    freeifaddrs(ifap);
#endif
    // make sure we got an address:
    if (!o2n_internal_ip[0]) {
        strcpy(o2n_internal_ip, "7f000001");  // localhost
    }
}


static int hex_to_nibble(char hex)
{
    if (isdigit(hex)) return hex - '0';
    else if (hex >= 'A' && hex <= 'F') return hex - 'A' + 10;
    else if (hex >= 'a' && hex <= 'f') return hex - 'a' + 10;
#ifndef O2_NO_DEBUG
    printf("ERROR: bad hex character passed to hex_to_nibble()\n");
#endif
    return 0;
}

static int hex_to_byte(const char *hex)
{
    return (hex_to_nibble(hex[0]) << 4) + hex_to_nibble(hex[1]);
}



unsigned int o2_hex_to_int(const char *hex)
{
    char h;
    int i = 0;
    while ((h = *hex++)) {
        i = (i << 4) + hex_to_nibble(h);
    }
    return i;
}


// convert 8-char, 32-bit hex representation to dot-notation,
//   e.g. "7f000001" converts to "127.0.0.1"
//   dot must be a string of length 16 or more
void o2_hex_to_dot(const char *hex, char *dot)
{
    int i1 = hex_to_byte(hex);
    int i2 = hex_to_byte(hex + 2);
    int i3 = hex_to_byte(hex + 4);
    int i4 = hex_to_byte(hex + 6);
    snprintf(dot, 16, "%d.%d.%d.%d", i1, i2, i3, i4);
}

