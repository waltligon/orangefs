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

#define TYPE_MACHINE 0x1
#define TYPE_STATE 0x2	
#define TYPE_EXTERN_STATE 0x4	

extern int line;

typedef struct sym_ent
{
	char *name;		/* ptr to string table */
	int type;
	int flag;
	int offset;
	struct sym_ent *next;	/* next in line */
} sym_ent, *sym_ent_p;

sym_ent_p symenter(char *name);
sym_ent_p symlook(char *name);
void init_symbol_table(void);

#endif

