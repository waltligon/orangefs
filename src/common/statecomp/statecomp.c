/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <symbol.h>
#include <unistd.h>

#ifdef __GNUC__
#ifdef YYPARSE_PARAM
int yyparse (void *);
#else
int yyparse (void);
#endif
#endif


static void initialize(void);
static void parse_args(int argc, char **argv);
static void finalize(void);
void gen_init(void);
void yywrap(void);
void yyerror(char *s);

/*
 * Global Variables
 */

int list_flag = 0;
FILE *list_file;
int list_file_flag = 0;
int out_file_flag = 0;
FILE *out_file;
int line = 1;

int main(int argc, char **argv)
{
	initialize();
	parse_args(argc, argv);
	gen_init();
	yyparse();
	finalize();
	return 0;
}

static void initialize()
{
	init_symbol_table();
}

static void parse_args(int argc, char **argv)
{
	int c;
	int file_name_size;
	char *file_name;
	//int digit_optind = 0;
	while(1)
	{
		//int this_option_optind = optind ? optind : 1;
		int option_index = 0;
		static struct option long_options[] = {
			/* {"option_name", has_arg, *flag, val}, */
			{"option_name", 0, NULL, 0},
			{0, 0, 0, 0}
		};
		c = getopt_long ( argc, argv, "lo:", long_options, &option_index);
		if (c == -1)
			break;
		switch (c) {
			case 0 : /* a long option */
				break;

			case 'l' : /* turn on listings */
				list_file = fopen("c.lst","w");
				list_flag = 1;
				break;

			default:
				fprintf(stderr,"%s: undefined option %c given\n", argv[0], c);
		}
	}
	/* first non-option argument should be input file with .sm suffix */
	if (optind == argc)
	{
		/* input file name missing */
		fprintf(stderr, "usage: %s [-l] input_file.sm\n", argv[0]);
		exit(-1);
	}
	/* we have an input file argument */
	file_name_size = strlen(argv[optind]);
	file_name = malloc(file_name_size);
	strcpy (file_name, argv[optind]);
	/* let's see if it has the suffix */
	if (strcmp(&file_name[file_name_size-3], ".sm"))
	{
		/* input file name has wrong suffix */
		fprintf(stderr, "usage: %s [-l] input_file.sm\n", argv[0]);
		exit(-1);
	}
	/* open input file as stdin */
	if (!freopen(file_name, "r", stdin))
	{
		/* error opening input file */
		perror("opening input file");
		exit(-1);
	}
	/* construct output file name */
	file_name[file_name_size-2] = 'c';
	file_name[file_name_size-1] = 0;
	/* open output file */
	if (!(out_file = fopen(file_name, "w")))
	{
		/* error opening output file */
		perror("opening output file");
		exit(-1);
	}
	/* check for any extra arguments */
	if (argc > optind+1)
	{
		/* report we are ignoring them */
		fprintf(stderr, "usage: %s [-l] input_file.sm\n", argv[0]);
		fprintf(stderr, "ignoring extra arguments\n");
	}
}

static void finalize(void)
{
	fclose(out_file);
	if (list_flag)
	{
		fclose(list_file);
	}
}

void yyerror(char *s)
{
	fprintf(stderr,"syntax error line %d: %s\n", line, s);
}

void yywrap(void)
{
}

void produce_listing(int line, char *listing)
{
	/* fprintf(stderr, "produce_listing\n"); */
	if (list_flag)
		fprintf(list_file, "[%d]\t%s\n", line, listing);
}

