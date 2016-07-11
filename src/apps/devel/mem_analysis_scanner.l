
%{

/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup statecomp
 *
 *  Lexical analyzer for pint-malloc-anal devel tool.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mem_analysis.h"
#include "mem_analysis_parser.h"

/*
  we're never interactive, so avoid isatty call that sometimes
  fails due to occassional improper ioctl command mappings --
  these can cause an infinite loop in statecomp execution.
*/
#define YY_ALWAYS_INTERACTIVE 0
#define YY_NEVER_INTERACTIVE 1

/* No push/pop of scanner state. */
#define YY_STACK_USED 0

/* No main needed. */
#define YY_MAIN 0

/* For the new and broken flex-2.5.33, it defines isatty() no matter what,
 * but we can tell it not to include unistd.h that really defines that.
 */
#define YY_NO_UNISTD_H 1

/*
 * flex-2.5.33 produces these non-static functions that are never used
 * internally or externally by us.  Shut up gcc complaints by pretending we
 * have declared them.  Older flex-2.5.4 is not so dumb.
 */
int yyget_lineno(void);
FILE *yyget_in(void);
FILE *yyget_out(void);
char *yyget_text(void);
void yyset_lineno(int line_number);
void yyset_in(FILE *in_str );
void yyset_out(FILE *out_str);
int yyget_debug(void);
void yyset_debug(int bdebug);
int yylex_destroy(void);

/* flex-2.5.33 does generate a prototype, but 2.5.4 does not */
#if !defined(YY_FLEX_SUBMINOR_VERSION) || (YY_FLEX_SUBMINOR_VERSION < 33)
extern int yylex(void);
#endif

#define	RETURNVAL(x)	yylval.i = x; col += yyleng; return x;
static int comment_caller;

%}

hint        "0x"[0-9a-fA-F][0-9a-fA-F]*
dint        [-+]*[0-9][0-9]*
filename    ([.][/]+)*(src|include)[/]+([-_0-9a-zA-Z]+[/]+)*[-_0-9a-zA-Z]*[.](c|h|y|l|sm)
nil         [(]nil[)]
%x	        CODE
%x	        COMMENT

/* avoid a warning since we do not use yyunput() */
%option     nounput

%%

"init_glibc_malloc:running\n"   {BEGIN(CODE); line++; col = 1;}
[^%\n]*		{col += yyleng;   /* not in the CODE or COMMENT section */}
"\n"		{line++; col = 1; /* not in the CODE or COMMENT section */}
"\%"		{col += yyleng;   /* not in the CODE or COMMENT section */}

            /* "\%\%"		{BEGIN(CODE);} */

            /* <CODE>"\%\%"	{BEGIN(0);} */

<CODE>"line"            {RETURNVAL(LINE);}
<CODE>"addr" 	        {RETURNVAL(ADDR);}
<CODE>"realaddr"        {RETURNVAL(REALADDR);}
<CODE>"size"	        {RETURNVAL(SIZE);}
<CODE>"returning"       {RETURNVAL(RETURNING);}
<CODE>"align"	        {RETURNVAL(ALIGN);}
<CODE>"newaddr"	        {RETURNVAL(NEWADDR);}
<CODE>"returned"        {RETURNVAL(RETURNED);}
<CODE>"MALLOC"	        {RETURNVAL(MALLOC);}
<CODE>"MEMALIGN"        {RETURNVAL(MEMALIGN);}
<CODE>"REALLOC"	        {RETURNVAL(REALLOC);}
<CODE>"FREE"	        {RETURNVAL(FREE);}

<CODE>"/*"              {comment_caller = CODE; BEGIN(COMMENT); col += yyleng;}
<COMMENT>[^*\n]*        {col += yyleng; /* eat anything that's not a '*' */}
<COMMENT>"*"+[^*/\n]*   {col += yyleng; /* eat up '*'s not followed by '/'s */}
<COMMENT>\n             {line++; col = 1;}
<COMMENT>"*"+"/"        {BEGIN(comment_caller); col += yyleng;}

<CODE>{hint}   	        {yylval.i = strtol(yytext, NULL, 16);
                         col += yyleng; return(HINT);}
<CODE>{nil}   	        {yylval.i = 0; col += yyleng; return(HINT);}
<CODE>{dint}   	        {yylval.i = strtol(yytext, NULL, 10);
                         col += yyleng; return(DINT);}
<CODE>{filename}        {yylval.c = yytext; col += yyleng; return(FILENAME);}

<CODE>"\n"	        {yylval.i = EOL; line++; col = 1; return(EOL);}
<CODE>[ \t]+	        {col += yyleng; /* eat white space */}
<*>.		        {char s[256];
                         sprintf(s, "bad character '%s'", yytext); yyerror(s); }

%%

/* On EOF, no more files. */
int yywrap(void)
{
    return 1;
}

/*
 * Local variables:
 *  mode: c
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
