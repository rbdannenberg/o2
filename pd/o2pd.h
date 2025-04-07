/* o2pd.h -- header for building o2pd libraries */
 /* Roger B. Dannenberg & Chukwkuemeka Nkama
  * August 2024
 */

// Setup definition to allow o2pd, o2ensemble, o2receive libraries 
// compile successfully on Windows
#if defined(WIN32)
// note: hidden feature of CMake is it defines {libraryname}_EXPORTS
// when building a shared library according to Prof. Roger Dannenberg..
    #if defined(libo2pd_EXPORTS)
        #define O2PD_EXPORT __declspec(dllexport) extern 
    #else
        // needed for libs linking to libo2pd or o2pd
        #define O2PD_EXPORT __declspec(dllimport) extern
    #endif

    // I can't use O2PD_EXPORT for functions like o2ensemble_setup since
    // o2pd_error_report can't be dllimport and dllexport at the same time.
    // Hence I define another macro needed for libs linking to libo2pd or o2pd
    #define PDLIBS_EXPORT __declspec(dllexport) extern
#else 
    #define PDLIBS_EXPORT
    #define O2PD_EXPORT
#endif

#define NEW_OBJ(objtype) ((objtype *) getbytes(sizeof(objtype)))
#define FREE_STRING(str) { freebytes((void *)(str), strlen(str)); (str) = NULL; }
#define FREE_OBJ(obj) { freebytes((obj), sizeof(*(obj))); (obj) = NULL; }

// The following definitions need to be exported to O2pd library and imported
// in o2receive, o2ensemble etc...

//---------------- address data structure ----------------

// free all of the data structure (before o2_finish is called) without
// calling o2_method_free() or o2_service_free()
O2PD_EXPORT void remove_all_addressnodes(void);

O2PD_EXPORT void update_receive_address(t_o2rcv *x);

O2PD_EXPORT void remove_o2receive(t_o2rcv *x);

O2PD_EXPORT void show_receivers(const char* info);

//---------------- end address data structure ----------------



O2PD_EXPORT O2err o2pd_error_report(t_object* x, const char* context,
                                     O2err err);
O2PD_EXPORT void o2rcv_handler(O2_HANDLER_ARGS);
O2PD_EXPORT void install_handlers(t_object* x, addressnode* anode);
O2PD_EXPORT void o2pd_post(const char *fmt, ...);
O2PD_EXPORT const char *o2pd_heapify(const char *str);

// see if address conflicts with an existing address. Return true if there
// is a conflict. Otherwise, if *addr is not NULL, it is a matching address.
//
O2PD_EXPORT bool check_for_conflict(const char *path, const char *types,
                                    addressnode **addr);

// remove t_o2rcv from its address, possibly calls o2_method_free() and 
// o2_service_free():
//
O2PD_EXPORT void remove_o2receive(t_o2rcv *x);


// add x as an active o2receiver if there is no O2 address conflict.
// May create or share an addressnode. If a new addressnode is created,
// o2_method_new() will be called. Before that, if there is no other
// address receiving for the same service, o2_service_new() may be called.
// A special case is where we are changing the address of x to a new address
// with the same service. In that case, we do not free the service and we
// indicate this case by setting service_exists = true.  This means we can
// call o2_method_new() without first creating a new service for it.
//
O2PD_EXPORT void add_o2receive(t_o2rcv *x, bool service_exists);

