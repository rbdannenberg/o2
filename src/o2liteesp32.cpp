// o2liteesp32.c -- discovery implementation for ESP32 o2lite
//
// Roger B. Dannenberg
// Aug 2021

// this also includes some o2lite functions that require C++

#include "o2lite.h"
#include "Printable.h"
#include "WiFi.h"
#include <string.h>
#include <mdns.h>
//#include <lwip/sockets.h>
//#include <lwip/netdb.h>
//#include "esp32-hal.h"

const int LED_PIN = 5;
const int BUTTON_PIN = 0;

void print_line()
{
    printf("\n");
    for (int i = 0; i < 30; i++)
        printf("-");
    printf("\n");
}

// manager for blinking the blue status light on ESP32 Thing
// (for other ESP32 boards, we will have to change this or figure
// out a compile-time switch to do the right thing.)
//
// Call this with the "status code" - a small integer controlling
// how many blinks.
// 
// Example sequence: if status is 2, blink_count is set to 4, and
// blink_next is set to 250ms and LED is turned on
// Time:        0 250 500 750 1500 ...
// blink_count: 3   2   1   0    3 ...
// LED:        ON OFF  ON OFF   ON ...
#define BLINK_PERIOD 250
int blink_count = 0;   // countdown for how many blinks * 2
int blink_next = 0;    // next time to do something

void blink_init()
{
    blink_next = millis();
    blink_count = 0;
    pinMode(LED_PIN, OUTPUT);  // ESP32 Thing specific output setup
    pinMode(BUTTON_PIN, INPUT_PULLUP);
}

// blink N flashes followed by longer interval. This does not
// delay the caller much, but must be called repeatedly with
// the same value of n.
//
// Prerequisite: caller must have recently called thing_blink_init()
//
void blink(int n)
{
    if (blink_next > millis()) {
        return;
    }
    digitalWrite(LED_PIN, blink_count & 1);
    blink_next += BLINK_PERIOD;
    if (blink_count == 0) {
        blink_count = (n << 1);
        blink_next += (BLINK_PERIOD << 1);
    }
    blink_count--;
}


// quick flash -- delays caller by 100ms
// repeated calls may just appear to stuck on
void flash()
{
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
}
    

void connect_to_wifi(const char *hostname, const char *ssid, const char *pwd)
{
    blink_init();
    print_line();
    printf("Connecting to WiFi network: %s\n", ssid);
    WiFi.begin(ssid, pwd);
    WiFi.setHostname(hostname);
  
    while (WiFi.status() != WL_CONNECTED) {
        blink(1);  // Blink LED while we're connecting:
    }
    uint32_t ip = WiFi.localIP();
    ip = htonl(ip);
    snprintf(o2n_internal_ip, O2N_IP_LEN, "%08x", ip);
    char dot_ip[O2N_IP_LEN];
    o2_hex_to_dot(o2n_internal_ip, dot_ip);
    printf("\nWiFi connected! IP address: %s (%s)\n", o2n_internal_ip, dot_ip);
}


#ifndef O2_NO_ZEROCONF

// assumes connect_to_wifi has been called already.
int o2ldisc_init(const char *ensemble)
{
    o2l_ensemble = ensemble;
    //initialize mDNS service
    esp_err_t err = mdns_init();
    if (err) {
        printf("ERROR: mdns_init() failed: %d\n", err);
        return O2L_FAIL;
    }

    //set hostname
    mdns_hostname_set("o2esp32");

    //set default instance
    mdns_instance_name_set("O2 ESP32");

    return O2L_SUCCESS;
}


int check_for_proc(const char *proc_name, const char *vers_num, 
                   char *internal_ip, int port, int *udp_send_port)
{
    // names are fixed length -- reject if invalid
    if (!proc_name || strlen(proc_name) != 28) {
        return -1;
    }
    O2LDB printf("o2lite: discovered name=%s\n", proc_name);
    if (!o2l_is_valid_proc_name(proc_name, port, internal_ip, udp_send_port)) {
        return -1;
    }
    int version;
    O2LDB if (vers_num) { printf("        discovered vers=%s\n", vers_num); }
    if (!vers_num ||
        (version = o2l_parse_version(vers_num, strlen(vers_num)) == 0)) {
        return -1;
    }
    return 0;
}


void o2ldisc_poll()
{
    static int resolve_timeout = 0; // start as soon as possible
    O2LDBV printf("tcp_sock %d\n", tcp_sock);
    if (tcp_sock != INVALID_SOCKET) {
        return;
    }
    if (o2l_local_now < resolve_timeout) {
    if (o2l_local_now < resolve_timeout) {
        blink(2);  // 2 means waiting to find an O2 host
        return;
    }
    mdns_result_t *results = NULL;
    flash();
    esp_err_t err = mdns_query_ptr("_o2proc", "_tcp", 3000, 20, &results);
    blink_init();  // since time has passed, reset blink state
    resolve_timeout = o2l_local_time() + 2.0; // 2s before we try again
    if (err) {
        printf("ERROR: mdns_query_ptr Failed: %d\n", err);
        return;
    }
    if (!results) {
        return;
    }
        
    mdns_result_t *r = results;
    while (r) {
        const char *proc_name = NULL;
        const char *vers_num = NULL;
        char internal_ip[O2N_IP_LEN];
        int udp_port;

        if (!r->instance_name ||
            !streql(r->instance_name, o2l_ensemble) ||
            !r->txt_count) {
            continue;
        }
        for (int i = 0; i < r->txt_count; i++) {
            if (streql(r->txt[i].key, "name")) {
                proc_name = r->txt[i].value;
            }
            if (streql(r->txt[i].key, "vers")) {
                vers_num = r->txt[i].value;
            }
        }
        if (check_for_proc(proc_name, vers_num, internal_ip, r->port,
                           &udp_port) < 0) {
            continue;
        }
        char iip_dot[16];
        o2_hex_to_dot(internal_ip, iip_dot);
        o2l_address_init(&udp_server_sa, iip_dot, udp_port, false);
        o2l_address_init(&udp_server_sa, iip_dot, udp_port, false);
        o2l_network_connect(iip_dot, r->port);
        break;
    }
    mdns_query_results_free(results);
}

void o2ldisc_events(fd_set *read_set_ptr)
{
    return;
}

#endif
