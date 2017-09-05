/* o2_sched.c -- scheduling
 */

/* Overview:
 
 There are two schedulers here: o2_gtsched, and o2_ltsched. They are identical,
 but one should use "real" local time, and the other should use
 synchronized clock time. There is no code here for smoothing the
 synchronized clock or making sure it does not go backward. (Well, maybe
 if it goes backward, nothing happens.)
 
 The algorithm is the "timing wheel" algorithm: times are quantized to
 "bins" of 10ms. These are "hashed" into a table using modulo arithmetic,
 so each time you poll for work, you linearly search bins for activity.
 Assuming you poll every 10ms or less, on average, you will only look
 into one bin. Each bin has a simple linked list of messages with
 timestamps. The lists are sorted in increasing time order. This means
 the insertion cost is O(N), but assuming messages are distributed evenly
 into the bins, the cost is reduced by a factor of SCHED_TABLE_LEN == 128.
 My guess is that few messages are actually scheduled, so in practice
 there should not be many collisions, e.g. the typical list length is
 0 or 1, making this a constant-time insert algorithm. The dispatch
 time is O(1) because lists are sorted with earliest time first. You
 also have to look at 100 bins per second whether anything is scheduled
 or not, but if you call poll every 10ms or so, scanning bins is not an
 issue. If time jumps by d, there is O(d) cost to scan the bins. However,
 the time constant is small: scanning even 10s worth of bins only requires
 looking at 1000 bins.
 
 Two difficult issues are: (1) the floating point time can be in the
 middle of a bin, so we need to be careful not to dispatch messages
 in the future, and since there may be messages in the bin that were
 not dispatched on the previous poll, we have to begin each poll by
 reexamining the bin where we stopped in the previous poll.
 
 (2) if time jumps ahead by more than SCHED_TABLE_LEN, we'll "wrap around"
 when we scan for messages and might schedule something out of order. To
 avoid this, we detect jumps and dispatch in 1s increments (the table size
 is 1.28s) to schedule messages in time order. (See more comments on this
 below.)
 
 This code assumes message structures have a "next" field so that we can
 make a linked list of messages, and also a "time" field with the scheduled
 time.
 
 */

#include "ctype.h"
#include "o2_internal.h"
#include "o2_message.h"
#include "o2_sched.h"
#include "o2_clock.h"
#include "o2_send.h"


#define SCHED_BIN(time) ((int64_t) ((time) * 100))
#define SCHED_BIN_TO_INDEX(b) ((b) & (O2_SCHED_TABLE_LEN - 1))  // Get modulo
#define SCHED_INDEX(t) (SCHED_BIN_TO_INDEX(SCHED_BIN(t) ))

o2_sched o2_gtsched, o2_ltsched;
o2_sched_ptr o2_active_sched = &o2_gtsched;
int o2_gtsched_started = FALSE;  // cannot use o2_gtsched until clock is in sync

/* KEEP THIS FOR DEBUGGING
 void sched_debug_print(const char *msg, sched_ptr s)
 {
 printf("sched_debug_print from %s: s %p, last_bin %lld, last_time %g\n",
 msg, s, s->last_bin, s->last_time);
 for (int i = 0; i < SCHED_TABLE_LEN; i++) {
 for (o2_message_ptr m = s->table[i]; m; m = m->next) {
 printf("    %d: %p %s\n", i, m, m->path);
 }
 }
 printf("\n");
 }
 */


void o2_sched_finish(o2_sched_ptr s)
{
    for (int i = 0; i < O2_SCHED_TABLE_LEN; i++) {
        o2_message_list_free(s->table[i]);
    }
    o2_gtsched_started = FALSE;
}


void o2_sched_start(o2_sched_ptr s, o2_time start_time)
{
    memset(s->table, 0, sizeof(s->table));
    s->last_bin = SCHED_BIN(start_time);
    if (s == &o2_gtsched) {
        o2_gtsched_started = TRUE;
    }
    s->last_time = start_time;
}

void o2_sched_initialize()
{
    // TODO: is start_time right?
    o2_sched_start(&o2_ltsched, o2_local_time());
    o2_gtsched_started = FALSE;
}

/*DEBUG
int scheduled_for(o2_sched_ptr s, double when)
{
    for (int i = 0; i < O2_SCHED_TABLE_LEN; i++) {
        o2_message_ptr msg = s->table[i];
        while (msg) {
            if (msg->data.timestamp == when) return TRUE;
            msg = msg->next;
        }
    }
    return FALSE;
}
DEBUG*/

// Schedule a message for a particular service. Assumes that the service is local.
// Use o2_message_send() if you do not know if the service is local or not.
//
int o2_schedule(o2_sched_ptr s, o2_message_ptr m)
{
    o2_time mt = m->data.timestamp;
    if (mt <= 0 || mt < s->last_time) {
        // it was probably a mistake to schedule the message when the timestamp
        // is not in the future, but we'll try a local delivery anyway
        o2_msg_data_deliver(&m->data, m->tcp_flag, NULL, NULL);
        o2_message_free(m);
        return O2_SUCCESS;
    }
    if (s == &o2_gtsched && !o2_gtsched_started) {
        // cannot schedule in the future until there is a valid clock
        o2_message_free(m);
        return O2_NO_CLOCK;
    }
    int64_t index = SCHED_INDEX(mt);
    o2_message_ptr *m_ptr = &(s->table[index]);
    
    // find insertion point in list so that messages are sorted
    while (*m_ptr && ((*m_ptr)->data.timestamp <= mt)) {
        m_ptr = &((*m_ptr)->next);
    }
    // either *m_ptr is null or it points to a time > mt
    m->next = *m_ptr;
    *m_ptr = m;
    // assert(scheduled_for(s, m->data.timestamp));
    return O2_SUCCESS;
}


// This looks for messages <= now and delivers them
//
static void sched_dispatch(o2_sched_ptr s, o2_time run_until_time)
{
    // examine slots between last_bin and bin, inclusive
    // this is tricky: if time has advanced more than SCHED_TABLE_LEN,
    // then we'll "wrap around" the table and if nothing is done to
    // stop it, messages could be dispatched not in time order. We'll
    // detect the wrap-around and advance time 1s at a time to avoid
    // the problem.
    while (s->last_time + 1 < run_until_time) {
        sched_dispatch(s, s->last_time + 1);
    }
    int64_t bin = SCHED_BIN(run_until_time);
    // now we know that we have less than 1s to go to catch up, so the
    // table will not wrap around
    while (s->last_bin <= bin) {
        o2_message_ptr *m_ptr = &(s->table[SCHED_BIN_TO_INDEX(s->last_bin)]);
        while (*m_ptr && ((*m_ptr)->data.timestamp <= run_until_time)) {
            o2_message_ptr m = *m_ptr;
            *m_ptr = m->next; // unlink message m
            o2_active_sched = s; // if we recursively schedule another message,
            // use this same scheduler.
            // careful: this can call schedule and change the table
            O2_DBt(if (m->data.address[1] != '_' &&
                       !isdigit(m->data.address[1]))
                       o2_dbg_msg("sched_dispatch", &m->data, NULL, NULL));
            O2_DBT(if (m->data.address[1] == '_' ||
                       isdigit(m->data.address[1]))
                       o2_dbg_msg("sched_dispatch", &m->data, NULL, NULL));
            o2_message_send_sched(m, FALSE); // don't assume local and call
            // o2_msg_data_deliver; maybe this is an OSC message
        }
        s->last_bin++;
    }
    s->last_bin--; // we should revisit this bin next time
    s->last_time = run_until_time;
}


// call this periodically
void o2_sched_poll()
{
    sched_dispatch(&o2_ltsched, o2_local_now);

    if (o2_gtsched_started) {
        sched_dispatch(&o2_gtsched, o2_global_now);
    }
}
