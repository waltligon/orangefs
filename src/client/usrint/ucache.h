/* 
 * (C) 2011 Clemson University
 *
 * See COPYING in top-level directory.
 */

#ifndef UCACHE_H
#define UCACHE_H

#define UCACHE_ENABLED
#define UCACHE_LOCKING_ENABLED

#include <stdio.h>
#include <sys/shm.h>
#include <stdint.h>
#include <semaphore.h>
#include <pthread.h>

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
#define CACHE_FLAGS (SVSHM_MODE)
#define NIL (-1)

#define DBG 0
#define INTERNAL_TESTING 0


#define LOCK_TYPE 1 /* 0 for Semaphore, 1 for Mutex, 2 for Spinlock */
#if (LOCK_TYPE == 0)
#define ucache_lock_t sem_t
#define LOCK_SIZE sizeof(sem_t)
#elif (LOCK_TYPE == 1)
#define ucache_lock_t pthread_mutex_t /* sizeof(pthread_mutex_t)=24 */
#define LOCK_SIZE sizeof(pthread_mutex_t)
#elif (LOCK_TYPE == 2)
#define ucache_lock_t pthread_spinlock_t
#define LOCK_SIZE sizeof(pthread_spinlock_t)
#endif

typedef uint32_t PVFS_fs_id;
typedef uint64_t PVFS_handle;

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

struct ucache_ref_s
{
    union user_cache_u *ucache;           /* pointer to ucache shmem */
    ucache_lock_t *ucache_locks;    /* pointer to ucache locks */
};

/* externally visible API */
void ucache_initialize(void);
int ucache_open_file(PVFS_fs_id *fs_id, PVFS_handle *handle, 
                                  struct mem_table_s *mtbl);
int ucache_close_file(PVFS_fs_id *fs_id, PVFS_handle *handle);
void *ucache_lookup(PVFS_fs_id *fs_id, PVFS_handle *handle, uint64_t offset);
void *ucache_insert(PVFS_fs_id *fs_id, PVFS_handle *handle, uint64_t offset);
int ucache_remove(PVFS_fs_id *fs_id, PVFS_handle *handle, uint64_t offset);
int ucache_flush(PVFS_fs_id *fs_id, PVFS_handle *handle);
void ucache_dec_ref_cnt(struct mem_table_s * mtbl);
void ucache_inc_ref_cnt(struct mem_table_s * mtbl);
void ucache_info(
    FILE * out,
    union user_cache_u * ucache, 
    ucache_lock_t * ucache_lock
);
ucache_lock_t * get_block_lock(int block_index);

#if LOCK_TYPE==0
int ucache_lock_getvalue(ucache_lock_t * lock, int *sval);
#endif /* LOCK_TYPE */
/****************************************  End of Internal Only Functions    */
#endif /* UCACHE_H */
/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

