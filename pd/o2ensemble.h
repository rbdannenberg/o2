/* o2ensemble.h -- header for pd class o2ensemble */
/* Roger B. Dannenberg
 * Aug 2022
 */

#include "m_pd.h"
#include "s_stuff.h"
#include "o2.h"

// You can enable lots of printing here:
#define DBG if (0) 


O2err o2ens_error_report(t_object *x, const char *context, O2err err);

typedef struct o2rcv
{
    t_object x_obj;
    const char *servicename;  // owned by pd (symbol name)
    struct _addressnode *address;  // owned by addressnode and may be shared
} t_o2rcv;       //  with other t_o2rcv objects


typedef struct _receivernode {
    t_o2rcv *receiver;  // owned by pd (when pd deletes object,
                        // we remove this node)
    struct _receivernode *next;
} receivernode;


typedef struct _addressnode {
    const char *path;  // we own this, but this node and this path can only
                       // be deleted when there are no more receivers
    const char *types; // owned by pd (symbol name)
    struct _servicenode *service;  // NULL unless this path is just service name
                                   // owned by o2ens_services list
    receivernode *receivers;
    struct _addressnode *next;
} addressnode;


typedef struct _servicenode {
    const char *service;  // owned by pd (symbol name)
    // servicenode has a list of all addresses that begin with the
    // service (name). One of these addresses may be the service itself,
    // which means ALL messages for the service are delivered to handlers
    // for that address. This whole service address is also pointed to by
    // the wholeservice field, which is otherwise NULL.
    addressnode *addresses;
    addressnode *wholeservice;
    struct _servicenode *next;
} servicenode;

extern servicenode *o2ens_services;

void o2rcv_handler(O2_HANDLER_ARGS);
void service_delete(t_object *x, servicenode **snode, int free_it, char *src);
void install_handlers(t_object *x, addressnode *anode);
void receiver_delete(servicenode *snode, addressnode *anode,
                     t_o2rcv *receiver, const char *src);
void show_receivers(const char *info);
