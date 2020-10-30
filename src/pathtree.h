//  pathtree.h - a tree of named branches for searching paths
//
// Roger Dannenberg
//
// April 2020

o2_err_t o2_method_new_internal(const char *path, const char *typespec,
        o2_method_handler h, const void *user_data, bool coerce, bool parse);

bool o2_find_handlers_rec(char *remaining, char *name,
        o2_node_ptr node, o2_msg_data_ptr msg, char *types);
