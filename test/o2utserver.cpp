//  o2utserver.c - benchmark for message passing
//
//  This program works with o2utclient.c. It is a performance test
//  that sends UDP and TCP from client to server and measures 
//  timing and packet drops.

//  Send 100 messages per second, alternating TCP and UDP from
//  client to server. 10000 of each type: 200 seconds (3.3 mins)
//  Count message drops from UDP and measure longest time interval
//  between UDP messages.
//  For TCP, measure longest time interval between messages.

#undef NDEBUG
#include "o2.h"
#include "stdio.h"
#include "string.h"
#include "assert.h"
#define MAX(x, y) ((x) > (y) ? (x) : (y))

#define LATE 0.110 // how late increments late_counts?

#define MAX_MSG_COUNT 200000

int last_udp = -2;
int last_tcp = -1;
bool running = true;
double last_tcp_time = 1000000;
double max_tcp_interval = 0;
int tcp_late_count = 0;
double last_udp_time = 1000000;
int udp_drop_count = 0;
int udp_late_count = 0;
double max_udp_interval = 0;


// this is a handler for incoming messages. It simply sends a message
// back to one of the client addresses
//
void server_tcp(O2msg_data_ptr msg, const char *types,
                O2arg_ptr *argv, int argc, const void *user_data)
{
    assert(argc == 1);
    int i = argv[0]->i32;
    if (i == -1) {
        running = false;
    } else {
        last_tcp += 2;
        assert(last_tcp == i);
        double now = o2_local_time();
		double lateness = now - last_tcp_time;
        max_tcp_interval = MAX(max_tcp_interval, lateness);
		if (lateness > LATE) tcp_late_count++;
        last_tcp_time = now;
    }
}


void server_udp(O2msg_data_ptr msg, const char *types,
                O2arg_ptr *argv, int argc, const void *user_data)
{
    assert(argc == 1);
    int i = argv[0]->i32;

    last_udp += 2;
    int dropped = (i - last_udp) / 2;
    if (dropped >= 0) {
        udp_drop_count += dropped;
        last_udp = i;
    }
    double now = o2_local_time();
	double lateness = now - last_tcp_time;
	max_udp_interval = MAX(max_udp_interval, lateness);
	if (lateness > LATE) udp_late_count++;
    last_udp_time = now;
}


// poll O2 every ms for about dur seconds. If dur == 0, 
//     poll once and delay 1ms
static void ppause(double dur)
{
    do {
        o2_poll();
        o2_sleep(1); // 1ms
        dur -= 0.001;
    } while (dur > 0);
}


int main(int argc, const char *argv[])
{
    printf("Usage: o2utserver [debugflags]\n"
           "    see o2.h for flags, use a for (almost) all, - for none\n");
    if (argc >= 2) {
        if (argv[1][0] != '-') {
            o2_debug_flags(argv[1]);
            printf("debug flags are: %s\n", argv[1]);
        }
    }
    if (argc > 2) {
        printf("WARNING: o2server ignoring extra command line argments\n");
    }

    o2_initialize("test");
    o2_service_new("server");
    o2_method_new("/server/tcp", "i", &server_tcp, NULL, false, true);
    o2_method_new("/server/udp", "i", &server_udp, NULL, false, true);

    
    // we are the master clock
    o2_clock_set(NULL, NULL);
    
    while (running) {
        ppause(0.0);
    }
    
    o2_finish();
	printf("udp drop count %d\n", udp_drop_count);
    printf("last udp message id was %d\n", last_udp);
    printf("max_udp_interval %g\n", max_udp_interval);
	printf("udp_late_count %d\n", udp_late_count);
    printf("last tcp message id was %d\n", last_tcp);
    printf("max_tcp_interval %g\n", max_tcp_interval);
	printf("tcp_late_count %d\n", tcp_late_count);
    printf("SERVER DONE\n");
    return 0;
}
