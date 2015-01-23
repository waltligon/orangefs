/* 
 * (C) 2011 Clemson University
 *
 * See COPYING in top-level directory.
 */

/** 
 * \file  
 * \ingroup usrint
 * 
 * Experimental cache for user data. 
 *
 */
#include <pvfs2-config.h>
#include <gen-locks.h>
#include <malloc.h>
#include "usrint.h"
#include "posix-ops.h"
#include "openfile-util.h"
#include "iocommon.h"
#if PVFS_UCACHE_ENABLE
#include "ucache.h"

/* Global Variables */
FILE *out;                   /* For Logging Purposes */

/* static uint32_t ucache_blk_cnt = 0; */

/* Global pointers to data in shared mem. Pointers set in ucache_initialize */
union ucache_u *ucache = 0;
struct ucache_aux_s *ucache_aux = 0; /* All locks and stats stored here */

/* ucache_aux is a pointer to the actual data summarized by the following 
 * pointers 
*/
ucache_lock_t *ucache_locks = 0; /* The shmem of all ucache locks */
ucache_lock_t *ucache_lock = 0;  /* Global Lock maintaining concurrency */
struct ucache_stats_s *ucache_stats = 0; /* Pointer to stats structure*/

/* Per-process (thread) execution statistics */
struct ucache_stats_s these_stats = { 0, 0, 0, 0, 0 }; 

/* Flags indicating ucache status */
int ucache_enabled = 0;
char ftblInitialized = 0;

/* Internal Only Function Declarations */

/* Initialization */
static void add_mtbls(uint16_t blk);
static void init_memory_table(struct mem_table_s *mtbl);
static inline void init_memory_entry(struct mem_table_s *mtbl, int16_t index);

/* Gets */
static uint16_t get_next_free_mtbl(uint16_t *free_mtbl_blk, uint16_t *free_mtbl_ent);
static uint16_t get_free_fent(void);
static inline uint16_t get_free_ment(struct mem_table_s *mtbl);
static inline uint16_t get_free_blk(void);

/* Puts */
static int put_free_mtbl(struct mem_table_s *mtbl, struct file_ent_s *file);
static void put_free_fent(struct file_ent_s *fent);
static void put_free_ment(struct mem_table_s *mtbl, uint16_t ent);
static inline void put_free_blk(uint16_t blk);

/* File Entry Chain Iterator */
static unsigned char file_done(uint16_t index);
static uint16_t file_next(struct file_table_s *ftbl, uint16_t index);

/* Memory Entry Chain Iterator */
static inline unsigned char ment_done(uint16_t index);
static inline uint16_t ment_next(struct mem_table_s *mtbl, uint16_t index);

/* Dirty List Iterator */
static inline unsigned char dirty_done(uint16_t index);
static inline uint16_t dirty_next(struct mem_table_s *mtbl, uint16_t index);

/* File and Memory Insertion */
uint16_t insert_file(uint32_t fs_id, uint64_t handle);

static inline void *insert_mem(struct file_ent_s *fent, 
                                       uint64_t offset, 
                                    uint16_t *block_ndx
);

static inline void *set_item(struct file_ent_s *fent,
                      uint64_t offset, 
                      uint16_t index
);

/* File and Memory Lookup */
static struct mem_table_s *lookup_file(
    uint32_t fs_id, 
    uint64_t handle,
    uint16_t *file_mtbl_blk,    /* Can be NULL if not desired */
    uint16_t *file_mtbl_ent,  
    uint16_t *file_ent_index,
    uint16_t *file_ent_prev_index
);
static inline void *lookup_mem(struct mem_table_s *mtbl, 
                    uint64_t offset, 
                    uint16_t *item_index,
                    uint16_t *mem_ent_index,
                    uint16_t *mem_ent_prev_index
);

/* File and Memory Entry Removal */
static int remove_file(struct file_ent_s *fent);
static int wipe_mtbl(struct mem_table_s *mtbl);
static int remove_mem(struct file_ent_s *fent, uint64_t offset);

/* Eviction Utilities */
static uint16_t locate_max_fent(struct file_ent_s **fent);
static void update_LRU(struct mem_table_s *mtbl, uint16_t index);
static int evict_LRU(struct file_ent_s *fent);

/* Logging */
//static void log_ucache_stats(void);

/* List Printing Functions */ 
void print_LRU(struct mem_table_s *mtbl);
void print_dirty(struct mem_table_s *mtbl);

/* Flushing of individual files and blocks */
int flush_file(struct file_ent_s *fent);
int flush_block(struct file_ent_s *fent, struct mem_ent_s *ment);

/*  Externally Visible API
 *      The following functions are thread/processor safe regarding the cache 
 *      tables and data.      
 */

/**  
 * Initializes the cache. 
 * Mainly, it aquires a previously created shared memory segment used to 
 * cache data. The shared mem. creation and ftbl initialization should already
 * have been done by the daemon at this point. 
 * 
 * The whole cache is protected globally by a locking mechanism.
 *
 * Locks (same type as global lock) can be used to protect block level data. 
 */
int ucache_initialize(void)
{
    int rc = 0;
    //gossip_set_debug_mask(1, GOSSIP_UCACHE_DEBUG);  

    /* Aquire pointers to shmem segments (ucache_aux and ucache) */
    /* shmget segment containing ucache_aux */
    key_t key = ftok(KEY_FILE, SHM_ID1);
    int shmflg = SVSHM_MODE;
    int aux_shmid = shmget(key, 0, shmflg);
    if(aux_shmid == -1)
    {
        //gossip_debug(GOSSIP_UCACHE_DEBUG, 
        //    "ucache_initialize - ucache_aux shmget: errno = %d\n", errno);
        return -1;
    }
    /* shmat ucache_aux */
    ucache_aux = shmat(aux_shmid, NULL, 0);
    if((long int)ucache_aux == -1)
    {
        //gossip_debug(GOSSIP_UCACHE_DEBUG,        
        //    "ucache_initialize - ucache_aux shmat: errno = %d\n", errno);
        return -1;
    }

    /* Set our global pointers to data in the ucache_aux struct */
    ucache_locks = ucache_aux->ucache_locks;
    ucache_lock = get_lock(BLOCKS_IN_CACHE);
    ucache_stats = &(ucache_aux->ucache_stats);

    /* ucache */
    key = ftok(KEY_FILE, SHM_ID2);
    int ucache_shmid = shmget(key, 0, shmflg);
    if(ucache_shmid == -1)
    {
        //gossip_debug(GOSSIP_UCACHE_DEBUG,        
        //    "ucache_initialize - ucache shmget: errno = %d\n", errno);
        return -1;
    }
    ucache = (union ucache_u *)shmat(ucache_shmid, NULL, 0);
    if((long int)ucache == -1) 
    {
        //gossip_debug(GOSSIP_UCACHE_DEBUG,        
        //    "ucache_initialize - ucache shmat: errno = %d\n", errno);
        return -1;
    }

    /* When this process ends we may want to dump ucache stats to a log file */
    //rc = atexit(log_ucache_stats);    

    /* Declare the ucache enabled! */
    ucache_enabled = 1;
    return rc;
}

/** 
 * Returns a pointer to the mtbl corresponding to the blk & ent. 
 * Input must be reliable otherwise invalid mtbl could be returned.
 */
inline struct mem_table_s *ucache_get_mtbl(uint16_t mtbl_blk, uint16_t mtbl_ent)
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
 * Initializes the ucache file table if it hasn't previously been initialized.
 * Although this function is visible, DO NOT CALL THIS FUNCTION. 
 * It is meant to be called in the ucache daemon or during testing.
 * see: src/apps/ucache/ucached.c for more info.
 *
 * Sets the char booelan ftblInitialized when ftbl has been successfully 
 * initialized.
 * 
 * Returns 0 on success, -1 on failure.
 */
int ucache_init_file_table(char forceCreation)
{
    int i;

    /* check if already initialized? */
    if(ftblInitialized == 1 && !forceCreation) 
    {
        return -1;
    }
    if(ucache)
    {
        memset(ucache, 0, CACHE_SIZE);
    }
    else
    {
        return -1;
    }
        

    /* initialize mtbl free list table */
    ucache->ftbl.free_mtbl_blk = NIL16;
    ucache->ftbl.free_mtbl_ent = NIL16;
    add_mtbls(0);

    /* set up list of free blocks */
    ucache->ftbl.free_blk = 1;
    for (i = 1; i < (BLOCKS_IN_CACHE - 1); i++)
    {
        ucache->b[i].mtbl[0].free_list_blk = i + 1;
    }
    ucache->b[BLOCKS_IN_CACHE - 1].mtbl[0].free_list_blk = NIL16;

    /* set up file hash table */
    for (i = 0; i < FILE_TABLE_HASH_MAX; i++)
    {
        ucache->ftbl.file[i].tag_handle = NIL64;
        ucache->ftbl.file[i].tag_id = NIL32;
        ucache->ftbl.file[i].mtbl_blk = NIL16;
        ucache->ftbl.file[i].mtbl_ent = NIL16;
        ucache->ftbl.file[i].size = NIL64;
        ucache->ftbl.file[i].next = NIL16;
    }

    /* set up list of free hash table entries */
    ucache->ftbl.free_list = FILE_TABLE_HASH_MAX;
    for (i = FILE_TABLE_HASH_MAX; i < FILE_TABLE_ENTRY_COUNT - 1; i++)
    {
        ucache->ftbl.file[i].mtbl_blk = NIL16;
        ucache->ftbl.file[i].mtbl_ent = NIL16;
        ucache->ftbl.file[i].next = i + 1;
    }
    ucache->ftbl.file[FILE_TABLE_ENTRY_COUNT - 1].next = NIL16;

    /* Success */
    ftblInitialized = 1;
    return 0;
}

/**
 * Opens a file in ucache.
 */
int ucache_open_file(PVFS_fs_id *fs_id,
                     PVFS_handle *handle, 
                     struct file_ent_s **fent)
{
    int rc = -1;
    uint16_t file_mtbl_blk;
    uint16_t file_mtbl_ent;
    uint16_t file_ent_index;
    uint16_t file_ent_prev_index;

    lock_lock(ucache_lock);

    struct mem_table_s *mtbl = lookup_file((uint32_t)(*fs_id), 
                                           (uint64_t)(*handle), 
                                            &file_mtbl_blk, 
                                            &file_mtbl_ent, 
                                            &file_ent_index,
                                            &file_ent_prev_index);

    if(mtbl == (struct mem_table_s *)NIL)
    {
        uint16_t fentIndex  = insert_file((uint32_t)*fs_id, (uint64_t)*handle);
        if(fentIndex > FILE_TABLE_ENTRY_COUNT)
        {
            rc = -1;
            goto done;
        }
        *fent = &(ucache->ftbl.file[fentIndex]);
        if((*fent)->mtbl_blk == NIL16 || (*fent)->mtbl_ent == NIL16)
        {
            rc = -1;
            goto done;
        }

        mtbl = ucache_get_mtbl((*fent)->mtbl_blk, (*fent)->mtbl_ent);
        if(mtbl == (struct mem_table_s *)NILP)
        {   
            /* Error - Could not insert */
            rc = -1;
            goto done;
        }
        else
        {
            /* File Inserted */
            mtbl->ref_cnt = 1;
            ucache_stats->file_count++;
            rc = 0;
            goto done;
        }
    }
    else
    {
        /* File was previously Inserted */
        mtbl->ref_cnt++;
        *fent = &(ucache->ftbl.file[file_ent_index]);
        rc = 1;
        goto done;
    }
done:
    lock_unlock(ucache_lock);
    return rc;
}

/** 
 * Returns ptr to block in ucache based on file and offset 
 */
inline void *ucache_lookup(struct file_ent_s *fent, uint64_t offset, 
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
        struct mem_table_s *mtbl = ucache_get_mtbl(fent->mtbl_blk, fent->mtbl_ent); 
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
inline void *ucache_insert(struct file_ent_s *fent, 
                    uint64_t offset, 
                    uint16_t *block_ndx
)
{
    lock_lock(ucache_lock);
    void * retVal = insert_mem(fent, offset, block_ndx);
    lock_unlock(ucache_lock);
    return (retVal); 
}

#if 0
/** 
 * Removes a cached block of data from mtbl 
 * Returns 1 on success, 0 on failure.
 */ 
int ucache_remove(struct file_ent_s *fent, uint64_t offset)
{
    int rc = 0;
    lock_lock(ucache_lock);
    rc = remove_mem(fent , offset);
    lock_unlock(ucache_lock);
    return rc;
}
#endif

/** 
 * Flushes the entire ucache's dirty blocks (every file's dirty blocks)
 * Returns 0 on success, -1 on failure
 */
int ucache_flush_cache(void)
{
    int rc = 0;
    lock_lock(ucache_lock);
    struct file_table_s *ftbl = &ucache->ftbl;
    int i;
    for(i = 0; i < FILE_TABLE_HASH_MAX; i++)
    {
        if((ftbl->file[i].tag_handle != NIL64) &&
               (ftbl->file[i].tag_handle != 0))
        {
            /* Iterate accross file table chain. */ 
            uint16_t j;
            for(j = i; !file_done(j); j = file_next(ftbl, j))
            {
                rc = flush_file(&ftbl->file[j]);
                if(rc !=0)
                {
                    rc = -1;
                    goto done;
                }
            }
        }
    }

done:
    lock_unlock(ucache_lock);
    return rc;
}

/** 
 * Externally visible wrapper of the internal flush file function.
 * This is intended to allow an external flush file call which locks the 
 * global lock, flushes the file, then releases the global lock.
 * To prevent deadlock, do not call this in any function that aquires the 
 * global lock.
 * Returns 0 on success, -1 on failure.
 */
int ucache_flush_file(struct file_ent_s *fent)
{
    int rc = 0;
    lock_lock(ucache_lock);
    rc = flush_file(fent);
    lock_unlock(ucache_lock);
    return rc;
}

/** 
 * Internal only function - Flushes dirty blocks to the I/O Nodes 
 * Returns 0 on success and -1 on failure.
 */
int flush_file(struct file_ent_s *fent)
{
    int rc = 0;
    struct mem_table_s *mtbl = ucache_get_mtbl(fent->mtbl_blk, fent->mtbl_ent);
    PVFS_object_ref ref = {fent->tag_handle, fent->tag_id, 0};

    uint16_t i;
    uint16_t temp_next = NIL16;
    for(i = mtbl->dirty_list; !dirty_done(i); i = temp_next)
    {
        struct mem_ent_s *ment = &(mtbl->mem[i]);
        if(ment->tag == NIL64 || ment->item == NIL16)
        {
            break;
        }

        /* Aquire block lock - TODO:check if this is redundant due to global lock */
        ucache_lock_t *blk_lock = get_lock(ment->item);
        lock_lock(blk_lock);

        temp_next = mtbl->mem[i].dirty_next;
        mtbl->mem[i].dirty_next = NIL16; 

        struct iovec vector =
        {
            &(ucache->b[ment->item].mblk[0]),
            0
        };

        /** Check the file size , so that we know how much to write to disk.
         */
        /* Determine how much data is left to flush based on file size */
        if((fent->size - ment->tag) < (CACHE_BLOCK_SIZE_K * 1024))
        {
            vector.iov_len = fent->size - ment->tag;
        }
        else
        {
            vector.iov_len = CACHE_BLOCK_SIZE_K * 1024;
        }

        rc = iocommon_vreadorwrite(2, &ref, ment->tag, 1, &vector); 

        lock_unlock(blk_lock);
        if(rc == -1)
        {
           goto done; 
        }
    }
    mtbl->dirty_list = NIL16;
    rc = 0;
done:
    return rc;
}

/**
 * This function is meant to be called only inside remove_mem.
 * Returns 0 on success, -1 on failure 
 */
int flush_block(struct file_ent_s *fent, struct mem_ent_s *ment)
{
    int rc = 0;
    PVFS_object_ref ref = {fent->tag_handle, fent->tag_id, 0};
    struct iovec vector = {&(ucache->b[ment->item].mblk[0]), CACHE_BLOCK_SIZE_K * 1024};
    rc = iocommon_vreadorwrite(2, &ref, ment->tag, 1, &vector);
    return rc;
}


/** 
 * For testing purposes only!
 */
int wipe_ucache(void)
{
    int rc = 0;

    /* Aquire pointers to shmem segments (just ucache) */
    int shmflg = SVSHM_MODE;

    /* ucache */
    key_t key = ftok(KEY_FILE, SHM_ID2);
    int ucache_shmid = shmget(key, 0, shmflg);
    if(ucache_shmid == -1)
    {
        glibc_ops.perror("wipe_ucache - ucache shmget");
        return -1;
    }
    ucache = (union ucache_u *)shmat(ucache_shmid, NULL, 0);
    if((long int)ucache == -1)
    {
        glibc_ops.perror("wipe ucache - ucache shmat");
        return -1;
    }

    /* wipe the cache, locks, and reinitialize */
    memset(ucache, 0, CACHE_SIZE);

    /* Force Re-creation of ftbl */
    rc = ucache_init_file_table(1);
    return rc;
}

/** 
 * Removes all memory entries in the mtbl corresponding to the file info 
 * provided as parameters. It also removes the mtbl and the file entry from 
 * the cache.
 */
int ucache_close_file(struct file_ent_s *fent)
{
    int rc = 0;
    rc = lock_lock(ucache_lock);
    rc = remove_file(fent);
    if(rc == 0)
    {
        ucache_stats->file_count--;
    }
    lock_unlock(ucache_lock);
    return rc;
}

/** May dump stats to log file if the envar LOG_UCACHE_STATS is set to 1.
 *
 */
#if 0
void log_ucache_stats(void)
{
    /* Return if envar not set to 1 */
    char *var = getenv("LOG_UCACHE_STATS");
    if(!var)
    {
        return;
    }
    if(atoi(var) != 1)
    {
        return;
    }

    float attempts = these_stats.hits + these_stats.misses;
    float percentage = 0.0;
    /* Don't Divide By Zero! */
    if(attempts)
    {
        percentage = ((float)these_stats.hits) / attempts;
    }
   /* 
    gossip_debug(GOSSIP_UCACHE_DEBUG,
        "user cache statistics for this execution:\n"
        "\thits=\t%llu\n"
        "\tmisses=\t%llu\n"
        "\thit percentage=\t%f\n"
        "\tpseudo_misses=\t%llu\n"
        "\tblock_count=\t%hu\n"
        "\tfile_count=\t%hu\n",
        (long long unsigned int) these_stats.hits,
        (long long unsigned int) these_stats.misses,
        percentage,
        (long long unsigned int) these_stats.pseudo_misses,
        these_stats.block_count,
        these_stats.file_count
    );
   */
}
#endif

/** 
 * Dumps all cache related information to the specified file pointer.
 * Returns 0 on succes, -1 on failure meaning the ucache wasn't enabled 
 * for some reason. 
 */
int ucache_info(FILE *out, char *flags)
{
    if(!ucache_enabled)
    {
        ucache_initialize();
    } 
    if(!ucache_enabled)
    {
        //fprintf(out, "ucache is not enabled. See ucache.log and ucached.log.\n");
        return -1;
    }
   
    /* Decide what to show */
    unsigned char show_all = 0;
    unsigned char show_summary = 0;
    unsigned char show_parameters = 0;
    unsigned char show_contents = 0;
    unsigned char show_free = 0;

    int char_ndx;
    for (char_ndx = 0; char_ndx < strlen(flags); char_ndx++)
    {
        char c = flags[char_ndx];
        switch(c)
        {
            case 'a':
                show_all = 1;
                break;
            case 's':
                show_summary = 1;             
                break;
            case 'p':
                show_parameters = 1;
                break;
            case 'c':
                show_contents = 1;
                break;
            case 'f':
                show_free = 1;
                break;
        }
    }

    float attempts = ucache_stats->hits + ucache_stats->misses;
    float percentage = 0.0;

    /* Don't Divide By Zero! */
    if(attempts)
    {
        percentage = ((float) ucache_stats->hits) / attempts;
    }

    if(show_all || show_summary)
    {
        fprintf(out, 
            "user cache statistics:\n"
            "\thits=\t%llu\n"
            "\tmisses=\t%llu\n"
            "\thit percentage=\t%f\n"
            "\tpseudo_misses=\t%llu\n"
            "\tblock_count=\t%hu\n"
            "\tfile_count=\t%hu\n",
            (long long unsigned int) ucache_stats->hits, 
            (long long unsigned int) ucache_stats->misses, 
            (percentage * 100), 
            (long long unsigned int) ucache_stats->pseudo_misses,
            ucache_stats->block_count,
            ucache_stats->file_count
        );
    }

    if(show_all || show_parameters)
    {

        fprintf(out, "\n#defines:\n");
        /* First, print many of the #define values */
        fprintf(out, "MEM_TABLE_ENTRY_COUNT = %d\n", MEM_TABLE_ENTRY_COUNT);
        fprintf(out, "FILE_TABLE_ENTRY_COUNT = %d\n", FILE_TABLE_ENTRY_COUNT);
        fprintf(out, "CACHE_BLOCK_SIZE_K = %d\n", CACHE_BLOCK_SIZE_K);
        fprintf(out, "MEM_TABLE_HASH_MAX = %d\n", MEM_TABLE_HASH_MAX);
        fprintf(out, "FILE_TABLE_HASH_MAX = %d\n", FILE_TABLE_HASH_MAX);
        fprintf(out, "MTBL_PER_BLOCK  = %d\n", MTBL_PER_BLOCK );
        fprintf(out, "KEY_FILE = %s\n", KEY_FILE);
        fprintf(out, "SHM_ID1 = %d\n", SHM_ID1);
        fprintf(out, "SHM_ID2 = %d\n", SHM_ID2);
        fprintf(out, "BLOCKS_IN_CACHE = %d\n", BLOCKS_IN_CACHE);
        fprintf(out, "CACHE_SIZE = %d(B)\t%d(MB)\n", CACHE_SIZE, 
                                        (CACHE_SIZE/(1024*1024)));
        fprintf(out, "AT_FLAGS = %d\n", AT_FLAGS);
        fprintf(out, "SVSHM_MODE = %d\n", SVSHM_MODE);
        fprintf(out, "CACHE_FLAGS = %d\n", CACHE_FLAGS);
        fprintf(out, "NIL = 0X%X\n", NIL);
        fprintf(out, "NIL8 = 0X%X\n", NIL8);
        fprintf(out, "NIL16 = 0X%X\n", NIL16);    
        fprintf(out, "NIL32 = 0X%X\n", NIL32);
        fprintf(out, "NIL64 = 0X%lX\n", NIL64);
    
        /* Print sizes of ucache elements */
        fprintf(out, "sizeof union cache_block_u = %lu\n", sizeof(union cache_block_u));
        fprintf(out, "sizeof struct file_table_s = %lu\n", sizeof(struct file_table_s));
        fprintf(out, "sizeof struct file_ent_s = %lu\n", sizeof(struct file_ent_s));
        fprintf(out, "sizeof struct mem_table_s = %lu\n", sizeof(struct mem_table_s));
        fprintf(out, "sizeof struct mem_ent_s = %lu\n", sizeof(struct mem_ent_s));
    }

    if(show_all || show_contents)
    {
        /* Auxilliary structure related to ucache */
        fprintf(out, "ucache_aux ptr:\t\t0X%lX\n", (long int)ucache_aux);

        /* ucache Shared Memory Info */
        fprintf(out, "ucache ptr:\t\t0X%lX\n", (long int)ucache);
    
        /* FTBL Info */
        struct file_table_s *ftbl = &(ucache->ftbl);
        fprintf(out, "ftbl ptr:\t\t0X%lX\n", (long int)&(ucache->ftbl));
        fprintf(out, "free_blk = %hu\n", ftbl->free_blk);
        fprintf(out, "free_mtbl_blk = %hu\n", ftbl->free_mtbl_blk);
        fprintf(out, "free_mtbl_ent = %hu\n", ftbl->free_mtbl_ent);
        fprintf(out, "free_list = %hu\n", ftbl->free_list);
    
        uint16_t i;

        if(show_all || show_free)
        {
            /* Other Free Blocks */
            fprintf(out, "\nIterating Over Free Blocks:\n\n");
            for(i = ftbl->free_blk; i < BLOCKS_IN_CACHE; i = ucache->b[i].mtbl[0].
                                                                      free_list_blk)
            {
                fprintf(out, "Free Block:\tCurrent: %hu\tNext: %hu\n", i, 
                                       ucache->b[i].mtbl[0].free_list_blk); 
            }
            fprintf(out, "End of Free Blocks List\n");


            /* Iterate Over Free Mtbls */
            fprintf(out, "\nIterating Over Free Mtbls:\n");
            uint16_t current_blk = (uint16_t)ftbl->free_mtbl_blk;
            uint16_t current_ent = ftbl->free_mtbl_ent;
            while(current_blk != NIL16)
            {
                fprintf(out, "free mtbl: block = %hu\tentry = %hu\n", 
                        current_blk, current_ent);
                uint16_t temp_blk = ucache->b[current_blk].mtbl[current_ent].free_list_blk;
                uint16_t temp_ent = ucache->b[current_blk].mtbl[current_ent].free_list;
                current_blk = temp_blk;
                current_ent = temp_ent;
            }
            fprintf(out, "End of Free Mtbl List\n\n");
        
            /* Iterating Over Free File Entries */
            fprintf(out, "Iterating Over Free File Entries:\n");
            uint16_t current_fent; 
            for(current_fent = ftbl->free_list; current_fent != NIL16; 
                                current_fent = ftbl->file[current_fent].next)
            {
                fprintf(out, "free file entry: index = %d\n", (int16_t)current_fent);
            }
            fprintf(out, "End of Free File Entry List\n\n");
        }
    
        fprintf(out, "Iterating Over File Entries in Hash Table:\n\n");
        /* iterate over file table entries */
        for(i = 0; i < FILE_TABLE_HASH_MAX; i++)
        {
            if((ftbl->file[i].tag_handle != NIL64) && 
                   (ftbl->file[i].tag_handle != 0))
            {
                /* iterate accross file table chain */
                uint16_t j;
                for(j = i; !file_done(j); j = file_next(ftbl, j))
                {
                    fprintf(out, "FILE ENTRY INDEX %hu ********************\n", j);
                    struct file_ent_s * fent = &(ftbl->file[j]);
                    fprintf(out, "tag_handle = 0X%llX\n", 
                                (long long int)fent->tag_handle);
                    fprintf(out, "tag_id = 0X%X\n", (uint32_t)fent->tag_id);
                    fprintf(out, "mtbl_blk = %hu\n", fent->mtbl_blk);
                    fprintf(out, "mtbl_ent = %hu\n", fent->mtbl_ent);
                    fprintf(out, "next = %hu\n", fent->next);
                    fprintf(out, "index = %hu\n", fent->index);
                    fprintf(out, "size = %lu\n", fent->size);
    
                    struct mem_table_s * mtbl = ucache_get_mtbl(fent->mtbl_blk, 
                                                        fent->mtbl_ent);
    
                    fprintf(out, "\tMTBL LRU List ****************\n");
                    print_LRU(mtbl); 
                    print_dirty(mtbl); 
    
                    fprintf(out, "\tMTBL INFO ********************\n");
                    fprintf(out, "\tnum_blocks = %hu\n", mtbl->num_blocks);
                    fprintf(out, "\tfree_list = %hu\n", mtbl->free_list); 
                    fprintf(out, "\tfree_list_blk = %hu\n", mtbl->free_list_blk);
                    fprintf(out, "\tlru_first = %hu\n", mtbl->lru_first);
                    fprintf(out, "\tlru_last = %hu\n", mtbl->lru_last);
                    fprintf(out, "\tdirty_list = %hu\n", mtbl->dirty_list);
                    fprintf(out, "\tref_cnt = %hu\n\n", mtbl->ref_cnt);
                    fflush(out);
                    /* Iterate Over Memory Entries */
                    uint16_t k;
                    for(k = 0; k < MEM_TABLE_HASH_MAX; k++)
                    {
                        if(mtbl->bucket[k] == NIL16)
                            continue;
   
                        if(mtbl->mem[mtbl->bucket[k]].tag != NIL64)
                        {
                            uint16_t l;
                            for(l = mtbl->bucket[k]; !ment_done(l); l = ment_next(mtbl, l))
                            {
                                struct mem_ent_s * ment = &(mtbl->mem[l]);
                                fprintf(out, "\t\tMEMORY ENTRY INDEX %hd **********"
                                                                  "*********\n", l);
                                fprintf(out, "\t\ttag = 0X%lX\n", 
                                             (long unsigned int)ment->tag);

                                fprintf(out, "\t\titem = %hu\n", 
                                                    ment->item);
                                fprintf(out, "\t\tnext = %hu\n", 
                                                    ment->next);
                                fprintf(out, "\t\tdirty_next = %hu\n", 
                                                    ment->dirty_next);
                                fprintf(out, "\t\tlru_next = %hu\n", 
                                                    ment->lru_next);
                                fprintf(out, "\t\tlru_prev = %hu\n\n", 
                                                      ment->lru_prev);
                            } 
                        }
                        else
                        {
                            if(mtbl->num_blocks != 0 
                                && (show_all || show_free))
                            {
                                fprintf(out, "\tvacant memory entry @ index = %d\n",
                                    mtbl->bucket[k]);
                            }
                        }
                    }
                }
                fprintf(out, "End of chain @ Hash Table Index %hu\n\n", i);
            }
            else
            {
                if(show_all || show_free)
                {
                    fprintf(out, "vacant file entry @ index = %hu\n\n", i);
                }
            }
        }
    }
    return 0;
}

/** 
 * Returns a pointer to the lock corresponding to the block_index.
 * If the index is out of range, then 0 is returned.
 */
inline ucache_lock_t *get_lock(uint16_t block_index)
{
    if(block_index >= (BLOCKS_IN_CACHE + 1))
    {
        return (ucache_lock_t *)0;
    }
    return &ucache_locks[block_index];
}

/** 
 * Initializes the proper lock based on the LOCK_TYPE 
 * Returns 0 on success, -1 on error
 */
int lock_init(ucache_lock_t * lock)
{
    int rc = -1;
    /* TODO: ability to disable locking */
    #if LOCK_TYPE == 0
    rc = sem_init(lock, 1, 1);
    if(rc != -1)
    {
        rc = 0; 
    }
    #elif LOCK_TYPE == 1
    pthread_mutexattr_t attr;
    rc = pthread_mutexattr_init(&attr);
    assert(rc == 0);
    rc = pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    assert(rc == 0);
    rc = pthread_mutex_init(lock, &attr);
    assert(rc == 0);
    if(rc != 0)
    {
        return -1;
    }
    #elif LOCK_TYPE == 2
    rc = pthread_spin_init(lock, 1);
    if(rc != 0)
    {
        return -1;
    }
    #elif LOCK_TYPE == 3
    /* *lock = (ucache_lock_t) GEN_SHARED_MUTEX_INITIALIZER_NP; */
    rc = gen_shared_mutex_init(lock);
    if(rc != 0)
    {
        return -1;
    }
    #endif
    return 0;
}

/** 
 * Returns 0 when lock is locked; otherwise, return -1 and sets errno.
 */
inline int lock_lock(ucache_lock_t * lock)
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
inline int lock_unlock(ucache_lock_t * lock)
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
 * Upon successful completion, returns zero 
 * Otherwise, returns -1 and sets errno.
 */
#if (LOCK_TYPE == 0)
int ucache_lock_getvalue(ucache_lock_t * lock, int *sval)
{
    return sem_getvalue(lock, sval);
}
#endif

/** 
 * Tries the lock to see if it's available:
 * Returns 0 if lock has not been aquired ie: success
 * Otherwise, returns -1
 */
inline int lock_trylock(ucache_lock_t * lock)
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
/***************************************** End of Externally Visible API */

/* Beginning of internal only (static) functions */

/* Dirty List Iterator */
/** 
 * Returns true if current index is NIL, otherwise, returns 0.
 */
static inline unsigned char dirty_done(uint16_t index)
{
    return (index == NIL16);
}

/** 
 * Returns the next index in the dirty list for the provided mtbl and index 
 */
static inline uint16_t dirty_next(struct mem_table_s *mtbl, uint16_t index)
{
    return mtbl->mem[index].dirty_next;
}

/*  Memory Entry Chain Iterator */
/** 
 * Returns true if current index is NIL, otherwise, returns 0.
 */
static inline unsigned char ment_done(uint16_t index)
{
    return (index == NIL16);
}

/** 
 * Returns the next index in the memory entry chain for the provided mtbl 
 * and index. 
 */
static inline uint16_t ment_next(struct mem_table_s *mtbl, uint16_t index)
{
    return mtbl->mem[index].next;
}

/*  File Entry Chain Iterator   */
/** 
 * Returns true if current index is NIL, otherwise, returns 0 
 */
static unsigned char file_done(uint16_t index)
{
    return (index == NIL16);
}

/** 
 * Returns the next index in the file entry chain for the provided mtbl 
 * and index. 
 */
static uint16_t file_next(struct file_table_s *ftbl, uint16_t index)
{
    return ftbl->file[index].next;
}

/**
 * This function should only be called when the ftbl has no free mtbls. 
 * It initizializes MTBL_PER_BLOCK additional mtbls in the block provided,
 * meaning this block will no longer be used for storing file data but 
 * hash table related data instead.
 */
static void add_mtbls(uint16_t blk)
{
    uint16_t i, start_mtbl;
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
    for (i = start_mtbl; i < (MTBL_PER_BLOCK - 1); i++)
    {
        b->mtbl[i].free_list_blk = blk;
        b->mtbl[i].free_list = i + 1;
    }
    b->mtbl[i].free_list_blk = NIL16;
    b->mtbl[i].free_list = NIL16;
    ftbl->free_mtbl_blk = blk;
    ftbl->free_mtbl_ent = start_mtbl;   
}
/**
 * Initializes a memory entry.
 */
static inline void init_memory_entry(struct mem_table_s *mtbl, int16_t index)
{
        assert(index < MEM_TABLE_ENTRY_COUNT);
        mtbl->mem[index].tag = NIL64;
        mtbl->mem[index].item = NIL16;
        mtbl->mem[index].next = NIL16;
        mtbl->mem[index].dirty_next = NIL16;
        mtbl->mem[index].lru_prev = NIL16;
        mtbl->mem[index].lru_next = NIL16;
}

/** 
 * Initializes a mtbl which is a hash table of memory entries.
 * The mtbl will be located at the provided entry index within 
 * the provided block.
 */
static void init_memory_table(struct mem_table_s *mtbl)
{
    uint16_t i;
    mtbl->num_blocks = 0;
    mtbl->free_list_blk = NIL16;
    mtbl->lru_first = NIL16;
    mtbl->lru_last = NIL16;
    mtbl->dirty_list = NIL16;
    mtbl->ref_cnt = 0;

    /* Initialize Buckets */
    for(i = 0; i < MEM_TABLE_HASH_MAX; i++)
    {
        mtbl->bucket[i] = NIL16;
    }

    /* set up free ments */
    mtbl->free_list = 0;
    for(i = 0; i < (MEM_TABLE_ENTRY_COUNT - 1); i++)
    {
        init_memory_entry(mtbl, i);
        mtbl->mem[i].next = i + 1;

    }
    /* NIL Terminate the last entries next index */
    init_memory_entry(mtbl, MEM_TABLE_ENTRY_COUNT - 1);
    mtbl->mem[MEM_TABLE_ENTRY_COUNT - 1].next = NIL16;
}

/** 
 * This function asks the file table if a free block is avaialable. 
 * If so, returns the block's index; otherwise, returns NIL.
 */
static inline uint16_t get_free_blk(void)
{
    struct file_table_s *ftbl = &(ucache->ftbl);
    uint16_t desired_blk = ftbl->free_blk;
    if(desired_blk != NIL16 && desired_blk < BLOCKS_IN_CACHE)
    {  
        /* Update the head of the free block list */ 
        /* Use mtbl index zero since free_blks have no ititialized mem tables */
        ftbl->free_blk = ucache->b[desired_blk].mtbl[0].free_list_blk; 
        return desired_blk;
    }
    return NIL16;
}

/** 
 * Accepts an index corresponding to a block that is put back on the file 
 * table free list.
 */
static inline void put_free_blk(uint16_t blk)
{
    struct file_table_s *ftbl = &(ucache->ftbl);
    /* set the block's next value to the current head of the block free list */
    ucache->b[blk].mtbl[0].free_list_blk = ftbl->free_blk;
    /* blk is now the head of the ftbl blk free list */
    ftbl->free_blk = blk;
}

/** 
 * Consults the file table to retrieve an index corresponding to a file entry
 * If available, returns the file entry index, otherwise returns NIL.
 */
static uint16_t get_free_fent(void)
{
    struct file_table_s *ftbl = &(ucache->ftbl);
    uint16_t entry = ftbl->free_list;
    if(entry != NIL16)
    {
        ftbl->free_list = ftbl->file[entry].next;
        ftbl->file[entry].next = NIL16;
        return entry;
    }
    else
    {
        return NIL16;
    }
}

/** 
 * Places the file entry located at the provided index back on the file table's
 * free file entry list. If the index is < FILE_TABLE_HASH_MAX, then set next 
 * to NIL since this index must remain the head of the linked list. Otherwise,
 * set next to the current head of fent free list and set the free list head to
 * the provided index.
 */
static void put_free_fent(struct file_ent_s *fent)
{
    struct file_table_s *ftbl = &(ucache->ftbl);
    fent->tag_handle = NIL64;
    fent->tag_id = NIL32;
    fent->size = NIL64;
    if(fent->index < FILE_TABLE_HASH_MAX)
    {
        fent->next = NIL16;
    }
    else
    {
        /* Set next index to the current head of the free list */
        fent->next = ftbl->free_list;
        /* Set fent index as the head of the free_list */
        ftbl->free_list = fent->index;
    }
}

/** 
 * Consults the provided mtbl's memory entry free list to get the index of the
 * next free memory entry. Returns the index if one is available, otherwise 
 * returns NIL.
 */
static inline uint16_t get_free_ment(struct mem_table_s *mtbl)
{
    uint16_t ment = mtbl->free_list;
    if(ment != NIL16)
    {
        mtbl->free_list = mtbl->mem[ment].next;
        mtbl->mem[ment].next = NIL16;
    }
    return ment;
}

/** 
 * Puts the memory entry corresponding to the provided mtbl and entry index 
 * back on the mtbl's memory entry free list. 
 */
static void put_free_ment(struct mem_table_s *mtbl, uint16_t ent)
{
    /* Reset ment values */
    mtbl->mem[ent].tag = NIL64;
    mtbl->mem[ent].item = NIL16;
    mtbl->mem[ent].dirty_next = NIL16;
    mtbl->mem[ent].lru_prev = NIL16;
    mtbl->mem[ent].lru_next = NIL16;
    /* Set next index to the current head of the free list */
    mtbl->mem[ent].next = mtbl->free_list;
    /* Update free list to include this entry */
    mtbl->free_list = ent;
}

/** 
 * Perform a file lookup on the ucache using the provided fs_id and handle.
 *
 * Additional parameters (references) may used that will be set to values 
 * pertaining to mtbl and file entry location. If NULL is passed in place of 
 * these parameters, then they cannot be set.
 *
 * If the file is found, a pointer to the mtbl is returned and the parameter 
 * references set accordingly. Otherwise, NIL is returned. 
 */
static struct mem_table_s *lookup_file(
    uint32_t fs_id, 
    uint64_t handle,
    uint16_t *file_mtbl_blk,
    uint16_t *file_mtbl_ent,
    uint16_t *file_ent_index,
    uint16_t *file_ent_prev_index
)
{
    /* Index into file hash table */
    uint16_t index = handle % FILE_TABLE_HASH_MAX; 

    struct file_table_s *ftbl = &(ucache->ftbl);
    struct file_ent_s *current = &(ftbl->file[index]);

    /* previous, current, next fent index */
    uint16_t p = NIL16;
    uint16_t c = index;
    uint16_t n = current->next;

    while(1)
    {
        if((current->tag_id == fs_id) && (current->tag_handle == handle))
        {
            /* If params !NULL, set their values */
            if(file_mtbl_blk!=NULL && file_mtbl_ent!=NULL && 
                file_ent_index!=NULL && file_ent_prev_index!=NULL)
            {
                    *file_mtbl_blk = current->mtbl_blk;
                    *file_mtbl_ent = current->mtbl_ent;
                    *file_ent_index = c;
                    *file_ent_prev_index = p;
            }
            return (struct mem_table_s *)&(ucache->b[current->mtbl_blk].mtbl[
                                                            current->mtbl_ent]);
        }
        /* No match yet */
        else    
        {
            if(current->next == NIL16 || current->next == 0)
            {
                return (struct mem_table_s *)NIL;
            }
            else
            {
                current = &(ftbl->file[current->next]);
                p=c; 
                c=n; 
                n=current->next;
            }

        }
    }    
}

/** 
 * Function that locates the next free mtbl.
 * On success, Returns 1 and sets reference parameters to proper indexes.
 * On failure, returns NIL; 
 */
static uint16_t get_next_free_mtbl(uint16_t *free_mtbl_blk, uint16_t *free_mtbl_ent)
{
        struct file_table_s *ftbl = &(ucache->ftbl);

        /* Get next free mtbl_blk and ent from ftbl */
        *free_mtbl_blk = ftbl->free_mtbl_blk;
        *free_mtbl_ent = ftbl->free_mtbl_ent;

        /* Is free mtbl_blk available? */
        if((*free_mtbl_blk == NIL16) || 
             (*free_mtbl_ent == NIL16))
        { 
            return NIL16;
        }

        /* Update ftbl to contain new next free mtbl */
        ftbl->free_mtbl_blk = ucache->b[*free_mtbl_blk].mtbl[*free_mtbl_ent].
                                                                free_list_blk;
        ftbl->free_mtbl_ent = ucache->b[*free_mtbl_blk].mtbl[*free_mtbl_ent].
                                                                    free_list;

        /* Set free info to NIL */
        ucache->b[*free_mtbl_blk].mtbl[*free_mtbl_ent].free_list = NIL16;
        ucache->b[*free_mtbl_blk].mtbl[*free_mtbl_ent].free_list_blk = NIL16;

        return 1;
}

/** 
 * Places memory entries' corresponding blocks 
 * back on the ftbl block free list. Reinitializes mtbl.
 * Assumes mtbl->ref_cnt is 0.  
 */
static int wipe_mtbl(struct mem_table_s *mtbl)
{
    uint16_t i;
    for(i = 0; i < MEM_TABLE_HASH_MAX; i++)
    {
        uint16_t j;
        for(j = mtbl->bucket[i]; !ment_done(j); j = ment_next(mtbl, j))
        {
            /* Current Memory Entry */
            struct mem_ent_s *ment = &(mtbl->mem[j]);
            /*  Account for empty head of ment chain    */
            if((ment->tag == NIL64) || (ment->item == NIL16))
            {
                break;
            }
            put_free_blk(ment->item);
        }
    }
    memset(&mtbl->mem[0], 0, sizeof(struct mem_ent_s) * MEM_TABLE_ENTRY_COUNT);
    init_memory_table(mtbl);
    return 1;
}

/** 
 * Places the provided mtbl back on the ftbl's mtbl free list provided it 
 * isn't currently referenced.
 */
static int put_free_mtbl(struct mem_table_s *mtbl, struct file_ent_s *file)
{
    /* Remove mtbl */
    mtbl->num_blocks = 0;   /* number of used blocks in this mtbl */
    mtbl->lru_first = NIL16;  /* index of first block on lru list */
    mtbl->lru_last = NIL16;   /* index of last block on lru list */
    mtbl->dirty_list = NIL16; /* index of first dirty block */
    mtbl->ref_cnt = 0;      /* number of clients using this record */

    /* Add mem_table back to free list */
    /* Temporarily store copy of current head (the new next) */
    uint16_t tmp_blk = ucache->ftbl.free_mtbl_blk;
    uint16_t tmp_ent = ucache->ftbl.free_mtbl_ent;
    /* newly free mtbl becomes new head of free mtbl list */
    ucache->ftbl.free_mtbl_blk = file->mtbl_blk;
    ucache->ftbl.free_mtbl_ent = file->mtbl_ent;
    /* Point to the next free mtbl (the former head) */
    mtbl->free_list_blk = tmp_blk;
    mtbl->free_list = tmp_ent;

    return 1;
}

/** 
 * Insert information about file into ucache (no file data inserted)
 * Returns pointer to mtbl on success.
 * 
 * Returns NIL if necessary data structures could not be aquired from the free
 * lists or through an eviction policy (meaning references are held).
 */
uint16_t insert_file(
    uint32_t fs_id,
    uint64_t handle 
)
{
    struct file_table_s *ftbl = &(ucache->ftbl);
    struct file_ent_s *current;     /* Current ptr for iteration */
    uint16_t free_fent = NIL16;       /* Index of next free fent */

    /* index into file hash table */
    uint16_t index = handle % FILE_TABLE_HASH_MAX;
    current = &(ftbl->file[index]);

    unsigned char indexOccupied = (current->tag_handle != NIL64 && current->tag_id != NIL32);

    /* Get free mtbl */
    uint16_t free_mtbl_blk = NIL16;
    uint16_t free_mtbl_ent = NIL16; 
    /* Create free mtbls if none are available */
    if(get_next_free_mtbl(&free_mtbl_blk, &free_mtbl_ent) != 1)
    {   
        if(ucache->ftbl.free_blk == NIL16) 
        {
            /* Evict a block from mtbl with most mem entries */
            struct file_ent_s *max_fent = 0;
            struct mem_table_s *max_mtbl;
            uint16_t ment_count = 0;
            ment_count = locate_max_fent(&max_fent);
            max_mtbl = ucache_get_mtbl(max_fent->mtbl_blk, max_fent->mtbl_ent);
            if(ment_count == 0 || max_mtbl->lru_last == NIL16)
            {
            }
            else
            {
                evict_LRU(max_fent);
            }
        }
        /* TODO: other policy? */
        if(ucache->ftbl.free_blk == NIL16)
        {

        }
        /* Intitialize memory tables */
        if(ucache->ftbl.free_blk != NIL16)
        {
            int16_t free_blk = get_free_blk(); 
            add_mtbls(free_blk);
            get_next_free_mtbl(&free_mtbl_blk, &free_mtbl_ent);
        }
        else
        {
            /* Couldn't get free mtbl - unlikely */
            return NIL16;
        }
    }

    /* Now, we know which hashed chain we are trying to insert into and have a 
     * mtbl ready to be filled.
     */

    /* Insert at the head or just after the head, since we can't change the 
     * indexing (only can change "nexts"). 
     */
    if(indexOccupied)
    {
        /* Certain a file entry is required */
        /* get free file entry and update ftbl */
        free_fent = get_free_fent();
        if(free_fent != NIL16)
        {
            uint16_t temp_next = current->next;
            current->next = free_fent;
            current = &(ftbl->file[free_fent]);
            current->next = temp_next; /* repair link */
            current->index = free_fent;
        }
        else
        {
            /* Return an error indicating the ucache is full and file couldn't 
             * be cached 
             */
            return NIL16;
        }
    }
    else
    {
        current->index = index;
    }

    /* Insert file data @ index */
    current->tag_id = fs_id;
    current->tag_handle = handle;
    /* Update fent with it's new mtbl: blk and ent */
    current->mtbl_blk = free_mtbl_blk;
    current->mtbl_ent = free_mtbl_ent;
    current->size = 0;
    /* Initialize Memory Table */
    init_memory_table(ucache_get_mtbl(free_mtbl_blk, free_mtbl_ent));
    return current->index;
}

/** 
 * Remove file entry and memory table of file identified by parameters
 * Returns 1 following removal
 * Returns -1 if file is referenced or if the file could not be located.
 */
static int remove_file(struct file_ent_s *fent)
{
    int rc = 0;
    struct mem_table_s *mtbl = ucache_get_mtbl(fent->mtbl_blk,
                                       fent->mtbl_ent);

    if(mtbl == (struct mem_table_s *)NILP)
    {
        return -1;
    }

    /* Flush file blocks before file removal */
    mtbl->ref_cnt--;

    if(mtbl->ref_cnt > 0)
    {
        return (int) mtbl->ref_cnt;
    }

    /* Flush dirty blocks before file removal from cache */
    rc = flush_file(fent);
    if(rc == -1)
    {
        return rc;
    }

    /* Instead of removing individually, since memory entries are already 
     * flushed, just wipe the mtbl 
     */
    rc = wipe_mtbl(mtbl);
    if(rc == -1)
    {
        /* Couldn't remove entries */
        return rc;
    }

    rc = put_free_mtbl(mtbl, fent);
    if(rc == -1)
    {
        return rc;
    }

    put_free_fent(fent);
    if(rc == -1)
    {
        return rc;
    }

    /* Success */
    return 0;
}

/** 
 * Lookup the memory location of a block of data in cache that is identified 
 * by the mtbl and offset parameters.
 *
 * If located, returns a pointer to memory where the desired block of data is 
 * stored. Otherwise, NIL is returned.
 *
 * pertaining to the memory entry's location. If NULLs are passed in place of 
 * these parameters, then they will not be set.
 */
inline static void *lookup_mem(struct mem_table_s *mtbl, 
                    uint64_t offset, 
                    uint16_t *item_index,
                    uint16_t *mem_ent_index,
                    uint16_t *mem_ent_prev_index)
{
    /* index into mem hash table */
    uint16_t index = (uint16_t) ((offset / CACHE_BLOCK_SIZE) % MEM_TABLE_HASH_MAX);

    /* If the bucket is empty then go ahead and return */
    if(mtbl->bucket[index] == NIL16)
    {
        return (struct mem_table_s *)NIL;
    }

    uint16_t bucket_index = mtbl->bucket[index];
    struct mem_ent_s *current = &(mtbl->mem[bucket_index]);

    /* previous, current, next memory entry index in mtbl */
    int16_t p = NIL16;
    int16_t c = bucket_index;
    int16_t n = current->next;  

    while(1)
    {
        if(offset == current->tag)
        {
            /* If parameters !NULL, set their values */
            if(item_index != NULL)
            {
                *item_index = current->item;
            }
            if((mem_ent_index != NULL) && (mem_ent_prev_index != NULL))
            {
                    *mem_ent_index = c;
                    *mem_ent_prev_index = p;
            }
            return (void *)(&ucache->b[current->item].mblk);
        }
        else
        {
            if(current->next == NIL16)
            {
                return (struct mem_table_s *)NIL;
            }
            else
            {
                /* Iterate */
                current = &(mtbl->mem[current->next]);
                p = c; 
                c = n; 
                n = current->next;
            }
        }
    }
}

/** 
 * Update the provided mtbl's LRU doubly-linked list by placing the memory 
 * entry, identified by the provided index, at the head of the list (lru_first).
 */
static inline void update_LRU(struct mem_table_s *mtbl, uint16_t index)
{
    /* First memory entry used becomes the head and tail of the list */
    if((mtbl->lru_first == NIL16) && 
        (mtbl->lru_last == NIL16))
    {
        mtbl->lru_first = index;
        mtbl->lru_last = index;
        mtbl->mem[index].lru_prev = NIL16;
        mtbl->mem[index].lru_next = NIL16;
    }
    /* 2nd Memory Entry */
    else if(mtbl->lru_first == mtbl->lru_last)
    {
        /* Do nothing if this index is already the only entry */
        if(mtbl->lru_first == index)
        {
            return;
        }
        else
        {   
            /* Must be 2nd unique memory entry */
            /* point tail.prev to new */
            mtbl->mem[mtbl->lru_first].lru_prev = index;
            /* point new.prev to NIL */  
            mtbl->mem[index].lru_prev = NIL16;
            /* point the new.next to the tail */      
            mtbl->mem[index].lru_next = mtbl->lru_first;
            /* point the head to the new */  
            mtbl->lru_first = index;                    
        }
    }
    /* 3rd+ Memory Entry */
    else
    {
        if(mtbl->mem[index].lru_prev == NIL16 && 
            mtbl->mem[index].lru_next == NIL16)
        {
            /* First time on the LRU List, Add to the front */
            mtbl->mem[index].lru_next = mtbl->lru_first;
            mtbl->mem[mtbl->lru_first].lru_prev = index;    
        }
        else if(mtbl->mem[index].lru_prev == NIL16)
        {
            /* Already the head of MRU */
            return;
        }
        else if(mtbl->mem[index].lru_next == NIL16)
        {
            /* Relocate the LRU to become the MRU */
            mtbl->lru_last = mtbl->mem[index].lru_prev;
            mtbl->mem[mtbl->lru_last].lru_next = NIL16;
            mtbl->mem[mtbl->lru_first].lru_prev = index;
            mtbl->mem[index].lru_next = mtbl->lru_first;
            mtbl->mem[index].lru_prev = NIL16;
        }
        else
        {
            /* Relocate interior LRU list item to head */
            uint16_t current_prev = mtbl->mem[index].lru_prev;
            uint16_t current_next = mtbl->mem[index].lru_next;

            mtbl->mem[current_prev].lru_next = current_next;
            mtbl->mem[current_next].lru_prev = current_prev;

            mtbl->mem[index].lru_prev = NIL16;
            mtbl->mem[index].lru_next = mtbl->lru_first;
        }
        mtbl->lru_first = index;
    }
}    

/** 
 * Searches the ftbl for the mtbl with the most entries.
 * Returns the number of memory entries the max mtbl has. The double ptr 
 * parameter is used to store a reference to the mtbl pointer with the most 
 * memory entries. 
 */
static uint16_t locate_max_fent(struct file_ent_s **fent)
{
    struct file_table_s *ftbl = &(ucache->ftbl);
    uint16_t value_of_max = 0;
    /* Iterate over file hash table indices */
    uint16_t i;
    for(i = 0; i < FILE_TABLE_HASH_MAX; i++)
    {

        if((ftbl->file[i].tag_handle == NIL64) ||
               (ftbl->file[i].tag_handle == 0))
            continue;

        /* Iterate over hash table chain */
        uint16_t j;
        for(j = i; !file_done(j); j = file_next(ftbl, j))
        {
            struct file_ent_s *current_fent = &(ftbl->file[j]);
            if((current_fent->mtbl_blk == NIL16) || 
                    (current_fent->mtbl_ent == NIL16))
            {
                break;
            }
            /* Examine the mtbl's value of num_blocks to see if it's the 
             * greatest. 
             */
            struct mem_table_s *current_mtbl = ucache_get_mtbl(
                current_fent->mtbl_blk,
                current_fent->mtbl_ent);

            if(current_mtbl->num_blocks >= value_of_max)
            {
                *fent = current_fent; /* Set the parameter to this mtbl */
                value_of_max = current_mtbl->num_blocks;
            }
        }
    }
    return value_of_max;
}

/** 
 * Evicts the LRU memory entry from the tail (lru_last) of the provided
 * mtbl's LRU list.
 * 
 * Returns 1 on success; 0 on failure, meaning there was no LRU
 * or that the block's lock couldn't be aquired.
 */
static int evict_LRU(struct file_ent_s *fent)
{
    int rc = 0;
    
    struct mem_table_s *mtbl = ucache_get_mtbl(fent->mtbl_blk, fent->mtbl_ent);

    if(mtbl->num_blocks != 0 && mtbl->lru_last != NIL16)
    {
        //printf("evicting: %hu\n", mtbl->lru_last);
        rc = remove_mem(fent, mtbl->mem[mtbl->lru_last].tag);
        if(rc == 1)
        {
            return 1;
        } 
    }
    return 0;
}


/** 
 * Used to obtain a block for storage of data identified by the offset 
 * parameter and maintained in the mtbl at the memory entry identified by the 
 * index parameter.
 *
 * If a free block could be aquired, returns the memory address of the block 
 * just inserted. Otherwise, returns NIL.
 */
static inline void *set_item(struct file_ent_s *fent, 
                    uint64_t offset, 
                    uint16_t index)
{
        uint16_t free_blk = get_free_blk();

        struct mem_table_s *mtbl = ucache_get_mtbl(fent->mtbl_blk, fent->mtbl_ent);

        /* No Free Blocks Available */
        if(free_blk == NIL16)
        {
            evict_LRU(fent); 
            free_blk = get_free_blk();
        }
        
        /* After Eviction Routine - No Free Blocks Available, Evict from mtbl 
         * with the most memory entries 
         */
        if(free_blk == NIL16)   
        {
            struct file_ent_s *max_fent = 0;
            struct mem_table_s *max_mtbl;
            uint16_t ment_count = 0;
            ment_count = locate_max_fent(&max_fent);
            max_mtbl = ucache_get_mtbl(max_fent->mtbl_blk, max_fent->mtbl_ent);
            if(ment_count == 0 || max_mtbl->lru_last == NIL16)
            {
                goto errout;
            }
            evict_LRU(max_fent);
            free_blk = get_free_blk();
        }
        /* TODO: other policy? */


        /* A Free Block is Avaiable for Use */
        if(free_blk != NIL16)
        {
            mtbl->num_blocks++;
            update_LRU(mtbl, index);
            /* set item to block number */
            mtbl->mem[index].tag = offset;
            mtbl->mem[index].item = free_blk;
            /* add block index to head of dirty list */
            mtbl->mem[index].dirty_next = mtbl->dirty_list;
            mtbl->dirty_list = index;
            /* Return the address of the block where data is stored */
            return (void *)&(ucache->b[free_blk]); 
        }
errout:
    return (void *)(NIL);
}

/** 
 * Requests a location in memory to place the data identified by the mtbl and 
 * offset parameters. Also inserts the necessary info into the mtbl.
 *
 */
static inline void *insert_mem(struct file_ent_s *fent, uint64_t offset,
                                              uint16_t *block_ndx)
{
    void* rc = 0;
    struct mem_table_s *mtbl = ucache_get_mtbl(fent->mtbl_blk, fent->mtbl_ent);

    /* Lookup first */
    void *returnValue = lookup_mem(mtbl, offset, block_ndx, NULL, NULL);
    if(returnValue != (void *)NIL)
    {
        /* Already exists in mtbl so just return a ptr to the blk */
        return returnValue;
    }

    /* Index into mem hash table */
    /* Hash to a bucket */
    uint16_t index = (uint16_t) ((offset / CACHE_BLOCK_SIZE) % MEM_TABLE_HASH_MAX);

    int evict_rc = 0;
    uint16_t mentIndex = get_free_ment(mtbl);
    if(mentIndex == NIL16)
    {   /* No free ment available, so attempt eviction, and try again */
        evict_rc = evict_LRU(fent);
        if(evict_rc == 1)
        {
            mentIndex = get_free_ment(mtbl);
        }
    }

    /* Eviction Failed */
    if(mentIndex == NIL16)
    {
        return (void *)NULL;
    }

    /* Procede with memory insertion if ment aquired */
    uint16_t next_ment = NIL16;
    /* Insert at head, keeping track of the previous head */
    next_ment = mtbl->bucket[index];
    mtbl->bucket[index] = mentIndex;

    rc = set_item(fent, offset, mentIndex);
    if(rc != (void *)NIL)
    {
        mtbl->mem[mentIndex].next = next_ment;
        *block_ndx = mtbl->mem[mentIndex].item;
        return rc;      
    }
    else
    {
        /* Restore the previous head back to head of the chain */
        mtbl->bucket[index] = next_ment;
        return (void *)NIL;   
    } 
}

/** 
 * Removes all table info regarding the block identified by the mtbl and
 * offset provided the block isn't locked. 
 *
 * Flushing the block to fs now occurs here upon removal from cache. 
 * 
 * On success returns 1, on failure returns 0.
 *
 */
static int remove_mem(struct file_ent_s *fent, uint64_t offset)
{
    struct mem_table_s *mtbl = ucache_get_mtbl(fent->mtbl_blk, fent->mtbl_ent);

    /* Some Indices */
    uint16_t item_index = NIL16; /* index of cached block */
    uint16_t mem_ent_index = NIL16;
    uint16_t mem_ent_prev_index = NIL16;

    void *retValue = lookup_mem(mtbl, offset, &item_index, &mem_ent_index, 
                                                     &mem_ent_prev_index);
    /* Verify we've recieved the necessary info */
    if(retValue == (void *)NIL)
    {
        return 0;
    }

    /* Verify the block isn't being used by trying the corresponding lock */
    ucache_lock_t *block_lock = get_lock(mtbl->mem[mem_ent_index].item);
    int rc = lock_trylock(block_lock);
    if(rc != 0)
    {
        return -1;
    }

    /* Aquire Lock */
    lock_lock(block_lock);

    /* Optionally flush block - may need to be mandatory */
    flush_block(fent, &(mtbl->mem[mem_ent_index]));

    /* Update First and Last...First */
    if(mem_ent_index == mtbl->lru_first)
    {
        /* Node is the head */
        mtbl->lru_first = mtbl->mem[mem_ent_index].lru_next;
    }
    if(mem_ent_index == mtbl->lru_last)
    {
        /* Node is the tail */
        mtbl->lru_last = mtbl->mem[mem_ent_index].lru_prev;
    }
    
    /* Remove from LRU */
    /* Update each of the adjacent nodes' link */
    uint16_t lru_prev = mtbl->mem[mem_ent_index].lru_prev;
    if(lru_prev != NIL16)
    {
        mtbl->mem[lru_prev].lru_next = mtbl->mem[mem_ent_index].lru_next;
    }
    uint16_t lru_next = mtbl->mem[mem_ent_index].lru_next;
    if(lru_next != NIL16)
    {
        mtbl->mem[lru_next].lru_prev = mtbl->mem[mem_ent_index].lru_prev;
    }

    /* Add memory block back to free list */
    put_free_blk(item_index);

    /* Repair link */
    if(mem_ent_prev_index != NIL16)
    {
        mtbl->mem[mem_ent_prev_index].next = mtbl->mem[mem_ent_index].next;
    }

    /* Newly free mem entry becomes new head of free mem entry list if index 
     * is less than hash table max 
     */
    put_free_ment(mtbl, mem_ent_index);
    mtbl->num_blocks--;

    /* Release Lock */
    lock_unlock(block_lock);
    return 1;
}

/* The following two functions are provided for error checking purposes. */
/**
 * Prints the Least Recently Used (LRU) list.
 */
void print_LRU(struct mem_table_s *mtbl)
{
    fprintf(out, "\tprinting lru list:\n");
    fprintf(out, "\t\tmru: %hu\n", mtbl->lru_first);
    fprintf(out, "\t\t\tmru->lru_prev = %hu\n\t\t\tmru->lru_next = %hu\n", 
        mtbl->mem[mtbl->lru_first].lru_prev, mtbl->mem[mtbl->lru_first].lru_next);
    uint16_t current = mtbl->mem[mtbl->lru_first].lru_next; 
    while(current != mtbl->lru_last && current != NIL16)
    {
        fprintf(out, "\t\t\tcurr->lru_prev = %hu\n", 
                       mtbl->mem[current].lru_prev);
        fprintf(out, "\t\t%hu\n", current);
        fprintf(out, "\t\t\tcurr->lru_next = %hu\n",
                       mtbl->mem[current].lru_next);
        current = mtbl->mem[current].lru_next;
    }
    fprintf(out, "\t\tlru: %hu\n", mtbl->lru_last);
    fprintf(out, "\t\t\tlru->lru_prev = %hu\n\t\t\tlru->lru_next = %hu\n", 
        mtbl->mem[mtbl->lru_last].lru_prev, mtbl->mem[mtbl->lru_last].lru_next);
}

/**
 * Prints the list of dirty (modified) blocks that should eventually be 
 * flushed to disk.
 */
void print_dirty(struct mem_table_s *mtbl)
{
    fprintf(out, "\tprinting dirty list:\n");
    int i;
    for(i = 0; !dirty_done(i); i = dirty_next(mtbl, i))
    {
        fprintf(out, "\t\tment index = %hu\t\t\tdirty_next = %hu\n", 
                                            i, dirty_next(mtbl, i));
    }
    if(i >= MEM_TABLE_ENTRY_COUNT && i != NIL16)
    {
        fprintf(out, "BAD MEM_TABLE_ENTRY INDEX: %hu\n", i);
        exit(0);
    } 
    fprintf(out, "\t\tdone w/ dirty list\n");
} 

/*  End of Internal Only Functions    */
#endif /* PVFS_UCACHE_ENABLE */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
