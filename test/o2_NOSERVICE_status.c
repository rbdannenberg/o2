//  dispatchtest.c -- dispatch messages between local services
//

#include <stdio.h>
#include "o2.h"
#include "assert.h"
#include "o2_message.h"


#define N_ADDRS 20

int expected = 0;

void service_one(o2_msg_data_ptr data, const char *types,
                 o2_arg_ptr *argv, int argc, void *user_data)
{
    assert(argc == 1);
    assert(argv[0]->i == 1234);
    printf("service_one called\n");
    assert(expected % 10 == 1);
    expected /= 10;
}

void service_two(o2_msg_data_ptr data, const char *types,
                 o2_arg_ptr *argv, int argc, void *user_data)
{
    assert(argc == 1);
    assert(argv[0]->i == 2345);
    printf("service_two called\n");
    assert(expected % 10 == 2);
    expected /= 10;
}


int main(int argc, const char * argv[])
{
    o2_initialize("test");
    o2_service_new("one");
    o2_method_new("/one/i", "i", &service_one, NULL, TRUE, TRUE);  
    o2_service_new("two");
    o2_method_new("/two/i", "i", &service_two, NULL, TRUE, TRUE);

	int status_exist = o2_status("one");
	printf("STATUS %d \n",status_exist);

//checking for o2_status_from_info
//fails when a service which does not exits is passed
	int status = o2_status("four");
	printf("STATUS %d \n",status);

int status_new = o2_status("!@#$");
	printf("STATUS %d \n",status_new);
return 0;
}
