#ifndef O2ENSEMBLE_H
#define O2ENSENBLE_H
#include "ext.h"
#include "ext_obex.h"
#include "o2.h"

typedef struct _o2receive{
	t_object x_obj;
	const char *path;
	const char *types;//max holds this as symbol
	struct _addressnode *address;
	struct _o2receive *next;
}t_o2receive;

typedef struct _addressnode{
	const char *path;
	// const char *types;//max holds this as symbol
	t_o2receive *receivers;
	struct _addressnode *next;
}addressnode;
#endif