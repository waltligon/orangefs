/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef _SYMBOL_H_
#define _SYMBOL_H_

/*
 *	symbol.h --- declarations for all the public types
 */

#define TYPE_MACHINE 0x1
#define TYPE_STATE 0x2	

extern int line;

typedef struct sym_ent
{
	char *name;		/* ptr to string table */
	int type;
	int offset;
	struct sym_ent *next;	/* next in line */
} sym_ent, *sym_ent_p;

sym_ent_p symenter(char *name);
sym_ent_p symlook(char *name);
void init_symbol_table();

#endif

