// sharedmem.h -- a brige to shared memory O2 service
//
// Roger B. Dannenberg
// Dec 2020


extern O2queue o2sm_incoming;
extern Bridge_protocol *o2sm_protocol;

O2message_ptr get_messages_reversed(O2queue *head);

class O2sm_protocol : public Bridge_protocol {
public:
    O2sm_protocol() : Bridge_protocol("O2sm") { }
    virtual ~O2sm_protocol() {
        O2_DBb(printf("%s deleting O2sm_protocol@%p\n", o2_debug_prefix,
                      this));
        o2_method_free("/_o2/o2sm");  // remove all o2sm support handlers

        // free all messages arriving from shared memory instances:
        o2sm_incoming.free();
        o2sm_protocol = NULL;

    /* THIS IS SLIGHTLY DIFFERENT FROM Bridge_protocol::remove_services(),
       BUT THAT WILL BE CALLED FROM ~Bridge_protocol(), SO THIS CODE
       SHOULD NOT BE NECESSARY:
        // by now, shared memory thread should be shut down cleanly,
        // so no more O2sm_info objects (representing connections to
        // threads) exist. If they do, then in principle they should
        // be removed, but they have shared queues with their
        // thread. We can at least remove any services that are
        // offered by the thread, although the thread could then try
        // to offer another service.  In practice, the order should
        // be: 1. Shut down thread(s), 2. call o2_finish(),
        // 3. o2_finish() will delete o2sm_protocol, bringing us here
        // safely.
        // Remove O2sm_info services based on
        // Services_entry::remove_services_by():
        Vec<Services_entry *> services_list;
        Services_entry::list_services(services_list);
        for (int i = 0; i < services_list.size(); i++) {
            Services_entry *services = services_list[i];
            for (int j = 0; j < services->services.size(); j++) {
                Service_provider *spp = &services->services[j];
                Bridge_info *bridge = (Bridge_info *) (spp->service);
                if (ISA_BRIDGE(bridge) && bridge->proto == o2sm_protocol) {
                    O2_DBb(printf("%s removing service %s delegating to "
                                  "O2sm_protocol@%p\n", o2_debug_prefix,
                                  services->key, this));
                    services->proc_service_remove(services->key, bridge,
                                                  services, j);
                    break; // can only be one of services offered by bridge,
                    // and maybe even services was removed, so we should move
                    // on to the next service in services list
                }
            }
        }
     */
    }
    
    virtual O2err bridge_poll() {
        O2err rslt = O2_SUCCESS;
        O2message_ptr msgs = get_messages_reversed(&o2sm_incoming);
        while (msgs) {
            O2message_ptr next = msgs->next;
            msgs->next = NULL; // remove pointer before it becomes dangling
            // printf("O2sm_protocol::bridge_poll sending %s\n",
            //       msgs->data.address);
            O2err err = o2_message_send(msgs);
            // return the first non-success error code if any
            if (rslt) rslt = err;
            msgs = next;
        }
        return rslt;
    }

};


class O2sm_info : public Bridge_info {
public:
    O2queue outgoing;

    O2sm_info() : Bridge_info(o2sm_protocol) {
        tag |= O2TAG_SYNCED;
    }

    virtual ~O2sm_info() {
        assert(this);
        // remove all services delegating to this connection
        proto->remove_services(this);
        outgoing.free();
    }

    // O2sm is always "synchronized" with the Host because it uses the
    // host's clock. Also, since 3rd party processes do not distinguish
    // between O2sm services and Host services at this IP address, they
    // see the service status according to the Host status. Once the Host
    // is synchronized with the 3rd party, the 3rd party expects that
    // timestamps will work. Thus, we always report that the O2sm
    // process is synchronized.
    virtual bool local_is_synchronized() { return true; }

    // O2sm does scheduling, but only for increasing timestamps.
    virtual bool schedule_before_send() { return false; }

    virtual O2err send(bool block) {
        bool tcp_flag;
        O2message_ptr msg = pre_send(&tcp_flag);
        assert(msg->next == NULL);
        // send taps first because we will lose ownership of msg to o2sm
        O2err err = send_to_taps(msg);
        // we have a message to send to the service via shared
        // memory -- find queue and add the message there atomically
        // printf("O2sm_info sending %p to thread, %s\n", msg,
        //        msg->data.address);
        outgoing.push((O2list_elem *) msg);
        return err;
    }
    
    void poll_outgoing();


#ifndef O2_NO_DEBUG
    virtual void show(int indent) {
        Bridge_info::show(indent);
    }
#endif
    // virtual O2status status(const char **process);  -- see Bridge_info

    // Net_interface:
    O2err accepted(Fds_info *conn) { return O2_FAIL; } // we are not a server
    O2err connected() { return O2_FAIL; } // we are not a TCP client
};


O2_EXPORT O2sm_info *o2_shmem_inst_new();

// return number of shared memory instances
O2_EXPORT int o2_shmem_inst_count();
