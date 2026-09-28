#include "ext.h"
#include "ext_obex.h"
#include "o2ensemble.h"
#include "o2max.h"
typedef struct _o2ensemble{
	t_object x_obj;
	struct _o2ensemble *next;
}t_o2ensemble;

static t_o2ensemble *o2ens_list=NULL; //list of all o2ensemble
static t_o2ensemble *o2ens_active=NULL;

static int o2ens_count=0;
static t_clock *o2ens_timer=NULL;
static long o2ens_ticks=0;
static int o2ens_is_clock_ref=false;
static int o2ens_clockjump_called=false;

//called at each max tick to call o2_poll
void o2ensemble_clock_tick(void *nl){
	CRITICAL_ENT;
	o2_poll();
	CRITICAL_EXT;
	clock_delay(o2ens_timer,0);
}

//called by O2 when clock sync detects a big jump
o2_time_jump_callback o2ensemble_time_jump_callback
(double local_time,double old_global_time,double new_global_time){
	o2ens_clockjump_called=false;
	if(o2ens_active){
		t_atom outv[3];
		atom_setfloat(outv,local_time*1000.0);
		atom_setfloat(outv+1,old_global_time*1000.0);
		atom_setfloat(outv+2,new_global_time*1000.0);
		outlet_anything(o2ens_active->x_obj.o_outlet,gensym("timejump"),3,outv);
	}
	return o2ens_clockjump_called;
}


//find flags and remove them from argv
void o2ensemble_check_flags(t_o2ensemble *x,int *argc,t_atom **argv,char **options,int *clock){
	const char *opt;
	if(*argc>1&&(*argv)->a_type==A_SYM){
		post("argv=%s",atom_getsym(*argv)->s_name);
	}
	while(*argc>1&&(*argv)->a_type==A_SYM&&(opt=atom_getsym(*argv)->s_name)[0]=='-'){
		post("opt=%s",opt);
		int a_type=(*argv)[1].a_type;
		if(streql(opt,"-d")&&a_type==A_SYM){
			(*options)=atom_getsym(*argv+1)->s_name;
		}else if(streql(opt,"-c")&&a_type==A_FLOAT){
			*clock=(atom_getfloat(*argv+1)!=0);
		}else{
			object_error((t_object*)x,"o2ensemble unexpected option %s",opt);
		}
		(*argc)-=2,(*argv)+=2;
	}
}

//initialize the o2ensemble object
void o2ensemble_initialize(t_o2ensemble *x,int is_join,int argc,t_atom *argv){
	CRITICAL_ENT;
	if(o2ens_active&&o2ens_active!=x){
		object_error((t_object*)x,"object is passive because another o2ensemble is active");
		CRITICAL_RET;
	}
	int network_level=2;
	int o2lite=1;
	char mqtt_ip[32];
	int mqtt_port=0;
	int http=0;
	int http_port=8080;
	const char *http_root="web";
	char *opt=NULL;
	int clock=true;
	mqtt_ip[0]=0; //default

	o2_time_jump_callback_set(o2ensemble_time_jump_callback);

	o2ensemble_check_flags(x,&argc,&argv,&opt,&clock);

	//1st arg: ensemble name
	const char *ensemble_name=NULL;
	if(argc){
		if(argv->a_type==A_SYM){
			ensemble_name=atom_getsym(argv)->s_name;
		}else{
			object_error((t_object*)x,"O2: expected symbol for ensemble name");
			CRITICAL_RET;
		}
		argc--,argv++;
	}else if(is_join){
		object_error((t_object*)x,"cannot join: no ensemble name given; join ignored");
		CRITICAL_RET;
	}else{ //no arg, do not start 
		CRITICAL_RET;
	}

	//if we try to join twice, print a error
	if(o2_ensemble_name){
		object_error((t_object*)x,"o2ens: O2 is already initialized");
		if(!streql(o2_ensemble_name,ensemble_name)){
			object_error((t_object*)x,"o2ens: join is attempting to change ensemble "
			         "name from %s to %s; need to leave first",
			         o2_ensemble_name, ensemble_name);
		}
		CRITICAL_RET;
	}

	o2ensemble_check_flags(x,&argc,&argv,&opt,&clock);

	//2nd arg: network level
	if(argc){
		if(argv->a_type==A_LONG){
			network_level=atom_getlong(argv);
		}else if(argv->a_type==A_SYM){ //mqtt level
			network_level=3;
			const char *ip=atom_getsym(argv)->s_name;
			const char *colon=strchr(ip,':');
			strncpy(mqtt_ip,ip,32);
			mqtt_ip[31]=0; //truncate the IP string if too long
			if(colon&&(colon-ip)<32){
				mqtt_ip[colon-ip]=0; //turncate port
				mqtt_port=atoi(colon+1);
			}
		}else{
			object_error((t_object*)x,"O2 ensemble expected number for network-level");
			CRITICAL_RET;
		}
		argc--,argv++;
	}

	o2ensemble_check_flags(x,&argc,&argv,&opt,&clock);

	//3rd arg: o2lite_enable
	if(argc){
		if(argv->a_type==A_LONG){
			o2lite=atom_getlong(argv);
		}else{
			object_error((t_object*)x,"O2 ensemble expected number for o2lite-enable");
			CRITICAL_RET;
		}
		argc--,argv++;
	}

	o2ensemble_check_flags(x,&argc,&argv,&opt,&clock);

	//4th arg: http_enable
	if(argc){
		if(argv->a_type==A_LONG){
			http=atom_getlong(argv);
		}else if(argv->a_type==A_SYM&&atom_getsym(argv)->s_name[0]==':'){
			http=1;
			http_port=atoi(atom_getsym(argv)->s_name+1);
		}else{
			object_error((t_object*)x,"o2ens: expected http-enable");
			CRITICAL_RET;
		}
		argc--,argv++;
	}

	o2ensemble_check_flags(x, &argc, &argv, &opt, &clock);

	//5th arg: http_root
	if(argc){
		if(argv->a_type==A_SYM){
			http_root=atom_getsym(argv)->s_name;
		}else{
			object_error((t_object*)x,"o2ens: expected symbol (path) for http-root");
			CRITICAL_RET;
		}
		argc--,argv++;
	}

	o2ensemble_check_flags(x, &argc, &argv, &opt, &clock);

	if(argc){
		object_error((t_object*)x,"Extra parameter(s) in o2ensemble ignored");
	}

	char mqtt_info[64]="";
	if(mqtt_ip[0]){
		snprintf(mqtt_info,63," (MQTT url %s",mqtt_ip);
		int len=(int)strlen(mqtt_info);
		if(mqtt_port){
			snprintf(mqtt_info+len,64-len,":%d)",mqtt_port);
		}else{
			strcpy(mqtt_info+len,")");
		}
	}

	char http_info[64];
	snprintf(http_info,64," (port %d,root %s)",http_port,http_root);

	char flag_info[64]="";
	if(opt){
		snprintf(flag_info,64," flags %s",opt);
	}
	post("o2ens: name %s network-level %d%s o2lite %d http %d%s%s",
	     ensemble_name,network_level,mqtt_info,o2lite,http,http_info,flag_info);
	
	//activate

	o2ens_active=x;
	if(opt){
		o2_debug_flags(opt);
	}
	O2CALL(x,"network enable",o2_network_enable(network_level>0));
	O2CALL(x,"internet enable",o2_internet_enable(network_level>1));
	O2CALL(x,"initialization",o2_initialize(ensemble_name));
	if(clock){
		O2CALL(x,"clock",o2_clock_set(NULL,NULL));
	}
	if(network_level>2){
		O2CALL(x,"mqtt enable",o2_mqtt_enable(mqtt_ip,mqtt_port));
	}
	if(o2lite){
		O2CALL(x,"o2lite initialization",o2lite_initialize());
	}
	if(http){
		char dot[16];
		o2_hex_to_dot(o2n_internal_ip,dot);
		int p=http_port?http_port:8080;
		post("o2ensemble creatinig http://%s:%d serving %s\n",dot,p,http_root);
		O2CALL(x,"http initialization",o2_http_initialize(http_port,http_root));
	}
	CRITICAL_RET;
}

static t_class *s_o2ensemble_class;

//called when an o2ensemble is created
void *o2ensemble_new(t_symbol *s,long argc,t_atom *argv){
	t_o2ensemble *x=(t_o2ensemble*)object_alloc(s_o2ensemble_class);
	outlet_new((t_object*)x,NULL);
	CRITICAL_ENT;
	if(!(o2ens_count++)){
		o2ens_timer=clock_new(NULL,(method)o2ensemble_clock_tick);
		o2ensemble_clock_tick(NULL);
	}
	//add it to o2ens_list
	x->next=o2ens_list;
	o2ens_list=x;
	o2ensemble_initialize(x,false,argc,argv);
	CRITICAL_RETA(x);
}

//join an ensemble, and initialize O2
void o2ensemble_join(t_o2ensemble *x,t_symbol *s,int argc,t_atom *argv){
	post("o2ens: join");
	o2ensemble_initialize(x,true,argc,argv);
}

//leave the ensemble
void o2ensemble_leave(t_o2ensemble *x){
	post("o2ens: leave");
	CRITICAL_ENT;
	if(o2ens_active&&x!=o2ens_active) {
		object_error((t_object*)x,"leave sent to inactive o2ensemble; ignored");
		CRITICAL_RET;
	}
	if(o2_ensemble_name==NULL){  //no ensemble active; ignored
		object_error((t_object*)x,"nothing to leave; O2 is not initialized");
		CRITICAL_RET;
	}
	o2_finish();
	remove_all_addressnodes();
	CRITICAL_RET;
}

void o2ensemble_version(t_o2ensemble *x){
	post("o2ens: version");
	char vers[16];
	t_atom outv[2];
	o2_version(vers);
	atom_setsym(outv,gensym(vers));
	outlet_anything(x->x_obj.o_outlet,gensym("version"),1,outv);
}

void o2ensemble_hex_to_dot(const char *hex, char *dot){
	int i1=o2_hex_to_byte(hex);
	int i2=o2_hex_to_byte(hex+2);
	int i3=o2_hex_to_byte(hex+4);
	int i4=o2_hex_to_byte(hex+6);
	snprintf(dot,16,"%d.%d.%d.%d",i1,i2,i3,i4);
}

//get address information
void o2ensemble_addresses(t_o2ensemble *x){
	post("o2ens: addresses");
	if(o2_ensemble_name==NULL) {
		object_error((t_object*)x,"O2 is not initialized");
	}else{
		const char *public_ip="";
		const char *internal_ip="";
		int port=0;
		char public_dot[24];
		char internal_dot[24];
		char port_string[24];
		O2CALL(x,"o2_get_addresses",
		        o2_get_addresses(&public_ip,&internal_ip,&port));
		o2ensemble_hex_to_dot(public_ip,public_dot);
		o2ensemble_hex_to_dot(internal_ip,internal_dot);
		snprintf(port_string,24,"%d",port);
		t_atom outv[3];
		atom_setsym(outv,gensym(public_dot));
		atom_setsym(outv+1,gensym(internal_dot));
		atom_setsym(outv+2,gensym(port_string));
		outlet_list(x->x_obj.o_outlet,NULL,3,outv);
	}
}

void o2ensemble_check_tap_flag(int *argc,t_atom **argv,O2tap_send_mode *mode){
	if(*argc&&(*argv)->a_type==A_SYM){
		const char *opt=atom_getsym(*argv)->s_name;
		if(streql(opt,"-r")){
			*mode=TAP_RELIABLE;
		}else if(streql(opt,"-b")){
			*mode=TAP_BEST_EFFORT;
		}else if(streql(opt,"-k")){
			*mode=TAP_KEEP;
		}else return;
		(*argc)--,(*argv)++;
	}
}

//tap a service
void o2ensemble_tap(t_o2ensemble *x,t_symbol *s,int argc,t_atom *argv){
	post("o2ens: tap");
	if(o2_ensemble_name==NULL) {
		object_error((t_object*)x,"O2 is not initialized");
	}else{
		O2tap_send_mode send_mode=TAP_KEEP;
		const char *tappee;
		const char *tapper;
		o2ensemble_check_tap_flag(&argc,&argv,&send_mode);
		if (argc&&argv->a_type==A_SYM) { //get tappee
			tappee=atom_getsym(argv)->s_name;
		}else{
			object_error((t_object*)x,"O2 tap: tappee not found");
			return;
		}
		argc--,argv++;
		o2ensemble_check_tap_flag(&argc,&argv,&send_mode);
		if(argc&&argv->a_type==A_SYM) {  //get tapper
			tapper=atom_getsym(argv)->s_name;
		}else{
			object_error((t_object*)x,"O2 tap: tapper not found");
			return;
		}
		argc--,argv++;
		o2ensemble_check_tap_flag(&argc, &argv, &send_mode);
		if(argc){
			object_error((t_object*)x,"O2 tap: extra parameters ignored");
		}
		O2CALL(x,"tap",o2_tap(tappee,tapper,send_mode));
	}
}

//untap a service
void o2ensemble_untap(t_o2ensemble *x,t_symbol *tappee,t_symbol *tapper){
	O2CALL(x,"untap",o2_untap(tappee->s_name,tapper->s_name));
}

//get the status of a service
void o2ensemble_status(t_o2ensemble *x,t_symbol *service){
	CRITICAL_ENT;
	int status=o2_status(service->s_name);
	CRITICAL_EXT;
	if(status>=-1){
		t_atom outv[2];
		atom_setsym(outv,gensym(service->s_name));
		atom_setlong(outv+1,status);
		outlet_anything(x->x_obj.o_outlet,gensym("status"),2,outv);
	}else{
		o2_call((t_object*)x,"status",status);
	}
}

//get time
void o2ensemble_time(t_o2ensemble *x){
	CRITICAL_ENT;
	O2time now=o2_time_get();
	CRITICAL_EXT;
	if(now>=0){
		t_atom outv[1];
		atom_setfloat(outv,now*1000);
		outlet_anything(x->x_obj.o_outlet,gensym("time"),1,outv);
	}
}

//set reference clock existence
void o2ensemble_clock(t_o2ensemble *x,t_atom_long reference_flag){
	o2ens_is_clock_ref=(reference_flag>0);
}

void o2ensemble_clockjump(t_o2ensemble *x,t_atom_float localms,t_atom_float globalms,t_atom_float adjust){
	o2ens_clockjump_called=true;
	O2CALL(x,"clock_jump",o2_clock_jump(localms*0.001,globalms*0.001,adjust!=0));
}

void o2ensemble_check_tcp_flag(int *argc,t_atom **argv,int *mode){
	if(*argc&&(*argv)->a_type==A_SYM){
		const char *opt=atom_getsym(*argv)->s_name;
		if(streql(opt,"-r")){
			*mode=true;
		}else if(streql(opt,"-b")){
			*mode=false;
		}else{
			return;
		}
		(*argc)--,(*argv)++;
	}
}

//create an osc server port - we become an OSC server
void o2ensemble_oscport(t_o2ensemble *x,t_symbol *s,int argc,t_atom *argv){
	post("o2ens: oscport");
	if(o2_ensemble_name==NULL){
		object_error((t_object*)x,"O2 is not initialized");
	}else{
		int tcp_flag=false;
		const char *service;
		int port;
		o2ensemble_check_tcp_flag(&argc,&argv,&tcp_flag);
		if(argc&&argv->a_type==A_SYM){ //service
			service=atom_getsym(argv)->s_name;
		}else{
			object_error((t_object*)x,"O2 oscport: service not specified");
			return;
		}
		argc--,argv++;
		o2ensemble_check_tcp_flag(&argc,&argv,&tcp_flag);
		if(argc&&argv->a_type==A_LONG){ //port
			port=atom_getlong(argv);
		}else{
			object_error((t_object*)x,"O2 oscport: port not specified");
			return;
		}
		argc--,argv++;
		o2ensemble_check_tcp_flag(&argc,&argv,&tcp_flag);
		if(argc){
			object_error((t_object*)x,"O2 oscport: extra parameters ignored");
		}
		O2CALL(x,"oscport",o2_osc_port_new(service,port,tcp_flag));
	}
}

//delegate o2 service to an osc port - we become an osc client
void o2ensemble_oscdelegate(t_o2ensemble *x,t_symbol *s,int argc,t_atom *argv){
	post("o2ens: oscdelegate");
	if(o2_ensemble_name==NULL){
		object_error((t_object*)x,"O2 is not initialized");
	}else{
		int tcp_flag=false;
		const char *service;
		const char *address;
		int port;
		o2ensemble_check_tcp_flag(&argc,&argv,&tcp_flag);
		if(argc&&argv->a_type==A_SYM){ //service
			service=atom_getsym(argv)->s_name;
		}else{
			object_error((t_object*)x,"O2 oscdelegate: service not specified");
		}
		argc--,argv++;
		o2ensemble_check_tcp_flag(&argc,&argv,&tcp_flag);
		if(argc&&argv->a_type==A_SYM){ //address
			address=atom_getsym(argv)->s_name;
		}else{
			object_error((t_object*)x,"O2 oscdelegate: address not specified");
		}
		argc--,argv++;
		o2ensemble_check_tcp_flag(&argc,&argv,&tcp_flag);
		if(argc&&argv->a_type==A_LONG){ //port
			port=atom_getlong(argv);
		}else{
			object_error((t_object*)x,"O2 oscdelegate: port not specified");
		}
		argc--,argv++;
		o2ensemble_check_tcp_flag(&argc,&argv,&tcp_flag);
		if(argc){
			object_error((t_object*)x,"O2 oscdelegate: extra parameters ignored");
		}
		O2CALL(x,"oscdelegate",o2_osc_delegate(service,address,port,tcp_flag));
	}
}

void o2ensemble_free(t_o2ensemble *x){
	post("o2ens: free");
	CRITICAL_ENT;
	if(--o2ens_count==0){
		clock_free(o2ens_timer);
	}
	t_o2ensemble *pre=NULL,*o2ens=o2ens_list;
	while(o2ens!=x){
		pre=o2ens;
		o2ens=o2ens->next;
	}
	if(!o2ens){
		object_error((t_object*)x,"(internal error) not found in o2ensemble list");
		CRITICAL_RET;
	}
	if(!pre){
		o2ens_list=o2ens_list->next;
	}else{
		pre->next=x->next;
	}
	x->next=NULL;
	if(x==o2ens_active){
		o2ens_active=NULL;
		O2CALL(x,"time_jump_callback_set",o2_time_jump_callback_set(NULL));
	}
	CRITICAL_RET;
}

void ext_main(void *r){
	t_class *c;
	c=class_new("o2ensemble",(method)o2ensemble_new,
	            (method)o2ensemble_free,sizeof(t_o2ensemble),0L,A_GIMME,0);
	class_addmethod(c,(method)o2ensemble_join,
	                "join",A_GIMME,0);
	class_addmethod(c,(method)o2ensemble_leave,
	                "leave",0);
	class_addmethod(c,(method)o2ensemble_version,
	                "version",0);
	class_addmethod(c,(method)o2ensemble_addresses,
	                "addresses",0);
	class_addmethod(c,(method)o2ensemble_tap,
	                "tap",A_GIMME,0);
	class_addmethod(c,(method)o2ensemble_untap,
	                "untap",A_SYM,A_SYM,0);
	class_addmethod(c,(method)o2ensemble_status,
	                "status",A_SYM,0);
	class_addmethod(c,(method)o2ensemble_time,
	                "time",0);
	class_addmethod(c,(method)o2ensemble_clock,
	                "clock",A_LONG,0);
	class_addmethod(c,(method)o2ensemble_clockjump,
	                "clockjump",A_FLOAT,A_FLOAT,A_FLOAT,0);
	class_addmethod(c,(method)o2ensemble_oscport,
	                "oscport",A_GIMME,0);
	class_addmethod(c,(method)o2ensemble_oscdelegate,
	                "oscdelegate",A_GIMME,0);
	class_register(CLASS_BOX,c);
	s_o2ensemble_class=c;
}