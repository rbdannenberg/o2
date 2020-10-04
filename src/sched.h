//  o2_sched.h -- header for os_sched.c

o2_err_t o2_schedule(o2_sched_ptr scheduler);

void o2_sched_finish(o2_sched_ptr s);

void o2_sched_start(o2_sched_ptr s, o2_time start_time);

void o2_sched_initialize(void);

void o2_sched_poll(void);

