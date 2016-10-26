//  coercetest.c -- test coercion of O2 parameters
//	You can change the send string in line 17. when "current_send"
//	is 0, 1, 2, 3, 4, it will send "i", "h", "f", "d", and "t", 
//	respectively.

#include <stdio.h>
#include "o2.h"
#include "assert.h"
#include "string.h"


int got_the_message = FALSE;

o2_blob_ptr a_blob;
char a_midi_msg[4];
char* send_char[5] = {"i","h","f","d","t"};
int current_send = 1;//change this to change the sended string 

// receive float as int
int service_i(const o2_message_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(data);
    assert(strcmp(types, send_char[current_send]) == 0);
    o2_arg_ptr arg = o2_get_next('i');
    assert(arg);
    if(arg->i != 12345)
        printf("error:send:%s    service_i  arg->i=%d\n", send_char[current_send], arg->i);
    //assert(arg->i == 1234);
    printf("service_i types=%s int=%d\n", types, arg->i);
    got_the_message = TRUE;
    return O2_SUCCESS;
}


// receive int as bool
int service_B(const o2_message_ptr data, const char *types,
              o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(data);
    assert(strcmp(types, send_char[current_send]) == 0);
    o2_arg_ptr arg = o2_get_next('B');
    assert(arg);
    if(arg->B != TRUE)
        printf("error:send:%s    service_B  arg->B=%d\n", send_char[current_send], arg->B);
    //assert(arg->B == TRUE);
    printf("service_B types=%s bool=%d\n", types, arg->B);
    got_the_message = TRUE;
    return O2_SUCCESS;
}


// receive int32 as int64
int service_h(const o2_message_ptr data, const char *types,
              o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(data);
    assert(strcmp(types, send_char[current_send]) == 0);
    o2_arg_ptr arg = o2_get_next('h');
    assert(arg);
    if(arg->h != 12345LL)
        printf("error:send:%s    service_h  arg->h=%d\n", send_char[current_send], arg->h);
    //assert(arg->h == 12345);
    printf("service_h types=%s int64=%ld\n", types, arg->h);
    got_the_message = TRUE;
    return O2_SUCCESS;
}


// receive int32 as float
int service_f(const o2_message_ptr data, const char *types,
              o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(data);
    assert(strcmp(types, send_char[current_send]) == 0);
    o2_arg_ptr arg = o2_get_next('f');
    assert(arg);
    if(arg->f != 1234.0)
        printf("error:send:%s    service_f  arg->i=%g\n", send_char[current_send], arg->f);
    //assert(arg->f == 1234.0);
    printf("service_f types=%s float=%g\n", types, arg->f);
    got_the_message = TRUE;
    return O2_SUCCESS;
}


// receive int32 as double
int service_d(const o2_message_ptr data, const char *types,
              o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(data);
    assert(strcmp(types, send_char[current_send]) == 0);
    o2_arg_ptr arg = o2_get_next('d');
    assert(arg);
    if(arg->d != 1234)
        printf("error:send:%s    service_d  arg->d=%g\n", send_char[current_send], arg->d);
    //assert(arg->d == 1234.0);
    printf("service_d types=%s double=%g\n", types, arg->d);
    got_the_message = TRUE;
    return O2_SUCCESS;
}


// receive int32 as time
int service_t(const o2_message_ptr data, const char *types,
              o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(data);
    assert(strcmp(types, send_char[current_send]) == 0);
    o2_arg_ptr arg = o2_get_next('t');
    assert(arg);
    if(arg->t != 1234.0)
        printf("error:send:%s    service_t  arg->t=%g\n", send_char[current_send], arg->t);
    //assert(arg->t == 1234.0);
    printf("service_t types=%s time=%g\n", types, arg->t);
    got_the_message = TRUE;
    return O2_SUCCESS;
}


// receive int32 as TRUE
int service_T(const o2_message_ptr data, const char *types,
              o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(data);
    assert(strcmp(types, send_char[current_send]) == 0);
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
    assert(strcmp(types, send_char[current_send]) == 0);
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
	int i = current_send;

	//for (int i = 0; i<2; i++)
    //{
        o2_add_method("/one/i", send_char[i], &service_i, NULL, FALSE, FALSE);
        o2_add_method("/one/B", send_char[i], &service_B, NULL, FALSE, FALSE);
        o2_add_method("/one/h", send_char[i], &service_h, NULL, FALSE, FALSE);
        o2_add_method("/one/f", send_char[i], &service_f, NULL, FALSE, FALSE);
        o2_add_method("/one/d", send_char[i], &service_d, NULL, FALSE, FALSE);
        o2_add_method("/one/t", send_char[i], &service_t, NULL, FALSE, FALSE);
        o2_add_method("/one/T", send_char[i], &service_T, NULL, FALSE, FALSE);
        o2_add_method("/one/F", send_char[i], &service_F, NULL, FALSE, FALSE);
    //}   

	if (current_send == 0)
	{ 
        o2_send("/one/i", 0, send_char[current_send], 1234);
        send_the_message();
        o2_send("/one/B", 0, send_char[current_send], 1234);
        send_the_message();
		o2_send("/one/h", 0, send_char[current_send], 12345);
        send_the_message();
        o2_send("/one/f", 0, send_char[current_send], 1234);
        send_the_message();
        o2_send("/one/d", 0, send_char[current_send], 1234);
        send_the_message();
        o2_send("/one/t", 0, send_char[current_send], 1234);
        send_the_message();
        o2_send("/one/T", 0, send_char[current_send], 1111);
        send_the_message();
        o2_send("/one/F", 0, send_char[current_send], 0);
        send_the_message();
	} else if (current_send == 1) {	
		o2_send("/one/i", 0, send_char[current_send], 12345LL);
		send_the_message();
		o2_send("/one/B", 0, send_char[current_send], 12345LL);
		send_the_message();
		o2_send("/one/h", 0, send_char[current_send], 12345LL);
		send_the_message();
		o2_send("/one/f", 0, send_char[current_send], 1234LL);
		send_the_message();
		o2_send("/one/d", 0, send_char[current_send], 1234LL);
		send_the_message();
		o2_send("/one/t", 0, send_char[current_send], 1234LL);
		send_the_message();
		o2_send("/one/T", 0, send_char[current_send], 1111LL);
		send_the_message();
		o2_send("/one/F", 0, send_char[current_send], 0LL);
		send_the_message();
	} else if (current_send == 2 || current_send == 3 || current_send == 4) {
		o2_send("/one/i", 0, send_char[current_send], 1234.0);
		send_the_message();
		o2_send("/one/B", 0, send_char[current_send], 1234.0);
		send_the_message();
		o2_send("/one/h", 0, send_char[current_send], 12345.0);
		send_the_message();
		o2_send("/one/f", 0, send_char[current_send], 1234.0);
		send_the_message();
		o2_send("/one/d", 0, send_char[current_send], 1234.0);
		send_the_message();
		o2_send("/one/t", 0, send_char[current_send], 1234.0);
		send_the_message();
		o2_send("/one/T", 0, send_char[current_send], 1111.0);
		send_the_message();
		o2_send("/one/F", 0, send_char[current_send], 0.0);
		send_the_message();
	}
	int a;
	scanf("%d", &a);
    printf("DONE\n");
    o2_finish();
    return 0;
}
