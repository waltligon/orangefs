/* 
 * (C) 2011 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#ifndef UCACHE_INTERNAL_H
#define UCACHE_INTERNAL_H

#include <stdio.h>
#include <sys/shm.h>
#include <stdint.h>
#include <semaphore.h>

/* The following includes may end up not being needed. 
#include <sys/types.h>
#include <sys/sem.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
*/

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
#define AT_FLAGS 0
#define SVSHM_MODE (SHM_R | SHM_W | SHM_R>>3 | SHM_R>>6)
#define CACHE_FLAGS (SVSHM_MODE | IPC_CREAT)
#define NIL (-1)

#define DBG 0
#define INTERNAL_TESTING 0
#define ucache_lock_t sem_t

typedef uint32_t PVFS_fs_id;
typedef uint64_t PVFS_object_ref;

/** A link for one block of memory in a files hash table
 *
 */
/* 20 bytes */
struct mem_ent_s
{
    uint64_t tag;       /* offset of data block in file */
    uint32_t item;      /* index of cache block with data */
    uint16_t next;      /* use for hash table chain */
    uint16_t dirty_next;    /* if dirty used in dirty list */
    uint16_t lru_next;  /* used in lru list */
    uint16_t lru_prev;  /* used in lru list */
};

/** A cache for a specific file
 *
 *  Keyed on the address of the block of memory
 */
struct mem_table_s
{
    uint16_t num_blocks;    /* number of used blocks in this mtbl */
    uint16_t free_list; /* index of next free mem entry */
    uint16_t free_list_blk; /* only used when mtbl is on mtbl free list */
    uint16_t lru_first; /* index of first block on lru list */
    uint16_t lru_last;  /* index of last block on lru list */
    uint16_t dirty_list;    /* index of first dirty block */
    uint16_t ref_cnt;   /* number of clients using this record */
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
    uint64_t tag_handle;    /* PVFS_handle */
    uint32_t tag_id;    /* PVFS_fs_id */
    uint32_t mtbl_blk;  /* block index of this mtbl */
    uint16_t mtbl_ent;  /* entry index of this mtbl */
    uint16_t next;      /* next mtbl in chain */
};

/** A hash table to find caches for specific files
 *
 *  Keyed on fs_id/handle of the file
 */
struct file_table_s
{
    uint32_t free_blk;  /* index of the next free block */
    uint32_t free_mtbl_blk; /* block index of next free mtbl */
    uint16_t free_mtbl_ent; /* entry index of next free mtbl */
    uint16_t free_list; /* index of next free file entry */
    /*  pthread_spinlock_t spinlock */
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

/* externally visible API */
extern void ucache_initialize(void);
extern int ucache_open_file(PVFS_fs_id *fs_id, PVFS_object_ref *handle);
extern void *ucache_lookup(PVFS_fs_id *fs_id, PVFS_object_ref *handle, uint64_t offset);
extern void *ucache_insert(PVFS_fs_id *fs_id, PVFS_object_ref *handle, uint64_t offset);
extern int ucache_remove(PVFS_fs_id *fs_id, PVFS_object_ref *handle, uint64_t offset);
extern int ucache_flush(PVFS_fs_id *fs_id, PVFS_object_ref *handle);
extern int ucache_close_file(PVFS_fs_id *fs_id, PVFS_object_ref *handle);

/* Internal Only Function Declarations   */
    /*  Cache Locking Functions */
static int ucache_lock_init(ucache_lock_t * lock);
static int ucache_lock_lock(ucache_lock_t * lock);
static int ucache_lock_unlock(ucache_lock_t * lock);
static int ucache_lock_getvalue(ucache_lock_t * lock, int *sval);
static int ucache_lock_destroy(ucache_lock_t * lock);

    /*  Dirty List Iterator */
static int dirty_done(uint16_t index);
static int dirty_next(struct mem_table_s *mtbl, uint16_t index);

    /*  Memory Entry Chain Iterator */
static int ment_done(int index);
static int ment_next(struct mem_table_s *mtbl, int index);

    /*  File Entry Chain Iterator   */
static int file_done(int index);
static int file_next(struct file_table_s *ftbl, int index);

static void add_free_mtbls(int blk);
static void init_memory_table(int blk, int ent);
static uint16_t get_free_blk(void);
static void put_free_blk(int blk);
static int get_free_fent(void);
static void put_free_fent(int fent);
static int get_free_ment(struct mem_table_s *mtbl);
static void put_free_ment(struct mem_table_s *mtbl, int ent);
static struct mem_table_s *lookup_file(
    uint32_t fs_id, 
    uint64_t handle,
    uint32_t *file_mtbl_blk,        /* Can be NULL if not desired   */
    uint16_t *file_mtbl_ent,        /* Can be NULL if not desired   */
    uint16_t *file_ent_index,   /* Can be NULL if not desired   */
    uint16_t *file_ent_prev_index   /* Can be NULL if not desired   */
);
static int get_next_free_mtbl(uint32_t *free_mtbl_blk, uint16_t *free_mtbl_ent);
static void remove_all_memory_entries(struct mem_table_s *mtbl);
static void put_free_mtbl(struct mem_table_s *mtbl, struct file_ent_s *file);
static void evict_file(unsigned int index);
static struct mem_table_s *insert_file(uint32_t fs_id, uint64_t handle);
static int remove_file(uint32_t fs_id, uint64_t handle);
static void *lookup_mem(struct mem_table_s *mtbl, 
                    uint64_t offset, 
                    uint32_t *item_index,
                    uint16_t *mem_ent_index,
                    uint16_t *mem_ent_prev_index
);
static void update_lru(struct mem_table_s *mtbl, uint16_t index);
static int locate_max_mtbl(struct mem_table_s **mtbl);
static void evict_LRU(struct mem_table_s *mtbl);
static void *set_item(struct mem_table_s *mtbl, 
                    uint64_t offset, 
                    uint16_t index
);
static void *insert_mem(struct mem_table_s *mtbl, uint64_t offset);
static int remove_mem(struct mem_table_s *mtbl, uint64_t offset);
    /*  list printing functions */
static void print_lru(struct mem_table_s *mtbl);
static void print_dirty(struct mem_table_s *mtbl);
/****************************************  End of Internal Only Functions    */
#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

