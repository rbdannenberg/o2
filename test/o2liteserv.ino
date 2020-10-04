// o2liteserv.ino -- this is an adaptation of o2liteserv.c. It is modified to
// run on ESP32 under Arduino IDE. The main difference is that the Arduino
// environment initially calls setup() (instead of main), and then calls
// loop(). In this version of o2liteserv, the loop code maintains a state
// and switches to different code blocks in sequence to simulate the sequential
// organization of o2liteserv.c
//
// Roger B. Dannenberg
// Aug 2020

#include "src/o2lite.h"

// WiFi network name and password:
// Yes, this is really my network and password, but it's a stand-alone network
// that I rarely connect to and has no Internet service, so you don't need to
// notify me that I stupidly exposed my network password.
//
// To use this code elsewhere, fill in your information here and keep it safe
// or use a spare WiFi hub you can give open access to.
#define NETWORK_NAME "Aylesboro"
#define NETWORK_PSWD "4125214147"
#define HOSTNAME "esp32"

// no command line, so we'll select tcp here. If you select false and you
// drop a message, this test may not complete.

#define use_tcp 1

#define streql(a, b) (strcmp(a, b) == 0)

int n_addrs = 20;
bool running = true;
int msg_count = 0;
char **client_addresses = NULL;
char **server_addresses = NULL;

// mustbe is a replacement for assert that hangs in a loop instead of rebooting
// so that serial output will stop and you can more easily study the problem
//
#define mustbe(EX) (void) ((EX) || (assertfn(#EX, __FILE__, __LINE__), 0))

void print_line(); // declare handy utility from o2lite.cpp

void stop()
{
    print_line();
    printf("BEGIN INFINITE LOOP\n");
    print_line();
    while (true) {
        ;
    }
}
        

void assertfn(const char *msg, const char *file, int line)
{
    print_line();
    printf("Assertion failed: %s at %s, line %d\n", msg, file, line);
    stop();
}


bool about_equal(double a, double b)
{
    return a / b > 0.999999 && a / b < 1.000001;
}

// this is a handler for incoming messages. It simply sends a message
// back to one of the client addresses
//
void server_test(o2l_msg_ptr msg, const char *types, void *data, void *info)
{
    int got_i = o2l_get_int32();

    msg_count++;
    o2l_send_start(client_addresses[msg_count % n_addrs], 0, "i", use_tcp);
    o2l_put_int32(msg_count);
    o2l_send();

    if (msg_count % 10000 == 0) {
        printf("server received %d messages\n", msg_count);
    }
    if (msg_count < 100) {
        printf("server message %d is %d\n", msg_count, got_i);
    }
    if (got_i == -1) {
        running = false;
    } else {
        mustbe(msg_count == got_i);
    }
}


bool sift_called = false;

// handles types "ist"
void sift_han(o2l_msg_ptr msg, const char *types, void *data, void *info)
{
    printf("sift_han called\n");
    mustbe(info = (void *) 111);
    mustbe(streql(o2l_get_string(), "this is a test"));
    mustbe(o2l_get_int32() == 1234);
    mustbe(about_equal(o2l_get_float(), 123.4));
    mustbe(about_equal(o2l_get_time(), 567.89));
    sift_called = true;
}


void setup() {
    Serial.begin(115200);
    printf("This is o2liteserv\n");
    connect_to_wifi(HOSTNAME, NETWORK_NAME, NETWORK_PSWD);
    o2l_initialize("test");
    o2l_set_services("sift");
    o2l_method_new("/sift", "sift", true, &sift_han, (void *) 111);
}


int test_state = 0;
o2l_time start_wait = 0;

void loop() {
    if (millis() % 10000 == 0) {
        printf("test_state is %d\n", test_state);
    }
    o2l_poll();
    switch (test_state) {
      case 0:
        if (o2l_bridge_id >= 0) {
            printf("main detected o2lite connected\n");

            o2l_send_start("/sift", 0, "sift", true);
            o2l_put_string("this is a test");
            o2l_put_int32(1234);
            o2l_put_float(123.4);
            o2l_put_time(567.89);
            o2l_send();
            test_state = 1;
        }
        break;
      case 1:
        if (o2l_time_get() > 0) { // not synchronized
            printf("main detected o2lite clock sync\n");
            start_wait = o2l_time_get();
            test_state = 2;
        }            
        break;
      case 2:
        if (start_wait + 1 < o2l_time_get()) {
            printf("main timed out waiting for loop-back message\n");
            test_state = 3;
        }
        // no break, fall through, might as well keep waiting
      case 3:
        if (sift_called) {
            printf("main received loop-back message\n");
            // now create addresses and handlers to receive server messages
            client_addresses = (char **) malloc(sizeof(char **) * n_addrs);
            server_addresses = (char **) malloc(sizeof(char **) * n_addrs);
            for (int i = 0; i < n_addrs; i++) {
                char path[100];
                sprintf(path, "!client/benchmark/%d", i);
                client_addresses[i] = (char *) (malloc(strlen(path)));
                strcpy(client_addresses[i], path);

                sprintf(path, "/server/benchmark/%d", i);
                server_addresses[i] = (char *) (malloc(strlen(path)));
                strcpy(server_addresses[i], path);
                o2l_method_new(server_addresses[i], "i", true, 
                               &server_test, NULL);
                // we are ready for the client, so announce the server services
                o2l_set_services("sift,server");
            }
            test_state = 4;
        }
        break;
      case 4:
        if (!running) {
            printf("o2liteserv DONE\n");
            test_state = 5;
            stop();
        }
        break;
      default:
        break; // nothing to do here
    }
}
