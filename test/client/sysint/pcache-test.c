#include "pcache.h"
#include "pcache-test.h"

void print_pinode(pinode *toprint);

int main(int argc,char* argv[])
{
	pinode *pinode1, *pinode2, *pinode3, *test_pinode = NULL;
	int ret;

	gossip_enable_stderr();
	gossip_set_debug_mask(1,CLIENT_DEBUG);

	ret = PINT_pcache_initialize( );
	if (ret < 0)
	{
		printf("pcache init failed with errcode %d\n", ret);
		return(-1);
	}
	printf("pcache initialized\n");

	PINT_pcache_pinode_alloc(&pinode1);
	if (pinode1 == NULL)
	{
		printf("malloc pinode 1 failed\n");
		return(-1);
	}

	PINT_pcache_pinode_alloc(&pinode2);
	if (pinode2 == NULL)
	{
		printf("malloc pinode 2 failed\n");
		return(-1);
	}

	PINT_pcache_pinode_alloc(&pinode3);
	if (pinode3 == NULL)
	{
		printf("malloc pinode 3 failed\n");
		return(-1);
	}

	PINT_pcache_pinode_alloc(&test_pinode);
	if (pinode1 == NULL)
	{
		printf("malloc test_pinode failed\n");
		return(-1);
	}

	/* insert some elements */

	pinode1->pinode_ref.handle = 1;
	pinode1->pinode_ref.fs_id = 1;
	pinode1->attr.owner = 1;
	pinode1->attr.group = 1;
	pinode1->attr.perms = 1;
	pinode1->attr.ctime = 1;
	pinode1->attr.mtime = 1;
	pinode1->attr.atime = 1;
	pinode1->attr.objtype = PVFS_TYPE_METAFILE;
	//pinode1->attr.u.meta = ;
	pinode1->mask = 5;
	pinode1->size = 1;
	pinode1->tstamp.tv_sec = 111;
	pinode1->tstamp.tv_usec = 111;

	pinode2->pinode_ref.handle = 2;
	pinode2->pinode_ref.fs_id = 2;
	pinode2->attr.owner = 2;
	pinode2->attr.group = 2;
	pinode2->attr.perms = 2;
	pinode2->attr.ctime = 2;
	pinode2->attr.mtime = 2;
	pinode2->attr.atime = 2;
	pinode2->attr.objtype = PVFS_TYPE_METAFILE;
	//pinode2->attr.u.meta = ;
	pinode2->mask = 6;
	pinode2->size = 2;
	pinode2->tstamp.tv_sec = 222;
	pinode2->tstamp.tv_usec = 222;

	pinode3->pinode_ref.handle = 3;
	pinode3->pinode_ref.fs_id = 3;
	pinode3->attr.owner = 3;
	pinode3->attr.group = 3;
	pinode3->attr.perms = 3;
	pinode3->attr.ctime = 3;
	pinode3->attr.mtime = 3;
	pinode3->attr.atime = 3;
	pinode3->attr.objtype = PVFS_TYPE_METAFILE;
	//pinode3->attr.u.meta = ;
	pinode3->mask = 7;
	pinode3->size = 3;
	pinode3->tstamp.tv_sec = 333;
	pinode3->tstamp.tv_usec = 333;

	ret = PINT_pcache_insert(pinode1);
	if (ret < 0)
	{
		printf("pcache insert failed (#1) with errcode %d\n", ret);
		return(-1);
	}
	printf("pinode 1 inserted\n");

	ret = PINT_pcache_insert(pinode2);
	if (ret < 0)
	{
		printf("pcache insert failed (#2) with errcode %d\n", ret);
		return(-1);
	}
	printf("pinode 2 inserted\n");

	ret = PINT_pcache_insert(pinode3);
	if (ret < 0)
	{
		printf("pcache insert failed (#3) with errcode %d\n", ret);
		return(-1);
	}
	printf("pinode 3 inserted\n");

	/* lookup element that was inserted */

	ret = PINT_pcache_lookup(pinode2->pinode_ref, &test_pinode);
	if (ret < 0)
	{
		printf("pcache lookup failed (#2) with errcode %d\n", ret);
		return(-1);
	}
	printf("pinode 2 looked up\n");

#if 0
	print_pinode( test_pinode );

	/* remove an element */
	/* lookup element that was removed */
#endif

	ret = PINT_pcache_finalize( );
	if (ret < 0)
	{
		printf("pcache finalize failed with errcode %d\n", ret);
		return(-1);
	}
	printf("pcache finalized\n");

	gossip_disable();

	return(0);
}

void print_pinode(pinode *toprint)
{
	printf("============printing=pnode=========\n");
	printf("pinode.pinode_ref.handle = %d\n", (int)toprint->pinode_ref.handle);
	printf("pinode.pinode_ref.fs_id = %d\n", (int)toprint->pinode_ref.fs_id);
	printf("pinode.attr.owner = %d\n", (int)toprint->attr.owner);
	printf("pinode.attr.group = %d\n", (int)toprint->attr.group);
	printf("pinode.attr.perms = %d\n", (int)toprint->attr.perms);
	printf("pinode.attr.ctime = %d\n", (int)toprint->attr.ctime);
	printf("pinode.attr.mtime = %d\n", (int)toprint->attr.mtime);
	printf("pinode.attr.atime = %d\n", (int)toprint->attr.atime);
	switch(toprint->attr.objtype)
	{
		case PVFS_TYPE_METAFILE:
			printf("pinode.attr.objtype = PVFS_TYPE_METAFILE\n");
		default:
			printf("pinode.attr.objtype = dunno\n");
			break;
	}
	printf("pinode.mask = %d\n", (int)toprint->mask);
	printf("pinode.size = %d\n", (int)toprint->size);
	printf("pinode.tstamp.tv_sec = %d\n", (int)toprint->tstamp.tv_sec);
	printf("pinode.tstamp.tv_usec = %d\n", (int)toprint->tstamp.tv_usec);
	printf("===================================\n");
}

