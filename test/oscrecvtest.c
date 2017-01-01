//  oscrecvtest.c - test o2_create_osc_port()
//
//  this test is designed to run with oscrecvtest.c

#include "o2.h"
#include "stdio.h"
#include "string.h"
#include "assert.h"

#ifdef WIN32
#include <windows.h> 
#else
#include <unistd.h>
#endif

int message_count = 0;


void osc_i_handler(o2_msg_data_ptr data, const char *types,
                   o2_arg_ptr *argv, int argc, void *user_data)
{
    assert(argv);
    assert(argc == 1);
    assert(argv[0]->i == 1234);
    message_count++;
    printf("osc_i_handler receieved 1234 at /osc/i\n");
}


int main(int argc, const char * argv[]) {
    o2_initialize("test");
    assert(o2_create_osc_port("osc", 8100, FALSE) == O2_SUCCESS);
    
    o2_add_method("/osc/i", "i", osc_i_handler, NULL, FALSE, TRUE);
    while (message_count < 10) {
        o2_poll();
        usleep(10000); // 10ms
    }
    o2_finish();
    printf("OSCRECV DONE\n");
    return 0;
}
