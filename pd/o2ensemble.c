/* o2ensemble - pd class for o2. */
/* Roger B. Dannenberg
 * July 2022
 */

#include <stdlib.h>
#include <string.h>
#include "o2ensemble.h"

/* global state for O2 interface */
static int o2ens_instance_count = 0;  /* counts number of o2 objects in Pd */
static t_clock *o2ens_timer = NULL;   /* clock object to schedule polling */
static long o2ens_ticks = 0;          /* counts calls to o2ens_clock_tick */
static int o2ens_is_clock_ref = true; /* if initialized, we provide the 
                                         reference clock */

/* Want to poll O2 at the Pd tick rate.
   Get a clock callback every tick, using APPROXTICKSPERSEC.
   From m_sched.c:
 */
#define APPROXTICKSPERSEC \
    ((int)(STUFF->st_dacsr / (double)STUFF->st_schedblocksize))

typedef struct o2ens
{
  t_object x_obj;
} t_o2ens;


/* this is called when the first o2ensemble is created and stopped when
   the last o2ensemble is deleted */
void o2ens_clock_tick(void *client)
{
    o2_poll();
    if (++o2ens_ticks == 1000) {
        post("o2ens count: %ld", o2ens_ticks);
    }
    clock_delay(o2ens_timer, 1000.0 / APPROXTICKSPERSEC);
}

/* this is called back when o2ensemble gets a "float" message (i.e., a
   number.) */
void o2ens_float(t_o2ens *x, t_floatarg f)
{
    post("o2ens: %f", f);
}


/* skip past the current *argv and check for -d flags, skipping those too */
void o2ens_check_flags(t_o2ens *x, int *argc, t_atom **argv,
                       char **options, int *clock)
{
    const char *opt;
    if (*argc > 1 && (*argv)->a_type == A_SYMBOL &&
        (opt = (*argv)[0].a_w.w_symbol->s_name)[0] == '-') {
        int a_type = (*argv)[1].a_type;
        if (!strcmp(opt, "-d") && (*argv)[1].a_type == A_SYMBOL) {
            /* to assign, options must not be const **, so we have to
               cast away const from s_name: */
            (*options) = (char *) ((*argv)[1].a_w.w_symbol->s_name);
        } else if (!strcmp(opt, "-c") && (*argv)[1].a_type == A_FLOAT) {
            /* to assign, options must not be const **, so we have to
               cast away const from s_name: */
            *clock = (atom_getfloat(*argv + 1) != 0);
        } else {
            pd_error(x, "o2ensemble unexpected option %s", opt);
        }
        (*argc) -= 2;
        (*argv) += 2;
    }
}


O2err o2ens_error_report(t_object *x, const char *context, O2err err)
{
    if (err < 0) {
        pd_error(x, "O2 %s error: %s", context, o2_error_to_string(err));
    }
    return err;
}


void o2ens_initialize(t_o2ens *x, int is_join, int argc, t_atom *argv)
{
    if (o2_ensemble_name != NULL) {
        pd_error(x, "O2 is already initialized");
    } else {
        const char *ens_name;
        int network_level = 2;
        int o2lite = 1;
        char mqtt_ip[32];
        int mqtt_port = 0;
        int http = 0;
        int http_port = 0;
        const char *http_root = "web";
        char *opt = NULL;
        int clock = true;
        mqtt_ip[0] = 0;  /* default indicated by empty string */

        o2ens_check_flags(x, &argc, &argv, &opt, &clock);

        if (argc) {
            if (argv->a_type == A_SYMBOL) {
                ens_name = argv->a_w.w_symbol->s_name;
            } else {
                pd_error(x, "O2: expected symbol for ensemble name");
                return;
            }
            argc--; argv++;
        } else if (is_join) {
            pd_error(x, "O2: join has no ensemble name; ignored");
            return;
        } else {  // do not start O2 when there are no parameters
            return;
        }

        o2ens_check_flags(x, &argc, &argv, &opt, &clock);

        if (argc) {
            if (argv->a_type == A_FLOAT) {  /* network level is 0-3 */
                network_level = atom_getfloat(argv);
            } else if (argv->a_type == A_SYMBOL) {
                network_level = 3; /* MQTT level */
                const char *ip = argv->a_w.w_symbol->s_name;
                const char *colon = strchr(ip, ':');
                strncpy(mqtt_ip, ip, 32);
                /* strncpy does not guarantee termination */
                mqtt_ip[31] = 0; /* truncate the IP string if too long */
                if (colon && (colon - ip) < 32) {
                    mqtt_ip[colon - ip] = 0; /* terminate at colon */
                    mqtt_port = atoi(colon + 1);
                }
            } else {
                pd_error(x, "O2 join expected float for network-level");
                return;
            }
            argc--; argv++;
        }

        o2ens_check_flags(x, &argc, &argv, &opt, &clock);

        if (argc) {
            if (argv->a_type == A_FLOAT) {  /* get o2lite-enable */
                o2lite = atom_getfloat(argv);
            } else {
                pd_error(x, "O2 join expected float for o2lite-enable");
                return;
            }
            argc--; argv++;
        }

        o2ens_check_flags(x, &argc, &argv, &opt, &clock);

        if (argc) {
            if (argv->a_type == A_FLOAT) {  /* get http-enable */
                http = atom_getfloat(argv + 1);
            } else if (argc && argv->a_type == A_SYMBOL &&
                       argv->a_w.w_symbol->s_name[0] == ':') {
                http = 1;
                http_port = atoi(argv->a_w.w_symbol->s_name + 1);
            } else {
                pd_error(x, "O2 join expected http-enable");
                return;
            }
            argc--; argv++;
        }

        o2ens_check_flags(x, &argc, &argv, &opt, &clock);
        if (argc) {
            if (argv->a_type == A_SYMBOL) {  /* get http-root */
                http_root = argv->a_w.w_symbol->s_name;
            } else {
                pd_error(x, "O2 join expected symbol (path) for http-root");
                return;
            }
            argc--; argv++;
        }

        o2ens_check_flags(x, &argc, &argv, &opt, &clock);
        if (argc) {
            pd_error(x, "Extra parameter(s) in join message ignored");
        }

        char mqtt_info[64] = "";
        if (mqtt_ip[0]) {
            snprintf(mqtt_info, 63, " (MQTT url %s", mqtt_ip);
            int len = strlen(mqtt_info);
            if (mqtt_port) {
                snprintf(mqtt_info + len, 64 - len, ":%d)", mqtt_port);
            } else {
                strcpy(mqtt_info + len, ")");
            }
        }

        char http_info[64] = "";
        if (http_port) {
            snprintf(http_info, 64, " (port %d, root %s)",
                     http_port, http_root);
        } else if (http) {
            snprintf(http_info, 64, " (root %s)", http_root);
        }
        
        char flag_info[64] = "";
        if (opt) {
            snprintf(flag_info, 64, " flags %s", opt);
        }

        post("O2 join: network-level %d%s o2lite %d http %d%s%s",
             network_level, mqtt_info, o2lite, http, http_info, flag_info);

        if (opt) {
            o2_debug_flags(opt);  /* returns void */
        }
        o2ens_error_report(&x->x_obj, "network enable",
                           o2_network_enable(network_level > 0));
        o2ens_error_report(&x->x_obj, "internet enable",
                           o2_internet_enable(network_level > 1));
        o2ens_error_report(&x->x_obj, "initialization",
                           o2_initialize(ens_name));
        if (clock) {
            o2ens_error_report(&x->x_obj, "clock",
                               o2_clock_set(NULL, NULL));
        }
        if (network_level > 2) {
            o2ens_error_report(&x->x_obj, "mqtt enable",
                               o2_mqtt_enable(mqtt_ip, mqtt_port));
        }
        if (o2lite) {
            o2ens_error_report(&x->x_obj, "o2lite initialization", 
                               o2lite_initialize());
        }
        if (http) {
            o2ens_error_report(&x->x_obj, "http initialization",
                               o2_http_initialize(http_port, http_root));
        }

        // if o2receive objects initialized first or we are rejoining
        // for some reason, we need to create services and install handlers
        for (servicenode **snode = &o2ens_services; *snode;
             snode = &((*snode)->next)) {
            if (!(*snode)->addresses) {  // no more handlers; delete service
                service_delete(&x->x_obj, snode, false,
                               "internal error: o2ensemble");
            } else {
                post("o2ensemble creating O2 service %s\n",
                     (*snode)->service);
                o2ens_error_report(&x->x_obj, "o2_service_new",
                                   o2_service_new((*snode)->service));
                if ((*snode)->wholeservice) {
                    addressnode *a = (*snode)->wholeservice;
                    // top-level service handler
                    o2ens_error_report(&x->x_obj, "o2ensemble",
                            o2_method_new(a->path, NULL, o2rcv_handler,
                                          (void *) a, false, false));
                } else {
                    install_handlers(&x->x_obj, (*snode)->addresses);
                }
            }
        }
    }
}


/* join an ensemble (initialize O2)  */
void o2ens_join(t_o2ens *x, t_symbol *s, int argc, t_atom *argv)
{
    post("o2ens: join");
    const char *ens_name;
    o2ens_initialize(x, true, argc, argv);
}


void o2ens_leave(t_o2ens *x)
{
    post("o2ens: leave");
    if (o2_ensemble_name == NULL) {
        pd_error(x, "O2 is not initialized");
    } else {
        o2_finish();
    }
}


void o2ens_version(t_o2ens *x)
{
    post("o2ens: version");
    char vers[16];
    t_atom outv[2];

    o2_version(vers);
    SETSYMBOL(outv, gensym(vers));
    outlet_anything(x->x_obj.ob_outlet, gensym("version"), 1, outv);
}


void o2ens_hex_to_dot(const char *hex, char *dot)
{
    int i1 = o2_hex_to_byte(hex);
    int i2 = o2_hex_to_byte(hex + 2);
    int i3 = o2_hex_to_byte(hex + 4);
    int i4 = o2_hex_to_byte(hex + 6);
    snprintf(dot, 16, "%d.%d.%d.%d", i1, i2, i3, i4);
}


void o2ens_addresses(t_o2ens *x)
{
    post("o2ens: addresses");
    if (o2_ensemble_name == NULL) {
        pd_error(x, "O2 is not initialized");
    } else {
        const char *public_ip = "";
        const char *internal_ip = "";
        int port = 0;
        char public_dot[24];
        char internal_dot[24];
        char port_string[24];
        o2ens_error_report(&x->x_obj, "o2_get_addresses",
                           o2_get_addresses(&public_ip, &internal_ip, &port));
        o2ens_hex_to_dot(public_ip, public_dot);
        o2ens_hex_to_dot(internal_ip, internal_dot);
        snprintf(port_string, 24, "%d", port);
        t_atom outv[3];
        SETSYMBOL(outv, gensym(public_dot));
        SETSYMBOL(outv + 1, gensym(internal_dot));
        SETSYMBOL(outv + 2, gensym(port_string));
        outlet_list(x->x_obj.ob_outlet, &s_list, 3, outv);
    }
}


/* test for and skip -r or -b flag */
void o2ens_check_tap_flag(int *argc, t_atom **argv, O2tap_send_mode *mode)
{
    if (*argc && (*argv)->a_type == A_SYMBOL) {
        if (!strcmp((*argv)->a_w.w_symbol->s_name, "-r")) {
            *mode = TAP_RELIABLE;
        } else if (!strcmp((*argv)->a_w.w_symbol->s_name, "-b")) {
            *mode = TAP_BEST_EFFORT;
        } else if (!strcmp((*argv)->a_w.w_symbol->s_name, "-k")) {
            *mode = TAP_KEEP;
        } else {
            return;  // no flag, so return without any changes.
        }
        (*argc)--;  // didn't return so we found a flag. Skip past it.
        (*argv)++;
    }
}


/* tap a service */
void o2ens_tap(t_o2ens *x,  t_symbol *s, int argc, t_atom *argv)
{
    post("o2ens: tap");
    if (o2_ensemble_name == NULL) {
        pd_error(x, "O2 is not initialized");
    } else {
        O2tap_send_mode send_mode = TAP_KEEP;
        const char *tappee;
        const char *tapper;
        o2ens_check_tap_flag(&argc, &argv, &send_mode);
        if (!argc || argv->a_type != A_SYMBOL) {  /* get tappee */
        }
        if (argc && argv->a_type == A_SYMBOL) {  /* get tappee */
            tappee = argv->a_w.w_symbol->s_name;
        } else {
            pd_error(x, "O2 tap: tappee not found");
            return;
        }
        argc--; argv++;

        o2ens_check_tap_flag(&argc, &argv, &send_mode);
        if (!argc || argv->a_type != A_SYMBOL) {  /* get tapper */
            pd_error(x, "O2 tap: tapper not found");
            return;
        }
        tapper = argv->a_w.w_symbol->s_name;
        argc--; argv++;

        o2ens_check_tap_flag(&argc, &argv, &send_mode);
        if (argc) {  /* should be done by now */
            pd_error(x, "O2 tap: extra parameters ignored");
        }

        o2ens_error_report(&x->x_obj, "tap", o2_tap(tappee, tapper, send_mode));
    }
}


void o2ens_untap(t_o2ens *x, t_symbol *tappee, t_symbol *tapper)
{
    o2ens_error_report(&x->x_obj, "untap", o2_untap(tappee->s_name,
                                            tapper->s_name));
}


void o2ens_status(t_o2ens *x, t_symbol *service)
{
    int status = o2_status(service->s_name);
    o2ens_error_report(&x->x_obj, "status", status);
    if (status >= 0) {
        t_atom outv[2];
        SETSYMBOL(outv, gensym(service->s_name));
        SETFLOAT(outv + 1, status);
        outlet_anything(x->x_obj.ob_outlet, gensym("status"), 2, outv);
    }
}


void o2ens_time(t_o2ens *x)
{
    O2time now = o2_time_get();
    if (now >= 0) {
        t_atom outv[2];
        SETFLOAT(outv, now * 1000);  // output in ms
        outlet_anything(x->x_obj.ob_outlet, gensym("time"), 1, outv);
    }
}


void o2ens_clock(t_o2ens *x, float reference_flag)
{
    o2ens_is_clock_ref = (reference_flag > 0);
}


void o2ens_check_tcp_flag(int *argc, t_atom **argv, int *mode)
{
    if (*argc && (*argv)->a_type == A_SYMBOL) {
        if (!strcmp((*argv)->a_w.w_symbol->s_name, "-r")) {
            *mode = true;
        } else if (!strcmp((*argv)->a_w.w_symbol->s_name, "-b")) {
            *mode = false;
        } else {
            return;  // no flag, so return without any changes.
        }
        (*argc)--;  // didn't return so we found a flag. Skip past it.
        (*argv)++;
    }
}


/* create an osc server port - we become an OSC server */
void o2ens_oscport(t_o2ens *x,  t_symbol *s, int argc, t_atom *argv)
{
    post("o2ens: oscport");
    if (o2_ensemble_name == NULL) {
        pd_error(x, "O2 is not initialized");
    } else {
        int tcp_flag = false;
        const char *service;
        int port;

        o2ens_check_tcp_flag(&argc, &argv, &tcp_flag);
        if (argc && argv->a_type == A_SYMBOL) {  /* get service*/
            service = argv->a_w.w_symbol->s_name;
        } else {
            pd_error(x, "O2 oscport: service not specified");
            return;
        }
        argc--; argv++;

        o2ens_check_tcp_flag(&argc, &argv, &tcp_flag);
        if (argc && argv->a_type == A_FLOAT) {  /* get port */
            port = atom_getfloat(argv);
        } else {
            pd_error(x, "O2 oscport: port not specified");
            return;
        }
        argc--; argv++;

        o2ens_check_tcp_flag(&argc, &argv, &tcp_flag);
        if (argc) {  /* should be done by now */
            pd_error(x, "O2 oscport: extra parameters ignored");
        }
        o2ens_error_report(&x->x_obj, "oscport", 
                           o2_osc_port_new(service, port, tcp_flag));
    }
}


/* delegate o2 service to an osc port - we become an osc client */
void o2ens_oscdelegate(t_o2ens *x,  t_symbol *s, int argc, t_atom *argv)
{
    post("o2ens: oscdelegate");
    if (o2_ensemble_name == NULL) {
        pd_error(x, "O2 is not initialized");
    } else {
        int tcp_flag = false;
        const char *service;
        const char *address;
        int port;

        o2ens_check_tcp_flag(&argc, &argv, &tcp_flag);
        if (argc && argv->a_type == A_SYMBOL) {  /* get service */
            service = argv->a_w.w_symbol->s_name;
        } else {
            pd_error(x, "O2 oscdelegate: service not specified");
            return;
        }
        argc--; argv++;

        o2ens_check_tcp_flag(&argc, &argv, &tcp_flag);
        if (argc && argv->a_type == A_SYMBOL) {  /* get address */
            address = argv->a_w.w_symbol->s_name;
        } else {
            pd_error(x, "O2 oscdelegate: address not specified");
            return;
        }
        argc--; argv++;

        o2ens_check_tcp_flag(&argc, &argv, &tcp_flag);
        if (argc && argv->a_type == A_FLOAT) {  /* get port */
            port = atom_getfloat(argv);
        } else {
            pd_error(x, "O2 oscdelegate: port not specified");
            return;
        }
        argc--; argv++;

        o2ens_check_tcp_flag(&argc, &argv, &tcp_flag);
        if (argc) {  /* should be done by now */
            pd_error(x, "O2 oscdelegate: extra parameters ignored");
        }
        o2ens_error_report(&x->x_obj, "oscdelegate", 
                           o2_osc_delegate(service, address, port, tcp_flag));
    }
}


/* this is a pointer to the class for "o2ens", which is created in the
   "setup" routine below and used to create new ones in the "new" routine. */
t_class *o2ens_class;

/* this is called when a new "o2ens" object is created. */
void *o2ens_new(t_symbol *s, int argc, t_atom *argv)
{
    t_o2ens *x = (t_o2ens *)pd_new(o2ens_class);
    DBG printf("o2ens_new called argc %d argv %p\n", argc, argv);
    DBG fflush(stdout);
    // outlet_new(&x->x_obj, gensym("bang"));
    outlet_new(&x->x_obj, &s_list);
    if (o2ens_instance_count++ == 0) {
        o2ens_timer = clock_new(NULL, (t_method)o2ens_clock_tick);
        post("o2ens_timer dacsr %g blocksize %d", 
             STUFF->st_dacsr, STUFF->st_schedblocksize);
        o2ens_clock_tick(NULL);  // get the clock started
    }
    o2ens_initialize(x, false, argc, argv);
    return (void *)x;
}


void o2ens_free(t_o2ens *x)
{
    DBG printf("o2ens_free called\n"); DBG fflush(stdout);
    if (--o2ens_instance_count == 0) {
        clock_free(o2ens_timer);
    }
}


/* this is called once at setup time, when this code is loaded into Pd. */
void o2ensemble_setup(void)
{
    post("o2ens_setup");
    o2ens_class = class_new(gensym("o2ensemble"), (t_newmethod)o2ens_new,
                    (t_method)o2ens_free, sizeof(t_o2ens), 0, A_GIMME, 0);
    class_addmethod(o2ens_class, (t_method)o2ens_join,
                                         gensym("join"), A_GIMME, 0);
    class_addmethod(o2ens_class, (t_method)o2ens_leave,
                                         gensym("leave"), A_NULL, 0);
    class_addmethod(o2ens_class, (t_method)o2ens_version, 
                                         gensym("version"), A_NULL, 0);
    class_addmethod(o2ens_class, (t_method)o2ens_addresses,
                                         gensym("addresses"), A_NULL, 0);
    class_addmethod(o2ens_class, (t_method)o2ens_tap,
                                         gensym("tap"), A_GIMME, 0);
    class_addmethod(o2ens_class, (t_method)o2ens_untap, 
                                        gensym("untap"), A_SYMBOL, A_SYMBOL, 0);
    class_addmethod(o2ens_class, (t_method)o2ens_status,
                                         gensym("status"), A_SYMBOL, 0);
    class_addmethod(o2ens_class, (t_method)o2ens_time,
                                         gensym("time"), A_NULL, 0);
    class_addmethod(o2ens_class, (t_method)o2ens_clock,
                                         gensym("clock"), A_FLOAT, 0);
    class_addmethod(o2ens_class, (t_method)o2ens_oscport,
                                         gensym("oscport"), A_GIMME, 0);
    class_addmethod(o2ens_class, (t_method)o2ens_oscdelegate,
                                         gensym("oscdelegate"), A_GIMME, 0);
}

