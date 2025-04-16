// stuniptest.c -- get public IP address using stun server
//
// Roger B. Dannenberg
// August 2020

#undef NDEBUG
#include "stdio.h"
#include "o2.h"
#include "o2internal.h"

int main(int argc, const char * argv[])
{
    printf("stuniptest [debugflags] - test if O2 obtains public IP\n");
    if (argc == 2) {
        o2_debug_flags(argv[1]);
    }
    o2_initialize("test");
    O2err err = o2_mqtt_enable(NULL, 0);
    if (o2n_network_found && err) {
        printf("Error from o2_mqtt_enable: %d\n", err);
        assert(!err);
    } else if (!o2n_network_found && err != O2_NO_NETWORK) {
        printf("Unexpected return value %s (%d) when not o2n_network_found\n",
               o2_error_to_string(err), err);
    }
    // test for reinitialization attempt:
    assert(o2_initialize("test") == O2_ALREADY_RUNNING);
    for (int i = 0; i < 5000; i++) { // run for up to 10 seconds
        o2_poll();
        if (o2n_public_ip[0]) break;
        o2_sleep(2); // 2ms
        if (i % 500 == 0) printf("- polling @ %g\n", o2_local_time());
    }
    if (o2n_public_ip[0]) {
        char pip_dot[O2N_IP_LEN];
        o2_hex_to_dot(o2n_public_ip, pip_dot);
        printf("Public IP: %s (%s)\n", o2n_public_ip, pip_dot);
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
