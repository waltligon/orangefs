#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#include "null_params.h"

void *null_params_parser(char *paramstr){
	null_params *myparams = malloc(sizeof(null_params));

	sscanf(paramstr,"%d %d\n",&myparams->p1,&myparams->p2);

	return myparams;
}
