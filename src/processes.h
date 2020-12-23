/* processes.h - manage o2 processes and their service lists */

/* Roger B. Dannenberg
 * April 2020
 */

#ifndef O2_NO_HUB
// hub flags are used to tell receiver of /dy message what to do.
typedef enum hub_type {
    O2_NOT_HUB = 0,          // sender is normal discovery broadcast
    O2_BE_MY_HUB = 1,        // receiver is the hub
    O2_HUB_CALL_ME_BACK = 2, // receiver is the hub, but hub needs to close
                             //      socket and connect to sender
    O2_I_AM_HUB = 3,         // sender is the hub (and client), OR if this
                             //      is an o2n_info.proc.hub
    O2_HUB_REMOTE = 4        // remote is HUB
} hub_type;
#endif

class Proc_info : public Proxy_info {
public:
    // store process name in key, e.g. "@128.2.1.100:55765". This is used
    // so that when we add a service, we can enumerate all the processes and
    // send them updates. Updates are addressed using this name field.
    // name is "owned" by this process_info struct and will be deleted
    // when the struct is freed:
#ifndef O2_NO_HUB
    // hub_remote indicates this remote process is our hub
    // i_am_hub means this remote process treats local process as hub
    // no_hub means neither case is true
    hub_type uses_hub;
#endif
    Net_address udp_address;

    Proc_info() : Proxy_info(NULL, O2TAG_PROC) {
#ifndef O2_NO_HUB
        uses_hub = O2_NOT_HUB;
#endif
        memset(&udp_address, 0, sizeof udp_address);
    }
    virtual ~Proc_info();

    O2err send(bool block);

    // Implement the Net_interface:
    O2err accepted(Fds_info *conn);
    O2err connected();
    // O2err deliver(); is inherited from Proxy_info

    bool local_is_synchronized() { o2_send_clocksync_proc(this);
                                   return IS_SYNCED(this); }
    virtual O2status status(const char **process) {
        if (process) {
            *process = get_proc_name();
        }
        return (o2_clock_is_synchronized && IS_SYNCED(this)) ?
               (this == o2_ctx->proc ? O2_LOCAL : O2_REMOTE) :
               (this == o2_ctx->proc ? O2_LOCAL_NOTIME : O2_REMOTE_NOTIME);
    }

    const char *get_proc_name() {
        if (key) return key;
        if (this == o2_ctx->proc) return "_o2";
        return NULL;
    }

    // send() is inherited from Proxy_info

#ifndef O2_NO_DEBUG
    void show(int indent);
#endif

    static Proc_info *create_tcp_proc(int tag, const char *ip, int port);

};


#ifdef O2_NO_DEBUG
#define TO_PROC_INFO(node) ((proc_info_ptr) (node))
#else
#define TO_PROC_INFO(node) (assert(ISA_PROC((Proc_info *) (node))), \
                            ((Proc_info *) (node)))
void o2_show_sockets(void);

#endif


void o2_processes_initialize(void);
