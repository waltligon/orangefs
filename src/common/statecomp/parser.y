
%{

/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>
#include <stdio.h>

#include "statecomp-symbol.h"
#include "state-machine-values.h"

char *enter_string(char *);
void gen_init(void);
void gen_state_decl(char *state_name);
void gen_state_array(char *machine_name, char *first_state_name);
void gen_state_start(char *state_name);
void gen_state_action(char *run_func, int flag);
void gen_return_code(char *return_code);
void gen_next_state(int flag, char *new_state);
void gen_state_end(void);
void gen_machine(char *machine_name, char *first_state_name);

int yylex(void);
void yyerror(char *);

/*
 * Local variables:
 *    c-indent-level: 4
 *    c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

%}

%union{
   int i;
   double f;
   char *c;
   sym_ent *s;
};

%token <i> MACHINE
%token <i> NESTED
%token <i> INIT
%token <i> STATE
%token <i> EXTERN
%token <i> RUN
%token <i> JUMP
%token <i> STATE_RETURN
%token <i> STATE_TERMINATE
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

%type <i> .state_body. state_body state_action .NESTED. .EXTERN.

%type <c> identifier return_code

%type <s> state_decl_list .state_decl_list. state_decl
	  state_def state_def_list .state_def_list.
	  transition transition_list state_machine target 

%start state_machine

%%

state_machine	  : .NESTED. MACHINE identifier
			{$$ = symenter($3);
			 $$->type = TYPE_MACHINE;
			 $$->flag = $1;}
		     LPAREN .state_decl_list. RPAREN LBRACE
			{gen_machine($3, $6->name);}
		    .state_def_list. RBRACE
			 {;}
		  ;

.NESTED.	  : /* empty */
		     {$$ = SM_NONE;}
		  | NESTED
		     {$$ = SM_NESTED;}
		  ;

.state_decl_list. : /* empty */
		     {$$ = NULL;}
		  | state_decl_list
		  ;

state_decl_list   : state_decl
		  | state_decl COMMA state_decl_list
		  ;

state_decl	  : .EXTERN. identifier
		     {$$ = symlook($2);
		      if ($$ != NULL) {
			 fprintf(stderr,"identifier already declared %s\n", $2);
			 exit(1);
		      } 
		      else {
			 $$ = symenter($2);
			 $$->type = TYPE_STATE;
			 $$->flag = $1;
			 gen_state_decl($2);}}
		  ;

.EXTERN.	  : /* empty */
		     {$$ = SM_NONE;}
		  | EXTERN 
		     {$$ = SM_EXTERN;}
		  ;

.state_def_list.  : /* empty */
		     {$$ = NULL;}
		  | state_def_list
		  ;

state_def_list	  : state_def
		  | state_def state_def_list
		  ;

state_def	  : STATE identifier LBRACE
		     {$$ = symlook($2);
		      if ($$->type != TYPE_STATE){
			 fprintf(stderr,"bad state identifier %s\n", $2);
			 fprintf(stderr,"declared as another type\n");
			 exit(1);
		      }
		      else{
			 gen_state_start($2);}}
		    .state_body. RBRACE
		      {gen_state_end();}
		  ;

.state_body.	  : /* empty */
		     {$$ = 0;}
		  | state_body
		  ;

state_body	  : state_action transition_list
		  ;

state_action	  : RUN identifier SEMICOLON
		     {gen_state_action($2, SM_NONE);}
		  | JUMP identifier SEMICOLON
		     {gen_state_action($2, SM_JUMP);}
		  ;

transition_list   : transition 
		  | transition transition_list
		  ;

transition	  : return_code
		      {gen_return_code($1);}
		    ARROW target SEMICOLON
		     {$$ = $4;}
		  ;

target		  : identifier
		     {$$ = symlook($1);
		      if ($$ == NULL){
			 fprintf(stderr,"jump to undeclared state %s\n", $1);
			 exit(1);
		      }
		      else{
			 gen_next_state(SM_NEXT, $$->name);}}
		  | STATE_RETURN
		     {$$ = NULL;
		      gen_next_state(SM_RETURN, NULL);}
		  | STATE_TERMINATE
		     {$$ = NULL;
		      gen_next_state(SM_TERMINATE, NULL);}
		  ;

return_code	  : SUCCESS {$$ = "0";}
		  | INTEGER {$$ = enter_string($1);}
		  | identifier {$$ = $1;} /* check for decl */
		  | DEFAULT {$$ = "-1";}
		  ;

identifier	  : IDENTIFIER {$$ = enter_string($1);}
		  ;
