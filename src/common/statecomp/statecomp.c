/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
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
}

static void initialize()
{
	init_symbol_table();
}

static void parse_args(int argc, char **argv)
{
	int c;
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
				list_flag = 1;
				break;

			case 'o' : /* set output file */
				out_file = fopen(optarg,"w");
				out_file_flag = 1;
				break;

			default:
				fprintf(stderr,"%s: undefined option %c given\n", argv[0], c);
		}
	}
	if (!out_file_flag)
	{
		/* open c.out as output file */
		out_file = fopen("st.tab.c","w");
		out_file_flag = 1;
	}
	if (list_flag)
	{
		/* open c.out as output file */
		list_file = fopen("c.lst","w");
		list_file_flag = 1;
	}
	/* check to make sure we can continue with current args */
	if (!out_file_flag)
	{
		fprintf(stderr, "%s: cannot continue with these arguments\n", argv[0]);
		exit(-1);
	}
}

static void finalize()
{
	fclose(out_file);
	if (list_flag)
	{
		fclose(list_file);
	}
}

void yyerror(s)
{
	fprintf(stderr,"syntax error line %d: %s\n", line, s);
}

void yywrap()
{
}

void produce_listing(int line, char *listing)
{
	/* fprintf(stderr, "produce_listing\n"); */
	if (list_flag)
		fprintf(list_file, "[%d]\t%s\n", line, listing);
}

