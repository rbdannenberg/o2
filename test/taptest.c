//  taptest.c -- send messages of all (but vector and array) types
//      to a collection of services that are tapped and check
//      that the delivery to tapper services works
//

#include <stdio.h>
#include "o2.h"
#include "assert.h"
#include "string.h"

bool got_the_message = false;
bool tapped_the_message = false;

o2_blob_ptr a_blob;
uint32_t a_midi_msg;

void service_none(o2_msg_data_ptr data, const char *types,
                  o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    assert(strcmp(types, "") == 0);
    printf("service_none types=%s\n", types);
    got_the_message = true;
}


void service_nonep(o2_msg_data_ptr data, const char *types,
                   o2_arg_ptr *argv, int argc, const void *user_data)
{
    assert(strcmp(types, "") == 0);
    assert(argc == 0);
    printf("service_ip types=%s\n", types);
    got_the_message = true;
}


void service_i(o2_msg_data_ptr data, const char *types,
               o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    assert(strcmp(types, "i") == 0);
    o2_arg_ptr arg = o2_get_next(O2_INT32);
    assert(arg->i == 1234);
    printf("service_i types=%s int32=%d\n", types, arg->i);
    got_the_message = true;
}


void service_ip(o2_msg_data_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, const void *user_data)
{
    assert(strcmp(types, "i") == 0);
    assert(argc == 1);
    assert(argv[0]->i == 1234);
    printf("service_ip types=%s int32=%d\n", types, argv[0]->i);
    got_the_message = true;
}


void service_c(o2_msg_data_ptr data, const char *types,
               o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    assert(strcmp(types, "c") == 0);
    o2_arg_ptr arg = o2_get_next(O2_CHAR);
    assert(arg->c == 'Q');
    printf("service_c types=%s char=%c\n", types, arg->c);
    got_the_message = true;
}


void service_cp(o2_msg_data_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, const void *user_data)
{
    assert(strcmp(types, "c") == 0);
    assert(argc == 1);
    assert(argv[0]->c == 'Q');
    printf("service_cp types=%s char=%c\n", types, argv[0]->c);
    got_the_message = true;
}


void service_B(o2_msg_data_ptr data, const char *types,
               o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    assert(strcmp(types, "B") == 0);
    o2_arg_ptr arg = o2_get_next(O2_BOOL);
    assert(arg->B == true);
    printf("service_B types=%s bool=%d\n", types, arg->B);
    got_the_message = true;
}


void service_Bp(o2_msg_data_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, const void *user_data)
{
    assert(strcmp(types, "B") == 0);
    assert(argc == 1);
    assert(argv[0]->B == true);
    printf("service_Bp types=%s bool=%d\n", types, argv[0]->B);
    got_the_message = true;
}


void service_h(o2_msg_data_ptr data, const char *types,
               o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    assert(strcmp(types, "h") == 0);
    o2_arg_ptr arg = o2_get_next(O2_INT64);
    assert(arg->h == 12345);
    // long long "coercion" to make gcc happy
    printf("service_h types=%s int64=%lld\n", types,
           (long long) arg->h);
    got_the_message = true;
}


void service_hp(o2_msg_data_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, const void *user_data)
{
    assert(strcmp(types, "h") == 0);
    assert(argc == 1);
    assert(argv[0]->h == 12345);
    // long long "coercion" to make gcc happy
    printf("service_hp types=%s int64=%lld\n", types, 
           (long long) argv[0]->h);
    got_the_message = true;
}


void service_f(o2_msg_data_ptr data, const char *types,
               o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    assert(strcmp(types, "f") == 0);
    o2_arg_ptr arg = o2_get_next(O2_FLOAT);
    assert(arg->f == 1234.5);
    printf("service_f types=%s float=%g\n", types, arg->f);
    got_the_message = true;
}


void service_fp(o2_msg_data_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, const void *user_data)
{
    assert(strcmp(types, "f") == 0);
    assert(argc == 1);
    assert(argv[0]->f == 1234.5);
    printf("service_fp types=%s float=%g\n", types, argv[0]->f);
    got_the_message = true;
}


void service_d(o2_msg_data_ptr data, const char *types,
               o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    assert(strcmp(types, "d") == 0);
    o2_arg_ptr arg = o2_get_next(O2_DOUBLE);
    assert(arg->d == 1234.56);
    printf("service_d types=%s double=%g\n", types, arg->d);
    got_the_message = true;
}


void service_dp(o2_msg_data_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, const void *user_data)
{
    assert(strcmp(types, "d") == 0);
    assert(argc == 1);
    assert(argv[0]->d == 1234.56);
    printf("service_dp types=%s double=%g\n", types, argv[0]->d);
    got_the_message = true;
}


void service_t(o2_msg_data_ptr data, const char *types,
               o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    assert(strcmp(types, "t") == 0);
    o2_arg_ptr arg = o2_get_next(O2_TIME);
    assert(arg->t == 1234.567);
    printf("service_t types=%s time=%g\n", types, arg->t);
    got_the_message = true;
}


void service_tp(o2_msg_data_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, const void *user_data)
{
    assert(strcmp(types, "t") == 0);
    assert(argc == 1);
    assert(argv[0]->t == 1234.567);
    printf("service_tp types=%s time=%g\n", types, argv[0]->t);
    got_the_message = true;
}


void service_s(o2_msg_data_ptr data, const char *types,
               o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    assert(strcmp(types, "s") == 0);
    o2_arg_ptr arg = o2_get_next(O2_STRING);
    assert(strcmp(arg->s, "1234") == 0);
    printf("service_s types=%s string=%s\n", types, arg->s);
    got_the_message = true;
}


void service_sp(o2_msg_data_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, const void *user_data)
{
    assert(strcmp(types, "s") == 0);
    assert(argc == 1);
    assert(strcmp(argv[0]->s, "1234") == 0);
    printf("service_sp types=%s string=%s\n", types, argv[0]->s);
    got_the_message = true;
}


void service_S(o2_msg_data_ptr data, const char *types,
               o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    assert(strcmp(types, "S") == 0);
    o2_arg_ptr arg = o2_get_next(O2_SYMBOL);
    assert(strcmp(arg->S, "123456") == 0);
    printf("service_S types=%s symbol=%s\n", types, arg->S);
    got_the_message = true;
}


void service_Sp(o2_msg_data_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, const void *user_data)
{
    assert(strcmp(types, "S") == 0);
    assert(argc == 1);
    assert(strcmp(argv[0]->S, "123456") == 0);
    printf("service_Sp types=%s symbol=%s\n", types, argv[0]->S);
    got_the_message = true;
}


void service_b(o2_msg_data_ptr data, const char *types,
               o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    assert(strcmp(types, "b") == 0);
    o2_arg_ptr arg = o2_get_next(O2_BLOB);
    assert(arg->b.size = a_blob->size &&
           memcmp(arg->b.data, a_blob->data, 15) == 0);
    printf("service_b types=%s blob=%p\n", types, &arg->b);
    got_the_message = true;
}


void service_bp(o2_msg_data_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, const void *user_data)
{
    assert(strcmp(types, "b") == 0);
    assert(argc == 1);
    assert(argv[0]->b.size = a_blob->size &&
           memcmp(argv[0]->b.data, a_blob->data, 15) == 0);
    printf("service_bp types=%s blob=%p\n", types, &argv[0]->b);
    got_the_message = true;
}


void service_m(o2_msg_data_ptr data, const char *types,
               o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    assert(strcmp(types, "m") == 0);
    o2_arg_ptr arg = o2_get_next(O2_MIDI);
    assert(arg->m == a_midi_msg);
    printf("service_m types=%s midi = %2x %2x %2x\n", types,
           (arg->m >> 16) & 0xff, (arg->m >> 8) & 0xff, arg->m & 0xff);
    got_the_message = true;
}


void service_mp(o2_msg_data_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, const void *user_data)
{
    assert(strcmp(types, "m") == 0);
    assert(argc == 1);
    o2_arg_ptr arg = argv[0];
    assert(arg->m == a_midi_msg);
    printf("service_mp types=%s midi = %2x %2x %2x\n", types,
           (arg->m >> 16) & 0xff, (arg->m >> 8) & 0xff, arg->m & 0xff);
    got_the_message = true;
}


void service_T(o2_msg_data_ptr data, const char *types,
               o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    assert(strcmp(types, "T") == 0);
    printf("service_T types=%s\n", types);
    got_the_message = true;
}


void service_Tp(o2_msg_data_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, const void *user_data)
{
    assert(strcmp(types, "T") == 0);
    assert(argc == 1);
    printf("service_Tp types=%s\n", types);
    got_the_message = true;
}


void service_F(o2_msg_data_ptr data, const char *types,
               o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    assert(strcmp(types, "F") == 0);
    printf("service_F types=%s\n", types);
    got_the_message = true;
}


void service_Fp(o2_msg_data_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, const void *user_data)
{
    assert(strcmp(types, "F") == 0);
    assert(argc == 1);
    printf("service_Fp types=%s\n", types);
    got_the_message = true;
}


void service_I(o2_msg_data_ptr data, const char *types,
               o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    assert(strcmp(types, "I") == 0);
    printf("service_I types=%s\n", types);
    got_the_message = true;
}


void service_Ip(o2_msg_data_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, const void *user_data)
{
    assert(strcmp(types, "I") == 0);
    assert(argc == 1);
    printf("service_Ip types=%s\n", types);
    got_the_message = true;
}


void service_N(o2_msg_data_ptr data, const char *types,
               o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    assert(strcmp(types, "N") == 0);
    printf("service_N types=%s\n", types);
    got_the_message = true;
}


void service_Np(o2_msg_data_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, const void *user_data)
{
    assert(strcmp(types, "N") == 0);
    assert(argc == 1);
    printf("service_Np types=%s\n", types);
    got_the_message = true;
}


void service_many(o2_msg_data_ptr data, const char *types,
                  o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    o2_arg_ptr arg = o2_get_next(O2_INT32);
    assert(arg->i == 1234);
    arg = o2_get_next(O2_CHAR);
    assert(arg->c == 'Q');
    arg = o2_get_next(O2_BOOL);
    assert(arg->B == true);
    arg = o2_get_next(O2_INT64);
    assert(arg->h == 12345LL);
    arg = o2_get_next(O2_FLOAT);
    assert(arg->f == 1234.5);
    arg = o2_get_next(O2_DOUBLE);
    assert(arg->d == 1234.56);
    arg = o2_get_next(O2_TIME);
    assert(arg->t == 1234.567);
    arg = o2_get_next(O2_STRING);
    assert(strcmp(arg->s, "1234") == 0);
    arg = o2_get_next(O2_SYMBOL);
    assert(strcmp(arg->S, "123456") == 0);
    arg = o2_get_next(O2_BLOB);
    assert((arg->b.size == a_blob->size) &&
           memcmp(arg->b.data, a_blob->data, 15) == 0);
    arg = o2_get_next(O2_MIDI);
    assert(arg->m == a_midi_msg);
    arg = o2_get_next(O2_TRUE);
    assert(arg);
    arg = o2_get_next(O2_FALSE);
    assert(arg);
    arg = o2_get_next(O2_INT32);
    assert(arg);
    arg = o2_get_next(O2_NIL);
    assert(arg);
    arg = o2_get_next(O2_INT32);
    assert(arg->i == 1234);

    assert(strcmp(types, "icBhfdtsSbmTFINi") == 0);
    printf("service_many types=%s\n", types);
    got_the_message = true;
}


void service_manyp(o2_msg_data_ptr data, const char *types,
                   o2_arg_ptr *argv, int argc, const void *user_data)
{
    assert(argc == 16);
    assert(argv[0]->i == 1234);
    assert(argv[1]->c == 'Q');
    assert(argv[2]->B == true);
    assert(argv[3]->h == 12345LL);
    assert(argv[4]->f == 1234.5);
    assert(argv[5]->d == 1234.56);
    assert(argv[6]->t == 1234.567);
    assert(strcmp(argv[7]->s, "1234") == 0);
    assert(strcmp(argv[8]->S, "123456") == 0);
    assert((argv[9]->b.size == a_blob->size) &&
           memcmp(argv[9]->b.data, a_blob->data, 15) == 0);
    assert(argv[10]->m == a_midi_msg);
    assert(argv[15]->i == 1234);
    assert(strcmp(types, "icBhfdtsSbmTFINi") == 0);
    printf("service_manyp types=%s\n", types);
    got_the_message = true;
}


// this handles every message to service_two
//    we'll support two things: /two/i and /two/id
void service_two(o2_msg_data_ptr msg, const char *types,
                 o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(msg);
    if (strcmp(msg->address + 1, "two/i") == 0) {
        o2_arg_ptr arg = o2_get_next(O2_INT32);
        assert(arg && arg->i == 1234);
        printf("service_two types=%s arg=%d\n", types, arg->i);
    } else if (strcmp(msg->address + 1, "two/id") == 0) {
        o2_arg_ptr arg = o2_get_next(O2_INT32);
        int i;
        assert(arg && arg->i == 1234);
        i = arg->i;
        arg = o2_get_next(O2_DOUBLE);
        assert(arg->d == 1234.56);
        printf("service_two types=%s args=%d %g\n", types, i, arg->d);
    } else {
        assert(false);
    }
    got_the_message = true;
}


// this handles every message to service_two
//    we'll support two things: /two/i and /two/id
void service_three(o2_msg_data_ptr msg, const char *types,
                   o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(msg);
    if (strcmp(msg->address + 1, "three/i") == 0) {
        o2_arg_ptr arg = o2_get_next(O2_INT32);
        assert(arg->i == 1234);
        printf("service_three types=%s arg=%d\n", types, arg->i);
    } else if (strcmp(msg->address + 1, "three/id") == 0) {
        o2_arg_ptr arg = o2_get_next(O2_INT32);
        int i;
        assert(arg && arg->i == 1234);
        i = arg->i;
        arg = o2_get_next(O2_DOUBLE);
        assert(arg->d == 1234.56);
        printf("service_three types=%s args=%d %g\n", types, i, arg->d);
    } else {
        assert(false);
    }
    got_the_message = true;
}


// this handles every message to service_two
//    we'll support two things: /two/i and /two/id
void service_four(o2_msg_data_ptr msg, const char *types,
                  o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(msg);
    if (strcmp(msg->address + 1, "four/i") == 0) {
        o2_arg_ptr arg = o2_get_next(O2_INT32);
        assert(arg->i == 1234);
        printf("service_four types=%s arg=%d\n", types, arg->i);
    } else if (strcmp(msg->address + 1, "four/id") == 0) {
        o2_arg_ptr arg = o2_get_next(O2_INT32);
        int i;
        assert(arg && arg->i == 1234);
        i = arg->i;
        arg = o2_get_next(O2_DOUBLE);
        assert(arg->d == 1234.56);
        printf("service_four types=%s args=%d %g\n", types, i, arg->d);
    } else {
        assert(false);
    }
    got_the_message = true;
}


void service_nonetap(o2_msg_data_ptr data, const char *types,
                     o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    assert(strcmp(types, "") == 0);
    printf("service_nonetap types=%s\n", types);
    tapped_the_message = true;
}


void service_noneptap(o2_msg_data_ptr data, const char *types,
                   o2_arg_ptr *argv, int argc, const void *user_data)
{
    assert(strcmp(types, "") == 0);
    assert(argc == 0);
    printf("service_noneptap types=%s\n", types);
    tapped_the_message = true;
}


void service_itap(o2_msg_data_ptr data, const char *types,
               o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    assert(strcmp(types, "i") == 0);
    o2_arg_ptr arg = o2_get_next(O2_INT32);
    assert(arg->i == 1234);
    printf("service_itap types=%s int32=%d\n", types, arg->i);
    tapped_the_message = true;
}


void service_iptap(o2_msg_data_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, const void *user_data)
{
    assert(strcmp(types, "i") == 0);
    assert(argc == 1);
    assert(argv[0]->i == 1234);
    printf("service_iptap types=%s int32=%d\n", types, argv[0]->i);
    tapped_the_message = true;
}


void service_ctap(o2_msg_data_ptr data, const char *types,
               o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    assert(strcmp(types, "c") == 0);
    o2_arg_ptr arg = o2_get_next(O2_CHAR);
    assert(arg->c == 'Q');
    printf("service_ctap types=%s char=%c\n", types, arg->c);
    tapped_the_message = true;
}


void service_cptap(o2_msg_data_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, const void *user_data)
{
    assert(strcmp(types, "c") == 0);
    assert(argc == 1);
    assert(argv[0]->c == 'Q');
    printf("service_cptap types=%s char=%c\n", types, argv[0]->c);
    tapped_the_message = true;
}


void service_Btap(o2_msg_data_ptr data, const char *types,
               o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    assert(strcmp(types, "B") == 0);
    o2_arg_ptr arg = o2_get_next(O2_BOOL);
    assert(arg->B == true);
    printf("service_Btap types=%s bool=%d\n", types, arg->B);
    tapped_the_message = true;
}


void service_Bptap(o2_msg_data_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, const void *user_data)
{
    assert(strcmp(types, "B") == 0);
    assert(argc == 1);
    assert(argv[0]->B == true);
    printf("service_Bptap types=%s bool=%d\n", types, argv[0]->B);
    tapped_the_message = true;
}


void service_htap(o2_msg_data_ptr data, const char *types,
               o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    assert(strcmp(types, "h") == 0);
    o2_arg_ptr arg = o2_get_next(O2_INT64);
    assert(arg->h == 12345);
    // long long "coercion" to make gcc happy
    printf("service_htap types=%s int64=%lld\n", types,
           (long long) arg->h);
    tapped_the_message = true;
}


void service_hptap(o2_msg_data_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, const void *user_data)
{
    assert(strcmp(types, "h") == 0);
    assert(argc == 1);
    assert(argv[0]->h == 12345);
    // long long "coercion" to make gcc happy
    printf("service_hptap types=%s int64=%lld\n", types, 
           (long long) argv[0]->h);
    tapped_the_message = true;
}


void service_ftap(o2_msg_data_ptr data, const char *types,
               o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    assert(strcmp(types, "f") == 0);
    o2_arg_ptr arg = o2_get_next(O2_FLOAT);
    assert(arg->f == 1234.5);
    printf("service_ftap types=%s float=%g\n", types, arg->f);
    tapped_the_message = true;
}


void service_fptap(o2_msg_data_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, const void *user_data)
{
    assert(strcmp(types, "f") == 0);
    assert(argc == 1);
    assert(argv[0]->f == 1234.5);
    printf("service_fptap types=%s float=%g\n", types, argv[0]->f);
    tapped_the_message = true;
}


void service_dtap(o2_msg_data_ptr data, const char *types,
               o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    assert(strcmp(types, "d") == 0);
    o2_arg_ptr arg = o2_get_next(O2_DOUBLE);
    assert(arg->d == 1234.56);
    printf("service_dtap types=%s double=%g\n", types, arg->d);
    tapped_the_message = true;
}


void service_dptap(o2_msg_data_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, const void *user_data)
{
    assert(strcmp(types, "d") == 0);
    assert(argc == 1);
    assert(argv[0]->d == 1234.56);
    printf("service_dptap types=%s double=%g\n", types, argv[0]->d);
    tapped_the_message = true;
}


void service_ttap(o2_msg_data_ptr data, const char *types,
               o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    assert(strcmp(types, "t") == 0);
    o2_arg_ptr arg = o2_get_next(O2_TIME);
    assert(arg->t == 1234.567);
    printf("service_ttap types=%s time=%g\n", types, arg->t);
    tapped_the_message = true;
}


void service_tptap(o2_msg_data_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, const void *user_data)
{
    assert(strcmp(types, "t") == 0);
    assert(argc == 1);
    assert(argv[0]->t == 1234.567);
    printf("service_tptap types=%s time=%g\n", types, argv[0]->t);
    tapped_the_message = true;
}


void service_stap(o2_msg_data_ptr data, const char *types,
               o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    assert(strcmp(types, "s") == 0);
    o2_arg_ptr arg = o2_get_next(O2_STRING);
    assert(strcmp(arg->s, "1234") == 0);
    printf("service_stap types=%s string=%s\n", types, arg->s);
    tapped_the_message = true;
}


void service_sptap(o2_msg_data_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, const void *user_data)
{
    assert(strcmp(types, "s") == 0);
    assert(argc == 1);
    assert(strcmp(argv[0]->s, "1234") == 0);
    printf("service_sptap types=%s string=%s\n", types, argv[0]->s);
    tapped_the_message = true;
}


void service_Stap(o2_msg_data_ptr data, const char *types,
               o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    assert(strcmp(types, "S") == 0);
    o2_arg_ptr arg = o2_get_next(O2_SYMBOL);
    assert(strcmp(arg->S, "123456") == 0);
    printf("service_Stap types=%s symbol=%s\n", types, arg->S);
    tapped_the_message = true;
}


void service_Sptap(o2_msg_data_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, const void *user_data)
{
    assert(strcmp(types, "S") == 0);
    assert(argc == 1);
    assert(strcmp(argv[0]->S, "123456") == 0);
    printf("service_Sptap types=%s symbol=%s\n", types, argv[0]->S);
    tapped_the_message = true;
}


void service_btap(o2_msg_data_ptr data, const char *types,
               o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    assert(strcmp(types, "b") == 0);
    o2_arg_ptr arg = o2_get_next(O2_BLOB);
    assert(arg->b.size = a_blob->size &&
           memcmp(arg->b.data, a_blob->data, 15) == 0);
    printf("service_btap types=%s blob=%p\n", types, &(arg->b));
    tapped_the_message = true;
}


void service_bptap(o2_msg_data_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, const void *user_data)
{
    assert(strcmp(types, "b") == 0);
    assert(argc == 1);
    assert(argv[0]->b.size = a_blob->size &&
           memcmp(argv[0]->b.data, a_blob->data, 15) == 0);
    printf("service_bptap types=%s blob=%p\n", types, &argv[0]->b);
    tapped_the_message = true;
}


void service_mtap(o2_msg_data_ptr data, const char *types,
               o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    assert(strcmp(types, "m") == 0);
    o2_arg_ptr arg = o2_get_next(O2_MIDI);
    assert(arg->m == a_midi_msg);
    printf("service_mtap types=%s midi = %2x %2x %2x\n", types,
           (arg->m >> 16) & 0xff, (arg->m >> 8) & 0xff, arg->m & 0xff);
    tapped_the_message = true;
}


void service_mptap(o2_msg_data_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, const void *user_data)
{
    assert(strcmp(types, "m") == 0);
    assert(argc == 1);
    o2_arg_ptr arg = argv[0];
    assert(arg->m == a_midi_msg);
    printf("service_mptap types=%s midi = %2x %2x %2x\n", types,
           (arg->m >> 16) & 0xff, (arg->m >> 8) & 0xff, arg->m & 0xff);
    tapped_the_message = true;
}


void service_Ttap(o2_msg_data_ptr data, const char *types,
               o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    assert(strcmp(types, "T") == 0);
    printf("service_Ttap types=%s\n", types);
    tapped_the_message = true;
}


void service_Tptap(o2_msg_data_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, const void *user_data)
{
    assert(strcmp(types, "T") == 0);
    assert(argc == 1);
    printf("service_Tptap types=%s\n", types);
    tapped_the_message = true;
}


void service_Ftap(o2_msg_data_ptr data, const char *types,
               o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    assert(strcmp(types, "F") == 0);
    printf("service_Ftap types=%s\n", types);
    tapped_the_message = true;
}


void service_Fptap(o2_msg_data_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, const void *user_data)
{
    assert(strcmp(types, "F") == 0);
    assert(argc == 1);
    printf("service_Fptap types=%s\n", types);
    tapped_the_message = true;
}


void service_Itap(o2_msg_data_ptr data, const char *types,
               o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    assert(strcmp(types, "I") == 0);
    printf("service_Itap types=%s\n", types);
    tapped_the_message = true;
}


void service_Iptap(o2_msg_data_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, const void *user_data)
{
    assert(strcmp(types, "I") == 0);
    assert(argc == 1);
    printf("service_Iptap types=%s\n", types);
    tapped_the_message = true;
}


void service_Ntap(o2_msg_data_ptr data, const char *types,
               o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    assert(strcmp(types, "N") == 0);
    printf("service_Ntap types=%s\n", types);
    tapped_the_message = true;
}


void service_Nptap(o2_msg_data_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, const void *user_data)
{
    assert(strcmp(types, "N") == 0);
    assert(argc == 1);
    printf("service_Nptap types=%s\n", types);
    tapped_the_message = true;
}


void service_manytap(o2_msg_data_ptr data, const char *types,
                  o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(data);
    o2_arg_ptr arg = o2_get_next(O2_INT32);
    assert(arg->i == 1234);
    arg = o2_get_next(O2_CHAR);
    assert(arg->c == 'Q');
    arg = o2_get_next(O2_BOOL);
    assert(arg->B == true);
    arg = o2_get_next(O2_INT64);
    assert(arg->h == 12345LL);
    arg = o2_get_next(O2_FLOAT);
    assert(arg->f == 1234.5);
    arg = o2_get_next(O2_DOUBLE);
    assert(arg->d == 1234.56);
    arg = o2_get_next(O2_TIME);
    assert(arg->t == 1234.567);
    arg = o2_get_next(O2_STRING);
    assert(strcmp(arg->s, "1234") == 0);
    arg = o2_get_next(O2_SYMBOL);
    assert(strcmp(arg->S, "123456") == 0);
    arg = o2_get_next(O2_BLOB);
    assert((arg->b.size == a_blob->size) &&
           memcmp(arg->b.data, a_blob->data, 15) == 0);
    arg = o2_get_next(O2_MIDI);
    assert(arg->m == a_midi_msg);
    arg = o2_get_next(O2_TRUE);
    assert(arg);
    arg = o2_get_next(O2_FALSE);
    assert(arg);
    arg = o2_get_next(O2_INT32);
    assert(arg);
    arg = o2_get_next(O2_NIL);
    assert(arg);
    arg = o2_get_next(O2_INT32);
    assert(arg->i == 1234);

    assert(strcmp(types, "icBhfdtsSbmTFINi") == 0);
    printf("service_manytap types=%s\n", types);
    tapped_the_message = true;
}


void service_manyptap(o2_msg_data_ptr data, const char *types,
                   o2_arg_ptr *argv, int argc, const void *user_data)
{
    assert(argc == 16);
    assert(argv[0]->i == 1234);
    assert(argv[1]->c == 'Q');
    assert(argv[2]->B == true);
    assert(argv[3]->h == 12345LL);
    assert(argv[4]->f == 1234.5);
    assert(argv[5]->d == 1234.56);
    assert(argv[6]->t == 1234.567);
    assert(strcmp(argv[7]->s, "1234") == 0);
    assert(strcmp(argv[8]->S, "123456") == 0);
    assert((argv[9]->b.size == a_blob->size) &&
           memcmp(argv[9]->b.data, a_blob->data, 15) == 0);
    assert(argv[10]->m == a_midi_msg);
    assert(argv[15]->i == 1234);
    assert(strcmp(types, "icBhfdtsSbmTFINi") == 0);
    printf("service_manyptap types=%s\n", types);
    tapped_the_message = true;
}


// this handles every message to service_two
//    we'll support two things: /two/i and /two/id
void service_twotap(o2_msg_data_ptr msg, const char *types,
                 o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(msg);
    if (strcmp(msg->address + 1, "twotap/i") == 0) {
        o2_arg_ptr arg = o2_get_next(O2_INT32);
        assert(arg && arg->i == 1234);
        printf("service_twotap types=%s arg=%d\n", types, arg->i);
    } else if (strcmp(msg->address + 1, "twotap/id") == 0) {
        o2_arg_ptr arg = o2_get_next(O2_INT32);
        int i;
        assert(arg && arg->i == 1234);
        i = arg->i;
        arg = o2_get_next(O2_DOUBLE);
        assert(arg->d == 1234.56);
        printf("service_twotap types=%s args=%d %g\n", types, i, arg->d);
    } else {
        assert(false);
    }
    tapped_the_message = true;
}


// this handles every message to service_two
//    we'll support two things: /two/i and /two/id
void service_threetap(o2_msg_data_ptr msg, const char *types,
                   o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(msg);
    if (strcmp(msg->address + 1, "threetap/i") == 0) {
        o2_arg_ptr arg = o2_get_next(O2_INT32);
        assert(arg->i == 1234);
        printf("service_threetap types=%s arg=%d\n", types, arg->i);
    } else if (strcmp(msg->address + 1, "threetap/id") == 0) {
        o2_arg_ptr arg = o2_get_next(O2_INT32);
        int i;
        assert(arg && arg->i == 1234);
        i = arg->i;
        arg = o2_get_next(O2_DOUBLE);
        assert(arg->d == 1234.56);
        printf("service_threetap types=%s args=%d %g\n", types, i, arg->d);
    } else {
        assert(false);
    }
    tapped_the_message = true;
}


// this handles every message to service_two
//    we'll support two things: /two/i and /two/id
void service_fourtap(o2_msg_data_ptr msg, const char *types,
                  o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(msg);
    if (strcmp(msg->address + 1, "fourtap/i") == 0) {
        o2_arg_ptr arg = o2_get_next(O2_INT32);
        assert(arg->i == 1234);
        printf("service_fourtap types=%s arg=%d\n", types, arg->i);
    } else if (strcmp(msg->address + 1, "fourtap/id") == 0) {
        o2_arg_ptr arg = o2_get_next(O2_INT32);
        int i;
        assert(arg && arg->i == 1234);
        i = arg->i;
        arg = o2_get_next(O2_DOUBLE);
        assert(arg->d == 1234.56);
        printf("service_fourtap types=%s args=%d %g\n", types, i, arg->d);
    } else {
        assert(false);
    }
    tapped_the_message = true;
}


void send_the_message()
{
    while (!got_the_message) {
        o2_poll();
    }
    while (!tapped_the_message) {
        o2_poll();
    }
    got_the_message = false;
    tapped_the_message = false;
}


int main(int argc, const char * argv[])
{
    a_blob = (o2_blob_ptr) malloc(20);
    a_blob->size = 15;
    memcpy(a_blob->data, "This is a blob", 15);

    a_midi_msg = (0x90 << 16) + (60 << 8) + 100;

    o2_initialize("test");    
    o2_service_new("one");
    o2_service_new("two");
    o2_service_new("three");
    o2_service_new("four");

    o2_service_new("testtap");
    o2_service_new("onetap");
    o2_service_new("twotap");


    o2_tap("test", "testtap");
    o2_tap("one", "onetap");
    o2_tap("two", "twotap");
    o2_tap("three", "threetap");
    o2_tap("four", "fourtap");

    // should be ok to make tapper AFTER the o2_tap call...
    o2_service_new("threetap");
    o2_service_new("fourtap");

    o2_method_new("/one/none", "", &service_none, NULL, false, false);
    o2_method_new("/onetap/none", "", &service_nonetap, NULL, false, false);
    o2_method_new("/one/nonep", "", &service_nonep, NULL, false, true);
    o2_method_new("/onetap/nonep", "", &service_noneptap, NULL, false, true);
    o2_method_new("/one/i", "i", &service_i, NULL, false, false);
    o2_method_new("/onetap/i", "i", &service_itap, NULL, false, false);
    o2_method_new("/one/ip", "i", &service_ip, NULL, false, true);
    o2_method_new("/onetap/ip", "i", &service_iptap, NULL, false, true);
    o2_method_new("/one/c", "c", &service_c, NULL, false, false);
    o2_method_new("/onetap/c", "c", &service_ctap, NULL, false, false);
    o2_method_new("/one/cp", "c", &service_cp, NULL, false, true);
    o2_method_new("/onetap/cp", "c", &service_cptap, NULL, false, true);
    o2_method_new("/one/B", "B", &service_B, NULL, false, false);
    o2_method_new("/onetap/B", "B", &service_Btap, NULL, false, false);
    o2_method_new("/one/Bp", "B", &service_Bp, NULL, false, true);
    o2_method_new("/onetap/Bp", "B", &service_Bptap, NULL, false, true);
    o2_method_new("/one/h", "h", &service_h, NULL, false, false);
    o2_method_new("/onetap/h", "h", &service_htap, NULL, false, false);
    o2_method_new("/one/hp", "h", &service_hp, NULL, false, true);
    o2_method_new("/onetap/hp", "h", &service_hptap, NULL, false, true);
    o2_method_new("/one/f", "f", &service_f, NULL, false, false);
    o2_method_new("/onetap/f", "f", &service_ftap, NULL, false, false);
    o2_method_new("/one/fp", "f", &service_fp, NULL, false, true);
    o2_method_new("/onetap/fp", "f", &service_fptap, NULL, false, true);
    o2_method_new("/one/d", "d", &service_d, NULL, false, false);
    o2_method_new("/onetap/d", "d", &service_dtap, NULL, false, false);
    o2_method_new("/one/dp", "d", &service_dp, NULL, false, true);
    o2_method_new("/onetap/dp", "d", &service_dptap, NULL, false, true);
    o2_method_new("/one/t", "t", &service_t, NULL, false, false);
    o2_method_new("/onetap/t", "t", &service_ttap, NULL, false, false);
    o2_method_new("/one/tp", "t", &service_tp, NULL, false, true);
    o2_method_new("/onetap/tp", "t", &service_tptap, NULL, false, true);
    o2_method_new("/one/s", "s", &service_s, NULL, false, false);
    o2_method_new("/onetap/s", "s", &service_stap, NULL, false, false);
    o2_method_new("/one/sp", "s", &service_sp, NULL, false, true);
    o2_method_new("/onetap/sp", "s", &service_sptap, NULL, false, true);
    o2_method_new("/one/S", "S", &service_S, NULL, false, false);
    o2_method_new("/onetap/S", "S", &service_Stap, NULL, false, false);
    o2_method_new("/one/Sp", "S", &service_Sp, NULL, false, true);
    o2_method_new("/onetap/Sp", "S", &service_Sptap, NULL, false, true);
    o2_method_new("/one/b", "b", &service_b, NULL, false, false);
    o2_method_new("/onetap/b", "b", &service_btap, NULL, false, false);
    o2_method_new("/one/bp", "b", &service_bp, NULL, false, true);
    o2_method_new("/onetap/bp", "b", &service_bptap, NULL, false, true);
    o2_method_new("/one/m", "m", &service_m, NULL, false, false);
    o2_method_new("/onetap/m", "m", &service_mtap, NULL, false, false);
    o2_method_new("/one/mp", "m", &service_mp, NULL, false, true);
    o2_method_new("/onetap/mp", "m", &service_mptap, NULL, false, true);
    o2_method_new("/one/T", "T", &service_T, NULL, false, false);
    o2_method_new("/onetap/T", "T", &service_Ttap, NULL, false, false);
    o2_method_new("/one/Tp", "T", &service_Tp, NULL, false, true);
    o2_method_new("/onetap/Tp", "T", &service_Tptap, NULL, false, true);
    o2_method_new("/one/F", "F", &service_F, NULL, false, false);
    o2_method_new("/onetap/F", "F", &service_Ftap, NULL, false, false);
    o2_method_new("/one/Fp", "F", &service_Fp, NULL, false, true);
    o2_method_new("/onetap/Fp", "F", &service_Fptap, NULL, false, true);
    o2_method_new("/one/I", "I", &service_I, NULL, false, false);
    o2_method_new("/onetap/I", "I", &service_Itap, NULL, false, false);
    o2_method_new("/one/Ip", "I", &service_Ip, NULL, false, true);
    o2_method_new("/onetap/Ip", "I", &service_Iptap, NULL, false, true);
    o2_method_new("/one/N", "N", &service_N, NULL, false, false);
    o2_method_new("/onetap/N", "N", &service_Ntap, NULL, false, false);
    o2_method_new("/one/Np", "N", &service_Np, NULL, false, true);
    o2_method_new("/onetap/Np", "N", &service_Nptap, NULL, false, true);
    o2_method_new("/one/many", "icBhfdtsSbmTFINi", &service_many,
                  NULL, false, false);
    o2_method_new("/onetap/many", "icBhfdtsSbmTFINi", &service_manytap,
                  NULL, false, false);
    o2_method_new("/one/manyp", "icBhfdtsSbmTFINi", &service_manyp,
                  NULL, false, true);
    o2_method_new("/onetap/manyp", "icBhfdtsSbmTFINi", &service_manyptap,
                  NULL, false, true);
    o2_method_new("/two", NULL, &service_two, NULL, false, false);
    o2_method_new("/twotap", NULL, &service_twotap, NULL, false, false);
    o2_method_new("/three", "i", &service_three, NULL, false, true);
    o2_method_new("/threetap", "i", &service_threetap, NULL, false, true);
    o2_method_new("/four", "i", &service_four, NULL, true, true);
    o2_method_new("/fourtap", "i", &service_fourtap, NULL, true, true);

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
    printf("DONE\n");
    o2_finish();
    return 0;
}
