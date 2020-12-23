// msgprint.c -- print function for messages
//
// Roger B. Dannenberg
// July 2020
//
// This is mostly for debugging. Compilation is enabled by either
// *not* defining O2_NO_DEBUG, or defining O2_MSGPRINT

#include "o2internal.h"
#include "message.h"

#ifdef O2_MSGPRINT

// o2_msg_data_print - print message as text to stdout
//
// It would be most convenient to use o2_extract_start() and o2_get_next()
// here, but this would overwrite extracted parameters if called from a
// message handler, so here we duplicate some code to pull parameters from
// messages (although the code is simple since there's no coercion).
//
void o2_msg_data_print(o2_msg_data_ptr msg)
{
    int i;
    printf("%s @ %g", msg->address, msg->timestamp);
    printf(" by %s", msg->flags & O2_TCP_FLAG ? "TCP" : "UDP");
    if (msg->timestamp > 0.0) {
        if (msg->timestamp > o2_global_now) {
            printf(" (now+%gs)", msg->timestamp - o2_global_now);
        } else {
            printf(" (%gs late)", o2_global_now - msg->timestamp);
        }
    }
#ifndef O2_NO_BUNDLES
    if (IS_BUNDLE(msg)) {
        FOR_EACH_EMBEDDED(msg, printf(" <ELEM ");
                          o2_msg_data_print(embedded);
                          printf(" >");
                          len = embedded->length)
        return;
    }
#endif
    const char *types = o2_msg_data_types(msg);
    const char *data_next = o2_msg_data_params(types);
    
    while (*types) {
        switch (*types) {
            case O2_INT32:
                printf(" %d", *((int32_t *) data_next));
                data_next += sizeof(int32_t);
                break;
            case O2_FLOAT:
                printf(" %gf", *((float *) data_next));
                data_next += sizeof(float);
                break;
            case O2_STRING:
                printf(" \"%s\"", data_next);
                data_next += o2_strsize(data_next);
                break;
            case O2_BLOB: {
                int size = *((int32_t *) data_next);
                data_next += sizeof(int32_t);
                if (size > 12) {
                    printf(" (%d byte blob)", size);
                } else {
                    printf(" (");
                    for (i = 0; i < size; i++) {
                        if (i > 0) printf(" ");
                        printf("%#02x", (unsigned char) (data_next[i]));
                    }
                    printf(")");
                }
                data_next += ((size + 3) & ~3);
                break;
            }
            case O2_INT64:
                // note: gcc complained about %lld with int64_t:
                printf(" %lld", *((long long *) data_next));
                data_next += sizeof(int64_t);
                break;
            case O2_DOUBLE:
                printf(" %g", *((double *) data_next));
                data_next += sizeof(double);
                break;
            case O2_TIME:
                printf(" %gs", *((double *) data_next));
                data_next += sizeof(double);
                break;
            case O2_SYMBOL:
                printf(" '%s", data_next);
                data_next += o2_strsize(data_next);
                break;
            case O2_CHAR:
                printf(" '%c'", *((int32_t *) data_next));
                data_next += sizeof(int32_t);
                break;
            case O2_MIDI:
                printf(" <MIDI: ");
                for (i = 0; i < 4; i++) {
                    if (i > 0) printf(" "); 
                    printf("0x%02x", data_next[i]);
                }
                printf(">");
                data_next += 4;
                break;
            case O2_BOOL:
                printf(" %s", (*(int32_t *) data_next) ?
                             "Bool:true" : "Bool:false");
                data_next += sizeof(int32_t);
                break;
            case O2_TRUE:
                printf(" #T");
                break;
            case O2_FALSE:
                printf(" #F");
                break;
            case O2_NIL:
                printf(" Nil");
                break;
            case O2_INFINITUM:
                printf(" Infinitum");
                break;
            case O2_ARRAY_START:
                printf(" [");
                break;
            case O2_ARRAY_END:
                printf(" ]");
                break;
            case O2_VECTOR: {
                int len = *((int32_t *) data_next);
                data_next += sizeof(int32_t);
                printf(" <");
                O2type vtype = (O2type) (*types++);
                for (i = 0; i < len; i++) {
                    if (i > 0) printf(" ");
                    if (vtype == O2_INT32) {
                        printf(" %d", *((int32_t *) data_next));
                        data_next += sizeof(int32_t);
                    } else if (vtype == O2_INT64) {
                        // note: gcc complains about %lld and int64_t
                        printf(" %lld", *((long long *) data_next));
                        data_next += sizeof(int64_t);
                    } else if (vtype == O2_FLOAT) {
                        printf(" %gf", *((float *) data_next));
                        data_next += sizeof(float);
                    } else if (vtype == O2_DOUBLE) {
                        printf(" %g", *((double *) data_next));
                        data_next += sizeof(double);
                    }
                    // note: vector of O2_TIME is not valid
                }
                break;
            }
            default:
                printf(" O2 WARNING: unhandled type: %c\n", *types);
                break;
        }
        types++;
    }
}


void O2message_print(O2message_ptr msg)
{
    o2_msg_data_print(&msg->data);
}
#endif
