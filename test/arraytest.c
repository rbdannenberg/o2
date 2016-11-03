// arraytest.c -- test array/vector messages
//

// What does this test?
// 1. sending typestring [i] (an array with one integer)
// 2. sending typestring [] (an array with no integers)
// 3. sending typestring [ii] (an array with 2 integers)
// 4. sending typestring [xixdx] where x is one of: hfcBbtsSmTFIN
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
char a_midi_msg[4];

int arg_count = 0;

// 1. sending typestring [i] (an array with one integer)
// 
int service_ai(const o2_message_ptr data, const char *types,
               o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(data);

    assert(*types++ == '[');
    o2_arg_ptr arg = o2_get_next('[');
    assert(arg = o2_got_start_array);

    assert(*types++ == 'i');
    arg = o2_get_next('i');
    assert(arg->i == 123);

    assert(*types++ == ']');
    arg = o2_get_next(']');
    assert(arg = o2_got_end_array);

    assert(*types == 0); // end of string, got arg_count floats
    got_the_message = TRUE;
    return O2_SUCCESS;
}

// 2. sending typestring [] (an array with no integers)
// 3. sending typestring [ii] (an array with 2 integers)
// 4. sending typestring [xixdx] where x is one of: hfcBbtsSmTFIN
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
    char types[N * 2];
    o2_initialize("test");    
    o2_add_service("one");

    o2_add_method("/one/service_ai", "[i]", &service_ai, NULL, FALSE, FALSE);
    o2_start_send();
    o2_add_start_array();
    o2_add_int32(123);
    o2_add_end_array();
    o2_finish_send(0, "/one/service_ai");
    send_the_message();

    printf("DONE sending [123]\n");

    printf("DONE\n");
    o2_finish();
    return 0;
}
