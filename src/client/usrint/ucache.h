/* 
 * (C) 2011 Clemson University
 *
 * See COPYING in top-level directory.
 */

/** 
 * \file 
 * \ingroup usrint
 * ucache routines
 */
#ifndef UCACHE_H
#define UCACHE_H 1

#include <stdint.h>
#include <pthread.h>
#include <sys/shm.h>

#define MEM_TABLE_ENTRY_COUNT 679
#define FILE_TABLE_ENTRY_COUNT 512 
#define CACHE_BLOCK_SIZE_K 256
#define CACHE_BLOCK_SIZE (CACHE_BLOCK_SIZE_K * 1024)
#define MEM_TABLE_HASH_MAX 31
#define FILE_TABLE_HASH_MAX 31
#define MTBL_PER_BLOCK 16
#define KEY_FILE "/etc/fstab"
#define SHM_ID1 'l'
#define SHM_ID2 'm'
#ifndef BLOCKS_IN_CACHE 
# define BLOCKS_IN_CACHE 1024
#endif
#define CACHE_SIZE (CACHE_BLOCK_SIZE * BLOCKS_IN_CACHE)
#define AT_FLAGS 0
#define SVSHM_MODE (SHM_R | SHM_W | SHM_R>>3 | SHM_R>>6)
#define CACHE_FLAGS (SVSHM_MODE)
#define NIL (-1)

#ifndef UCACHE_MAX_BLK_REQ 
# define UCACHE_MAX_BLK_REQ MEM_TABLE_ENTRY_COUNT
#endif

#ifndef UCACHE_MAX_REQ 
# define UCACHE_MAX_REQ (CACHE_BLOCK_SIZE * UCACHE_MAX_BLK_REQ)
#endif

/* Define multiple NILS to there's no need to cast for different types */
#define NIL8  0XFF
#define NIL16 0XFFFF
#define NIL32 0XFFFFFFFF
#define NIL64 0XFFFFFFFFFFFFFFFF
#if (PVFS2_SIZEOF_VOIDP == 32)
# define NILP NIL32
#elif (PVFS2_SIZEOF_VOIDP == 64)
# define NILP NIL64
#endif


#ifndef DBG
#define DBG 0 
#endif

#ifndef UCACHE_LOG_FILE
#define UCACHE_LOG_FILE "/tmp/ucache.log"
#endif

/* TODO: set this to an appropriate value. */
#define GOSSIP_UCACHE_DEBUG 0x0010000000000000

#ifndef LOCK_TYPE
#define LOCK_TYPE 3 /* 0 for Semaphore, 1 for Mutex, 2 for Spinlock */
#endif

#if (LOCK_TYPE == 0)
# include <semaphore.h>
# define ucache_lock_t sem_t
# define LOCK_SIZE sizeof(sem_t)
#elif (LOCK_TYPE == 1)
# define ucache_lock_t pthread_mutex_t /* sizeof(pthread_mutex_t)=24 */
# define LOCK_SIZE sizeof(pthread_mutex_t)
#elif (LOCK_TYPE == 2)
# define ucache_lock_t pthread_spinlock_t
# define LOCK_SIZE sizeof(pthread_spinlock_t)
#elif (LOCK_TYPE == 3)
# define ucache_lock_t gen_mutex_t
# define LOCK_SIZE sizeof(gen_mutex_t)
#endif

#define LOCKS_SIZE ((LOCK_SIZE) * (BLOCKS_IN_CACHE + 1))

#define UCACHE_STATS_64 3
#define UCACHE_STATS_16 2
/* This is the size of the ucache_aux auxilliary shared mem segment */
#define UCACHE_AUX_SIZE ( LOCKS_SIZE + (UCACHE_STATS_64 * 8) + \
    (UCACHE_STATS_16 * 2))

/* Globals */
extern FILE * out;
extern int ucache_enabled;
extern union ucache_u *ucache;
extern struct ucache_aux_s *ucache_aux;
extern ucache_lock_t *ucache_locks;
extern ucache_lock_t *ucache_lock;
extern struct ucache_stats_s *ucache_stats;
extern struct ucache_stats_s these_stats;

/** A structure containing the statistics summarizing the ucache. 
 *
 */
struct ucache_stats_s
{
    uint64_t hits;
    uint64_t misses;
    uint64_t pseudo_misses;
    uint16_t block_count;
    uint16_t file_count;
};

/** A structure containing the auxilliary data required by ucache to properly
 * function.
 */
struct ucache_aux_s
{
    ucache_lock_t ucache_locks[BLOCKS_IN_CACHE + 1]; /* +1 for global lock */
    struct ucache_stats_s ucache_stats; /* Summary Statistics of ucache */
};

/** A link for one block of memory in a files hash table
 *
 */
/* 24 bytes */
struct mem_ent_s
{
    uint64_t tag;           /* offset of data block in file */
    uint16_t item;          /* index of cache block with data */
    uint16_t next;          /* use for hash table chain */
    uint16_t dirty_next;    /* if dirty used in dirty list */
    uint16_t lru_prev;      /* used in lru list */
    uint16_t lru_next;      /* used in lru list */
    char pad[6];
};

/** A cache for a specific file
 *
 *  Keyed on the address of the block of memory
 */
struct mem_table_s
{
    uint16_t num_blocks;        /* number of used blocks in this mtbl */
    uint16_t free_list;         /* index of next free mem entry */
    uint16_t free_list_blk;     /* used when mtbl is on mtbl free list and to track free blks */
    uint16_t lru_first;         /* index of first block on lru list */
    uint16_t lru_last;          /* index of last block on lru list */
    uint16_t dirty_list;        /* index of first dirty block */
    uint16_t ref_cnt;           /* number of clients using this record */
    uint16_t bucket[MEM_TABLE_HASH_MAX]; /* bucket may contain index of ment */
    char pad[4];
    struct mem_ent_s mem[MEM_TABLE_ENTRY_COUNT];
    char pad2[8];
};

/** One allocation block in the cache
 *
 *  Either a block of memory or a block of mtbls
 */
union cache_block_u
{
    struct mem_table_s mtbl[MTBL_PER_BLOCK];
    char mblk[CACHE_BLOCK_SIZE_K * 1024];
}; 

/** A link for one file in the top level hash table
 *
 */
/* 32 bytes */
struct file_ent_s
{
    uint64_t tag_handle;    /* PVFS_handle */
    uint32_t tag_id;        /* PVFS_fs_id */
    uint16_t mtbl_blk;      /* block index of this mtbl */
    uint16_t mtbl_ent;      /* entry index of this mtbl */
    uint16_t next;          /* next fent in chain */
    uint16_t index;         /* fent index in ftbl */
    uint64_t size;          /* cache maintenance of file size */
    char pad[4];
};

/** A hash table to find caches for specific files
 *
 *  Keyed on fs_id/handle of the file
 */
struct file_table_s
{
    uint16_t free_blk;  /* index of the next free block */
    uint16_t free_mtbl_blk; /* block index of next free mtbl */
    uint16_t free_mtbl_ent; /* entry index of next free mtbl */
    uint16_t free_list; /* index of next free file entry */
    char pad[8];
    struct file_ent_s file[FILE_TABLE_ENTRY_COUNT];
};

/** The whole system wide cache
 *
 */
union ucache_u
{
    struct file_table_s ftbl;
    union cache_block_u b[0]; /* actual size of this varies */
};

struct ucache_ref_s
{
    union ucache_u *ucache;     /* pointer to ucache shmem */
    ucache_lock_t *ucache_locks;    /* pointer to ucache locks */
};

/* externally visible API */
union ucache_u *get_ucache(void);
int ucache_initialize(void);
int ucache_open_file(PVFS_fs_id *fs_id,
                     PVFS_handle *handle, 
                     struct file_ent_s **fent);
int ucache_close_file(struct file_ent_s *fent);

/**
 * Returns a pointer to the mtbl corresponding to the blk & ent.
 * Input must be reliable otherwise invalid mtbl could be returned.
 */
static inline struct mem_table_s *ucache_get_mtbl(uint16_t mtbl_blk, uint16_t mtbl_ent)
{
    if( mtbl_blk < BLOCKS_IN_CACHE &&
        mtbl_ent < MEM_TABLE_ENTRY_COUNT)
    {
        return &(ucache->b[mtbl_blk].mtbl[mtbl_ent]);
    }
    else
    {
        return (struct mem_table_s *)NILP;
    }

}

/**
 * Returns ptr to block in ucache based on file and offset
 */
static inline void *ucache_lookup(struct file_ent_s *fent, uint64_t offset,
    uint16_t *block_ndx)
{
    if(DBG)
    {
        printf("offset = %lu\n", offset);
    }
    void *retVal = (void *) NIL;
    if(fent)
    {
        lock_lock(ucache_lock);
        struct mem_table_s *mtbl = ucache_get_mtbl(fent->mtbl_blk,
            fent->mtbl_ent);
        retVal = lookup_mem(mtbl,
                            offset,
                            block_ndx,
                            NULL,
                            NULL);
        lock_unlock(ucache_lock);
    }
    return retVal;
}

/**
 * Prepares the data structures for block storage.
 * On success, returns a pointer to where the block of data should be written.
 * On failure, returns NIL.
 */
static inline void *ucache_insert(struct file_ent_s *fent,
                    uint64_t offset,
                    uint16_t *block_ndx
)
{
    lock_lock(ucache_lock);
    void * retVal = insert_mem(fent, offset, block_ndx);
    lock_unlock(ucache_lock);
    return (retVal);
}

int ucache_info(FILE *out, char *flags);

int ucache_flush_cache(void); 
int ucache_flush_file(struct file_ent_s *fent);

/* Don't call this except in ucache daemon */
int ucache_init_file_table(char forceCreation);

/* Used only in testing */
int wipe_ucache(void);

/* Lock Routines */
int lock_init(ucache_lock_t * lock);

/**
 * Returns a pointer to the lock corresponding to the block_index.
 * If the index is out of range, then 0 is returned.
 */
static inline ucache_lock_t *get_lock(uint16_t block_index)
{
    if(block_index >= (BLOCKS_IN_CACHE + 1))
    {
        return (ucache_lock_t *)0;
    }
    return &ucache_locks[block_index];
}

/** 
 * Returns 0 when lock is locked; otherwise, return -1 and sets errno.
 */
static inline int lock_lock(ucache_lock_t * lock)
{
    int rc = 0;
    #if LOCK_TYPE == 0
    return sem_wait(lock);
    #elif LOCK_TYPE == 1
/*
    while(1)
    {
        rc = pthread_mutex_trylock(lock);
        if(rc != 0)
        {
            printf("couldn't lock lock 0X%lX\n", (long unsigned int) lock); 
            fflush(stdout);
            rc = -1;
        }
        else
        {
            break;
        }
    }
*/
    rc = pthread_mutex_lock(lock);
    return rc;
    #elif LOCK_TYPE == 2
    return pthread_spin_lock(lock);
    #elif LOCK_TYPE == 3
    rc = gen_mutex_lock(lock);
    return rc;
    #endif   
}

/** 
 * If successful, return zero; otherwise, return -1 and sets errno. 
 */
static inline int lock_unlock(ucache_lock_t * lock)
{
    #if LOCK_TYPE == 0
    return sem_post(lock);
    #elif LOCK_TYPE == 1
    return pthread_mutex_unlock(lock); 
    #elif LOCK_TYPE == 2
    return pthread_spin_unlock(lock);
    #elif LOCK_TYPE == 3
    return gen_mutex_unlock(lock);
    #endif
}

/** 
 * Tries the lock to see if it's available:
 * Returns 0 if lock has not been aquired ie: success
 * Otherwise, returns -1
 */
static inline int lock_trylock(ucache_lock_t * lock)
{
    int rc = -1;
    #if (LOCK_TYPE == 0)
    int sval = 0;
    rc = sem_getvalue(lock, &sval);
    if(sval <= 0 || rc == -1){
        rc = -1;
    }
    else
    {
        rc = 0;
    }
    #elif (LOCK_TYPE == 1)
    rc = pthread_mutex_trylock(lock);
    if( rc != 0)
    {
        rc = -1;
    }
    #elif (LOCK_TYPE == 2)
    rc = pthread_spin_trylock(lock);
    if(rc != 0)
    {
        rc = -1;
    }
    #elif LOCK_TYPE == 3
    rc = gen_mutex_trylock(lock);
    if(rc != 0)
    {
        rc = -1;
    }
    #endif
    if(rc == 0)
    {
        /* Unlock before leaving if lock wasn't already set */
        rc = lock_unlock(lock);
    }
    return rc;
}

#endif /* UCACHE_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

