#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

/* for getopt_long */
#include <getopt.h>

#include "generic.h"

/* "stuff" will look something like "--mode 0 --path donkey".  it might look
 * like "--foo=true --bar=disable "
 */
 

/* this generic_param_parser could be the basis for any other parameter parser.
 * if one wishes to use the --foo blah --bar=yes style, 
 * . define a parameter struct and pass that into your param_parser
 * . change NR_ARGS to at least many arument tokens your string will have
 * . add the right long_opts for your function
 * . add the right handlers for each option
 */
 
/* returns -1 if chokes on parameter list given by "stuff", 
 * otherwise returns 0 and populates the generic_params structure ( gp )
 */
#define NR_ARGS 6  /* generic_params only has a few elements */
void *generic_param_parser( char * stuff) {
        generic_params *gp = malloc(sizeof(generic_params));
	char *args[NR_ARGS];  /* a simulated argv */
	int argc=1; /* skip over argv[0] when initializing our simulated argv */
	int c=0,  option_index=0, j;
	char * buf=NULL, *p=NULL;
	char  * delim = " \t\n";

	static struct option long_opts[] = {
		{ "mode", required_argument, NULL, 'm'},
		{"path", required_argument, NULL, 'p'},
		{0,0,0,0}
	};

	bzero(args, sizeof(args));
	optind=0; /* getopt_long has some statefullness. this helps multiple
		     uses of the "while ( (c = getopt_long())) { ..} " 
		     idiom work */

	if ( stuff == NULL ) {
		/* nothing provided by user, so initialize with defaults */
	} else  {
		/* tokenize the way the shell would tokenize */
		args[0]=strdup("generic_param_parser");
		buf = strdup(stuff);

		p = strtok(buf, delim);
		do {
			args[argc]=strdup(p);
			argc++;
		}while( (argc<NR_ARGS) && ((p = strtok(NULL, delim)) != NULL));
	}

	while ( (c = getopt_long(argc, args, "m:p:", 
					long_opts, &option_index)) != -1) {
		switch(c) {
			case 'm':
				gp->mode=strtoul(optarg,  NULL, 0);
				break;
			case 'p':
				strncpy(gp->path, optarg, PVFS_NAME_MAX);
				break;
			case ':':
				fprintf(stderr, "missing parameter to argument\n");
				return(NULL);
			case '?':
				fprintf(stderr, "format error\n");
				return(NULL);
			default:
				printf("getopt returned code 0%o\n", c);
				return(NULL);
		}
	}
	/* memroy management time */
	free(buf);
	for ( j = 0; j < argc; j++) {
		free ( args[j] );
	}

	return(gp);
}
	
#ifdef STANDALONE

int main(void) {
	generic_params my_gp;
	bzero(&my_gp, sizeof(my_gp));
	generic_param_parser("--mode 111  -p /a/a/a/a/a", &my_gp);
	printf ("mode: %d path: %s\n", my_gp.mode, my_gp.path);
	bzero(&my_gp, sizeof(my_gp));
	generic_param_parser("--mode 99 --path=/b/path/to/a/file/blah", &my_gp);
	printf ("mode: %d path: %s\n", my_gp.mode, my_gp.path);
	bzero(&my_gp, sizeof(my_gp));
	generic_param_parser("--mode=39 --path=/a/deat/to/a/file/blah", &my_gp);
	printf ("mode: %d path: %s\n", my_gp.mode, my_gp.path);
	bzero(&my_gp, sizeof(my_gp));
	generic_param_parser("-m 14 --path=/a/path/fish/heads/blah", &my_gp);
	printf ("mode: %d path: %s\n", my_gp.mode, my_gp.path);
	bzero(&my_gp, sizeof(my_gp));
	generic_param_parser("-m 60 -p /monkey/to/a/file/blah", &my_gp);
	printf ("mode: %d path: %s\n", my_gp.mode, my_gp.path);
	return 0;
}
#endif
