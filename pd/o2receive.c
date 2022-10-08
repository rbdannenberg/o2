/* o2receive - pd class for o2. */
/* Roger B. Dannenberg
 * July 2022
 */

#include <stdlib.h>
#include <string.h>
#include "o2ensemble.h"  // includes o2 and some helper functions for pd o2
#include "x_vexp.h"
#include "z_libpd.h"
#ifdef _WIN32
# include <malloc.h> /* MSVC or mingw on windows */
#elif defined(__linux__) || defined(__APPLE__) || defined(HAVE_ALLOCA_H)
# include <alloca.h> /* linux, mac, mingw, cygwin */
#else
# include <stdlib.h> /* BSDs for example */
#endif


/*  MULTIPLE RECEIVERS, SAME ADDRESS
    To deal with the possibility of multiple receivers on the same address,
keep a list of lists of lists:
        servicenode is a service descriptor with a service name and a
            list of addresses with handlers. A next field links to
            other servicenodes
        addressnode is an address descriptor with a full path string
            and a list of handlers. A next field links to other
            addressnodes.
        handlernode is a handler descriptor with a t_o2rcv pointer and
            a pointer to the containing addressnode, which is also the
            data item registered with the O2 handler callback. A next
            field links to other handlernodes of the same address.
   When a message arrives, deliver the message data to each handler in
the handlernode.
   When a t_o2rcv object is deleted, search the addressnode object's 
handlernode and remove the matching handler. If the list becomes empty,
remove the object from addressnode.
   Another problem is service-only handlers that receive every message
to that service. We point to a service-only addressnode from a special
field (it is not on the addresses list). All messages will be directed
to the address node for the service.  To continue delivering to
addresses that match the path, we need to search all addresses for a
match and deliver to those as well. To facilitate this, the
whole-service address has a backpointer to the parent servicenode, and
we search all addresses on the servicenode for a match.
   If the address was service only, then deleting the last handler of
the service will not leave any handlers for paths within the service.
To solve this, any address that represents the entire service has a 
backpointer to the servicenode, so we traverse this list and reinstall
handlers for each remaining address.
   Another problem is that if there is a general handler for every
service message and we add a new handler for a sub-node of the
service, this will conflict: you cannot have a handler for both
/service and /service/subnode. For this reason, each servicenode has a
field wholeservice pointing to an addressnode that handles the whole
service (if any).  If this addressnode exists, then we do not need to
install a handler for a subnode to O2 because it will be handled by
searching the service's addressnode. We still install a handlernode
for each o2receive object because a whole-service handler has to
search for a match to addresses with subnodes. Also, if all
whole-service o2receive objects are deleted, then we traverse the
other addresses and install a handler for each.

    POLLING
    o2ensemble is already polling at the Pd tick rate, so rather than 
schedule another time object, we will rely on o2ensemble to call o2_poll,
and callbacks from O2 will activate o2receive when messages arrive.
*/


servicenode *find_service(const char *service)
{
    servicenode *s;
    for (s = o2ens_services; s; s = s->next) {
        if (strcmp(s->service, service) == 0) {
            return s;
        }
    }
    return NULL;
}


addressnode *find_address(servicenode *service, const char *path)
{
    addressnode *a;
    for (a = service->addresses; a; a = a->next) {
        if (strcmp(a->path, path) == 0) {
            return a;
        }
    }
    return NULL;
}


void o2rcv_check_types(int *argc, t_atom **argv, const char **types)
{
    if (*argc > 1 && (*argv)[0].a_type == A_SYMBOL &&
        (*argv)[1].a_type == A_SYMBOL &&
        !strcmp((*argv)->a_w.w_symbol->s_name, "-t")) {
        *types = (*argv)[1].a_w.w_symbol->s_name;
        (*argc) -= 2;
        (*argv) += 2;
    }
}


// set up O2 message handling for Pd receiver object. 
//   service - the service name, no slashes
//   path - the full path, e.g. /service/node1/node2, owned by caller
//   types - the typestring for Pd message
//
int receive_new(t_o2rcv *receiver, const char *service, const char *path,
                const char *types)
{
    // find the servicenode
    int is_new_service = false;  // used for error recovery
    servicenode *s = find_service(service);
    if (!s) {  // no servicenode found; make one
        // new service, so O2 needs to create/advertise the service
        post("o2receive creating new service %s\n", service);
        if (o2_ensemble_name) {
            if (o2ens_error_report(&receiver->x_obj, "o2_service_new", 
                                   o2_service_new(service))) {
                return 1; // error
            }
        }
        s = (servicenode *) getbytes(sizeof(servicenode));
        s->next = o2ens_services;
        o2ens_services = s;
        s->service = service; // service is from symbol, so it's permanent
        s->wholeservice = NULL;
        is_new_service = true;
    }

    addressnode *a = find_address(s, path);
    if (!a) {  // no match found, so create a new address:
        post("o2receive adding new address %s\n", path);
        a = (addressnode *) getbytes(sizeof(addressnode));
        size_t len = strlen(path) + 1;
        a->path = (const char *) getbytes(len);
        strncpy((char *) (a->path), path, len);
        a->types = types;
        a->service = NULL;
        a->receivers = NULL;
        if (!strchr(path + 1, '/')) {  // it's just a service name
            a->service = s;  // backpointer to find other receivers
        }
        // address is linked into service addresses in any case:
        a->next = s->addresses;
        s->addresses = a;
        printf("new address @ %p, path %s types %s service %p\n",
               a, a->path, a->types, a->service); fflush(stdout);
        // new address, so need a new O2 handler for path unless there's
        // a top-level service handler
        if (!s->wholeservice && o2_ensemble_name) {
            // printf("took branch for !wholeservice and o2_ensemble_name\n");
            // fflush(stdout);
            O2err err;
            post("o2receive adding new handler for %s\n", path);
            if (a->service) {  // install top level service handler
                s->wholeservice = a;
                // No types, no type coercion, no type checking here
                err = o2_method_new(path, NULL, o2rcv_handler,
                                    (void *) a, false, false);
            } else {
                err = o2_method_new(path, types, o2rcv_handler,
                                    (void *) a, false, false);
            }
            if (o2ens_error_report(&receiver->x_obj, "o2_method_new", err)) {
                // detach (new) addressnode from service:
                if (a == s->wholeservice) {
                    s->wholeservice = NULL;
                } else {
                    s->addresses = a->next;
                }
                freebytes(a, sizeof(addressnode));
                if (is_new_service) {
                    // delete the service because there was no existing handler
                    post("o2receive removing service %s\n", service);
                    o2ens_services = o2ens_services->next; // remove s from list
                    o2ens_error_report(&receiver->x_obj, "o2_service_free", 
                                       o2_service_free(service));
                    freebytes(s, sizeof(servicenode));
                }
                return 1; // error
            }
        }
    } else if ((a->types && types && strcmp(a->types, types) != 0) ||
               (a->types && !types) || (!a->types && types)) {
        // mismatched types -- install new types
        pd_error(receiver, "new type spec for %s: %s", path, types);
        printf("mismatched type for %s\n", path); fflush(stdout);
    }
    printf("adding object to address\n"); fflush(stdout);
    // add object to the address
    receivernode *r = (receivernode *) getbytes(sizeof(receivernode));
    r->receiver = receiver;
    r->next = a->receivers;
    a->receivers = r;
    receiver->address = a;
    printf("new receiver @ %p address %p\n", r, a); fflush(stdout);
    return 0;
}


void remove_o2rcv_record(t_o2rcv *r)
{
    servicenode *snode;
    if (!r->servicename) {
        return;  // this receiver is not on a list if no servicename
    }
    snode = find_service(r->servicename);
    if (!snode) {
        pd_error(r, "internal error: o2receive has no servicenode");
    }
    if (!r->address) {
        pd_error(r, "internal error: o2receive has no addressnode");
        return;
    }
    receiver_delete(snode, r->address, r, "o2receive");
    r->servicename = NULL;
    r->address = NULL;
}


static void get_address(t_o2rcv *x,  t_symbol *s, int argc, t_atom *argv)
{
    const char *types = NULL;
    char path[128];
    int last = 0;  // index of EOS
    path[0] = 0;
    remove_o2rcv_record(x);
    // check for -t <types> before or after path nodes
    o2rcv_check_types(&argc, &argv, &types);
    while (argc && argv->a_type == A_SYMBOL) {  /* get next node */
        const char *nodename = argv->a_w.w_symbol->s_name;
        int nodelen = strlen(nodename);
        if (strcmp(nodename, "-t") == 0) {
            break;
        }
        if (!x->servicename) {  // keep first node as service name
            x->servicename = nodename;
        }
        if (last + nodelen + 2 >= 128) {
            pd_error(x, "O2 address is too long");
            return;
        }
        path[last++] = '/';
        strcpy(path + last, nodename);
        last += nodelen;
        argc--; argv++;
    }
    o2rcv_check_types(&argc, &argv, &types);
    
    if (argc) {  /* should be done by now */
        pd_error(x, "O2 address: extra parameters ignored");
    }
    printf("get_address calls receive_new path %s\n", path); fflush(stdout);
    receive_new(x, x->servicename, path, types);
}


void o2rcv_address(t_o2rcv *x,  t_symbol *s, int argc, t_atom *argv)
{
    post("o2rcv: address");
    show_receivers("before o2rcv_address");
    remove_o2rcv_record(x);
    show_receivers("after remove_o2rcv_record");
    get_address(x, s, argc, argv);
    show_receivers("after o2rcv_address");
}


void o2rcv_types(t_o2rcv *x, t_symbol *types)
{
    post("o2rcv: types %s", types->s_name);
    char *typestr = (char *) types->s_name;
    if (typestr[0] == 0) typestr = NULL;
    for (int i = 0; typestr[i]; i++) {
        if (strchr("ifhdtsSc", typestr[i]) == NULL) {
            pd_error(x, "o2receive: types string %s has invalid character %c",
                     typestr, typestr[i]);
            typestr = NULL;
        }
    }
    addressnode *a = x->address;
    if (!a) {
        pd_error(x, "o2receive: must have address before setting types");
        return;
    } else if ((a->types && typestr && strcmp(a->types, typestr) != 0) ||
               (a->types && !typestr) || (!a->types && typestr)) {
        pd_error(x, "new type spec for %s: %s", a->path, typestr);
        printf("mismatched type for %s\n", a->path); fflush(stdout);
    }
    a->types = typestr;
}


void o2rcv_disable(t_o2rcv *x,  t_symbol *s, int argc, t_atom *argv)
{
    post("o2rcv: disable");
    if (o2_ensemble_name == NULL) {
        pd_error(x, "O2 is not initialized");
    } else {
        remove_o2rcv_record(x);
    }
}


/* this is a pointer to the class for "o2rcv", which is created in the
   "setup" routine below and used to create new ones in the "new" routine. */
t_class *o2rcv_class;

/* this is called when a new "o2rcv" object is created. */
void *o2rcv_new(t_symbol *s, int argc, t_atom *argv)
{
    t_o2rcv *x = (t_o2rcv *)pd_new(o2rcv_class);
    printf("o2rcv_new called\n"); fflush(stdout);
    x->servicename = NULL;
    x->address = NULL;
    if (argc > 0) {  // get service
        printf("o2rcv_new calls get_address\n"); fflush(stdout);
        get_address(x, s, argc, argv);
    }
    outlet_new(&x->x_obj, &s_list);
    printf("new o2receive object %p outlet %p\n", x, x->x_obj.ob_outlet); fflush(stdout);
    post("o2rcv_new");
    return (void *)x;
}


void o2rcv_free(t_o2rcv *x)
{
    printf("o2rcv_free called\n"); fflush(stdout);
    if (x->servicename) {
        remove_o2rcv_record(x);
    }
}


/* this is called once at setup time, when this code is loaded into Pd. */
void o2receive_setup(void)
{
    post("o2rcv_setup");
    o2rcv_class = class_new(gensym("o2receive"), (t_newmethod)o2rcv_new,
                            (t_method)o2rcv_free, sizeof(t_o2rcv), 0,
                            A_GIMME, 0);
    class_addmethod(o2rcv_class, (t_method)o2rcv_address,
                                         gensym("address"), A_GIMME, 0);
    class_addmethod(o2rcv_class, (t_method)o2rcv_types,
                                         gensym("types"), A_SYMBOL, 0);
    class_addmethod(o2rcv_class, (t_method)o2rcv_disable,
                                         gensym("disable"), 0);
}

