%{

/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "stdio.h"
#include "stdlib.h"
#include "symbol.h"
#include "parser.h"
#include "string.h"

extern YYSTYPE yylval;

extern FILE *out_file;
void produce_listing(int line, char *listing);

/* int      atoi();
 * double   atof();
 */

#define	LIST				strcat(text,yytext);
#define	RETURNVAL(x)	yylval.i = (int)(token = x); return((int)x);
#define	MAKELIST			produce_listing(line,text);text[0]=0;
char     text[132];
int      token;

%}

string      ["][^"]*["]
float1      [-+]?[0-9]+"."[0-9]*([eE][-+]?[0-9]+)?
float2      [-+]?"."[0-9]+([eE][-+]?[0-9]+)?
float3      [-+]?[0-9]+[eE][-+]?[0-9]+
int      [-+][0-9]+
id    [_a-zA-Z][_0-9a-zA-Z]*
w     [ \t\n]*

%x				CODE

%%

[^%\n]*				{fprintf(out_file,"%s", yytext);}
"\n"					{fprintf(out_file,"%s", yytext); line++;}
"\%"					{fprintf(out_file,"%s", yytext);}
"\%\%"				{BEGIN(CODE);}

<CODE>"\%\%"		{BEGIN(0);}

<CODE>"machine" 	{LIST; RETURNVAL(MACHINE);}
<CODE>"state"	 	{LIST; RETURNVAL(STATE);}
<CODE>"run"			{LIST; RETURNVAL(RUN);}
<CODE>"success"	{LIST; RETURNVAL(SUCCESS);}
<CODE>"default"	{LIST; RETURNVAL(DEFAULT);}
<CODE>"\{"			{LIST; RETURNVAL(LBRACE);}
<CODE>"\}"			{LIST; RETURNVAL(RBRACE);}
<CODE>"\("			{LIST; RETURNVAL(LPAREN);}
<CODE>"\)"			{LIST; RETURNVAL(RPAREN);}
<CODE>"\,"		   {LIST; RETURNVAL(COMMA);}
<CODE>"\;"			{LIST; RETURNVAL(SEMICOLON);}
<CODE>"\=\>"		{LIST; RETURNVAL(ARROW);}

<CODE>{id}     	{LIST; yylval.c = yytext; return(IDENTIFIER);}
<CODE>{int}    	{LIST; yylval.c = yytext; return(INTEGER);}

<CODE>"\n"			{MAKELIST; line++;}
<CODE>[ \t]+		{LIST; /* eat white space */ }
<*>.					{LIST; fprintf(stderr,"scanner error bad char %s\n",yytext);}

%%
