

%{

/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup pint-malloc-anal
 *
 *  Parser for pint-malloc-anal tool.
 */

#include <stdlib.h>
#include <stdio.h>

#include "mem_analysis.h"

#ifdef WIN32
#define _STDLIB_H  /* mark stdlib.h included */
#endif

/* We never use this, disable default. */
#define YY_LOCATION_PRINT 0

/* No NLS. */
#define YYENABLE_NLS 0

static struct entry *init_entry(struct clause aclause);
static struct entry *add_entry(struct entry *aclause, struct entry *anentry);
static struct entry *process_entry(char *path,
                                   int line,
                                   int op,
                                   struct entry *anentry);

/* in scanner.l generated code */
extern int yylex(void);

/*
 * Local variables:
 *    c-indent-level: 4
 *    c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */


%}

%union {
    int i;
    char *c;
    struct clause cl;
    struct entry *e;
};

%token <i> LINE
%token <i> ADDR
%token <i> REALADDR
%token <i> SIZE
%token <i> RETURNING
%token <i> ALIGN
%token <i> NEWADDR
%token <i> RETURNED
%token <i> MALLOC
%token <i> MEMALIGN
%token <i> REALLOC
%token <i> FREE
%token <i> EOL
%token <i> HINT
%token <i> DINT
%token <c> FILENAME

%type <cl> addr_clause realaddr_clause size_clause
           returning_clause align_clause newaddr_clause returned_clause

%type <e> clause clause_list mem_trace_entry mem_trace mem_trace_file

%type <i>  op line

%start mem_trace_file

%%

mem_trace_file : mem_trace
               | /* NULL */ {$$ = NULL;}
               ;

mem_trace : mem_trace_entry
	      | mem_trace_entry mem_trace
	      ;

mem_trace_entry : FILENAME line op clause_list EOL
		    {
			$$ = process_entry($1, $2, $3, $4);
		    }
            ;

line : LINE DINT {$$ = $2;}
     ;

op : MALLOC
   | MEMALIGN
   | REALLOC
   | FREE
   ;

clause_list : clause 
            | clause clause_list {$$ = add_entry($1, $2);}
            ;

clause : addr_clause {$$ = init_entry($1);}
       | realaddr_clause {$$ = init_entry($1);}
       | size_clause {$$ = init_entry($1);}
       | returning_clause {$$ = init_entry($1);}
       | align_clause {$$ = init_entry($1);}
       | newaddr_clause {$$ = init_entry($1);}
       | returned_clause {$$ = init_entry($1);}
       ;

addr_clause : ADDR HINT {$$.type = $1; $$.value = $2;}
            ;

realaddr_clause : REALADDR HINT {$$.type = $1; $$.value = $2;}
                ;

size_clause : SIZE DINT {$$.type = $1; $$.value = $2;}
            ;

returning_clause : RETURNING HINT {$$.type = $1; $$.value = $2;}
                 ;

align_clause : ALIGN DINT {$$.type = $1; $$.value = $2;}
             ;

newaddr_clause : NEWADDR HINT {$$.type = $1; $$.value = $2;}
                              /* part of realloc */
               ;

returned_clause : RETURNED HINT {$$.type = $1; $$.value = $2;}
                                /* part of realloc */
                ;

%%

static struct entry *init_entry(struct clause aclause)
{
}

static struct entry *add_entry(struct entry *aclause, struct entry *anentry)
{
}

static struct entry *process_entry(char *path,
                                   int line,
                                   int op,
                                   struct entry *anentry)
{
}

