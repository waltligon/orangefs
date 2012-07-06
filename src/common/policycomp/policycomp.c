/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/** \defgroup policycomp policycomp source-to-source translator
 *
 *  policycomp is a source-to-source translator.  It takes
 *  policy descriptions (.policy extension) as input and creates a
 *  corresponding C source file for compilation.  Both clients
 *  and servers rely on these policies for processing.  This
 *  executable is built at compile time and used only during
 *  compilation of PVFS2.
 *
 * @{
 */

/** \file
 *
 *  Core of policy source-to-source translator executable,
 *  policycomp, including processing arguments, calling the
 *  parser, and producing warning and error messages.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef WIN32
#include <unistd.h>
#endif

#include "policycomp.h"

#ifdef __GNUC__
#ifdef YYPARSE_PARAM
int yyparse (void *);
#else
int yyparse (void);
#endif
#endif

#ifdef WIN32
#define __func__    __FUNCTION__
#define unlink      _unlink

extern int yyparse();
#endif

extern int yydebug; /* for debugging the parser */

/*
 * Global Variables
 */
struct state *states = NULL;
int line = 1;
FILE *header;
FILE *code;
FILE *code2;
const char *in_file_name;

static const char *progname;
static char *header_name;
static char *code_name;
static char *code2_name;

static void parse_args(int argc, char **argv);
static void finalize(void);

int main(int argc, char **argv)
{
    int retval;
    yydebug = 0; /* set to 1 to debug */
    parse_args(argc, argv);
    retval = yyparse();
    switch (retval)
    {
        case 0:
            /* successful parse */
            break;
        case 1:
            /* syntax error */
            fprintf(stderr,"yyparse returned syntax error\n");
            break;
        case 2:
            /* out of memory error */
            fprintf(stderr,"yyparse returned out of memory error\n");
            break;
        default:
            /* unknown error */
            fprintf(stderr,"yyparse returned unknown error\n");
            break;
    }
    finalize();
    return retval;
}

static void usage(void)
{
    fprintf(stderr, "Usage: %s [-l] input_file.policy [output_file.c]\n", progname);
    exit(1);
}

static void parse_args(int argc, char **argv)
{
    int len;
    const char *cp;

    for (cp=progname=argv[0]; *cp; cp++)
    {
        if (*cp == '/')
        {
            progname = cp+1;
        }
    }

    if (argc < 2 || argc > 3)
    {
        usage();
    }

    in_file_name = argv[1];
    code_name = argc == 3 ? argv[2] : NULL;

    len = strlen(in_file_name);
    if (len <= 7 || strcmp(&in_file_name[len-7], ".policy") != 0)
    {
        usage();
    }

    if (!freopen(in_file_name, "r", stdin))
    {
        perror("open input file");
        exit(1);
    }

    if (!code_name)
    {
        /* construct output file name from input file name */
        code_name = estrdup(in_file_name);
        code_name[len-6] = 'c';
        code_name[len-5] = 0;

        /* construct 2nd output file name from input file name */
        code2_name = estrdup(in_file_name);
        code2_name[len-6] = '2';
        code2_name[len-5] = '.';
        code2_name[len-4] = 'c';
        code2_name[len-3] = 0;
    }

    /* construct header file name from code file name */
    header_name = estrdup(code_name);
    len = strlen(header_name);
    header_name[len-1] = 'h';
    header_name[len] = 0;

    /* open code file */
    code = fopen(code_name, "w");
    if (!code)
    {
        perror("opening code file");
        exit(1);
    }

    /* dump header comment and bp into code file */
    fprintf(code, "/* WARNING: THIS FILE IS AUTOMATICALLY "
            "GENERATED FROM A .POLICY FILE.\n");
    fprintf(code, " * Changes made here will certainly "
            "be overwritten.\n");
    fprintf(code, " */\n\n");
    fprintf(code, "#include <policyeval.h>\n");
    fprintf(code, "#include <%s>\n", header_name);
    fprintf(code, "#include <db.h>\n");
    fprintf(code, "\n");

    /* open 2nd code file */
    code2 = fopen(code2_name, "w");
    if (!code2)
    {
        perror("opening code2 file");
        exit(1);
    }

    /* dump header comment and bp into 2nd code file */
    fprintf(code2, "/* WARNING: THIS FILE IS AUTOMATICALLY "
            "GENERATED FROM A .POLICY FILE.\n");
    fprintf(code2, " * Changes made here will certainly "
            "be overwritten.\n");
    fprintf(code2, " */\n\n");
    fprintf(code2, "#include <policyeval.h>\n");
    fprintf(code2, "#include <%s>\n", header_name);
    fprintf(code2, "#include <db.h>\n");
    fprintf(code2, "\n");

    /* open header file */
    header = fopen(header_name, "w");
    if (!header)
    {
        perror("opening header file");
        exit(1);
    }

    /* dump header comment into header file */
    fprintf(header, "/* WARNING: THIS FILE IS AUTOMATICALLY "
            "GENERATED FROM A .POLICY FILE.\n");
    fprintf(header, " * Changes made here will certainly "
            "be overwritten.\n");
    fprintf(header, " */\n\n");
    fprintf(header, "#ifndef POLICY_DEF_H\n");
    fprintf(header, "#define POLICY_DEF_H 1\n");
    fprintf(header, "\n");
}

static void finalize(void)
{
    fclose(code);
    fprintf(header, "#endif\n");
    fclose(header);
}

void yyerror(char *s)
{
    fprintf(stderr, "%s: %s:%d: %s\n", progname, in_file_name,
            line, s);
    if (!yydebug)
    {
        unlink(code_name);
        unlink(header_name); 
    }
    exit(1);
}

/*
 * Error checking malloc.
 */
void *emalloc(size_t size)
{
    void *p;

    p = malloc(size);
    if (!p) {
        fprintf(stderr, "%s: no more dynamic storage - aborting\n",
                progname);
        exit(1);
    }
    return p;
}

char *estrdup(const char *oldstring)
{
    char *s;

    s = emalloc(strlen(oldstring)+1);
    strcpy(s, oldstring);
    return s;
}

/* @} */
    
/*  
 * Local variables: 
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *  
 * vim: ts=8 sts=4 sw=4 expandtab
 */
