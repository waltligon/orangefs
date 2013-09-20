#ifndef INCLUDE_GENERIC_H
#define INCLUDE_GENERIC_H
#include <sys/param.h>

#include "pvfs2-types.h"

typedef struct generic_params{
	int mode;
	char path[PVFS_NAME_MAX];
} generic_params;


void *generic_param_parser( char * stuff);

#endif
