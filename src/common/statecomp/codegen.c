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

#define PVFS_MALLOC_REDEF_OVERRIDE 1
#include "pvfs2-internal.h"
#include "../quicklist/quicklist.h"
#include "../quickhash/quickhash.h"

#include "statecomp.h"

static int needcomma = 1;

static void gen_state_decl(struct state *s, char *state_name);
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
    for (s = states; s; s = s->next)
    {
        if(s->action == ACTION_RUN || s->action == ACTION_PJMP)
        {
            gen_runfunc_decl(s->function_or_machine);
        }
        gen_state_decl(s, s->name);
    }

    /* delcare the machine start point */
    fprintf(out_file, "\nstruct PINT_state_machine_s %s = {\n", machine_name);
#ifdef WIN32
    /* Windows (VC++) does not support field names in structs */
    fprintf(out_file, "\t\"%s\",  /* name */\n", machine_name);
    fprintf(out_file, "\t&ST_%s  /* first_state */\n", states->name);
#else
    fprintf(out_file, "\t.name = \"%s\",\n", machine_name);
    fprintf(out_file, "\t.first_state = &ST_%s\n", states->name);
#endif
    fprintf(out_file, "};\n\n");

    /* generate all output */
    for (s = states; s; s = s->next) 
    {
        gen_state_start(s->name, machine_name);
        gen_state_action(s->action, s->function_or_machine, s->name);
        if(s->action == ACTION_PJMP)
        {
            gen_pjtbl(s->name);

            /* if there are tasks (the action must be a pjmp) so we output
             * stuff for the tasks
             */
            for (task = s->task; task; task = task->next)
            {
                gen_return_code(task->return_code);
                gen_next_state(TRANS_PJMP, task->task_name);
            }
            gen_return_code("-1");
            gen_next_state(TRANS_PJMP, NULL);
        }

        gen_trtbl(s->name);

        for (t = s->transition; t; t = t->next) {
            gen_return_code(t->return_code);
            gen_next_state(t->type, t->next_state);
        }
        gen_state_end();
    }

    /* purge for next machine */
    for (s = states; s; s = snext) {
        if(s->task)
        {
            for (task = s->task; task; task = tasknext)
            {
                tasknext = task->next;
                free(task->return_code);
                free(task);
            }
        }

        for (t = s->transition; t; t = tnext) {
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

static int runfunc_compare(const void *key, struct qhash_head *link)
{
    if(!strcmp((const char *)key, 
               qhash_entry(link, struct runfunc_decl_entry, link)->func_name))
    {
        return 1;
    }
    return 0;
}

static int runfunc_hash(const void *key, int table_size)
{
    const char *k = key;
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

static void gen_state_decl(struct state *s, char *state_name)
{
#ifdef WIN32
    struct task *task;
    struct transition *t;
    int count;
#endif
    fprintf(out_file, "static struct PINT_state_s ST_%s;\n", state_name);
#ifdef WIN32
    /* determine PJMP count for array declaration */
    if (s->action == ACTION_PJMP)
    {
        for (task = s->task, count = 0; task; task = task->next, ++count) ;
        fprintf(out_file, "static struct PINT_pjmp_tbl_s ST_%s_pjtbl[%d];\n", 
                state_name, count);
    }
    /* determine transition count for array declaration */
    for (t = s->transition, count = 0; t; t = t->next, ++count) ;
    fprintf(out_file, "static struct PINT_tran_tbl_s ST_%s_trtbl[%d];\n",
            state_name, count);
#else
    if (s->action == ACTION_PJMP)
    {
        fprintf(out_file, "static struct PINT_pjmp_tbl_s ST_%s_pjtbl[];\n", 
                state_name);
    }
    fprintf(out_file, "static struct PINT_tran_tbl_s ST_%s_trtbl[];\n",
            state_name);
#endif
}

void gen_state_start(char *state_name, char *machine_name)
{
#ifdef WIN32
    fprintf(out_file,
            "static struct PINT_state_s ST_%s = {\n"
            "\t \"%s\" ,  /* state_name */\n"
            "\t &%s , /* parent_machine */\n",
            state_name, state_name, machine_name);
#else
    fprintf(out_file,
            "static struct PINT_state_s ST_%s = {\n"
            "\t .state_name = \"%s\" ,\n"
            "\t .parent_machine = &%s ,\n",
            state_name, state_name, machine_name);
#endif
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
#ifdef WIN32
            fprintf(out_file, "\t SM_RUN ,  /* flag */\n");
            fprintf(out_file, "\t { %s } ,  /* action.func */\n", run_func);
            fprintf(out_file, "\t NULL ,  /* pjtbl */\n");
            fprintf(out_file, "\t ST_%s_trtbl  /* trtbl */", state_name);
#else
            fprintf(out_file, "\t .flag = SM_RUN ,\n");
            fprintf(out_file, "\t .action.func = %s ,\n", run_func);
            fprintf(out_file, "\t .pjtbl = NULL ,\n");
            fprintf(out_file, "\t .trtbl = ST_%s_trtbl ", state_name);
#endif
            break;
        case ACTION_PJMP:
#ifdef WIN32
            fprintf(out_file, "\t SM_PJMP ,  /* flag */\n");
            fprintf(out_file, "\t { &%s },  /* action.func */\n", run_func);
            fprintf(out_file, "\t ST_%s_pjtbl ,  /* pjtbl */\n", state_name);
            fprintf(out_file, "\t ST_%s_trtbl  /* trtbl */", state_name);

#else
            fprintf(out_file, "\t .flag = SM_PJMP ,\n");
            fprintf(out_file, "\t .action.func = &%s ,\n", run_func);
            fprintf(out_file, "\t .pjtbl = ST_%s_pjtbl ,\n", state_name);
            fprintf(out_file, "\t .trtbl = ST_%s_trtbl ", state_name);
#endif
            break;
        case ACTION_JUMP:
#ifdef WIN32
            fprintf(out_file, "\t SM_JUMP ,  /* flag */\n");
            fprintf(out_file, "\t { &%s }, /* action.nested */\n", run_func);
            fprintf(out_file, "\t NULL ,  /* pjtbl */\n");
            fprintf(out_file, "\t ST_%s_trtbl  /* trtbl */", state_name);
#else
            fprintf(out_file, "\t .flag = SM_JUMP ,\n");
            fprintf(out_file, "\t .action.nested = &%s ,\n", run_func);
            fprintf(out_file, "\t .pjtbl = NULL ,\n");
            fprintf(out_file, "\t .trtbl = ST_%s_trtbl ", state_name);
#endif
            break;
        case ACTION_SWITCH:
#ifdef WIN32
            fprintf(out_file, "\t SM_SWITCH ,  /* flag */\n");
            fprintf(out_file, "\t { NULL }, /* action.nested */\n");
            fprintf(out_file, "\t NULL ,  /* pjtbl */\n");
            fprintf(out_file, "\t ST_%s_trtbl  /* trtbl */", state_name);
#else
            fprintf(out_file, "\t .flag = SM_SWITCH ,\n");
            fprintf(out_file, "\t .action.nested = NULL ,\n");
            fprintf(out_file, "\t .pjtbl = NULL ,\n");
            fprintf(out_file, "\t .trtbl = ST_%s_trtbl ", state_name);
#endif
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
#ifdef WIN32
    fprintf(out_file, "\t{ %s ", return_code);
#else
    fprintf(out_file, "\t{ .return_value = %s ", return_code);
#endif
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
#ifdef WIN32
            fprintf(out_file, "\t SM_NONE ,\n");  /* flag */
            if (new_state != NULL)
            {
                fprintf(out_file, "\n\t &%s }", new_state);
            }
            else
            {
                fprintf(out_file, "\n\t NULL }");
            }
#else
            if (new_state != NULL)
            {
                fprintf(out_file, "\n\t .state_machine = &%s }", new_state);
            }
            else
            {
                fprintf(out_file, "\n\t .state_machine = NULL }");
            }
#endif
            break;
        case TRANS_NEXT_STATE:
#ifdef WIN32
            fprintf(out_file, "\t SM_NONE ,\n");  /* flag */
            fprintf(out_file, "\t &ST_%s }", new_state);
#else
            fprintf(out_file, "\t .next_state = &ST_%s }", new_state);
#endif
            break;
        case TRANS_RETURN:
            terminate_path_flag = 1;
#ifdef WIN32
            fprintf(out_file, "\t SM_RETURN ,\n"); /* flag */
            fprintf(out_file, "\t NULL }");  /* next_state/state_machine */

#else
            fprintf(out_file, "\t .flag = SM_RETURN }");
#endif
            break;
        case TRANS_TERMINATE:
            terminate_path_flag = 1;
#ifdef WIN32
            fprintf(out_file, "\t SM_TERM ,\n"); /* flag */
            fprintf(out_file, "\t NULL }");  /* next_state/state_machine */
#else
            fprintf(out_file, "\n\t .flag = SM_TERM }");
#endif
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
