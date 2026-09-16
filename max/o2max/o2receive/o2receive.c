#include "ext.h"
#include "ext_obex.h"
#include "o2ensemble.h"
#include "o2max.h"
#ifdef WIN32
#include<malloc.h>
#else
#include<alloca.h>
#endif

void o2receive_check_flags(t_o2receive *x,int *argc,t_atom **argv,const char **types,bool *wait){
	const char *opt;
	while((*argc)>0&&(*argv)[0].a_type==A_SYM&&(opt=atom_getsym(*argv)->s_name)[0]=='-'){
		if(streql(opt,"-t")&&(*argc)>1&&(*argv)[1].a_type==A_SYM){
			*types=atom_getsym(*argv+1)->s_name;
			(*argc)--,(*argv)++;
		}else if(streql(opt,"-w")){
			*wait=true;
		}else{
			object_error((t_object*)x,"o2receive expected option %s",opt);
		}
		(*argc)--,(*argv)++;
	}
}

static bool o2receive_get_address(t_o2receive *x,t_symbol *s,int argc,t_atom *argv){
	CRITICAL_ENT;
	const char *types=NULL;
	char path[128];
	int last=0;
	path[0]=0;
	bool wait=false;
	o2receive_check_flags(x,&argc,&argv,&types,&wait);
	while(argc>0&&argv->a_type==A_SYM){
		const char *name=atom_getsym(argv)->s_name;
		int len=strlen(name);
		if(last+len+2>=128){
			object_error((t_object*)x,"O2 address is too long");
			CRITICAL_RET(false);
		}
		path[last++]='/';
		strcpy(path+last,name);
		last+=len;
		argc--,argv++;
		o2receive_check_flags(x,&argc,&argv,&types,&wait);
	}
	if(path[0]){
		int len=strlen(path);
		if(x->path){
			FREEBYTES(x->path);
		}
		char *t=GETBYTES(len+1);
		t[len]=0;
		memcpy(t,path,len);
		x->path=t;
	}
	x->types=types;
	if(argc){
		object_error((t_object*)x,"O2 address: extra parameters ignored");
	}
	CRITICAL_RETA(wait);
}

static t_class *s_o2receive_class;

void *o2receive_new(t_symbol *s,int argc,t_atom *argv){
	t_o2receive *x=(t_o2receive*)object_alloc(s_o2receive_class);
	x->path=NULL;
	x->types=NULL;
	x->next=NULL;
	x->address=NULL;
	if(argc>0){
		bool wait=o2receive_get_address(x,s,argc,argv);
		if(x->path&&!wait){
			update_o2receive(x);
		}
	}
	outlet_new((t_object*)x,NULL);
	DEBUG("NEW o2rcv: %p\n",x);
	post("o2rcv: new");
	return x;
}

//set the address
void o2receive_address(t_o2receive *x,t_symbol *s,int argc,t_atom *argv){
	post("o2rcv: address");
	CRITICAL_ENT;
	show_receivers("before o2rcv_address");
	o2receive_get_address(x,s,argc,argv);
	DEBUG("NEW ADDR: %s\n",x->path);
	update_o2receive(x);
	show_receivers("after o2rcv_address");
	CRITICAL_RET;
}

//activate
void o2receive_bang(t_o2receive *x){
	post("o2rcv: bang");
	show_receivers("before o2rcv_bang");
    update_o2receive(x);
    show_receivers("after o2rcv_bang");
}

static void o2receive_check_special_types(const char **types){
	if(streql(*types,"none")){
		*types="";
	}else if(streql(*types,"any")){
		*types=NULL;
	}
}

//set types
void o2receive_types(t_o2receive *x,t_symbol *types){
	post("o2rcv: types %s",types->s_name);
	const char *typestr=types->s_name;
	o2receive_check_special_types(&typestr);
	CRITICAL_ENT;
	if(typestr!=NULL){
		for(int i=0;typestr[i];++i){
			if(strchr("ifhdtsSc",typestr[i])==NULL){
				object_error((t_object*)x,"o2erceive: types string %s has invalid character %c",
				             x->types,x->types[i]);
				typestr=NULL;
				break;
			}
		}
	}
	addressnode *a=x->address;
	if(!a){
		// object_error((t_object*)x,"o2receive: setting types, but there is no address yet");
		post("o2rcv: types change failed, because the object is disabled");
		CRITICAL_RET;
	}
	x->types=typestr;
	show_receivers("in types before update_receive_address");
    update_o2receive(x);
    show_receivers("in types after update_receive_address");
	CRITICAL_RET;
}

//disable the object
void o2receive_disable(t_o2receive *x,t_symbol *s,int argc,t_atom *argv){
	post("o2rcv: disable");
	if(o2_ensemble_name==NULL){
		object_error((t_object*)x,"O2 is not initialized");
	}else{
		remove_o2receive(x);
	}
	show_receivers("after disable");
}

void o2receive_free(t_o2receive *x){
	remove_o2receive(x);
}

void ext_main(void *r){
	t_class *c;
	c=class_new("o2receive",(method)o2receive_new,(method)o2receive_free,
	            sizeof(t_o2receive),0L,A_GIMME,0);
	class_addmethod(c,(method)o2receive_address,
	                "address",A_GIMME,0);
	class_addmethod(c,(method)o2receive_types,
	                "types",A_SYM,0);
	class_addmethod(c,(method)o2receive_disable,
	                "disable",0);
	class_addmethod(c,(method)o2receive_bang,
	                "bang",0);
	class_register(CLASS_BOX,c);
	s_o2receive_class=c;
}