//  O2sched.h -- header for os_sched.c

// we use a bin for every 10ms, but this is arbitrary: too small and you
// have to examine many bins to advance time (if polling is not frequent);
// too large and you get more collisions, which means linear list insertion
// time to make sure messages in the same bin are sorted in time order.
// Also, the table size should be greater than 1 second because if time
// between polling is >1s, we simulate polling every 1s until we catch up.
// Otherwise we could dispatch messages out of order due to wrap-around.
#define O2_SCHED_BIN(time) ((int64_t) ((time) * 100))
#define O2_SCHED_BIN_TO_INDEX(b) ((b) & (O2_SCHED_TABLE_LEN - 1))  // Get modulo
#define O2_SCHED_INDEX(t) (O2_SCHED_BIN_TO_INDEX(O2_SCHED_BIN(t) ))

O2err o2_schedule(O2sched_ptr scheduler);

void o2_sched_finish(O2sched_ptr s);

void o2_sched_start(O2sched_ptr s, O2time start_time);

void o2_sched_initialize(void);

void o2_sched_poll(void);

