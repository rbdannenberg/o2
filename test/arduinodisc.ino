#include "Printable.h"
#include "WiFi.h"
#include <string.h>
#include <mdns.h>

#define NETWORK_NAME "Aylesboro"
#define NETWORK_PSWD "4125214147"
#define HOSTNAME "esp32"
const int LED_PIN = 5;


static char host_ip[32]; // "100.100.100.100:65000" -> 21 chars

void print_line()
{
    printf("\n");
    for (int i = 0; i < 30; i++)
        printf("-");
    printf("\n");
}


void start_mdns_service()
{
    //initialize mDNS service
    esp_err_t err = mdns_init();
    if (err) {
        printf("MDNS Init failed: %d\n", err);
        return;
    }

    //set hostname
    mdns_hostname_set("my-esp32");
    //set default instance
    mdns_instance_name_set("Roger's ESP32 Thing");
    printf("start_mdns_service completed\n");
}


void connect_to_wifi(const char *hostname, const char *ssid, const char *pwd)
{
    int ledState = 0;

    print_line();
    printf("Connecting to WiFi network: %s\n", ssid);
    WiFi.begin(ssid, pwd);
    WiFi.setHostname(hostname);
  
    while (WiFi.status() != WL_CONNECTED) {
        // Blink LED while we're connecting:
        digitalWrite(LED_PIN, ledState);
        ledState = (ledState + 1) % 2; // Flip ledState
        delay(500);
        printf(".");
    }
    strcpy(host_ip, WiFi.localIP().toString().c_str());

    printf("\n");
    printf("WiFi connected!\nIP address: %s\n", host_ip);
}


void setup()
{
    Serial.begin(115200);
    printf("This is arduinodisc.ino\n");
    connect_to_wifi(HOSTNAME, NETWORK_NAME, NETWORK_PSWD);

    start_mdns_service();
    //find_mdns_service("_o2proc", "_tcp");
}

static const char * if_str[] = {"STA", "AP", "ETH", "MAX"};
static const char * ip_protocol_str[] = {"V4", "V6", "MAX"};

void mdns_print_results(mdns_result_t * results) {
    mdns_result_t * r = results;
    mdns_ip_addr_t * a = NULL;
    int i = 1, t;
    while(r){
        printf("%d: Interface: %s, Type: %s\n", i++, if_str[r->tcpip_if], ip_protocol_str[r->ip_protocol]);
        if(r->instance_name){
            printf("  PTR : %s\n", r->instance_name);
        }
        if(r->hostname){
            printf("  SRV : %s.local:%u\n", r->hostname, r->port);
        }
        if(r->txt_count){
            printf("  TXT : [%u] ", r->txt_count);
            for(t=0; t<r->txt_count; t++){
                printf("%s=%s; ", r->txt[t].key, r->txt[t].value);
            }
            printf("\n");
        }
        a = r->addr;
        while(a){
            if(a->addr.type == IPADDR_TYPE_V6){
                printf("  AAAA: " IPV6STR "\n", IPV62STR(a->addr.u_addr.ip6));
            } else {
                printf("  A   : " IPSTR "\n", IP2STR(&(a->addr.u_addr.ip4)));
            }
            a = a->next;
        }
        r = r->next;
    }
}

void find_mdns_service(const char * service_name, const char * proto)
{
    printf("INFO: Query PTR: %s.%s.local\n", service_name, proto);

    mdns_result_t * results = NULL;
    esp_err_t err = mdns_query_ptr(service_name, proto, 3000, 20,  &results);
    if (err) {
        printf("ERROR: Query Failed\n");
        return;
    }
    if (!results) {
        printf("WARNING: No results found!\n");
        return;
    }

    mdns_print_results(results);
    mdns_query_results_free(results);
}


int find_time = 1000;

void loop()
{ 
    if (millis() > find_time) {
	find_time = millis() + 5000;
        printf("... calling find_mdns_service()\n");
        find_mdns_service("_o2proc", "_tcp");
    }
}
