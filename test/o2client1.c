

#include "o2.h"
#include "stdio.h"
#include "string.h"
#include "assert.h"

#ifdef WIN32
#include "usleep.h" // special windows implementation of sleep/usleep
#else
#include <unistd.h>
#endif


#define N_ADDRS 20

int max_msg_count = 90;

char *client2_addresses[N_ADDRS];
int msg_count = 0;
int running = TRUE;

void client1_test(o2_msg_data_ptr data, const char *types,
                 o2_arg_ptr *argv, int argc, void *user_data)
{	
	//printf("Entered handler method client1");
	msg_count++;
    // the value we send is arbitrary, but we've already sent
    // 1 message with value 1, so the 2nd message will have 2, etc...
    int32_t i = msg_count + 1;

    // server will shut down when it gets data == -1
    if (msg_count >= max_msg_count) {
        i = -1;
        running = FALSE;
    }
    o2_send_cmd(client2_addresses[msg_count % N_ADDRS], 0, "i", i);

    if (msg_count < 100) {
        printf("client message %d is %d\n", msg_count, argv[0]->i32);
    }
  //  assert(msg_count == argv[0]->i32);
  
}


int main(int argc, const char *argv[])
{
    printf("Usage: o2client1 maxmsgs debugflags "
           "(see o2.h for flags, use a for all)\n");
    if (argc >= 2) {
        max_msg_count = atoi(argv[1]);
        printf("max_msg_count set to %d\n", max_msg_count);
    }
    if (argc >= 3) 
	{
        o2_debug_flags(argv[2]);
        printf("debug flags are: %s\n", argv[2]);
    }
    if (argc > 3) {
        printf("WARNING: o2client1 ignoring extra command line argments\n");
    }
	printf("BEGIN: Initializing client1 node \n");
    o2_initialize("test");
    o2_service_new("client1");
    
    for (int i = 0; i < N_ADDRS; i++) {
        char path[100];
        sprintf(path, "/client1/benchmark/%d", i);
        o2_method_new(path, "i", &client1_test, NULL, FALSE, TRUE);
    }
    
    for (int i = 0; i < N_ADDRS; i++) {
        char path[100];
        sprintf(path, "!client2/benchmark/%d", i);
        client2_addresses[i] = (char *) (O2_MALLOC(strlen(path)));
        strcpy(client2_addresses[i], path);
    }
	
	o2_clock_set(NULL, NULL);
	printf("Setting o2client1 as the master clock for others to sync up.\n");
	//printf("client2 status is..%d.\n", o2_status("client2"));
    while (o2_status("client2") < O2_REMOTE) {
		
        o2_poll();
        usleep(2000); // 2ms
    }
	//printf("client2 status is..%d.\n", o2_status("client2"));
    printf("We discovered client2.\ntime is %g.\n", o2_time_get());
    
    double now = o2_time_get();
    while (o2_time_get() < now + 1) {
        o2_poll();
        usleep(2000);
    }
    
    printf("Here we go! ...\ntime is %g.\n", o2_time_get());
    
    o2_send_cmd("!client2/benchmark/0", 0, "i", 1);
    
    while (running) {
        o2_poll();
        //usleep(2000); // 2ms // as fast as possible
    }

    o2_finish();
    printf("CLIENT1 DONE\n");
    return 0;
}
