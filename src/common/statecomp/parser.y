
%{

/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup statecomp
 *
 *  Parser for statecomp source-to-source translator.
 */

#include <stdlib.h>
#include <stdio.h>

#include "statecomp-symbol.h"
#include "state-machine-values.h"

extern char *enter_string(char *);

extern int yylex(void);
extern void yyerror(char *);

/* We never use this, disable default. */
#define YY_LOCATION_PRINT 0

/* No NLS. */
#define YYENABLE_NLS 0

/* building these things up as the rules go */
static struct state *cur_state;
static struct transition *cur_transition;

%}

%union {
   int i;
   char *c;
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

%type <c> identifier return_code
%start state_machine_list

%%

state_machine_list : state_machine 
		  | state_machine state_machine_list
		  ;

state_machine	  : .NESTED. MACHINE identifier
		     LPAREN .state_decl_list. RPAREN LBRACE
		    .state_def_list. RBRACE
		    {
			gen_machine($3);
		    }
		  ;

.NESTED.	  : /* empty */
		  | NESTED  /* ignored */
		  ;

.state_decl_list. : /* empty */
		  | state_decl_list
		  ;

state_decl_list   : state_decl
		  | state_decl COMMA state_decl_list
		  ;

state_decl	  : .EXTERN. identifier
		    {
			/* ignored, for backward compatibility */
			free($2);
		    }
		  ;

.EXTERN.	  : /* empty */
		  | EXTERN   /* ignored */
		  ;

.state_def_list.  : /* empty */
		  | state_def_list
		  ;

state_def_list	  : state_def
		  | state_def state_def_list
		  ;

state_def	  : STATE identifier LBRACE
		    {
			cur_state = new_state($2);
		    }
		    .state_body. RBRACE
		  ;

.state_body.	  : /* empty */
		  | state_body
		  ;

state_body	  : state_action transition_list
		  ;

state_action	  : RUN identifier SEMICOLON
		    {
			cur_state->action = ACTION_RUN;
			cur_state->function_or_machine = $2;
		    }
		  | JUMP identifier SEMICOLON
		    {
			cur_state->action = ACTION_JUMP;
			cur_state->function_or_machine = $2;
		    }
		  ;

transition_list   : transition 
		  | transition transition_list
		  ;

transition	  : return_code ARROW
		    {
			cur_transition = new_transition(cur_state, $1);
		    }
		    target SEMICOLON
		  ;

target		  : identifier
		    {
			cur_transition->type = NEXT_STATE;
			cur_transition->next_state = $1;
		    }
		  | STATE_RETURN
		    {
			cur_transition->type = RETURN;
		    }
		  | STATE_TERMINATE
		    {
			cur_transition->type = TERMINATE;
		    }
		  ;

return_code	  : SUCCESS {$$ = enter_string("0");}
		  | INTEGER {$$ = enter_string($1);}
		  | identifier {$$ = $1;}
		  | DEFAULT {$$ = enter_string("-1");}
		  ;

identifier	  : IDENTIFIER {$$ = enter_string($1);}
		  ;

