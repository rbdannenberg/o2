// bridgeapi.c -- simple test of bridge
//
// Roger B. Dannenberg
// July 2020

/* 
This test. We'll call our new protocol class Demo_protocol and 
subclass of Bridge_info will be Demo_info.
- install a new bridge with protocol "Demo"
- create a service named "bridge"
- view the service status
- send to the service
- receive and print messages arriving over the bridge
- send a timed message to the service
- receive message over the bridge and check timing
- change the bridge tag to internal scheduling
- send an untimed message
- check receipt of the untimed message to scheduling bridge
- send a timed message to BRIDGE
- check receipt of the timed message to scheduling bridge
- close the bridge
*/


// o2usleep.h goes first to set _XOPEN_SOURCE to define usleep:
#include <stdio.h>
#include <assert.h>
#include "o2internal.h"
#include "services.h"

O2message_ptr demo_incoming = NULL; // message queue
O2message_ptr sent_message = NULL;
int message_int = -9999;
int poll_outgoing_count = 0;
bool demo_protocol_destructed = false;
bool demo_info_destructed = false;

class Demo_protocol : public Bridge_protocol {
public:
    Demo_protocol() : Bridge_protocol("Demo") { }
    virtual ~Demo_protocol() { demo_protocol_destructed = true; }

    virtual O2err bridge_poll() {
        O2err rslt = O2_SUCCESS;
        // deliver just the last on each poll
        O2message_ptr *ptr = &demo_incoming;
        // we're not at the end if *ptr points to another message
        while (*ptr && (*ptr)->next) ptr = &((*ptr)->next);
        if (*ptr) { // send a mesage
            O2message_ptr msg = *ptr;
            *ptr = NULL;
            O2err err = o2_message_send(msg);
            // return the first non-success error code if any
            if (rslt == O2_SUCCESS) rslt = err;
        }
        return rslt;
    }
};

Bridge_protocol *demo_protocol = NULL;

static void print_status(int stat)
{
#ifndef O2_NO_DEBUG
    printf("Status of bridge is %s\n", o2_status_to_string((O2status) stat));
#else
    printf("Status of bridge is %d\n", stat);
#endif
}


class Demo_info : public Bridge_info {
public:
    bool no_scheduling_here;
    Demo_info() : Bridge_info(demo_protocol) {
        tag |= O2TAG_SYNCED;
        no_scheduling_here = true;
    }

    virtual ~Demo_info() {
        if (!this) return;
        // remove all sockets serviced by this connection
        proto->remove_services(this);
        demo_info_destructed = true;
    }

    // Demo is always "synchronized" with the Host because it uses the
    // host's clock. Also, since 3rd party processes do not distinguish
    // between Demo services and Host services at this IP address, they
    // see the service status according to the Host status. Once the Host
    // is synchronized with the 3rd party, the 3rd party expects that
    // timestamps will work. Thus, we always report that the Demo
    // process is synchronized.
    bool local_is_synchronized() { return true; }

    // Demo does scheduling, but only for increasing timestamps.
    bool schedule_before_send() { return no_scheduling_here; }

    O2err send(bool block) {
        bool tcp_flag;
        O2message_ptr msg = pre_send(&tcp_flag);
        // we have a message to send to the service via shared
        // memory -- find queue and add the message there atomically
        sent_message = msg;
        const char *types_str = o2_msg_types(msg);
        if (streql(msg->data.address, "/demobridge1/test") &&
            streql(types_str, "i")) {
            o2_extract_start(&msg->data);
            message_int = o2_get_next(O2_INT32)->i32;
            printf("got message at %g with int32 %d\n",
                   o2_time_get(), message_int);
            O2_FREE(msg);
        }
        return O2_SUCCESS;
    }

    void poll_outgoing() {
        poll_outgoing_count++;
    }

#ifndef O2_NO_DEBUG
    virtual void show(int indent) {
        printf("\n");
    }
#endif
    // virtual O2status status(const char **process);  -- see Bridge_info

    // Net_interface:
    O2err accepted(Fds_info *conn) { return O2_FAIL; }  // cannot accept
    O2err connected() { return O2_FAIL; } // we are not a TCP client
};

Demo_info *demo_info = NULL;

int main(int argc, const char * argv[])
{
    O2err err = O2_SUCCESS;

    if (argc == 2) {
        o2_debug_flags(argv[1]);
    }
    if (argc > 2) {
        printf("WARNING: bridgeapi ignoring extra command line argments\n");
    }
        
    if (o2_initialize("test") != O2_SUCCESS) {
        printf("o2_initialize failed\n");
        exit(1);
    }
    // install a new bridge with protocol "Demo"
    demo_protocol = new Demo_protocol();

    demo_info = new Demo_info();
    assert(demo_info);

    // create a service named "demobridge1"
    err = Services_entry::service_provider_new(
            "demobridge1", NULL, demo_info, o2_get_context()->proc);
    assert(err == O2_SUCCESS);

    // view the service status
    int stat = o2_status("demobridge1");
    print_status(stat);
    assert(stat == O2_BRIDGE_NOTIME);
    // send to the service
    o2_send("/demobridge1/test", 0, "i", 23); // no clock, no sync
    assert(message_int == 23);
    message_int = -1;

    // send a message to the service
    o2_clock_set(NULL, NULL);
    stat = o2_status("demobridge1");
    print_status(stat);
    assert(stat == O2_BRIDGE);
    o2_send("/demobridge1/test", 0, "i", 34); // clock, no sync
    assert(message_int == 34);
    message_int = -1;
    
    // send message over the bridge and check timing
    double now = o2_time_get();
    printf("timed send at %g for %g\n", now, now + 0.2);
    o2_send("/demobridge1/test", now + 0.2, "i", 45); // clock, future, no sync
    while (o2_time_get() < now + 0.4 && message_int != 45) {
        o2_poll();
        o2_sleep(2);
    }
    assert(message_int == 45);
    double delay = o2_time_get() - now;
    printf("expected delay = 0.2, actual delay = %g\n", delay);
    assert(delay > 0.19 && delay < 0.21);
    message_int = -1;

    // change the bridge status to internal scheduling
    demo_info->no_scheduling_here = false;
    stat = o2_status("demobridge1");
    print_status(stat);
    assert(stat == O2_BRIDGE);

    // send an untimed message
    o2_send("/demobridge1/test", 0, "i", 56); // clock, sync
    // check receipt of the untimed message to SYNCED bridge
    assert(message_int == 56);
    message_int = -1;

    // send a timed message to SYNCED bridge
    demo_info->tag |= O2TAG_SYNCED;
    now = o2_time_get();
    o2_send("/demobridge1/test", now + 0.2, "i", 67); // clock, future, sync

    // check receipt of the timed message to SYNCED bridge
    while (o2_time_get() < now + 0.4 && message_int != 67) {
        o2_poll();
        o2_sleep(2); // 2 ms
    }
    assert(message_int == 67);
    delay = o2_time_get() - now;
    printf("expected delay for timed message = 0.0, actual delay = %g\n",
           delay);
    assert(delay >= 0.0 && delay < 0.01);
    message_int = -1;

    // close the bridge
    delete demo_protocol;
    assert(demo_protocol_destructed);
    assert(demo_info_destructed);
   
    printf("calling o2_finish()\n");
    o2_finish();
    printf("BRIDGEAPI\nDONE\n");
}
