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
#include <string.h>
#include <assert.h>

#include "../quicklist/quicklist.h"
#include "../quickhash/quickhash.h"

#include "statecomp.h"

static int needcomma = 1;

static void gen_state_decl(char *state_name);
static void gen_runfunc_decl(char *func_name);
static void gen_state_start(char *state_name, char *machine_name);
static void gen_state_action(
    enum state_action action, char *run_func, char *state_name);
static void gen_return_code(char *return_code);
static void gen_next_state(enum transition_type type, char *new_state);
static void gen_state_end(void);
static void gen_trtbl(char *state_name);
static void gen_pjtbl(char *state_name);

void gen_machine(char *machine_name)
{
    struct state *s, *snext;
    struct transition *t, *tnext;
    struct task *task, *tasknext;

    if (states == NULL)
        fprintf(stderr, "%s: no states declared in machine %s\n", __func__,
                machine_name);

    /* dump forward declarations of all the states */
    for (s=states; s; s=s->next)
    {
        if(s->action == ACTION_RUN || s->action == ACTION_PJMP)
            gen_runfunc_decl(s->function_or_machine);
        gen_state_decl(s->name);
    }

    /* delcare the machine start point */
    fprintf(out_file, "\nstruct PINT_state_machine_s %s = {\n", machine_name);
    fprintf(out_file, "\t.name = \"%s\",\n", machine_name);
    fprintf(out_file, "\t.first_state = &ST_%s\n", states->name);
    fprintf(out_file, "};\n\n");

    /* generate all output */
    for (s=states; s; s=s->next) {
        gen_state_start(s->name, machine_name);
        gen_state_action(s->action, s->function_or_machine, s->name);
        if(s->action == ACTION_PJMP)
        {
            gen_pjtbl(s->name);

            /* if there are tasks (the action must be a pjmp) so we output
             * stuff for the tasks
             */
            for (task=s->task; task; task=task->next)
            {
                gen_return_code(task->return_code);
                gen_next_state(TRANS_PJMP, task->task_name);
            }
        }

        gen_trtbl(s->name);

        for (t=s->transition; t; t=t->next) {
            gen_return_code(t->return_code);
            gen_next_state(t->type, t->next_state);
        }
        gen_state_end();
    }

    /* purge for next machine */
    for (s=states; s; s=snext) {
        if(s->task)
        {
            for (task=s->task; task; task=tasknext)
            {
                tasknext = task->next;
                free(task->return_code);
                free(task);
            }
        }

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

struct runfunc_decl_entry
{
    char *func_name;
    struct qhash_head link;
};

static int runfunc_compare(void *key, struct qhash_head *link)
{
    if(!strcmp((char *)key, 
               qhash_entry(link, struct runfunc_decl_entry, link)->func_name))
    {
        return 1;
    }
    return 0;
}

static int runfunc_hash(void *key, int table_size)
{
    char *k = (char *)key;
    int g, h = 0;
    while(*k)
    {
        h = (h << 4) + *k++;
        if((g = (h & 0xF0UL)))
        {
            h ^= g >> 24;
            h ^= g;
        }
    }

    return h % table_size;
}

static struct qhash_table *runfunc_table = NULL;

static void gen_runfunc_decl(char *func_name)
{
    if(!runfunc_table)
    {
        runfunc_table = qhash_init(runfunc_compare, runfunc_hash, 1024);
        assert(runfunc_table);
    }

    if(!qhash_search(runfunc_table, func_name))
    {
        struct runfunc_decl_entry *entry =
            malloc(sizeof(struct runfunc_decl_entry));
        assert(entry);

        entry->func_name = strdup(func_name);
        qhash_add(runfunc_table, entry->func_name, &entry->link);

        fprintf(out_file,
                "\nstatic PINT_sm_action %s(\n"
                "\tstruct PINT_smcb *smcb, job_status_s *js_p);\n\n",
                func_name);
    }
}

static void gen_state_decl(char *state_name)
{
    fprintf(out_file, "static struct PINT_state_s ST_%s;\n", state_name);
    fprintf(out_file, "static struct PINT_pjmp_tbl_s ST_%s_pjtbl[];\n", 
            state_name);
    fprintf(out_file, "static struct PINT_tran_tbl_s ST_%s_trtbl[];\n",
            state_name);
}

void gen_state_start(char *state_name, char *machine_name)
{
    fprintf(out_file,
            "static struct PINT_state_s ST_%s = {\n"
            "\t .state_name = \"%s\" ,\n"
            "\t .parent_machine = &%s ,\n",
            state_name, state_name, machine_name);
}

/** generates first two lines in the state machine (I think),
 * the first one indicating what kind of action it is ("run"
 * or "jump") and the second being the action itself (either a
 * function or a nested state machine).
 */
void gen_state_action(enum state_action action,
                             char *run_func,
                             char *state_name)
{
    switch (action) {
	case ACTION_RUN:
	    fprintf(out_file, "\t .flag = SM_RUN ,\n");
            fprintf(out_file, "\t .action.func = %s ,\n", run_func);
            fprintf(out_file,"\t .pjtbl = NULL ,\n");
            fprintf(out_file,"\t .trtbl = ST_%s_trtbl ", state_name);
	    break;
	case ACTION_PJMP:
	    fprintf(out_file, "\t .flag = SM_PJMP ,\n");
            fprintf(out_file, "\t .action.func = &%s ,\n", run_func);
            fprintf(out_file,"\t .pjtbl = ST_%s_pjtbl ,\n", state_name);
            fprintf(out_file,"\t .trtbl = ST_%s_trtbl ", state_name);
	    break;
	case ACTION_JUMP:
	    fprintf(out_file, "\t .flag = SM_JUMP ,\n");
            fprintf(out_file, "\t .action.nested = &%s ,\n", run_func);
            fprintf(out_file,"\t .pjtbl = NULL ,\n");
            fprintf(out_file,"\t .trtbl = ST_%s_trtbl ", state_name);
	    break;
    }
    /* generate the end of the state struct with refs to jump tbls */
}

void gen_trtbl(char *state_name)
{
    fprintf(out_file,"\n};\n\n");
    fprintf(out_file,"static struct PINT_tran_tbl_s ST_%s_trtbl[] = {\n",
            state_name);
    needcomma = 0;
}

void gen_pjtbl(char *state_name)
{
    fprintf(out_file,"\n};\n\n");
    fprintf(out_file,"static struct PINT_pjmp_tbl_s ST_%s_pjtbl[] = {\n",
            state_name);
    needcomma = 0;
}

static void gen_return_code(char *return_code)
{
    if (needcomma)
    {
        fprintf(out_file,",\n");
    }
    fprintf(out_file, "\t{ .return_value = %s ", return_code);
    needcomma = 1;
}

static void gen_next_state(enum transition_type type, char *new_state)
{
    if (needcomma)
    {
        fprintf(out_file,",\n");
    }
    switch (type) {
	case TRANS_PJMP:
	    fprintf(out_file, "\n\t .state_machine = &%s }", new_state);
	    break;
	case TRANS_NEXT_STATE:
	    fprintf(out_file, "\t .next_state = &ST_%s }", new_state);
	    break;
	case TRANS_RETURN:
	    terminate_path_flag = 1;
	    fprintf(out_file, "\t .flag = SM_RETURN }");
	    break;
	case TRANS_TERMINATE:
	    terminate_path_flag = 1;
	    fprintf(out_file, "\n\t .flag = SM_TERM }");
	    break;
    }
    needcomma = 1;
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
