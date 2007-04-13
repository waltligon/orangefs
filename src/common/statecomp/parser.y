
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

#include "statecomp.h"

/* We never use this, disable default. */
#define YY_LOCATION_PRINT 0

/* No NLS. */
#define YYENABLE_NLS 0

/* building these things up as the rules go */
static struct state *cur_state;
static struct transition *cur_transition;
static struct task *cur_task;

void gen_state_start(char *state_name, char *machine_name);
void gen_state_action(enum state_action action,
                             char *run_func, char *state_name);
void gen_return_code(char *return_code);
void gen_next_state(enum transition_type type, char *new_state);
void gen_state_end(void);
char *current_machine_name(void);

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
};

%token <i> MACHINE
%token <i> NESTED
%token <i> STATE
%token <i> RUN
%token <i> PJMP
%token <i> JUMP
%token <i> STATE_RETURN
%token <i> STATE_TERMINATE
%token <i> SUCCESS
%token <c> DEFAULT
%token <i> LBRACE
%token <i> RBRACE
%token <i> SEMICOLON
%token <i> ARROW
%token <c> IDENTIFIER

%type <i> state_body state_action

%type <c> identifier return_code transition transition_list

%type <i>  state_def state_machine target state_machine_list
	   task task_list

%start state_machine_list

%%

state_machine_list : state_machine
		   | state_machine state_machine_list
		   ;

state_machine	  : NESTED MACHINE identifier LBRACE state_def_list RBRACE
		    {
			gen_machine($3);
		    }
		  | MACHINE identifier LBRACE state_def_list RBRACE
                    {
			gen_machine($2);
		    }
                  ;

state_def_list	  : state_def
		  | state_def state_def_list
		  ;

state_def	  : STATE identifier LBRACE
		    {
			cur_state = new_state($2);
                    }
		    state_body RBRACE
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
		  | PJMP identifier LBRACE task_list RBRACE
		    {
			cur_state->action = ACTION_PJMP;
			cur_state->function_or_machine = $2;
		    }
		  ;

task_list	: task
		| task task_list
		  ;

task    : return_code ARROW identifier SEMICOLON
	  {
	      cur_task = new_task(cur_state, $1, $3);
	  }

transition_list   : transition
		  | transition transition_list
		  ;

transition	  : return_code ARROW
		    {
			cur_transition = new_transition(cur_state, $1);
		    }
		    target SEMICOLON
		  ;

target            : identifier
		    {
			cur_transition->type = TRANS_NEXT_STATE;
			cur_transition->next_state = $1;
		    }
		  | STATE_RETURN
		    {
			cur_transition->type = TRANS_RETURN;
		    }
		  | STATE_TERMINATE
		    {
		  	cur_transition->type = TRANS_TERMINATE;
		    }
		  ;

return_code	  : SUCCESS {$$ = estrdup("0");}
		  | DEFAULT {$$ = estrdup("-1");}
		  | identifier {$$ = $1;}
		  ;

identifier	  : IDENTIFIER {$$ = estrdup($1);}
		  ;

