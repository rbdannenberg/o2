// stuniptest.c -- get public IP address using stun server
//
// Roger B. Dannenberg
// August 2020

#include "o2usleep.h"
#include "stdio.h"
#include "o2.h"
#include "o2internal.h"
#include "mqtt.h"

int main(int argc, const char * argv[])
{
    if (argc == 2) {
        o2_debug_flags(argv[1]);
    }
    o2_initialize("test");
    o2_err_t err = o2_mqtt_enable(NULL, 0);
    if (o2n_network_found && err) {
        printf("Error from o2_mqtt_enable: %d\n", err);
        assert(!err);
    } else if (!o2n_network_found && err != O2_NO_NETWORK) {
        printf("Unexpected return value %s (%d) when not o2n_network_found\n",
               o2_error_to_string(err), err);
    }
    o2_initialize("test");
    for (int i = 0; i < 1000; i++) { // run for 2 seconds
        o2_poll();
        usleep(2000); // 2ms
    }
    if (o2n_public_ip[0]) {
        printf("Public IP: %s\n", o2n_public_ip);
        printf("Full name: %s\n", o2_get_proc_name()); 
        if (o2n_network_found && !streql(o2n_public_ip, "00000000")) {
            printf("DONE\n");
        } else if (!o2n_network_found && streql(o2n_public_ip, "00000000")) {
            printf("DONE\n");
        } else {
            printf("FAILED\n");
        }
    }
    o2_finish();
    return 0;
}
