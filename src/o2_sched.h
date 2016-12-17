//  o2_sched.h -- header for os_sched.c

void o2_start_a_scheduler(o2_sched_ptr s, o2_time start_time);
void o2_sched_poll();
int o2_check_clock();
void o2_schedule_on(o2_sched_ptr s, o2_message_ptr m, generic_entry_ptr service);

