/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup statecomp
 *
 *  Defines and prototypes for statecomp symbol table.
 */

#ifndef _SYMBOL_H_
#define _SYMBOL_H_

enum transition_type { NEXT_STATE, RETURN, TERMINATE };
enum state_action { ACTION_RUN, ACTION_JUMP };

struct transition {
    struct transition *next;
    char *return_code;
    enum transition_type type;
    char *next_state;
};

struct state {
    struct state *next;
    char *name;
    enum state_action action;
    char *function_or_machine;
    struct transition *transition;
};

extern struct state *states;

void init_symbol_table(void);
struct transition *new_transition(struct state *s, char *return_code);
struct state *new_state(char *name);
void gen_machine(char *machine_name);

#endif

