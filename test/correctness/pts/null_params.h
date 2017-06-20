#ifndef INCLUDE_NULL_PARAMS_H
#define INCLUDE_NULL_PARAMS_H
#include <sys/param.h>

typedef struct null_params_t{
	int p1;
	int p2;
} null_params;

void *null_params_parser(char * stuff);

#endif
