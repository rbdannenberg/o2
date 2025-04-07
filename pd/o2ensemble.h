/* o2ensemble.h -- header for pd class o2ensemble */
/* Roger B. Dannenberg
 * Aug 2022
 */

#include "m_pd.h"
#include "s_stuff.h"
#include "o2.h"

// You can enable lots of printing here:
#define DBG if (1) 
#define DBG2 if (1) /* VERY TEMPORARY DEBUGGING - REMOVE THIS! */

typedef struct o2rcv
{
    t_object x_obj;
    const char *path;    // local owned copy of the string
    const char *types ;  // owned by pd (symbol name)
    struct _addressnode *address;  // owned by addressnode which may be shared
    struct o2rcv *next;  // list of o2rcv sharing same address
} t_o2rcv;               //  with other t_o2rcv objects


typedef struct _receivernode {
    t_o2rcv *receiver;  // owned by pd (when pd deletes object,
                        // we remove this node)
    struct _receivernode *next;
} receivernode;


typedef struct _addressnode {
    const char *path;  // we own this, but this node and this path can only
                       // be deleted when there are no more receivers
    const char *types; // owned by pd (symbol name)
    t_o2rcv *receivers;
    struct _addressnode *next;
} addressnode;



