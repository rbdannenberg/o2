// o2_clock.h -- header for internally shared clock declarations

void o2_time_initialize(void);

void o2_clocksynced_handler(O2msg_data_ptr msg, const char *types,
                 O2arg_ptr *argv, int argc, const void *user_data);

void o2_ping_send_handler(O2msg_data_ptr msg, const char *types,
               O2arg_ptr *argv, int argc, const void *user_data);

void o2_clockrt_handler(O2msg_data_ptr msg, const char *types,
             O2arg_ptr *argv, int argc, const void *user_data);

void o2_clock_initialize(void);

void o2_start_cs_pings(void);

void o2_clock_finish(void); // used when shutting down O2

O2time o2_local_to_global(O2time local);

O2err o2_send_clocksync_proc(Proxy_info *proc);

void o2_clock_status_change(Proxy_info *info);

