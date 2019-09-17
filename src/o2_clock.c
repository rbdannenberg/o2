// o2_clock.c -- clock synchronization
//
// Roger Dannenberg, 2016

#include <ctype.h>
#include "o2_internal.h"
#include "o2_message.h"
#include "o2_clock.h"
#include "o2_sched.h"
#include "o2_send.h"
#include "o2_search.h"

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

static int is_master; // initially FALSE, set true by o2_clock_set()
static int found_clock_service = FALSE; // set when service appears
static o2_time start_sync_time; // local time when we start syncing
static int clock_sync_id = 0;
static o2_time clock_sync_send_time;
static o2string clock_sync_reply_to;
static o2_time_callback time_callback = NULL;
static void *time_callback_data = NULL;
static int clock_rate_id = 0;
// data for clock sync. Each reply results in the computation of the
// round-trip time and the master-vs-local offset. These results are
// stored at ping_reply_count % CLOCK_SYNC_HISTORY_LEN
#define CLOCK_SYNC_HISTORY_LEN 5
static int ping_reply_count = 0;
static o2_time round_trip_time[CLOCK_SYNC_HISTORY_LEN];
static o2_time master_minus_local[CLOCK_SYNC_HISTORY_LEN];

static o2_time time_offset = 0.0; // added to time_callback()

#ifdef __APPLE__
#include "sys/time.h"
#include "CoreAudio/HostTime.h"
static uint64_t start_time;
#elif __linux__
#include "sys/time.h"
static long start_time;
#elif WIN32
static long start_time;
#endif

static void announce_synchronized();
static void clock_status_change(o2n_info_ptr info, int status);
static void compute_osc_time_offset(o2_time now);

// call this with the local and master time when clock sync is first obtained
//
static void o2_clock_synchronized(o2_time local_time, o2_time master_time)
{
    if (o2_clock_is_synchronized) {
        return;
    }
    o2_clock_is_synchronized = TRUE;
    o2_sched_start(&o2_gtsched, master_time);
    if (!is_master) {
        // do not set local_now or global_now because we could be inside
        // o2_sched_poll() and we don't want "now" to change, but we can
        // set up the mapping from local to global time:
        local_time_base = local_time;
        global_time_base = master_time;
        clock_rate = 1.0;
    }
    // tell all other processes that this one's status is synchronized;
    // also gets clock_status_change() to send !_o2/si for any active
    // service that is now synchronized
    announce_synchronized();
    // in addition, compute the offset to absolute time in case we need an
    // OSC timestamp
    compute_osc_time_offset(master_time);
    O2_DBg(printf("%s obtained clock sync at %g\n",
                  o2_debug_prefix, o2_time_get()));
}

// catch_up_handler -- handler for "/_o2/cu"
//    called when we are slowing down or speeding up to return
//    the clock rate to 1.0 because we should be synchronized
//
static void catch_up_handler(o2_msg_data_ptr msg, const char *types,
                      o2_arg_ptr *argv, int argc, void *user_data)
{
    int rate_id = argv[0]->i32;
    if (rate_id != clock_rate_id) return; // this task is cancelled
    // assume the scheduler sets local_now and global_now
    global_time_base = LOCAL_TO_GLOBAL(msg->timestamp);
    local_time_base = msg->timestamp;
    clock_rate = 1.0;
}


static void will_catch_up_after(double delay)
{
    // build a message that will call catch_up_handler(rate_id) at local_time
    if (o2_send_start() ||
        o2_add_int32(clock_rate_id))
        return;
    
    o2_message_ptr msg = o2_message_finish(local_time_base + delay, "!_o2/cu", FALSE);
    o2_schedule(&o2_ltsched, msg);
}


static void set_clock(double local_time, double new_master)
{
    global_time_base = LOCAL_TO_GLOBAL(local_time); // current estimate
    local_time_base = local_time;
    O2_DBk(printf("%s set_clock: using %.3f, should be %.3f\n",
        o2_debug_prefix, global_time_base, new_master));
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
    } else { // clock_advance <= -1
        clock_rate = 0; // we're way ahead: stop until next clock sync
        // maybe we should try to run clock sync soon since we are
        // way out of sync and do not know if master time is running
        // This seems to be such a bad situation that the best recovery
        // is probably application-dependent. Until we have a real
        // problem to fix, we'll just let things resynchronize (perhaps
        // after a long time) on their own.
    }
    O2_DBk(printf("%s adjust clock to %g, rate %g\n",
                  o2_debug_prefix, LOCAL_TO_GLOBAL(local_time), clock_rate));
}


int o2_send_clocksync(o2n_info_ptr proc)
{
    if (!o2_clock_is_synchronized)
        return O2_SUCCESS;
    if (o2_send_start()) return O2_FAIL;
    o2_send_by_tcp(proc, FALSE, o2_message_finish(0.0, "!_o2/cs/cs", TRUE));
    return O2_SUCCESS;
}
    

static void compute_osc_time_offset(o2_time now)
{
    // osc_time_offset is initialized using system clock, but you
    // can call o2_osc_time_offset() to change it, e.g. periodically
    // using a different time source
#ifdef WIN32
    // this code comes from liblo
    /* 
     * FILETIME is the time in units of 100 nsecs from 1601-Jan-01
     * 1601 and 1900 are 9435484800 seconds apart.
     */
    int64_t osc_time;
    FILETIME ftime;
    double dtime;
    GetSystemTimeAsFileTime(&ftime);
    dtime =
        ((ftime.dwHighDateTime * 4294967296.e-7) - 9435484800.) +
        (ftime.dwLowDateTime * 1.e-7);
    osc_time = (uint64_t) dtime;
    osc_time = (osc_time << 32) +
        (uint32_t) ((dtime - osc_time) * 4294967296.);
#else
#define JAN_1970 0x83aa7e80     /* 2208988800 1970 - 1900 in seconds */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t osc_time = (uint64_t) (tv.tv_sec + JAN_1970);
    osc_time = (osc_time << 32) + (uint64_t) (tv.tv_usec * 4294.967295);
#endif
    osc_time -= (uint64_t) (now * 4294967296.0);
    o2_osc_time_offset(osc_time);
    O2_DBk(printf("%s osc_time_offset (in sec) %g\n",
                  o2_debug_prefix, osc_time / 4294967296.0));
}


// when clock becomes synchronized, we must tell all other
// processes about it. To find all other processes, use the o2_context->fds_info
// table since all but a few of the entries are connections to processes
//
// additionally, call clock_status_change for every synchronized remote process,
// because we need to send !_o2/si messages for their newly synchronized services
//
static void announce_synchronized()
{
    for (int i = 0; i < o2_context->fds_info.length; i++) {
        o2n_info_ptr info = GET_PROCESS(i);
        if (TAG_IS_REMOTE(info->tag)) {
            o2_send_clocksync(info);
            // if info is synchronized, call clock_status_change()
            if (info->tag != INFO_TCP_SOCKET) { // remote process is also synchronized
                clock_status_change(info, O2_REMOTE);
            }
        }
    }
    clock_status_change(o2_context->info, O2_LOCAL);
}


// All services offered by this proc, described by info, have changed to 
// status, so send !_o2/si messages for them. Only active service changes
// are searched and reported. This is called in response to a /_o2/cs/cs clock
// sync status change announcement. Also, when the local process gets sync,
// o2_clock_synchronized() calls this for *every other process that is
// synchronized* because all services offered by them now have status
// O2_REMOTE, *and* o2_clock_synchronized() calls this with the local
// process so that !_o2/si messages are sent with status O2_LOCAL for each
// active local service.
//
static void clock_status_change(o2n_info_ptr info, int status)
{
    // status can only change if the local process
    // is synchronized (note also that once synchronized, the local
    // process does not lose sychronization, even if the cs service goes
    // away. (Maybe this should be fixed or maybe it is a feature.)
    //
    if (!o2_clock_is_synchronized) {
        return;
    }
    o2_do_not_reenter++;
    for (int i = 0; i < info->proc.services.length; i++) {
        services_entry_ptr ss = DA_GET(info->proc.services,
                                       proc_service_data, i)->services;
        // if the active service is provided by info (the newly synchronized
        // process), then send a status info (/_o2/si) message with status
        if (ss->services.length == 0) {
            continue;
        } else if (status == O2_LOCAL) { // local process
            o2_node_ptr node = GET_SERVICE(ss->services, 0);
            if (node->tag != NODE_HASH && node->tag != NODE_HANDLER) {
                continue;
            }
            if (isdigit(ss->key[0])) { // Don't report the local IP:PORT service.
                continue;           // It is redundant with "_o2"
            }
        } else { // status == O2_REMOTE
            if (GET_SERVICE(ss->services, 0) != (o2_node_ptr) info) {
                continue;
            }
        }
        // assert: either active service is local and info is the local
        // process (so active service is offered by info) or active service
        // is remote and offered by info as a remote process
        o2_send_cmd("!_o2/si", 0.0, "sis", ss->key,
                    status, info->proc.name);
    }
    o2_do_not_reenter--;
}


// handle messages to /_o2/cs/cs that announce when clock sync is obtained
//
void o2_clocksynced_handler(o2_msg_data_ptr msg, const char *types,
                            o2_arg_ptr *argv, int argc, void *user_data)
{
    o2string name = o2_message_source->proc.name;
    services_entry_ptr services;
    o2_node_ptr entry = o2_service_find(name, &services);
    if (entry && entry->tag == INFO_TCP_NOCLOCK) {
        o2n_info_ptr info = (o2n_info_ptr) entry;
        if (info->net_tag != NET_TCP_CLIENT && info->net_tag != NET_TCP_CONNECTION) {
            printf("ERROR: unexpected net_tag %d on entry %p in o2_clocksynced handler\n",
                   info->net_tag, info);
            return;
        }
        entry->tag = INFO_TCP_SOCKET;
        printf("info %p gets status INFO_TCP_SOCKET in o2_clocksynced_handler\n", info);
        clock_status_change(info, O2_REMOTE);
        return;
    }
    O2_DBg(printf("%s ### ERROR in o2_clocksynced_handler, bad service %s\n", o2_debug_prefix, name));
}


static double mean_rtt = 0;
static double min_rtt = 0;


void o2_clockrt_handler(o2_msg_data_ptr msg, const char *types,
                            o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_extract_start(msg);
    o2_arg_ptr reply_to_arg = o2_get_next('s');
    if (!reply_to_arg) return;
    char *replyto = reply_to_arg->s;
    int len = (int) strlen(replyto);
    if (len > 1000) {
        fprintf(stderr, "o2_clockrt_handler ignoring /cs/rt message with long reply_to argument\n");
        return; // address too long - ignore it
    }
    char address[1024];
    memcpy(address, replyto, len);
    memcpy(address + len, "/get-reply", 11); // include EOS
    o2_send(address, 0, "sff", o2_context->info->proc.name, mean_rtt, min_rtt);
}


static void cs_ping_reply_handler(o2_msg_data_ptr msg, const char *types,
                                  o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_arg_ptr arg;
    o2_extract_start(msg);
    if (!(arg = o2_get_next('i'))) return;
    // if this is not a reply to the most recent message, ignore it
    if (arg->i32 != clock_sync_id) return;
    if (!(arg = o2_get_next('t'))) return;
    o2_time master_time = arg->t;
    o2_time now = o2_local_time();
    o2_time rtt = now - clock_sync_send_time;
    // estimate current master time by adding 1/2 round trip time:
    master_time += rtt * 0.5;
    int i = ping_reply_count % CLOCK_SYNC_HISTORY_LEN;
    round_trip_time[i] = rtt;
    master_minus_local[i] = master_time - now;
    ping_reply_count++;
    O2_DBk(printf("%s got clock reply, master_time %g, rtt %g, count %d\n",
                  o2_debug_prefix, master_time, rtt, ping_reply_count));
    if (o2_debug & O2_DBk_FLAG) {
        int start, count;
        if (ping_reply_count < CLOCK_SYNC_HISTORY_LEN) {
            start = 0; count = ping_reply_count;
        } else {
            start = ping_reply_count % CLOCK_SYNC_HISTORY_LEN;
            count = CLOCK_SYNC_HISTORY_LEN;
        }
        printf("%s master minus local:", o2_debug_prefix);
        int k = start;
        for (int j = 0; j < count; j++) {
            printf(" %g", master_minus_local[k]);
            k = (k + 1) % CLOCK_SYNC_HISTORY_LEN;
        }
        printf("\n%s round trip:", o2_debug_prefix);
        for (int j = 0; j < count; j++) {
            printf(" %g", round_trip_time[start]);
            start = (start + 1) % CLOCK_SYNC_HISTORY_LEN;
        }
        printf("\n");
    }

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
        //printf("*    %s: time adjust %g\n", o2_debug_prefix,
        //       now + master_minus_local[best_i] - o2_time_get());
        o2_time new_master = now + master_minus_local[best_i];
        if (!o2_clock_is_synchronized) {
            o2_clock_synchronized(now, new_master);
        } else {
            set_clock(now, new_master);
        }
    }
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
//   then send ping every 0.1s CLOCK_SYNC_HISTORY_LEN times, 
//   then every 0.5s for 5s, then every 10s
//
void o2_ping_send_handler(o2_msg_data_ptr msg, const char *types,
                          o2_arg_ptr *argv, int argc, void *user_data)
{
    // this function gets called periodically to drive the clock sync
    // protocol, but if the process calls o2_clock_set(), then we
    // become the master, at which time we stop polling and announce
    // to all other processes that we know what time it is, and we
    // return without scheduling another callback.
    if (is_master) {
        o2_clock_is_synchronized = TRUE;
        return; // no clock sync; we're the master
    }
    clock_sync_send_time = o2_local_time();
    int status = o2_status("_cs");
    if (!found_clock_service) {
        found_clock_service = (status >= 0);
        if (found_clock_service) {
            O2_DBc(printf("%s ** found clock service, is_master=%d\n",
                          o2_debug_prefix, is_master));
            if (status == O2_LOCAL || status == O2_LOCAL_NOTIME) {
                assert(is_master);
            } else { // record when we started to send clock sync messages
                start_sync_time = clock_sync_send_time;
                char path[48]; // enough room for !IP:PORT/cs/get-reply
                snprintf(path, 48, "/%s/cs/get-reply",
                         o2_context->info->proc.name);
                o2_method_new(path, "it", &cs_ping_reply_handler,
                              NULL, FALSE, FALSE);
                snprintf(path, 48, "/%s/cs/rt", o2_context->info->proc.name);
                o2_method_new(path, "s", &o2_clockrt_handler, NULL, FALSE, FALSE);
                snprintf(path, 32, "!%s/cs", o2_context->info->proc.name);
                clock_sync_reply_to = o2_heapify(path);
            }
        }
    }
    // earliest time to call this action again is clock_sync_send_time + 0.1s:
    o2_time when = clock_sync_send_time + 0.1;
    if (found_clock_service) { // found service, but it's non-local
        if (status < 0) { // we lost the clock service, resume looking for it
            found_clock_service = FALSE;
        } else {
            clock_sync_id++;
            o2_send("!_cs/get", 0, "is", clock_sync_id, clock_sync_reply_to);
            // we're not checking the return value here. The worst that can
            // happen seems to be an error sending to a UDP port, and if that
            // happens, perror() will be called so at least if there is a console
            // an error message will appear. Not much else we can do.
            //
            // run every 0.1 second until at least CLOCK_SYNC_HISTORY_LEN pings
            // have been sent to get a fast start, then ping every 0.5s until 5s,
            // then every 10s.
            o2_time t1 = CLOCK_SYNC_HISTORY_LEN * 0.1 - 0.01;
            if (clock_sync_send_time - start_sync_time > t1) when += 0.4;
            if (clock_sync_send_time - start_sync_time > 5.0) when += 9.5;
            O2_DBk(printf("%s clock request sent at %g\n",
                          o2_debug_prefix, clock_sync_send_time));
        }
    }
    // schedule another call to o2_ping_send_handler
    o2_clock_ping_at(when);
}

void o2_clock_ping_at(o2_time when)
{
    o2_send_start();
    o2_message_ptr m = o2_message_finish(when, "!_o2/ps", FALSE);
    // printf("*    schedule ping_send at %g, now is %g\n", when, o2_local_time());
    o2_schedule(&o2_ltsched, m);
}

int clock_initialized = FALSE;

void o2_clock_initialize()
{
	if (clock_initialized) {
		o2_clock_finish();
	}
#ifdef __APPLE__
	start_time = AudioGetCurrentHostTime();
#elif __linux__
	struct timeval tv;
	gettimeofday(&tv, NULL);
	start_time = tv.tv_sec;
#elif WIN32
	timeBeginPeriod(1); // get 1ms resolution on Windows
	start_time = timeGetTime();
#else
#error o2_clock has no implementation for this system
#endif
	// until local clock is synchronized, LOCAL_TO_GLOBAL will return -1:
	local_time_base = 0;
	global_time_base = -1;
	clock_rate = 0;

    is_master = FALSE;
    o2_clock_is_synchronized = FALSE;
    time_callback = NULL;
    time_callback_data = NULL;
    found_clock_service = FALSE;
    ping_reply_count = 0;
    time_offset = 0;
    o2_method_new("/_o2/ps", "", &o2_ping_send_handler, NULL, FALSE, TRUE);
    o2_method_new("/_o2/cu", "i", &catch_up_handler, NULL, FALSE, TRUE);
}


void o2_clock_finish()
{
#ifdef WIN32
	timeEndPeriod(1); // give up 1ms resolution for Windows
#endif
	clock_initialized = FALSE;
}    


// cs_ping_handler -- handler for /_cs/get
//   return the master clock time. Arguments are serial_no and reply_to.
//   send serial_no and current time to serial_no + "/get-reply"
static void cs_ping_handler(o2_msg_data_ptr msg, const char *types,
                     o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_arg_ptr serial_no_arg, reply_to_arg;
    o2_extract_start(msg);
    if (!(serial_no_arg = o2_get_next('i')) ||
        !(reply_to_arg = o2_get_next('s'))) {
        return;
    }
    int serial_no = serial_no_arg->i32;
    char *replyto = reply_to_arg->s;
    int len = (int) strlen(replyto);
    if (len > 1000) {
        fprintf(stderr, "cs_ping_handler ignoring /_cs/get message with long reply_to argument\n");
        return; // address too long - ignore it
    }
    char address[1024];
    memcpy(address, replyto, len);
    memcpy(address + len, "/get-reply", 11); // include EOS
    o2_send(address, 0, "it", serial_no, o2_time_get());
}


int o2_clock_set(o2_time_callback callback, void *data)
{
    if (!o2_ensemble_name) {
        O2_DBk(printf("%s o2_clock_set cannot be called before "
                      "o2_initialize.\n", o2_debug_prefix));
        return O2_FAIL;
    }

    // adjust local_start_time to ensure continuity of time (this allows
    // user to change the time source):
    //   new_local_time - new_time_offset == old_local_time - old_time_offset
    //   new_time_offset = new_local_time - (old_local_time - old_time_offset)
    o2_time old_local_time = o2_local_time(); // (includes -old_time_offset)
    time_callback = callback;
    time_callback_data = data;
    time_offset = 0.0; // get the time without any offset
    o2_time new_local_time = o2_local_time();
    time_offset = new_local_time - old_local_time;

    // if we are already the master, then there is nothing more to do.
    if (is_master) {
        return O2_SUCCESS;
    }
    
    // start the scheduler, record that we are synchronized now:
    o2_clock_synchronized(new_local_time, new_local_time);
    
    o2_service_new2("_cs\000\000");
    o2_method_new("/_cs/get", "is", &cs_ping_handler, NULL, FALSE, FALSE);
    O2_DBg(printf("%s ** master clock established, time is now %g\n",
                  o2_debug_prefix, o2_local_time()));
    is_master = TRUE;

    return O2_SUCCESS;
}


o2_time o2_local_time()
{
    if (time_callback) {
        return (*time_callback)(time_callback_data) - time_offset;
    }
#ifdef __APPLE__
    uint64_t clock_time, nsec_time;
    clock_time = AudioGetCurrentHostTime() - start_time;
    nsec_time = AudioConvertHostTimeToNanos(clock_time);
    return ((o2_time) (nsec_time * 1.0E-9)) - time_offset;
#elif __linux__
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((tv.tv_sec - start_time) + (tv.tv_usec * 0.000001)) - time_offset;
#elif WIN32
	return ((timeGetTime() - start_time) * 0.001) - time_offset;
#else
#error o2_clock has no implementation for this system
#endif
}


o2_time o2_local_to_global(double lt)
{
    return (is_master ? lt : LOCAL_TO_GLOBAL(lt));
}


o2_time o2_time_get()
{
    o2_time t = o2_local_time();
    return (is_master ? t : LOCAL_TO_GLOBAL(t));
}
