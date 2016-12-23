/* o2_interoperation.h -- header for OSC functions */

int o2_deliver_osc(o2_message_ptr msg, fds_info_ptr info);
void o2_send_osc(osc_entry_ptr service, o2_message_ptr msg);
