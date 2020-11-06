// clock.c -- clock synchronization
//
// Roger Dannenberg, 2016

#include <ctype.h>
#include "o2internal.h"
#include "services.h"
#include "message.h"
#include "clock.h"
#include "o2sched.h"
#include "msgsend.h"
#include "pathtree.h"
#include "mqtt.h"

// get the reference clock - clock time is estimated as
//   global_time_base + elapsed_time * clock_rate, where
//   elapsed_time is local_time - local_time_base
//
#define LOCAL_TO_GLOBAL(t) \
    (global_time_base + ((t) - local_time_base) * clock_rate)


static o2_time local_time_base;
static o2_time global_time_base;
static double clock_rate;

bool o2_clock_is_synchronized = false; // can we read the time?

static bool is_refclk; // initially false, set true by o2_clock_set()
static int found_clock_service = false; // set when service appears
static o2_time start_sync_time; // local time when we start syncing
static int clock_sync_id = 0;
static o2_time clock_sync_send_time;
static o2string clock_sync_reply_to = NULL;
static o2_time_callback time_callback = NULL;
static void *time_callback_data = NULL;
static int clock_rate_id = 0;
// data for clock sync. Each reply results in the computation of the
// round-trip time and the reference-vs-local offset. These results are
// stored at ping_reply_count % CLOCK_SYNC_HISTORY_LEN
#define CLOCK_SYNC_HISTORY_LEN 5
static int ping_reply_count = 0;
static o2_time round_trip_time[CLOCK_SYNC_HISTORY_LEN];
static o2_time ref_minus_local[CLOCK_SYNC_HISTORY_LEN];

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

static void announce_synchronized(void);
static void clock_status_change(o2_node_ptr info, const char *name,
                                o2_status_t status);
static void compute_osc_time_offset(o2_time now);

// call this with the local and reference time when clock sync is obtained
//
static void o2_clock_synchronized(o2_time local_time, o2_time ref_time)
{
    if (o2_clock_is_synchronized) {
        return;
    }
    o2_clock_is_synchronized = true;
    o2_sched_start(&o2_gtsched, ref_time);
    if (!is_refclk) {
        // do not set local_now or global_now because we could be inside
        // o2_sched_poll() and we don't want "now" to change, but we can
        // set up the mapping from local to global time:
        local_time_base = local_time;
        global_time_base = ref_time;
        clock_rate = 1.0;
    }
    // tell all other processes that this one's status is synchronized;
    // also gets clock_status_change() to send !_o2/si for any active
    // service that is now synchronized
    announce_synchronized();
#ifndef O2_NO_OSC
    // in addition, compute the offset to absolute time in case we need an
    // OSC timestamp
    compute_osc_time_offset(ref_time);
    O2_DBG(printf("%s obtained clock sync at %g\n",
                  o2_debug_prefix, o2_time_get()));
#endif
}

// catch_up_handler -- handler for "/_o2/cu"
//    called when we are slowing down or speeding up to return
//    the clock rate to 1.0 because we should be synchronized
//
static void catch_up_handler(o2_msg_data_ptr msg, const char *types,
                      o2_arg_ptr *argv, int argc, const void *user_data)
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
    
    o2_prepare_to_deliver(o2_message_finish(local_time_base + delay,
                                            "!_o2/cu", false));
    o2_schedule(&o2_ltsched);
}


static void set_clock(double local_time, double new_ref)
{
    global_time_base = LOCAL_TO_GLOBAL(local_time); // current estimate
    local_time_base = local_time;
    O2_DBk(printf("%s set_clock: using %.3f, should be %.3f\n",
        o2_debug_prefix, global_time_base, new_ref));
    double clock_advance = new_ref - global_time_base; // how far to catch up
    clock_rate_id++; // cancel any previous calls to catch_up_handler()
    // compute when we will catch up: estimate will increase at clock_rate
    // while (we assume) reference increases at rate 1, so at what t will
    //   global_time_base + (t - local_time_base) * clock_rate ==
    //   new_ref + (t - local_time_base)
    // =>
    //   new_ref - global_time_base ==
    //       (t - local_time_base) * clock_rate - (t - local_time_base)
    // =>
    //   clock_advance == (clock_rate - 1) * (t - local_time_base)
    // =>
    //   t == local_time_base + clock_advance / (clock_rate - 1)
    if (clock_advance > 1) {
        clock_rate = 1.0;
        global_time_base = new_ref; // we are way behind: jump ahead
    } else if (clock_advance > 0) { // we are a little behind,
        clock_rate = 1.1;           // go faster to catch up
        will_catch_up_after(clock_advance * 10);
    } else if (clock_advance > -1) { // we are a little ahead
        clock_rate = 0.9; // go slower until the reference clock catches up
        will_catch_up_after(clock_advance * -10);
    } else { // clock_advance <= -1
        clock_rate = 0; // we're way ahead: stop until next clock sync
        // maybe we should try to run clock sync soon since we are
        // way out of sync and do not know if reference time is running
        // This seems to be such a bad situation that the best recovery
        // is probably application-dependent. Until we have a real
        // problem to fix, we'll just let things resynchronize (perhaps
        // after a long time) on their own.
    }
    O2_DBk(printf("%s adjust clock to %g, rate %g\n",
                  o2_debug_prefix, LOCAL_TO_GLOBAL(local_time), clock_rate));
}


o2_err_t o2_send_clocksync_proc(proc_info_ptr proc)
{
    if (!o2_clock_is_synchronized)
        return O2_SUCCESS;
    if (o2_send_start()) return O2_FAIL;
    o2_add_string(o2_ctx->proc->name);
    o2_prepare_to_deliver(o2_message_finish(0.0, "!_o2/cs/cs", O2_TCP_FLAG));
    o2_send_remote(proc, false);
    return O2_SUCCESS;
}
    
#ifndef O2_NO_OSC
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
#endif

// when clock becomes synchronized, we must tell all other
// processes about it. To find all other processes, use the o2_ctx->fds_info
// table since all but a few of the entries are connections to processes
//
// additionally, call clock_status_change for every synchronized remote process,
// because we need to send !_o2/si messages for their newly synchronized
// services
//
static void announce_synchronized()
{
    int i = 0;
    o2n_info_ptr info;
    while ((info = o2n_get_info(i++))) {
        proc_info_ptr proc = (proc_info_ptr) info->application;
        if (proc && IS_REMOTE_PROC(proc)) {
            o2_send_clocksync_proc(proc);
            // if info is synchronized, call clock_status_change()
            if (proc->tag == PROC_SYNCED) { // remote proc is also synchronized
                clock_status_change((o2_node_ptr) proc, proc->name, O2_REMOTE);
            }
        }
    }
#ifndef O2_NO_MQTT
    for (int i = 0; i < o2_mqtt_procs.length; i++) {
        proc_info_ptr mqtt_proc = DA_GET(o2_mqtt_procs, proc_info_ptr, i);
        o2_send_clocksync_proc(mqtt_proc);
        // see if it is synchronized
        if (mqtt_proc->tag == MQTT_SYNCED) {
            clock_status_change((o2_node_ptr) mqtt_proc,
                                mqtt_proc->name, O2_REMOTE);
        }
    }
#endif
    // name is "_o2"
    clock_status_change((o2_node_ptr) o2_ctx->proc, NULL, O2_LOCAL);
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
// Algorithm: for each service offered by proc,
//                if the status is O2_LOCAL (implies proc is local) and
//                   a local service is active and
//                   the service is not IP:PORT:
//                    send !_o2/si with service_name, status, local IP:PORT
//                elif the status is O2_REMOTE and
//                     the service is offered by proc:
//                    send !_o2/si with service_name, status, remote IP:PORT
static void clock_status_change(o2_node_ptr proc, const char *name,
                                o2_status_t status)
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
    // find all active services offered by proc and send /si message
    enumerate enumerator;
    o2_enumerate_begin(&enumerator, &o2_ctx->path_tree.children);
    o2_node_ptr entry;
    while ((entry = o2_enumerate_next(&enumerator))) {
        services_entry_ptr services = TO_SERVICES_ENTRY(entry);
        if (services->services.length > 0) {
            service_provider_ptr spp =
                    GET_SERVICE_PROVIDER(services->services, 0);
            if (status == O2_LOCAL) { // local proc and HASH or HANDLER
                if (spp->service->tag == NODE_HASH ||
                    spp->service->tag == NODE_HANDLER) {
                    O2_DBk(printf("%s clock_status_change sends /si \"%s\" "
                                  "%s(%d) proc \"_o2\" properties \"%s\"\n",
                                  o2_debug_prefix, services->key,
                                  o2_status_to_string(status), status,
                                  spp->properties ? spp->properties + 1 : ""));
                    o2_send_cmd("!_o2/si", 0.0, "siss", services->key, status,
                            "_o2", spp->properties ? spp->properties + 1 : "");
                }
            } else if (status == O2_REMOTE && (proc->tag == PROC_SYNCED
#ifndef O2_NO_MQTT
                                               || proc->tag == MQTT_SYNCED
#endif
                                              ) && spp->service == proc) {
                O2_DBk(printf("%s clock_status_change sends /si \"%s\" "
                        "%s(%d) proc \"%s\" properties \"%s\"\n",
                        o2_debug_prefix, services->key,
                        o2_status_to_string(status), status, name,
                        spp->properties ? spp->properties + 1 : ""));
                o2_send_cmd("!_o2/si", 0.0, "siss", services->key, status,
                        name, spp->properties ? spp->properties + 1 : "");
            }
        }
    }
    o2_do_not_reenter--;
}


// handle messages to /_o2/cs/cs that announce when clock sync is obtained
//
void o2_clocksynced_handler(o2_msg_data_ptr msg, const char *types,
                            o2_arg_ptr *argv, int argc, const void *user_data)
{
    // guard against a bridged process sending !_o2/cs/cs:
    if (ISA_BRIDGE((bridge_inst_ptr)
                    (o2n_message_source->application))) return;
    o2string name = argv[0]->s;
    services_entry_ptr services;
    o2_node_ptr entry = o2_service_find(name, &services);
    if (entry) {
        if (entry->tag == PROC_NOCLOCK) {
            o2n_info_ptr info = TO_PROC_INFO(entry)->net_info;
            if (info->net_tag != NET_TCP_CLIENT &&
                info->net_tag != NET_TCP_CONNECTION) {
                printf("ERROR: unexpected net_tag %d on entry %p in "
                       "o2_clocksynced handler\n", info->net_tag, info);
                return;
            }
            entry->tag = PROC_SYNCED;
        } else if (entry->tag == PROC_SYNCED) {
            return;
#ifndef O2_NO_MQTT
        } else if (entry->tag == MQTT_NOCLOCK) {
            entry->tag = MQTT_SYNCED;
        } else if (entry->tag == MQTT_SYNCED) {
            return;
#endif
        } else {
            printf("ERROR: unexpected tag for %s in !_o2/cs/cs message\n",
                   name);
            return;
        }
        clock_status_change(entry, name, O2_REMOTE);
        return;
    }
    O2_DBG(printf("%s ### ERROR in o2_clocksynced_handler, bad service %s\n",
                  o2_debug_prefix, name));
}


static double mean_rtt = 0;
static double min_rtt = 0;


void o2_clockrt_handler(o2_msg_data_ptr msg, const char *types,
             o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_extract_start(msg);
    o2_arg_ptr reply_to_arg = o2_get_next(O2_STRING);
    if (!reply_to_arg) return;
    char *replyto = reply_to_arg->s;
    o2_send(replyto, 0, "sff", o2_ctx->proc->name, mean_rtt, min_rtt);
}


// handler for cs/put, which is a reply to cs/get:
//
static void cs_ping_reply_handler(o2_msg_data_ptr msg, const char *types,
                       o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_arg_ptr arg;
    o2_extract_start(msg);
    if (!(arg = o2_get_next(O2_INT32))) return;
    // if this is not a reply to the most recent message, ignore it
    if (arg->i32 != clock_sync_id) return;
    if (!(arg = o2_get_next(O2_TIME))) return;
    o2_time ref_time = arg->t;
    o2_time now = o2_local_time();
    o2_time rtt = now - clock_sync_send_time;
    // estimate current reference time by adding 1/2 round trip time:
    ref_time += rtt * 0.5;
    int i = ping_reply_count % CLOCK_SYNC_HISTORY_LEN;
    round_trip_time[i] = rtt;
    ref_minus_local[i] = ref_time - now;
    ping_reply_count++;
    O2_DBk(printf("%s got clock reply, ref_time %g, rtt %g, count %d\n",
                  o2_debug_prefix, ref_time, rtt, ping_reply_count));
#ifndef O2_NO_DEBUG
    if (o2_debug & O2_DBk_FLAG) {
        int start, count;
        if (ping_reply_count < CLOCK_SYNC_HISTORY_LEN) {
            start = 0; count = ping_reply_count;
        } else {
            start = ping_reply_count % CLOCK_SYNC_HISTORY_LEN;
            count = CLOCK_SYNC_HISTORY_LEN;
        }
        printf("%s reference minus local:", o2_debug_prefix);
        int k = start;
        for (int j = 0; j < count; j++) {
            printf(" %g", ref_minus_local[k]);
            k = (k + 1) % CLOCK_SYNC_HISTORY_LEN;
        }
        printf("\n%s round trip:", o2_debug_prefix);
        for (int j = 0; j < count; j++) {
            printf(" %g", round_trip_time[start]);
            start = (start + 1) % CLOCK_SYNC_HISTORY_LEN;
        }
        printf("\n");
    }
#endif
    if (ping_reply_count >= CLOCK_SYNC_HISTORY_LEN) {
        // find minimum round trip time
        min_rtt = round_trip_time[0];
        mean_rtt = 0;
        int best_i = 0;
        for (i = 1; i < CLOCK_SYNC_HISTORY_LEN; i++) {
            mean_rtt += round_trip_time[i];
            if (round_trip_time[i] < min_rtt) {
                min_rtt = round_trip_time[i];
                best_i = i;
            }
        }
        mean_rtt /= ping_reply_count;
        // best estimate of ref_minus_local is stored at i
        //printf("*    %s: time adjust %g\n", o2_debug_prefix,
        //       now + ref_minus_local[best_i] - o2_time_get());
        o2_time new_ref = now + ref_minus_local[best_i];
        if (!o2_clock_is_synchronized) {
            o2_clock_synchronized(now, new_ref);
        } else {
            set_clock(now, new_ref);
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
                          o2_arg_ptr *argv, int argc, const void *user_data)
{
    // this function gets called periodically to drive the clock sync
    // protocol, but if the process calls o2_clock_set(), then we
    // become the reference, at which time we stop polling and announce
    // to all other processes that we know what time it is, and we
    // return without scheduling another callback.
    if (is_refclk) {
        o2_clock_is_synchronized = true;
        return; // no clock sync; we're the reference
    }
    clock_sync_send_time = o2_local_time();
    int status = o2_status("_cs");
    if (!found_clock_service) {
        found_clock_service = (status >= 0);
        if (found_clock_service) {
            O2_DBc(printf("%s ** found clock service, is_refclk=%d\n",
                          o2_debug_prefix, is_refclk));
            if (status == O2_LOCAL || status == O2_LOCAL_NOTIME) {
                assert(is_refclk);
            } else { // record when we started to send clock sync messages
                start_sync_time = clock_sync_send_time;
                o2_method_new_internal("/_o2/cs/put", "it",
                        &cs_ping_reply_handler, NULL, false, false);
                o2_method_new_internal("/_o2/cs/rt", "s", &o2_clockrt_handler,
                        NULL, false, false);
                char path[48];
                snprintf(path, 32, "!%s/cs/put", o2_ctx->proc->name);
                clock_sync_reply_to = o2_heapify(path);
            }
        }
    }
    // earliest time to call this action again is clock_sync_send_time + 0.1s:
    o2_time when = clock_sync_send_time + 0.1;
    if (found_clock_service) { // found service, but it's non-local
        if (status < 0) { // we lost the clock service, resume looking for it
            found_clock_service = false;
        } else {
            clock_sync_id++;
            o2_send("!_cs/get", 0, "is", clock_sync_id, clock_sync_reply_to);
            // we're not checking the return value here. The worst that can
            // happen seems to be an error sending to a UDP port, and if that
            // happens, perror() will be called so at least if there is a
            // console an error message will appear. Not much else we can do.
            //
            // run every 0.1 second until at least CLOCK_SYNC_HISTORY_LEN
            // pings have been sent to get a fast start, then ping every
            // 0.5s until 5s, then every 10s.
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
    o2_prepare_to_deliver(o2_message_finish(when, "!_o2/ps", false));
    o2_schedule(&o2_ltsched);
}

static bool clock_initialized = false;

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

    is_refclk = false;
    o2_clock_is_synchronized = false;
    time_callback = NULL;
    time_callback_data = NULL;
    found_clock_service = false;
    ping_reply_count = 0;
    time_offset = 0;
    o2_method_new_internal("/_o2/ps", "", &o2_ping_send_handler,
                           NULL, false, true);
    o2_method_new_internal("/_o2/cu", "i", &catch_up_handler,
                           NULL, false, true);
}


void o2_clock_finish()
{
#ifdef WIN32
	timeEndPeriod(1); // give up 1ms resolution for Windows
#endif
	clock_initialized = false;
    if (clock_sync_reply_to) {
        O2_FREE(clock_sync_reply_to);
        clock_sync_reply_to = NULL;
    }
}    


// cs_ping_handler -- handler for /_cs/get
//   return the reference clock time. Arguments are serial_no and reply_to.
//   send serial_no and current time to reply_to
static void cs_ping_handler(o2_msg_data_ptr msg, const char *types,
                     o2_arg_ptr *argv, int argc, const void *user_data)
{
    o2_arg_ptr serial_no_arg, reply_to_arg;
    o2_extract_start(msg);
    if (!(serial_no_arg = o2_get_next(O2_INT32)) ||
        !(reply_to_arg = o2_get_next(O2_STRING))) {
        return;
    }
    int serial_no = serial_no_arg->i32;
    char *replyto = reply_to_arg->s;
    o2_send(replyto, 0, "it", serial_no, o2_time_get());
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

    // if we are already the reference, then there is nothing more to do.
    if (is_refclk) {
        return O2_SUCCESS;
    }
    
    // start the scheduler, record that we are synchronized now:
    o2_clock_synchronized(new_local_time, new_local_time);
    
    o2_service_new2("_cs\000\000");
    o2_method_new_internal("/_cs/get", "is", &cs_ping_handler,
                           NULL, false, false);
    O2_DBG(printf("%s ** reference clock established, time is now %g\n",
                  o2_debug_prefix, o2_local_time()));
    is_refclk = true;

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
    return (is_refclk ? lt : LOCAL_TO_GLOBAL(lt));
}


o2_time o2_time_get()
{
    o2_time t = o2_local_time();
    return (is_refclk ? t : LOCAL_TO_GLOBAL(t));
}
