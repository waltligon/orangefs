%{

/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup statecomp
 *
 *  Lexical analyzer for statecomp source-to-source translator.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "statecomp.h"
#include "parser.h"

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
int yyget_leng(void);
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

#define	RETURNVAL(x)	yylval.i = x; return x;
static int comment_caller;

%}

id          [_a-zA-Z][_0-9a-zA-Z]*

%x		CODE
%x		COMMENT

/* avoid a warning since we do not use yyunput() */
%option nounput

%%

[^%\n]*		{fprintf(out_file, "%s", yytext);}
"\n"		{fprintf(out_file, "%s", yytext); line++;}
"\%"		{fprintf(out_file, "%s", yytext);}
"\%\%"		{BEGIN(CODE);}

<CODE>"\%\%"	{fprintf(out_file,"# %d \"%s\"\n", line, in_file_name);
                 BEGIN(0);}

<CODE>"machine"    {RETURNVAL(MACHINE);}
<CODE>"nested" 	   {RETURNVAL(NESTED);}
<CODE>"state"	   {RETURNVAL(STATE);}
<CODE>"run"	   {RETURNVAL(RUN);}
<CODE>"pjmp"       {RETURNVAL(PJMP);}
<CODE>"jump"	   {RETURNVAL(JUMP);}
<CODE>"return"	   {RETURNVAL(STATE_RETURN);}
<CODE>"terminate"  {RETURNVAL(STATE_TERMINATE);}
<CODE>"success"	   {RETURNVAL(SUCCESS);}
<CODE>"default"	   {RETURNVAL(DEFAULT);}
<CODE>"\{"	   {RETURNVAL(LBRACE);}
<CODE>"\}"	   {RETURNVAL(RBRACE);}
<CODE>"\;"	   {RETURNVAL(SEMICOLON);}
<CODE>"\=\>"	   {RETURNVAL(ARROW);}

<CODE>"/*"              {comment_caller = CODE; BEGIN(COMMENT);}
<COMMENT>[^*\n]*        /* eat anything that's not a '*' */
<COMMENT>"*"+[^*/\n]*   /* eat up '*'s not followed by '/'s */
<COMMENT>\n             line++;
<COMMENT>"*"+"/"        BEGIN(comment_caller);

<CODE>{id}     	{yylval.c = yytext; return(IDENTIFIER);}

<CODE>"\n"	{line++;}
<CODE>[ \t]+	/* eat white space */
<*>.		{char s[256];
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
