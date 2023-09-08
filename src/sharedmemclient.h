// sharedmemclient.h -- to be included by shared memory processes
// undefines a bunch of unsafe o2 functions,
// defines a bunch of functions to only use in a shared memory process
//
// Roger B. Dannenberg
// August 2020

#undef o2_send
#undef o2_send_cmd
#undef O2_HANDLER_ARGS
#define O2SM_HANDLER_ARGS const O2msg_data_ptr msg, const char *types, \
                          O2arg_ptr *argv, int argc, const void *user_data
#define o2_time_get() DO_NOT_CALL_THIS_EXCEPT_FROM_O2_PROCESS
#define o2_initialize() DO_NOT_CALL_THIS_EXCEPT_FROM_O2_PROCESS
#define o2_finish() DO_NOT_CALL_THIS_EXCEPT_FROM_O2_PROCESS
#define o2_service_new(s) DO_NOT_CALL_THIS_EXCEPT_FROM_O2_PROCESS
#define o2_method_new  DO_NOT_CALL_THIS_EXCEPT_FROM_O2_PROCESS
#define o2_poll() DO_NOT_CALL_THIS_EXCEPT_FROM_O2_PROCESS
#define o2_send_finish() DO_NOT_CALL_THIS_EXCEPT_FROM_O2_PROCESS

// these are safe o2 function calls, but we'll define alternate o2sm_ names
#define o2sm_send_start o2_send_start
#define o2sm_add_float o2_add_float
#define o2sm_add_string_or_symbol o2_add_string_or_symbol
#define o2sm_add_symbol o2_add_symbol
#define o2sm_add_string o2_add_string
#define o2sm_add_blob o2_add_blob
#define o2sm_add_blob_data o2_add_blob_data
#define o2sm_add_int64 o2_add_int64
#define o2sm_add_double_or_time o2_add_double_or_time
#define o2sm_add_double o2_add_double
#define o2sm_add_time o2_add_time
#define o2sm_add_int32 o2_add_int32
#define o2sm_add_char o2_add_char
#define o2sm_add_midi o2_add_midi
#define o2sm_add_only_typecode o2_add_only_typecode
#define o2sm_add_true o2_add_true
#define o2sm_add_false o2_add_false
#define o2sm_add_tf o2_add_tf
#define o2sm_add_bool o2_add_bool
#define o2sm_add_nil o2_add_nil
#define o2sm_add_infinitum o2_add_infinitum
#define o2sm_add_start_array o2_add_start_array
#define o2sm_add_end_array o2_add_end_array
#define o2sm_add_vector o2_add_vector

#define o2sm_extract_start o2_extract_start
#define o2sm_get_next o2_get_next

#define o2sm_send(path, time, ...)         \
    o2sm_send_marker(path, time, false,    \
                   __VA_ARGS__, O2_MARKER_A, O2_MARKER_B)

#define o2sm_send_cmd(path, time, ...) \
    o2sm_send_marker(path, time, true, \
                   __VA_ARGS__, O2_MARKER_A, O2_MARKER_B)

O2err o2sm_send_marker(const char *path, double time, bool tcp_flag,
                          const char *typestring, ...);
O2err o2sm_send_finish(O2time time, const char *address, bool tcp_flag);
O2err o2sm_message_send(O2message_ptr msg);

int o2sm_get_id(); 
void o2sm_poll();
O2time o2sm_time_get();
O2err o2_shmem_inst_finish(Bridge_info *inst);
void o2sm_initialize(O2_context *ctx, Bridge_info *inst);
O2err o2_shmem_finish();
O2message_ptr o2sm_get_message(Bridge_info *inst);
// o2sm_service_new announces services to the O2 process.
O2err o2sm_service_new(const char *service, const char *properties);

// o2sm_method_new attaches a handler to a path. Unless O2SM_PATTERNS
// is defined, this function does not require a service to have been
// created, saving a check for the service when a message arrives.
// But without a call to o2sm_service_new, no messages will be sent to
// this path.
O2err o2sm_method_new(const char *path, const char *typespec,
                      O2method_handler h, void *user_data,
                      bool coerce, bool parse);
void o2sm_finish();

