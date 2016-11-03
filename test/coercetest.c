// coercetest.c -- test coercion of O2 parameters
//
// send from all coercible types: i, h, f, d, t to 
// handlers that ask for i, h, f, d, t, B, T, F

#include <stdio.h>
#include "o2.h"
#include "assert.h"
#include "string.h"


int got_the_message = FALSE;

// we do not declare a different handler for each send type, but
// we check that the message has the expected type string. To
// enable the test, we put the sender's type string in this
// global:
//
char *send_types = "";

// receive float as int
int service_i(const o2_message_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(data);
    assert(strcmp(types, send_types) == 0);
    o2_arg_ptr arg = o2_get_next('i');
    assert(arg);
    printf("service_i types=%s int=%d\n", types, arg->i);
    assert(arg->i == 12345);
    got_the_message = TRUE;
    return O2_SUCCESS;
}


// receive int as bool
int service_B(const o2_message_ptr data, const char *types,
              o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(data);
    assert(strcmp(types, send_types) == 0);
    o2_arg_ptr arg = o2_get_next('B');
    assert(arg);
    printf("service_B types=%s bool=%d\n", types, arg->B);
    assert(arg->B == TRUE);
    got_the_message = TRUE;
    return O2_SUCCESS;
}


// receive int32 as int64
int service_h(const o2_message_ptr data, const char *types,
              o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(data);
    assert(strcmp(types, send_types) == 0);
    o2_arg_ptr arg = o2_get_next('h');
    assert(arg);
    printf("service_h types=%s int64=%lld\n", types, arg->h);
    assert(arg->h == 12345LL);
    got_the_message = TRUE;
    return O2_SUCCESS;
}


// receive int32 as float
int service_f(const o2_message_ptr data, const char *types,
              o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(data);
    assert(strcmp(types, send_types) == 0);
    o2_arg_ptr arg = o2_get_next('f');
    assert(arg);
    printf("service_f types=%s float=%g\n", types, arg->f);
    assert(arg->f == 1234.0);
    got_the_message = TRUE;
    return O2_SUCCESS;
}


// receive int32 as double
int service_d(const o2_message_ptr data, const char *types,
              o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(data);
    assert(strcmp(types, send_types) == 0);
    o2_arg_ptr arg = o2_get_next('d');
    assert(arg);
    printf("service_d types=%s double=%g\n", types, arg->d);
    assert(arg->d == 1234.0);
    got_the_message = TRUE;
    return O2_SUCCESS;
}


// receive int32 as time
int service_t(const o2_message_ptr data, const char *types,
              o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(data);
    assert(strcmp(types, send_types) == 0);
    o2_arg_ptr arg = o2_get_next('t');
    assert(arg);
    printf("service_t types=%s time=%g\n", types, arg->t);
    assert(arg->t == 1234.0);
    got_the_message = TRUE;
    return O2_SUCCESS;
}


// receive int32 as TRUE
int service_T(const o2_message_ptr data, const char *types,
              o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(data);
    assert(strcmp(types, send_types) == 0);
    o2_arg_ptr arg = o2_get_next('T');
    printf("service_T types=%s\n", types);
    assert(arg);
    got_the_message = TRUE;
    return O2_SUCCESS;
}


// receive int32 as FALSE
int service_F(const o2_message_ptr data, const char *types,
              o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(data);
    assert(strcmp(types, send_types) == 0);
    o2_arg_ptr arg = o2_get_next('F');
    assert(arg);
    printf("service_F types=%s\n", types);
    got_the_message = TRUE;
    return O2_SUCCESS;
}


// expects hifdt, but receives them as ihdff
//
int service_many(const o2_message_ptr data, const char *types,
    o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(data);
    o2_arg_ptr arg = o2_get_next('i');
    assert(arg->i == 12345);
    arg = o2_get_next('h');
    assert(arg->h == 1234LL);
    arg = o2_get_next('d');
    // note that we must convert the double back to a float
    // and compare to a float, because if you assign 123.456
    // to a float, the stored value is approximately 123.45600128173828
    assert(((float) arg->d) == 123.456F);
    arg = o2_get_next('f');
    assert(arg->f == 123.456F);
    arg = o2_get_next('f');
    assert(arg->f == 123.456F);
    assert(strcmp(types, "hifdt") == 0);
    printf("service_N types=%s\n", types);
    got_the_message = TRUE;
    return O2_SUCCESS;
}


void send_the_message()
{
    while (!got_the_message) {
        o2_poll();
    }
    got_the_message = FALSE;
}


int main(int argc, const char * argv[])
{
    o2_initialize("test");    
    o2_add_service("one");

    char address[32];
    for (int i = 0; i < 5; i++) {
        char send_type = ("ihfdt")[i];
        char send_types[4];
        send_types[0] = send_type;
        send_types[1] = 0;
        snprintf(address, 32, "/one/%ci", send_type);
        o2_add_method(address, send_types, &service_i, NULL, FALSE, FALSE);
        snprintf(address, 32, "/one/%cB", send_type);
        o2_add_method(address, send_types, service_B, NULL, FALSE, FALSE);
        snprintf(address, 32, "/one/%ch", send_type);
        o2_add_method(address, send_types, service_h, NULL, FALSE, FALSE);
        snprintf(address, 32, "/one/%cf", send_type);
        o2_add_method(address, send_types, service_f, NULL, FALSE, FALSE);
        snprintf(address, 32, "/one/%cd", send_type);
        o2_add_method(address, send_types, service_d, NULL, FALSE, FALSE);
        snprintf(address, 32, "/one/%ct", send_type);
        o2_add_method(address, send_types, service_t, NULL, FALSE, FALSE);
        snprintf(address, 32, "/one/%cT", send_type);
        o2_add_method(address, send_types, service_T, NULL, FALSE, FALSE);
        snprintf(address, 32, "/one/%cF", send_type);
        o2_add_method(address, send_types, service_F, NULL, FALSE, FALSE);
    } 
    o2_add_method("/one/many", "hifdt", &service_many, NULL, FALSE, FALSE);

    o2_send("/one/many", 0, "hifdt", 12345LL, 1234,
            123.456, 123.456, 123.456);

    send_types = "i";
    o2_send("/one/ii", 0, "i", 12345);
    send_the_message();
    o2_send("/one/iB", 0, "i", 1234);
    send_the_message();
    o2_send("/one/ih", 0, "i", 12345);
    send_the_message();
    o2_send("/one/if", 0, "i", 1234);
    send_the_message();
    o2_send("/one/id", 0, "i", 1234);
    send_the_message();
    o2_send("/one/it", 0, "i", 1234);
    send_the_message();
    o2_send("/one/iT", 0, "i", 1111);
    send_the_message();
    o2_send("/one/iF", 0, "i", 0);
    send_the_message();

    send_types = "h";
    o2_send("/one/hi", 0, "h", 12345LL);
    send_the_message();
    o2_send("/one/hB", 0, "h", 12345LL);
    send_the_message();
    o2_send("/one/hh", 0, "h", 12345LL);
    send_the_message();
    o2_send("/one/hf", 0, "h", 1234LL);
    send_the_message();
    o2_send("/one/hd", 0, "h", 1234LL);
    send_the_message();
    o2_send("/one/ht", 0, "h", 1234LL);
    send_the_message();
    o2_send("/one/hT", 0, "h", 1111LL);
    send_the_message();
    o2_send("/one/hF", 0, "h", 0LL);
    send_the_message();

    send_types = "f";
    o2_send("/one/fi", 0, "f", 12345.0);
    send_the_message();
    o2_send("/one/fB", 0, "f", 1234.0);
    send_the_message();
    o2_send("/one/fh", 0, "f", 12345.0);
    send_the_message();
    o2_send("/one/ff", 0, "f", 1234.0);
    send_the_message();
    o2_send("/one/fd", 0, "f", 1234.0);
    send_the_message();
    o2_send("/one/ft", 0, "f", 1234.0);
    send_the_message();
    o2_send("/one/fT", 0, "f", 1111.0);
    send_the_message();
    o2_send("/one/fF", 0, "f", 0.0);
    send_the_message();

    send_types = "d";
    o2_send("/one/di", 0, "d", 12345.0);
    send_the_message();
    o2_send("/one/dB", 0, "d", 1234.0);
    send_the_message();
    o2_send("/one/dh", 0, "d", 12345.0);
    send_the_message();
    o2_send("/one/df", 0, "d", 1234.0);
    send_the_message();
    o2_send("/one/dd", 0, "d", 1234.0);
    send_the_message();
    o2_send("/one/dt", 0, "d", 1234.0);
    send_the_message();
    o2_send("/one/dT", 0, "d", 1111.0);
    send_the_message();
    o2_send("/one/dF", 0, "d", 0.0);
    send_the_message();

    send_types = "t";
    o2_send("/one/ti", 0, "t", 12345.0);
    send_the_message();
    o2_send("/one/tB", 0, "t", 1234.0);
    send_the_message();
    o2_send("/one/th", 0, "t", 12345.0);
    send_the_message();
    o2_send("/one/tf", 0, "t", 1234.0);
    send_the_message();
    o2_send("/one/td", 0, "t", 1234.0);
    send_the_message();
    o2_send("/one/tt", 0, "t", 1234.0);
    send_the_message();
    o2_send("/one/tT", 0, "t", 1111.0);
    send_the_message();
    o2_send("/one/tF", 0, "t", 0.0);
    send_the_message();

    printf("DONE\n");
    o2_finish();
    return 0;
}
