#ifndef INCLUDE_GENERIC_H
#define INCLUDE_GENERIC_H
#include <sys/param.h>

typedef struct generic_params{
	int mode;
	char path[MAXPATHLEN];
} generic_params;


void *generic_param_parser( char * stuff);

#endif
