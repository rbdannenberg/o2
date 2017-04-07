/* usleep.h -- implement a substitute for usleep and sleep on windows
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
 * This file can just be included -- it's used in a lot of test
 * programs which all have one source file that links to O2. It
 * seems unnecessary to create a separate object file to link and
 * then have system dependent link instructions.
 */

#include <windows.h>
#include <timeapi.h>

long last_time = 0;
long long implied_wakeup_us = 0;
void usleep(long usec)
{
    long now = timeGetTime();
    if (now - last_time < 50) {
        // assume the intention is a sequence of short delays
        implied_wakeup_us += usec;
    } else { // a long time has elapsed
        implied_wakeup_us = now * 1000LL + usec;
    }
    long wake_ms = (int) (implied_wakeup_us / 1000);
    if (wake_ms > now + 1) Sleep(wake_ms - now);
    last_time = wake_ms;
}

void sleep(int secs)
{
    Sleep(secs * 1000);
}
