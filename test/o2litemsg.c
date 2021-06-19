// o2litemsg.c -- simple test of message create and dispatch for o2lite
//
// Roger B. Dannenberg
// July 2020

/* 
This test:

- create a handler
- create a message with all parameter types
- copy message to incoming buffer
- dispatch message
- test different ways to match messages:
  + full message match is tested above
  + test full match handler reject partial match
  + test partial match up to "/"
  + test for non-match when string matches but not up to "/"
  + test for priority: message matches the last handler created

*/

#include <stdio.h>
#include <assert.h>
#include "o2lite.h"
#include <string.h>

// these are duplicate declarations to access "private" stuff in o2lite.c:
extern char tcpinbuf[MAX_MSG_LEN];
extern char outbuf[MAX_MSG_LEN];
extern o2l_msg_ptr out_msg;
extern int out_msg_cnt;
void o2l_dispatch(o2l_msg_ptr msg);

bool about_equal(double a, double b)
{
    return a / b > 0.999999 && a / b < 1.000001;
}


void deliver(bool tcp)
{
    out_msg->length = o2lswap32(out_msg_cnt);
    memcpy(tcpinbuf, outbuf, MAX_MSG_LEN);
    o2l_dispatch((o2l_msg_ptr) tcpinbuf);
}


bool abcde_called = false;

// handles types "ist"
void abcde_han(o2l_msg_ptr msg, const char *types, void *data, void *info)
{
    assert(o2l_get_int32() == 1234);
    assert(streql(o2l_get_string(), "this is a test"));
    o2l_time t = (o2l_time) o2l_get_time();
    assert(about_equal(t, 567.89));
    abcde_called = true;
}


bool abcde2_called = false;
// handles types "ist"
void abcde2_han(o2l_msg_ptr msg, const char *types, void *data, void *info)
{
    assert(o2l_get_int32() == 4567);
    assert(streql(o2l_get_string(), "this is a test"));
    o2l_time t = (o2l_time) o2l_get_time();
    assert(about_equal(t, 4567.89));
    abcde2_called = true;
}


bool xyzrst_called = false;
// handles types "iii"
void xyzrst_han(o2l_msg_ptr msg, const char *types, void *data, void *info)
{
    assert(streql(types, "iii"));
    assert(o2l_get_int32() == 1234);
    assert(o2l_get_int32() == 5678);
    assert(o2l_get_int32() == 9012);
    xyzrst_called = true;
}


bool xyzrs_called = false;
// handles types "iii"
void xyzrs_han(o2l_msg_ptr msg, const char *types, void *data, void *info)
{
    assert(streql(types, "iii"));
    assert(o2l_get_int32() == 9012);
    assert(o2l_get_int32() == 5678);
    assert(o2l_get_int32() == 1234);
    xyzrs_called = true;
}


bool any_called = false;
// handles types "sift"
void any_han(o2l_msg_ptr msg, const char *types, void *data, void *info)
{
    assert(streql(types, "sift"));
    assert(streql(o2l_get_string(), "this is another string"));
    assert(o2l_get_int32() == 5678);
    assert(about_equal(o2l_get_float(), 9.012));
    assert(about_equal(o2l_get_time(), 34567.89));
    any_called = true;
}


bool noargs_called = false;
// handles types ""
void noargs_han(o2l_msg_ptr msg, const char *types, void *data, void *info)
{
    assert(streql(types, ""));
    noargs_called = true;
}


// should not call this ever
void xyz_han(o2l_msg_ptr msg, const char *types, void *data, void *info)
{
    assert(false);
}


int main(int argc, const char * argv[])
{
    if (argc > 1) {
        printf("WARNING: o2litemsg ignoring extra command line argments\n");
    }

    o2l_initialize("test");

    // put some handlers at the end of the list
    o2l_method_new("/noargs", "", true, &noargs_han, (void *) 111);
    o2l_method_new("/any", NULL, true, &any_han, NULL);

    // test full match handler
    o2l_method_new("/abcde", "ist", true, &abcde_han, NULL);

    o2l_send_start("/abcde", 0, "ist", true);
    o2l_add_int32(1234);
    o2l_add_string("this is a test");
    o2l_add_time(567.89);
    deliver(true);
    assert(abcde_called);

    // test full match handler reject partial match

    o2l_method_new("/xyz/rst", "iii", true, &xyzrst_han, NULL);
    // later methods are searched first. This will partial match, but
    // true means only call handler on a full match:
    o2l_method_new("/xyz", "ist", true, &xyz_han, NULL);

    o2l_send_start("/xyz/rst", 0, "iii", true);
    o2l_add_int32(1234);
    o2l_add_int32(5678);
    o2l_add_int32(9012);
    deliver(true);
    assert(xyzrst_called);
    xyzrst_called = false;

    // test for non-match when string matches but not up to "/"
    // search this first. Partial match is ok, but /xyz/rs is not allowed
    // to match /xyz/rst, so snd should go to a different handler
    o2l_method_new("/xyz/rs", "iii", false, &xyzrs_han, NULL);
    o2l_send_start("/xyz/rst", 0, "iii", false);
    o2l_add_int32(1234);
    o2l_add_int32(5678);
    o2l_add_int32(9012);
    deliver(true);
    assert(xyzrst_called);
    xyzrst_called = false;

    // test partial match up to "/": this will match /xyz/rs handler
    o2l_send_start("/xyz/rs/tuv", 0, "iii", false);
    o2l_add_int32(9012);
    o2l_add_int32(5678);
    o2l_add_int32(1234);
    deliver(true);
    assert(xyzrs_called);
    xyzrs_called = false;

    // test for priority: message matches the last handler created
    // create a newer handler for /abcde
    o2l_method_new("/abcde", "ist", false, &abcde2_han, NULL);
    o2l_send_start("/abcde", 0, "ist", true);
    o2l_add_int32(4567);
    o2l_add_string("this is a test");
    o2l_add_time(4567.89);
    deliver(true);
    assert(abcde2_called);

    // test for empty types
    o2l_send_start("/noargs", 0, "", true);
    deliver(true);
    assert(noargs_called);

    // test for NULL types
    o2l_send_start("/any", 0, "sift", true);
    o2l_add_string("this is another string");
    o2l_add_int32(5678);
    o2l_add_float(9.012F);
    o2l_add_time(34567.89);
    deliver(true);
    assert(any_called);

    printf("o2litemsg\nDONE\n");
}
