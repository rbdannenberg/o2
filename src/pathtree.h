//  pathtree.h - a tree of named branches for searching paths
//
// Roger Dannenberg
//
// April 2020



void o2_find_handlers_rec(char *remaining, char *name,
        o2_node_ptr node, o2_msg_data_ptr msg,
                          char *types);
