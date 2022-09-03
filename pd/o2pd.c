// o2pd.c -- shared code for o2ensemble, o2receive, o2send, o2property
//
// Roger B. Dannenberg
// Aug 2022

// this library will include all of O2 (via libo2_static.a) and
// the global shared variable o2ens_services, which is a 3-level
// directory (o2ens_services, implemented as linked lists) of
// 1. all services offered
//    2. all addresses we have handlers for
//       3. the o2receive objects for each address
// Typically, there will be only one o2receive object for a given
// address, but we allow for receiving a single O2 message and
// fanning out to multiple o2receive objects.
//
// The o2ens_services is here because it is referenced by both
// o2ensemble and o2receive. Maybe another solution would be to
// put libo2_static.a and o2ens_services in the o2ensemble library
// and explicitly load that library when o2receive is loaded.

#include <stdlib.h>
#include <string.h>
#include <float.h>
#include "o2ensemble.h"
#include "m_pd.h"
// TODO: remove this:
#include "debug.h"


// limit exists to avoid overflowing stack, but there's no clear
// upper bound. Does anyone really want to send 100 parameters in
// one message?
#define MAX_O2_ARGS 100

#define ATOMS_ALLOCA(x, n) ((x) = (t_atom *)((n) <= MAX_O2_ARGS ? \
                                   alloca((n) * sizeof(t_atom)) : NULL));

servicenode *o2ens_services = NULL;


void install_handlers(t_object *x, addressnode *anode)
{
    // we cleared all handling for this service, but there are
    // addresses with subpaths; install a handler for each address
    for (anode; anode; anode = anode->next) {
        // no recovery from this error: we just print errors and 
        // pretend like a method was installed.
        o2ens_error_report(x, "o2_method_new",
                o2_method_new(anode->path, anode->types, o2rcv_handler,
                              (void *) anode, false, false));
    }
}


// delete a servicenode
//
// snode is the address of the pointer to servicenode (either a
// &(servicenode->next) or &o2ens_services)
//
void service_delete(t_object *x, servicenode **snode, int free_it, char *src)
{
    if ((*snode)->addresses) {
        pd_error(x, "internal error: service_delete snode has addresses");
        return;
    }
    post("%s deleting servicenode for %s\n", src, (*snode)->service);
    if (free_it) {
        o2ens_error_report(x, "o2_service_free",
                           o2_service_free((*snode)->service));
    }
    servicenode *deleteme = *snode;
    *snode = (*snode)->next;
    freebytes(deleteme, sizeof(servicenode));
}
    


// delete a receiver from the lists
//
// If the receiver is the only receiver for the address, remove the
// addressnode.
// If the service has no more addresses, remove the servicenode, and
// if O2 is initialied, free the corresponding O2 service.
// If the receiver is the last receiver to handle ALL messages to this
// service, and if O2 is initialized, clear the service's
// wholeservice addressnode and add handlers for all the other
// addresses.
//
void receiver_delete(servicenode *snode, addressnode *anode,
                     t_o2rcv *receiver, const char *src)
{
    DBG printf("receiver_delete called to delete %p\n", receiver);
    // find and remove receiver on the list anode->receivers
    receivernode **r;
    for (r = &(anode->receivers); *r; r = &((*r)->next)) {
        if ((*r)->receiver == receiver) {
            receivernode *delete_me = *r;
            DBG printf("%s removing receiver record with path %s\n",
                       src, anode->path);
            (*r) = delete_me->next;  // splice *r out of the list
            freebytes(delete_me, sizeof(receivernode));
            break;
        }
    }
    // later, we'll need to know if we removed all receivers of
    // the whole service:
    int receiver_receives_whole_service = (anode->service != NULL);
    // now, if anode is empty, free the method corresponding to anode
    if (!anode->receivers) {
        if (o2_ensemble_name) {  // is O2 initialized?
            DBG printf("o2receiver uninstalling handler for %s\n", anode->path);
            o2ens_error_report(&receiver->x_obj, "o2_method_free", 
                               o2_method_free(anode->path));
        }
        // and free anode, which can be wholeservice too:
        addressnode **aptr;
        if (snode->wholeservice == anode) {
            snode->wholeservice = NULL;
        }
        for (aptr = &snode->addresses; *aptr; aptr = &((*aptr)->next)) {
            if (*aptr == anode) {  // found it
                DBG printf("%s removing address record for %s\n", src,
                           anode->path);
                *aptr = anode->next;
                if (anode->path) {
                    freebytes((void *) anode->path,
                              strlen(anode->path) + 1);
                }
                freebytes(anode, sizeof(addressnode));
                anode = NULL;
                break;
            }
        }
        if (!snode->addresses && !snode->wholeservice) {  // no handlers left!
            servicenode **sptr;
            for (sptr = &o2ens_services; *sptr; sptr = &((*sptr)->next)) {
                if (*sptr == snode) {  // found it
                    service_delete(&receiver->x_obj, sptr,
                                   o2_ensemble_name != NULL, "o2receive");
                    break;
                }
            }
        } else if (receiver_receives_whole_service && o2_ensemble_name) {
            install_handlers(&receiver->x_obj, snode->addresses);
        }
    }
}


// extract parameters from message, coercing into symbols and floats
// O2 types ihfdtTFBcI -> float, types sSN -> symbol (including "nil")
// O2 types b (blob) and m (midi) and v (vector) return 1 (error).
// pdmsg is the Pd message, preallocated with n slots of t_atom. n is
// also the length of msgtypes.
// returns actual size of pdmsg on completion, or -1 on error.
int unpack_any_message(addressnode *a, O2msg_data_ptr msg, const char *msgtypes,
                       t_atom *pdmsg, int n)
{
    DBG printf("unpack_any_message a %p msgtypes %s pdmsg %p n %d\n",
               a, msgtypes, pdmsg, n); fflush(stdout);
    DBG printf("unpack msg %p msg->address %s\n", msg, msg->address);

#ifdef O2_NO_DEBUG
#error oops - O2_NO_DEBUG is defined
#endif
    
    o2_dbg_msg("unpack_any_message", NULL, msg, NULL, NULL);
    fflush(stdout);
    o2_extract_start(msg);
    for (int i = 0; i < n; i++) {
        char c = msgtypes[i];
        O2arg_ptr arg;
        DBG printf("unpack type %c\n", c); DBG fflush(stdout);
        switch (c) {
          case 'i': case 'h': case 'f': case 'd': 
          case 't': case 'T': case 'F': case 'B': 
            arg = o2_get_next(O2_FLOAT);
            DBG printf("set float %g\n", arg->f); DBG fflush(stdout);
            SETFLOAT(pdmsg + i, arg->f);
            break;
          case 's': case 'S':
            arg = o2_get_next(O2_SYMBOL);
            DBG printf("set symbol arg %p symbol %s\n", arg, arg->S); DBG fflush(stdout);
            SETSYMBOL(pdmsg + i, gensym((const char *) (arg->S)));
            break;
          case 'I':
            arg = o2_get_next(O2_INFINITUM);
            SETFLOAT(pdmsg + i, FLT_MAX);
            break;
          case 'c':
            arg = o2_get_next(O2_CHAR);
            SETFLOAT(pdmsg + i, arg->c);
            break;
          default: {
                t_object *x = &a->receivers->receiver->x_obj;
                pd_error(x, "dropping O2 message with types %s", msgtypes);
                return -1;  // error
            }
        }
    }
    return n;  // no error
}


// unpacks O2 message and forms Pd message according to msgtypes string
// pdmsg is the Pd message, preallocated with n slots of t_atom. n is
// also the length of msgtypes.
// returns actual size of pdmsg on completion, or -1 on error.
int unpack_typed_message(addressnode *a, O2msg_data_ptr msg, 
                         const char *msgtypes, const char *types,
                         t_atom *pdmsg, int n)
{
    DBG printf("unpack msgtypes %s types %s n %d\n", msgtypes, types, n);
    DBG fflush(stdout);

    const char *DROPMSG = "dropping O2 message with types %s, expected %s";
    t_object *x = &a->receivers->receiver->x_obj;
    DBG printf("unpack x %p msg %p msg->address %s\n", x, msg, msg->address);
    DBG fflush(stdout);

    if (types && (n != (int)strlen(types))) {
        pd_error(x, DROPMSG, msgtypes, types);
        return -1; // error
    }
    o2_extract_start(msg);
    for (int i = 0; i < n; i++) {
        char c = msgtypes[i];
        DBG printf("unpack c %c\n", c); DBG fflush(stdout);
        O2arg_ptr arg;
        switch (c) {
          case 'i': case 'h': case 'f': case 'd': 
          case 't': case 'T': case 'F': case 'B': 
            if (types && types[n] != 'f') {
                pd_error(x, DROPMSG, msgtypes, types);
                return -1;
            }
            arg = o2_get_next(O2_FLOAT);
            DBG printf("unpack arg %p\n", arg); DBG fflush(stdout);
            DBG printf("unpack calling SETFLOAT %d %g\n", i, arg->f);
            DBG fflush(stdout);
            SETFLOAT(pdmsg + i, arg->f);
            break;
          case 's': case 'S':
            if (types && types[n] != 's') {
                pd_error(x, DROPMSG, msgtypes, types);
                return -1;
            }
            arg = o2_get_next(O2_SYMBOL);
            SETSYMBOL(pdmsg + i, gensym((const char *) (arg->S)));
            break;
          case 'I':
            if (types && types[n] != 'f') {
                pd_error(x, DROPMSG, msgtypes, types);
                return -1;
            }
            arg = o2_get_next(O2_INFINITUM);
            SETFLOAT(pdmsg + i, FLT_MAX);
            break;
          case 'c':
            if (types && types[n] != 'f') {
                pd_error(x, DROPMSG, msgtypes, types);
                return -1;
            }
            arg = o2_get_next(O2_CHAR);
            SETFLOAT(pdmsg + i, arg->c);
            break;
          default:
            pd_error(x, DROPMSG, msgtypes, types ? types : "");
            return -1;  // error
        }
    }
    return n;  // no error    
}


// this is referenced by both o2ensemble and o2receive:
//
void o2rcv_handler(O2_HANDLER_ARGS)
{
    DBG printf("o2rcv_handler called user_data %p types %s\n",
               user_data, types); DBG fflush(stdout);

    receivernode *r;
    addressnode *a = (addressnode *) user_data;
    int msglen = strlen(types);
    t_atom *pdmsg;
    ATOMS_ALLOCA(pdmsg, msglen);
    if (!pdmsg) {
        return;  // error - O2 message has too many parameters or alloca failed
    }
    DBG printf("o2rcv_handler called %s msglen %d\n", a->path, msglen);
    DBG fflush(stdout);
    if (a->service) {  // this particular addressnode gets all messages to this
        // service, so we have to search ALL handlers for this service. A 
        // handler should receive the message if (1) the handler is for the 
        // entire service (in which case a->service is non-null) OR (2) the
        // handler address matches the message address.
        char *msg_addr = msg->address;
        DBG printf("start searching for handlers: a->service %p\n", a->service);
        DBG fflush(stdout);
        for (a = a->service->addresses; a; a = a->next) {
            DBG printf("       a %p\n", a); fflush(stdout);
            DBG printf("       a->service %p\n", a->service); fflush(stdout);
            DBG printf("       a->path %s\n", a->path); fflush(stdout);
            // if a has only a service name, handle the message
            if (a->service) {
                if ((msglen = unpack_any_message(a, msg, types,
                                                 pdmsg, msglen)) < 0) {
                    return;  // error - could not unpack for Pd
                }
            } else if (strcmp(msg_addr + 1, a->path + 1) == 0) {
                if ((msglen = unpack_typed_message(a, msg, types,
                                      a->types, pdmsg, msglen)) < 0) {
                    return;  // error - could not unpack for Pd or type error
                }
            }
            for (r = a->receivers; r; r = r->next) {
                outlet_list(r->receiver->x_obj.ob_outlet,
                            &s_list, msglen, pdmsg);
            }
        }
    } else {  // deliver typed message to handlers
        DBG printf("calling unpack_typed_message\n"); DBG fflush(stdout);
        if ((msglen = unpack_typed_message(a, msg, types,
                              a->types, pdmsg, msglen)) < 0) {
            return;  // error - could not unpack for Pd or type error
        }
        DBG printf("unpack_typed_message returned %d receivers %p\n",
                   msglen, a->receivers); DBG fflush(stdout);
        for (r = a->receivers; r; r = r->next) {
            DBG printf("outlet_list for r=%p\n", r); DBG fflush(stdout);
            DBG printf("    receiver %p\n", r->receiver); DBG fflush(stdout);
            DBG printf("    outlet %p\n", r->receiver->x_obj.ob_outlet);
            DBG fflush(stdout);
            outlet_list(r->receiver->x_obj.ob_outlet, &s_list, msglen, pdmsg);
        }                
    }
}


void show_receivers(const char *info)
{
    DBG {
    printf("RECEIVERS %s\n", info);
    for (servicenode *s = o2ens_services; s; s = s->next) {
        printf("  Service %p %s", s, s->service);
        if (s->wholeservice) {
            printf(" wholeservice %p", s->wholeservice);
        }
        printf("\n");
        for (addressnode *a = s->addresses; a; a = a->next) {
            printf("    Address %p %s types %s", a, a->path, a->types);
            if (a->service) {
                printf(" (whole)service(for) %p", a->service);
            }
            printf("\n");
            for (receivernode *r = a->receivers; r; r = r->next) {
                printf("      Receiver %p -> %p\n", r, r->receiver);
            }
        }
    }
    } // END DBG
}
