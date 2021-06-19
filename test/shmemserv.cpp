// shmemserv.c -- test for a bridged shared memory process
//
// Roger B. Dannenberg
// July 2020

/* 
This test:
- initialize o2lite
- wait for discovery
- wait for clock sync
- send a message to self over O2 with sift types
- respond to messages from o2litehost's client services
*/

// o2usleep.h goes first to set _XOPEN_SOURCE to define usleep:
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
#include "sharedmem.h"


Bridge_info *smbridge = NULL;

int n_addrs = 20;
bool running = true;
int msg_count = 0;
char **client_addresses = NULL;
char **server_addresses = NULL;


bool about_equal(double a, double b)
{
    return a / b > 0.999999 && a / b < 1.000001;
}

bool use_tcp = false;


void *sharedmem();

int main(int argc, const char * argv[])
{
    printf("Usage: shmemserv tcp [debugflags]\n"
           "    pass t to test with TCP, u for UDP\n");
    if (argc >= 2) {
        if (strchr(argv[1], 't' )) {
            use_tcp = true;
        }
        printf("Using %s to reply to client\n", use_tcp ? "TCP" : "UDP");
    }
    if (argc >= 3) {
        o2_debug_flags(argv[2]);
        printf("debug flags are: %s\n", argv[2]);
    }
    if (argc > 3) {
        printf("WARNING: shmemserv ignoring extra command line argments\n");
    }
    o2_initialize("test");
    int res = o2_shmem_initialize();
    assert(res == O2_SUCCESS);
    smbridge = o2_shmem_inst_new();

    sharedmem(); // start and run the thread

    // we are the master clock
    o2_clock_set(NULL, NULL);

    o2_run(500);
    printf("** shmemserv main returned from o2_run\n");
    // wait 0.1s for thread to finish
    O2time now = o2_time_get();
    while (o2_time_get() < now + 0.1) {
        o2_poll();
        o2_sleep(2); // 2ms
    }
    printf("*** shmemserv main called o2_poll() for 0.1s after\n"
           "    shared mem process finished; calling o2_finish...\n");

    o2_finish();
}


/**************************** O2SM PROCESS **************************/
// switch to o2sm environment
#include "sharedmemclient.h"


// this is a handler for incoming messages. It simply sends a message
// back to one of the client addresses
//
void server_test(o2_msg_data_ptr msg, const char *types, 
                 O2arg_ptr *argv, int argc, const void *user_data)
{
    assert(argc == 1);
    o2_extract_start(msg);
    O2arg_ptr i_arg = o2_get_next(O2_INT32);
    assert(i_arg);
    int got_i = i_arg->i;

    msg_count++;
    if (use_tcp) {
        o2sm_send_cmd(client_addresses[msg_count % n_addrs], 0, "i", msg_count);
    } else {
        o2sm_send(client_addresses[msg_count % n_addrs], 0, "i", msg_count);
    }
    if (msg_count % 10000 == 0) {
        printf("server received %d messages\n", msg_count);
    }
    if (msg_count < 100) {
        printf("server message %d is %d\n", msg_count, got_i);
    }
    if (got_i == -1) {
        running = false;
    } else {
        assert(msg_count == got_i);
    }
}


bool sift_called = false;

// handles types "sift"
void sift_han(o2_msg_data_ptr msg, const char *types,
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
        if (running) {
            return true;
        } else {
            for (int i = 0; i < n_addrs; i++) {
                O2_FREE(client_addresses[i]);
                O2_FREE(server_addresses[i]);
            }
            O2_FREE(client_addresses);
            O2_FREE(server_addresses);

            o2sm_finish();
            o2_stop_flag = true; // shut down O2

            printf("shmemserv:\nSERVER DONE\n");
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
        o2_sleep(2); // don't poll too fast - it's unnecessary
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
