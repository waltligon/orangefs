/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/*
*	symbol --- symbol table and mapping routines
*/

#include <stdio.h>
#include <string.h>

#include "statecomp-symbol.h"
extern int line;

void *emalloc(unsigned int size);

/*
 *	MAXHASH --- determines the hash table width.
 *	symtab --- the symbol table structure
 */

#define MAXHASH	311

static sym_ent_p symtab[MAXHASH];

/*
 *	hash --- scramble a name (hopefully) uniformly to fit in a table
 */
static unsigned int hash(char *name)
{
    register unsigned int h = 0;
    while (*name)
    {
	h <<= 4;
	h ^= *name++;
    }
    return(h % MAXHASH);
}

/*
 *	symenter --- enter a name into the symbol table
 */
sym_ent_p symenter(char *name)
{
    register sym_ent_p p;
    unsigned int h;

    /* create an entry and insert it at the front of the table */
    h = hash(name);
    p = (sym_ent_p)emalloc(sizeof(sym_ent));
    p->name = name;
    p->next = symtab[h];
    symtab[h] = p;

    return(p);
}

/*
 *	symlook --- lookup a symbol in the symbol table
 *
 *		symlook scans the symbol table and returns a pointer to
 *		the symbol table entry
 */
sym_ent_p symlook(char *name)
{
    register sym_ent_p p;
    unsigned int h;
    h = hash(name);
    for (p = symtab[h]; p != 0; p = p->next)
	if (strcmp(p->name, name) == 0)
	    break;

    return(p);
}

void init_symbol_table(void)
{
    memset(symtab, 0, MAXHASH * sizeof(sym_ent_p));
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
