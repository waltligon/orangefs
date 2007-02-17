/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup statecomp
 *
 * Symbol table and mapping routines for state machine source-to-source
 * translator.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "statecomp-symbol.h"

extern void *emalloc(unsigned int size);

struct state *states = NULL;

struct state *new_state(char *name)
{
    struct state *s, **sprev = &states;

    for (s=states; s; s=s->next) {
        if (!strcmp(s->name, name)) {
            fprintf(stderr, "%s: state %s already exists\n", __func__, name);
            exit(1);
        }
        sprev = &s->next;
    }
    s = emalloc(sizeof(*s));
    s->name = name;
    s->transition = NULL;
    s->next = NULL;
    *sprev = s;
    return s;
}

struct transition *new_transition(struct state *s, char *return_code)
{
    struct transition *t, **tprev = &s->transition;

    for (t=s->transition; t; t=t->next) {
        if (!strcmp(t->return_code, return_code)) {
            fprintf(stderr, "%s: state %s: return code %s already exists\n",
                    __func__, s->name, return_code);
            exit(1);
        }
        tprev = &t->next;
    }
    t = emalloc(sizeof(*t));
    t->return_code = return_code;
    t->next = NULL;
    *tprev = t;
    return t;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
