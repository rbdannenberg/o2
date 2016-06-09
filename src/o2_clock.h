// o2_clock.h -- header for internally shared clock declarations

int o2_clock_is_synchronized;

void o2_time_init();

void o2_clock_init();

o2_time o2_local_to_global(o2_time local);

int o2_send_clocksync(process_info_ptr process);

int o2_ping_send_handler(o2_message_ptr msg, const char *types,
                         o2_arg_ptr *argv, int argc, void *user_data);

int o2_clocksynced_handler(o2_message_ptr msg, const char *types,
                           o2_arg_ptr *argv, int argc, void *user_data);
