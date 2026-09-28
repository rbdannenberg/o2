#include "ext.h"
#include "ext_obex.h"
#include "o2ensemble.h"
#include "o2max.h"

#define MAX_ARGS 100

//copy a string into heap
char *make_str(const char* s){
	char *ret=GETBYTES(strlen(s)+1);
	strcpy(ret,s);
	return ret;
}

static addressnode *addresses=NULL;

//create an addressnode
static addressnode *create_addressnode(const char *path,const char *types){
	CRITICAL_ENT;
	addressnode *addr=NEW_OBJ(addressnode);
	addr->path=make_str(path);
	addr->receivers=NULL;
	addr->next=addresses;
	addresses=addr;
	CRITICAL_RETA(addr);
}

//add an o2receive to its addressnode
static void add_o2rcv_address(t_o2receive *x,addressnode *y){
	CRITICAL_ENT;
	x->address=y;
	x->next=y->receivers;
	y->receivers=x;
	CRITICAL_EXT;
}

static t_o2receive **find_o2rcv(const addressnode *x,const t_o2receive *y){
	t_o2receive **tmp=&(x->receivers);
	while((*tmp)&&(*tmp)!=y)tmp=&((*tmp)->next);
	if(!(*tmp))error("o2receive not found in the list");
	return tmp;
}
//remove an o2receive from its addressnode, the object is not freed
static void remove_o2rcv_address(t_o2receive *x){
	CRITICAL_ENT;
	addressnode *addr=x->address;
	t_o2receive **next=find_o2rcv(addr,x);
	if(*next){
		*next=x->next;
		x->next=NULL;
		x->address=NULL;
	}
	CRITICAL_EXT;
}

static addressnode **find_adressnode(const addressnode *x){
	addressnode **tmp=&addresses;
	while((*tmp)&&(*tmp)!=x)tmp=&((*tmp)->next);
	if(!(*tmp))error("addressnode not found in the list");
	return tmp;
}
//remove an addressnode and free
static void remove_addressnode(addressnode *x){
	CRITICAL_ENT;
	//remove
	addressnode **next=find_adressnode(x);
	if(*next){
		*next=x->next;
		x->next=NULL;
	}
	//free
	while(x->receivers)remove_o2rcv_address(x->receivers);
	FREEBYTES(x->path);
	FREE_OBJ(x);
	CRITICAL_EXT;
}

//remove all addresses
void remove_all_addressnodes(){
	while(addresses)remove_addressnode(addresses);
}

//check path's service
static bool path_has_service(const char *path,const char *service){
	while((*path)&&(*service)){
		if((*path)!=(*service))return false;
		++path,++service;
	}
	return !(*service)&&((*path)=='/'||!(*path));
}

//check if any address has a service
static bool check_service(const char *service){
	CRITICAL_ENT;
	bool flag=false;
	for(addressnode* i=addresses;i;i=i->next){
		if(path_has_service(i->path,service)){
			flag=true;
			break;
		}
	}
	CRITICAL_RETA(flag);
}

//get the service of a path
static void get_service(const char *path,char **service){
	int len=1;
	while(path[len]&&path[len]!='/')++len;
	*service=GETBYTES(len+1);
	(*service)[len]=0;
	memcpy(*service,path,len);
}

//compare two type strings
static bool check_types(const char *t1,const char *t2){
	if(t1&&t2)return streql(t1,t2);
	return !t1&&!t2;
}

//check if an addressnode(path and types) conflict with the o2receive
//return 0 for conflicts, 1 for identical path found, 2 for others
static int check_conflict_address(const char *path,const char *types,const addressnode *x){
	const char *s1=path,*s2=x->path;
	while((*s1)&&(*s2)&&(*s1)==(*s2))++s1,++s2;
	//identical path, check types
	// if(!(*s1)&&!(*s2))return check_types(types,x->types);
	//multiple type string is allowed, just return 1
	if(!(*s1)&&!(*s2))return 1;
	//if one path includes another, there must be an conflict
	if((!(*s1)&&(*s2)=='/')||(!(*s2)&&(*s1)=='/'))return 0;
	return 2;
}

//check if a o2receive conflicts
bool check_for_conflict(const char *path,const char *types,addressnode **addr,bool *service_found){
	CRITICAL_ENT;
	*addr=NULL;
	const char *service;
	get_service(path,&service);
	DEBUG("service=%s\n",service);
	for(addressnode *i=addresses;i;i=i->next){
		DEBUG("checkpath %s\n",i->path);
		if(path_has_service(i->path,service)){
			(*service_found)=true;
			int res=check_conflict_address(path,types,i);
			if(!res){
				*addr=i;
				FREEBYTES(service);
				CRITICAL_RETA(true);
			}
			if(res==1){
				*addr=i;
				FREEBYTES(service);
				CRITICAL_RETA(false);
			}
		}
	}
	FREEBYTES(service);
	CRITICAL_RETA(false);
}

//add an o2receive object
void add_o2receive(t_o2receive *x,bool service_found){
	DEBUG("ADD o2service %d\n",(int)service_found);
	addressnode *addr=NULL;
	CRITICAL_ENT;
	//check if there's any conflict, and find addressnode if it exists
	if(check_for_conflict(x->path,x->types,&addr,&service_found)){
		object_error((t_object*)x,"o2receive address \"%s\" conflicts with"
					 " existing address and types \"%s\"",x->path,addr->path);
		CRITICAL_RET;
	}
	//if the service is not exist, create it
	if(!service_found){
		const char *service=NULL;
		get_service(x->path,&service);
		post("creating new service %s...",service);
		if(o2_ensemble_name){
			O2err ret;
			O2CALLRET(x,"o2_service_new",o2_service_new(service+1),ret);
			if(ret){
				FREEBYTES(service);
				CRITICAL_RET;
			}
		}else{
			object_error((t_object*)x,"O2 cannot start receiving until an ensemble is joined");
			FREEBYTES(service);
			CRITICAL_RET;
		}
		FREEBYTES(service);
	}
	//if no addressnode, create it
	if(!addr){
		post("creating new method %s...",x->path);
		addr=create_addressnode(x->path,x->types);
		DEBUG("%s address=%p\n",addr->path,addr);
		O2err ret;
		O2CALLRET(x,"o2_method_new",
			   o2_method_new(addr->path,NULL,o2rcv_handler,(void*)addr,true,false),ret);
		if(ret){
			CRITICAL_RET;
		}
	}
	add_o2rcv_address(x,addr);
	CRITICAL_RET;
}

//update the status of a o2receive object
void update_o2receive(t_o2receive *x){
	//if not active, activate it
	addressnode *addr=x->address;
	if(addr==NULL){
		add_o2receive(x,false);
		return;
	}
	//no updates
	CRITICAL_ENT;
	if(x->path!=NULL&&streql(x->path,addr->path)/*&&check_types(x->types,addr->types)*/){
		CRITICAL_RET;
	}
	//remove o2receive from previous address
	remove_o2rcv_address(x);
	//no receivers, remove the addressnode
	bool service_exists=true;
	if(addr->receivers==NULL){
		const char *service;
		get_service(addr->path,&service);
		O2CALL(x,"o2_method_free",o2_method_free(addr->path));
		remove_addressnode(addr);
		addr=NULL;
		if(!(service_exists=check_service(service))){
			if(x->path==NULL||!path_has_service(x->path,service)){
				//no addressnode under the service, remove it
				O2CALL(x,"o2_service_free",o2_service_free(service+1));
			}else service_exists=true;
		}
		FREEBYTES(service);
	}
	if(x->path!=NULL)add_o2receive(x,service_exists);
	CRITICAL_RET;
}

//remove o2receive object, which will disable it
void remove_o2receive(t_o2receive *x){
	addressnode *addr=x->address;
	if(addr){
		CRITICAL_ENT;
		const char *tmp=x->path;
		x->path=NULL;
		update_o2receive(x);
		x->path=tmp;
		CRITICAL_EXT;
	}
}

/*---functions about O2---*/
//call an O2 function and process error
O2err o2_call(t_object *x,const char *text,O2err err){
	if(err<0){
		object_error(x,"O2 %s error: %s",text,o2_error_to_string(err));
	}
	return err;
}

bool types_compatible(char t1,char t2){
	switch(t1){
		case 'i': case 'h': case 'f': case 'd':
		case 't': case 'T': case 'F': case 'B':
		case 'c':
		return strchr("ifhdtc",t2)!=NULL;
		case 'I':
		return strchr("fd",t2)!=NULL;
		case 's': case 'S':
		return strchr("sS",t2)!=NULL;
		default:
		return false;
	}
}

//unpack message from o2
int unpack_message(t_o2receive *x,O2msg_data_ptr msg,
					const char *msgtypes,const char *rcvtypes,
					t_atom *maxmsg,int n){
	const char *expected=(rcvtypes?(rcvtypes[0]?rcvtypes:"none"):"any");
	#define errpost object_error((t_object*)x,"address %s dropping O2 message with type %s, expected %s",x->path,msgtypes,expected)
	if(rcvtypes&&n!=strlen(rcvtypes)){
		errpost;
		return -1;
	}
	CRITICAL_ENT;
	o2_extract_start(msg);
	for(int i=0;i<n;++i){
		char c=msgtypes[i];
		O2arg_ptr arg;
		if(rcvtypes!=NULL){
			if(!types_compatible(c,rcvtypes[i])){
				errpost;
				CRITICAL_RETA(-1);
			}
			switch(rcvtypes[i]){
				case 'i':
				arg=o2_get_next(O2_INT32);
				atom_setlong(maxmsg+i,arg->i32);
				DEBUG("val=%d\n",atom_getlong(maxmsg+i));
				break;

				case 'h':
				arg=o2_get_next(O2_INT64);
				atom_setlong(maxmsg+i,arg->i64);
				DEBUG("val=%lld\n",atom_getlong(maxmsg+i));
				break;

				case 'f': case 't':
				if(c!='I'){
					arg=o2_get_next(O2_FLOAT);
					atom_setfloat(maxmsg+i,arg->f32);
				}else{
					arg=o2_get_next(O2_INFINITUM);
					atom_setfloat(maxmsg+i,FLT_MAX);
				}
				DEBUG("val=%f\n",atom_getfloat(maxmsg+i));
				break;

				case 'd':
				if(c!='I'){
					arg=o2_get_next(O2_DOUBLE);
					atom_setfloat(maxmsg+i,arg->f64);
				}else{
					arg=o2_get_next(O2_INFINITUM);
					atom_setfloat(maxmsg+i,DBL_MAX);
				}
				DEBUG("val=%lf\n",atom_getfloat(maxmsg+i));
				break;

				case 's': case 'S':
				arg=o2_get_next(O2_SYMBOL);
				atom_setsym(maxmsg+i,gensym((const char*)arg->S));
				DEBUG("val=%s\n",atom_getsym(maxmsg+i)->s_name);
				break;

				case 'c':
				arg=o2_get_next(O2_CHAR);
				atom_setlong(maxmsg+i,arg->c);
				DEBUG("val=%c\n",atom_getlong(maxmsg+i));
				break;
			}
		}else{
			switch(c){
				case 'i':
				arg=o2_get_next(O2_INT32);
				atom_setlong(maxmsg+i,arg->i32);
				DEBUG("val=%d\n",atom_getlong(maxmsg+i));
				break;

				case 'h':
				arg=o2_get_next(O2_INT64);
				atom_setlong(maxmsg+i,arg->i64);
				DEBUG("val=%lld\n",atom_getlong(maxmsg+i));
				break;

				case 'f': case 't': case 'T':
				case 'F': case 'B':
				arg=o2_get_next(O2_FLOAT);
				atom_setfloat(maxmsg+i,arg->f32);
				DEBUG("val=%f\n",atom_getfloat(maxmsg+i));
				break;
				
				case 'd':
				arg=o2_get_next(O2_DOUBLE);
				atom_setfloat(maxmsg+i,arg->f64);
				DEBUG("val=%lf\n",atom_getfloat(maxmsg+i));
				break;

				case 's': case 'S':
				arg=o2_get_next(O2_SYMBOL);
				atom_setsym(maxmsg+i,gensym((const char*)arg->S));
				DEBUG("val=%s\n",atom_getsym(maxmsg+i)->s_name);
				break;

				case 'I':
				arg=o2_get_next(O2_INFINITUM);
				if(sizeof(t_atom_float)==8){
					atom_setfloat(maxmsg+i,DBL_MAX);
				}else{
					atom_setfloat(maxmsg+i,FLT_MAX);
				}
				DEBUG("val=%lf\n",atom_getfloat(maxmsg+i));
				break;

				case 'c':
				arg=o2_get_next(O2_CHAR);
				atom_setlong(maxmsg+i,arg->c);
				DEBUG("val=%c\n",atom_getlong(maxmsg+i));
				break;

				default:
    	        errpost;
				CRITICAL_RETA(-1);
			}
		}
	}
	CRITICAL_RETA(n);
}

//handler for o2ensemble and o2receive
void o2rcv_handler(O2_HANDLER_ARGS){
	DEBUG("message received from O2\n");
	addressnode *addr=(addressnode*)user_data;
	int len=strlen(types);
	DEBUG("msg_len=%d, types=%s\n",len,types);
	if(len>MAX_ARGS)return;//too many arguments
	
	t_atom *maxmsg=(t_atom*)GETBYTES(len*sizeof(t_atom));
	// if(unpack_message(addr,msg,types,addr->types,maxmsg,len)<0){//unpack failed
	// 	return;
	// }
	for(t_o2receive *i=addr->receivers;i;i=i->next){
		DEBUG("send to %p\n",i);
		if(unpack_message(i,msg,types,i->types,maxmsg,len)<0){//unpack failed
			continue;
		}
		outlet_list(i->x_obj.o_outlet,NULL,len,maxmsg);
	}
}

void show_receivers(const char* info){
	if(!DBG)return;
	DEBUG("receivers (%s):\n",info);
	for(addressnode *i=addresses;i;i=i->next){
		DEBUG("    address %s:\n",i->path);
		for(t_o2receive *j=i->receivers;j;j=j->next){
			DEBUG("    %p (types %s)\n",j,j->types);
		}
	}
	DEBUG("\n");
}