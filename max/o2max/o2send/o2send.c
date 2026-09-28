#include "ext.h"
#include "ext_obex.h"
#include "o2ensemble.h"
#include "o2max.h"
#ifdef WIN32
#include<malloc.h>
#else
#include<alloca.h>
#endif

typedef struct _o2send{
	t_object x_obj;
	const char *servicename; //max holds this as a symbol
	const char *address;
	const char *types; //max holds this as a symbol
	double timestamp; //time to send next message
	int tcp_flag;
}t_o2send;

void o2send_check_flags(t_o2send *x,int *argc,t_atom **argv){
	const char *opt;
	while(*argc>0&&(*argv)->a_type==A_SYM&&(opt=atom_getsym(*argv)->s_name)[0]=='-'){
		if(streql(opt,"-t")&&(*argc)>1&&(*argv)[1].a_type==A_SYM){
			x->types=atom_getsym(*argv+1)->s_name;
			(*argc)--,(*argv)++;
		}else if(streql(opt,"-r")){
			x->tcp_flag=true;
		}else if(streql(opt,"-b")){
			x->tcp_flag=false;
		}else{
			object_error((t_object*)x,"o2send expected option %s",opt);
		}
		(*argc)--,(*argv)++;
	}
}

static void o2send_get_address(t_o2send *x,t_symbol *s,int argc,t_atom *argv){
	CRITICAL_ENT;
	const char *types=NULL;
	x->tcp_flag=false; //default sending with udp
	char path[128];
	int last=0;
	path[0]=0;
	o2send_check_flags(x,&argc,&argv);
	while(argc>0&&argv->a_type==A_SYM){
		const char *name=atom_getsym(argv)->s_name;
		int len=strlen(name);
		if(!x->servicename){
			x->servicename=name;
		}
		if(last+len+2>=128){
			object_error((t_object*)x,"O2 address is too long");
			CRITICAL_RET;
		}
		path[last++]='/';
		strcpy(path+last,name);
		last+=len;
		argc--,argv++;
		o2send_check_flags(x,&argc,&argv);
	}
	if(path[0]){
		int len=strlen(path);
		if(x->address){
			FREEBYTES(x->address);
		}
		x->address=GETBYTES(len+1);
		strncpy((char*)(x->address),path,len+1);
	}
	if(argc){
		object_error((t_object*)x,"O2 address: extra parameters ignored");
	}
	CRITICAL_RET;
}

static t_class *s_o2send_class;

void *o2send_new(t_symbol *s,int argc,t_atom *argv){
	t_o2send *x=(t_o2send*)object_alloc(s_o2send_class);
	x->servicename=NULL;
	x->address=NULL;
	x->types=NULL;
	o2send_get_address(x,s,argc,argv);
	outlet_new((t_object*)x,NULL);
	post("o2snd: new");
	return x;
}

//set address
void *o2send_address(t_o2send *x,t_symbol *s,int argc,t_atom *argv){
	post("o2snd: address");
	o2send_get_address(x,s,argc,argv);
}

//set next message time
void *o2send_time(t_o2send *x,t_atom_float time){
	post("o2snd: time %g",time);
	x->timestamp=time;
}

//set next message delay
void *o2send_delay(t_o2send *x,t_atom_float delay){
	post("o2snd: delay %g",delay);
	CRITICAL_ENT;
	O2time now=o2_time_get();
	CRITICAL_EXT;
	if(now>=0){
		x->timestamp=now*1000+delay;
	}else{
		object_error((t_object*)x,"o2send delay: O2 is not initialized");
	}
}

//set send message types
void o2send_types(t_o2send *x,t_symbol *types){
	post("o2snd: types %s",types->s_name);
	x->types=types->s_name;
	if(x->types[0]==0)x->types=NULL;
	CRITICAL_ENT;
	for(int i=0;x->types[i];++i){
		if(strchr("ifhdtsSc",x->types[i])==NULL){
			object_error((t_object*)x,"o2send: types string %s has invalid character %c",
			             x->types,x->types[i]);
			x->types=NULL;
		}
	}
	CRITICAL_RET;
}

void o2send_status(t_o2send *x){
	post("o2snd: status (for %s)",x->servicename);
	CRITICAL_ENT;
	O2err status=o2_status(x->servicename);
	CRITICAL_EXT;
	if(o2_call((t_object*)x,"status",status)>=0){
		t_atom outv[2];
		atom_setsym(outv,gensym(x->servicename));
		atom_setlong(outv+1,status);
		outlet_anything(x->x_obj.o_outlet,gensym("status"),2,outv);
	}
}

void o2send_list(t_o2send *x,t_symbol *s,int argc,t_atom *argv){
	bool error_flag=false;
	post("o2snd: list");
	if(!o2_ensemble_name){
		object_error((t_object*)x,"o2send: o2 not initialized");
		return;
	}
	CRITICAL_ENT;
	o2_send_start();
	if(x->types){
		int len=strlen(x->types);
		if(len!=argc){
			object_error((t_object*)x,"o2send: arg count does not match types %s length %d",
			             x->types,len);
			CRITICAL_RET;
		}
	}
	for(int i=0;i<argc;++i){
		t_atom *arg=argv+i;
		if(x->types){
			char typ=x->types[i];
			switch(typ){
				case 'i':
				if(arg->a_type==A_LONG){
					o2_add_int32((int)atom_getlong(arg));
				}else{
					error_flag=true;
				}
				break;
				case 'f':
				if(arg->a_type==A_FLOAT){
					o2_add_float(atom_getfloat(arg));
				}else{
					error_flag=true;
				}
				break;
				case 'h':
				if(arg->a_type==A_LONG){
					o2_add_int64((int64_t)atom_getlong(arg));
				}else{
					error_flag=true;
				}
				break;
				case 'd':
				if(arg->a_type==A_FLOAT){
					o2_add_double(atom_getfloat(arg));
				}else{
					error_flag=true;
				}
				break;
				case 't':
				if(arg->a_type==A_FLOAT){
					o2_add_time(atom_getfloat(arg));
				}else{
					error_flag=true;
				}
				break;
				case 'c':
				if(arg->a_type==A_LONG){
					o2_add_char((int)atom_getfloat(arg));
				}else{
					error_flag=true;
				}
				break;
				case 's':
				if(arg->a_type==A_SYM){
					o2_add_string(atom_getsym(arg)->s_name);
				}else{
					error_flag=true;
				}
				break;
				case 'S':
				if(arg->a_type==A_SYM){
					o2_add_symbol(atom_getsym(arg)->s_name);
				}else{
					error_flag=true;
				}
				break;
				default:
				object_error((t_object*)x,"o2send: unexpected type character %c",typ);
				CRITICAL_RET;
			}
			if(error_flag){
				object_error((t_object*)x,"o2send: arg %d incompatible with type %c",
				             i,typ);
				CRITICAL_RET;
			}
		}else if(arg->a_type==A_FLOAT){//no type specification, according to max types
			o2_add_float(atom_getfloat(arg));
		}else if(arg->a_type==A_LONG){
			if(sizeof(t_atom_long)==8){
				o2_add_int64(atom_getlong(arg));
			}else{
				o2_add_int32(atom_getlong(arg));
			}
		}else if(arg->a_type==A_SYM){
			o2_add_symbol(atom_getsym(arg)->s_name);
		}else{
			object_error((t_object*)x,"o2send arg %d is not a float, long or symbol",i);
			CRITICAL_RET;
		}
	}
	O2CALL(x,"o2send",o2_send_finish(x->timestamp*0.001,x->address,x->tcp_flag));
	x->timestamp=0;
	CRITICAL_RET;
}

void o2send_free(t_o2send *x){
	if(x->address){
		FREEBYTES(x->address);
	}
}

void ext_main(void *r){
	t_class *c;
	c=class_new("o2send",(method)o2send_new,(method)o2send_free,
	            sizeof(t_o2send),0L,A_GIMME,0);
	class_addmethod(c,(method)o2send_address,
	                "address",A_GIMME,0);
	class_addmethod(c,(method)o2send_types,
	                "types",A_SYM,0);
	class_addmethod(c,(method)o2send_time,
	                "time",A_FLOAT,0);
	class_addmethod(c,(method)o2send_delay,
	                "delay",A_FLOAT,0);
	class_addmethod(c,(method)o2send_list,
	                "list",A_GIMME,0);
	class_addmethod(c,(method)o2send_status,
	                "status",0);
	class_register(CLASS_BOX,c);
	s_o2send_class=c;
}