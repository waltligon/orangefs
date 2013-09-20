/*
 * simple little ditty showing how one might use the qlist stuff 
 */

#include <stdlib.h>
#include <stdio.h>
#include "quicklist.h"

void init_list(void);
int add_item(int data);
void dump_list(void);

struct foo_struct 
{
	int blah;
	struct qlist_head link;
};

QLIST_HEAD(list_head);

void init_list(void)
{
	INIT_QLIST_HEAD(&list_head);
}

int add_item(int data)
{
	struct foo_struct *foo = (struct foo_struct *)calloc(1, sizeof(*foo));
	foo->blah = data;

	qlist_add(&(foo->link), &list_head);
	return 0;
}

void dump_list(void) 
{
	struct qlist_head *p;

	qlist_for_each(p, &list_head) {
		printf("data: %d\n", qlist_entry(p, struct foo_struct, link)->blah);
	}
}

int main(int argc, char ** argv )
{
	int i;
	init_list();

	for (i=0; i<10; i++) {
		add_item(i);
	}

	dump_list();
	return 0;
}
