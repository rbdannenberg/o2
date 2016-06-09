//
/** \file o2.h
 \brief Top-level O2 API
 
 O2 is a communication protocol for interactive music and media
 applications. 


 In OSC, most applications require users to manually set up connections by
 entering IP and port numbers. In contrast, O2 provides “services.”
 A service is essentially just an OSC server with a name. O2 addresses begin
 with the service name, as if services are the top-level node of a global address
 space. Thus, a complete O2 address would be written simply as
 "/synth/filter/cutoff," where "synth" is the service name.

 A service is created using the functions: o2_add_service(service_name) and
 o2_add_method("address", "types", handler, data), where o2_add_method is called
 to install a handler for each node, and each “address” includes the service name
 as the first node.

 Each o2 process will record their local and remote services' names and number
 in a list. Thus, every time when the process want to send or recieve the message.
 They can just look up in the table and find certain information.

 Note: o2 will just record the remote process for all the services in a single
 process as they have the same ip addredss, port numbers, sockets
 and discover message.

 The major components and concepts of O2 are the
 following:
 
 Application -- a collection of collaborating processes are called
 an "application." Applications are named by a simple ASCII string.
 In O2, all components belong to an application, and O2 supports
 communication only *within* an application. This allows multiple
 independent applications to co-exist and share the same local area
 network.
 
 Host -- (conventional definition) a host is a computer (virtual
 or real) that may host multiple processes.
 
 Process -- (conventional definition) an address space and one or
 more threads. A process can offer one or more O2 services and
 can serve as a client of O2 services in the same or other processes.
 Each process using O2 has one directory of services shared by all
 O2 activity in that process (thus, this O2 implementation can be
 thought of as a "singleton" object). The single O2 instance in a
 process belongs to one and only one application. O2 does not support
 communication between applications.
 
 Service -- an O2 service is a named server that receives and acts
 upon O2 messages. A service is addressed by name. Multiple services
 can exist within one process. A service does not imply a (new) thread
 because services are "activated" by calling o2_poll(). It is up to
 the programmer ("user") to call o2_poll() frequently, and if this
 is done using multiple threads, it is up to the programmer to deal
 with all concurrency issues. O2 is not reentrant, so o2_poll() should
 be called by only one thread. Since there is at most one O2 instance
 per process, a single call to o2_poll() handles all services in the
 process.
 
 Message -- an O2 message, similar to an OSC message, contains an
 address pattern, a type string and a set of values.
 
 Address Pattern -- O2 messages use URL-like addresses, as does OSC,
 but the top-level node in the hierarchical address space is the
 service name. Thus, to send a message to the "note" node of the
 "synth" service, the address pattern might be "/synth/note." The
 OSC pattern specification language is used unless the first character
 is "!", e.g. "!synth/note" denotes the same address as "/synth/note"
 except that O2 can assume that there are no pattern characters such
 as "*" or "[".
 
 */

#ifndef O2_H
#define O2_H
#include <stdlib.h>

#ifdef _WIN32
#define usleep(x) Sleep(x/1000)
#endif

#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif

// Status return values used in o2 functions
#define O2_SUCCESS 0			 // function was successful
#define O2_FAIL (-1)			 // function encountered an error
#define O2_SERVICE_CONFLICT (-2) // path to handler specifies a remote service
#define O2_NO_SERVICE (-3)       // path to handler specifies non-existant service
#define O2_NO_MEMORY (-4) // O2_MALLOC failed to return requested memory
// Status for o2_status function
#define O2_LOCAL_NOTIME 0  // local service, but no clock sync yet
#define O2_REMOTE_NOTIME 1 // remote service, but no clock sync yet
#define O2_BRIDGE_NOTIME 2 // service attached by non-IP link, no clock sync
#define O2_TO_OSC_NOTIME 3 // messages to this service are forwarded to an OSC server
#define O2_LOCAL 4         // local service, and clock is synced
#define O2_REMOTE 5        // remote service, and clock is synced
#define O2_BRIDGE 6        // service attached by non-IP link, clock is synced
#define O2_TO_OSC 7        // messages to this service are forwarded to an OSC server
// Macros for o2 protocol
//#ifdef _WIN32
//#define o2_send o2_send_marker
//#define o2_send_cmd o2_send_cmd_marker
//#define o2_send_osc_message o2_send_osc_message_marker
//#else
/* an internal value, ignored in transmission but check against O2_MARKER in the
 * argument list. Used to do primitive bounds checking */
#define O2_MARKER_A (void *) 0xdeadbeefdeadbeefL
#define O2_MARKER_B (void *) 0xf00baa23f00baa23L
//#endif

extern void *((*o2_malloc)(size_t size));
extern void ((*o2_free)(void *));
void *o2_calloc(size_t n, size_t s);

#define O2_MALLOC(x) (*o2_malloc)(x)
#define O2_FREE(x) (*o2_free)(x)
#define O2_CALLOC(n, s) o2_calloc((n), (s))

// These two macros are for debugging
#define return return
#define O2_DEBUG_PREAMBLE assert(path_tree_table->locations > 0);

// Types for O2 API:
#ifdef _WIN32
#else
#endif
// get uint32_t, etc.:
#include <stdlib.h>
#include <stdint.h>

typedef double o2_time; ///< O2 timestamps are doubles

/** \brief an O2 message
 *
 * There is a free list of 256-byte (or so) messages that are
 * used for most messages. Longer messages need to be allocated
 * and freed as needed. Also, if the freelist runs out, more
 * 256-byte messages are allocated, but they are freed by putting
 * them on the freelist.
 */
typedef struct o2_message {
    struct o2_message *next; // links used for free list and scheduler
    int allocated;           // how many bytes allocated in data part
    int length;              // the length of the message in data part
    struct {
        o2_time timestamp;
        char address[4];     // 4 is not realistic, address and remainder of
                             // the message begin at address[0]
    } data;           // the message (type string and payload follow address)
} o2_message, *o2_message_ptr;


/**
 *  The structure for binary large object.
 *  Can be passed over O2 using the 'b' type. Created by calls to o2_blob_new().
 */
typedef struct o2_blob {
    uint32_t size;  // size of data
    char data[4];   // the data
} o2_blob, *o2_blob_ptr;



/**
 *  An enumeration of the O2 message types.
 */
typedef enum {
    /** basic O2 types */
    O2_INT32 =     'i',     // 32 bit signed integer.
    O2_FLOAT =     'f',     // 32 bit IEEE-754 float.
    O2_STRING =    's',     // NULL terminated string (Standard C).
    O2_BLOB =      'b',     // Binary Large OBject (BLOB) type.
    
    /** extended O2 types */
    O2_INT64 =     'h',     // 64 bit signed integer.
    O2_TIME  =     't',     // OSC time type.
    O2_DOUBLE =    'd',     // 64 bit IEEE-754 double.
    O2_SYMBOL =    'S',     // Used in systems distinguish strings and symbols.
    O2_CHAR =      'c',     // 8bit char variable (Standard C).
    O2_MIDI =      'm',     // 4 byte MIDI packet.
    O2_TRUE =      'T',     // Sybol representing the value True.
    O2_FALSE =     'F',     // Sybol representing the value False.
    O2_NIL =       'N',     // Sybol representing the value Nil.
    O2_INFINITUM = 'I',     // Sybol representing the value Infinitum.

    /** O2 types */
    O2_BOOL =      'B'      // Boolean value returned as either 0 or 1
} o2_type, *o2_type_ptr;


/* Message unpacking */
/**
 * An o2_arg_ptr is a pointer to an O2 message argument. If argument
 * parsing is requested (by setting the parse parameter in o2_add_method),
 * then the handler receives an array of o2_arg_ptrs. If argument parsing
 * is not requested, you have the option of parsing the message one 
 * parameter at a time by calling o2_get_next(), which returns an
 * o2_arg_ptr.
 *    The o2_arg_ptr can then be dereferenced to obtain a value of the 
 * expected type. For example, you could write 
 *    double d = o2_get_next()->d;
 * to extract a parameter of type double. (This assumes that the message
 * is properly formed and the type string indicates that this parameter is
 * a double, or that type coercion was enabled by the coerce flag in
 * o2_add_method().)
 */
typedef union {
    int32_t    i32;  ///< 32 bit signed integer.
    int32_t    i;
    int64_t    i64;  ///< 64 bit signed integer.
    int64_t    h;
    float      f;    ///< 32 bit IEEE-754 float.
    float      f32;
    double     d;    ///< 64 bit IEEE-754 double.
    double     f64;
    char       s[4]; ///< Standard C, NULL terminated string.
    char       S[4]; ///< Standard C, NULL terminated, string. Used in
                     ///  systems which distinguish strings and symbols.
    int        c;    /// Standard C, 8 bit, char, stored as int.
    uint8_t    m[4]; /// A 4 byte MIDI packet.
    o2_time    t;    /// TimeTag value.
    o2_blob    b;    /// a blob
    int32_t    B;    /// a boolean value, either 0 or 1
} o2_arg, *o2_arg_ptr;


extern char *o2_application_name;
extern int o2_stop_flag;

/**
 *  A callback function to receive notification of matching message
 *  arriving in the server or server thread.
 *
 * The return value tells the method dispatcher whether this handler
 * has dealt with the message correctly: a return value of 0 indicates
 * that it has been handled, and it should not attempt to pass it on
 * to any other handlers, non-0 means that it has not been handled and
 * the dispatcher will attempt to find more handlers that match the
 * path and types of the incoming message. (In the current 
 * implementation, all matching handlers receive the message, and there
 * is no default or fall-back for non-handled messages.)
 *
 * On callback the paramters will be set to the following values:
 *
 * @param msg The full message in host byte order.
 * @param types If you set a type string in your method creation call,
 *              then this type string is provided here. If you did not
 *              specify a string, types will be the type string from the
 *              message (without the initial ','). If parse_args and 
 *              coerce_flag were set in the method creation call, 
 *              types will match the types in argv, but not necessarily
 *              the type string or types in msg.
 * @param argv An array of o2_arg types containing the values, e.g. if the
 *             first argument of the incoming message is of type 'f' then 
 *             the value will be found in argv[0]->f. (If parse_args was
 *             not set in the method creation call, argv will be NULL.)
 * @param argc The number of arguments received. (This is valid even if
 *             parse_args was not set in the method creation call.)
 * @param user_data This contains the user_data value passed in the call
 *             to the method creation call.
 */
typedef int (*o2_method_handler)(const o2_message_ptr msg, const char *types,
                                 o2_arg_ptr *argv, int argc, void *user_data);


/** Application provides one of these callback functions to 
 *  o2_set_clock() in order to establish a master clock service,
 *  which is required of one host in the application. Other hosts
 *  synchronise to the time returned by this master clock.
 */
typedef o2_time (*o2_time_callback)(void *rock);


/**
 *  Add a new application and start O2.
 *  If O2 has not been initialized, it is created and intialized.
 *  O2 will begin to establish connections to other instances
 *  with a matching application name.
 *
 *  @param application the name of the application
 *
 *  @return 0 if succeed, -1 if fail, -2 if already running.
 *  (Need symbolic constants O2_SUCCESS, O2_FAIL, O2_RUNNING)
 */
int o2_initialize(char *application_name);

/**
 *  Define the memory function.
 */
int o2_memory(void *((*malloc)(size_t size)), void ((*free)(void *)));


/**
 *  Add a service to the current application.
 *  e.g. If you want to add a clock service, use o2_add_service("clock")
 *
 *  @param service_name the name of the service
 *
 *  @return 0 (O2_SUCCESS) if succeed, -1 (O2_FAIL) if not.
 *
 */
int o2_add_service(char *service_name);


/**
 * Add a handler for an address.
 *
 * @param path      the address including the service name
 * @param typespec  the types of parameters, use "" for no parameters and
 *                      NULL for no type checking
 * @param h         the handler
 * @param user_data pointer saved and passed to handler
 * @param coerce    is true if you want to allow automatic coercion of types.
 *                      Coercion is only enabled if both coerce and parse are
 *                      true.
 * @param parse     is true if you want O2 to construct an argv argument 
 *                      vector to pass to the handle
 *
 * @return O2_SUCCESS if succeed, O2_FAIL if not
 */
int o2_add_method(const char *path, const char *typespec,
                  o2_method_handler h, void *user_data, int coerce, int parse);


/**
 *  Process current O2 messages.
 *  Since O2 does not create a thread and O2 requires active processing
 *  to establish and maintain connections, the O2 programmer (user)
 *  should call o2_poll() periodically, even if not offering a service.
 *  o2_poll() runs a discovery protocol to find and connect to other
 *  processes, runs a clock synchronization protocol to establish valid
 *  time stamps, and handles incoming messages to all services. O2_poll()
 *  should be called at least 10 times per second. Messages can only be
 *  delivered during a call to o2_poll() so more frequent calls will
 *  generally lower the message latency (at the cost of greater CPU
 *  utilization).
 *
 *  @return 0 (O2_SUCCESS) if succeed, -1 (O2_FAIL) if not.
 */
int o2_poll();

/**
 * Run O2.
 * Call o2_poll() at the rate (in Hz) indicated.
 * Returns if a handler sets o2_stop_flag to non-zero.
 */
int o2_run(int rate);

/**
 *  Check the status of the service.
 *
 *
 *  @param service the name of the service
 *
 *  @return O2_FAIL if no service is found,
 *    O2_LOCAL_NOTIME if service is local but we have no clock sync yet,
 *    O2_REMOTE_NOTIME if services is remote but we have no clock sync yet,
 *    O2_BRIDGE_NOTIME if services is attached by non-IP link, but we have 
 *        no clock sync yet,
 *    O2_LOCAL if service is local and we have clock sync,
 *    O2_REMOTE if services is remote and we have clock sync,
 *    O2_BRIDGE if services is attached by non-IP link, and we have clock sync.
 *    In the cases with no clock sync, it is safe to send an immediate message
 *    with timestamp = 0, but non-zero timestamps are meaningless because 
 *    a
 */
int o2_status(const char *service);


/**
 *  Get the network round-trip information.
 *
 * @return If clock is synchronized, return O2_SUCCESS and set
 *   *mean to the mean round-trip time and *min is the minimum
 *   round-trip time of the last 5 (where 5 is the value of 
 *   CLOCK_SYNC_HISTORY_LEN) clock sync requests. Otherwise, 
 *   O2_FAIL is returned and *mean and *min are unaltered.
 */
int o2_roundtrip(double *mean, double *min);


/**
 *  Provide a time source to O2.
 *  Exactly one process per O2 application should provide a master
 *  clock. All other processes synchronize to the master. To become
 *  the master, call o2_set_clock().
 *
 *  @param gettime function to get the time in units of seconds. The
 *  reference may be operating system time, audio system time, MIDI
 *  system time, or any other time source. The times returned by this
 *  function must be non-decreasing and must increase by one second
 *  per second of real time to close approximation. The value may be
 *  NULL, in which case a default time reference will be used.
 *
 *  @parm rock an arbitrary value that is passed to the gettime
 *  function. This may be need to provide context. Use NULL if no
 *  context is required.
 *
 *  @return 0  (O2_SUCCESS) if succeed, -1 (O2_FAIL) if not.
 */
int o2_set_clock(o2_time_callback gettime, void *rock);

/**
 *  Send O2 message with best effort protocol
 *  Normally, this constructs and sends an O2 message via UDP. If the
 *  destination service is reached via some other network protocol
 *  (e.g. Bluetooth), the message is delivered in the lowest latency
 *  protocol available, with no guaranteed delivery.
 *
 *  @param path an address pattern
 *  @param time when to dispatch the message, 0 means right now. In any
 *  case, the message is sent to the receiving service as soon as
 *  possible. If the message arrives early, it will be held at the
 *  service and dispatched as soon as possible after the indicated time.
 *  @param typestring the type string for the message. Each character
 *  indicates one data item. Type codes are as in OSC.
 *  @param ...  the data of the message. There is one parameter for each
 *  character in the typestring.
 *
 *  @return 0 (O2_SUCCESS) if succeed, -1 (O2_FAIL) if not.
 */
#define o2_send(path, time, typestring...) \
    o2_send_marker(path, time, FALSE, typestring, O2_MARKER_A, O2_MARKER_B)

int o2_send_marker(char *path, double time, int tcp_flag, char *typestring, ...);

/**
 *  Send O2 message with a reliable, in-order delivery protocol
 *  Normally, this constructs and sends an O2 message via TCP. If the
 *  destination service is reached via some other network protocol
 *  (e.g. Bluetooth), the message is delivered using the most reliable
 *  protocol available. (Thus, this call is considered a "hint" rather
 *  than an absolute requirement.)
 *
 *  @param path an address pattern
 *  @param time when to dispatch the message, 0 means right now. In any
 *  case, the message is sent to the receiving service as soon as
 *  possible. If the message arrives early, it will be held at the
 *  service and dispatched as soon as possible after the indicated time.
 *  @param typestring the type string for the message. Each character
 *  indicates one data item. Type codes are as in OSC.
 *  @param ...  the data of the message. There is one parameter for each
 *  character in the typestring.
 *
 *  @return 0 (O2_SUCCESS) if succeed, -1 (O2_FAIL) if not.
 */
#define o2_send_cmd(path, time, typestring...) \
    o2_send_marker(path, time, TRUE, typestring, O2_MARKER_A, O2_MARKER_B)

// TODO: add description
int o2_send_message(o2_message_ptr msg, int tcp_flag);

/**
 * Get the estimated synchronized global O2 time. The result
 * could be wildly wrong if 
 *
 *  @return the time
 */
double o2_get_time();


/**
 * Get the real time in seconds using the default local O2 clock
 *
 * @return the local time
 */
double o2_local_time();

/**
 *  Return text representation of an O2 error
 *
 *  @param i error number
 *
 *  @return return the error message in a string
 */
char *o2_get_error(int i);

/**
 *  Release the memory and exit.
 *  o2_finish() will shut down O2 and all services.
 *
 *  @return 0 (O2_SUCCESS) if succeed, -1 (O2_FAIL) if not.
 */
int o2_finish();


// Interoperate with OSC

/**
 *  Create a port to receive OSC messages. Messages are directed to the service.
 *  E.g. if the service is "maxmsp" and the message address is /foo/x, then the
 *  message is directed to and handled by /maxmsp/foo/x.
 *
 *  @param service_name The name of the service
 *  @param port_num     Port number.
 *
 *  @return O2_SUCCESS if succeed, O2_FAIL if not.
 */
int o2_create_osc_port(char *service_name, int port_num, int udp_flag);

/**
 *  Send an OSC message. This basically bypasses O2 and just constructs a message
 *  and sends it directly via UDP to an OSC server.
 *
 *  Note: Before calling o2_send_osc_message, you should first use o2_add_osc_service
 *  to add in the osc service and give it a service name. All this information
 *  will be recorded in the address list. Then you can use the service name to send
 *  the message.
 *
 *  @param service_name The o2 name for the remote osc server, named by calling
 *                      o2_add_osc_service().
 *  @param path         The osc path.
 *  @param typestring   The type string for the message
 *  @param ...          The data values to be transmitted.
 *
 *  @return O2_SUCCESS if succeed, O2_FAIL if not.
 */
#define o2_send_osc_message(service_name, path, typestring...) \
    o2_send_osc_message_marker(service_name, path, typestring, \
                               O2_MARKER_A, O2_MARKER_B)

int o2_send_osc_message_marker(char *service_name, const char *path,
                               const char *typestring, ...);

/**
 *  Create an o2 service. When the service recieve any messages, it will
 *  automatically send the message to the osc server.
 *
 *  @param service_name The o2 service name.
 *  @param ip           The ip address of the osc server.
 *  @param port_num     The port number of the osc server.
 *  @param tcp_flag     Send OSC message via TCP protocol, in which case
 *                      port_num is the TCP server port, not a connection.
 *
 *  @return O2_SUCCESS if succeed, O2_FAIL if not.
 */
int o2_delegate_to_osc(char *service_name, char *ip, int port_num, int tcp_flag);

/* low-level message building.
 *     Rather than passing all parameters in one call, these functions 
 * can build a message one parameter at a time. The functions operate
 * on a "hidden" message, so the code is not reentrant. You should begin
 * by calling o2_start_send() to allocate a message. Then call o2_add_?()
 * functions to add parameters. Finally, call either o2_finish_send() or 
 * o2_finish_send_cmd() to send the message.
 */

int o2_start_send();
int o2_add_int32(int32_t i);
int o2_add_float(float f);
int o2_add_symbol(char *s);
int o2_add_string(char *s);
int o2_add_blob(o2_blob *b);
int o2_add_int64(int64_t i);
int o2_add_time(o2_time t);
int o2_add_double(double d);
int o2_add_char(char c);
int o2_add_midi(uint8_t *m);
int o2_add_true();
int o2_add_false();
int o2_add_bool(int i);
int o2_add_nil();
int o2_add_infinitum();
/// finish and return the message, does not send it,
///  returns NULL if an error occurs
o2_message_ptr o2_finish_message(o2_time time, char *address);
int o2_finish_send(o2_time time, char *address);
int o2_finish_send_cmd(o2_time time, char *address);

/*   These functions can retrieve message arguments one-at-a-time.
There are some hidden state variables to keep track of the state
of unpacking, so these functions are not reentrant.
 * Arguments are returned into a union type declared above.
 *
 * Before calling o2_get_next(), call o2_start_extract() to
 * initialize internal state to parse, extract, and coerce
 * message arguments.
 */
int o2_start_extract(o2_message_ptr msg);

/// return a pointer to the next message parameter or NULL
/// if no more parameters (or a type error). The parameter
/// points into the message or to o2_coerced_value if type
/// coercion is required. This storage is only valid until
/// the next call to o2_get_next, so normally, you copy
/// the return value immediately. If the value is a pointer
/// (string, symbol, midi data, blob), then the value was
/// not copied to o2_coerced_value, so there should never
/// be the need to immediately copy the data pointed to.
/// However, the storage for the value is the message, and
/// the message will be freed when the handler returns, so
/// pointers to strings, symbols, midi data, and blobs
/// *must not* be used after the handler returns.
///
/* Example 1: 
Note: call o2_add_method() with type_spec = "id", h = my_handler, 
      coerce = false, parse = false. In this case, since there is
      no type coercion, type_spec must match the message exactly,
      so o2_get_next() should always return a non-null o2_arg_ptr.

int my_handler(o2_message_ptr msg, char *types,
               o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(msg);
    // assume that we expect an int32 and a double argument
    int32_t my_int = o2_get_next('i')->i32;
    double my_double = o2_get_next('d')->d;
    ...
}

Example 2: Type coercion and type checking.
Note: call o2_add_method() with type_spec = NULL, h = my_handler, 
      coerce = false, parse = false. In this case, even though 
      coerce is false, there is no type_spec, so the handler will
      be called without type checking. We could check the
      actual message types (given by types), but here, we will
      coerce into our desired types (int32 and double) if possible.
      Since type coercion can fail (e.g. string will not be converted
      to number, not even "123"), we need to check the return value
      from o2_get_next(), where NULL indicates incompatible types.

int my_handler(o2_message_ptr msg, char *types,
               o2_arg_ptr *argv, int argc, void *user_data)
{
    o2_start_extract(msg);
    // assume that we expect an int32 and a double argument
    o2_arg_ptr ap = o2_get_next('i');
    if (!ap) return O2_FAIL; // parameter cannot be coerced
    int32_t my_int = ap->i32;
    o2_arg_ptr ap = o2_get_next('d');
    if (!ap) return O2_FAIL; // parameter cannot be coerced
    double my_double = ap->d;
    ...
}

*/
/// int my_handler

o2_arg o2_coerced_value;
o2_arg_ptr o2_get_next(char type_code);

/* Scheduling */
/**
 *  We have both real time schedule and virtual time schedule. But the virtual time
 *  schedule will be initialized as unstarted. Only if the local process has get the
 *  clock sync, will the virtual time schedule be started.
 */

/// The structure of the schedule (real time and virtual time schedule).
///
/// Messages are stored in the table modulo their timestamp, so the
/// table acts sort of like a hash table (this is also called the
/// timing wheel structure). Messages are stored as linked lists sorted
/// by increasing timestamps when there are collisions.
///
#define O2_SCHED_TABLE_LEN 128
typedef struct o2_sched {
    int64_t last_bin;
    double last_time;
    o2_message_ptr table[O2_SCHED_TABLE_LEN];
} o2_sched, *o2_sched_ptr;

extern o2_sched gtsched, ltsched;
extern o2_sched_ptr o2_active_sched; // the scheduler that should be used
extern int ltsched_started;


void o2_schedule(o2_sched_ptr scheduler, o2_message_ptr msg);

#endif /* O2_H */
