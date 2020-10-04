// longtest.c -- test long messages that require allocation
//

#include <stdio.h>
#include "o2.h"
#include "assert.h"
#include "string.h"
// why do we need this? If we need this we also need thread_local from o2_internal.h
//          #include "o2_message.h"


bool got_the_message = false;

o2_blob_ptr a_blob;
char a_midi_msg[4];

int arg_count = 0;

// receive arg_count floats
void service_f(o2_msg_data_ptr data, const char *types,
               o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_extract_start(data);
    for (int i = 0; i < arg_count; i++) {
        assert(*types ==  'f');
#ifndef NDEBUG
        o2_arg_ptr arg = // only needed for assert()
#endif
        o2_get_next('f');
        assert(arg);
        assert(arg->f == i + 123);
        types++;
    }
    assert(*types == 0); // end of string, got arg_count floats
    got_the_message = true;
}


// receive arg_count doubles
void service_d(o2_msg_data_ptr data, const char *types,
               o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_extract_start(data);
    for (int i = 0; i < arg_count; i++) {
        assert(*types ==  'd');
#ifndef NDEBUG
        o2_arg_ptr arg = // only needed for assert()
#endif
        o2_get_next('d');
        assert(arg);
        assert(arg->d == i + 1234);
        types++;
    }
    assert(*types == 0); // end of string, got arg_count floats
    got_the_message = true;
}


// receive arg_count floats, coerced to ints, with parsing
void service_fc(o2_msg_data_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, void *user_data)
{
    assert(argc == arg_count);
    o2_extract_start(data);
    for (int i = 0; i < arg_count; i++) {
        assert(*types == 'i');
        assert(argv[i]);
#ifndef NDEBUG
        int actual = // only needed for assert
#endif
        argv[i]->i;
        assert(actual == i + 123);
        types++;
    }
    assert(*types == 0); // end of string, got arg_count floats
    got_the_message = true;
}


// receive arg_count doubles, coerced to ints, with parsing
void service_dc(o2_msg_data_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, void *user_data)
{
    assert(argc == arg_count);
    o2_extract_start(data);
    for (int i = 0; i < arg_count; i++) {
        assert(*types ==  'h');
        assert(argv[i]);
#ifndef NDEBUG
        int64_t actual = argv[i]->h; // only needed for assert()
#endif
        assert(actual == i + 1234);
        types++;
    }
    assert(*types == 0); // end of string, got arg_count floats
    got_the_message = true;
}


void send_the_message()
{
    while (!got_the_message) {
        o2_poll();
    }
    got_the_message = false;
}


int main(int argc, const char * argv[])
{
    const int N = 100;
    char address[32];
    char types[200];
    o2_initialize("test");    
    o2_service_new("one");

    // send from 0 to N-1 floats, without coercion
    for (int i = 0; i < N; i++) {
        sprintf(address, "/one/f%d", i);
        for (int j = 0; j < i; j++) {
            types[j] = 'f';
        }
        types[i] = 0;
        o2_method_new(address, types, &service_f, NULL, false, false);
        o2_send_start();
        for (int j = 0; j < i; j++) {
            o2_add_float(j + 123.0F);
        }
        arg_count = i;
        o2_send_finish(0, address, true);
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
        o2_method_new(address, types, &service_d, NULL, false, false);
        o2_send_start();
        for (int j = 0; j < i; j++) {
            o2_add_double(j + 1234);
        }
        arg_count = i;
        o2_send_finish(0, address, true);
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
        o2_method_new(address, types, &service_fc, NULL, true, true);
        o2_send_start();
        for (int j = 0; j < i; j++) {
            o2_add_float(j + 123.0F);
        }
        arg_count = i;
        o2_send_finish(0, address, true);
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
        o2_method_new(address, types, &service_dc, NULL, true, true);
        o2_send_start();
        for (int j = 0; j < i; j++) {
            o2_add_double(j + 1234);
        }
        arg_count = i;
        o2_send_finish(0, address, true);
        send_the_message();
    }
    printf("DONE sending 0 to %d doubles coerced to int64_t with parsing\n",
           N - 1);

    printf("DONE\n");
    o2_finish();
    return 0;
}
