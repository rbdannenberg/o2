// longtest.c -- test long messages that require allocation
//

#include <stdio.h>
#include "o2.h"
#include "assert.h"
#include "string.h"
#include "o2_message.h"


int got_the_message = FALSE;

o2_blob_ptr a_blob;
char a_midi_msg[4];

int arg_count = 0;

// receive arg_count floats
int service_f(const o2_message_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(data);
    for (int i = 0; i < arg_count; i++) {
        assert(*types ==  'f');
        o2_arg_ptr arg = o2_get_next('f');
        assert(arg);
        assert(arg->f == i + 123);
        types++;
    }
    assert(*types == 0); // end of string, got arg_count floats
    got_the_message = TRUE;
    return O2_SUCCESS;
}


// receive arg_count doubles
int service_d(const o2_message_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(data);
    for (int i = 0; i < arg_count; i++) {
        assert(*types ==  'd');
        o2_arg_ptr arg = o2_get_next('d');
        assert(arg);
        assert(arg->d == i + 1234);
        types++;
    }
    assert(*types == 0); // end of string, got arg_count floats
    got_the_message = TRUE;
    return O2_SUCCESS;
}


// receive arg_count floats, coerced to ints, with parsing
int service_fc(const o2_message_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, void *user_data)
{
    assert(argc == arg_count);
    o2_start_extract(data);
    for (int i = 0; i < arg_count; i++) {
        assert(*types == 'i');
        assert(argv[i]);
        int actual = argv[i]->i;
        assert(actual == i + 123);
        types++;
    }
    assert(*types == 0); // end of string, got arg_count floats
    got_the_message = TRUE;
    return O2_SUCCESS;
}


// receive arg_count doubles, coerced to ints, with parsing
int service_dc(const o2_message_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, void *user_data)
{
    assert(argc == arg_count);
    o2_start_extract(data);
    for (int i = 0; i < arg_count; i++) {
        assert(*types ==  'h');
        assert(argv[i]);
        int64_t actual = argv[i]->h;
        assert(actual == i + 1234);
        types++;
    }
    assert(*types == 0); // end of string, got arg_count floats
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
    const int N = 100;
    char address[32];
    char types[200];
    o2_initialize("test");    
    o2_add_service("one");

    // send from 0 to N-1 floats, without coercion
    for (int i = 0; i < N; i++) {
        sprintf(address, "/one/f%d", i);
        for (int j = 0; j < i; j++) {
            types[j] = 'f';
        }
        types[i] = 0;
        o2_add_method(address, types, &service_f, NULL, FALSE, FALSE);
        o2_start_send();
        for (int j = 0; j < i; j++) {
            o2_add_float(j + 123.0F);
        }
        arg_count = i;
        o2_finish_send(0, address);
        send_the_message();
    }
    printf("DONE sending 0 to %d floats\n", N - 1);

    // send from 0 to N-1 doubles, without coercion
    for (int i = 0; i < N; i++) {
        sprintf(address, "/one/d%d", i);
        for (int j = 0; j < i; j++) {
            types[j] = 'd';
        }
        types[i] = 0;
        o2_add_method(address, types, &service_d, NULL, FALSE, FALSE);
        o2_start_send();
        for (int j = 0; j < i; j++) {
            o2_add_double(j + 1234);
        }
        arg_count = i;
        o2_finish_send(0, address);
        send_the_message();
    }
    printf("DONE sending 0 to %d doubles\n", N - 1);

    // send from 0 to N-1 floats, with coercion to int and parsing
    for (int i = 0; i < N; i++) {
        sprintf(address, "/one/fc%d", i);
        for (int j = 0; j < i; j++) {
            types[j] = 'i';
        }
        types[i] = 0;
        o2_add_method(address, types, &service_fc, NULL, TRUE, TRUE);
        o2_start_send();
        for (int j = 0; j < i; j++) {
            o2_add_float(j + 123.0F);
        }
        arg_count = i;
        o2_finish_send(0, address);
        send_the_message();
    }
    printf("DONE sending 0 to %d floats coerced to ints with parsing\n",
           N - 1);

    // send from 0 to N-1 doubles, with coercion to int64_t and parsing
    for (int i = 0; i < N; i++) {
        sprintf(address, "/one/dc%d", i);
        for (int j = 0; j < i; j++) {
            types[j] = 'h';
        }
        types[i] = 0;
        o2_add_method(address, types, &service_dc, NULL, TRUE, TRUE);
        o2_start_send();
        for (int j = 0; j < i; j++) {
            o2_add_double(j + 1234);
        }
        arg_count = i;
        o2_finish_send(0, address);
        send_the_message();
    }
    printf("DONE sending 0 to %d doubles coerced to int64_t with parsing\n",
           N - 1);

    printf("DONE\n");
    o2_finish();
    return 0;
}
