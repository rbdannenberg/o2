/* processes.h - manage o2 processes and their service lists */

/* Roger B. Dannenberg
 * April 2020
 */


// tag values:
#define PROC_TCP_SERVER    20 // the local process
#define PROC_NOMSGYET      21 // client or server remote proc initially
#define PROC_NOCLOCK       22 // not-synced client or server remote proc
#define PROC_SYNCED        23 // clock-synced client or server remote proc
#define PROC_TEMP          24 // temporary client connection for discovery

typedef struct proc_info {
    int tag;
    o2n_info_ptr net_info;
    // process name, e.g. "128.2.1.100:55765". This is used so that
    // when we add a service, we can enumerate all the processes and
    // send them updates. Updates are addressed using this name field.
    // name is "owned" by this process_info struct and will be deleted
    // when the struct is freed:
    o2string name;
    // O2_HUB_REMOTE indicates this remote process is our hub
    // O2_I_AM_HUB means this remote process treats local process as hub
    // O2_NO_HUB means neither case is true
    int uses_hub;
    o2n_address udp_address;
} proc_info, *proc_info_ptr;


// is this a proc_info structure?
#define ISA_PROC(p) (((p)->tag) >= PROC_TCP_SERVER && \
                     ((p)->tag) <= PROC_TEMP)
#define PROC_IS_REMOTE(proc) ((proc)->tag == PROC_NOCLOCK || \
                              ((proc)->tag == PROC_SYNCED))
#ifdef O2_NO_DEBUG
#define TO_PROC_INFO(node) ((proc_info_ptr) (node))
#else
#define TO_PROC_INFO(node) (assert(ISA_PROC((proc_info_ptr) (node))), \
                            ((proc_info_ptr) (node)))
#endif


const char *o2_node_to_ipport(o2_node_ptr node);

int o2_status_from_proc(o2_node_ptr entry, const char **process);

int o2_net_accepted(o2n_info_ptr info, o2n_info_ptr conn);

int o2_net_connected(o2n_info_ptr info);

int o2_net_info_remove(o2n_info_ptr info);

void o2_proc_info_show(proc_info_ptr proc);

proc_info_ptr o2_create_tcp_proc(int net_tag, const char *ip, int port);

int o2_processes_initialize();

void o2_proc_info_free(proc_info_ptr proc);
