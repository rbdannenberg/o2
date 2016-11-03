//  coercetest.c -- test coercion of O2 parameters
//

#include <stdio.h>
#include "o2.h"
#include "assert.h"
#include "string.h"


int got_the_message = FALSE;

o2_blob_ptr a_blob;
char a_midi_msg[4];

// receive float as int
int service_i(const o2_message_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(data);
    assert(strcmp(types, "f") == 0);
    o2_arg_ptr arg = o2_get_next('i');
    assert(arg);
    assert(arg->i == 1234);
    printf("service_i types=%s int=%d\n", types, arg->i);
    got_the_message = TRUE;
    return O2_SUCCESS;
}


// receive int as bool
int service_B(const o2_message_ptr data, const char *types,
              o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(data);
    assert(strcmp(types, "i") == 0);
    o2_arg_ptr arg = o2_get_next('B');
    assert(arg);
    assert(arg->B == TRUE);
    printf("service_B types=%s bool=%d\n", types, arg->B);
    got_the_message = TRUE;
    return O2_SUCCESS;
}


// receive int32 as int64
int service_h(const o2_message_ptr data, const char *types,
              o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(data);
    assert(strcmp(types, "i") == 0);
    o2_arg_ptr arg = o2_get_next('h');
    assert(arg);
    assert(arg->h == 12345);
    printf("service_h types=%s int64=%lld\n", types, arg->h);
    got_the_message = TRUE;
    return O2_SUCCESS;
}


// receive int32 as float
int service_f(const o2_message_ptr data, const char *types,
              o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(data);
    assert(strcmp(types, "i") == 0);
    o2_arg_ptr arg = o2_get_next('f');
    assert(arg);
    assert(arg->f == 1234.0);
    printf("service_f types=%s float=%g\n", types, arg->f);
    got_the_message = TRUE;
    return O2_SUCCESS;
}


// receive int32 as double
int service_d(const o2_message_ptr data, const char *types,
              o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(data);
    assert(strcmp(types, "i") == 0);
    o2_arg_ptr arg = o2_get_next('d');
    assert(arg);
    assert(arg->d == 1234.0);
    printf("service_d types=%s double=%g\n", types, arg->d);
    got_the_message = TRUE;
    return O2_SUCCESS;
}


// receive int32 as time
int service_t(const o2_message_ptr data, const char *types,
              o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(data);
    assert(strcmp(types, "i") == 0);
    o2_arg_ptr arg = o2_get_next('t');
    assert(arg);
    assert(arg->t == 1234.0);
    printf("service_t types=%s time=%g\n", types, arg->t);
    got_the_message = TRUE;
    return O2_SUCCESS;
}


// receive int32 as TRUE
int service_T(const o2_message_ptr data, const char *types,
              o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(data);
    assert(strcmp(types, "i") == 0);
    o2_arg_ptr arg = o2_get_next('t');
    assert(arg);
    printf("service_T types=%s\n", types);
    got_the_message = TRUE;
    return O2_SUCCESS;
}


// receive int32 as FALSE
int service_F(const o2_message_ptr data, const char *types,
              o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(data);
    assert(strcmp(types, "i") == 0);
    o2_arg_ptr arg = o2_get_next('F');
    assert(arg);
    printf("service_F types=%s\n", types);
    got_the_message = TRUE;
    return O2_SUCCESS;
}


// TO DO: NEED TO ADD TESTS WHERE WE SEND int_64, float, double and time
// types and receive them in all possible forms


void send_the_message()
{
    while (!got_the_message) {
        o2_poll();
    }
    got_the_message = FALSE;
}


int main(int argc, const char * argv[])
{
    a_blob = malloc(20);
    a_blob->size = 15;
    memcpy(a_blob->data, "This is a blob", 15);

    a_midi_msg[0] = 0x90;
    a_midi_msg[1] = 60;
    a_midi_msg[2] = 100;
    a_midi_msg[3] = 0;

    o2_initialize("test");    
    o2_add_service("one");
    o2_add_method("/one/i", "i", &service_i, NULL, FALSE, FALSE);
    o2_add_method("/one/B", "B", &service_B, NULL, FALSE, FALSE);
    o2_add_method("/one/h", "h", &service_h, NULL, FALSE, FALSE);
    o2_add_method("/one/f", "f", &service_f, NULL, FALSE, FALSE);
    o2_add_method("/one/d", "d", &service_d, NULL, FALSE, FALSE);
    o2_add_method("/one/t", "t", &service_t, NULL, FALSE, FALSE);
    o2_add_method("/one/T", "T", &service_T, NULL, FALSE, FALSE);
    o2_add_method("/one/F", "F", &service_F, NULL, FALSE, FALSE);

    o2_send("/one/i", 0, "i", 1234);
    send_the_message();
    o2_send("/one/B", 0, "i", 1234);
    send_the_message();
    o2_send("/one/h", 0, "i", 12345);
    send_the_message();
    o2_send("/one/f", 0, "i", 1234);
    send_the_message();
    o2_send("/one/d", 0, "i", 1234);
    send_the_message();
    o2_send("/one/t", 0, "i", 1234);
    send_the_message();
    o2_send("/one/T", 0, "i", 1111);
    send_the_message();
    o2_send("/one/F", 0, "i", 0);
    send_the_message();
    printf("DONE\n");
    o2_finish();
    return 0;
}
