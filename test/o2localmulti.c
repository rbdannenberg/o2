//  o2client.c - part of performance benchmark
//
//  see o2server.c for details


#include "o2.h"
#include "stdio.h"
#include "string.h"
#include "assert.h"

#ifdef WIN32
#include "usleep.h" // special windows implementation of sleep/usleep
#else
#include <unistd.h>
#endif


#define N_ADDRS 5
int max_msg_count = 5;
char *server_addresses[N_ADDRS];
int msg_count_client1 = 0, msg_count_client2 = 0, msg_count_client3 = 0, msg_count_client4 = 0, msg_count = 0;
int id = 1;
int serverrunning = TRUE;
int client1running = TRUE;
int client2running = TRUE;
int client3running = TRUE;
int client4running = TRUE;


char *client1_addresses[N_ADDRS], *client2_addresses[N_ADDRS], *client3_addresses[N_ADDRS], *client4_addresses[N_ADDRS];

// this is a handler for incoming messages. It simply sends a message
// back to one of the client addresses
//
void server_test(o2_msg_data_ptr msg, const char *types,
	o2_arg_ptr *argv, int argc, void *user_data)
{
	assert(argc == 1);
	msg_count++;
	o2_send(client1_addresses[msg_count % N_ADDRS], 0, "i", msg_count);

	printf("server received %d messages\n", msg_count);


	if (argv[0]->i32 == -1) {
		serverrunning = FALSE;
	}
		


}

void client1_test(o2_msg_data_ptr data, const char *types,
                 o2_arg_ptr *argv, int argc, void *user_data)
{
    msg_count_client1++;
    // the value we send is arbitrary, but we've already sent
    // 1 message with value 1, so the 2nd message will have 2, etc...
    int32_t i = msg_count_client1 + 1;

    // server will shut down when it gets data == -1
	 if (argv[0]->i32 == -1) {
        i = -1;
        client1running = FALSE;
    } 
  
    o2_send(client2_addresses[msg_count_client1 % N_ADDRS], 0, "i", i);
	o2_send(client3_addresses[msg_count_client1 % N_ADDRS], 0, "i", i);

    if (msg_count_client1 < 20) {
        printf("Same message has been sent to client 2 and client 3", i);
    }
    //assert(msg_count_client1 == argv[0]->i32);
}


void client2_test(o2_msg_data_ptr data, const char *types,
                 o2_arg_ptr *argv, int argc, void *user_data)
{
    msg_count_client2++;
    // the value we send is arbitrary, but we've already sent
    // 1 message with value 1, so the 2nd message will have 2, etc...
    int32_t i = msg_count_client2 + 1;

    // server will shut down when it gets data == -1
    if (argv[0]->i32 == -1) {
        i = -1;
        client2running = FALSE;
    } 
    o2_send(client3_addresses[msg_count_client2 % N_ADDRS], 0, "i", i);
	o2_send(client1_addresses[msg_count_client2 % N_ADDRS], 0, "i", i);

    printf("Same message has been sent to client 3 and client 4", i);
    
	// assert(msg_count_client2 == argv[0]->i32);
}


void client3_test(o2_msg_data_ptr data, const char *types,
                 o2_arg_ptr *argv, int argc, void *user_data)
{
    msg_count_client3++;
    // the value we send is arbitrary, but we've already sent
    // 1 message with value 1, so the 2nd message will have 2, etc...
    int32_t i = msg_count_client3 + 1;

    // server will shut down when it gets data == -1
    if (argv[0]->i32 == -1) {
        i = -1;
        client3running = FALSE;
    } 
    o2_send(client1_addresses[msg_count_client3 % N_ADDRS], 0, "i", i);
	o2_send(client4_addresses[msg_count_client3 % N_ADDRS], 0, "i", i);

    printf("Same message has been sent to client 4 and client 1", i);
   
	// assert(msg_count_client3 == argv[0]->i32);
}

void client4_test(o2_msg_data_ptr data, const char *types,
                 o2_arg_ptr *argv, int argc, void *user_data)
{
    msg_count_client4++;
    // the value we send is arbitrary, but we've already sent
    // 1 message with value 1, so the 2nd message will have 2, etc...
    int32_t i = msg_count_client4 + 1;

    // server will shut down when it gets data == -1
    if (argv[0]->i32 == -1) {
        i = -1;
        client4running = FALSE;
    } 
	
	
	if(msg_count_client4 == max_msg_count)
	{
		i = -1;
		
	}
	
    o2_send(server_addresses[msg_count_client4 % N_ADDRS], 0, "i", i);
	o2_send(client2_addresses[msg_count_client4 % N_ADDRS], 0, "i", i);

    printf("Same message has been sent to server and client 2", i);
	
    
    //assert(msg_count_client4 == argv[0]->i32);
}

int main(int argc, const char *argv[])
{
    printf("Usage: o2client id debugflags "
           "(see o2.h for flags, use a for all)\n");
    if (argc >= 2) {
        id = atoi(argv[1]);
        printf("ID is set to %d\n", id);
    }
    if (argc >= 3) {
        o2_debug_flags(argv[2]);
        printf("debug flags are: %s\n", argv[2]);
    }
    if (argc > 3) {
        printf("WARNING: ignoring extra command line argments\n");
    }

    o2_initialize("test");
	
	
	if(id == 1) 
	{	
		o2_service_new("server");
		for (int i = 0; i < N_ADDRS; i++) {
        char path[100];
        sprintf(path, "/server/benchmark/%d", i);
        o2_method_new(path, "i", &server_test, NULL, FALSE, TRUE);
		}
		
		// create an address for each destination so we do not have to
		// do string manipulation to send a message
		for (int i = 0; i < N_ADDRS; i++) {
			char path[100];
			
			sprintf(path, "!client1/benchmark/%d", i);
			client1_addresses[i] = (char *) (O2_MALLOC(strlen(path)));
			strcpy(client1_addresses[i], path);
			
			sprintf(path, "!client2/benchmark/%d", i);
			client2_addresses[i] = (char *) (O2_MALLOC(strlen(path)));
			strcpy(client2_addresses[i], path);
			
			sprintf(path, "!client3/benchmark/%d", i);
			client3_addresses[i] = (char *) (O2_MALLOC(strlen(path)));
			strcpy(client3_addresses[i], path);
			
			sprintf(path, "!client4/benchmark/%d", i);
			client4_addresses[i] = (char *) (O2_MALLOC(strlen(path)));
			strcpy(client4_addresses[i], path);
		}

		// we are the master clock
		o2_clock_set(NULL, NULL);
		
		// wait for client1 service to be discovered
	/*	while (o2_status("client1") < O2_REMOTE) {
			o2_poll();
			usleep(2000); // 2ms
		}
		
		printf("We discovered the client1 at time %g.\n", o2_time_get());
		
		while (o2_status("client2") < O2_REMOTE) {
			o2_poll();
			usleep(2000); // 2ms
		}
		
		printf("We discovered the client2 at time %g.\n", o2_time_get());
		
		while (o2_status("client3") < O2_REMOTE) {
			o2_poll();
			usleep(2000); // 2ms
		}
		
		printf("We discovered the client3 at time %g.\n", o2_time_get());
		
		while (o2_status("client4") < O2_REMOTE) {
			o2_poll();
			usleep(2000); // 2ms
		}
		
		printf("We discovered the client4 at time %g.\n", o2_time_get());
		
		*/
		while(serverrunning) {
			o2_poll();
			//usleep(2000); // 2ms // as fast as possible
		}

		o2_finish();
		printf("SERVER DONE\n");
		return 0;
	
	}
	
	if(id == 2)
	{	
		
		o2_service_new("client1");
		for (int i = 0; i < N_ADDRS; i++) {
		char path[100];
        sprintf(path, "/client1/benchmark/%d", i);
        o2_method_new(path, "i", &client1_test, NULL, FALSE, TRUE);
		}
		
		
		for (int i = 0; i < N_ADDRS; i++) {
        char path[100];
        sprintf(path, "!server/benchmark/%d", i);
        server_addresses[i] = (char *) (O2_MALLOC(strlen(path)));
        strcpy(server_addresses[i], path);
		}

		while (o2_status("server") < O2_REMOTE) {
			//printf("Server status is..%d.\n", o2_status("server"));
			o2_poll();
			usleep(2000); // 2ms
		}
		printf("We discovered the server.\ntime is %g.\n", o2_time_get());
		
		double now = o2_time_get();
		while (o2_time_get() < now + 1) {
			o2_poll();
			usleep(2000);
		}
		
		printf("Here we go! ...\ntime is %g.\n", o2_time_get());
		
		o2_send_cmd("!server/benchmark/0", 0, "i", 1);
		
		
		while(client1running) {
			o2_poll();
			//usleep(2000); // 2ms // as fast as possible
		}

		o2_finish();
		printf("CLIENT 1 DONE\n");
		return 0;
	}    
	
	if(id == 3)
	{	
		
		o2_service_new("client2");
		for (int i = 0; i < N_ADDRS; i++) {
		char path[100];
        sprintf(path, "/client2/benchmark/%d", i);
        o2_method_new(path, "i", &client2_test, NULL, FALSE, TRUE);
		}
		
		while(client2running) {
			o2_poll();
			//usleep(2000); // 2ms // as fast as possible
		}

		o2_finish();
		printf("CLIENT 2 DONE\n");
		return 0;
	}    
 
 
 	if(id == 4)
	{	
		
		o2_service_new("client3");
		for (int i = 0; i < N_ADDRS; i++) {
		char path[100];
        sprintf(path, "/client3/benchmark/%d", i);
        o2_method_new(path, "i", &client3_test, NULL, FALSE, TRUE);
		}
		
		while(client3running) {
			o2_poll();
			//usleep(2000); // 2ms // as fast as possible
		}

		o2_finish();
		printf("CLIENT 3 DONE\n");
		return 0;
	}    
 
	if(id == 5)
	{	
		o2_service_new("client4");
		for (int i = 0; i < N_ADDRS; i++) {
		char path[100];
        sprintf(path, "/client4/benchmark/%d", i);
        o2_method_new(path, "i", &client4_test, NULL, FALSE, TRUE);
		}
		
		while(client4running) {
			o2_poll();
			//usleep(2000); // 2ms // as fast as possible
		}

		o2_finish();
		printf("CLIENT 4 DONE\n");
		return 0;
	} 
   
    
    //o2_send("!server/benchmark/0", 0, "i", 1);
    
   
}
