//  infotest2.c -- test if we get info via /_o2/si
//
// intended to run in parallel with clockmirror
// Tests /si messages. Expected messages are listed in si_status. They
// are grouped because the exact order is not specified, but we go through
// a sequence of transitions resulting in groups of status messages as listed.
//
// Based on clockref.cpp


#include "testassert.h"
#include <stdio.h>
#include "o2.h"
#include <stdlib.h>  // exit
#include <string.h>
#include <ctype.h>

#define N_ADDRS 10

char remote_ip_port[O2_MAX_PROCNAME_LEN];

int si_msg_count = 0;


void service_one(O2msg_data_ptr data, const char *types,
                 O2arg_ptr *argv, int argc, const void *user_data)
{
    printf("Service one received a message\n");
}


O2time cs_time = 1000000.0;

// this is a handler that polls for current status
//
void clockmaster(O2msg_data_ptr msg, const char *types,
                 O2arg_ptr *argv, int argc, const void *user_data)
{
    int ss = o2_status("server");
    int cs = o2_status("client");
    int rs = o2_status(remote_ip_port);
    printf("infotest2: local time %g global time %g "
           "server status %d client status %d remote status %d\n",
           o2_local_time(), o2_time_get(), ss, cs, rs);
    // record when the client synchronizes
    if (cs == O2_REMOTE) {
        if (o2_time_get() < cs_time) {
            cs_time = o2_time_get();
            printf("infotest2 sync time %g\n", cs_time);
        }
    }
    // stop 12s later to make sure clockmirror shuts down first and
    // we get the status info (/_o2/si) messages about it
    if (o2_time_get() > cs_time + 12) {
        o2_stop_flag = true;
        printf("infotest2 set stop flag true at %g\n", o2_time_get());
    }
    o2_send("!server/clockmaster", o2_time_get() + 1, "");
}


// local services are create first in this order.
// LN is local non-synchronized, L is local synchronized

const char *group1[8] = {"one", "LN", NULL, NULL, NULL, NULL, NULL, NULL};
const char *group2[8] = {"server", "LN", NULL, NULL, NULL, NULL, NULL, NULL};
const char *group3[8] = {"_cs", "L", NULL, NULL, NULL, NULL, NULL, NULL};
const char *group4[8] = {"one", "L", "server", "L", "_o2", "L", NULL, NULL};

// "remote" refers to the remote process -- the string "remote" is 
// interpreted as the process name, e.g. @4a6dd865:c0a801a6:ec8a.
// client is a service it offers
// RN is remote, not synchronized; R is remote, synchronized, X is dead

const char *group5[8] = {"remote", "RN", "client", "RN",
                         NULL, NULL, NULL, NULL};
const char *group6[8] = {"remote", "R", "client", "R", NULL, NULL, NULL, NULL};
const char *group7[8] = {"remote", "X", "client", "X", NULL, NULL, NULL, NULL};

const char **si_status[8];

void init_si_status()
{
    si_status[0] = group1;
    si_status[1] = group2;
    si_status[2] = group3;
    si_status[3] = group4;
    si_status[4] = group5;
    si_status[5] = group6;
    si_status[6] = group7;
    si_status[7] = NULL;
}

// Expected si service and status is encoded in si_status.
// We need to find each member in the group at si_status[0].
// Then we shift si_status to the next group.
//
const char **find_group()
{
    /*
    printf("find_group\n");
    for (int i = 0; si_status[i]; i++) {
        printf("    [");
        for (int j = 0; si_status[i][j]; j += 2) {
            printf(" %s %s ", si_status[i][j], si_status[i][j + 1]);
        }
        printf("]\n");
    } */
    if (!si_status[0]) {
        return NULL;
    } else if (!si_status[0][0]) {
        // used every member of si_status[0], so shift
        int i = 0;
        while (si_status[i]) {
            si_status[i] = si_status[i + 1];
            i++;
        }
    }
    if (si_status[0]) {
        printf("find_group returns [");
        for (int j = 0; si_status[0][j]; j += 2) {
            printf("%s %s  ", si_status[0][j], si_status[0][j + 1]);
        }
        printf("]\n");
    } else {
        printf("find_group returns (nothing left)\n");
    }
    return si_status[0];
}


int check_service(const char *service, const char *ip_port, int status)
{
    const char **group = find_group();
    // when we first hear about a remote process, store the name here
    if (!remote_ip_port[0] && !streql(ip_port, "_o2")) {
        strcpy(remote_ip_port, ip_port);
        printf("remote_ip_port is %s\n", remote_ip_port);
    }
    if (!group) {
        printf("In check_service, no group found - test fails\n");
        return false;
    }
    // search group for expected service/status
    int i = 0;
    while (group[i]) {
        if (streql(group[i], "remote")) {
            group[i] = remote_ip_port;
        }
        if (streql(group[i], service)) { // found it
            bool good_ip_port =
                    (group[i + 1][0] == 'L' && streql(ip_port, "_o2")) ||
                    (group[i + 1][0] == 'R' &&
                     streql(ip_port, remote_ip_port)) ||
                    (group[i + 1][0] == 'X');
            if (!good_ip_port) {
#ifndef O2_NO_DEBUG
                printf("Bad ip_port %s for service %s, status %s\n",
                       ip_port, service, o2_status_to_string(status));
#else
                printf("Bad ip_port %s for service %s, status %d\n",
                       ip_port, service, status);
#endif
            }
            if ((status == O2_LOCAL_NOTIME && streql(group[i+1], "LN")) ||
                (status == O2_LOCAL && streql(group[i+1], "L")) ||
                (status == O2_REMOTE_NOTIME && streql(group[i+1], "RN")) ||
                (status == O2_REMOTE && streql(group[i+1], "R")) ||
                (status == O2_FAIL && streql(group[i+1], "X"))) {
                // delete by shifting remaining members
                printf("    found service \"%s\" in group\n", service);
                while (group[i]) {
                    group[i] = group[i + 2];
                    group[i + 1] = group[i + 3];
                    i = i + 2;
                }
                return true;
            } else {
#ifndef O2_NO_DEBUG
printf("Bad status %s for %s, expected %s\n",
       o2_status_to_string(status), service, group[i + 1]);
#else
                printf("Bad status %d for %s, expected %s\n",
                       status, service, group[i + 1]);
#endif
                return false; // bad status
            }
        }
        i += 2;
    }
#ifndef O2_NO_DEBUG
    printf("Service %s not expected, status is %s.\n", service,
           o2_status_to_string(status));
#else
    printf("Service %s not expected, status is %d.\n", service, status);
#endif
    return false; // did not find the service
}


void service_info_handler(O2msg_data_ptr data, const char *types,
                 O2arg_ptr *argv, int argc, const void *user_data)
{
    const char *service_name = argv[0]->s;
    int status = argv[1]->i32;
    const char *ip_port = argv[2]->s;
    const char *properties = argv[3]->s;
#ifndef O2_NO_DEBUG
    const char *status_string = o2_status_to_string(status);
    printf("service_info_handler called: %s at %s status %s msg %d "
           "properties %s\n",
           service_name, ip_port, status_string, si_msg_count, properties);
#else
    printf("service_info_handler called: %s at %s status %d msg %d "
           "properties %s\n",
           service_name, ip_port, status, si_msg_count, properties);
#endif
    if (!properties || properties[0]) {
        printf("FAILURE -- expected empty string for properties\n");
    }
    if (!check_service(service_name, ip_port, status)) {
        printf("FAILURE\n");
        exit(-1);
    }
    si_msg_count++;
}


int main(int argc, const char * argv[])
{
    init_si_status();
    printf("Usage: infotest2 [debugflags] "
           "(see o2.h for flags, use a for (almost) all)\n");
    if (argc == 2) {
        o2_debug_flags(argv[1]);
        printf("debug flags are: %s\n", argv[1]);
    }
    if (argc > 2) {
        printf("WARNING: infotest2 ignoring extra command line argments\n");
    }

    remote_ip_port[0] = 0; // initialize to empty string meaning "unknown"
    if (o2_initialize("test")) {
        printf("FAIL\n");
        return -1;
    }
    o2_method_new("/_o2/si", "siss", &service_info_handler, NULL, false, true);

    o2_service_new("one");
    for (int i = 0; i < N_ADDRS; i++) {
        char path[100];
        sprintf(path, "/one/benchmark/%d", i);
        o2_method_new(path, "i", &service_one, NULL, false, false);
    }
    
    o2_service_new("server");
    o2_method_new("/server/clockmaster", "", &clockmaster, NULL, false, false);

    o2_send("/one/benchmark/0", 0, "i", 0);
    for (int i = 0; i < 1000; i++) {
        o2_poll();
    }

    // we are the master clock
    o2_clock_set(NULL, NULL);
    o2_send("!server/clockmaster", 0.0, ""); // start polling
    o2_run(100);
    o2_finish();
    o2_sleep(1000);
    if (si_msg_count != 12) {
        printf("FAILURE - wrong si_msg_count (%d)\n", si_msg_count);
    } else {
        printf("INFOTEST2 DONE\n");
    }
    return 0;
}
