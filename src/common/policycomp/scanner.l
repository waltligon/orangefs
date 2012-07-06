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

#include "policycomp.h"
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

/* For the new and broken flex-2.5.33, it defines isatty() no
 * matter what,
 * but we can tell it not to include unistd.h that really defines
 * that.
 */
#define YY_NO_UNISTD_H 1

/*
 * flex-2.5.33 produces these non-static functions that are never
 * used
 * internally or externally by us.  Shut up gcc complaints by
 * pretending we
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

#define RETURNVAL(x)    yylval.i = x; return x;
static int comment_caller;

%}

id              [_a-zA-Z][_0-9a-zA-Z]*
int             [0-9][0-9]*

%x              CODE
%x              COMMENT

/* avoid a warning since we do not use yyunput() */
%option nounput

%%

[^%\n]*         {fprintf(code, "%s", yytext);}
"\n"            {fprintf(code, "%s", yytext); line++;}
"\%"            {fprintf(code, "%s", yytext);}
"\%\%"          {BEGIN(CODE);}

<CODE>"\%\%"    {fprintf(code, "#ifndef WIN32\n");
                 fprintf(code, "# %d \"%s\"\n", line,
in_file_name);
                 fprintf(code, "#endif\n");
                 BEGIN(0);}

<CODE>"attribute"  {RETURNVAL(ATTRIBUTE);}
<CODE>"policy"     {RETURNVAL(POLICY);}
<CODE>"for"        {RETURNVAL(FOR);}
<CODE>"sets"       {RETURNVAL(SETS);}
<CODE>"spread"     {RETURNVAL(SPREAD);}
<CODE>"others"     {RETURNVAL(OTHERS);}
<CODE>"select"     {RETURNVAL(SELECT);}
<CODE>"where"      {RETURNVAL(WHERE);}
<CODE>"\{"         {RETURNVAL(LBRACE);}
<CODE>"\}"         {RETURNVAL(RBRACE);}
<CODE>"\("         {RETURNVAL(LPAREN);}
<CODE>"\)"         {RETURNVAL(RPAREN);}
<CODE>"\;"         {RETURNVAL(SEMICOLON);}
<CODE>"\,"         {RETURNVAL(COMMA);}
<CODE>"\-"         {RETURNVAL(DASH);}
<CODE>"\&\&"       {RETURNVAL(AND);}
<CODE>"\|\|"       {RETURNVAL(OR);}
<CODE>"\=\="       {RETURNVAL(EQ);}
<CODE>"\!\="       {RETURNVAL(NE);}
<CODE>"\>"         {RETURNVAL(GT);}
<CODE>"\>\="       {RETURNVAL(GE);}
<CODE>"\<"         {RETURNVAL(LT);}
<CODE>"\<\="       {RETURNVAL(LE);}

<CODE>"/*"              {comment_caller = CODE; BEGIN(COMMENT);}
<COMMENT>[^*\n]*        /* eat anything that's not a '*' */
<COMMENT>"*"+[^*/\n]*   /* eat up '*'s not followed by '/'s */
<COMMENT>\n             line++;
<COMMENT>"*"+"/"        BEGIN(comment_caller);

<CODE>{id}      {yylval.c = yytext; return(IDENTIFIER);}
<CODE>{int}     {yylval.c = yytext; return(INTEGER);}

<CODE>"\n"      {line++;}
<CODE>[ \t]+    /* eat white space */
<*>.            {char s[256];
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

