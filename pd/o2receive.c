/* o2receive - pd class for o2. */
/* Roger B. Dannenberg
 * July 2022
 */

#include <stdlib.h>
#include <string.h>
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


// See design.txt for an overview
// parameters in o2receive are:
// optional service followed by more atoms for notes, e.g.
//    the sequence synth osc freq denotes /synth/osc/freq
// optional flags before or after the address sequence:
//    -w: wait for bang before creating a message handler
//    -t typestring: limit messages to those with compatible
//         types, and coerce incoming types according to
//         typestring. You can receive:
//            ihfdtTfBIc as Pd float,
//            sS as Pd symbol

// if types is "any" change to NULL.
// if types is "none" change top empty string.
// (Pd cannot normally express "" or NULL directly as constants.)
void check_special_type_string(const char **types)
{
    if (streql(*types, "none")) {
        *types = "";
    } else if (streql(*types, "any")) {
        *types = NULL;
    }
}


// look for -t types or -w flags in args and process them
void o2rcv_check_types(t_o2rcv *x, int *argc, t_atom **argv, 
                       const char **types, bool *wait)
{
    while (*argc > 0 && (*argv)[0].a_type == A_SYMBOL) {
        const char *option = (*argv)->a_w.w_symbol->s_name;
        if (streql(option, "-t") && *argc > 1 &&
            (*argv)[1].a_type == A_SYMBOL) {
            *types = (*argv)[1].a_w.w_symbol->s_name;
            check_special_type_string(types);
            (*argc) -= 2;
            (*argv) += 2;
        } else if (streql(option, "-w")) {
            *wait = true;
            (*argc) -= 1;
            (*argv) += 1;
        } else if (option[0] == '-') {
            pd_error((t_object *) x,
                     "o2receive: unknown option %s ignored", option);
            (*argc) -= 1;
            (*argv) += 1;
        } else {
            return;  // no option found, argc, argv are unmodified
        }
    }
}


// called in response to initialization, address, and bang messages
// x->path is NULL if this is initialization; x->address is NULL
// if x is disabled (not receiving). Otherwise, assume x is receiving
// and remove address before installing the new one.
//
static bool set_address_from_args(t_o2rcv *x,  t_symbol *s,
                                  int argc, t_atom *argv)
{
    DBG2 printf("set_address_from_args called, x->path %p\n", x->path);
    const char *types = NULL;  // will be replaced with types if found
    char path[128];
    int last = 0;  // index of EOS
    path[0] = 0;
    bool wait = false;

    // check for -t <types> before or after path nodes
    o2rcv_check_types(x, &argc, &argv, &types, &wait);

    while (argc && argv->a_type == A_SYMBOL) {  /* get next node */
        const char *nodename = argv->a_w.w_symbol->s_name;
        DBG2 printf("set_address_from_args in loop got %s\n", nodename);
        if (nodename[0] == '-') {  // address terminated by option
            break;
        }
        int nodelen = (int) strlen(nodename);
        if (last + nodelen + 2 >= 128) {
            pd_error(x, "O2 address is too long");
            return false;
        }
        path[last++] = '/';
        strcpy(path + last, nodename);
        last += nodelen;
        argc--; argv++;
    }

    DBG2 printf("set_address_from_args final call to check_types\n");
    o2rcv_check_types(x, &argc, &argv, &types, &wait);
    DBG2 printf("set_address_from_args check_types returned, argc %d\n", argc);
    
    if (argc) {  /* should be done by now */
        pd_error(x, "o2receive: %d extra parameters ignored", argc);
        DBG if (argv && argv->a_type == A_SYMBOL) {
                printf("  first extra parameter is %s\n",
                       argv->a_w.w_symbol->s_name);
            }
    }
    DBG2 printf("set_address_from_args install path %s, was %p\n", path, x->path);
    DBG2 fflush(stdout);

    // install path and types on x:
    if (path[0]) {  // not empty string
        if (x->path) {
            FREE_STRING(x->path);
        }
        DBG2 printf("set_address_from_args call heapify %s\n", path);
        x->path = o2pd_heapify(path);
    }
    x->types = types;

    printf("set_address_from_args: path |%s|, types |%s|, address %p, wait %d\n",
           path, types, x->address, wait);
    fflush(stdout);
    return wait;
}


void o2rcv_address(t_o2rcv *x,  t_symbol *s, int argc, t_atom *argv)
{
    o2pd_post("o2rcv: address");
    DBG2 printf("o2rcv: address, x->path %p\n", x->path);

    show_receivers("before o2rcv_address");
    set_address_from_args(x, s, argc, argv);  // -w flag is ignored
    DBG2 printf("o2rcv_address after set: x->path %s x->address %p "
                "x->address->path %s\n",
                x->path, x->address, x->address ? x->address->path : NULL);
    update_receive_address(x);
    show_receivers("after o2rcv_address"); fflush(stdout);
}


void o2rcv_bang(t_o2rcv *x)
{
    o2pd_post("o2rcv: bang");
    show_receivers("before o2rcv_bang");
    update_receive_address(x);
    show_receivers("after o2rcv_address");
}


void o2rcv_types(t_o2rcv *x, t_symbol *types)
{
    o2pd_post("o2rcv: types %s", types->s_name);
    const char *typestr = (char *) (types->s_name);
    check_special_type_string(&typestr);
    DBG2 printf("o2rcv_types types after check %s\n", typestr);
    if (typestr != NULL) {
        for (int i = 0; typestr[i]; i++) {
            if (strchr("ifhdtsSc", typestr[i]) == NULL) {
                pd_error(x, "o2receive: types string %s has invalid "
                         "character %c", typestr, typestr[i]);
                typestr = NULL;
                break;
            }
        }
    }
    DBG2 printf("o2rcv_types types %s\n", typestr);
    addressnode *a = x->address;
    if (!a) {
        pd_error(x, "o2receive: setting types, but there is no address yet");
        return;
    }
    DBG2 printf("o2rcv_types setting %p types to %s\n", x, typestr);
    x->types = typestr;
    show_receivers("in types before update_receive_address");
    update_receive_address(x);
    show_receivers("in types after update_receive_address");
}


void o2rcv_disable(t_o2rcv *x,  t_symbol *s, int argc, t_atom *argv)
{
    DBG2 printf("o2rcv_disable at start, x->path %p\n", x->path);
    o2pd_post("o2rcv: disable");
    if (o2_ensemble_name == NULL) {
        pd_error(x, "O2 is not initialized");
    } else {
        remove_o2receive(x);
    }
    DBG show_receivers("after disable");
    DBG2 printf("o2rcv_disable at end, x->path %p\n", x->path);
}


/* this is a pointer to the class for "o2rcv", which is created in the
   "setup" routine below and used to create new ones in the "new" routine. */
t_class *o2rcv_class;

/* this is called when a new "o2rcv" object is created. */
void *o2rcv_new(t_symbol *s, int argc, t_atom *argv)
{
    t_o2rcv *x = (t_o2rcv *)pd_new(o2rcv_class);
    printf("o2rcv_new called\n"); fflush(stdout);
    x->path = NULL;
    x->types = NULL;
    x->next = NULL;
    x->address = NULL;
    if (argc > 0) {  // get service
        printf("o2rcv_new calls set_address_from_args\n"); fflush(stdout);
        bool wait = set_address_from_args(x, s, argc, argv);
        if (x->path && !wait) {
            update_receive_address(x);
        } else {
            DBG printf("o2rcv_address got wait option, so not receiving yet\n");
        }
    }
    outlet_new(&x->x_obj, &s_list);
    printf("new o2receive object %p outlet %p\n", x, x->x_obj.ob_outlet);
    fflush(stdout);
    o2pd_post("o2rcv_new");
    return (void *)x;
}


void o2rcv_free(t_o2rcv *x)
{
    printf("o2rcv_free called\n"); fflush(stdout);
    remove_o2receive(x);
}


/* this is called once at setup time, when this code is loaded into Pd. */
PDLIBS_EXPORT void o2receive_setup(void)
{
    o2pd_post("o2rcv_setup");
    o2rcv_class = class_new(gensym("o2receive"), (t_newmethod)o2rcv_new,
                            (t_method)o2rcv_free, sizeof(t_o2rcv), 0,
                            A_GIMME, 0);
    class_addmethod(o2rcv_class, (t_method)o2rcv_address,
                                         gensym("address"), A_GIMME, 0);
    class_addmethod(o2rcv_class, (t_method)o2rcv_types,
                                         gensym("types"), A_SYMBOL, 0);
    class_addmethod(o2rcv_class, (t_method)o2rcv_disable,
                                         gensym("disable"), 0);
    class_addbang(o2rcv_class, o2rcv_bang);
}

