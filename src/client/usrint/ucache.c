#include <sys/types.h>
#include <sys/shm.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>

#define MEM_TABLE_ENTRY_COUNT 818
#define FILE_TABLE_ENTRY_COUNT 818
#define CACHE_BLOCK_SIZE_K 256
#define MEM_TABLE_HASH_MAX 31
#define FILE_TABLE_HASH_MAX 31
#define MTBL_PER_BLOCK 16
#define GET_KEY_FILE "/etc/fstab"
#define PROJ_ID 61
#define BLOCKS_IN_CACHE 512
#define CACHE_SIZE (CACHE_BLOCK_SIZE_K * 1024 * BLOCKS_IN_CACHE)
#define CACHE_FLAGS 0
#define AT_FLAGS 0

#define NIL (-1)

/** A link for one block of memory in a files hash table
 *
 */
/* 20 bytes */
struct mem_ent_s
{
	uint64_t tag;		/* offset of data block in file */
	uint32_t item;		/* index of cache block with data */
	uint16_t next;		/* use for hash table chain */
	uint16_t dirty_next;	/* if dirty used in dirty list */
	uint16_t lru_next;	/* used in lru list */
	uint16_t lur_prev;	/* used in lru list */
};

/** A cache for a specific file
 *
 *  Keyed on the address of the block of memory
 */
struct mem_table_s
{
	uint16_t num_blocks;	/* number of used blocks in this mtbl */
	uint16_t free_list;	/* index of next free mem entry */
	uint16_t free_list_blk;	/* only used when mtbl is on mtbl free list */
	uint16_t lru_first;	/* index of first block on lru list */
	uint16_t lru_last;	/* index of last block on lru list */
	uint16_t dirty_list;	/* index of first dirty block */
	uint16_t ref_cnt;	/* number of clients using this record */
	char pad[12];
	struct mem_ent_s mem[MEM_TABLE_ENTRY_COUNT];
};

/** One allocation block in the cache
 *
 *  Either a block of memory or a block of mtbls
 */
union cache_block_u
{
	struct mem_table_s mtbl[MTBL_PER_BLOCK];
	char mblk[CACHE_BLOCK_SIZE_K*1024];
};

/** A link for one file in the top level hash table
 *
 */
/* 20 bytes */
struct file_ent_s
{
	uint64_t tag_handle;	/* PVFS_handle */
	uint32_t tag_id;	/* PVFS_fs_id */
	uint32_t mtbl_blk;	/* block index of this mtbl */
	uint16_t mtbl_ent;	/* entry index of this mtbl */
	uint16_t next;		/* next mtbl in chain */
};

/** A hash table to find caches for specific files
 *
 *  Keyed on fs_id/handle of the file
 */
struct file_table_s
{
	uint32_t free_blk;	/* index of the next free block */
	uint32_t free_mtbl_blk;	/* block index of next free mtbl */
	uint16_t free_mtbl_ent;	/* entry index of next free mtbl */
	uint16_t free_list;	/* index of next free file entry */
	char pad[12];
	struct file_ent_s file[FILE_TABLE_ENTRY_COUNT];
};

/** The whole system wide cache
 *
 */
union user_cache_u
{
	struct file_table_s ftbl;
	union cache_block_u b[0]; /* actual size of this varies */
};

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
	ucache->b[blk].mtbl[0].free_list_blk = NIL;	/*	necessary?	*/
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

/** Hash Function - also in quickhash library in: pvfs2/src/common/quickhash/quickhash.h
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

	char str[25]; //94/4 + 1
	int index;

	//convert identifiers into concatenated hex string to be sent to hash function  	
	sprintf(str, "%08lX%016llX", (long unsigned int)fs_id, (long long unsigned int)handle);

	//hash info and determine index
	index = hash(str, FILE_TABLE_HASH_MAX);

	//keep current ptr
	struct file_ent_s *current = &(ftbl->file[index]);

	if(current->mtbl_blk!=NIL && current->mtbl_ent!=NIL){
		//iterate ..examining the fs_id and handles
		while(current!=0){//stop when ids match or next is NIL
			if(current->tag_id==fs_id && current->tag_handle==handle){//ids match
				return (struct mem_table *)&(ucache->b[current->mtbl_blk].mtbl[current->mtbl_ent]);
			}
			current = &(ftbl->file[current->next]);
		}
		return (struct mem_table *)0;
	}
	else{
		if(current->tag_id==fs_id && current->tag_handle==handle){//ids match
			return (struct mem_table *)&(ucache->b[current->mtbl_blk].mtbl[current->mtbl_ent]);
		}
		return (struct mem_table *)0;
	} 	 
}

static struct mem_ent_s *insert_file(uint32_t fs_id, uint64_t handle)
{
	/* maybe call init_memory_table here */
}

static int remove_file(uint32_t fs_id, uint64_t handle)
{
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
