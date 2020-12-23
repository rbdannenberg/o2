//  O2sched.h -- header for os_sched.c

O2err o2_schedule(O2sched_ptr scheduler);

void o2_sched_finish(O2sched_ptr s);

void o2_sched_start(O2sched_ptr s, O2time start_time);

void o2_sched_initialize(void);

void o2_sched_poll(void);

