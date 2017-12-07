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


#define N_ADDRS 10
int max_msg_count;
char *server_addresses[N_ADDRS];
int msg_count_client=0;
int id = 1;
int serverrunning = TRUE;
int clientrunning = TRUE;
int globalcount;


char *client_addresses[N_ADDRS];

// this is a handler for incoming messages. It simply sends a message
// back to all the client addresses

void server_test(o2_msg_data_ptr msg, const char *types,
    o2_arg_ptr *argv, int argc, void *user_data)
{
    //assert(argc == 1);
    max_msg_count++;
    int32_t i = max_msg_count + 1;
   // if(i == max_msg_count) serverrunning = FALSE;
      //client1running = FALSE;
    printf("\n server received message from client%d", argv[0]->i32);
		
		for (int i = 0; i < N_ADDRS; i++) {
            o2_send_cmd(client_addresses[i], 0, "i", 0);
		//o2_send(client_addresses[1], 0, "i", 0);
        }
		
	if (max_msg_count == (globalcount-1) * 10)
		serverrunning = FALSE;
}

void client_test(o2_msg_data_ptr data, const char *types,
                 o2_arg_ptr *argv, int argc, void *user_data)
{
	//printf("Client handler called");
    msg_count_client++;
    // the value we send is arbitrary, but we've already sent
    // 1 message with value 1, so the 2nd message will have 2, etc...
    int32_t i = msg_count_client + 1;

    // server will shut down when it gets data == -1
	if (msg_count_client == 10) {
        //i = -1;
        clientrunning = FALSE;
    } 	
		
	o2_send_cmd(server_addresses[msg_count_client % N_ADDRS], 0, "i", i);
	printf("Message %d has been sent to the server\n", i);
}


int main(int argc, const char *argv[])
{
    
    if (argc >= 2) {
        id = atoi(argv[1]);
        globalcount = atoi(argv[2]);
        printf("ID is set to %d\n", id);
    }
    if (argc >= 4) {
        printf("WARNING: ignoring extra command line arguments\n");
    }

    o2_initialize("test");


    if(id == 1)
    {
        printf("I AM THE SERVER! \n");
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
            sprintf(path, "!client%d/benchmark/%d",i, i);
            client_addresses[i] = (char *) (O2_MALLOC(strlen(path)));
            strcpy(client_addresses[i], path);
        }

        // we are the master clock
        o2_clock_set(NULL, NULL);

        while(serverrunning) {
            o2_poll();
            //usleep(2000); // 2ms // as fast as possible
        }

        o2_finish();
        printf("\nSERVER DONE\n");
        return 0;

    }

    else
    {			
        printf("I AM CLIENT !\n");
        char path[100];
        sprintf(path, "client%d", id-2);
        o2_service_new(path);

        for (int i = 0; i < N_ADDRS; i++) {
            char path[100];
            sprintf(path, "!server/benchmark/%d", i);
            server_addresses[i] = (char *) (O2_MALLOC(strlen(path)));
            strcpy(server_addresses[i], path);
        }
   
        char clientPath[100];
        sprintf(clientPath, "/client%d/benchmark/%d", id-2, id-2);
        o2_method_new(clientPath, "i", &client_test, NULL, FALSE, TRUE);       

        printf("server status %d \n",o2_status("server") );
        while (o2_status("server") < O2_REMOTE) {
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
        o2_send_cmd("!server/benchmark/0", 0, "i", id-2);

        while(clientrunning) {
            o2_poll();
        } 
       
        o2_finish();
        printf("\nCLIENT DONE\n");
        return 0;
    }


}
