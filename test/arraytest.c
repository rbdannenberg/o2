// arraytest.c -- test array/vector messages
//

// What does this test?
// 1. sending typestring [i] (an array with one integer)
// 2. sending typestring [] (an array with no integers)
// 3. sending typestring [ii] (an array with 2 integers)
// 4. sending typestring [xixdx] where x is one of: ihfdcBbtsSmTFIN
//    (just in case a mix of sizes causes problems)
// 5. sending typestring i[ih][fdt]d to test multiple arrays
// 6. sending typestring [ddddd...] where there are 1 to 100 d's
// 7. sending typestring vi (with length 0 to 100)
// 8. sending typestring vf (with length 0 to 100)
// 9. sending typestring vh (with length 0 to 100)
// 10. sending typestring vd (with length 0 to 100)
// 11. sending typestring vt (with length 0 to 100)
// 12. sending typestring ifvtif (with vector length 0 to 100)
//     (this last test is an extra check for embedded vectors)
// 13. sending typestring vivd (with lenghts 0 to 100)
//     (another test to look for bugs in allocation, receiving multiple
//      vectors in one message)
// 14. sending i[xxxx...]i where x is in ihfdt and there are 0 to 100
//     of them AND the data is received as a vector using coercion
// 15. sending ivxi where x is in ihfdt and there are 0 to 100
//     of them AND the data is received as an array using coercion

#include <stdio.h>
#include "o2.h"
#include "assert.h"
#include "string.h"

int got_the_message = FALSE;

o2_blob_ptr a_blob;
uint32_t a_midi_msg;

char xtype = 0; // used to tell handler what type(s) to expect when
char ytype = 0; // used to tell handler what type(s) to coerce to
// a handler is used to accept multiple message types

int arg_count = 0; // used to tell handler how many is correct

// 1. sending typestring [i] (an array with one integer)
// 
void service_ai(o2_msg_data_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_extract_start(data);

    assert(*types++ == '[');
    o2_arg_ptr arg = o2_get_next('[');
    assert(arg == o2_got_start_array);

    assert(*types++ == 'i');
    arg = o2_get_next('i');
    assert(arg->i == 3456);

    assert(*types++ == ']');
    arg = o2_get_next(']');
    assert(arg == o2_got_end_array);

    assert(*types == 0);
    got_the_message = TRUE;
}

// 2. sending typestring [] (an array with no integers)
// 
void service_a(o2_msg_data_ptr data, const char *types,
               o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_extract_start(data);

    assert(*types++ == '[');
    o2_arg_ptr arg = o2_get_next('[');
    assert(arg == o2_got_start_array);

    assert(*types++ == ']');
    arg = o2_get_next(']');
    assert(arg == o2_got_end_array);

    assert(*types == 0);
    got_the_message = TRUE;
}


// 3. sending typestring [ii] (an array with 2 integers)
// 
void service_aii(o2_msg_data_ptr data, const char *types,
                 o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_extract_start(data);

    assert(*types++ == '[');
    o2_arg_ptr arg = o2_get_next('[');
    assert(arg == o2_got_start_array);

    assert(*types++ == 'i');
    arg = o2_get_next('i');
    assert(arg->i == 123);

    assert(*types++ == 'i');
    arg = o2_get_next('i');
    assert(arg->i == 234);

    assert(*types++ == ']');
    arg = o2_get_next(']');
    assert(arg == o2_got_end_array);

    assert(*types == 0);
    got_the_message = TRUE;
}


void check_val(char actual_type)
{
    o2_arg_ptr arg;
    assert(actual_type == xtype);
    arg = o2_get_next(xtype);
    switch (xtype) {
        case O2_INT32:
            assert(arg->i == 1234);
            break;
        case O2_INT64:
            assert(arg->h == 12345);
            break;
        case O2_FLOAT:
            assert(arg->f == 1234.56F);
            break;
        case O2_DOUBLE:
            assert(arg->d == 1234.567);
            break;
        case O2_TIME:
            assert(arg->t == 2345.678);
            break;
        case O2_BOOL:
            assert(arg->B);
            break;
        case O2_CHAR:
            assert(arg->c == '$');
            break;
        case O2_TRUE:
        case O2_FALSE:
        case O2_INFINITUM:
        case O2_NIL:
            assert(arg);
            break;
        case O2_BLOB:
            assert(arg->b.size == a_blob->size &&
                   memcmp(arg->b.data, a_blob->data, 15) == 0);
            break;
        case O2_STRING:
            assert(strcmp(arg->S, "This is a string") == 0);
            break;
        case O2_SYMBOL:
            assert(strcmp(arg->S, "This is a symbol") == 0);
            break;
        case O2_MIDI:
            assert(arg->m == a_midi_msg);
            break;
        default:
            assert(FALSE);
    }
    return;
}

void icheck(char typ, int val)
{
    assert(typ == 'i');
    o2_arg_ptr arg = o2_get_next('i');
    assert(arg && arg->i == val);
}


void hcheck(char typ, int64_t val)
{
    assert(typ == 'h');
    o2_arg_ptr arg = o2_get_next('h');
    assert(arg && arg->h == val);
}


void dcheck(char typ, double val)
{
    assert(typ == 'd');
    o2_arg_ptr arg = o2_get_next('d');
    assert(arg && arg->d == val);
}


void tcheck(char typ, double val)
{
    assert(typ == 't');
    o2_arg_ptr arg = o2_get_next('t');
    assert(arg && arg->t == val);
}


void fcheck(char typ, float val)
{
    assert(typ == 'f');
    o2_arg_ptr arg = o2_get_next('f');
    assert(arg && arg->f == val);
}


void acheck(char typ)
{
    assert(typ == '[');
    o2_arg_ptr arg = o2_get_next('[');
    assert(arg && arg == o2_got_start_array);
}

void zcheck(char typ)
{
    assert(typ == ']');
    o2_arg_ptr arg = o2_get_next(']');
    assert(arg && arg == o2_got_end_array);
}




// 4. sending typestring [xixdx] where x is one of: ihfdcBbtsSmTFIN
//    (just in case a mix of sizes causes problems); the global
//    char xtype; provides the value of x
// 
void service_xixdx(o2_msg_data_ptr data, const char *types,
                   o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_extract_start(data);

    acheck(*types++);
    check_val(*types++);
    icheck(*types++, 456);
    check_val(*types++);
    dcheck(*types++, 234.567);
    check_val(*types++);
    zcheck(*types++);

    assert(*types == 0);
    got_the_message = TRUE;
}


// 5. sending typestring i[ih][fdt]d to test multiple arrays
//
void service_2arrays(o2_msg_data_ptr data, const char *types,
                     o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_extract_start(data);

    icheck(*types++, 456);

    acheck(*types++);
    icheck(*types++, 1234);
    hcheck(*types++, 12345);
    zcheck(*types++);

    acheck(*types++);
    fcheck(*types++, 1234.56F);
    dcheck(*types++, 1234.567);
    tcheck(*types++, 2345.678);
    zcheck(*types++);

    dcheck(*types++, 1234.567);

    assert(*types == 0);
    got_the_message = TRUE;
}


// 6. sending typestring [ddddd...] where there are 1 to 100 d's
//
void service_bigarray(o2_msg_data_ptr data, const char *types,
                      o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_extract_start(data);
    acheck(*types++);
    for (int i = 0; i < arg_count; i++) {
        dcheck(*types++, 123.456 + i);
    }
    zcheck(*types++);
    assert(*types == 0); // got all of typestring
    got_the_message = TRUE;
}


// 7. sending typestring vi (with length 0 to 100)
//
void service_vi(o2_msg_data_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_extract_start(data);
    assert(*types++ == 'v');
    o2_arg_ptr arg = o2_get_next('v');
    assert(arg);
    assert(*types++ == 'i');
    o2_arg_ptr arg2 = o2_get_next('i');
    assert(arg2);
    assert(arg2 == arg);
    assert(arg->v.len == arg_count);
    assert(arg->v.typ == 'i');
    for (int i = 0; i < arg_count; i++) {
        assert(arg->v.vi);
        assert(arg->v.vi[i] == 1234 + i);
    }
    assert(*types == 0); // got all of typestring
    got_the_message = TRUE;
}


// 8. sending typestring vf (with length 0 to 100)
//
void service_vf(o2_msg_data_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_extract_start(data);
    assert(*types++ == 'v');
    o2_arg_ptr arg = o2_get_next('v');
    assert(arg);
    assert(*types++ == 'f');
    o2_arg_ptr arg2 = o2_get_next('f');
    assert(arg2);
    assert(arg2 == arg);
    assert(arg->v.len == arg_count);
    assert(arg->v.typ == 'f');
    for (int i = 0; i < arg_count; i++) {
        float correct = 123.456F + i;
        assert(arg->v.vf);
        assert(arg->v.vf[i] == correct);
    }
    assert(*types == 0); // got all of typestring
    got_the_message = TRUE;
}


// 9. sending typestring vh (with length 0 to 100)
void service_vh(o2_msg_data_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_extract_start(data);
    assert(*types++ == 'v');
    o2_arg_ptr arg = o2_get_next('v');
    assert(arg);
    assert(*types++ == 'h');
    o2_arg_ptr arg2 = o2_get_next('h');
    assert(arg2);
    assert(arg2 == arg);
    assert(arg->v.len == arg_count);
    assert(arg->v.typ == 'h');
    for (int i = 0; i < arg_count; i++) {
        int64_t correct = 123456 + i;
        assert(arg->v.vh);
        assert(arg->v.vh[i] == correct);
    }
    assert(*types == 0); // got all of typestring
    got_the_message = TRUE;
}


// 10. sending typestring vd (with length 0 to 100)
void service_vd(o2_msg_data_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_extract_start(data);
    assert(*types++ == 'v');
    o2_arg_ptr arg = o2_get_next('v');
    assert(arg);
    assert(*types++ == 'd');
    o2_arg_ptr arg2 = o2_get_next('d');
    assert(arg2);
    assert(arg2 == arg);
    assert(arg->v.len == arg_count);
    assert(arg->v.typ == 'd');
    for (int i = 0; i < arg_count; i++) {
        double correct = 1234.567 + i;
        assert(arg->v.vd);
        assert(arg->v.vd[i] == correct);
    }
    assert(*types == 0); // got all of typestring
    got_the_message = TRUE;
}


// 12. sending typestring ifv?if (with vector length 0 to 100)
//     (this last test is an extra check for embedded vectors)
void service_ifvxif(o2_msg_data_ptr data, const char *types,
                    o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_extract_start(data);
    icheck(*types++, 2345);
    fcheck(*types++, 345.67F);
    
    assert(*types++ == 'v');
    o2_arg_ptr arg = o2_get_next('v');
    assert(arg);
    assert(*types++ == xtype);
    o2_arg_ptr arg2 = o2_get_next(xtype);
    assert(arg2);
    assert(arg2 == arg);
    assert(arg->v.len == arg_count);
    assert(arg->v.typ == xtype);
    for (int i = 0; i < arg_count; i++) {
        assert(arg->v.vd);
        switch (xtype) {
            case 'i': {
                assert(arg->v.vi[i] == 1234 + i);
                break;
            }
            case 'h': {    
                assert(arg->v.vh[i] == 123456 + i);
                break;
            }
            case 'f': {
                assert(arg->v.vf[i] == 123.456F + i);
                break;
            }
            case 'd': {    
                assert(arg->v.vd[i] == 1234.567 + i);
                break;
            }
            default:
                assert(FALSE);
                break;
        }
    }

    icheck(*types++, 4567);
    fcheck(*types++, 567.89F);
    assert(*types == 0); // got all of typestring
    got_the_message = TRUE;
}


// 13. sending typestring vivd (with lenghts 0 to 100)
//     (another test to look for bugs in allocation, receiving multiple
//      vectors in one message)
void service_vivd(o2_msg_data_ptr data, const char *types,
                  o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_extract_start(data);
    assert(*types++ == 'v');
    o2_arg_ptr arg = o2_get_next('v');
    assert(arg);
    assert(*types++ == 'i');
    o2_arg_ptr arg2 = o2_get_next('i');
    assert(arg2);
    assert(arg2 == arg);
    assert(arg->v.len == arg_count);
    assert(arg->v.typ == 'i');
    for (int i = 0; i < arg_count; i++) {
        assert(arg->v.vi);
        assert(arg->v.vi[i] == 1234 + i);
    }
    assert(*types++ == 'v');
    arg = o2_get_next('v');
    assert(arg);
    assert(*types++ == 'd');
    arg2 = o2_get_next('d');
    assert(arg2);
    assert(arg2 == arg);
    assert(arg->v.len == arg_count);
    assert(arg->v.typ == 'd');
    for (int i = 0; i < arg_count; i++) {
        assert(arg->v.vi);
        assert(arg->v.vd[i] == 1234.567 + i);
    }
    assert(*types == 0); // got all of typestring
    got_the_message = TRUE;
}


// 14. sending i[xxxx...]i where x is in ihfdt and there are 0 to 100
//     of them AND the data is received as a vector using coercion
void service_coerce(o2_msg_data_ptr data, const char *types,
                    o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_extract_start(data);
    icheck(*types++, 5678);
    o2_arg_ptr arg = o2_get_next('v');
    assert(*types++ == '[');
    o2_arg_ptr arg2 = o2_get_next(ytype);
    assert(arg2);
    assert(arg2 == arg);
    assert(arg->v.len == arg_count);
    assert(arg->v.typ == ytype);
    for (int i = 0; i < arg_count; i++) {
        assert(arg->v.vi);
        double expected;
        switch (xtype) {
            case 'i': expected = 543 + i; break;
            case 'h': expected = 543 + i; break;
            case 'f': expected = (float) (543.21 + i); break;
            case 'd': expected = 543.21 + i; break;
        }
        switch (ytype) {
            case 'i': assert(arg->v.vi[i] == (int32_t) expected); break;
            case 'h': assert(arg->v.vh[i] == (int64_t) expected); break;
            case 'f': assert(arg->v.vf[i] == (float) expected); break;
            case 'd': assert(arg->v.vd[i] == expected); break;
            default: assert(FALSE);
        }
        assert(*types++ == xtype);
    }
    assert(*types++ == ']');
    arg2 = o2_get_next(']');
    assert(arg2 == o2_got_end_array);
    icheck(*types++, 6789);
    assert(*types == 0); // got all of typestring
    got_the_message = TRUE;
}


// 15. sending ivxi where x is in ihfdt and there are 0 to 100
//     of them AND the data is received as an array using coercion
void service_coerce2(o2_msg_data_ptr data, const char *types,
                    o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_extract_start(data);
    icheck(*types++, 5678);
    fcheck(*types++, 567.89F);
    assert(*types++ == 'v');
    assert(*types++ == xtype);
    o2_arg_ptr arg = o2_get_next('[');
    assert(arg);
    for (int i = 0; i < arg_count; i++) {
        arg = o2_get_next(ytype);
        assert(arg);
        double expected;
        switch (xtype) {
            case 'i': {
                expected = 1234 + i;
                break;
            }
            case 'h': {
                expected = 123456 + i;
                break;
            }
            case 'f': {
                expected = 123.456F + i;
                break;
            }
            case 'd': {
                expected = 1234.567 + i;
                break;
            }
            default:
                assert(FALSE);
                break;
        }

        switch (ytype) {
            case 'i': assert(arg->i == (int32_t) expected); break;
            case 'h': assert(arg->h == (int64_t) expected); break;
            case 'f': assert(arg->f == (float) expected); break;
            case 'd': case 't':
                assert(arg->d == expected); break;
            default: assert(FALSE);
        }
    }
    arg = o2_get_next(']');
    assert(arg == o2_got_end_array);
    icheck(*types++, 6789);
    fcheck(*types++, 567.89F);
    assert(*types == 0); // got all of typestring
    got_the_message = TRUE;
}


void send_the_message()
{
    for (int i = 0; i < 1000000; i++) {
        if (got_the_message) break;
        o2_poll();
    }
    assert(got_the_message);
    got_the_message = FALSE;
}


// add a parameter of type xtype
void add_x_parameter()
{
    switch (xtype) {
        case O2_INT32:
            o2_add_int32(1234);
            break;
        case O2_INT64:
            o2_add_int64(12345);
            break;
        case O2_FLOAT:
            o2_add_float(1234.56F);
            break;
        case O2_DOUBLE:
            o2_add_double(1234.567);
            break;
        case O2_TIME:
            o2_add_time(2345.678);
            break;
        case O2_CHAR:
            o2_add_char('$');
            break;
        case O2_BOOL:
            o2_add_bool(TRUE);
            break;
        case O2_TRUE:
            o2_add_true();
            break;
        case O2_FALSE:
            o2_add_false();
            break;
        case O2_INFINITUM:
            o2_add_infinitum();
            break;
        case O2_NIL:
            o2_add_nil();
            break;
        case O2_BLOB:
            o2_add_blob(a_blob);
            break;
        case O2_STRING:
            o2_add_string("This is a string");
            break;
        case O2_SYMBOL:
            o2_add_symbol("This is a symbol");
            break;
        case O2_MIDI:
            o2_add_midi(a_midi_msg);
            break;
        default:
            assert(FALSE);
    }
    return;
}
    


int main(int argc, const char * argv[])
{
    a_blob = malloc(20);
    a_blob->size = 15;
    memcpy(a_blob->data, "This is a blob", 15);

    a_midi_msg = (0x90 << 16) + (60 << 8) + 100;

    o2_initialize("test");    
    o2_service_new("one");

    o2_method_new("/one/service_ai", "[i]", &service_ai, NULL, FALSE, FALSE);
    o2_method_new("/one/service_a", "[]", &service_a, NULL, FALSE, FALSE);
    o2_method_new("/one/service_aii", "[ii]", &service_aii,
                  NULL, FALSE, FALSE);
    // [xixdx] where x is one of: hfcBbtsSmTFIN
    char *xtypes = "ihfdcBbtsSmTFIN";
    for (char *xtp = xtypes; *xtp; xtp++) {
        xtype = *xtp;
        char type_string[32];
        snprintf(type_string, 32, "[%ci%cd%c]", xtype, xtype, xtype);
        char address[32];
        snprintf(address, 32, "/one/service_%ci%cd%c", xtype, xtype, xtype);
        o2_method_new(address, type_string, &service_xixdx,
                      NULL, FALSE, FALSE);
    }
    o2_method_new("/one/service_2arrays", "i[ih][fdt]d", &service_2arrays, NULL, FALSE, FALSE);
    // use NULL for type string to disable type-string checking
    o2_method_new("/one/service_bigarray", NULL, &service_bigarray, NULL, FALSE, FALSE);
    o2_method_new("/one/service_vi", NULL, &service_vi, NULL, FALSE, FALSE);
    o2_method_new("/one/service_vf", NULL, &service_vf, NULL, FALSE, FALSE);
    o2_method_new("/one/service_vh", NULL, &service_vh, NULL, FALSE, FALSE);
    o2_method_new("/one/service_vd", NULL, &service_vd, NULL, FALSE, FALSE);
    o2_method_new("/one/service_ifvxif", NULL,
                  &service_ifvxif, NULL, FALSE, FALSE);
    o2_method_new("/one/service_vivd", NULL, &service_vivd, NULL, FALSE, FALSE);
    o2_method_new("/one/service_coerce", NULL, &service_coerce, NULL, FALSE, FALSE);
    o2_method_new("/one/service_coerce2", NULL, &service_coerce2, NULL, FALSE, FALSE);
    
    o2_send_start();
    o2_add_start_array();
    o2_add_int32(3456);
    o2_add_end_array();
    o2_send_finish(0, "/one/service_ai", TRUE);
    send_the_message();
    printf("DONE sending [3456]\n");

    o2_send_start();
    o2_add_start_array();
    o2_add_end_array();
    o2_send_finish(0, "/one/service_a", TRUE);
    send_the_message();
    printf("DONE sending []\n");

    o2_send_start();
    o2_add_start_array();
    o2_add_int32(123);
    o2_add_int32(234);
    o2_add_end_array();
    o2_send_finish(0, "/one/service_aii", TRUE);
    send_the_message();
    printf("DONE sending [123, 234]\n");

    // 4. sending typestring [xixdx] where x is one of: hfcBbtsSmTFIN
    for (char *xtp = xtypes; *xtp; xtp++) {
        xtype = *xtp;
        o2_send_start();
        o2_add_start_array();
        add_x_parameter();
        o2_add_int32(456);
        add_x_parameter();
        o2_add_double(234.567);
        add_x_parameter();
        o2_add_end_array();
        char address[32];
        snprintf(address, 32, "/one/service_%ci%cd%c", xtype, xtype, xtype);
        o2_send_finish(0, address, TRUE);
        send_the_message();
    }
    printf("DONE sending [xixdx] messages\n");

    // 5. sending typestring i[ih][fdt]d to test multiple arrays
    o2_send_start();
    o2_add_int32(456);
    o2_add_start_array();
    o2_add_int32(1234);
    o2_add_int64(12345);
    o2_add_end_array();
    o2_add_start_array();
    o2_add_float(1234.56F);
    o2_add_double(1234.567);
    o2_add_time(2345.678);
    o2_add_end_array();
    o2_add_double(1234.567);
    o2_send_finish(0, "/one/service_2arrays", TRUE);
    send_the_message();
    printf("DONE sending 456,[456,12345][1234.56,1234.567,2345.678],1234.567\n");

    // 6. sending typestring [ddddd...] where there are 1 to 100 d's
    for (int i = 0; i < 101; i++) {
        arg_count = i;
        o2_send_start();
        o2_add_start_array();
        for (int j = 0; j < i; j++) {
            o2_add_double(123.456 + j);
        }
        o2_add_end_array();
        o2_send_finish(0, "/one/service_bigarray", TRUE);
        send_the_message();
    }
    printf("DONE sending [ddd...], size 0 through 100\n");

    // 7. sending typestring vi (with length 0 to 100)
    int ivec[102];
    for (int j = 0; j < 102; j++) {
        ivec[j] = 1234 + j;
    }
    for (int i = 0; i < 101; i++) {
        arg_count = i;
        o2_send_start();
        o2_add_vector('i', i, ivec);
        o2_send_finish(0, "/one/service_vi", TRUE);
        send_the_message();
    }
    printf("DONE sending vi, size 0 through 100\n");


    // 8. sending typestring vf (with length 0 to 100)
    float fvec[102];
    for (int j = 0; j < 102; j++) {
        fvec[j] = 123.456F + j;
    }
    for (int i = 0; i < 101; i++) {
        arg_count = i;
        o2_send_start();
        o2_add_vector('f', i, fvec);
        o2_send_finish(0, "/one/service_vf", TRUE);
        send_the_message();
    }
    printf("DONE sending vf, size 0 through 100\n");


    // 9. sending typestring vh (with length 0 to 100)
    int64_t hvec[102];
    for (int j = 0; j < 102; j++) {
        hvec[j] = 123456 + j;
    }
    for (int i = 0; i < 101; i++) {
        arg_count = i;
        o2_send_start();
        o2_add_vector('h', i, hvec);
        o2_send_finish(0, "/one/service_vh", TRUE);
        send_the_message();
    }
    printf("DONE sending vh, size 0 through 100\n");


    // 10. sending typestring vd (with length 0 to 100)
    double dvec[102];
    for (int j = 0; j < 102; j++) {
        dvec[j] = 1234.567 + j;
    }
    for (int i = 0; i < 101; i++) {
        arg_count = i;
        o2_send_start();
        o2_add_vector('d', i, dvec);
        o2_send_finish(0, "/one/service_vd", TRUE);
        send_the_message();
    }
    printf("DONE sending vd, size 0 through 100\n");


    // 12. sending typestring ifvxif (with length 0 to 100)
    for (char *xtp = "ihfd"; *xtp; xtp++) {
        xtype = *xtp;
        for (int i = 0; i < 101; i++) {
            o2_send_start();
            o2_add_int32(2345);
            o2_add_float(345.67F);
            arg_count = i;
            switch (xtype) {
                case 'i': o2_add_vector('i', i, ivec); break;
                case 'h': o2_add_vector('h', i, hvec); break;
                case 'f': o2_add_vector('f', i, fvec); break;
                case 'd': o2_add_vector('d', i, dvec); break;
                default: assert(FALSE);
            }
            o2_add_int32(4567);
            o2_add_float(567.89F);
            o2_send_finish(0, "/one/service_ifvxif", TRUE);
            send_the_message();
        }
    }
    printf("DONE sending ifvxif, types ihfd, size 0 through 100\n");

    // 13. sending typestring vivd (with length 0 to 100)
    for (int i = 0; i < 101; i++) {
        o2_send_start();
        arg_count = i;
        o2_add_vector('i', i, ivec);
        o2_add_vector('d', i, dvec);
        o2_send_finish(0, "/one/service_vivd", TRUE);
        send_the_message();
    }
    printf("DONE sending vivd, size 0 through 100\n");

    // 14. sending i[xxxx...]i where x is in ihfdt and there are 0 to 100
    //     of them AND the data is received as a vector using coercion
    for (char *xtp = "ihfd"; *xtp; xtp++) {
        xtype = *xtp;
        for (char *ytp = "ihfd"; *ytp; ytp++) {
            ytype = *ytp;
            for (int i = 0; i < 101; i++) {
                o2_send_start();
                o2_add_int32(5678);
                arg_count = i;
                o2_add_start_array();
                for (int j = 0; j < i; j++) {
                    switch (xtype) {
                        case 'i': o2_add_int32(543 + j); break;
                        case 'h': o2_add_int64(543 + j); break;
                        case 'f': o2_add_float(543.21F + j); break;
                        case 'd': o2_add_double(543.21 + j); break;
                        default: assert(FALSE);
                    }
                }
                o2_add_end_array();
                o2_add_int32(6789);
                o2_send_finish(0, "/one/service_coerce", TRUE);
                send_the_message();
            }
        }
    }
    printf("DONE sending ifvxif, types ihfdt, size 0 through 100\n");

    // 15. sending ivxi where x is in ihfd and there are 0 to 100
    //     of them AND the data is received as an array using coercion
    for (char *x = "ihfd"; *x; x++) {
        xtype = *x;
        for (char *y = "ihfdt"; *y; y++) {
            ytype = *y;
            for (int i = 0; i < 101; i++) {
                o2_send_start();
                o2_add_int32(5678);
                o2_add_float(567.89F);
                arg_count = i;
                switch (xtype) {
                    case 'i': o2_add_vector('i', i, ivec); break;
                    case 'h': o2_add_vector('h', i, hvec); break;
                    case 'f': o2_add_vector('f', i, fvec); break;
                    case 'd': o2_add_vector('d', i, dvec); break;
                    default: assert(FALSE);
                }
                o2_add_int32(6789);
                o2_add_float(567.89F);
                o2_send_finish(0, "/one/service_coerce2", TRUE);
                send_the_message();
            }
        }
    }

    printf("DONE\n");
    o2_finish();
    return 0;
}
