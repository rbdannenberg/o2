// vectortest.c -- test array/vector messages
//

// What does this test?
// 1. contains handlers to extract and print vector messages of type double, int64 and float


#include <stdio.h>
#include "o2.h"
#include "assert.h"
#include "string.h"
#ifdef WIN32
#define snprintf _snprintf
#endif


void service_b(o2_msg_data_ptr data, const char *types,
               o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_msg_data_print(data);
    o2_extract_start(data);
    assert(strcmp(types, "b") == 0);
    o2_arg_ptr arg = o2_get_next('b');
    printf("service_b types=%s blob=%p\n", types, &(arg->b));
}

void service_vd(o2_msg_data_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_msg_data_print(data);
    o2_extract_start(data);

	printf("Message received !");
    assert(*types++ == 'v');
#ifndef NDEBUG
    o2_arg_ptr arg = // only needed by assert()
#endif
    o2_get_next('v');
   // assert(arg);
    //assert(*types++ == 'd');
#ifndef NDEBUG
    o2_arg_ptr arg2 = // only needed by assert()
#endif
    o2_get_next('d');
    for (int i = 0; i < 5; i++) {
#ifndef NDEBUG
        double correct = 12345.67 + i;
#endif
    }

}

void service_vh(o2_msg_data_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, void *user_data)
{
  o2_msg_data_print(data);
  o2_extract_start(data);
	printf("Message received !");
  assert(*types++ == 'v');
#ifndef NDEBUG
    o2_arg_ptr arg = // only needed by assert()
#endif
    o2_get_next('v');
#ifndef NDEBUG
    o2_arg_ptr arg2 = // only needed by assert()
#endif
    o2_get_next('h');
    for (int i = 0; i < 5; i++) {
#ifndef NDEBUG
        long correct = 1234567 + i;
#endif
    }
}

void service_vf(o2_msg_data_ptr data, const char *types,
                o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_msg_data_print(data);
    o2_extract_start(data);

	printf("Message received !");
    assert(*types++ == 'v');
#ifndef NDEBUG
    o2_arg_ptr arg = // only needed by assert()
#endif
    o2_get_next('v');
#ifndef NDEBUG
    o2_arg_ptr arg2 = // only needed by assert()
#endif
    o2_get_next('f');
    for (int i = 0; i < 5; i++) {
#ifndef NDEBUG
        int correct = 123 + i;
#endif

    }
}

int main(int argc, const char * argv[])
{
    o2_initialize("test");
    o2_service_new("one");
    o2_method_new("/one/b", "b", &service_b, NULL, FALSE, FALSE);
    o2_service_new("vectortest");
    o2_method_new("/vectortest/service_vh", NULL, &service_vh, NULL, FALSE, FALSE);
    o2_method_new("/vectortest/service_vd", NULL, &service_vd, NULL, FALSE, FALSE);
    o2_method_new("/vectortest/service_vf", NULL, &service_vf, NULL, FALSE, FALSE);
    o2_method_new("/one/b", "b", &service_b, NULL, FALSE, FALSE);
    int j = 0;
	while(j < 100000)
	{
	o2_poll();
	j++;
	usleep(20);
	}

}
