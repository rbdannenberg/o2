// o2_clock.h -- header for internally shared clock declarations

void o2_time_initialize(void);

void o2_clocksynced_handler(o2_msg_data_ptr msg, const char *types,
                            o2_arg_ptr *argv, int argc, void *user_data);

void o2_ping_send_handler(o2_msg_data_ptr msg, const char *types,
                          o2_arg_ptr *argv, int argc, void *user_data);

void o2_clockrt_handler(o2_msg_data_ptr msg, const char *types,
                        o2_arg_ptr *argv, int argc, void *user_data);

void o2_clock_initialize(void);

void o2_clock_finish(void); // used when shutting down O2

o2_time o2_local_to_global(o2_time local);

void o2_clock_ping_at(o2_time when);






