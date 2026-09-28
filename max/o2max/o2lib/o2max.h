#ifndef O2MAX_H
#define O2MAX_H
#ifdef WIN32
	#ifdef BUILD_SHARED_O2MAXLIB
		#ifdef o2max_EXPORTS
			#define O2MAX_EXPORT __declspec(dllexport) extern
		#else
			#define O2MAX_EXPORT __declspec(dllimport) extern
		#endif
	#else
		#define O2MAX_EXPORT extern
	#endif
#else
	#define O2MAX_EXPORT
#endif

#ifdef _DEBUG
#define DEBUG(...) printf(__VA_ARGS__)
#define DBG 1
#else
#define DEBUG(...) 0
#define DBG 0
#endif

#define GETBYTES(n) sysmem_newptr(n)
#define NEW_OBJ(typ) ((typ*)GETBYTES(sizeof(typ)))
#define FREEBYTES(x) sysmem_freeptr(x)
#define FREE_OBJ(x) FREEBYTES(x)

#define CRITICAL_ENT critical_enter(0)
#define CRITICAL_EXT critical_exit(0)

// #define CRITICAL_ENT 0
// #define CRITICAL_EXT 0

#define CRITICAL_RET CRITICAL_EXT;return
#define CRITICAL_RETA(x) CRITICAL_EXT;return x



void remove_all_addressnodes();

O2MAX_EXPORT bool check_for_conflict(const char *path,const char *types,addressnode **addr,bool *service_found);

O2MAX_EXPORT void add_o2receive(t_o2receive *x);
O2MAX_EXPORT void update_o2receive(t_o2receive *x);
O2MAX_EXPORT void remove_o2receive(t_o2receive *x);
O2MAX_EXPORT void show_receivers(const char* info);

#define O2CALL(x,text,func)\
	CRITICAL_ENT;\
	o2_call((t_object*)x,text,func);\
	CRITICAL_EXT
#define O2CALLRET(x,text,func,ret)\
	CRITICAL_ENT;\
	ret=o2_call((t_object*)x,text,func);\
	CRITICAL_EXT
O2MAX_EXPORT O2err o2_call(t_object *x,const char *text,O2err err);
O2MAX_EXPORT void o2rcv_handler(O2_HANDLER_ARGS);


#endif