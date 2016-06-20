// o2_clock.c -- clock synchronization
//
// Roger Dannenberg, 2016

#include "o2.h"
#include "o2_dynamic.h"
#include "o2_socket.h"
#include "o2_search.h"
#include "o2_internal.h"
#include "o2_sched.h"

// get the master clock - clock time is estimated as
//   global_time_base + elapsed_time * clock_rate, where
//   elapsed_time is local_time - local_time_base
//
#define LOCAL_TO_GLOBAL(t) \
    (global_time_base + ((t) - local_time_base) * clock_rate)


static o2_time local_time_base;
static o2_time global_time_base;
static double clock_rate;

int o2_clock_is_synchronized = FALSE; // can we read the time?

static int is_master; // initially FALSE, set true by o2_set_clock()
static int found_clock_service = FALSE; // set when service appears
static o2_time start_sync_time; // local time when we start syncing
static int clock_sync_id = 0;
static o2_time clock_sync_send_time;
static char *clock_sync_reply_to;
static o2_time_callback clock_callback;
static void *clock_callback_data;
static int clock_rate_id = 0;
// data for clock sync. Each reply results in the computation of the
// round-trip time and the master-vs-local offset. These results are
// stored at ping_reply_count % CLOCK_SYNC_HISTORY_LEN
#define CLOCK_SYNC_HISTORY_LEN 5
static int ping_reply_count = 0;
static o2_time round_trip_time[CLOCK_SYNC_HISTORY_LEN];
static o2_time master_minus_local[CLOCK_SYNC_HISTORY_LEN];


#ifdef __APPLE__
#include <CoreAudio/HostTime.h>
static uint64_t start_time;
#elif __UNIX__
long start_time;
#endif

void o2_time_init()
{
#ifdef __APPLE__
    start_time = AudioGetCurrentHostTime();
#elif __UNIX__
    struct timeval tv;
    gettimeofday(&tv, NULL);
    start_time = tv.tv_sec;
#endif
    // until local clock is synchronized, LOCAL_TO_GLOBAL will return -1:
    local_time_base = 0;
    global_time_base = -1;
    clock_rate = 0;
}


// call this with the local and master time when clock sync is first obtained
//
void o2_clock_synchronized(o2_time local_time, o2_time master_time)
{
    o2_start_a_scheduler(&o2_gtsched, master_time);

    // do not set local_now or global_now because we could be inside
    // o2_sched_poll() and we don't want "now" to change, but we can
    // set up the mapping from local to global time:
    local_time_base = local_time;
    global_time_base = master_time;
    clock_rate = 1.0;
}

// catch_up_handler -- handler for "/_o2/cu"
//    called when we are slowing down or speeding up to return
//    the clock rate to 1.0 because we should be synchronized
//
void catch_up_handler(o2_message_ptr msg, const char *types,
                      o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_arg_ptr rate_id_arg;
    if (!(rate_id_arg = o2_get_next('i'))) {
        return;
    }
    int rate_id = rate_id_arg->i32;
    if (rate_id != clock_rate_id) return; // this task is cancelled
    // assume the scheduler sets local_now and global_now
    global_time_base = LOCAL_TO_GLOBAL(msg->data.timestamp);
    local_time_base = msg->data.timestamp;
    clock_rate = 1.0;
}


void will_catch_up_after(double delay)
{
    // build a message that will call catch_up_handler(rate_id) at local_time
    if (o2_start_send() ||
        o2_add_int32(clock_rate_id))
        return;
    
    o2_message_ptr msg = o2_finish_message(local_time_base + delay, "!_o2/cu");
    o2_schedule(&o2_ltsched, msg);
}


void set_clock(double local_time, double new_master)
{
    local_time_base = local_time;
    global_time_base = LOCAL_TO_GLOBAL(local_time_base); // current estimate
    double clock_advance = new_master - global_time_base; // how far to catch up
    clock_rate_id++; // cancel any previous calls to catch_up_handler()
    // compute when we will catch up: estimate will increase at clock_rate
    // while (we assume) master increases at rate 1, so at what t will
    //   global_time_base + (t - local_time_base) * clock_rate ==
    //   new_master + (t - local_time_base)
    // =>
    //   new_master - global_time_base ==
    //       (t - local_time_base) * clock_rate - (t - local_time_base)
    // =>
    //   clock_advance == (clock_rate - 1) * (t - local_time_base)
    // =>
    //   t == local_time_base + clock_advance / (clock_rate - 1)
    if (clock_advance > 1) {
        clock_rate = 1.0;
        global_time_base = new_master; // we are way behind: jump ahead
    } else if (clock_advance > 0) { // we are a little behind,
        clock_rate = 1.1;           // go faster to catch up
        will_catch_up_after(clock_advance * 10);
    } else if (clock_advance > -1) { // we are a little ahead
        clock_rate = 0.9; // go slower until the master clock catches up
        will_catch_up_after(clock_advance * -10);
    } else {
        clock_rate = 0; // we're way ahead: stop until next clock sync
        // TODO: maybe we should try to run clock sync soon since we are
        //       way out of sync and do not know if master time is running
    }
}


void reset_clock_rate()
{
}


int o2_send_clocksync(process_info_ptr process)
{
    if (!o2_clock_is_synchronized)
        return O2_SUCCESS;
    char address[32];
    snprintf(address, 32, "!%s/cs/cs", process->name);
    return o2_send_cmd(address, 0.0, "s", o2_process.name);
}
    

void announce_synchronized()
{
    // when clock becomes synchronized, we must tell all other
    // processes about it. To find all other processes, use the o2_fds_info
    // table since all but a few of the entries are connections to processes
    for (int i = 0; i < o2_fds_info.length; i++) {
        fds_info_ptr info = DA_GET(o2_fds_info, fds_info, i);
        if (info->tag == TCP_SOCKET) {
            o2_send_clocksync(info->u.process_info);
        }
    }
}


int o2_clocksynced_handler(o2_message_ptr msg, const char *types,
                           o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(msg);
    o2_arg_ptr arg = o2_get_next('s');
    if (!arg) return O2_FAIL;
    char *name = arg->s;
    int i;
    generic_entry_ptr *entry = lookup(&path_tree_table, name, &i);
    if (entry) {
        assert((*entry)->tag == O2_REMOTE_SERVICE);
        remote_service_entry_ptr service = (remote_service_entry_ptr) *entry;
        process_info_ptr process = service->parent;
        process->status = PROCESS_OK;
        return O2_SUCCESS;
    }
    return O2_FAIL;
}


static double mean_rtt = 0;
static double min_rtt = 0;


int cs_ping_reply_handler(o2_message_ptr msg, const char *types,
                          o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_arg_ptr arg;
    o2_start_extract(msg);
    if (!(arg = o2_get_next('i'))) {
        return O2_FAIL;
    }
    // if this is not a reply to the most recent message, ignore it
    if (arg->i32 != clock_sync_id) return O2_SUCCESS;
    if (!(arg = o2_get_next('t'))) {
        return O2_FAIL;
    }
    o2_time master_time = arg->t;
    o2_time now = o2_local_time();
    o2_time rtt = now - clock_sync_send_time;
    // estimate current master time by adding 1/2 round trip time:
    master_time += rtt * 0.5;
    int i = ping_reply_count % CLOCK_SYNC_HISTORY_LEN;
    round_trip_time[i] = rtt;
    master_minus_local[i] = master_time - now;
    ping_reply_count++;
    if (ping_reply_count >= CLOCK_SYNC_HISTORY_LEN) {
        // find minimum round trip time
        min_rtt = 9999.0;
        mean_rtt = 0;
        int best_i;
        for (i = 0; i < CLOCK_SYNC_HISTORY_LEN; i++) {
            mean_rtt += round_trip_time[i];
            if (round_trip_time[i] < min_rtt) {
                min_rtt = round_trip_time[i];
                best_i = i;
            }
        }
        // best estimate of master_minus_local is stored at i
        //printf("*    %s: time adjust %g\n", debug_prefix,
        //       now + master_minus_local[best_i] - o2_get_time());
        set_clock(now, now + master_minus_local[best_i]);
        if (!o2_clock_is_synchronized) {
            o2_clock_is_synchronized = TRUE;
            announce_synchronized();
        }
    }
    return O2_SUCCESS;
}


int o2_roundtrip(double *mean, double *min)
{
    if (!o2_clock_is_synchronized) return O2_FAIL;
    *mean = mean_rtt;
    *min = min_rtt;
    return O2_SUCCESS;
}


// o2_ping_send_handler -- handler for /_o2/ps (short for "ping send")
//   wait for clock sync service to be established,
//   then send ping every 0.5s for 5s, then every 10s
//
int o2_ping_send_handler(o2_message_ptr msg, const char *types,
                         o2_arg_ptr *argv, int argc, void *user_data)
{
    // printf("*    ping_send called at %g\n", o2_local_time());
    if (is_master) {
        o2_clock_is_synchronized = TRUE;
        announce_synchronized();
        return O2_SUCCESS; // no clock sync; we're the master
    }
    if (types[0]) return O2_FAIL; // not expecting any arguments
    clock_sync_send_time = o2_local_time();
    if (!found_clock_service) {
        int status = o2_status("_cs");
        found_clock_service = (status >= 0);
        if (found_clock_service) {
            if (status == O2_LOCAL || status == O2_LOCAL_NOTIME) {
                is_master = TRUE;
            } else { // record when we started to send clock sync messages
                start_sync_time = clock_sync_send_time;
                char path[48]; // enough room for !IP:PORT/cs/get-reply
                snprintf(path, 48, "!%s/cs/get-reply", o2_process.name);
                o2_add_method(path, "it", &cs_ping_reply_handler, NULL, FALSE, FALSE);
                snprintf(path, 32, "!%s/cs", o2_process.name);
                clock_sync_reply_to = o2_heapify(path);
            }
        }
    }
    // default time to call this action again is clock_sync_send_time + 0.5s:
    o2_time when = clock_sync_send_time + 0.5;
    if (found_clock_service) { // found service, but it's non-local
        clock_sync_id++;
        o2_send("!_cs/get", 0, "is", clock_sync_id, clock_sync_reply_to); // TODO: test return?
        // run every 1/2 second until at least CLOCK_SYNC_HISTORY_LEN pings
        // have been sent to get a fast start, then ping every 10s. Here, we
        // add 1.0 to allow for round-trip time and an extra ping just in case:
        int t1 = CLOCK_SYNC_HISTORY_LEN * 0.5 + 1.0;
        if (clock_sync_send_time - start_sync_time > t1) when += 9.5;
    }
    // schedule another call to o2_ping_send_handler
    int err;
    if ((err = o2_start_send())) return err;
    msg = o2_finish_message(when, "!_o2/ps");
    // printf("*    schedule ping_send at %g, now is %g\n", when, o2_local_time());
    o2_schedule(&o2_ltsched, msg);
    return O2_SUCCESS;
}


void o2_clock_init()
{
    is_master = FALSE;
    o2_add_method("/_o2/ps", "", &o2_ping_send_handler, NULL, FALSE, FALSE);
}


// cs_ping_handler -- handler for /_cs/get
//   return the master clock time
int cs_ping_handler(o2_message_ptr msg, const char *types,
                    o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_arg_ptr serial_no_arg, reply_to_arg;
    o2_start_extract(msg);
    if (!(serial_no_arg = o2_get_next('i')) ||
        !(reply_to_arg = o2_get_next('s'))) {
        return O2_FAIL;
    }
    int serial_no = serial_no_arg->i32;
    char *replyto = reply_to_arg->s;
    // replyto is the last thing in msg, so we'll just append to it
    // in place to construct the full reply path. First, check that
    // we have space. If not, assume sender is trying to attack us
    // because ordinarily, the address should be fairly short. 16
    // is the length of a padded "/get-reply" with bytes to spare:
    if (msg->allocated < msg->length + 16) {
        return O2_FAIL;
    }
    strcat(replyto, "/get-reply");
    o2_send(replyto, 0, "it", serial_no, o2_get_time());
    return O2_SUCCESS;
}


int o2_set_clock(o2_time_callback time_callback, void *data)
{
    /*Functions to get time
     time.h :
     asctime, ctime --return string
     gmtime, localtime --return struct tm
     time --return int (seconds)
     clock --
     sys/time.h:
     gettimeofday --return int
     gettime
     */
    if (is_master) {
        // fail if clock master has already been set
        printf("Clock has already been set up.\n");
        return O2_FAIL;
    }
    is_master = TRUE;
    o2_clock_is_synchronized = TRUE;
    clock_callback = time_callback;
    clock_callback_data = data;
    o2_add_service("_cs");
    o2_add_method("/_cs/get", "is", &cs_ping_handler, NULL, FALSE, FALSE);
    return O2_SUCCESS;
}


o2_time o2_local_time()
{
#ifdef __APPLE__
    uint64_t clock_time, nsec_time;
    clock_time = AudioGetCurrentHostTime() - start_time;
    nsec_time = AudioConvertHostTimeToNanos(clock_time);
    return (o2_time) (nsec_time * 1.0E-9);
#elif __UNIX__
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec - start_time) + (tv.tv_usec * 0.000001);
#endif
}


o2_time o2_local_to_global(double lt)
{
    return (is_master ? lt : LOCAL_TO_GLOBAL(lt));
}


o2_time o2_get_time()
{
    o2_time t = o2_local_time();
    return (is_master ? t : LOCAL_TO_GLOBAL(t));
}
