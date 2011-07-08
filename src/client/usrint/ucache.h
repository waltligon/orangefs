#ifndef UCACHE_H
#define UCACHE_H 1

#include <sys/types.h>
#include <sys/shm.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

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
#define SVSHM_MODE   (SHM_R | SHM_W | SHM_R>>3 | SHM_R>>6) 
#define CACHE_FLAGS (SVSHM_MODE | IPC_CREAT)

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

#endif 


