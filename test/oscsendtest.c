//  oscsendtest.c - test o2_delegate_to_osc()
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


int main(int argc, const char * argv[]) {
    o2_initialize("test");
    assert(o2_delegate_to_osc("osc", "localhost", 8100, FALSE) == O2_SUCCESS);
    
    // send 20 messages, 1 every 2s, and stop
    for (int n = 0; n < 20; n++) {
        o2_send("/osc/i", 0, "i", 1234);
        printf("sent 1234 to /osc/i\n");
        // pause for 2s, but keep running O2 by polling
        for (int i = 0; i < 1000; i++) {
            o2_poll();
            usleep(2000); // 2ms
        }
    }
    
    o2_finish();
    return 0;
}
