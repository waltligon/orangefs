
#include "ucache.h"

/** this is the master pointer to the cache */
static union user_cache_u *ucache;
static int ucache_blk_cnt;

static void add_free_mtbls(int blk)
{
	int i, start_mtbl;
	struct file_table_s *ftbl = &(ucache->ftbl);
	union cache_block_u *b = &(ucache->b[blk]);

	/* add mtbls in blk to ftbl free list */
	if (blk == 0)
	{
		start_mtbl = 1; /* skip blk 0 ent 0 which is ftbl */
	}
	else
	{
		start_mtbl = 0;
	}
	for (i = start_mtbl; i < MTBL_PER_BLOCK - 1; i++)
	{
		b->mtbl[i].free_list_blk = blk;
		b->mtbl[i].free_list = i + 1;
	}
	b->mtbl[i].free_list_blk = ftbl->free_mtbl_blk;
	b->mtbl[i].free_list = ftbl->free_mtbl_ent;
	ftbl->free_mtbl_blk = blk;
	ftbl->free_mtbl_ent = start_mtbl;
}

void ucache_initialize(void)
{
	int key, id, i;
	char *key_file_path;

	/* set up shared memory region */
	key_file_path = GET_KEY_FILE;
	key = ftok(key_file_path, PROJ_ID);
	id = shmget(key, CACHE_SIZE, CACHE_FLAGS);
	ucache = shmat(id, NULL, AT_FLAGS);
	ucache_blk_cnt = BLOCKS_IN_CACHE;
	/* initialize mtbl free list table */
	ucache->ftbl.free_mtbl_blk = NIL;
	ucache->ftbl.free_mtbl_ent = NIL;
	add_free_mtbls(0);
	/* set up list of free blocks */
	ucache->ftbl.free_blk = 1;
	for (i = 1; i < ucache_blk_cnt - 1; i++)
	{
		ucache->b[i].mtbl[0].free_list = i+1;
	}
	ucache->b[ucache_blk_cnt - 1].mtbl[0].free_list = NIL;
	/* set up file hash table */
	for (i = 0; i < FILE_TABLE_HASH_MAX; i++)
	{
		ucache->ftbl.file[i].mtbl_blk = NIL;
		ucache->ftbl.file[i].mtbl_ent = NIL;
	}
	/* set up list of free hash table entries */
	ucache->ftbl.free_list = FILE_TABLE_HASH_MAX;
	for (i = FILE_TABLE_HASH_MAX; i < FILE_TABLE_ENTRY_COUNT - 1; i++)
	{
		ucache->ftbl.file[i].next = i+1;
	}
	ucache->ftbl.file[FILE_TABLE_ENTRY_COUNT - 1].next = NIL;
}

static void init_memory_table(int blk, int ent)
{
	int i;
	struct mem_table_s *mtbl = &(ucache->b[blk].mtbl[ent]);

	mtbl->lru_first = NIL;
	mtbl->lru_last = NIL;
	mtbl->dirty_list = NIL;
	mtbl->num_blocks = 0;
	/* set up hash table */
	for (i = 0; i < MEM_TABLE_HASH_MAX; i++)
	{
		mtbl->mem[i].item = NIL;
	}
	/* set up list of free hash table entries */
	mtbl->free_list = MEM_TABLE_HASH_MAX;
	for (i = MEM_TABLE_HASH_MAX; i < MEM_TABLE_ENTRY_COUNT - 1; i++)
	{
		mtbl->mem[i].next = i+1;
	}
	mtbl->mem[MEM_TABLE_ENTRY_COUNT - 1].next = NIL;
}

static int get_free_blk(void)
{
	struct file_table_s *ftbl = &(ucache->ftbl);
	int ret = ftbl->free_blk;
	if(ret!=NIL){
		/* Use zero since free_blks have no ititialized mem tables */
 		ftbl->free_blk = ucache->b[ret].mtbl[0].free_list; 
		return ret;
	}
	else{
		/*	EVICT?	*/
		return NIL;
	}
}

static void put_free_blk(int blk)
{
	struct file_table_s *ftbl = &(ucache->ftbl);
	ucache->b[blk].mtbl[0].free_list = ftbl->free_blk;
	/*	necessary?	*/
	ucache->b[blk].mtbl[0].free_list_blk = NIL;
	
	ftbl->free_blk = blk;
}

static int get_free_fent(void)
{
	struct file_table_s *ftbl = &(ucache->ftbl);
	int ret = ftbl->free_list;
	if(ret!=NIL){
		ftbl->free_list = ftbl->file[ret].next;
		return ret;
	}
	else{
		/*	EVICT?	*/
		return NIL;
	}
}

static void put_free_fent(int fent)
{
	struct file_table_s *ftbl = &(ucache->ftbl);
	ftbl->file[fent].next = ftbl->free_list;
	ftbl->free_list = fent;
}

static int get_free_ment(struct mem_table_s *mtbl)
{
	int ret = mtbl->free_list;
	if(ret!=NIL){
		mtbl->free_list = mtbl->mem[ret].next;
		return ret;
	}
	else{
		/*	EVICT?	*/
		return NIL;
	}
}

static void put_free_ment(struct mem_table_s *mtbl, int ent)
{
	mtbl->mem[ent].next = mtbl->free_list;
	mtbl->free_list = ent;
}

/** Hash Function - 
 * Also in quickhash library in: pvfs2/src/common/quickhash/quickhash.h
 * derived from an algorithm found in Aho, Sethi and Ullman's
 * {Compilers: Principles, Techniques and Tools}, published by Addison-Wesley. 
 * This algorithm comes from P.J. Weinberger's C compiler. 
 */
static inline int hash(void *k, int table_size)
{
    const char *str = (char *)k;
    uint32_t g, h = 0;
    while(*str)
    {
        h = (h << 4) + *str++;
        if((g = (h & 0xF0UL)))
        {
            h ^= g >> 24;
            h ^= g;
        }
    }
    return (int)(h & ((uint64_t)(table_size - 1)));
}


static struct mem_table *lookup_file(uint32_t fs_id, uint64_t handle)
{
	struct file_table_s *ftbl = &(ucache->ftbl);
	struct file_ent_s *current;	/* Current ptr for iteration	*/
	char ids_as_string[25];	/*	(32+64)/4 + 1 (for null byte)	*/
	int index;		/*	index into file hash table	*/
	/*	convert identifiers into concatenated hex string to be sent 
	*	to hash function
	*/  	
	sprintf(ids_as_string, "%08lX%016llX",(long unsigned int)fs_id, 
		(long long unsigned int)handle);
	/*	Determine index	into hash table	*/
	index = hash(ids_as_string, FILE_TABLE_HASH_MAX);
	current = &(ftbl->file[index]);
	if(current->mtbl_blk!=NIL && current->mtbl_ent!=NIL){
		/*	Iterate and examine the fs_id and handle.
		*	stop when matched or next is NIL	*/
		while((int)current!=NIL){	
			if(current->tag_id==fs_id && 
				current->tag_handle==handle){
				return (struct mem_table *)&(ucache->b[
					current->mtbl_blk].mtbl[
					current->mtbl_ent]);
			}
			current = &(ftbl->file[current->next]);
		}
		return (struct mem_table *)NIL;
	}
	else{
		if(current->tag_id==fs_id && current->tag_handle==handle){
			return (struct mem_table *)&(ucache->b[
				current->mtbl_blk].mtbl[current->mtbl_ent]);
		}
		return (struct mem_table *)NIL;
	} 	 
}

static int get_next_free_mtbl(uint32_t *free_mtbl_blk, uint16_t *free_mtbl_ent){
		struct file_table_s *ftbl = &(ucache->ftbl);
		/*	get next free mtbl_blk and ent	*/
		*free_mtbl_blk = ftbl->free_mtbl_blk;
		*free_mtbl_ent = ftbl->free_mtbl_ent;
		/* is free mtbl_blk available? */
		if(*free_mtbl_blk!=NIL && *free_mtbl_ent!=NIL){ 
			ftbl->free_mtbl_blk = ucache->b[*free_mtbl_blk].
				mtbl[*free_mtbl_ent].free_list_blk;
			ftbl->free_mtbl_ent = ucache->b[*free_mtbl_blk].
				mtbl[*free_mtbl_ent].free_list;
			/* Set free info to NIL - NECESSARY? */
			ucache->b[*free_mtbl_blk].
				mtbl[*free_mtbl_ent].free_list = NIL;
			ucache->b[*free_mtbl_blk].
				mtbl[*free_mtbl_ent].free_list_blk = NIL;
			return 1;
		}
		else{
			return NIL;
		}
}

static struct mem_ent_s *insert_file(uint32_t fs_id, uint64_t handle)
{
	struct file_table_s *ftbl = &(ucache->ftbl);
	struct file_ent_s *current;	/* Current ptr for iteration	*/
	char ids_as_string[25];	/*	(32+64)/4 + 1 (for null byte)	*/
	int index;		/*	index into file hash table	*/
	/*	convert identifiers into concatenated hex string to be sent 
	*	to hash function
	*/  	  	
	sprintf(ids_as_string, "%08lX%016llX", (long unsigned int)fs_id, 
		(long long unsigned int)handle);
	/*	Determine index	into hash table	*/
	index = hash(ids_as_string, FILE_TABLE_HASH_MAX);
	current = &(ftbl->file[index]);	
	/*	Insert at index, relocating head data if necessary.	*/
	if(current->mtbl_blk!=NIL && current->mtbl_ent!=NIL){ /* relocating! */
		/*	get free file entry and update ftbl	*/
		uint16_t free_fent = get_free_fent();
		if(free_fent!=NIL){
			/*	copy data @ index to new file entry	*/
			memcpy ((void *)&(ftbl->file[free_fent]),
				(void *)&current,
				sizeof(struct file_ent_s));		
			/*	insert file data @ index	*/
			current->tag_id = fs_id;
			current->tag_handle = handle;
			/*	point new head's "next" to former head	*/
			current->next = free_fent;	
		}
		else{	/*	No free file entries available: policy?	*/
			/*	Evict or return NIL	*/
			return (struct mem_ent_s *)NIL;
		}
	}
	/*	Get next free mem_table	*/
	uint32_t free_mtbl_blk;
	uint16_t free_mtbl_ent;	
	/* Is there a free memory table available? */
	if(get_next_free_mtbl(&free_mtbl_blk, &free_mtbl_ent)){
		/*	Update fent with it's new mtbl: blk and ent */
		current->mtbl_blk = free_mtbl_blk;
		current->mtbl_ent = free_mtbl_ent;
		/*	Initialize Memory Table	*/
		init_memory_table(free_mtbl_blk, free_mtbl_ent);

		/* Copy data from file into mem entries	*/
		//... then return



		return &(ucache->b[current->mtbl_blk].mtbl[
			current->mtbl_ent].mem[0]);
	}
	else{	/*	No free mtbl available: policy?	*/
		/*	Evict or return NIL	*/
		return (struct mem_ent_s *)NIL;
	}
}

static int remove_file(uint32_t fs_id, uint64_t handle)
{
	//remove the link from the linked list


}

static void *lookup_mem(struct mem_table_s *mtbl, uint64_t offset)
{
}

static void *insert_mem(struct mem_table_s *mtbl, uint64_t offset)
{
}

static int remove_mem(struct mem_table_s *mtbl, uint64_t offset)
{
}

/* externally visible API */
#if 0
int ucache_open_file(PVFS_object_ref *handle)
{
}

void *ucache_lookup(PVFS_object_ref *handle, uint64_t offset)
{
}

void *ucache_insert(PVFS_object_ref *handle, uint64_t offset)
{
}

int ucache_remove(PVFS_object_ref *handle, uint64_t offset)
{
}

int ucache_flush(PVFS_object_ref *handle)
{
}

int ucache_close_file(PVFS_object_ref *handle)
{
}
#endif
