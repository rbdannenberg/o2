//  patterntest.c -- test use of address patterns
//

#include <stdio.h>
#include "o2.h"
#include "assert.h"
#include "string.h"

int message_count = 0;
int expected = 0;


void handler(o2_msg_data_ptr data, const char *types,
             o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    assert(strcmp(types, "i") == 0);
    o2_arg_ptr arg = o2_get_next(O2_INT32);
    printf("%s: types=%s int32=%d\n", (const char *) user_data, types, arg->i);
    assert(arg->i == expected);
    message_count++;
}


void send_the_message(int count)
{
    for (int i = 0; i < 5; i++) {
        o2_poll();
    }
    printf("    send_the_message expects count %d to be %d\n",
           message_count, count);
    assert(message_count == count);
    message_count = 0;
}


int main(int argc, const char * argv[])
{
    printf("Usage: patterntest [debugflags] "
           "(see o2.h for flags, use a for all)\n");
    if (argc >= 2) {
        o2_debug_flags(argv[1]);
        printf("debug flags are: %s\n", argv[1]);
    }
    if (argc > 2) {
        printf("WARNING: ignoring command line beyond debugflags\n");
    }

    o2_initialize("test");    
    o2_service_new("one");
    o2_service_new("two");
    o2_service_new("three");
    o2_method_new("/one/service1", "i", &handler,
                  "/one/service1", false, true);
    o2_method_new("/one/service2", "i", &handler,
                  "/one/service2", false, true);
    o2_method_new("/two/two-odd!x", "i", &handler,
                  "/two/two-odd!x", false, true);
    o2_method_new("/two/two-odd!y", "i", &handler,
                  "/two/two-odd!y", false, true);
    o2_method_new("/three/abc/def/ghi", "i", &handler,
                  "/three/abc/def/ghi", false, true);
    o2_method_new("/three/abc/def/ghj", "i", &handler,
                  "/three/abc/def/ghj", false, true);
    o2_method_new("/three/abc/def/ghk", "i", &handler,
                  "/three/abc/def/ghk", false, true);
    o2_method_new("/three/abc/def/nopqr", "i", &handler,
                  "/three/abc/def/nopqr", false, true);
    o2_method_new("/three/abc/xef/nopqr", "i", &handler,
                  "/three/abc/xef/nopqr", false, true);

    expected = 1;
    o2_send("!one/service1", 0, "i", 1);
    send_the_message(1);
    expected = 2;
    o2_send("/one/*", 0, "i", 2);
    send_the_message(2);
    expected = 3;
    o2_send("/one/serv*", 0, "i", 3);
    send_the_message(2);
    expected = 4;
    o2_send("/one/*ice2", 0, "i", 4);
    send_the_message(1);
    expected = 5;
    o2_send("/one/*service1*", 0, "i", 5);
    send_the_message(1);
    expected = 6;
    o2_send("/three/abc/def/gh?", 0, "i", 6);
    send_the_message(3);
    expected = 7;
    o2_send("/one/ser?????", 0, "i", 7);
    send_the_message(2);
    expected = 8;
    o2_send("/one/ser?ice1", 0, "i", 8);
    send_the_message(1);
    expected = 9;
    o2_send("/one/s?r?i?e?", 0, "i", 9);
    send_the_message(2);
    expected = 10;
    o2_send("/three/abc/def/gh?", 0, "i", 10);
    send_the_message(3);
    expected = 11;
    o2_send("/three/abc/def/*", 0, "i", 11);
    send_the_message(4);
    expected = 12;
    o2_send("/three/abc/def/gh[i-j]", 0, "i", 12);
    send_the_message(2);
    expected = 13;
    o2_send("/three/abc/def/[a-z]h[i-j]", 0, "i", 13);
    send_the_message(2);
    expected = 14;
    o2_send("/two/two[a-z1-9-]*x", 0, "i", 14);
    send_the_message(1);
    expected = 15;
    o2_send("/two/two-odd!x", 0, "i", 15);
    send_the_message(1);
    expected = 16;
    o2_send("/two/two-odd[ab!]?", 0, "i", 16);
    send_the_message(2);
    expected = 17;
    o2_send("/two/two-odd[ab!-$]*", 0, "i", 17);
    send_the_message(2);
    expected = 18;
    o2_send("/three/abc/def/gh[ij]", 0, "i", 18);
    send_the_message(2);
    expected = 19;
    o2_send("/two/two[!a-z]odd*", 0, "i", 19);
    send_the_message(2);
    expected = 20;
    o2_send("/three/abc/def/gh[!j-k]", 0, "i", 20);
    send_the_message(1);
    expected = 21;
    o2_send("/three/abc/def/gh[!ik]", 0, "i", 21);
    send_the_message(1);
    expected = 22;
    o2_send("/two/two-odd[!a-z][!x]", 0, "i", 22);
    send_the_message(1);
    expected = 23;
    o2_send("/two/two[!a-z][!!]dd!?", 0, "i", 23);
    send_the_message(2);
    expected = 24;
    o2_send("/one/{service,aaa,bbb}1", 0, "i", 24);
    send_the_message(1);
    expected = 25;
    o2_send("/three/abc/{ghi,jk,def}/{ghi,ghk}", 0, "i", 25);
    send_the_message(2);
    expected = 26;
    o2_send("/three/a{aa,bb,bc}/def/ghj", 0, "i", 26);
    send_the_message(1);
    expected = 27;
    o2_send("/three/abc/?ef/nopqr", 0, "i", 27);
    send_the_message(2);
    printf("DONE\n");
    o2_finish();
    return 0;
}
