/* o2property - pd class for o2. */
/* Roger B. Dannenberg
 * Aug 2022
 */

#include <stdlib.h>
#include <string.h>
#include <float.h>
#include "o2ensemble.h"  // includes o2 and some helper functions for pd o2
#include "x_vexp.h"
#include "z_libpd.h"
#include "o2pd.h"
#ifdef _WIN32
# include <malloc.h> /* MSVC or mingw on windows */
#elif defined(__linux__) || defined(__APPLE__) || defined(HAVE_ALLOCA_H)
# include <alloca.h> /* linux, mac, mingw, cygwin */
#else
# include <stdlib.h> /* BSDs for example */
#endif


// limit exists to avoid overflowing stack, but there's no clear
// upper bound. Does anyone really want to send 100 parameters in
// one message?
#define MAX_O2_ARGS 100
#define ATOMS_ALLOCA(x, n) ((x) = (t_atom *)((n) <= MAX_O2_ARGS ? \
                                   alloca((n) * sizeof(t_atom)) : NULL));

typedef struct o2prop
{
    t_object x_obj;
    const char *service;
    const char *attribute;
    t_outlet *x_outlet1;  // value is output here
    t_outlet *x_outlet2;  // bang is output here if no value
} t_o2prop;


static void o2prop_bang(t_o2prop *x)
{
    DBG printf("o2prop_bang\n"); DBG fflush(stdout);
    const char *sname = NULL;
    int i;
    const char *value;
    if (x->service && x->attribute) {
        o2_services_list();
        for (i = 0; (sname = o2_service_name(i)) != NULL; i++) {
            DBG printf("o2prop_bang service %s\n", sname); DBG fflush(stdout);
            if (streql(sname, x->service)) {
                break;
            }
        }
    }
    // if sname != NULL, then we have a service and attribute and we
    // searched for and found the index i of the service named sname:
    if (sname && (value = o2_service_getprop(i, x->attribute))) {
        DBG printf("o2prop_bang value %s\n", value); DBG fflush(stdout);
        outlet_symbol(x->x_outlet1, gensym(value));
    } else {  // either no args, no service, or no property:
        outlet_bang(x->x_outlet2);
    }
    o2_services_list_free();  // harmless even if no previous 
                              // call to o2_services_list
}


static void o2prop_get(t_o2prop *x, t_symbol *service, t_symbol *attribute)
{
    o2pd_post("o2prop: get");
    if (o2_ensemble_name == NULL) {
        pd_error(x, "o2property: O2 is not initialized");
    } else {
        DBG printf("o2prop_get service %p attribute %p\n",
                   service, attribute); DBG fflush(stdout);
        x->service = service->s_name;
        x->attribute = attribute->s_name;
        DBG printf("o2prop_get service name %s attribute name %s\n",
                   x->service, x->attribute); DBG fflush(stdout);
        o2prop_bang(x);
    }
}



static void o2prop_put(t_o2prop *x,  t_symbol *s, int argc, t_atom *argv)
{
    o2pd_post("o2prop: put");
    if (o2_ensemble_name == NULL) {
        pd_error(x, "o2property: O2 is not initialized");
        return;
    } else if (argc < 2 || argv[0].a_type != A_SYMBOL ||
               argv[1].a_type != A_SYMBOL) {
        pd_error(x, "o2property put requires at least service and attribute");
        return;
    }
    x->service = argv[0].a_w.w_symbol->s_name;
    x->attribute = argv[1].a_w.w_symbol->s_name;
    if (argc == 2) {
        o2pd_error_report(&x->x_obj, "o2_service_property_free",
                           o2_service_property_free(x->service, x->attribute));
    } else if (argc == 3) {
        const char *value = argv[2].a_w.w_symbol->s_name;
        o2pd_error_report(&x->x_obj, "o2_service_set_property",
                           o2_service_set_property(x->service, x->attribute,
                                                   value));
    } else {
        pd_error(x, "o2property got >3 arguments, list ignored");
    }        
}


static void o2prop_search(t_o2prop *x,  t_symbol *s,
                          t_symbol *attr, t_symbol *val)
{
    o2pd_post("o2prop: search");
    if (o2_ensemble_name == NULL) {
        pd_error(x, "o2property: O2 is not initialized");
        return;
    }
    const char *attribute = attr->s_name;
    const char *value = val->s_name;
    int resultmax = 8;
    int argc = 0;
    t_atom *result = (t_atom *)getbytes(resultmax * sizeof(t_atom));
    int si = 0;
    while ((si = o2_service_search(si, attribute, value)) >= 0) {
        if (argc >= resultmax) {
            result = resizebytes(result, resultmax * sizeof(t_atom),
                                 2 * resultmax * sizeof(t_atom));
            resultmax *= 2;
        }
        SETSYMBOL(result + argc, gensym(o2_service_name(si)));
        argc++;
        si++;  // start search after the service we just found
    }
    outlet_list(x->x_outlet1, &s_list, argc, result);
    freebytes(result, resultmax * sizeof(t_atom));
}


/* this is a pointer to the class for "o2prop", which is created in the
   "setup" routine below and used to create new ones in the "new" routine. */
t_class *o2prop_class;

/* this is called when a new "o2prop" object is created. */
void *o2prop_new(t_symbol *s, int argc, t_atom *argv)
{
    t_o2prop *x = (t_o2prop *)pd_new(o2prop_class);
    DBG printf("o2prop_new called\n"); DBG fflush(stdout);
    x->service = NULL;
    x->attribute = NULL;
    if (argc == 2) {
        if (argv[0].a_type == A_SYMBOL) {
            x->service = argv[0].a_w.w_symbol->s_name;
        } else {
            pd_error(x, "o2property expected symbol for service name");
        }        
        if (argv[1].a_type == A_SYMBOL) {
            x->attribute = argv[1].a_w.w_symbol->s_name;
        } else {
            pd_error(x, "o2property expected symbol for property name");
        }        
    } else if (argc != 0) {
        pd_error(x, "o2property expected 0 or 2 symbols");
    }
    x->x_outlet1 = outlet_new(&x->x_obj, &s_list);
    x->x_outlet2 = outlet_new(&x->x_obj, &s_bang);
    o2pd_post("o2prop_new");
    return (void *)x;
}


void o2prop_free(t_o2prop *x)
{
    DBG printf("o2prop_free called\n"); DBG fflush(stdout);
}


/* this is called once at setup time, when this code is loaded into Pd. */
PDLIBS_EXPORT void o2property_setup(void)
{
    o2pd_post("o2prop_setup");
    o2prop_class = class_new(gensym("o2property"), (t_newmethod)o2prop_new,
                             (t_method)o2prop_free, sizeof(t_o2prop), 0,
                             A_GIMME, 0);
    class_addmethod(o2prop_class, (t_method)o2prop_get,
                    gensym("get"), A_SYMBOL, A_SYMBOL, 0);
    class_addmethod(o2prop_class, (t_method)o2prop_put,
                    gensym("put"), A_GIMME, 0);
    class_addmethod(o2prop_class, (t_method)o2prop_search,
                    gensym("search"), A_SYMBOL, A_SYMBOL, 0);
    class_addbang(o2prop_class, o2prop_bang);
}

