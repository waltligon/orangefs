%{

/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "statecomp-symbol.h"
#include "parser.h"

/* avoid a warning since we do not use yyunput() */
#define YY_NO_UNPUT

/*
  we're never interactive, so avoid isatty call that sometimes
  fails due to occassional improper ioctl command mappings --
  these can cause an infinite loop in statecomp execution.
*/
#define YY_NEVER_INTERACTIVE 1

extern YYSTYPE yylval;

extern FILE *out_file;
extern char *in_file_name;
void produce_listing(int line, char *listing);

/* int      atoi();
 * double   atof();
 */

#define	LIST		strcat(text,yytext);
#define	RETURNVAL(x)	yylval.i = (int)(token = x); return((int)x);
#define	MAKELIST	produce_listing(line,text);text[0]=0;
char     text[132];
int      token;
int comment_caller;

%}

string      ["][^"]*["]
float1      [-+]?[0-9]+"."[0-9]*([eE][-+]?[0-9]+)?
float2      [-+]?"."[0-9]+([eE][-+]?[0-9]+)?
float3      [-+]?[0-9]+[eE][-+]?[0-9]+
int         [-+][0-9]+
id          [_a-zA-Z][_0-9a-zA-Z]*
w           [ \t\n]*

%x		CODE
%x		COMMENT 

%%

[^%\n]*		{fprintf(out_file,"%s", yytext);}
"\n"		{fprintf(out_file,"%s", yytext); line++;}
"\%"		{fprintf(out_file,"%s", yytext);}
"\%\%"		{BEGIN(CODE);}

<CODE>"\%\%"	{fprintf(out_file,"# %d \"%s\"\n", line, in_file_name); BEGIN(0);}

<CODE>"machine"    {LIST; RETURNVAL(MACHINE);}
<CODE>"nested" 	   {LIST; RETURNVAL(NESTED);}
<CODE>"initialize" {LIST; RETURNVAL(INIT);}
<CODE>"state"	   {LIST; RETURNVAL(STATE);}
<CODE>"extern"	   {LIST; RETURNVAL(EXTERN);}
<CODE>"return"	   {LIST; RETURNVAL(STATE_RETURN);}
<CODE>"run"	   {LIST; RETURNVAL(RUN);}
<CODE>"jump"	   {LIST; RETURNVAL(JUMP);}
<CODE>"success"	   {LIST; RETURNVAL(SUCCESS);}
<CODE>"default"	   {LIST; RETURNVAL(DEFAULT);}
<CODE>"terminate"  {LIST; RETURNVAL(STATE_TERMINATE);}
<CODE>"\{"	   {LIST; RETURNVAL(LBRACE);}
<CODE>"\}"	   {LIST; RETURNVAL(RBRACE);}
<CODE>"\("	   {LIST; RETURNVAL(LPAREN);}
<CODE>"\)"	   {LIST; RETURNVAL(RPAREN);}
<CODE>"\,"	   {LIST; RETURNVAL(COMMA);}
<CODE>"\;"	   {LIST; RETURNVAL(SEMICOLON);}
<CODE>"\=\>"	   {LIST; RETURNVAL(ARROW);}

<CODE>"/*"         {comment_caller = CODE; BEGIN(COMMENT);}
<COMMENT>[^*\n]*        /* eat anything that's not a '*' */
<COMMENT>"*"+[^*/\n]*   /* eat up '*'s not followed by '/'s */
<COMMENT>\n             line++;
<COMMENT>"*"+"/"        BEGIN(comment_caller);


<CODE>{id}     	{LIST; yylval.c = yytext; return(IDENTIFIER);}
<CODE>{int}    	{LIST; yylval.c = yytext; return(INTEGER);}

<CODE>"\n"	{MAKELIST; line++;}
<CODE>[ \t]+	{LIST; /* eat white space */ }
<*>.		{LIST; fprintf(stderr,"scanner error bad char %s\n",yytext);}

%%

/*
 * Local variables:
 *  mode: c
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
