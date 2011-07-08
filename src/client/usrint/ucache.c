#include "ucache.h"

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

	/*	Note: had to set: kernel.shmmax amd kernel.shmall	*/

	/* set up shared memory region */
	key_file_path = GET_KEY_FILE;
	key = ftok(key_file_path, PROJ_ID);
	printf("key:\t\t\t0x%x\n", key);
	id = shmget(key, CACHE_SIZE, CACHE_FLAGS);
	printf("id:\t\t\t%d\n", id);
	ucache = shmat(id, NULL, AT_FLAGS);
	printf("ucache ptr:\t\t0x%x\n", (unsigned int)ucache);
	ucache_blk_cnt = BLOCKS_IN_CACHE;

	/*	Error Reporting	*/

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
		//printf("next = %u\n", ucache->ftbl.file[i].next);
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
	uint32_t ret = ftbl->free_blk;
	if((int32_t)ret!=NIL){
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
	printf("trying to get free file entry...\n");
	struct file_table_s *ftbl = &(ucache->ftbl);
	uint16_t ret = ftbl->free_list;
	if((int16_t)ret!=NIL){
		ftbl->free_list = ftbl->file[ret].next;
		printf("\tfree file entry index = %d\n", ret);
		return ret;
	}
	else{
		/*	EVICT?	*/
		printf("\terror getting free file entry...\n");
		return NIL;
	}
}

static void put_free_fent(int fent)
{
	printf("freeing file entry = %d\n", fent);
	struct file_table_s *ftbl = &(ucache->ftbl);
	ftbl->file[fent].tag_handle = NIL;
	ftbl->file[fent].tag_id = NIL;
	printf("\ttag_id\t\t0X%X\n", NIL);
	printf("\ttag_handle\t0X%llX\n", ftbl->file[fent].tag_handle);
	if(fent>(FILE_TABLE_HASH_MAX-1)){
		ftbl->file[fent].next = ftbl->free_list;
		ftbl->free_list = fent;
	}
	else{
		ftbl->file[fent].next = NIL;
	}
}

static int get_free_ment(struct mem_table_s *mtbl)
{
	uint16_t ret = mtbl->free_list;
	if((int16_t)ret!=NIL){
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

static struct mem_table_s *lookup_file(
	uint32_t fs_id, 
	uint64_t handle,
	uint32_t *file_mtbl_blk,	/* Can be NULL if not desired	*/
	uint16_t *file_mtbl_ent,	/* Can be NULL if not desired	*/
	uint16_t *file_ent_index	/* Can be NULL if not desired	*/
)
{
	printf("performing lookup...\n");
	struct file_table_s *ftbl = &(ucache->ftbl);
	struct file_ent_s *current;	/* Current ptr for iteration	*/
	int index;			/* Index into file hash table	*/
	index = handle % FILE_TABLE_HASH_MAX;
	printf("\thashed index: %d\n", index);
	if(file_ent_index){
		*file_ent_index = index;
	}
	current = &(ftbl->file[index]);
	/*	Is there a link?	*/
	printf("\tcurrent->next = %d\n", (int16_t)current->next);
	if((int16_t)current->next!=-1){
		/*	Iterate and examine the fs_id and handle.
		*	stop when matched or next is NIL	*/
		while(1){
			if(current->tag_id==fs_id && 
				current->tag_handle==handle){
				printf("\tFile located in chain\n");
				if(file_mtbl_blk!=NULL){
					*file_mtbl_blk = current->mtbl_blk;
				}
				if(file_mtbl_ent!=NULL){
					*file_mtbl_ent = current->mtbl_ent;
				}
				return (struct mem_table_s *)&(ucache->b[
					current->mtbl_blk].mtbl[
					current->mtbl_ent]);
			}
			if(file_ent_index){
				*file_ent_index = current->next;
			}
			if((int16_t)current->next!=-1){
				current = &(ftbl->file[current->next]);	
				printf("\tIterating across the chain, next=%d\n", current->next);	
			}
			else{
				break;
			}
		}
		printf("\tlookup error: mtbl not found1\n");
		return (struct mem_table_s *)NIL;
	}
	else{
		printf("\tno chain detected\n");
		printf("\tcurrent->tag_id\t\t0X%X\n\tfs_id\t\t\t0X%X\n", current->tag_id, fs_id);
		printf("\tcurrent->tag_handle\t0X%llX\n\thandle\t\t\t0X%llX\n", current->tag_handle, handle);
		if(current->tag_id==fs_id && current->tag_handle==handle){
			if(file_mtbl_blk!=NULL){
				*file_mtbl_blk = current->mtbl_blk;
			}
			if(file_mtbl_ent!=NULL){
				*file_mtbl_ent = current->mtbl_ent;
			}
			printf("\tfile entry match\n");
			return (struct mem_table_s *)&(ucache->b[
				current->mtbl_blk].mtbl[current->mtbl_ent]);
		}
		printf("\tlookup error: mtbl not found2!\n");
		return (struct mem_table_s *)NIL;
	} 	 
}

static int get_next_free_mtbl(uint32_t *free_mtbl_blk, uint16_t *free_mtbl_ent){
		struct file_table_s *ftbl = &(ucache->ftbl);
		/*	get next free mtbl_blk and ent	*/
		*free_mtbl_blk = ftbl->free_mtbl_blk;
		*free_mtbl_ent = ftbl->free_mtbl_ent;
		/* is free mtbl_blk available? */
		if((int32_t)*free_mtbl_blk!=NIL && (int16_t)*free_mtbl_ent!=NIL){ 
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

static struct mem_table_s *insert_file(uint32_t fs_id, uint64_t handle)
{
	printf("trying to insert file...\n");
	struct file_table_s *ftbl = &(ucache->ftbl);
	struct file_ent_s *current;	/* Current ptr for iteration	*/
	/*	index into file hash table	*/
	int index = handle % FILE_TABLE_HASH_MAX;
	printf("\thashed index: %d\n", index);
	current = &(ftbl->file[index]);	
	/*	Insert at index, relocating head data if necessary.	*/

	/*	Need to relocate data?	*/
	if((int32_t)current->mtbl_blk!=NIL && (int16_t)current->mtbl_ent!=NIL){ /* relocating! */
		printf("\tmust relocate head data\n");
		/*	get free file entry and update ftbl	*/
		uint16_t free_fent = get_free_fent();
		if((int16_t)free_fent!=NIL){
			/*	copy data from 1 struct to the other	*/
			ftbl->file[free_fent] = *current;	
			/*	These should match	*/
			/*	point new head's "next" to former head	*/
			current->next = free_fent;	
			printf("\tnew head's next = %d\n", current->next);
		}
		else{	/*	No free file entries available: policy?	*/
			/*	Evict or return NIL	*/
			printf("\terror: no free file entries");
			return (struct mem_table_s *)NIL;
		}
	}
	else{
		printf("\tno head data @ index\n");
	}
	/*	Get next free mem_table	*/
	uint32_t free_mtbl_blk;
	uint16_t free_mtbl_ent;	
	/* Is there a free memory table available? */
	if(get_next_free_mtbl(&free_mtbl_blk, &free_mtbl_ent)==1){
		/*	insert file data @ index	*/
		current->tag_id = fs_id;
		current->tag_handle = handle;
		/*	Update fent with it's new mtbl: blk and ent */
		current->mtbl_blk = free_mtbl_blk;
		current->mtbl_ent = free_mtbl_ent;

		//current->next = NIL;
		/*	Initialize Memory Table	*/
		init_memory_table(free_mtbl_blk, free_mtbl_ent);
		printf("\trecieved free memory table: 0X%X\n", 
			(unsigned int)&(ucache->b[current->mtbl_blk].mtbl[
			current->mtbl_ent]));
		return &(ucache->b[current->mtbl_blk].mtbl[
			current->mtbl_ent]);
	}
	else{	/*	No free mtbl available: policy?	*/
		/*	Evict or return NIL	*/
		printf("\terror: no free memory tables available");
		return (struct mem_table_s *)NIL;
	}
}

//Needs work
static int remove_file(uint32_t fs_id, uint64_t handle)
{
	printf("trying to remove file...\n");
	struct file_table_s *ftbl = &(ucache->ftbl);
	int32_t file_mtbl_blk;
	int16_t file_mtbl_ent;
	int16_t file_ent_index;
	struct mem_table_s *mtbl = lookup_file(fs_id, handle, 
		&file_mtbl_blk, &file_mtbl_ent, &file_ent_index);
	/*	Verify we've recieved the necessary info	*/
	if((int32_t)file_mtbl_blk==NIL || (int16_t)file_mtbl_ent==NIL){
		printf("\tremoval error: no matching mtbl\n");
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
	put_free_fent(file_ent_index);
	printf("\tremoval sucessful\n");
	//print the values to ensure they really got set
	printf("\ttag_id\t\t0X%X\n", ftbl->file[file_ent_index].tag_id);
	printf("\ttag_handle\t0X%llX\n", ftbl->file[file_ent_index].tag_handle);

	return(1);
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

void simple_test_1(void){
	ucache_initialize();
	/*	Check Global Data	*/
	printf("address of ucache ptr:\t0x%x\n", (unsigned int)&ucache);
	printf("ucache ptr:\t\t0x%x\n", (unsigned int)ucache);
	printf("ftbl ptr:\t\t0x%x\n", (unsigned int)&(ucache->ftbl));
	printf("cache initialized...\n\n");
	lookup_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAAA, NULL, NULL, NULL);
	insert_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAAA);
	lookup_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAAA, NULL, NULL, NULL);
	remove_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAAA);
	lookup_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAAA, NULL, NULL, NULL);
}

void simple_test_2(void){
	ucache_initialize();
	/*	Check Global Data	*/
	printf("address of ucache ptr:\t0x%x\n", (unsigned int)&ucache);
	printf("ucache ptr:\t\t0x%x\n", (unsigned int)ucache);
	printf("ftbl ptr:\t\t0x%x\n", (unsigned int)&(ucache->ftbl));
	printf("cache initialized...\n\n");
	lookup_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAAA, NULL, NULL, NULL);
	lookup_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAC9, NULL, NULL, NULL);
	lookup_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAA8B, NULL, NULL, NULL);
	insert_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAAA);
	insert_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAC9);
	insert_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAA8B);
	lookup_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAAA, NULL, NULL, NULL);
	lookup_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAC9, NULL, NULL, NULL);
	lookup_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAA8B, NULL, NULL, NULL);
	remove_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAAA);
	remove_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAC9);
	remove_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAA8B);
	lookup_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAAA, NULL, NULL, NULL);
	lookup_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAC9, NULL, NULL, NULL);
	lookup_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAA8B, NULL, NULL, NULL);
}

/*	Note: When unsigned ints are set to NIL, their values are based on type:
		ex:	16		0xFFFF	
			32		0XFFFFFFFF
			64		0XFFFFFFFFFFFFFFFF 
		ALL EQUAL THE SIGNED REPRESENTATION OF -1, CALLED NIL.	*/
int main(){
	//simple_test_1();
	simple_test_2();
	return 0;
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
