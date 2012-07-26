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

enum transition_type
{
    TRANS_NEXT_STATE,
    TRANS_RETURN,
    TRANS_TERMINATE,
    TRANS_PJMP
};

enum state_action
{
    ACTION_RUN,
    ACTION_JUMP,
    ACTION_PJMP
};

struct task {
    struct task *next;
    char *return_code;
    char *task_name;
};

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
    struct task *task;
    struct transition *transition;
};

extern struct state *states;
extern int terminate_path_flag;
extern int line;
extern FILE *out_file;
extern const char *in_file_name;

void yyerror(char *);
void *emalloc(size_t size);
char *estrdup(const char *oldstring);
struct state *new_state(char *name);
struct transition *new_transition(struct state *s, char *return_code);
struct task *new_task(
    struct state *s, char *return_code, char *task_name);

void gen_machine(char *machine_name);

