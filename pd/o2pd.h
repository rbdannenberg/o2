/* o2pd.h -- header for building o2pd libraries */
 /* Roger B. Dannenberg & Chukwkuemeka Nkama
  * August 2024
 */

// Setup definition to allow o2pd, o2ensemble, o2receive libraries compile successfully on Windows
#if defined(WIN32)
// note: hidden feature of CMake is it defines {libraryname}_EXPORTS
// when building a shared library according to Prof. Roger Dannenberg..
	#if defined(libo2pd_EXPORTS)
		#define O2PD_EXPORT __declspec(dllexport) extern 
	#else
		#define O2PD_EXPORT __declspec(dllimport) extern // needed for libs linking to libo2pd or o2pd
	#endif

// I can't use O2PD_EXPORT for functions like o2ensemble_setup since o2ens_error_report can't be dllimport
// and dllexport at the same time.Hence I define another macro
	#define PDLIBS_EXPORT __declspec(dllexport) extern // needed for libs linking to libo2pd or o2pd
#else 
#define PDLIBS_EXPORT
#define O2PD_EXPORT
#endif

// The following definitions need to be exported to O2pd library and imported in o2receive, o2ensemble etc...
O2PD_EXPORT O2err o2ens_error_report(t_object* x, const char* context, O2err err);
servicenode* o2ens_services;
O2PD_EXPORT void o2rcv_handler(O2_HANDLER_ARGS);
O2PD_EXPORT void service_delete(t_object* x, servicenode** snode, int free_it, char* src);
O2PD_EXPORT void install_handlers(t_object* x, addressnode* anode);
O2PD_EXPORT void receiver_delete(servicenode* snode, addressnode* anode,
	t_o2rcv* receiver, const char* src);
O2PD_EXPORT void show_receivers(const char* info);
