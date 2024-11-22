// o2_bridge.h -- headers to support extensions for non-IP transports
//
// Roger B. Dannenberg
// April 2018

/**
 * /brief Add a new transport to O2 process
 *
 * A bridge is an extension to O2 to allow communication with devices
 * over non-TCP/IP O2 protocols. For example, you can bridge to
 * Bluetooth, WebSockets, shared-memory threads or even
 * microcontrollers with TCP/IP where a simpler point-to-point
 * connection is desired.  (This case is implemented as the "o2lite"
 * protocol.)  It is intended that bridges can be developed and linked
 * to O2 without changing or recompiling the O2 library. Therefore,
 * all bridges have status O2_BRIDGE or O2_BRIDGE_NOTIME rather than,
 * say, O2_LITE.
 *
 * A Bridge_protocol provides a unique name for this bridge, typically
 * reflecting the transport that it services, e.g. "O2lite" or
 * "WebSock".  Note the 7-character limit. Every service using the
 * protocol has a pointer to the protocol so it can be identified and
 * so that Bridge_info instances can be located if/when you shut down
 * the protocol. A Bridge_protocol implements #bridge_poll and a
 * deconstructor.
 *
 * Each protocol subclasses Bridge_protocol and Bridge_info. Details
 * on methods:
 *
 * The #bridge_poll method will normally accept data from the
 * transport, marshall the data into an O2 message, and invoke O2
 * functions to send the message.  Note that there is no special way
 * to inject messages into O2 from a bridge. Just use O2 functions.
 * For any protocol using sockets, notice that there is already
 * polling for TCP and UDP messages, so there may be no need to
 * implement #bridge_poll. The inherited method just returns
 * O2_SUCCESS.
 *
 * The Bridge_protocol deconstructor should free any allocated memory
 * associated with the bridge. Let ~Bridge_protocol locate and remove
 * all corresponding Bridge_info instances.
 *
 * A Bridge_info constructor should initialize its tag with or without
 * the O2TAG_OWNED_BY_TREE bit. If set, then if a 
 * Service_provider is removed and its service member is the Bridge_info,
 * then the Bridge_info will be deleted. If you allocate an instance of
 * a Bridge_info subclass for each service offered by the bridged process,
 * then set the bit. If the Bridge_info instance is shared by multiple
 * services, then do not set the bit, but make sure some other means is
 * provided to delete the Bridge_info when its protocol is removed. E.g.
 * if the Bridge_info corresponds to a socket (Fds_info), you can search
 * the sockets for owner members that are Bridge_info's and implement
 * the protocol.
 *
 * A Bridge_info represents a remote process or processes that provide
 * services.  Each subclass implements #send, #show,#proto,
 * #local_is_synchronized and the deconstructor.
 *
 * #send should usually call #pre_send to obtain the message to be
 * delivered (caller now owns the message) and other common
 * processing. After processing the message, free the message. To
 * avoid memory leaks or potential reports of memory leaks, the bridge
 * should be prepared to free the message with #O2_FREE if the
 * deconstructor is called before the bridge frees the message or at
 * least when the Bridge_protocol is deconstructed. Typically, the
 * message is held until the transport can deliver all of its data;
 * then use #O2_FREE to free the message.
 *
 * The #show method should usually call Bridge::show(indent) and then
 * print a newline. The subclass can optionally print additional
 * information.
 *
 * The #proto method should return the subclassed Bridge_protocol
 * instance. E.g. an O2lite_info object's #proto method returns
 * o2lite_protocol, which is an instance of O2lite_protocol.
 *
 * The #local_is_synchronized method returns a boolean that says
 * whether the bridge is synchronized with the global O2 clock. See
 * also o2node.h.
 *
 * A subclass of Bridge_info is a proxy in the O2 host process for a
 * remote process. Therefore, it can be referenced by many
 * Service_entry objects (one for each service offered by the bridged
 * process). When deleted, the proxy must remove itself from all
 * services that reference it.
 *
 * To forward messages from O2, the bridge must create a local O2 service. Use
 * #o2_service_provider_new with the bridge as the `service` parameter
 * and the local process, `o2_ctx->proc` as the `proc` parameter.  The
 * properties parameter can be any property string, but typically is NULL.
 * Note that the service name is an #O2string, which means it is zero 
 * padded to a 32-bit boundary to support rapid hash function computation.
 * This is not a normal C string, which has no such padding.
 * 
 * Bridges may install handlers for "/_o2/<protocol>/dy". The handler should
 * ensure that the first parameter matches #o2_ensemble_name and the rest 
 * of the parameters are protocol specific. E.g. "/_o2/o2lite/dy" is used
 * by O2lite processes to connect to O2 when o2lite is initialized.
 */


#ifndef O2_NO_BRIDGES

#ifdef O2_NO_DEBUG
#define TO_BRIDGE_INFO(node) ((Bridge_info *) (node))
#else
#define TO_BRIDGE_INFO(node) (assert(ISA_BRIDGE(((Bridge_info *) (node)))),\
                              ((Bridge_info *) (node)))
#endif

class Bridge_info;

O2_EXPORT int o2_bridge_next_id;

class O2_CLASS_EXPORT Bridge_protocol : public O2obj {
public:
    char protocol[8];
    Bridge_protocol(const char *name);

    // Bridge_info that share this protocol.
    Vec<Bridge_info *> instances;

    virtual ~Bridge_protocol();

    virtual O2err bridge_poll() { return O2_SUCCESS; }

    O2err remove_services(Bridge_info *bi);

    int find_loc(int id);
    
    Bridge_info *find(int id) {
        int i = find_loc(id);
        return (i < 0 ? NULL : instances[i]);
    }

    void remove_instance(int id) {
        int loc = find_loc(id);
        if (loc >= 0) {
            instances.remove(loc);
        }
    }

};


class O2_CLASS_EXPORT Bridge_info : public Proxy_info {
public:
    int id;  // a unique id for the bridged process
    Bridge_protocol *proto;  // this could almost be information returned
    // by a virtual method rather than allocating a pointer in every
    // instance, but we need it for the destructor, where you cannot
    // call virtual methods.
    
    Bridge_info(Bridge_protocol *proto_) :
            Proxy_info(NULL, O2TAG_BRIDGE) {
        proto = proto_;
        id = o2_bridge_next_id++;
        O2_DBw(dbprintf("new Bridge_info@%p id %d\n", this, id));
        proto->instances.push_back(this);
    }
    virtual ~Bridge_info() {
        O2_DBw(dbprintf("deleting Bridge_info@%p id %d\n", this, id));
        proto->remove_instance(id);
    }
  
    virtual O2err send(bool block) = 0;

    O2err send_to_taps(O2message_ptr msg);

#ifndef O2_NO_DEBUG
    virtual void show(int indent);
#endif
    virtual bool local_is_synchronized() { return IS_SYNCED(this); }
    O2status status(const char **process) {
        if (process) {
            *process = get_proc_name();
        }
        return (o2_clock_is_synchronized && IS_SYNCED(this)) ?
               O2_BRIDGE : O2_BRIDGE_NOTIME;
    }
};    

void o2_bridge_csget_handler(O2msg_data_ptr msgdata, int seqno,
                             const char *replyto);
void o2_bridge_cscs_handler();
void o2_bridge_cscs_handler(O2msg_data_ptr msgdata, const char *types,
                            O2arg_ptr *argv, int argc, const void *user_data);
void o2_bridge_sv_handler(O2msg_data_ptr msgdata, const char *types,
                          O2arg_ptr *argv, int argc, const void *user_data);
void o2_bridge_st_handler(O2msg_data_ptr msgdata, const char *types,
                          O2arg_ptr *argv, int argc, const void *user_data);
void o2_bridge_ls_handler(O2msg_data_ptr msgdata, const char *types,
                          O2arg_ptr *argv, int argc, const void *user_data);

/// \brief print a representation of the bridge for debugging.
void o2_bridge_show(Bridge_info *bridge);

int o2_bridge_find_protocol(const char *protocol_name,
                            Bridge_protocol **protocol);

int o2_poll_bridges(void);

void o2_bridges_initialize(void);

void o2_bridges_finish(void);

extern Bridge_protocol *o2lite_protocol;

#endif
