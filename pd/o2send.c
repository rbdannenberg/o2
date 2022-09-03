/* o2send - pd class for o2. */
/* Roger B. Dannenberg
 * July 2022
 */

#include <stdlib.h>
#include <string.h>
#include "o2ensemble.h"  // includes o2 and some helper functions for pd o2
#include "x_vexp.h"
#include "z_libpd.h"
#ifdef _WIN32
# include <malloc.h> /* MSVC or mingw on windows */
#elif defined(__linux__) || defined(__APPLE__) || defined(HAVE_ALLOCA_H)
# include <alloca.h> /* linux, mac, mingw, cygwin */
#else
# include <stdlib.h> /* BSDs for example */
#endif
//#include "o2.h"


typedef struct o2snd
{
    t_object x_obj;
    const char *servicename;  // owned by pd (symbol name)
    const char *address;  // owned by o2snd
    const char *types;  // owned by pd (symbol name)
    double timestamp;   // time to send next message
    int tcp_flag;
} t_o2snd;


void o2snd_check_flags(t_o2snd *x, int *argc, t_atom **argv)
{
    const char *opt;
    while (*argc > 0 && (*argv)->a_type == A_SYMBOL &&
           (opt = (*argv)[0].a_w.w_symbol->s_name)[0] == '-') {
        if (!strcmp(opt, "-t") && *argc > 1 && (*argv)[1].a_type == A_SYMBOL) {
            /* to assign, types must not be const **, so we have to
               cast away const from s_name: */
            x->types = (char *) ((*argv)[1].a_w.w_symbol->s_name);
            (*argc)--;
            (*argv)++;
        } else if (!strcmp(opt, "-r")) {
            x->tcp_flag = true;
        } else if (!strcmp(opt, "-b")) {
            x->tcp_flag = false;
        } else {
            pd_error(x, "o2send expected option %s", opt);
        }
        (*argc)--;
        (*argv)++;
    }
}


static void get_address(t_o2snd *x,  t_symbol *s, int argc, t_atom *argv)
{
    const char *types = NULL;
    int tcp_flag = false;  // default for sending is UDP
    char path[128];
    int last = 0;  // index of EOS
    path[0] = 0;
    o2snd_check_flags(x, &argc, &argv);
    // check for -t <types> before or after path nodes
    while (argc > 0 && argv->a_type == A_SYMBOL) {  /* get next node */
        const char *nodename = argv->a_w.w_symbol->s_name;
        int nodelen = strlen(nodename);
        if (!x->servicename) {  // keep first node as service name
            x->servicename = nodename;
        }
        if (last + nodelen + 2 >= 128) {
            pd_error(x, "O2 address is too long");
            return;
        }
        path[last++] = '/';
        strcpy(path + last, nodename);
        last += nodelen;
        argc--; argv++;
        o2snd_check_flags(x, &argc, &argv);
    }
    
    if (path[0]) {
        int len = strlen(path);
        if (x->address) {
            freebytes((void *)(x->address), strlen(x->address) + 1);
        }
        x->address = (const char *) getbytes(len + 1);
        strncpy((char *)(x->address), path, len + 1);
    }
    
    if (argc) {  /* should be done by now */
        pd_error(x, "O2 address: extra parameters ignored");
    }
}


void o2snd_address(t_o2snd *x,  t_symbol *s, int argc, t_atom *argv)
{
    post("o2snd: address");
    get_address(x, s, argc, argv);
}


void o2snd_time(t_o2snd *x, float time)
{
    post("o2snd: time %g", time);
    x->timestamp = time;
}


void o2snd_delay(t_o2snd *x, float delay)
{
    post("o2snd: delay %g", delay);
    O2time now = o2_time_get();
    if (now >= 0) {
        x->timestamp = now * 1000 + delay;
    } else {
        pd_error(x, "o2send delay: O2 is not initialized");
    }
}


void o2snd_types(t_o2snd *x, t_symbol *types)
{
    post("o2snd: types %s", types->s_name);
    x->types = types->s_name;
    if (x->types[0] == 0) x->types = NULL;
    for (int i = 0; x->types[i]; i++) {
        if (strchr("ifhdtsSc", x->types[i]) == NULL) {
            pd_error(x, "o2send: types string %s has invalid character %c",
                     x->types, x->types[i]);
            x->types = NULL;
        }
    }
}


void o2snd_status(t_o2snd *x)
{
    post("o2snd: status (for %s)", x->servicename);
    int status = o2_status(x->servicename);
    // careful: any result >=0 indicates "no error":
    if (o2ens_error_report(&x->x_obj, "status", status) >= 0) {
        t_atom outv[2];
        SETSYMBOL(outv, gensym(x->servicename));
        SETFLOAT(outv + 1, status);
        outlet_anything(x->x_obj.ob_outlet, gensym("status"), 2, outv);
        DBG printf("o2snd_status: %d\n", status);
    }
}


// send
void o2snd_list(t_o2snd *x,  t_symbol *s, int argc, t_atom *argv)
{
    bool error_flag = false;
    post("o2snd: list");
    DBG printf("o2snd: list\n"); DBG fflush(stdout);
    if (!o2_ensemble_name) {
        pd_error(x, "o2send: o2 not initialized");
        return;
    }
    o2_send_start();
    DBG printf("o2snd: list send started\n"); DBG fflush(stdout);
    
    if (x->types) {
        int tlen = (int) strlen(x->types);
        if (tlen != argc) {
            pd_error(x, "o2send: arg count does not match types %s length %d",
                     x->types, tlen);
            return;
        }
    }
    DBG printf("o2snd: types checked\n"); DBG fflush(stdout);
    for (int i = 0; i < argc; i++) {
        t_atom *arg = &argv[i];
        if (x->types) {
            char tchar = x->types[i];
            switch (tchar) {
            case 'i':
                if (arg->a_type != A_FLOAT) {
                    error_flag = true;
                } else {
                    o2_add_int32((int) arg->a_w.w_float);
                }
                break;
            case 'f':
                if (arg->a_type != A_FLOAT) {
                    error_flag = true;
                } else {
                    o2_add_float(arg->a_w.w_float);
                    DBG printf("o2snd: add float %g\n", arg->a_w.w_float);
                    DBG fflush(stdout);
                }
                break;
            case 'h':
                if (arg->a_type != A_FLOAT) {
                    error_flag = true;
                } else {
                    o2_add_int64((int64_t) arg->a_w.w_float);
                }
                break;
            case 'd':
                if (arg->a_type != A_FLOAT) {
                    error_flag = true;
                } else {
                    o2_add_double(arg->a_w.w_float);
                }
                break;
            case 't':
                if (arg->a_type != A_FLOAT) {
                    error_flag = true;
                } else {
                    o2_add_time(arg->a_w.w_float);
                }
                break;
            case 'c':
                if (arg->a_type != A_FLOAT) {
                    error_flag = true;
                } else {
                    o2_add_char((int) arg->a_w.w_float);
                }
                break;
            case 's':
                if (arg->a_type != A_SYMBOL) {
                    error_flag = true;
                } else {
                    o2_add_string(arg->a_w.w_symbol->s_name);
                }
                break;
            case 'S':
                if (arg->a_type != A_SYMBOL) {
                    error_flag = true;
                } else {
                    o2_add_symbol(arg->a_w.w_symbol->s_name);
                }
                break;
            default:
                pd_error(x, "o2send: unexpected type character %c", tchar);
                return;
            }
            if (error_flag) {
                pd_error(x, "o2send: arg %d incompatible with type %c",
                         i, tchar);
                return;
            }
        } else if (arg->a_type == A_FLOAT) {  // add according to Pd types
            o2_add_float(arg->a_w.w_float);
            DBG printf("o2snd: add float %g\n", arg->a_w.w_float);
            DBG fflush(stdout);
        } else if (arg->a_type == A_SYMBOL) {
            o2_add_string(arg->a_w.w_symbol->s_name);
        } else {
            pd_error(x, "o2send arg %d is not a float or symbol", i);
            return;
        }
    }
    DBG printf("o2snd: finish %g %s %d\n",
               x->timestamp * 0.001, x->address, x->tcp_flag);
    DBG fflush(stdout);
    o2ens_error_report(&x->x_obj, "o2send",
                       o2_send_finish(x->timestamp * 0.001, x->address, x->tcp_flag));
    x->timestamp = 0;
}


/* this is a pointer to the class for "o2snd", which is created in the
   "setup" routine below and used to create new ones in the "new" routine. */
t_class *o2snd_class;

/* this is called when a new "o2snd" object is created. */
void *o2snd_new(t_symbol *s, int argc, t_atom *argv)
{
    t_o2snd *x = (t_o2snd *)pd_new(o2snd_class);
    DBG printf("o2snd_new called\n"); DBG fflush(stdout);
    x->servicename = NULL;
    x->address = NULL;
    x->types = NULL;
    get_address(x, s, argc, argv);
    outlet_new(&x->x_obj, &s_list);
    post("o2snd_new");
    return (void *)x;
}


void o2snd_free(t_o2snd *x)
{
    DBG printf("o2snd_free called\n"); DBG fflush(stdout);
    if (x->address) {
        freebytes((void *)(x->address), strlen(x->address) + 1);
    }
}


/* this is called once at setup time, when this code is loaded into Pd. */
void o2send_setup(void)
{
    post("o2snd_setup");
    o2snd_class = class_new(gensym("o2send"), (t_newmethod)o2snd_new,
                            (t_method)o2snd_free, sizeof(t_o2snd), 0,
                            A_GIMME, 0);
    class_addmethod(o2snd_class, (t_method)o2snd_address,
                                         gensym("address"), A_GIMME, 0);
    class_addmethod(o2snd_class, (t_method)o2snd_types,
                                         gensym("types"), A_SYMBOL, 0);
    class_addmethod(o2snd_class, (t_method)o2snd_time,
                                         gensym("time"), A_FLOAT, 0);
    class_addmethod(o2snd_class, (t_method)o2snd_delay,
                                         gensym("delay"), A_FLOAT, 0);
    class_addmethod(o2snd_class, (t_method)o2snd_list, &s_list, A_GIMME, 0);
    class_addmethod(o2snd_class, (t_method)o2snd_status, gensym("status"), 0);
}

