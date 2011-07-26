#include "ucache.h"

/*	
*	Note: When unsigned ints are set to NIL, their values are based on type:
*	ex:	16		0xFFFF	
*		32		0XFFFFFFFF
*		64		0XFFFFFFFFFFFFFFFF 
*	ALL EQUAL THE SIGNED REPRESENTATION OF -1, CALLED NIL.	
*/

static union user_cache_u *ucache;
static int ucache_blk_cnt;
FILE *out; /*	For Logging Purposes	*/

/*	This function should only be called when the ftbl has no free mtbls	*/
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
	b->mtbl[i].free_list_blk = NIL;
	b->mtbl[i].free_list = NIL;
	ftbl->free_mtbl_blk = blk;
	ftbl->free_mtbl_ent = start_mtbl;
}

void ucache_initialize(void)
{
	int key, id, i;
	char *key_file_path;
	/*	Direct output	*/
	if(!out)out = stdout;
	/*	Note: had to set: kernel.shmmax amd kernel.shmall	*/
	/* set up shared memory region */
	key_file_path = GET_KEY_FILE;
	key = ftok(key_file_path, PROJ_ID);
	id = shmget(key, CACHE_SIZE, CACHE_FLAGS);
	ucache = shmat(id, NULL, AT_FLAGS);
	ucache_blk_cnt = BLOCKS_IN_CACHE;
	if(DBG)
	{
		fprintf(out, "key:\t\t\t0X%X\n", key);
		fprintf(out, "id:\t\t\t%d\n", id);
		fprintf(out, "ucache ptr:\t\t0X%X\n", (unsigned int)ucache);
	}
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
		ucache->ftbl.file[i].next = NIL;
	}
	/* set up list of free hash table entries */
	ucache->ftbl.free_list = FILE_TABLE_HASH_MAX;
	for (i = FILE_TABLE_HASH_MAX; i < FILE_TABLE_ENTRY_COUNT - 1; i++)
	{
		ucache->ftbl.file[i].mtbl_blk = NIL;
		ucache->ftbl.file[i].mtbl_ent = NIL;
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
		mtbl->mem[i].tag = NIL;
		mtbl->mem[i].next = NIL;
		mtbl->mem[i].lru_prev = NIL;
		mtbl->mem[i].lru_next = NIL;
	}
	/* set up list of free hash table entries */
	mtbl->free_list = MEM_TABLE_HASH_MAX;
	for (i = MEM_TABLE_HASH_MAX; i < MEM_TABLE_ENTRY_COUNT - 1; i++)
	{
		mtbl->mem[i].next = i+1;
	}
	mtbl->mem[MEM_TABLE_ENTRY_COUNT - 1].next = NIL;
}

static uint16_t get_free_blk(void)
{
	struct file_table_s *ftbl = &(ucache->ftbl);
	uint16_t free_blk = ftbl->free_blk;
	if((int16_t)free_blk!=NIL)
	{	
		/* Use zero since free_blks have no ititialized mem tables */
 		ftbl->free_blk = ucache->b[free_blk].mtbl[0].free_list; 
		return free_blk;
	}
	return NIL;

}

static void put_free_blk(int blk)
{
	if(DBG)fprintf(out, "freeing blk @ index = %d\n", blk);
	struct file_table_s *ftbl = &(ucache->ftbl);
	ucache->b[blk].mtbl[0].free_list = ftbl->free_blk;
	ucache->b[blk].mtbl[0].free_list_blk = NIL;	
	ftbl->free_blk = blk;
}

static int get_free_fent(void)
{
	if(DBG)fprintf(out, "trying to get free file entry...\n");
	struct file_table_s *ftbl = &(ucache->ftbl);
	uint16_t entry = ftbl->free_list;
	if((int16_t)entry!=NIL)
	{
		ftbl->free_list = ftbl->file[entry].next;
		ftbl->file[entry].next = NIL;
		if(DBG)fprintf(out, "\tfree file entry index = %u\n", entry);
		return entry;
	}
	else{
		if(DBG)fprintf(out, "\tno free file entry...\n");
		return NIL;
	}
}

static void put_free_fent(int fent)
{
	if(DBG)fprintf(out, "freeing file entry = %d\n", fent);
	struct file_table_s *ftbl = &(ucache->ftbl);
	ftbl->file[fent].tag_handle = NIL;
	ftbl->file[fent].tag_id = NIL;
	/*	Set fent index as the head of the free_list	*/
	if(fent>(FILE_TABLE_HASH_MAX-1))
	{
		ftbl->file[fent].next = ftbl->free_list;
		ftbl->free_list = fent;
	}
	else
	{
		ftbl->file[fent].next = NIL;
	}
}

static int get_free_ment(struct mem_table_s *mtbl)
{
	uint16_t ret = mtbl->free_list;
	if((int16_t)ret!=NIL)
	{
		mtbl->free_list = mtbl->mem[ret].next;
		mtbl->mem[ret].next = NIL;
		mtbl->mem[ret].lru_prev = NIL;
		mtbl->mem[ret].lru_next = NIL;
		return ret;
	}
	return NIL;
}

static void put_free_ment(struct mem_table_s *mtbl, int ent)
{
	if(DBG)fprintf(out, "freeing memory entry = %d\n", ent);
	/*	Reset ment values	*/
	mtbl->mem[ent].tag = NIL;
	mtbl->mem[ent].item = NIL;
	mtbl->mem[ent].dirty_next = NIL;
	mtbl->mem[ent].lru_next = NIL;
	mtbl->mem[ent].lru_prev = NIL;
	/*	Update free list to include this entry	*/
	if(ent>(MEM_TABLE_HASH_MAX-1))
	{
		mtbl->mem[ent].next = mtbl->free_list;
		mtbl->free_list = ent;
	}
}

static struct mem_table_s *lookup_file(
	uint32_t fs_id, 
	uint64_t handle,
	uint32_t *file_mtbl_blk,		/* Can be NULL if not desired	*/
	uint16_t *file_mtbl_ent,		/* Can be NULL if not desired	*/
	uint16_t *file_ent_index,	/* Can be NULL if not desired	*/
	uint16_t *file_ent_prev_index	/* Can be NULL if not desired	*/
)
{
	int16_t p,c,n;	/*	previous, current, next fent index	*/
	if(DBG)fprintf(out, "performing lookup...\n");
	struct file_table_s *ftbl = &(ucache->ftbl);
	struct file_ent_s *current;	/* Current ptr for iteration	*/
	int index;			/* Index into file hash table	*/
	index = handle % FILE_TABLE_HASH_MAX;
	if(DBG)fprintf(out, "\thashed index: %d\n", index);
	current = &(ftbl->file[index]);
	p=NIL; c = index; n = current->next;
	while(1)
	{
		if(DBG)fprintf(out, "\tp=%d\tc=%d\tn=%d\n", (int)p, (int)c, (int)n);
		if(current->tag_id==fs_id && current->tag_handle==handle)
		{
			if(DBG)fprintf(out, "\tFile located in chain\n");
			/*	If params !NULL, set their values	*/
			if(file_mtbl_blk!=NULL && file_mtbl_ent!=NULL && 
				file_ent_index!=NULL && file_ent_prev_index!=NULL)
			{
					*file_mtbl_blk = current->mtbl_blk;
					*file_mtbl_ent = current->mtbl_ent;
					*file_ent_index = c;
					*file_ent_prev_index = p;
			}
			return (struct mem_table_s *)&(ucache->b[
					current->mtbl_blk].mtbl[
					current->mtbl_ent]);
		}
		else	/*	No match yet	*/
		{
			if((int16_t)current->next==NIL)
			{
				if(DBG)fprintf(out, "\tlookup failure: mtbl not found\n");
				return (struct mem_table_s *)NIL;
			}
			else
			{
				current = &(ftbl->file[current->next]);
				p=c; c=n; n=current->next;
				if(DBG)fprintf(out, 
					"\tIterating across the chain, next=%d\n", 
					(int16_t)current->next);	
			}

		}
	}	 
}

static int get_next_free_mtbl(uint32_t *free_mtbl_blk, uint16_t *free_mtbl_ent)
{
		struct file_table_s *ftbl = &(ucache->ftbl);
		/*	get next free mtbl_blk and ent	*/
		*free_mtbl_blk = ftbl->free_mtbl_blk;
		*free_mtbl_ent = ftbl->free_mtbl_ent;
		/* is free mtbl_blk available? */
		if((int32_t)*free_mtbl_blk!=NIL && (int16_t)*free_mtbl_ent!=NIL)
		{ 
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
		return NIL;
}

/*	evict the file @ index, must be less than FILE_TABLE_HASH_MAX	*/
void evict_file(unsigned int index)
{
	if(DBG)fprintf(out, "evicting data @ index %d...\n", index);
	struct file_ent_s *file = &(ucache->ftbl.file[index]);
	struct mem_table_s *mtbl = 
		&(ucache->b[file->mtbl_blk].mtbl[file->mtbl_ent]);
	/*	remove all ments, including their associated blocks	*/
	int i = 0;
	for(i=0;i<MEM_TABLE_HASH_MAX;i++)
	{
		if(DBG)fprintf(out, "\tremoving memory entry %d\n", i);
		int16_t c,n;
		c = i;
		n = mtbl->mem[i].next;
		while(1)
		{
			struct mem_ent_s *ment = &(mtbl->mem[c]);
			if(DBG)fprintf(out, "\t(tag, item)=(%lld, %d)\n",\
				(int64_t)ment->tag,(int32_t)ment->item);
			if((int64_t)ment->tag==NIL || (int32_t)ment->item==NIL){
				break;
			}
			put_free_blk(ment->item);
			put_free_ment(mtbl, c);
			/*	if we are done with this chain then stop	*/
			if(n==NIL)	
			{
				break;
			}
			else{
				if(DBG)fprintf(out, "\titerating accross mtbl chain...\n");
				c = n;
				n = mtbl->mem[c].next;
			}
		}		
	}
	/*	remove mtbl	*/
	mtbl->num_blocks = NIL;	/* number of used blocks in this mtbl */
	mtbl->lru_first = NIL;	/* index of first block on lru list */
	mtbl->lru_last = NIL;	/* index of last block on lru list */
	mtbl->dirty_list = NIL;	/* index of first dirty block */
	mtbl->ref_cnt = NIL;	/* number of clients using this record */
	/*	add mem_table back to free list	*/
	/*	Temporarily store copy of current head (the new next)	*/
	uint32_t tmp_blk = ucache->ftbl.free_mtbl_blk;
	uint16_t tmp_ent = ucache->ftbl.free_mtbl_ent;
	/*	newly free mtbl becomes new head of free mtbl list	*/
	ucache->ftbl.free_mtbl_blk = file->mtbl_blk;
	ucache->ftbl.free_mtbl_ent = file->mtbl_ent;
	/* Point to the next free mtbl (the former head)	*/
	mtbl->free_list_blk = tmp_blk;
	mtbl->free_list = tmp_ent;
	/*	instead of removing, simply prepare fent for it's next occupant	*/
	file->tag_handle = NIL;
	file->tag_id = NIL;
	file->mtbl_blk = NIL;	/* block index of this mtbl */
	file->mtbl_ent = NIL;	/* entry index of this mtbl */
}

static struct mem_table_s *insert_file(uint32_t fs_id, uint64_t handle)
{
	if(DBG)fprintf(out, "trying to insert file...\n");
	struct file_table_s *ftbl = &(ucache->ftbl);
	struct file_ent_s *current;	/* Current ptr for iteration	*/
	uint16_t free_fent;	/*	Index of next free fent	*/
	/*	index into file hash table	*/
	unsigned int index = handle % FILE_TABLE_HASH_MAX;
	if(DBG)fprintf(out, "\thashed index: %u\n", index);
	current = &(ftbl->file[index]);
	/*	Get next free mem_table	*/
	uint32_t free_mtbl_blk;
	uint16_t free_mtbl_ent;	
	/*	Create free mtbls if none are available	*/
	if(get_next_free_mtbl(&free_mtbl_blk, &free_mtbl_ent)!=1)
	{	
		if((int16_t)ucache->ftbl.free_blk==NIL)	
		{
			/*	Evict a block from mtbl with most mem entries	*/
			struct mem_table_s *max_mtbl;
			int num_blocks = locate_max_mtbl(&max_mtbl);
			if(DBG)fprintf(out, "\tget_free_blk returned NIL, evicting a block from max mtbl which has %d entries\n",max_mtbl->num_blocks);
			evict_LRU(max_mtbl);
		}
		/*	Intitialize remaining blocks' memory tables	*/
		if((int16_t)ucache->ftbl.free_blk!=NIL)
		{
			if(DBG)fprintf(out, "\tadding memory tables to free block\n");
			add_free_mtbls(ucache->ftbl.free_blk);
			get_next_free_mtbl(&free_mtbl_blk, &free_mtbl_ent);
		}
	}
	/*	Insert at index, relocating data @ index if occupied	*/
	if((int32_t)current->mtbl_blk!=NIL && (int16_t)current->mtbl_ent!=NIL) 
	{
		if(DBG)fprintf(out, "\tmust relocate head data\n");
		/*	get free file entry and update ftbl	*/
		free_fent = get_free_fent();
		if((int16_t)free_fent!=NIL)
		{
			/*	copy data from 1 struct to the other	*/
			ftbl->file[free_fent] = *current;	

			/*	point new head's "next" to former head	*/
			current->next = free_fent;	
		}
		else
		{	/*	No free file entries available: policy?	*/
			if(DBG)fprintf(out, "\tno free file entries\n");
			evict_file(index);
		}
	}
	else
	{
		if(DBG)fprintf(out, "\tno head data @ index\n");
	}
	/*	insert file data @ index	*/
	current->tag_id = fs_id;
	current->tag_handle = handle;
	/*	Update fent with it's new mtbl: blk and ent */
	current->mtbl_blk = free_mtbl_blk;
	current->mtbl_ent = free_mtbl_ent;
	/*	Initialize Memory Table	*/
	init_memory_table(free_mtbl_blk, free_mtbl_ent);
	if(DBG)fprintf(out, "\trecieved free memory table: 0X%X\n", 
		(unsigned int)&(ucache->b[current->mtbl_blk].mtbl[
		current->mtbl_ent]));
	return &(ucache->b[current->mtbl_blk].mtbl[
		current->mtbl_ent]);
}

static int remove_file(uint32_t fs_id, uint64_t handle)
{
	if(DBG)fprintf(out, "trying to remove file...\n");
	struct file_table_s *ftbl = &(ucache->ftbl);
	int32_t file_mtbl_blk;
	int16_t file_mtbl_ent;
	int16_t file_ent_index;
	int16_t file_ent_prev_index;
	struct mem_table_s *mtbl = lookup_file(fs_id, handle, 
		&file_mtbl_blk, &file_mtbl_ent, &file_ent_index, 
		&file_ent_prev_index);
	/*	Verify we've recieved the necessary info	*/
	if((int32_t)file_mtbl_blk==NIL || (int16_t)file_mtbl_ent==NIL)
	{
		if(DBG)fprintf(out, "\tremoval failure: no mtbl matching file\n");
		return NIL;
	}
	/*	add mem_table back to free list	*/
	/*	Temporarily store copy of current head (the new next)	*/
	uint32_t tmp_blk = ftbl->free_mtbl_blk;
	uint16_t tmp_ent = ftbl->free_mtbl_ent;
	/*	newly free mtbl becomes new head of free mtbl list	*/
	ftbl->free_mtbl_blk = file_mtbl_blk;
	ftbl->free_mtbl_ent = file_mtbl_ent;
	/* Point to the next free mtbl (the former head)	*/
	mtbl->free_list_blk = tmp_blk;
	mtbl->free_list = tmp_ent;
	/*	Free the file entry	*/
	ftbl->file[file_ent_prev_index].next = ftbl->file[file_ent_index].next;
	put_free_fent(file_ent_index);
	if(DBG)fprintf(out, "\tremoval sucessful\n");
	return(1);
}

static void *lookup_mem(struct mem_table_s *mtbl, 
					uint64_t offset, 
					uint32_t *item_index,
					uint16_t *mem_ent_index,
					uint16_t *mem_ent_prev_index)
{
	if(DBG)fprintf(out, "performing ment lookup...\n");
	/*	index into mem hash table	*/
	unsigned int index = offset % MEM_TABLE_HASH_MAX;
	if(DBG)fprintf(out, "\toffset: 0X%llX hashes to index: %u\n",\
		offset, index);
	struct mem_ent_s *current = &(mtbl->mem[index]);
	/*	previous, current, next memory entry index in mtbl	*/
	int16_t p,c,n;	
	p = NIL; c = index; n = current->next;
	while(1)
	{
		if(offset==current->tag)
		{
			if(DBG)fprintf(out, "\tmatch located: 0X%llX==0X%llX\n",\
				offset, current->tag);
		
			/*	If params !NULL, set their values	*/
			if(item_index!=NULL && mem_ent_index!=NULL && 
				mem_ent_prev_index!=NULL)
			{
					*item_index = current->item;
					*mem_ent_index = c;
					*mem_ent_prev_index = p;
			}
			return (void *)(&ucache->b[current->item].mblk);
		}
		else
		{
			/*	if(DBG)fprintf(out, "\t0X%llX!=0X%llX\n", offset,\ 
				current->tag);	*/
			/*	if next available iterate	*/
			if((int16_t)current->next==NIL)
			{
				if(DBG)fprintf(out, "\tmemory entry not found\n");
				return (struct mem_table_s *)NIL;
			}
			else
			{
				current = &(mtbl->mem[current->next]);
				p=c; c=n; n=current->next;
			}
		}
	}
}

void update_lru(struct mem_table_s *mtbl, uint16_t index)
{
			if(DBG)fprintf(out, "updating lru...\n");
			if((int16_t)index<MEM_TABLE_HASH_MAX)
			{
				if(DBG)fprintf(out, "\t%d<MEM_TABLE_HASH_MAX, not adding to LRU\n", index);
				return;
			}
			/*	update LRU List	*/
			if((int16_t)mtbl->lru_first==NIL && (int16_t)mtbl->lru_last==NIL)
			{
				if(DBG)fprintf(out, "\tsetting lru first and last\n");
				mtbl->lru_first = index;
				mtbl->lru_last = index;
				mtbl->mem[index].lru_prev = NIL;
				mtbl->mem[index].lru_next = NIL;
			}
			else if(mtbl->lru_first==mtbl->lru_last)
			{
				if(mtbl->lru_first!=index)
				{	/*	if not already the first and last	*/
					if(DBG)fprintf(out, "\tinserting second record in	LRU\n");
					/*	point tail.prev to new	*/
					mtbl->mem[mtbl->lru_last].lru_prev = index; 
					/*	point new.prev to nil	*/	
					mtbl->mem[index].lru_prev = NIL;
					/*	point the new.next to the tail	*/		
					mtbl->mem[index].lru_next = mtbl->lru_last;
					/*	point the head to the new	*/	
					mtbl->lru_first = index;					
				}
			}
			else
			{
				if(DBG)fprintf(out,"\trepairing previous LRU links and inserting record in LRU\n");
				/*	repair links	*/	
				if((int16_t)mtbl->mem[index].lru_prev!=NIL)
				{
					mtbl->mem[mtbl->mem[index].lru_prev].lru_next = 
						mtbl->mem[index].lru_next;
				}
				if((int16_t)mtbl->mem[index].lru_next!=NIL)
				{
					mtbl->mem[mtbl->mem[index].lru_next].lru_prev = 	
						mtbl->mem[index].lru_prev;
				}
				/*	update nodes own link	*/
				mtbl->mem[mtbl->lru_first].lru_prev = index; 
				mtbl->mem[index].lru_next = mtbl->lru_first;
				mtbl->mem[index].lru_prev = NIL;
			/*	Finally, establish this entry as the first on lru	*/
				mtbl->lru_first = index;
			}
}

static int locate_max_mtbl(struct mem_table_s **mtbl)
{

	if(DBG)fprintf(out, "locating mtbl with most entries...\n");
	struct file_table_s *ftbl = &(ucache->ftbl);
	uint32_t index_of_max_blk;
	uint16_t index_of_max_ent;
	uint16_t value_of_max = 0;
	int i;
	for(i=0;i<FILE_TABLE_HASH_MAX;i++)
	{
		if(DBG)fprintf(out, "\texamining ftbl index %d\n", i);
		int16_t c,n;
		c = i;
		n = ftbl->file[i].next;
		while(1)
		{
			struct file_ent_s *fent = &(ftbl->file[c]);
			if(DBG)fprintf(out, "\t(blk, ent)=(%d, %d)\n", fent->mtbl_blk,\
				(int16_t)fent->mtbl_ent);
			if((int32_t)fent->mtbl_blk==NIL || (int16_t)fent->mtbl_ent==NIL){
				break;
			}
			struct mem_table_s *current_mtbl = &(ucache->b[fent->mtbl_blk].\
				mtbl[fent->mtbl_ent]);
			if(current_mtbl->num_blocks>value_of_max){
				if(DBG)fprintf(out, "\tmax updated @ %d\n", c);
				*mtbl = current_mtbl;	
				index_of_max_blk = fent->mtbl_blk;
				index_of_max_ent = fent->mtbl_ent;
				value_of_max = (*mtbl)->num_blocks;
			}
			if(n==NIL)
			{
				break;	/*	end of chain reached	*/
			}
			else{
				if(DBG)fprintf(out, "\titerating accross ftbl chain...\n");
				c = n;
				n = ftbl->file[c].next;
			}
		}		
	}
	return value_of_max;
}

void evict_LRU(struct mem_table_s *mtbl)
{
	if(DBG)fprintf(out, "evicting LRU...\n");
	if(mtbl->num_blocks!=0)
	{
		remove_mem(mtbl, mtbl->mem[mtbl->lru_last].tag);
	}
	else
	{	/*	Worst Case	*/
		if(DBG)fprintf(out, "\tno LRU on this mtbl\n");
	}
}

/*	Set the block index where data is stored	*/
static void *set_item(struct mem_table_s *mtbl, 
					uint64_t offset, 
					uint16_t index)
{	
		int16_t free_blk = get_free_blk();
		if(free_blk==NIL)
		{
			if(DBG)fprintf(out, "\tget_free_blk returned NIL, attempting removal of LRU\n");
			evict_LRU(mtbl);
			free_blk = get_free_blk();
		}
		
		if(free_blk==NIL)	
		{
			/*	Evict block from mtbl with most memory entries	*/
			struct mem_table_s *max_mtbl;
			int num_blocks = locate_max_mtbl(&max_mtbl);
			if(DBG)fprintf(out, "\tget_free_blk returned NIL, evicting a block from max mtbl which has %d entries\n",\
				max_mtbl->num_blocks);
			evict_LRU(max_mtbl);
			free_blk = get_free_blk();
		}
		if(free_blk!=NIL)
		{	/*	got block	*/
			/*	increment num_blocks used by this mtbl	*/
			mtbl->num_blocks++;
			update_lru(mtbl, index);
			if(DBG)fprintf(out, "\tsuccessfully inserted memory entry @	blk: %d\n", (int16_t) free_blk);
			/*	set item to block number	*/
			mtbl->mem[index].tag = offset;
			mtbl->mem[index].item = free_blk;
			/*	add to dirty list	*/
			if(DBG)fprintf(out, "\tadding memory entry to dirty list\n");
			mtbl->mem[index].dirty_next = mtbl->dirty_list;
			mtbl->dirty_list = index;
			/*	Return the address of the block where data is stored	*/
			return (void *)&(ucache->b[free_blk]); 
		}
}

static void *insert_mem(struct mem_table_s *mtbl, uint64_t offset)
{
	if(DBG)fprintf(out, "trying to insert mem...\n");
	/*	index into mem hash table	*/
	unsigned int index = offset % MEM_TABLE_HASH_MAX;
	if(DBG)fprintf(out, "\toffset: 0X%llX hashes to index: %u\n",\
		offset, index);
	struct mem_ent_s *current = &(mtbl->mem[index]);
	/*	lookup first	*/
	void *returnValue = lookup_mem(mtbl, offset, NULL, NULL, NULL);
	if((int)returnValue!=NIL)
	{
		if(DBG)fprintf(out, "\tblock for this offset already exists @ 0X%X",\
			(unsigned int)returnValue);
		return returnValue;
	}
	if(DBG)fprintf(out, "\tlookup returned NIL\n");
	if((int64_t)mtbl->mem[index].tag!=NIL)
	{	/*	if head occupied, need to get free ment	*/
		int mentIndex = get_free_ment(mtbl);
		if(mentIndex==NIL)
		{	/*	no free memory entry available	*/
			if(DBG)fprintf(out, "\tno ment\n");
			evict_LRU(mtbl);
			mentIndex = get_free_ment(mtbl);	/*	try to get ment again */
		}
		if(mentIndex!=NIL)
		{	
			if(DBG)fprintf(out, "\tfound free memory entry @ index = %d\n",\
				mentIndex);
			/*	insert after head of chain	*/
			uint16_t next_ment = current->next;
			current->next = mentIndex;
			mtbl->mem[mentIndex].next = next_ment;
			return set_item(mtbl, offset, mentIndex);	
		}
	}
	else
	{	/*	No need to iterate to next in chain, just use head	*/
		return set_item(mtbl, offset, index);					
	}
}

static int remove_mem(struct mem_table_s *mtbl, uint64_t offset)
{
	if(DBG)fprintf(out, "trying to remove memory entry...\n");
	/*	some indexes	*/
	int32_t item_index;
	int16_t mem_ent_index;
	int16_t mem_ent_prev_index;
	void *retValue = lookup_mem(mtbl, offset, &item_index, &mem_ent_index, 
		&mem_ent_prev_index);
	/*	Verify we've recieved the necessary info	*/
	if((int)retValue==NIL)
	{
		if(DBG)fprintf(out, "\tremoval failure: memory entry not found matching offset");
		return NIL;
	}
	/*	Remove from LRU	*/
		/*	update each of adjacent nodes' link	*/
	if((int16_t)mtbl->mem[mem_ent_index].lru_prev!=NIL)
	{
		mtbl->mem[mtbl->mem[mem_ent_index].lru_prev].lru_next = 
			mtbl->mem[mem_ent_index].lru_next;
	}
	if((int16_t)mtbl->mem[mem_ent_index].lru_next!=NIL)
	{
		mtbl->mem[mtbl->mem[mem_ent_index].lru_next].lru_prev = 
			mtbl->mem[mem_ent_index].lru_prev;
	}
	if(mem_ent_index==mtbl->lru_first)
	{	/* is node the head */
		mtbl->lru_first = mtbl->mem[mem_ent_index].lru_next;	
	}
	if(mem_ent_index==mtbl->lru_last)
	{	/* is node the tail */
		mtbl->lru_last = mtbl->mem[mem_ent_index].lru_prev;
	}
	/*	Remove from dirty list if Dirty	*/
	if(DBG)fprintf(out, "\tremoving from dirty list\n");
	uint16_t p, c, n;	/*	Previous, Current, Next	*/
	p = NIL;
	c = mtbl->dirty_list;
	n = mtbl->mem[c].dirty_next;
	while(c!=mem_ent_index)
	{
		/*if(DBG)fprintf(out, "\tp: %d\tc: %d\tn: %d\n", 
			(int16_t)p, (int16_t)c, (int16_t)n);*/
		p = c;
		c = n;
		n = mtbl->mem[c].dirty_next;
		if((int16_t)c == NIL)break;
	}
	if((int16_t)c != NIL)
	{	/*	if memory entry was located on the dirty_list	*/
		mtbl->mem[p].dirty_next = mtbl->mem[c].dirty_next;
		mtbl->mem[c].dirty_next = NIL;
	}
	/*	add memory block back to free list	*/
	put_free_blk(item_index);
	/*	Repair link	*/
	if((int16_t)mem_ent_prev_index!=NIL)
	{
		mtbl->mem[mem_ent_prev_index].next = mtbl->mem[mem_ent_index].next;
	}
	/*	newly free mem entry becomes new head of free mem entry list if less 
		than hash table max	*/
	put_free_ment(mtbl, mem_ent_index);
	mtbl->num_blocks--;
	if(DBG)fprintf(out, "\tmemory entry removal sucessful\n");
	return(1);
}

void print_lru(struct mem_table_s *mtbl)
{
	if(DBG)fprintf(out, "\tprinting lru list:\n");
	if(DBG)fprintf(out, "\t\tlru: %d\n", (int16_t)mtbl->lru_last);
	uint16_t current= mtbl->lru_last; 
	while((int16_t)current!=NIL)
	{
		current = mtbl->mem[current].lru_prev;
		if(DBG)fprintf(out, "\t\tprev: %d\n", (int16_t)current); 
	}
}

void print_dirty(struct mem_table_s *mtbl)
{
	if(DBG)fprintf(out, "\tprinting dirty list:\n");
	if(DBG)fprintf(out, "\t\tdirty_list head: %d\n",\
		(int16_t)mtbl->dirty_list);
	uint16_t current= mtbl->dirty_list;
	while((int16_t)current!=NIL)
	{
		if(DBG)fprintf(out, "\t\tcurrent: %d\tnext: %d\n", 
			(int16_t)current, 
			(int16_t)mtbl->mem[current].dirty_next); 
		current = mtbl->mem[current].dirty_next;
	}
}

