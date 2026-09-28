#include "ext.h"
#include "ext_obex.h"
#include "o2ensemble.h"
#include "o2max.h"
#ifdef WIN32
#include<malloc.h>
#else
#include<alloca.h>
#endif

typedef struct _o2property{
	t_object x_obj;
	const char *service;
	const char *attribute;
	t_outlet *x_outlet1; //value
	t_outlet *x_outlet2; //bang
}t_o2property;

static t_class *s_o2property_class;

void *o2property_new(t_symbol *s,int argc,t_atom *argv){
	t_o2property *x=(t_o2property*)object_alloc(s_o2property_class);
	x->service=NULL;
	x->attribute=NULL;
	if(argc==2){
		if(argv[0].a_type==A_SYM){
			x->service=atom_getsym(argv)->s_name;
		}else{
			object_error((t_object*)x,"o2property expected symbol for service name");
		}
		if(argv[1].a_type==A_SYM){
			x->attribute=atom_getsym(argv+1)->s_name;
		}else{
			object_error((t_object*)x,"o2property expected symbol for property name");
		}
	}else if(argc){
		object_error((t_object*)x,"o2property expected 0 or 2 symbols");
	}
	x->x_outlet2=outlet_new((t_object*)x,"bang");
	x->x_outlet1=outlet_new((t_object*)x,NULL);
	post("p2prop: new");
	return x;
}

//find an property
void o2property_bang(t_o2property *x){
	const char *sname=NULL;
	int i;
	const char *value;
	CRITICAL_ENT;
	if(x->service&&x->attribute){
		O2CALL(x,"services_call",o2_services_list());
		for(i=0;(sname=o2_service_name(i))!=NULL;++i){
			if(streql(sname,x->service))break;
		}
	}
	if(sname&&(value=o2_service_getprop(i,x->attribute))){
		t_atom *t=NEW_OBJ(t_atom);
		atom_setsym(t,gensym(value));
		outlet_list(x->x_outlet1,NULL,1,t);
		FREE_OBJ(t);
	}else{
		outlet_bang(x->x_outlet2);
	}
	o2_services_list_free();
	CRITICAL_RET;
}

void o2property_get(t_o2property *x,t_symbol *service,t_symbol *attribute){
	post("o2prop: get");
	CRITICAL_ENT;
	if(o2_ensemble_name==NULL){
		object_error((t_object*)x,"o2property: O2 is not initialized");
	}else{
		x->service=service->s_name;
		x->attribute=attribute->s_name;
		o2property_bang(x);
	}
	CRITICAL_RET;
}

//set a property, or unset it when value is not given
void o2property_put(t_o2property *x,t_symbol *s,int argc,t_atom *argv){
	post("o2prop: put");
	if(o2_ensemble_name==NULL){
		object_error((t_object*)x,"o2property: O2 is not initialized");
		return;
	}else if(argc<2||argv[0].a_type!=A_SYM||argv[1].a_type!=A_SYM){
		object_error((t_object*)x,"o2property put requires at least service and attribute");
	}
	x->service=atom_getsym(argv)->s_name;
	x->attribute=atom_getsym(argv+1)->s_name;
	if(argc==2){
		O2CALL(x,"o2_service_property_free",
		       o2_service_property_free(x->service,x->attribute));
	}else if(argc==3){
		const char *value=atom_getsym(argv+2)->s_name;
		O2CALL(x,"o2_service_set_property",
		       o2_service_set_property(x->service,x->attribute,value));
	}else{
		object_error((t_object*)x,"o2property got >3 arguments, list ignored");
	}
}

void o2property_search(t_o2property *x,t_symbol *s,t_symbol *attr,t_symbol *val){
	post("o2prop: search");
	if(o2_ensemble_name==NULL){
		object_error((t_object*)x,"o2property: O2 is not initialized");
		return;
	}
	const char *attribute=attr->s_name;
	const char *value=val->s_name;
	int resultmax=8;
	int argc=0;
	t_atom *res=(t_atom*)GETBYTES(resultmax*sizeof(t_atom));
	int si=0;
	CRITICAL_ENT;
	while((si=o2_service_search(si,attribute,value))>=0){
		if(argc>=resultmax){
			resultmax*=2;
			res=(t_atom*)sysmem_resizeptr(res,resultmax*sizeof(t_atom));
		}
		atom_setsym(res+argc,gensym(o2_service_name(si)));
		argc++;
		si++;
	}
	outlet_list(x->x_outlet1,NULL,argc,res);
	FREEBYTES(res);
	CRITICAL_RET;
}

void ext_main(void *r){
	t_class *c;
	c=class_new("o2property",(method)o2property_new,(method)NULL,
	            sizeof(t_o2property),0L,A_GIMME,0);
	class_addmethod(c,(method)o2property_get,
	                "get",A_SYM,A_SYM,0);
	class_addmethod(c,(method)o2property_put,
	                "put",A_GIMME,0);
	class_addmethod(c,(method)o2property_search,
	                "search",A_SYM,A_SYM,0);
	class_addmethod(c,(method)o2property_bang,
	                "bang",0);
	class_register(CLASS_BOX,c);
	s_o2property_class=c;
}