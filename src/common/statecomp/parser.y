
%{

/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>

#include "symbol.h"

char *enter_string(char *);
void gen_init(void);
void gen_state_decl(char *state_name);
void gen_state_array(char *machine_name, char *first_state_name);
void gen_state_start(char *state_name);
void gen_state_run(char *run_func);
void gen_return_code(char *return_code);
void gen_new_state(char *new_state);
void gen_state_end(void);

%}

%union{
	int i;
	double f;
	char *c;
	sym_ent *s;
}

%token <i> MACHINE
%token <i> STATE
%token <i> RUN
%token <i> SUCCESS
%token <c> DEFAULT
%token <i> LBRACE
%token <i> RBRACE
%token <i> LPAREN
%token <i> RPAREN
%token <i> COMMA
%token <i> SEMICOLON
%token <i> ARROW
%token <c> IDENTIFIER
%token <c> INTEGER

%type <i> state_machine, .state_body., state_body

%type <f>

%type <c> identifier, return_code

%type <s> state_decl_list, .state_decl_list., state_decl,
			 state_def, state_def_list, .state_def_list.,
			 transition, transition_list

%start state_machine

%%

state_machine		: MACHINE identifier LPAREN .state_decl_list. RPAREN LBRACE
							{gen_state_array($2, $4->name);}
						  .state_def_list. RBRACE
						;

.state_decl_list.	: /* empty */ {$$ = NULL;}
						| state_decl_list
						;

state_decl_list	: state_decl
						| state_decl COMMA state_decl_list
						;

state_decl			: identifier
							{$$ = symenter($1);
							 $$->type = TYPE_STATE;
							 gen_state_decl($1);}
						;

.state_def_list.	: /* empty */ {$$ = NULL;}
						| state_def_list
						;

state_def_list		: state_def
						| state_def state_def_list
						;

state_def			: STATE identifier LBRACE
							{$$ = symlook($2);
							 gen_state_start($2);}
						  .state_body. RBRACE
						 	{gen_state_end();}
						;

.state_body.		: /* empty */ {$$ = 0;}
						| state_body
						;

state_body			: RUN identifier SEMICOLON
							{gen_state_run($2);}
						  transition_list
						;

transition_list	: transition 
						| transition transition_list
						;

transition			: return_code ARROW identifier SEMICOLON
							{$$ = symlook($3);
							 gen_return_code($1);
							 gen_new_state($$->name);}
						;

return_code			: SUCCESS {$$ = "0";}
						| INTEGER {$$ = enter_string($1);}
						| identifier {$$ = $1;}
						| DEFAULT {$$ = "-1";}
						;

identifier			: IDENTIFIER {$$ = enter_string($1);}
						;
