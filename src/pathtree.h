//  pathtree.h - a tree of named branches for searching paths
//
// Roger Dannenberg
//
// April 2020

O2err o2_method_new_internal(const char *path, const char *typespec,
        O2method_handler h, const void *user_data, bool coerce, bool parse);

bool o2_find_handlers_rec(char *remaining, char *name,
        O2node *node, o2_msg_data_ptr msg, const char *types);
