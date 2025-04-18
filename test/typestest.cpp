//  typestest.c -- send messages of all (but vector and array) types
//

#include <stdio.h>
#include "o2.h"
#include "testassert.h"
#include "string.h"

bool got_the_message = false;

O2blob_ptr a_blob;
uint32_t a_midi_msg;

void service_none(O2msg_data_ptr data, const char *types,
                  O2arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    o2assert(strcmp(types, "") == 0);
    printf("service_none types=%s\n", types);
    got_the_message = true;
}


void service_nonep(O2msg_data_ptr data, const char *types,
                   O2arg_ptr *argv, int argc, const void *user_data)
{
    o2assert(strcmp(types, "") == 0);
    o2assert(argc == 0);
    printf("service_ip types=%s\n", types);
    got_the_message = true;
}


void service_i(O2msg_data_ptr data, const char *types,
               O2arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    o2assert(strcmp(types, "i") == 0);
    O2arg_ptr arg = o2_get_next(O2_INT32);
    o2assert(arg->i == 1234);
    printf("service_i types=%s int32=%d\n", types, arg->i);
    got_the_message = true;
}


void service_ip(O2msg_data_ptr data, const char *types,
                O2arg_ptr *argv, int argc, const void *user_data)
{
    o2assert(strcmp(types, "i") == 0);
    o2assert(argc == 1);
    o2assert(argv[0]->i == 1234);
    printf("service_ip types=%s int32=%d\n", types, argv[0]->i);
    got_the_message = true;
}


void service_c(O2msg_data_ptr data, const char *types,
               O2arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    o2assert(strcmp(types, "c") == 0);
    O2arg_ptr arg = o2_get_next(O2_CHAR);
    o2assert(arg->c == 'Q');
    printf("service_c types=%s char=%c\n", types, arg->c);
    got_the_message = true;
}


void service_cp(O2msg_data_ptr data, const char *types,
                O2arg_ptr *argv, int argc, const void *user_data)
{
    o2assert(strcmp(types, "c") == 0);
    o2assert(argc == 1);
    o2assert(argv[0]->c == 'Q');
    printf("service_cp types=%s char=%c\n", types, argv[0]->c);
    got_the_message = true;
}


void service_B(O2msg_data_ptr data, const char *types,
               O2arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    o2assert(strcmp(types, "B") == 0);
    O2arg_ptr arg = o2_get_next(O2_BOOL);
    o2assert(arg->B);
    printf("service_B types=%s bool=%d\n", types, arg->B);
    got_the_message = true;
}


void service_Bp(O2msg_data_ptr data, const char *types,
                O2arg_ptr *argv, int argc, const void *user_data)
{
    o2assert(strcmp(types, "B") == 0);
    o2assert(argc == 1);
    o2assert(argv[0]->B);
    printf("service_Bp types=%s bool=%d\n", types, argv[0]->B);
    got_the_message = true;
}


void service_h(O2msg_data_ptr data, const char *types,
               O2arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    o2assert(strcmp(types, "h") == 0);
    O2arg_ptr arg = o2_get_next(O2_INT64);
    o2assert(arg->h == 12345);
    // long long "coercion" to make gcc happy
    printf("service_h types=%s int64=%lld\n", types,
           (long long) arg->h);
    got_the_message = true;
}


void service_hp(O2msg_data_ptr data, const char *types,
                O2arg_ptr *argv, int argc, const void *user_data)
{
    o2assert(strcmp(types, "h") == 0);
    o2assert(argc == 1);
    o2assert(argv[0]->h == 12345);
    // long long "coercion" to make gcc happy
    printf("service_hp types=%s int64=%lld\n", types, 
           (long long) argv[0]->h);
    got_the_message = true;
}


void service_f(O2msg_data_ptr data, const char *types,
               O2arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    o2assert(strcmp(types, "f") == 0);
    O2arg_ptr arg = o2_get_next(O2_FLOAT);
    o2assert(arg->f == 1234.5);
    printf("service_f types=%s float=%g\n", types, arg->f);
    got_the_message = true;
}


void service_fp(O2msg_data_ptr data, const char *types,
                O2arg_ptr *argv, int argc, const void *user_data)
{
    o2assert(strcmp(types, "f") == 0);
    o2assert(argc == 1);
    o2assert(argv[0]->f == 1234.5);
    printf("service_fp types=%s float=%g\n", types, argv[0]->f);
    got_the_message = true;
}


void service_d(O2msg_data_ptr data, const char *types,
               O2arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    o2assert(strcmp(types, "d") == 0);
    O2arg_ptr arg = o2_get_next(O2_DOUBLE);
    o2assert(arg->d == 1234.56);
    printf("service_d types=%s double=%g\n", types, arg->d);
    got_the_message = true;
}


void service_dp(O2msg_data_ptr data, const char *types,
                O2arg_ptr *argv, int argc, const void *user_data)
{
    o2assert(strcmp(types, "d") == 0);
    o2assert(argc == 1);
    o2assert(argv[0]->d == 1234.56);
    printf("service_dp types=%s double=%g\n", types, argv[0]->d);
    got_the_message = true;
}


void service_t(O2msg_data_ptr data, const char *types,
               O2arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    o2assert(strcmp(types, "t") == 0);
    O2arg_ptr arg = o2_get_next(O2_TIME);
    o2assert(arg->t == 1234.567);
    printf("service_t types=%s time=%g\n", types, arg->t);
    got_the_message = true;
}


void service_tp(O2msg_data_ptr data, const char *types,
                O2arg_ptr *argv, int argc, const void *user_data)
{
    o2assert(strcmp(types, "t") == 0);
    o2assert(argc == 1);
    o2assert(argv[0]->t == 1234.567);
    printf("service_tp types=%s time=%g\n", types, argv[0]->t);
    got_the_message = true;
}


void service_s(O2msg_data_ptr data, const char *types,
               O2arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    o2assert(strcmp(types, "s") == 0);
    O2arg_ptr arg = o2_get_next(O2_STRING);
    o2assert(strcmp(arg->s, "1234") == 0);
    printf("service_s types=%s string=%s\n", types, arg->s);
    got_the_message = true;
}


void service_sp(O2msg_data_ptr data, const char *types,
                O2arg_ptr *argv, int argc, const void *user_data)
{
    o2assert(strcmp(types, "s") == 0);
    o2assert(argc == 1);
    o2assert(strcmp(argv[0]->s, "1234") == 0);
    printf("service_sp types=%s string=%s\n", types, argv[0]->s);
    got_the_message = true;
}


void service_S(O2msg_data_ptr data, const char *types,
               O2arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    o2assert(strcmp(types, "S") == 0);
    O2arg_ptr arg = o2_get_next(O2_SYMBOL);
    o2assert(strcmp(arg->S, "123456") == 0);
    printf("service_S types=%s symbol=%s\n", types, arg->S);
    got_the_message = true;
}


void service_Sp(O2msg_data_ptr data, const char *types,
                O2arg_ptr *argv, int argc, const void *user_data)
{
    o2assert(strcmp(types, "S") == 0);
    o2assert(argc == 1);
    o2assert(strcmp(argv[0]->S, "123456") == 0);
    printf("service_Sp types=%s symbol=%s\n", types, argv[0]->S);
    got_the_message = true;
}


void service_b(O2msg_data_ptr data, const char *types,
               O2arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    o2assert(strcmp(types, "b") == 0);
    O2arg_ptr arg = o2_get_next(O2_BLOB);
    o2assert(arg->b.size = a_blob->size &&
           memcmp(arg->b.data, a_blob->data, 15) == 0);
    printf("service_b types=%s blob=%p\n", types, &arg->b);
    got_the_message = true;
}


void service_bp(O2msg_data_ptr data, const char *types,
                O2arg_ptr *argv, int argc, const void *user_data)
{
    o2assert(strcmp(types, "b") == 0);
    o2assert(argc == 1);
    o2assert(argv[0]->b.size = a_blob->size &&
           memcmp(argv[0]->b.data, a_blob->data, 15) == 0);
    printf("service_bp types=%s blob=%p\n", types, &argv[0]->b);
    got_the_message = true;
}


void service_m(O2msg_data_ptr data, const char *types,
               O2arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    o2assert(strcmp(types, "m") == 0);
    O2arg_ptr arg = o2_get_next(O2_MIDI);
    o2assert(arg->m == a_midi_msg);
    printf("service_m types=%s midi = %2x %2x %2x\n", types,
           (arg->m >> 16) & 0xff, (arg->m >> 8) & 0xff, arg->m & 0xff);
    got_the_message = true;
}


void service_mp(O2msg_data_ptr data, const char *types,
                O2arg_ptr *argv, int argc, const void *user_data)
{
    o2assert(strcmp(types, "m") == 0);
    o2assert(argc == 1);
    O2arg_ptr arg = argv[0];
    o2assert(arg->m == a_midi_msg);
    printf("service_mp types=%s midi = %2x %2x %2x\n", types,
           (arg->m >> 16) & 0xff, (arg->m >> 8) & 0xff, arg->m & 0xff);
    got_the_message = true;
}


void service_T(O2msg_data_ptr data, const char *types,
               O2arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    o2assert(strcmp(types, "T") == 0);
    printf("service_T types=%s\n", types);
    got_the_message = true;
}


void service_Tp(O2msg_data_ptr data, const char *types,
                O2arg_ptr *argv, int argc, const void *user_data)
{
    o2assert(strcmp(types, "T") == 0);
    o2assert(argc == 1);
    printf("service_Tp types=%s\n", types);
    got_the_message = true;
}


void service_F(O2msg_data_ptr data, const char *types,
               O2arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    o2assert(strcmp(types, "F") == 0);
    printf("service_F types=%s\n", types);
    got_the_message = true;
}


void service_Fp(O2msg_data_ptr data, const char *types,
                O2arg_ptr *argv, int argc, const void *user_data)
{
    o2assert(strcmp(types, "F") == 0);
    o2assert(argc == 1);
    printf("service_Fp types=%s\n", types);
    got_the_message = true;
}


void service_I(O2msg_data_ptr data, const char *types,
               O2arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    o2assert(strcmp(types, "I") == 0);
    printf("service_I types=%s\n", types);
    got_the_message = true;
}


void service_Ip(O2msg_data_ptr data, const char *types,
                O2arg_ptr *argv, int argc, const void *user_data)
{
    o2assert(strcmp(types, "I") == 0);
    o2assert(argc == 1);
    printf("service_Ip types=%s\n", types);
    got_the_message = true;
}


void service_N(O2msg_data_ptr data, const char *types,
               O2arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    o2assert(strcmp(types, "N") == 0);
    printf("service_N types=%s\n", types);
    got_the_message = true;
}


void service_Np(O2msg_data_ptr data, const char *types,
                O2arg_ptr *argv, int argc, const void *user_data)
{
    o2assert(strcmp(types, "N") == 0);
    o2assert(argc == 1);
    printf("service_Np types=%s\n", types);
    got_the_message = true;
}


void service_many(O2msg_data_ptr data, const char *types,
                  O2arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    O2arg_ptr arg = o2_get_next(O2_INT32);
    o2assert(arg->i == 1234);
    arg = o2_get_next(O2_CHAR);
    o2assert(arg->c == 'Q');
    arg = o2_get_next(O2_BOOL);
    o2assert(arg->B);
    arg = o2_get_next(O2_INT64);
    o2assert(arg->h == 12345LL);
    arg = o2_get_next(O2_FLOAT);
    o2assert(arg->f == 1234.5);
    arg = o2_get_next(O2_DOUBLE);
    o2assert(arg->d == 1234.56);
    arg = o2_get_next(O2_TIME);
    o2assert(arg->t == 1234.567);
    arg = o2_get_next(O2_STRING);
    o2assert(strcmp(arg->s, "1234") == 0);
    arg = o2_get_next(O2_SYMBOL);
    o2assert(strcmp(arg->S, "123456") == 0);
    arg = o2_get_next(O2_BLOB);
    o2assert((arg->b.size == a_blob->size) &&
           memcmp(arg->b.data, a_blob->data, 15) == 0);
    arg = o2_get_next(O2_MIDI);
    o2assert(arg->m == a_midi_msg);
    arg = o2_get_next(O2_TRUE);
    o2assert(arg);
    arg = o2_get_next(O2_FALSE);
    o2assert(arg);
    arg = o2_get_next(O2_INFINITUM);
    o2assert(arg);
    arg = o2_get_next(O2_NIL);
    o2assert(arg);
    arg = o2_get_next(O2_INT32);
    o2assert(arg->i == 1234);

    o2assert(strcmp(types, "icBhfdtsSbmTFINi") == 0);
    printf("service_many types=%s\n", types);
    got_the_message = true;
}


void service_manyp(O2msg_data_ptr data, const char *types,
                   O2arg_ptr *argv, int argc, const void *user_data)
{
    o2assert(argc == 16);
    o2assert(argv[0]->i == 1234);
    o2assert(argv[1]->c == 'Q');
    o2assert(argv[2]->B);
    o2assert(argv[3]->h == 12345LL);
    o2assert(argv[4]->f == 1234.5);
    o2assert(argv[5]->d == 1234.56);
    o2assert(argv[6]->t == 1234.567);
    o2assert(strcmp(argv[7]->s, "1234") == 0);
    o2assert(strcmp(argv[8]->S, "123456") == 0);
    o2assert((argv[9]->b.size == a_blob->size) &&
           memcmp(argv[9]->b.data, a_blob->data, 15) == 0);
    o2assert(argv[10]->m == a_midi_msg);
    o2assert(argv[15]->i == 1234);
    o2assert(strcmp(types, "icBhfdtsSbmTFINi") == 0);
    printf("service_manyp types=%s\n", types);
    got_the_message = true;
}


// this handles every message to service_two
//    we'll support two things: /two/i and /two/id
void service_two(O2msg_data_ptr msg, const char *types,
                 O2arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(msg);
    if (strcmp(msg->address + 1, "two/i") == 0) {
        O2arg_ptr arg = o2_get_next(O2_INT32);
        o2assert(arg && arg->i == 1234);
        printf("service_two types=%s arg=%d\n", types, arg->i);
    } else if (strcmp(msg->address + 1, "two/id") == 0) {
        O2arg_ptr arg = o2_get_next(O2_INT32);
        int i;
        o2assert(arg && arg->i == 1234);
        i = arg->i;
        arg = o2_get_next(O2_DOUBLE);
        o2assert(arg->d == 1234.56);
        printf("service_two types=%s args=%d %g\n", types, i, arg->d);
    } else {
        o2assert(false);
    }
    got_the_message = true;
}


// this handles every message to service_two
//    we'll support two things: /two/i and /two/id
void service_three(O2msg_data_ptr msg, const char *types,
                   O2arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(msg);
    if (strcmp(msg->address + 1, "three/i") == 0) {
        O2arg_ptr arg = o2_get_next(O2_INT32);
        o2assert(arg->i == 1234);
        printf("service_three types=%s arg=%d\n", types, arg->i);
    } else if (strcmp(msg->address + 1, "three/id") == 0) {
        O2arg_ptr arg = o2_get_next(O2_INT32);
        int i;
        o2assert(arg && arg->i == 1234);
        i = arg->i;
        arg = o2_get_next(O2_DOUBLE);
        o2assert(arg->d == 1234.56);
        printf("service_three types=%s args=%d %g\n", types, i, arg->d);
    } else {
        o2assert(false);
    }
    got_the_message = true;
}


// this handles every message to service_two
//    we'll support two things: /two/i and /two/id
void service_four(O2msg_data_ptr msg, const char *types,
                  O2arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(msg);
    if (strcmp(msg->address + 1, "four/i") == 0) {
        O2arg_ptr arg = o2_get_next(O2_INT32);
        o2assert(arg->i == 1234);
        printf("service_four types=%s arg=%d\n", types, arg->i);
    } else if (strcmp(msg->address + 1, "four/id") == 0) {
        O2arg_ptr arg = o2_get_next(O2_INT32);
        int i;
        o2assert(arg && arg->i == 1234);
        i = arg->i;
        arg = o2_get_next(O2_DOUBLE);
        o2assert(arg->d == 1234.56);
        printf("service_four types=%s args=%d %g\n", types, i, arg->d);
    } else {
        o2assert(false);
    }
    got_the_message = true;
}


void send_the_message()
{
    while (!got_the_message) {
        o2_poll();
    }
    got_the_message = false;
}

void heap_tests()
{
    char *a = (char *) O2_MALLOC(1);
    char *b = O2_MALLOCT(char);
    char *c = O2_MALLOCNT(45, char);
    char *d = (char *) O2_CALLOC(5, 9);
    char *e = O2_CALLOCT(char);
    char *f = O2_CALLOCNT(45, char);
    char *g = O2_MALLOCNT(1000000, char); // REALLY BIG - special case
    // the point of writing into all these and freeing them is that
    // there are some heap consistency checks, and if enabled, and
    // if the memory is not properly managed, writing is a good way
    // to trigger any problems that consistency check might detect.
    *a = 'A';
    *b = 'B';
    strcpy(c, "this is a test 45 chars ,this is a test 45 c");
    strcpy(d, "this is a test 45 chars ,this is a test 45 c");
    *e = 'E';
    strcpy(f, "this is a test 45 chars ,this is a test 45 c");
    for (int i = 0; i < 1000000; i++) {
        g[i] = i % 256;
    }
    O2_FREE(a);
    O2_FREE(b);
    O2_FREE(c);
    O2_FREE(d);
    O2_FREE(e);
    O2_FREE(f);
    O2_FREE(g);
}


int main(int argc, const char * argv[])
{
    printf("Usage: typestest [debugflags] "
           "(see o2.h for flags, use a for all)\n");
    if (argc >= 2) {
        o2_debug_flags(argv[1]);
        printf("debug flags are: %s\n", argv[1]);
    }

    a_midi_msg = (0x90 << 16) + (60 << 8) + 100;

    o2_initialize("test");    

    a_blob = (O2blob_ptr) O2_MALLOC(20);
    a_blob->size = 15;
    memcpy(a_blob->data, "This is a blob", 15);

    heap_tests(); // also do some quick checks on O2_MALLOC, O2_FREE.
    o2_service_new("one");
    o2_service_new("two");
    o2_service_new("three");
    o2_service_new("four");
    o2_method_new("/one/none", "", &service_none, NULL, false, false);
    o2_method_new("/one/nonep", "", &service_nonep, NULL, false, true);
    o2_method_new("/one/i", "i", &service_i, NULL, false, false);
    o2_method_new("/one/ip", "i", &service_ip, NULL, false, true);
    o2_method_new("/one/c", "c", &service_c, NULL, false, false);
    o2_method_new("/one/cp", "c", &service_cp, NULL, false, true);
    o2_method_new("/one/B", "B", &service_B, NULL, false, false);
    o2_method_new("/one/Bp", "B", &service_Bp, NULL, false, true);
    o2_method_new("/one/h", "h", &service_h, NULL, false, false);
    o2_method_new("/one/hp", "h", &service_hp, NULL, false, true);
    o2_method_new("/one/f", "f", &service_f, NULL, false, false);
    o2_method_new("/one/fp", "f", &service_fp, NULL, false, true);
    o2_method_new("/one/d", "d", &service_d, NULL, false, false);
    o2_method_new("/one/dp", "d", &service_dp, NULL, false, true);
    o2_method_new("/one/t", "t", &service_t, NULL, false, false);
    o2_method_new("/one/tp", "t", &service_tp, NULL, false, true);
    o2_method_new("/one/s", "s", &service_s, NULL, false, false);
    o2_method_new("/one/sp", "s", &service_sp, NULL, false, true);
    o2_method_new("/one/S", "S", &service_S, NULL, false, false);
    o2_method_new("/one/Sp", "S", &service_Sp, NULL, false, true);
    o2_method_new("/one/b", "b", &service_b, NULL, false, false);
    o2_method_new("/one/bp", "b", &service_bp, NULL, false, true);
    o2_method_new("/one/m", "m", &service_m, NULL, false, false);
    o2_method_new("/one/mp", "m", &service_mp, NULL, false, true);
    o2_method_new("/one/T", "T", &service_T, NULL, false, false);
    o2_method_new("/one/Tp", "T", &service_Tp, NULL, false, true);
    o2_method_new("/one/F", "F", &service_F, NULL, false, false);
    o2_method_new("/one/Fp", "F", &service_Fp, NULL, false, true);
    o2_method_new("/one/I", "I", &service_I, NULL, false, false);
    o2_method_new("/one/Ip", "I", &service_Ip, NULL, false, true);
    o2_method_new("/one/N", "N", &service_N, NULL, false, false);
    o2_method_new("/one/Np", "N", &service_Np, NULL, false, true);
    o2_method_new("/one/many", "icBhfdtsSbmTFINi", &service_many,
                  NULL, false, false);
    o2_method_new("/one/manyp", "icBhfdtsSbmTFINi", &service_manyp,
                  NULL, false, true);
    o2_method_new("/two", NULL, &service_two, NULL, false, false);
    o2_method_new("/three", "i", &service_three, NULL, false, true);
    o2_method_new("/four", "i", &service_four, NULL, true, true);

    o2_send("/one/i", 0, "i", 1234);
    send_the_message();
    o2_send("/one/ip", 0, "i", 1234);
    send_the_message();
    o2_send("/one/c", 0, "c", 'Q');
    send_the_message();
    o2_send("/one/cp", 0, "c", 'Q');
    send_the_message();
    o2_send("/one/B", 0, "B", true);
    send_the_message();
    o2_send("/one/Bp", 0, "B", true);
    send_the_message();
    o2_send("/one/h", 0, "h", 12345LL);
    send_the_message();
    o2_send("/one/hp", 0, "h", 12345LL);
    send_the_message();
    o2_send("/one/f", 0, "f", 1234.5);
    send_the_message();
    o2_send("/one/fp", 0, "f", 1234.5);
    send_the_message();
    o2_send("/one/d", 0, "d", 1234.56);
    send_the_message();
    o2_send("/one/dp", 0, "d", 1234.56);
    send_the_message();
    o2_send("/one/t", 0, "t", 1234.567);
    send_the_message();
    o2_send("/one/tp", 0, "t", 1234.567);
    send_the_message();
    o2_send("/one/s", 0, "s", "1234");
    send_the_message();
    o2_send("/one/sp", 0, "s", "1234");
    send_the_message();
    o2_send("/one/S", 0, "S", "123456");
    send_the_message();
    o2_send("/one/Sp", 0, "S", "123456");
    send_the_message();
    o2_send("/one/b", 0, "b", a_blob);
    send_the_message();
    o2_send("/one/bp", 0, "b", a_blob);
    send_the_message();
    o2_send("/one/m", 0, "m", a_midi_msg);
    send_the_message();
    o2_send("/one/mp", 0, "m", a_midi_msg);
    send_the_message();
    o2_send("/one/T", 0, "T");
    send_the_message();
    o2_send("/one/Tp", 0, "T");
    send_the_message();
    o2_send("/one/F", 0, "F");
    send_the_message();
    o2_send("/one/Fp", 0, "F");
    send_the_message();
    o2_send("/one/I", 0, "I");
    send_the_message();
    o2_send("/one/Ip", 0, "I");
    send_the_message();
    o2_send("/one/N", 0, "N");
    send_the_message();
    o2_send("/one/Np", 0, "N");
    send_the_message();
    o2_send("/one/many", 0, "icBhfdtsSbmTFINi", 1234, 'Q', true, 12345LL,
            1234.5, 1234.56, 1234.567, "1234", "123456",
            a_blob, a_midi_msg, 1234);
    send_the_message();
    o2_send("/one/manyp", 0, "icBhfdtsSbmTFINi", 1234, 'Q', true, 12345LL,
            1234.5, 1234.56, 1234.567, "1234", "123456",
            a_blob, a_midi_msg, 1234);
    send_the_message();
    o2_send("/two/i", 0, "i", 1234);
    send_the_message();
    o2_send("!two/i", 0, "i", 1234);
    send_the_message();
    o2_send("/two/id", 0, "id", 1234, 1234.56);
    send_the_message();
    o2_send("/three/i", 0, "i", 1234);
    send_the_message();
    o2_send("/four/i", 0, "d", 1234.0);
    send_the_message();
    O2_FREE(a_blob);
    printf("DONE\n");
    o2_finish();
    return 0;
}
