/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup statecomp
 *
 * Code generation routines for statecomp.
 */

#include <stdio.h>
#include <stdlib.h>

#include "statecomp.h"

static void gen_state_decl(char *state_name);
static void gen_state_start(char *state_name, char *machine_name);
static void gen_state_action(enum state_action action, char *run_func);
static void gen_return_code(char *return_code);
static void gen_next_state(enum transition_type type, char *new_state);
static void gen_state_end(void);

void gen_machine(char *machine_name)
{
    struct state *s, *snext;
    struct transition *t, *tnext;

    if (states == NULL)
        fprintf(stderr, "%s: no states declared in machine %s\n", __func__,
                machine_name);

    /* dump forward declarations of all the states */
    for (s=states; s; s=s->next)
        gen_state_decl(s->name);

    /* delcare the machine start point */
    fprintf(out_file, "\nstruct PINT_state_machine_s %s = {\n", machine_name);
    fprintf(out_file, "\t.name = \"%s\",\n", machine_name);
    fprintf(out_file, "\t.state_machine = ST_%s\n", states->name);
    fprintf(out_file, "};\n\n");

    /* generate all output */
    for (s=states; s; s=s->next) {
        gen_state_start(s->name, machine_name);
        gen_state_action(s->action, s->function_or_machine);
        for (t=s->transition; t; t=t->next) {
            gen_return_code(t->return_code);
            gen_next_state(t->type, t->next_state);
        }
        gen_state_end();
    }

    /* purge for next machine */
    for (s=states; s; s=snext) {
        for (t=s->transition; t; t=tnext) {
            tnext = t->next;
            free(t->return_code);
            free(t);
        }
        snext = s->next;
        free(s->name);
        free(s);
    }
    states = NULL;
}

static void gen_state_decl(char *state_name)
{
    fprintf(out_file, "static union PINT_state_array_values ST_%s[];\n",
            state_name);
}

static void gen_state_start(char *state_name, char *machine_name)
{
    fprintf(out_file,
            "static union PINT_state_array_values ST_%s[] = {\n"
            "\t{ .state_name = \"%s\" },\n"
            "\t{ .parent_machine = &%s },\n", 
            state_name, state_name, machine_name);
}

/** generates first two lines in the state machine (I think),
 * the first one indicating what kind of action it is ("run"
 * or "jump") and the second being the action itself (either a
 * function or a nested state machine).
 */
static void gen_state_action(enum state_action action, char *run_func)
{
    switch (action) {
	case ACTION_RUN:
	    fprintf(out_file, "\t{ .flag = SM_NONE },\n");
            fprintf(out_file, "\t{ .state_action = %s }", run_func);
	    break;
	case ACTION_JUMP:
	    fprintf(out_file, "\t{ .flag = SM_JUMP },\n");
            fprintf(out_file, "\t{ .nested_machine = &%s }", run_func);
	    break;
    }
}

static void gen_return_code(char *return_code)
{
    fprintf(out_file, ",\n\t{ .return_value = %s }", return_code);
}

static void gen_next_state(enum transition_type type, char *new_state)
{
    switch (type) {
	case NEXT_STATE:
	    fprintf(out_file, ",\n\t{ .next_state = ST_%s }", new_state);
	    break;
	case RETURN:
	    terminate_path_flag = 1;
	    fprintf(out_file, ",\n\t{ .flag = SM_RETURN }");
	    break;
	case TERMINATE:
	    terminate_path_flag = 1;
	    fprintf(out_file, ",\n\t{ .flag = SM_TERMINATE }");
	    break;
    }
}

static void gen_state_end(void)
{
    fprintf(out_file,"\n};\n\n");
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
