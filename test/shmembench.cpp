// shmembench.cpp -- benchmark test for a bridged shared memory process
//    based on shmemserver.cpp and o2client.cpp
//
// Roger B. Dannenberg
// Jan 2022

/* 
This test:
- initialize o2lite
- send/recv messages from O2 main thread to shared memory thread
*/

// o2usleep.h goes first to set _XOPEN_SOURCE to define usleep:
#undef NDEBUG
#include <stdlib.h>
#include "o2internal.h"
#include "pathtree.h"
#ifdef WIN32
#include <windows.h>
#include <mmsystem.h>
#else
#include <pthread.h>
pthread_t pt_thread_pid;
#endif
#include "o2atomic.h"
#include "sharedmem.h"


Bridge_info *smbridge = NULL;

int max_msg_count = 1000;
int n_addrs = 20;
bool client_running = true;
bool server_running = true;
int client_msg_count = 0;
int server_msg_count = 0;
char **client_addresses = NULL;
char **server_addresses = NULL;
/* with amortize, messages are sent in groups of 10, and server
   responds only to messages where the count is a multiple of 10
   (first count is 1). Response also contains 10 messages, from
   count-9 to count-0. The client side does the same up until the
   total message count reaches or passes client_msg_count.
 */
int amortize = false;

bool about_equal(double a, double b)
{
    return a / b > 0.999999 && a / b < 1.000001;
}

void client_test(O2msg_data_ptr data, const char *types,
                 O2arg_ptr *argv, int argc, const void *user_data)
{
    client_msg_count++;
    // the value we send is arbitrary, but we've already sent
    // 1 message with value 1, so the 2nd message will have 2, etc...
    int32_t i = client_msg_count + 1;

    // server will shut down when it gets data == -1
    if (client_msg_count >= max_msg_count) {
        i = -1;
        client_running = false;
    }
    if (amortize) {
        if (client_msg_count % 10 == 0) {
            for (int i = 0; i < 10; i++) {
                int count = client_msg_count + i - 10;
                o2_send_cmd(server_addresses[count % n_addrs], 0, "i", count);
            }
        }
    } else {
        o2_send_cmd(server_addresses[client_msg_count % n_addrs], 0, "i", i);
    }
    assert(client_msg_count == argv[0]->i32);
}


void *sharedmem();

int main(int argc, const char * argv[])
{
    o2_memory(malloc, free, NULL, 0, false);
    printf("Usage: shmembench [maxmsgs] [debugflags]\n");
    if (argc >= 2) {
        max_msg_count = atoi(argv[1]);
        printf("max_msg_count set to %d\n", max_msg_count);
        if (strchr(argv[1], 'a')) {
            amortize = true;
            printf("Found 'a'mortize: sending messages in groups of 10 to\n");
            printf("    amortize scheduling and polling costs.\n");
        }
    }
    if (argc >= 3) {
        o2_debug_flags(argv[2]);
        printf("debug flags are: %s\n", argv[2]);
    }
    if (argc > 3) {
        printf("WARNING: shmembench ignoring extra command line argments\n");
    }
    o2_initialize("test");

    // create the client service in the main thread here
    o2_service_new("client");
    
    // create methods for receiving messages here in the client service
    for (int i = 0; i < n_addrs; i++) {
        char path[100];
        sprintf(path, "/client/benchmark/%d", i);
        o2_method_new(path, "i", &client_test, NULL, false, true);
    }
    
    // create destination addresses in the server (shared mem thread)
    server_addresses = O2_MALLOCNT(n_addrs, char *);
    for (int i = 0; i < n_addrs; i++) {
        char path[100];
        sprintf(path, "!server/benchmark/%d", i);
        server_addresses[i] = O2_MALLOCNT(strlen(path) + 1, char);
        strcpy(server_addresses[i], path);
    }

    // create the server (shared memory thread)
    int res = o2_shmem_initialize();
    assert(res == O2_SUCCESS);

    smbridge = o2_shmem_inst_new();

    sharedmem(); // start and run the thread

    // we are the master clock
    o2_clock_set(NULL, NULL);

    // wait for the server to appear
    while (o2_status("server") < O2_REMOTE) {
        o2_poll();
        o2_sleep(2); // 2ms
    }
    printf("We discovered the server.\ntime is %g.\n", o2_time_get());
    
    double now = o2_time_get();
    while (o2_time_get() < now + 1) {
        o2_poll();
        o2_sleep(2);
    }
    
    printf("Here we go! ...\ntime is %g.\n", o2_time_get());
    o2_send_cmd("!server/benchmark/0", 0, "i", 1);
    if (amortize) {
        for (int i = 0; i < 9; i++) { // 9 more
            o2_send_cmd("!server/benchmark/0", 0, "i", 1 + i);
        }
    }

    while (client_running) {  // full speed busy wait
        o2_poll();
    }

    printf("** shmembench main ended run loop **\n");
    // wait 0.1s for thread to finish
    now = o2_time_get();
    while (o2_time_get() < now + 0.1) {
        o2_poll();
        o2_sleep(2); // 2ms
    }
    printf("*** shmembench main called o2_poll() for 0.1s after\n"
           "    shared mem process finished; calling o2_finish...\n");

    o2_finish();
}


/**************************** O2SM PROCESS **************************/
// switch to o2sm environment
#include "sharedmemclient.h"


// this is a handler for incoming messages. It simply sends a message
// back to one of the client addresses
//
void server_test(O2msg_data_ptr msg, const char *types, 
                 O2arg_ptr *argv, int argc, const void *user_data)
{
    assert(argc == 1);
    o2_extract_start(msg);
    O2arg_ptr i_arg = o2_get_next(O2_INT32);
    assert(i_arg);
    int got_i = i_arg->i;

    server_msg_count++;
    if (got_i == -1) {
        server_running = false;
    } else {
        assert(server_msg_count == got_i);
        if (amortize) {
            if (server_msg_count % 10 == 0) {
                for (int i = 0; i < 10; i++) {
                    int count = server_msg_count + i - 10;
                    o2sm_send_cmd(client_addresses[count  % n_addrs], 0, "i", count);
                }
            }
        } else {
            o2sm_send_cmd(client_addresses[server_msg_count % n_addrs], 0, "i",
                          server_msg_count);
        }
    }
}


bool sift_called = false;

// handles types "sift"
void sift_han(O2msg_data_ptr msg, const char *types,
              O2arg_ptr *argv, int argc, const void *user_data)
{
    O2arg_ptr as, ai, af, at;
    o2_extract_start(msg);
    if (!(as = o2sm_get_next(O2_STRING)) || !(ai = o2sm_get_next(O2_INT32)) ||
        !(af = o2sm_get_next(O2_FLOAT)) || !(at = o2sm_get_next(O2_TIME))) {
        printf("sift_han problem getting parameters from message");
        assert(false);
    }
    printf("sift_han called\n");
    assert(user_data == (void *) 111);
    assert(streql(as->s, "this is a test"));
    assert(ai->i == 1234);
    assert(about_equal(af->f, 123.4));
    assert(about_equal(at->t, 567.89));
    sift_called = true;
}

O2_context o2sm_ctx;


void sharedmem_init()
{
    o2sm_initialize(&o2sm_ctx, smbridge);
    o2sm_service_new("sift", NULL);

    o2sm_method_new("/sift", "sift", &sift_han, (void *)111, false, false);

    printf("shmemthread detected connected\n");

    o2_send_start();
    o2_add_string("this is a test");
    o2_add_int32(1234);
    o2_add_float(123.4F);
    o2_add_time(567.89);
    o2sm_send_finish(0, "/sift", true);
    printf("sent sift msg\n");
}

static int phase = 0;
static O2time start_wait;

// perform whatever the thread does. Return false when done.
bool o2sm_act()
{
    o2sm_poll();
    if (phase == 0) {
        if (o2sm_time_get() < 0) { // not synchronized
            return true;
        } else {
            printf("shmemthread detected clock sync\n");
            start_wait = o2sm_time_get();
            phase++;
        }
    }
    if (phase == 1) {
        if (start_wait + 1 > o2sm_time_get() && !sift_called) {
            return true;
        } else {
            assert(sift_called);
            printf("shmemthread received loop-back message\n");
            // we are ready for the client, so announce the server services
            o2sm_service_new("server", NULL);
            // now create addresses and handlers to receive server messages
            client_addresses = O2_MALLOCNT(n_addrs, char *);
            server_addresses = O2_MALLOCNT(n_addrs, char *);
            for (int i = 0; i < n_addrs; i++) {
                char path[100];

                sprintf(path, "!client/benchmark/%d", i);
                client_addresses[i] = O2_MALLOCNT(strlen(path) + 1, char);
                strcpy(client_addresses[i], path);

                sprintf(path, "/server/benchmark/%d", i);
                server_addresses[i] = O2_MALLOCNT(strlen(path) + 1, char);
                strcpy(server_addresses[i], path);
                o2sm_method_new(path, "i", &server_test, NULL, false, true);
            }
            phase++;
        }
    }
    if (phase == 2) {
        if (server_running) {
            return true;
        } else {
            for (int i = 0; i < n_addrs; i++) {
                O2_FREE(client_addresses[i]);
                O2_FREE(server_addresses[i]);
            }
            O2_FREE(client_addresses);
            O2_FREE(server_addresses);

            o2sm_finish();

            printf("shmembench:\nSERVER DONE\n");
            phase++;
            return false;
        }
    }
    return true; // should never get here
}


#ifdef WIN32
bool shmem_initialized = false;

void CALLBACK o2sm_run_callback(UINT timer_id, UINT msg, DWORD_PTR data, 
                                DWORD_PTR dw1, DWORD_PTR dw2)
{
    if (!shmem_initialized) {
        sharedmem_init();
        shmem_initialized = true;
    }
    if (!o2sm_act()) {
        timeKillEvent(timer_id);
    }
}

void *sharedmem()
{
    timeSetEvent(1, 0, &o2sm_run_callback, NULL, TIME_PERIODIC);
    return NULL;
}
#else
void *sharedmem_action(void *ignore)
{
    sharedmem_init();
    while (o2sm_act()) {
        ; // poll as fast as possible -- it's a benchmark
    }
    return NULL;
}

void *sharedmem()
{
    int res = pthread_create(&pt_thread_pid, NULL, &sharedmem_action, NULL);
    if (res != 0) {
        printf("ERROR: pthread_create failed\n");
    }
    return NULL;
}
#endif
