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
#include <stdarg.h>
#include <float.h>
#include "o2ensemble.h"
#include "m_pd.h"
#include "o2pd.h"
#include "assert.h"
// TODO: remove this:
#include "debug.h"


// limit exists to avoid overflowing stack, but there's no clear
// upper bound. Does anyone really want to send 100 parameters in
// one message?
#define MAX_O2_ARGS 100

#define ATOMS_ALLOCA(x, n) ((x) = (t_atom *)((n) <= MAX_O2_ARGS ? \
                                   alloca((n) * sizeof(t_atom)) : NULL));

#ifdef WIN32
#define alloca(x) _alloca(x)
#endif


// make a copy of str on the heap
const char *o2pd_heapify(const char *str)
{
    const char *hstr = (const char *) getbytes(strlen(str) + 1);
    strcpy((char *) hstr, str);
    return hstr;
}


//---------------- address data structure ----------------
//
// INVARIANTS: (here "address" refers to an "addressnode"):
// INV_1) any t_o2rcv whose address == a will be in the list a->receivers
// INV_2) any t_o2rcv on a list a->receivers will have address == a
// INV_3) for each x: t_o2rcv on a->receivers, x->path == a->path and 
//      x->type a->types
// INV_4) every address a is on the list addresses
// INV_5) for every address a, there is at least one receiver and 
//      there was a call to o2_method_new(a->path, a->types, ...)
// INV_6) after each o2_method_new(a->path, a->types, ...), either a is
//      on the list addresses, or there was a matching o2_method_free()
// INV_7) for every service in the list addresses, there was an o2_service_new()
// INV_8) after each o2_service_new(), either there remains an a using that
//      service, or there was a matching o2_service_free()
//
// Note: When an o2receiver changes its address (path or types) to a 
// new one using the same service, there should be a temporary
// violation of the service invariant 8, where the addressnode for the
// former address is removed but the addressnode for the new address
// has not been created yet.
//
// interface for receive datastructure
static addressnode *addresses = NULL;  // all active addresses

// create an addressnode
// maintains INV_1 - INV_4
// caller should ensure INV_5 - INV_7
//
static addressnode *create_addressnode(const char *path, const char *types)
{
    addressnode *addr = NEW_OBJ(addressnode);
    addr->path = o2pd_heapify(path);
    addr->types = types;  // types is owned by caller and permanent,
                          // no need to copy
    addr->receivers = NULL;
    DBG2 printf("create_addressnode created %p\n", addr);

    // add the address to list
    addr->next = addresses;
    addresses = addr;

    return addr;
}


// add a t_o2rcv to an addressnode
// maintains INV_1 and INV_2, INV_4, INV_6 - INV_8
// caller should ensure INV_3, INV_5
static void add_o2rcv_to_address(t_o2rcv *x, addressnode *addr)
{
    x->address = addr;
    x->next = addr->receivers;
    addr->receivers = x;
}


// remove a t_o2rcv from an addressnode
// maintains INV_1 and INV_2, INV_4, INV_6 - INV_8
// caller should ensure INV_3, INV_5
static void remove_o2rcv_from_address(t_o2rcv *x)
{
    t_o2rcv **next = &(x->address->receivers);
    while (*next && *next != x) {
        next = &((*next)->next);
    }
    if (*next) {  // found it: *next == x
        *next = x->next;
    }  // else x was not on the list
    x->next = NULL;
    x->address = NULL;
}


// "delete method" for addressnode
// maintains INV_1, INV_2, INV_3
// caller should ensure INV_4 - INV_8
static void free_addressnode(addressnode *addr)
{
    addr->next = NULL;
    addr->types = NULL;
    // unlink receivers from this address
    while (addr->receivers != NULL) {
        remove_o2rcv_from_address(addr->receivers);
    }
    FREE_STRING(addr->path);
    FREE_OBJ(addr);
}


// remove addr from addresses and delete it
// if o2mf, also call o2_method_free to free the address in O2
// but do not ever call o2_service_free(). Client must do that.
// maintains INV_1 - INV_5
// caller should ensure INV_6, INV_7, INV_8
static void remove_addressnode(addressnode *addr)
{
    addressnode **next = &addresses;
    while (*next && *next != addr) {
        next = &((*next)->next);
    }
    if (*next) {  // found it
        *next = addr->next;
        free_addressnode(addr);
    }
}


// remove all addresses without o2_method_free
// maintains INV_1 - INV_5
// caller should ensure INV_6 - INV_8 (by calling o2_finish)
void remove_all_addressnodes(void)
{
    while (addresses != NULL) {
        remove_addressnode(addresses);
    }
}


// test if full path begins with service
static bool path_has_service(const char *path, const char *service)
{
    char c;
    path++;
    service++;
    while ((c = *service++)) {
        if (*path++ != c) {
            return false;  // did not match all of service
        }
    }
    // we matched all of service; *path corresponds to the EOS of service
    c = *path;
    return (c == 0   /* matched completely OR ...     */ ||
            c == '/' /* matched first node completely */);
}


// test if any address has service
static bool check_for_service(const char *service)
{
    for (addressnode *a = addresses; a; a = a->next) {
        if (path_has_service(a->path, service)) {
            return true;
        }
    }
    return false;
}


// copy the service from service into path
static void extract_service(const char *path, char *service, size_t service_size)
{
    // Copy the first character (/) of path
    service[0] = path[0];
    
    // Copy characters up to first slash or until service buffer is full
    size_t i = 1;
    while (path[i] != '/' && path[i] != '\0' && i < service_size - 1) {
        service[i] = path[i];
        i++;
    }
    
    // Null-terminate the service string
    service[i] = '\0';
}


// make a printable representation of types
const char *types_to_string(const char *types)
{
    if (types == NULL) return "any";
    if (types[0] == 0) return "none";
    return types;
}


// Associate t_o2rcv x with an addressnode so it can start receiving
//
// pre: x has a path and types, x->address may no longer be valid because
//      x->path and/or x->types has changed
// maintains: INV_1, INV_2, INV_4, INV_6, INV_7
// restores: INV_3, INV_5 (address has at least one receiver) if needed,
//           INV_8
void add_o2receive(t_o2rcv *x, bool service_exists)
{
    // check for conflict
    DBG2 printf("add_o2receive called, x->path %s x->address %p\n", x->path, x->address);
    addressnode *addr = NULL;
    if (x->path != NULL && check_for_conflict(x->path, x->types, &addr)) {
        pd_error(x, "o2receive address(types) %s(%s) conflicts with existing"
                 " address(types) %s(%s)", x->path, types_to_string(x->types),
                 addr ? addr->path : "?", addr ? types_to_string(addr->types) : "?");
        assert(x->address == NULL);
        assert(!service_exists);
        return;
    }
    DBG2 printf("add_o2receive see if we need address, addr %p\n", addr);
    if (addr == NULL) {  // we need an address
        DBG2 printf("add_o2receive needs an address for %s\n", x->path);
        // we made a new address, so we need a service and method
        if (!service_exists) {
            char service[64];
            extract_service(x->path, service, 64);
            service_exists = check_for_service(service);
            if (!service_exists) {  // no match, no conflict
                // there is no o2receiver receiving from service
                o2pd_post("o2receive creating new service %s\n", service + 1);
                if (o2_ensemble_name) {  // skip initial / or ! in service:
                    DBG2 printf("add_o2receive calling o2_service_new, o2_ensemble_name %s\n",
                                o2_ensemble_name);
                    if (o2pd_error_report((t_object *) x, "o2_service_new",
                                           o2_service_new(service + 1))) {
                        DBG2 printf("error from o2pd_error_report\n");
                        return; // error
                    }
                    DBG2 printf("add_o2receive after o2_service_new\n");
                } else {
                    pd_error((t_object *) x, "O2 cannot start receiving "
                             "until an ensemble is joined");
                    return;
                }
            }
        }
        DBG2 printf("now we have a service, we can add the method\n");
        // now we have a service, we can add the method
        addr = create_addressnode(x->path, x->types);
        o2pd_error_report((t_object *) x, "o2_method_new",
                           o2_method_new(x->path, x->types, o2rcv_handler, 
                                         (void *) addr, true, false));
        DBG2 printf("add the address %p to list %p\n", addr, addresses);
    }
    DBG2 printf("link x into receivers: x %p addr %p first rcvr %p\n",
                x, addr, addr->receivers);
    // link x into receivers
    add_o2rcv_to_address(x, addr);
    DBG2 printf("add_o2receive returns\n");
}


// return true iff typestr1 matches typestr2: both could be NULL or
// both could be matching strings
static bool types_match(const char *typestr1, const char *typestr2)
{
    return ((typestr1 == typestr2) ||  // both could be NULL or even the same
            (typestr1 && typestr2 &&   // or if strings, test string equality
             streql(typestr1, typestr2)));
}


// x has possibly new path and types, but may be previously set up
// to receive from x->address. Effectively delete the old address 
// (if any) and create a new one, but do it efficiently by ignoring
// the call if new address would have the same path and types, and
// avoid freeing and re-creating a service if the new address uses
// the same service as the old one.
// 
// Also, if we are about to delete x, call this with x->addr == NULL
// to remove x as a receiver (as if we are going to change the address,
// but x->addr == NULL means do not set up reception from a new address).
//
// The main function here is to detach x from its x->address. Setting up
// a new address is handled by add_o2receive().
//
// pre: INV_3 may not hold for this x
// maintains INV_1, INV_2, INV_4 - INV_8
// restores INV_3
void update_receive_address(t_o2rcv *x)
{
    char service[64];
    addressnode *addr = x->address;
    bool service_exists = false;

    DBG2 printf("update_receive_address: x %p x->address %p\n", x, x->address);
    if (addr == NULL) {  // no existing address to maybe remove, so
        // just try to create an address and start receiving:
        add_o2receive(x, false);
        return;
    }

    if (x->path != NULL && streql(addr->path, x->path) &&
        types_match(addr->types, x->types)) {
        return;  // no-op if new address & types matches old address & types
    }

    // remove x from x->address's receivers list
    DBG2 printf("update_receive_address: remove %p from %p list\n",
                x, x->address);
    remove_o2rcv_from_address(x);

    // did we eliminate the last receiver for an address?
    if (addr->receivers == NULL) {  // yes, remove the method
        // get the service before we free the path:
        extract_service(addr->path, service, 64);
        o2pd_error_report((t_object *) x, "o2_method_free",
                          o2_method_free(addr->path));
        remove_addressnode(addr);
        addr = NULL;

        // are we eliminating the last address for a service?  If so,
        // there will be no address now with service:
        service_exists = check_for_service(service);
        if (!service_exists) {  // no match, no conflict
            // so there is no other o2receiver receiving from service,
            // but maybe we need to retain service for x->path:
            if (x->path == NULL || !path_has_service(x->path, service)) {
                // we are going to use a different service or no service
                // at all, so no receiver exists for the old service:
                DBG printf("update_receive_address frees service %s\n",
                           service + 1);
                o2_service_free(service + 1);  // skip initial / or !
            } else {
                service_exists = true;  // we want to reuse service for x->path
            }
        }
    }
    if (x->path != NULL) {
        // so we install the address and x. If service_exists is true, it
        // indicates that the service for x->path exists even though no
        // address is using it (yet).
        add_o2receive(x, service_exists);
    }
    DBG2 printf("update_receive_address returns: x %p x->address %p\n", x, x->address);
}


// remove a t_o2rcv from its address, this will disable reception and could
//    remove the address entirely calling o2_method_free() and possibly
//    o2_service_free. x will retain it's path and types so that a future
//    bang can restart reception.
// maintains INV_1 - INV_8, 
void remove_o2receive(t_o2rcv *x)
{
    addressnode *addr = x->address;
    if (addr == NULL) {
        return;
    }
    // tell update_receive_address to just remove address:
    const char *temp = x->path;
    x->path = NULL;
    update_receive_address(x);  // remove x from list and free from address
    x->path = temp;
}


void show_receivers(const char *info)
{
    DBG {
        printf("RECEIVERS %s\n", info);
        for (addressnode *a = addresses; a; a = a->next) {
            printf("    Address %p %s types %s\n", a, a->path, a->types);
            for (t_o2rcv *r = a->receivers; r; r = r->next) {
                printf("      Receiver %p", r);
                if (r->path < (const char *) 0x1000000) {
                    printf(" path %p types %s\n", r->path, r->types);
                } else {
                    printf(" path %s types %s\n", r->path, r->types);
                }
            }
        }
        printf("END OF RECEIVERS\n");
    } // END DBG
}


//---------------- end address data structure ----------------


// helper function for searching for conflicts.
// returns 0 if conflict (one string is an "O2 prefix" of the other)
//      or 1 if no match
//      or 2 if exact match
int check_address_conflict(const char *str1, const char *str2)
{
    while (*str1 && *str2) {
        if (*str1 != *str2) {
            // Found a mismatch, so neither is a prefix of the other
            return 1;  // mismatch found
        }
        str1++;
        str2++;
    }
    
    // At this point, at least one string has reached its end
    
    // If both strings ended together, they are identical
    if (*str1 == '\0' && *str2 == '\0') {
        return 2; // identical found
    }
    
    // If only one string ended, it is a prefix of the other,
    // but we are only concerned about prefixes in terms of nodes,
    // e.g. /foo/bar is a prefix of /foo/bar/baz, but /foo/bar is
    // NOT a prefix of /foo/barbaz. To be a "true" path prefix, the
    // longer string must have a slash after the prefix.
    //
    if ((*str1 == '\0' && *str2 == '/') ||
        (*str2 == '\0' && *str1 == '/')) {
        return 0;  // prefix found
    }
    return 1;  // mismatch found at *str1 vs. *str2, so no conflict
}


// returns true if path and types would be incompatible with an existing
// address which already has an O2 message handler set up and sets *addr
// to the conflicting address, or
// returns false if path and types are compatible with existing addresses,
// setting *addr to NULL if there is no matching path and types, or
// setting *addr if there is already an O2 message handler set up for
// addr and types.
//
bool check_for_conflict(const char *path, const char *types,
                        addressnode **addr)
{
    *addr = NULL;
    for (addressnode *a = addresses; a; a = a->next) {
        int rslt = check_address_conflict(path, a->path);
        if (rslt == 0) {
            *addr = a;
            DBG2 printf("check_for_conflict returns true (0)\n");
            return true;  // conflict found
        } else if (rslt == 2) {  // identical paths
            *addr = a;
            if (types_match(types, a->types)) {
                DBG2 printf("check_for_conflict returns false (2)\n");
                return false;
            } else {
                DBG2 printf("check_for_conflict returns true (2) %s %s\n",
                            types, a->types);
                return true;
            }
        }  // else no match found, continue search
    }
    DBG2 printf("check_for_conflict returns false (1's)\n");
    return false;  // no conflict, no address match
} 


// same as post, but also writes to stdout so we can see it after a crash
void o2pd_post(const char *fmt, ...)
{
    char buf[MAXPDSTRING];
    va_list ap;
    t_int arg[8];
    int i;
    va_start(ap, fmt);
    vsnprintf(buf, MAXPDSTRING-1, fmt, ap);
    va_end(ap);
    post("%s", buf);
    fputs("[post] ", stdout);
    puts(buf);
}

O2err o2pd_error_report(t_object *x, const char *context, O2err err)
{
    if (err < 0) {
        pd_error(x, "O2 %s error: %s", context, o2_error_to_string(err));
    }
    return err;
}


// unpacks O2 message and forms Pd message according to types string
// pdmsg is the Pd message, preallocated with n slots of t_atom.
// If types is NULL, message is converted to default types.
// msgtypes is actual types provided by O2.
// n is also the length of msgtypes.
// returns actual size of pdmsg on completion, or -1 on error.
int unpack_message(addressnode *a, O2msg_data_ptr msg, 
                   const char *msgtypes, const char *types,
                   t_atom *pdmsg, int n)
{
    DBG printf("unpack msgtypes %s types %s n %d\n", msgtypes, types, n);
    DBG fflush(stdout);

    const char *DROPMSG = "dropping O2 message with types %s, expected %s";
    t_o2rcv *x = a->receivers;
    DBG printf("unpack x %p msg %p msg->address %s\n", x, msg, msg->address);
    DBG fflush(stdout);

    // if types == NULL, error will be "... expected Pd compatible types":
    const char *expected = types ? types : "Pd compatible types";
    // if types is "", error will be "... expected no parameters"
    if (expected[0] == 0) {
        expected = "no parameters";
    }

    if (types && (n != (int)strlen(types))) {
        pd_error(x, DROPMSG, msgtypes, expected);
        return -1; // error
    }
    o2_extract_start(msg);
    for (int i = 0; i < n; i++) {
        char c = msgtypes[i];
        DBG printf("unpack c=%c\n", c); DBG fflush(stdout);
        O2arg_ptr arg;
        switch (c) {
          case 'i': case 'h': case 'f': case 'd': 
          case 't': case 'T': case 'F': case 'B': 
            if (types && types[i] != 'f') {
                pd_error(x, DROPMSG, msgtypes, expected);
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
                pd_error(x, DROPMSG, msgtypes, expected);
                return -1;
            }
            arg = o2_get_next(O2_SYMBOL);
            SETSYMBOL(pdmsg + i, gensym((const char *) (arg->S)));
            break;
          case 'I':
            if (types && types[n] != 'f') {
                pd_error(x, DROPMSG, msgtypes, expected);
                return -1;
            }
            arg = o2_get_next(O2_INFINITUM);
            SETFLOAT(pdmsg + i, FLT_MAX);
            break;
          case 'c':
            if (types && types[n] != 'f') {
                pd_error(x, DROPMSG, msgtypes, expected);
                return -1;
            }
            arg = o2_get_next(O2_CHAR);
            SETFLOAT(pdmsg + i, arg->c);
            break;
          default:
            pd_error(x, DROPMSG, msgtypes, expected);
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

    addressnode *a = (addressnode *) user_data;
    int msglen = (int) strlen(types);
    t_atom *pdmsg;
    ATOMS_ALLOCA(pdmsg, msglen);
    if (!pdmsg) {
        return;  // error - O2 message has too many parameters or alloca failed
    }
    DBG printf("o2rcv_handler called %s msglen %d\n", a->path, msglen);
    DBG fflush(stdout);

    DBG printf("calling unpack_message\n"); DBG fflush(stdout);
    if (unpack_message(a, msg, types, a->types, pdmsg, msglen) < 0) {
        return;  // error - could not unpack for Pd or type error
    }
    DBG printf("unpack_message returned %d receivers %p\n",
               msglen, a->receivers); DBG fflush(stdout);
    for (t_o2rcv *r = a->receivers; r; r = r->next) {
        DBG printf("outlet_list for r=%p\n", r); DBG fflush(stdout);
        DBG printf("    outlet %p\n", r->x_obj.ob_outlet);
        DBG fflush(stdout);
        outlet_list(r->x_obj.ob_outlet, &s_list, msglen, pdmsg);
    }                
}
