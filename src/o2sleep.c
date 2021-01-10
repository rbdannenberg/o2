/* o2sleep.c -- implement a cross-platform ms sleep function
 *
 * Roger Dannenberg   Jan 2017
 *
 * I tried #define usleep(x) Sleep((x)/1000)
 * thinking that Sleep() would give ms delays and that would be good
 * enough, but in a loop that calls usleep(2000) 500 times, which
 * should nominally delay 1s, the delay could be 7s or more. I just
 * want to call o2_poll() while delaying. What I will try is to 
 * keep track of cummulative *intended* delay and return immediately
 * if we're seeing that delay already.
 *
 * On macOS and Linux, there is less to implement: we just
 * call nanosleep(), but it can be interrupted, so it's still
 * not just a renaming.
 */


#include "o2base.h"

#ifdef WIN32
#include <windows.h>
#include <timeapi.h>

static long last_time = 0;
static long implied_wakeup = 0;

void o2_sleep(int n)
{
    long now = timeGetTime();
    if (now - implied_wakeup < 50) {
        // assume the intention is a sequence of short delays
        implied_wakeup += n;
    } else { // a long time has elapsed
        implied_wakeup = now + n;
    }
    if (implied_wakeup > now + 1) {
        Sleep(implied_wakeup - now);
    }
}
#else

#include <time.h>
#include <errno.h>

void o2_sleep(int n)
{
    struct timespec ts;
    int res;

    ts.tv_sec = n / 1000;
    ts.tv_nsec = (n % 1000) * 1000000;

    do {
        res = nanosleep(&ts, &ts);
    } while (res && errno == EINTR);

    return;
}

#endif
