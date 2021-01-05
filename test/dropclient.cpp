//  dropclient.c - test drop warning
//
//  This program works with dropserver.c See that for test description.

#include "o2usleep.h"
#include "stdio.h"
#include "string.h"
#include "assert.h"
#include "o2.h"

int msg_count = 0;

void pollsome()
{
    for (int i = 0; i < 10; i++) {
        o2_poll();
        usleep(2000);
    }
}

#define streql(a, b) (strcmp(a, b) == 0)
const char *expected_warning = "";
int warning_count = 0;

static void drop_warning(const char *warn, o2_msg_data_ptr msg)
{
    assert(streql(warn, "dropping message because no handler was found"));
    printf("drop_warning: got \"%s\"\n", warn);
    warning_count++;
    printf("warning_count %d\n", warning_count);
}




// this is a handler for incoming messages
//
void bye(o2_msg_data_ptr msg, const char *types,
         O2arg_ptr *argv, int argc, const void *user_data)
{
    assert(argc == 1);
    msg_count++;
    printf("bye handler msg_count %d i %d\n", msg_count, argv[0]->i32);
}


int main(int argc, const char *argv[])
{
    printf("Usage: dropclient [debugflags]\n"
           "    see o2.h for flags, use a for all, - for none\n");
    if (argc >= 2) {
        if (argv[1][0] != '-') {
            o2_debug_flags(argv[1]);
            printf("debug flags are: %s\n", argv[1]);
        }
    }
    if (argc > 2) {
        printf("WARNING: dropclient ignoring extra command line argments\n");
    }

    o2_initialize("test");
    o2_message_warnings(drop_warning);
    o2_service_new("dropclient");
    //                                                   coerce parse
    o2_method_new("/dropclient/bye",    "i", &bye, NULL, false, true);
    
    // wait for server service to be discovered
    while (o2_status("dropserver") < O2_REMOTE) {
        o2_poll();
        usleep(2000); // 2ms
    }
    
    printf("We discovered the dropserver at time %g.\n", o2_local_time());
    
    while (msg_count < 1) pollsome();
    assert(warning_count == 1);
    for (int i = 0; i < 25; i++) pollsome();

    o2_finish();
    printf("DROPCLIENT DONE\n");
    return 0;
}
