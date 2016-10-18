//  dispatchtest.c -- dispatch messages between local services
//

#include <stdio.h>
#include "o2.h"
#include "assert.h"
#include "string.h"


int got_the_message = FALSE;

o2_blob_ptr a_blob;
char a_midi_msg[4];

int service_i(const o2_message_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(data);
    assert(strcmp(types, "i") == 0);
    o2_arg_ptr arg = o2_get_next('i');
    assert(arg->i == 1234);
    printf("service_i types=%s int32=%d\n", types, arg->i);
    got_the_message = TRUE;
    return O2_SUCCESS;
}


int service_c(const o2_message_ptr data, const char *types,
              o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(data);
    assert(strcmp(types, "c") == 0);
    o2_arg_ptr arg = o2_get_next('c');
    assert(arg->c == 'Q');
    printf("service_c types=%s char=%c\n", types, arg->c);
    got_the_message = TRUE;
    return O2_SUCCESS;
}


int service_B(const o2_message_ptr data, const char *types,
              o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(data);
    assert(strcmp(types, "B") == 0);
    o2_arg_ptr arg = o2_get_next('B');
    assert(arg->B == TRUE);
    printf("service_B types=%s bool=%d\n", types, arg->B);
    got_the_message = TRUE;
    return O2_SUCCESS;
}


int service_h(const o2_message_ptr data, const char *types,
              o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(data);
    assert(strcmp(types, "h") == 0);
    o2_arg_ptr arg = o2_get_next('h');
    assert(arg->h == 12345);
    printf("service_h types=%s int64=%ld\n", types, arg->h);
    got_the_message = TRUE;
    return O2_SUCCESS;
}


int service_f(const o2_message_ptr data, const char *types,
              o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(data);
    assert(strcmp(types, "f") == 0);
    o2_arg_ptr arg = o2_get_next('f');
    assert(arg->f == 1234.5);
    printf("service_f types=%s float=%g\n", types, arg->f);
    got_the_message = TRUE;
    return O2_SUCCESS;
}


int service_d(const o2_message_ptr data, const char *types,
              o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(data);
    assert(strcmp(types, "d") == 0);
    o2_arg_ptr arg = o2_get_next('d');
    assert(arg->d == 1234.56);
    printf("service_d types=%s double=%g\n", types, arg->d);
    got_the_message = TRUE;
    return O2_SUCCESS;
}


int service_t(const o2_message_ptr data, const char *types,
              o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(data);
    assert(strcmp(types, "t") == 0);
    o2_arg_ptr arg = o2_get_next('t');
    assert(arg->t == 1234.567);
    printf("service_t types=%s time=%g\n", types, arg->t);
    got_the_message = TRUE;
    return O2_SUCCESS;
}


int service_s(const o2_message_ptr data, const char *types,
              o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(data);
    assert(strcmp(types, "s") == 0);
    o2_arg_ptr arg = o2_get_next('s');
    assert(strcmp(arg->s, "1234") == 0);
    printf("service_s types=%s string=%s\n", types, arg->s);
    got_the_message = TRUE;
    return O2_SUCCESS;
}


int service_S(const o2_message_ptr data, const char *types,
              o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(data);
    assert(strcmp(types, "S") == 0);
    o2_arg_ptr arg = o2_get_next('S');
    assert(strcmp(arg->S, "123456") == 0);
    printf("service_S types=%s symbol=%s\n", types, arg->S);
    got_the_message = TRUE;
    return O2_SUCCESS;
}


int service_b(const o2_message_ptr data, const char *types,
              o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(data);
    assert(strcmp(types, "b") == 0);
    o2_arg_ptr arg = o2_get_next('b');
    assert(arg->b.size = a_blob->size &&
           memcmp(arg->b.data, a_blob->data, 15) == 0);
    printf("service_b types=%s blob=%p\n", types, &(arg->b));
    got_the_message = TRUE;
    return O2_SUCCESS;
}


int service_m(const o2_message_ptr data, const char *types,
              o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(data);
    assert(strcmp(types, "m") == 0);
    o2_arg_ptr arg = o2_get_next('m');
    assert(memcmp(arg->m, &(a_midi_msg[0]), 3) == 0);
    printf("service_m types=%s midi = %2x %2x %2x\n", types,
           arg->m[0], arg->m[1], arg->m[2]);
    got_the_message = TRUE;
    return O2_SUCCESS;
}


int service_T(const o2_message_ptr data, const char *types,
              o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(data);
    assert(strcmp(types, "T") == 0);
    printf("service_T types=%s\n", types);
    got_the_message = TRUE;
    return O2_SUCCESS;
}


int service_F(const o2_message_ptr data, const char *types,
              o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(data);
    assert(strcmp(types, "F") == 0);
    printf("service_F types=%s\n", types);
    got_the_message = TRUE;
    return O2_SUCCESS;
}


int service_I(const o2_message_ptr data, const char *types,
              o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(data);
    assert(strcmp(types, "I") == 0);
    printf("service_I types=%s\n", types);
    got_the_message = TRUE;
    return O2_SUCCESS;
}


int service_N(const o2_message_ptr data, const char *types,
              o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(data);
    assert(strcmp(types, "N") == 0);
    printf("service_N types=%s\n", types);
    got_the_message = TRUE;
    return O2_SUCCESS;
}


int service_many(const o2_message_ptr data, const char *types,
                 o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(data);
    o2_arg_ptr arg = o2_get_next('i');
    assert(arg->i == 1234);
    arg = o2_get_next('c');
    assert(arg->c == 'Q');
    arg = o2_get_next('B');
    assert(arg->B == TRUE);
    arg = o2_get_next('h');
    assert(arg->h == 12345LL);
    arg = o2_get_next('f');
    assert(arg->f == 1234.5);
    arg = o2_get_next('d');
    assert(arg->d == 1234.56);
    arg = o2_get_next('t');
    assert(arg->t == 1234.567);
    arg = o2_get_next('s');
    assert(strcmp(arg->s, "1234") == 0);
    arg = o2_get_next('S');
    assert(strcmp(arg->S, "123456") == 0);
    arg = o2_get_next('b');
    assert(arg->b.size = a_blob->size &&
           memcmp(arg->b.data, a_blob->data, 15) == 0);
    arg = o2_get_next('m');
    assert(memcmp(arg->m, &(a_midi_msg[0]), 3) == 0);
    o2_start_extract(data);
    arg = o2_get_next('i');

    assert(strcmp(types, "icBhfdtsSbmTFINi") == 0);
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
    o2_add_method("/one/c", "c", &service_c, NULL, FALSE, FALSE);
    o2_add_method("/one/B", "B", &service_B, NULL, FALSE, FALSE);
    o2_add_method("/one/h", "h", &service_h, NULL, FALSE, FALSE);
    o2_add_method("/one/f", "f", &service_f, NULL, FALSE, FALSE);
    o2_add_method("/one/d", "d", &service_d, NULL, FALSE, FALSE);
    o2_add_method("/one/t", "t", &service_t, NULL, FALSE, FALSE);
    o2_add_method("/one/s", "s", &service_s, NULL, FALSE, FALSE);
    o2_add_method("/one/S", "S", &service_S, NULL, FALSE, FALSE);
    o2_add_method("/one/b", "b", &service_b, NULL, FALSE, FALSE);
    o2_add_method("/one/m", "m", &service_m, NULL, FALSE, FALSE);
    o2_add_method("/one/T", "T", &service_T, NULL, FALSE, FALSE);
    o2_add_method("/one/F", "F", &service_F, NULL, FALSE, FALSE);
    o2_add_method("/one/I", "I", &service_I, NULL, FALSE, FALSE);
    o2_add_method("/one/N", "N", &service_N, NULL, FALSE, FALSE);
    o2_add_method("/one/many", "icBhfdtsSbmTFINi", &service_many,
                  NULL, FALSE, FALSE);

    o2_send("/one/i", 0, "i", 1234);
    send_the_message();
    o2_send("/one/c", 0, "c", 'Q');
    send_the_message();
    o2_send("/one/B", 0, "B", TRUE);
    send_the_message();
    o2_send("/one/h", 0, "h", 12345LL);
    send_the_message();
    o2_send("/one/f", 0, "f", 1234.5);
    send_the_message();
    o2_send("/one/d", 0, "d", 1234.56);
    send_the_message();
    o2_send("/one/t", 0, "t", 1234.567);
    send_the_message();
    o2_send("/one/s", 0, "s", "1234");
    send_the_message();
    o2_send("/one/S", 0, "S", "123456");
    send_the_message();
    o2_send("/one/b", 0, "b", a_blob);
    send_the_message();
    o2_send("/one/m", 0, "m", &(a_midi_msg[0]));
    send_the_message();
    o2_send("/one/T", 0, "T");
    send_the_message();
    o2_send("/one/F", 0, "F");
    send_the_message();
    o2_send("/one/I", 0, "I");
    send_the_message();
    o2_send("/one/N", 0, "N");
    send_the_message();
    o2_send("/one/many", 0, "icBhfdtsSbmTFINi", 1234, 'Q', TRUE, 12345LL,
            1234.5, 1234.56, 1234.567, "1234", "123456",
            a_blob, &(a_midi_msg[0]), 1234);
    send_the_message();
    printf("DONE\n");
    o2_finish();
    return 0;
}
