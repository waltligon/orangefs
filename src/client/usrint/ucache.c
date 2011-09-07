/* 
 * (C) 2011 Clemson University
 *
 * See COPYING in top-level directory.
 */

/* Experimental cache for user data
 * Currently under development.
 *
 * Note: When unsigned ints are set to NIL, their values are based on type:
 *   ex: 16      0xFFFF  
 *       32      0XFFFFFFFF
 *       64      0XFFFFFFFFFFFFFFFF 
 *   ALL EQUAL THE SIGNED REPRESENTATION OF -1, CALLED NIL IN ucache.h  
 */

#include "ucache.h"

/* Global Variables */
static FILE *out;                   /* For Logging Purposes */

static union user_cache_u *ucache;
static int ucache_blk_cnt;

ucache_lock_t *ucache_locks; /* will refer to the shmem of all ucache locks */
ucache_lock_t *ucache_lock;  /* Global Lock maintaining concurrency */
ucache_lock_t *ucache_block_lock;

/* Internal Only Function Declarations */

/* Locking 
int lock_init(ucache_lock_t * lock);
int lock_lock(ucache_lock_t * lock);
int lock_unlock(ucache_lock_t * lock);
int lock_destroy(ucache_lock_t * lock);
*/

/* Initialization */
static void add_free_mtbls(int blk);
static void init_memory_table(int blk, int ent);

/* Gets */
static int get_next_free_mtbl(uint32_t *free_mtbl_blk, uint16_t *free_mtbl_ent);
static int get_free_fent(void);
static int get_free_ment(struct mem_table_s *mtbl);
static uint16_t get_free_blk(void);

/* Puts */
static void put_free_mtbl(struct mem_table_s *mtbl, struct file_ent_s *file);
static void put_free_fent(int fent);
static void put_free_ment(struct mem_table_s *mtbl, int ent);
static void put_free_blk(int blk);

/* File Entry Chain Iterator */
static int file_done(int index);
static int file_next(struct file_table_s *ftbl, int index);

/* Memory Entry Chain Iterator */
static int ment_done(int index);
static int ment_next(struct mem_table_s *mtbl, int index);

/* Dirty List Iterator */
static int dirty_done(uint16_t index);
static int dirty_next(struct mem_table_s *mtbl, uint16_t index);

/* File and Memory Insertion */
static struct mem_table_s *insert_file(uint32_t fs_id, uint64_t handle);
static void *insert_mem(struct mem_table_s *mtbl, uint64_t offset, 
                                             uint32_t *block_ndx);
static void *set_item(struct mem_table_s *mtbl,uint64_t offset, uint16_t index);

/* File and Memory Lookup */
static struct mem_table_s *lookup_file(
    uint32_t fs_id, 
    uint64_t handle,
    uint32_t *file_mtbl_blk,    /* Can be NULL if not desired TODO: remove these later? */
    uint16_t *file_mtbl_ent,  
    uint16_t *file_ent_index,
    uint16_t *file_ent_prev_index
);
static void *lookup_mem(struct mem_table_s *mtbl, 
                    uint64_t offset, 
                    uint32_t *item_index,
                    uint16_t *mem_ent_index,
                    uint16_t *mem_ent_prev_index
);

/* File and Memory Entry Removal */
static int remove_file(uint32_t fs_id, uint64_t handle);
static void remove_all_memory_entries(struct mem_table_s *mtbl);
static int remove_mem(struct mem_table_s *mtbl, uint64_t offset);

/* Eviction Utilities */
static int evict_file(unsigned int index);
static int locate_max_mtbl(struct mem_table_s **mtbl);
static void update_lru(struct mem_table_s *mtbl, uint16_t index);
static int evict_LRU(struct mem_table_s *mtbl);

/* List Printing Functions */
/* 
 * static void print_lru(struct mem_table_s *mtbl);
 * static void print_dirty(struct mem_table_s *mtbl);
 */

/*  Externally Visible API
 *      The following functions are thread/processor safe regarding the cache 
 *      tables and data.      
 */

/**  Initializes the cache. 
 *  Mainly, it aquires a shared memory segment used to cache data. 
 * 
 *  This function also initializes the the FTBL and some MTBLs.
 *
 *  The whole cache is protected by a locking mechanism to maintain concurrency.
 *  Currently using posix semaphores
 */
void ucache_initialize(void)
{
    int i = 0;

    /* Aquire ptr to shared memory for ucache_locks */
    ucache_locks = shmat(shmget(ftok(GET_KEY_FILE, 'a'), 0, CACHE_FLAGS), NULL,
                                                                     AT_FLAGS);
    /* Global Cache lock stored in first LOCK_SIZE position */
    ucache_lock = ucache_locks; 

    /* Initialize Global Cache Lock */
    lock_init(ucache_lock);
    lock_lock(ucache_lock);

    /* The next BLOCKS_IN_CACHE number of block locks follow the global lock */
    ucache_block_lock = ucache_locks + 1;
    /* Initialize Block Level Locks */
    for(i = 0; i < BLOCKS_IN_CACHE; i++)
    {
        lock_init(get_block_lock(i));
    }

    /* Aquire ptr to shared memory for ucache */
    int key;
    int id;
    char *key_file_path;
    /*  Direct output   */
    if(!out)out = stdout;
    /* Note: had to set: kernel.shmmax amd kernel.shmall */
    /* set up shared memory region */
    key_file_path = GET_KEY_FILE;
    key = ftok(key_file_path, PROJ_ID);
    id = shmget(key, 0, CACHE_FLAGS);
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
        ucache->b[i].mtbl[0].free_list = i + 1;
    }
    ucache->b[ucache_blk_cnt - 1].mtbl[0].free_list = NIL;
    /* set up file hash table */
    for (i = 0; i < FILE_TABLE_HASH_MAX; i++)
    {
        ucache->ftbl.file[i].tag_handle = NIL;
        ucache->ftbl.file[i].tag_id = NIL;
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
        ucache->ftbl.file[i].next = i + 1;
    }
    ucache->ftbl.file[FILE_TABLE_ENTRY_COUNT - 1].next = NIL;
    lock_unlock(ucache_lock);
}

int ucache_open_file(PVFS_fs_id *fs_id, PVFS_handle *handle, 
                                    struct mem_table_s *mtbl
)
{
    lock_lock(ucache_lock);
    mtbl= lookup_file((uint32_t)(*fs_id), (uint64_t)(*handle), NULL, NULL, NULL,
                                                                          NULL);
    if((int)mtbl == NIL)
    {
        mtbl = insert_file((uint32_t)*fs_id, (uint64_t)*handle);
        if((int)mtbl == NIL)
        {   /* Error - Could not insert */
            lock_unlock(ucache_lock);
            return (-1);
        }
        else
        {
            /* File Inserted*/
            lock_unlock(ucache_lock);
            return 1;
        }
    }
    else
    {
        /* File was previously Inserted */
        lock_unlock(ucache_lock);
        return 0;
    }
}

/** Returns ptr to block in cache based on file and offset */
void *ucache_lookup(PVFS_fs_id *fs_id, PVFS_handle *handle, uint64_t offset, 
                                                      uint32_t *block_ndx)
{
    lock_lock(ucache_lock);
    struct mem_table_s *mtbl = lookup_file(
        (uint32_t)(*fs_id), (uint64_t)(*handle), NULL, NULL, NULL, NULL);
    if((int)mtbl != NIL)
    {
        char *retVal = (char *)lookup_mem(mtbl, (uint64_t)offset, block_ndx, 
                                                                NULL, NULL);
        lock_unlock(ucache_lock);
        return((void *)retVal); 
    }
    lock_unlock(ucache_lock);
    return (void *)NIL;
}

/** Prepares the data structures for block storage. 
 * On success, returns a pointer to where the block of data should be written. 
 * On failure, returns NIL.
 */
void *ucache_insert(PVFS_fs_id *fs_id, PVFS_handle *handle, uint64_t offset, 
                                                        uint32_t *block_ndx)
{
    lock_lock(ucache_lock);
    struct mem_table_s *mtbl = lookup_file(
        (uint32_t)(*fs_id), (uint64_t)(*handle), NULL, NULL, NULL, NULL);
    if((int)mtbl == NIL)
    {
        lock_unlock(ucache_lock);
        return (void *)NIL;
    }
    else
    {
        if(remove_mem(mtbl, (uint64_t)offset) == NIL)
        {
            lock_unlock(ucache_lock);
            return (void *)NIL;
        }
        char * retVal = insert_mem(mtbl, (uint64_t)offset, block_ndx);
        lock_unlock(ucache_lock);
        return ((void *)retVal); 
    }
}

/**  Removes a cached block of data from mtbl */
int ucache_remove(PVFS_fs_id *fs_id, PVFS_handle *handle, uint64_t offset)
{
    lock_lock(ucache_lock);
    struct mem_table_s *mtbl = lookup_file(
        (uint32_t)(*fs_id), (uint64_t)(*handle), NULL, NULL, NULL, NULL);
    if((int)mtbl != NIL)
    {
        int retVal = remove_mem(mtbl, (uint64_t)offset);
        lock_unlock(ucache_lock); 
        return retVal; 
    }        
    lock_unlock(ucache_lock);
    return NIL;
}

/** Flushes dirty blocks to the I/O Nodes */
int ucache_flush(pvfs_descriptor *pd)
{
    PVFS_fs_id *fs_id = &(pd->pvfs_ref.fs_id);
    PVFS_handle *handle = &(pd->pvfs_ref.handle);
    lock_lock(ucache_lock);
    struct mem_table_s *mtbl = lookup_file((uint32_t)(*fs_id), 
                 (uint64_t)(*handle), NULL, NULL, NULL, NULL);
    if((int)mtbl == NIL)
    {
        return NIL;
    }
    int i;
    for(i = mtbl->dirty_list; !dirty_done(i); i = dirty_next(mtbl, i))
    {
        struct mem_ent_s *ment = &(mtbl->mem[i]);
        mtbl->mem[i].dirty_next = NIL;
        if((int64_t)ment->tag == NIL || (int32_t)ment->item == NIL)
        {
            break;
        }

        /* Send it to the nodes */
        int rc = 0;
        size_t count = CACHE_BLOCK_SIZE_K * 1024;
        PVFS_Request freq, mreq;
        memset(&freq, 0, sizeof(freq));
        memset(&mreq, 0, sizeof(mreq));
        rc = PVFS_Request_contiguous(count, PVFS_BYTE, &freq);
        rc = PVFS_Request_contiguous(count, PVFS_BYTE, &mreq);
        /* flush block to disk. 2 means write */
        /* 
        iocommon_readorwrite(2, pd, ment->tag, 
                     &(ucache->b[ment->item].mblk[0]), 
                                          mreq, freq);
        */
        PVFS_Request_free(&freq);
        PVFS_Request_free(&mreq);
    }
    mtbl->dirty_list = NIL;
    lock_unlock(ucache_lock);
    return 1;
}

/** Removes all memory entries in the mtbl corresponding to the file info 
 * provided as parameters. It also removes the mtbl and the file entry from 
 * the cache.
 */
int ucache_close_file(PVFS_fs_id *fs_id, PVFS_handle *handle)
{
    lock_lock(ucache_lock);
    uint32_t file_mtbl_blk;
    uint16_t file_mtbl_ent;
    uint16_t file_ent_index;
    uint16_t file_ent_prev_index;
    struct mem_table_s *mtbl = lookup_file(
        (uint32_t)(*fs_id), 
        (uint64_t)(*handle), 
        &file_mtbl_blk, 
        &file_mtbl_ent, 
        &file_ent_index, 
        &file_ent_prev_index);
    if((int)mtbl == NIL)
    {
        lock_unlock(ucache_lock);
        return NIL;
    }
    remove_all_memory_entries(mtbl);
    struct file_ent_s *file = &(ucache->ftbl.file[file_ent_index]);
    put_free_mtbl(mtbl, file);
    put_free_fent(file_ent_index);
    lock_unlock(ucache_lock);
    return 1;
}

/** Decrements the reference count of a particular mtbl. */
void ucache_dec_ref_cnt(struct mem_table_s * mtbl)
{
    lock_lock(ucache_lock);
    /* decrement ref_cnt of mtbl */
    if(mtbl->ref_cnt != 0)
    {
        mtbl->ref_cnt--;
    }
    lock_unlock(ucache_lock); 
}

/** Increments the reference count of a particular mtbl. */
void ucache_inc_ref_cnt(struct mem_table_s * mtbl)
{
    lock_lock(ucache_lock);
    /* increment ref_cnt of mtbl */
    mtbl->ref_cnt++;
    lock_unlock(ucache_lock); 
}

/** Dumps all cache related information. */
void ucache_info(
    FILE *out,
    union user_cache_u *ucache, 
    ucache_lock_t *ucache_lock)
{
    fprintf(out, "\n#defines:\n");
    /* First, print many of the #define values */
    fprintf(out, "MEM_TABLE_ENTRY_COUNT = %d\n", MEM_TABLE_ENTRY_COUNT);
    fprintf(out, "FILE_TABLE_ENTRY_COUNT = %d\n", FILE_TABLE_ENTRY_COUNT);
    fprintf(out, "CACHE_BLOCK_SIZE_K = %d\n", CACHE_BLOCK_SIZE_K);
    fprintf(out, "MEM_TABLE_HASH_MAX = %d\n", MEM_TABLE_HASH_MAX);
    fprintf(out, "FILE_TABLE_HASH_MAX = %d\n", FILE_TABLE_HASH_MAX);
    fprintf(out, "MTBL_PER_BLOCK  = %d\n", MTBL_PER_BLOCK );
    fprintf(out, "GET_KEY_FILE = %s\n", GET_KEY_FILE);
    fprintf(out, "PROJ_ID = %d\n", PROJ_ID);
    fprintf(out, "BLOCKS_IN_CACHE = %d\n", BLOCKS_IN_CACHE);
    fprintf(out, "CACHE_SIZE = %d(B)\t%d(MB)\n", CACHE_SIZE, 
                                    (CACHE_SIZE/(1024*1024)));
    fprintf(out, "AT_FLAGS = %d\n", AT_FLAGS);
    fprintf(out, "SVSHM_MODE = %d\n", SVSHM_MODE);
    fprintf(out, "CACHE_FLAGS = %d\n", CACHE_FLAGS);
    fprintf(out, "NIL = %d\n\n", NIL);    

    /* Lock's Shared Memory Info */
    fprintf(out, "address of ucache_lock ptr:\t0X%X\n", 
                            (unsigned int) &ucache_lock);
    fprintf(out, "ucache_lock ptr:\t\t0X%X\n", (unsigned int) ucache_lock);
    /* Lock's Value */
    int lockValue = 0;
    #if LOCK_TYPE == 0
    ucache_lock_getvalue(ucache_lock, &lockValue);
    #endif
    fprintf(out, "ucache_lock value = %d\n", lockValue);
    /* ucache Shared Memory Info */
    fprintf(out, "address of ucache ptr:\t0X%X\n", (unsigned int)&ucache);
    fprintf(out, "ucache ptr:\t\t0X%X\n", (unsigned int)ucache);

    struct file_table_s *ftbl = &(ucache->ftbl);

    /* FTBL Info */
    fprintf(out, "ftbl ptr:\t\t0X%X\n", (unsigned int)&(ucache->ftbl));
    fprintf(out, "free_blk = %d\n", (int32_t)ftbl->free_blk);
    fprintf(out, "free_mtbl_blk = %d\n", (int32_t)ftbl->free_mtbl_blk);
    fprintf(out, "free_mtbl_ent = %d\n", (int16_t)ftbl->free_mtbl_ent);
    fprintf(out, "free_list = %d\n", (int16_t)ftbl->free_list);

    /* Other Free Blocks */
    fprintf(out, "\nIterating Over Free Blocks:\n\n");
    unsigned int i;
    for(i = ftbl->free_blk; (int16_t)i != NIL; i = ucache->b[i].mtbl[0].
                                                              free_list)
    {
        fprintf(out, "Free Block:\tCurrent: %d\tNext: %d\n", i, 
                               ucache->b[i].mtbl[0].free_list);
    }
    fprintf(out, "End of Free Blocks List\n");

    /* Iterate Over Free Mtbls */
    fprintf(out, "\nIterating Over Free Mtbls:\n");
    uint32_t current_blk = ftbl->free_mtbl_blk;
    uint16_t current_ent = ftbl->free_mtbl_ent;
    while((int16_t)current_blk != NIL)
    {
        fprintf(out, "free mtbl: block = %d\tentry = %d\n", 
                (int16_t)current_blk, (int16_t)current_ent);
        current_blk = (uint16_t)ucache->b[current_blk].mtbl[current_ent].
                                                            free_list_blk;
        current_ent = (uint16_t)ucache->b[current_blk].mtbl[current_ent].
                                                                free_list;
    }
    fprintf(out, "End of Free Mtbl List\n\n");
    
    /* Iterating Over Free File Entries */
    fprintf(out, "Iterating Over Free File Entries:\n");
    uint16_t current_fent; 
    for(current_fent = ftbl->free_list; (int16_t)current_fent != NIL; 
                        current_fent = ftbl->file[current_fent].next)
    {
        fprintf(out, "free file entry: index = %d\n", (int16_t)current_fent);
    }
    fprintf(out, "End of Free File Entry List\n\n");

    fprintf(out, "Iterating Over File Entries in Hash Table:\n\n");
    /* iterate over file table entries */
    for(i = 0; i < FILE_TABLE_HASH_MAX; i++)
    {
        if(((int64_t)ftbl->file[i].tag_handle != NIL) && 
               ((int64_t)ftbl->file[i].tag_handle != 0))
        {
            /* iterate accross file table chain */
            int j;
            for(j = i; !file_done(j); j = file_next(ftbl, j))
            {
                fprintf(out, "FILE ENTRY INDEX %d ********************\n", j);
                struct file_ent_s * fent = &(ftbl->file[j]);
                fprintf(out, "tag_handle = 0X%llX\n", 
                            (uint64_t)fent->tag_handle);
                fprintf(out, "tag_id = 0X%X\n", (uint32_t)fent->tag_id);
                fprintf(out, "mtbl_blk = %d\n", (int32_t)fent->mtbl_blk);
                fprintf(out, "mtbl_ent = %d\n", (int16_t)fent->mtbl_ent);
                fprintf(out, "next = %d\n", (int16_t)fent->next);

                struct mem_table_s * mtbl = &(ucache->b[fent->mtbl_blk].
                                                    mtbl[fent->mtbl_ent]);
                fprintf(out, "\tMTBL INFO ********************\n");
                fprintf(out, "\tnum_blocks = %d\n", (int16_t)mtbl->num_blocks);
                fprintf(out, "\tfree_list = %d\n", (int16_t)mtbl->free_list); 
                fprintf(out, "\tfree_list_blk = %d\n", 
                        (int16_t)mtbl->free_list_blk);
                fprintf(out, "\tlru_first = %d\n", (int16_t)mtbl->lru_first);
                fprintf(out, "\tlru_last = %d\n", (int16_t)mtbl->lru_last);
                fprintf(out, "\tdirty_list = %d\n", (int16_t)mtbl->dirty_list);
                fprintf(out, "\tref_cnt = %d\n\n", (int16_t)mtbl->ref_cnt);

                /* Iterate Over Memory Entries */
                int k;
                for(k = 0; k < MEM_TABLE_HASH_MAX; k++)
                {
                    if(((int64_t)mtbl->mem[k].tag != NIL) && 
                            ((int64_t)mtbl->mem[k].tag != 0))
                    {
                        int l;
                        for(l = k; !ment_done(l); l = ment_next(mtbl, l)){
                            struct mem_ent_s * ment = &(mtbl->mem[l]);
                            fprintf(out, "\t\tMEMORY ENTRY INDEX %d ***********"
                                                              "*********\n", l);
                            fprintf(out, "\t\ttag = 0X%llX\n", 
                                            (uint64_t)ment->tag);
                            fprintf(out, "\t\titem = %d\n", 
                                            (int32_t)ment->item);
                            fprintf(out, "\t\tnext = %d\n", 
                                            (int16_t)ment->next);
                            fprintf(out, "\t\tdirty_next = %d\n", 
                                        (int16_t)ment->dirty_next);
                            fprintf(out, "\t\tlru_next = %d\n", 
                                            (int16_t)ment->lru_next);
                            fprintf(out, "\t\tlru_prev = %d\n\n", 
                                            (int16_t)ment->lru_prev);
                        } 
                    }
                    else
                        if((uint16_t)mtbl->num_blocks != 0)
                        {
                            fprintf(out, "\tvacant memory entry @ index = %d\n",
                                                                             k);
                        }
                }
            }
            fprintf(out, "End of chain @ Hash Table Index %d\n\n", i);
        }
        else
            fprintf(out, "vacant file entry @ index = %d\n", i);
    }
}

/** Returns a pointer to the block level lock corresponding to the block_index.
 */
ucache_lock_t *get_block_lock(int block_index)
{
    /* TODO: check this out */
    return (ucache_block_lock + block_index);
}

/***************************************** End of Externally Visible API */


/* Beginning of internal only (static) functions */

/** Initializes the proper lock based on the LOCK_TYPE */
int lock_init(ucache_lock_t * lock)
{
    /*  Set pshared (2nd arg) to non-zero value to share semaphore b/w forked 
     *  processes
     */
    /* TODO: ability to disable locking */
    #if LOCK_TYPE == 0
    return sem_init(lock, 1, 1);
    #elif LOCK_TYPE == 1
    return pthread_mutex_init(lock, NULL);
    #elif LOCK_TYPE == 2
    return pthread_spin_init(lock, 1);
    #endif
}

/** Returns 0 when lock is locked; otherwise, return -1 and sets errno */
int lock_lock(ucache_lock_t * lock)
{
    #if LOCK_TYPE == 0
    return sem_wait(lock);
    #elif LOCK_TYPE == 1
    return pthread_mutex_lock(lock);
    #elif LOCK_TYPE == 2
    return pthread_spin_lock(lock);
    #endif   
}

/** If successful, return zero; otherwise, return -1 and sets errno */
int lock_unlock(ucache_lock_t * lock)
{
    #if LOCK_TYPE == 0
    return sem_post(lock);
    #elif LOCK_TYPE == 1
    //printf("lock size = %d\n", sizeof(*lock));
    return pthread_mutex_unlock(lock);
    #elif LOCK_TYPE == 2
    return pthread_spin_unlock(lock);
    #endif
}

/** Upon successful completion, returns zero; 
 * Otherwise, returns -1 and sets errno.
 */
#if (LOCK_TYPE == 0)
int ucache_lock_getvalue(ucache_lock_t * lock, int *sval)
{
    return sem_getvalue(lock, sval);
}
#endif

/** Upon successful completion, returns zero.
 * Otherwise, returns 1 and sets errno.
 */
int lock_destroy(ucache_lock_t * lock)
{
    #if (LOCK_TYPE == 0)
    return sem_destroy(lock); 
    #elif (LOCK_TYPE == 1)
    return pthread_mutex_destroy(lock);
    #elif (LOCK_TYPE == 2)
    return pthread_spin_destroy(lock);
    #endif
}

/* Dirty List Iterator */
/** Returns true if current index is NIL, otherwise, returns 0 */
static int dirty_done(uint16_t index)
{
    return ((int16_t)index == NIL);
}

/** Returns the next index in the dirty list for the provided mtbl and index */
static int dirty_next(struct mem_table_s *mtbl, uint16_t index)
{
    return mtbl->mem[index].dirty_next;
}

/*  Memory Entry Chain Iterator */
/** Returns true if current index is NIL, otherwise, returns 0 */
static int ment_done(int index)
{
    return ((int16_t)index == NIL);
}

/** Returns the next index in the memory entry chain for the provided mtbl 
 * and index. 
 */
static int ment_next(struct mem_table_s *mtbl, int index)
{
    return mtbl->mem[index].next;
}

/*  File Entry Chain Iterator   */
/** Returns true if current index is NIL, otherwise, returns 0 */
static int file_done(int index)
{
    return ((int16_t)index == NIL);
}

/** Returns the next index in the file entry chain for the provided mtbl 
 * and index. 
 */
static int file_next(struct file_table_s *ftbl, int index)
{
    return ftbl->file[index].next;
}

/**This function should only be called when the ftbl has no free mtbls. 
 * It initizializes MTBL_PER_BLOCK additional mtbls in the block provided,
 * meaning this block will no longer be used for storing file data but 
 * hash table related data instead.
 */
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

/** Initializes a mtbl which is a hash table of memory entries.
 * The mtbl will be located at the provided entry index within 
 * the provided block.
 */
static void init_memory_table(int blk, int ent)
{
    int i;
    struct mem_table_s *mtbl = &(ucache->b[blk].mtbl[ent]);
    mtbl->lru_first = NIL;
    mtbl->lru_last = NIL;
    mtbl->dirty_list = NIL;
    mtbl->num_blocks = 0;
    mtbl->ref_cnt = 0;
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
        mtbl->mem[i].next = i + 1;
    }
    mtbl->mem[MEM_TABLE_ENTRY_COUNT - 1].next = NIL;
}

/** This function asks the file table if a free block is avaialable. 
 * If so, returns the block's index; otherwise, returns NIL.
 */
static uint16_t get_free_blk(void)
{
    struct file_table_s *ftbl = &(ucache->ftbl);
    uint16_t free_blk = ftbl->free_blk;
    if((int16_t)free_blk != NIL)
    {   
        /* Use mtbl index zero since free_blks have no ititialized mem tables */
        ftbl->free_blk = ucache->b[free_blk].mtbl[0].free_list; 
        return free_blk;
    }
    return NIL;

}

/** Accepts an index corresponding to a block that is put back on the file 
 * table free list.
 */
static void put_free_blk(int blk)
{
    if(DBG)fprintf(out, "freeing blk @ index = %d\n", blk);
    struct file_table_s *ftbl = &(ucache->ftbl);
    /* set the block's next value to the current head of the block free list */
    ucache->b[blk].mtbl[0].free_list = ftbl->free_blk;
    /* set this to NIL, since only used when mtbl is on mtbl free list */
    ucache->b[blk].mtbl[0].free_list_blk = NIL; 
    /* blk is now the head of the ftbl blk free list */
    ftbl->free_blk = blk;
}

/** Consults the file table to retrieve an index corresponding to a file entry
 * If available, returns the file entry index, otherwise returns NIL.
 */
static int get_free_fent(void)
{
    if(DBG)fprintf(out, "trying to get free file entry...\n");
    struct file_table_s *ftbl = &(ucache->ftbl);
    uint16_t entry = ftbl->free_list;
    if((int16_t)entry != NIL)
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

/** Places the file entry located at the provided index back on the file table's
 * free file entry list. If the index is < FILE_TABLE_HASH_MAX, then set next 
 * to NIL since this index must remain the head of the linked list. Otherwise,
 * set next to the current head of fent free list and set the free list head to
 * the provided index.
 */
static void put_free_fent(int fent)
{
    if(DBG)fprintf(out, "freeing file entry = %d\n", fent);
    struct file_table_s *ftbl = &(ucache->ftbl);
    ftbl->file[fent].tag_handle = NIL;
    ftbl->file[fent].tag_id = NIL;
    if(fent<FILE_TABLE_HASH_MAX)
    {
        ftbl->file[fent].next = NIL;
    }
    else
    {
        /* Set next index to the current head of the free list */
        ftbl->file[fent].next = ftbl->free_list;
        /* Set fent index as the head of the free_list */
        ftbl->free_list = fent;
    }
}

/** Consults the provided mtbl's memory entry free list to get the index of the
 * next free memory entry. Returns the index if one is available, otherwise 
 * returns NIL.
 */
static int get_free_ment(struct mem_table_s *mtbl)
{
    uint16_t ment = mtbl->free_list;
    if((int16_t)ment != NIL)
    {
        mtbl->free_list = mtbl->mem[ment].next;
        mtbl->mem[ment].next = NIL;
/*      mtbl->mem[ment].lru_prev = NIL;
        mtbl->mem[ment].lru_next = NIL; */
    }
    return ment;
}

/** Puts the memory entry corresponding to the provided mtbl and entry index 
 * back on the mtbl's memory entry free list. 
 *
 * If the entry index is < MEM_TABLE_HASH_MAX, then set next to NIL since this 
 * index must remain the head of the linked list. Otherwise, set next to the 
 * current head of ment free list and set the free list head to the provided 
 * index.
 */
static void put_free_ment(struct mem_table_s *mtbl, int ent)
{
    if(DBG)fprintf(out, "freeing memory entry = %d\n", ent);
    /* Reset ment values */
    mtbl->mem[ent].tag = NIL;
    mtbl->mem[ent].item = NIL;
    mtbl->mem[ent].dirty_next = NIL;
    mtbl->mem[ent].lru_next = NIL;
    mtbl->mem[ent].lru_prev = NIL;
    if(ent<MEM_TABLE_HASH_MAX)
    {
        //mtbl->mem[ent].next = NIL;    NOOOOOOOOOOOOOOOOOOOOO!!!!!!!!!!!!
    }
    else
    {
        /* Set next index to the current head of the free list */
        mtbl->mem[ent].next = mtbl->free_list;
        /* Update free list to include this entry */
        mtbl->free_list = ent;
    }
}

/** Perform a file lookup on the ucache using the provided fs_id and handle.
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
    uint32_t *file_mtbl_blk,
    uint16_t *file_mtbl_ent,
    uint16_t *file_ent_index,
    uint16_t *file_ent_prev_index
)
{
    if(DBG)fprintf(out, "performing lookup...\n");

    /* Index into file hash table */
    int index = handle % FILE_TABLE_HASH_MAX;
    if(DBG)fprintf(out, "\thashed index: %d\n", index);

    struct file_table_s *ftbl = &(ucache->ftbl);
    struct file_ent_s *current = &(ftbl->file[index]);

    /* previous, current, next fent index */
    uint16_t p = NIL;
    uint16_t c = index;
    uint16_t n = current->next;

    while(1)
    {
        if(DBG)fprintf(out, "\tp=%d\tc=%d\tn=%d\n", (int16_t)p, (int16_t)c, 
                                                                (int16_t)n);
        if((current->tag_id == fs_id) && (current->tag_handle == handle))
        {
            if(DBG)fprintf(out, "\tFile located in chain\n");
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
            if((int16_t)current->next == NIL)
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

/** Function that locates the next free mtbl.
 * On success, Returns 1 and sets reference parameters to proper indexes.
 * On failure, returns NIL; 
 */
static int get_next_free_mtbl(uint32_t *free_mtbl_blk, uint16_t *free_mtbl_ent)
{
        struct file_table_s *ftbl = &(ucache->ftbl);

        /* Get next free mtbl_blk and ent from ftbl */
        *free_mtbl_blk = ftbl->free_mtbl_blk;
        *free_mtbl_ent = ftbl->free_mtbl_ent;

        /* Is free mtbl_blk available? */
        if(((int32_t)*free_mtbl_blk == NIL) || ((int16_t)*free_mtbl_ent == NIL))
        { 
            return NIL;
        }

        /* Update ftbl to contain new next free mtbl */
        ftbl->free_mtbl_blk = ucache->b[*free_mtbl_blk].mtbl[*free_mtbl_ent].
                                                                free_list_blk;
        ftbl->free_mtbl_ent = ucache->b[*free_mtbl_blk].mtbl[*free_mtbl_ent].
                                                                    free_list;

        /* Set free info to NIL - NECESSARY? */
        ucache->b[*free_mtbl_blk].mtbl[*free_mtbl_ent].free_list = NIL;
        ucache->b[*free_mtbl_blk].mtbl[*free_mtbl_ent].free_list_blk = NIL;
        return 1;
}

/** Removes all memory entries belonging to mtbl and places corresponding blocks 
 * back on the ftbl block free list, provided the mtbl->ref_cnt is 0.  
 */
static void remove_all_memory_entries(struct mem_table_s *mtbl)
{
    /* Don't do anything if this mtbl is referenced.*/
    if(mtbl->ref_cnt != 0)
    {
        return;
    }

    int i;
    for(i = 0; i < MEM_TABLE_HASH_MAX; i++)
    {
        if(DBG)fprintf(out, "\tremoving memory entry %d\n", i);
        int j;
        for(j = i;!ment_done(j); j = ment_next(mtbl, j))
        {
            /* Current Memory Entry */
            struct mem_ent_s *ment = &(mtbl->mem[j]);
            if(DBG)fprintf(out, "\t(tag, item)=(%lld, %d)\n", 
                    (int64_t)ment->tag,(int32_t)ment->item);
            /*  Account for empty head of ment chain    */
            if(((int64_t)ment->tag == NIL) || ((int32_t)ment->item == NIL))
            {
                break;
            }
            put_free_blk(ment->item);
            put_free_ment(mtbl, j);
        }
    }
}

/** Places the provided mtbl back on the ftbl's mtbl free list provided it 
 * isn't currently referenced.
 */
static void put_free_mtbl(struct mem_table_s *mtbl, struct file_ent_s *file)
{
    /* Don't do anything if this mtbl is referenced.*/
    if(mtbl->ref_cnt != 0)
    {
        return;
    }

    /* Remove mtbl */
    mtbl->num_blocks = 0;   /* number of used blocks in this mtbl */
    mtbl->lru_first = NIL;  /* index of first block on lru list */
    mtbl->lru_last = NIL;   /* index of last block on lru list */
    mtbl->dirty_list = NIL; /* index of first dirty block */
    mtbl->ref_cnt = 0;      /* number of clients using this record */

    /* Add mem_table back to free list */
    /* Temporarily store copy of current head (the new next) */
    uint32_t tmp_blk = ucache->ftbl.free_mtbl_blk;
    uint16_t tmp_ent = ucache->ftbl.free_mtbl_ent;
    /* newly free mtbl becomes new head of free mtbl list */
    ucache->ftbl.free_mtbl_blk = file->mtbl_blk;
    ucache->ftbl.free_mtbl_ent = file->mtbl_ent;
    /* Point to the next free mtbl (the former head) */
    mtbl->free_list_blk = tmp_blk;
    mtbl->free_list = tmp_ent;
}

/** Evict the file entry at the provided index.
 * On Success, returns 1. If file is currently referenced, do not evict and 
 * return 0.
 */
/*  evict the file @ index, must be less than FILE_TABLE_HASH_MAX   */
static int evict_file(unsigned int index)
{
    if(DBG)fprintf(out, "evicting data @ index %d...\n", index);
    struct file_ent_s *file = &(ucache->ftbl.file[index]);
    struct mem_table_s *mtbl = 
        &(ucache->b[file->mtbl_blk].mtbl[file->mtbl_ent]);

    if(mtbl->ref_cnt != 0){
        return 0;
    }

    remove_all_memory_entries(mtbl);
    put_free_mtbl(mtbl, file);

    /* Prepare fent for its next occupant */
    file->tag_handle = NIL;
    file->tag_id = NIL;
    file->mtbl_blk = NIL;   /* block index of this mtbl */
    file->mtbl_ent = NIL;   /* entry index of this mtbl */
    return 1;
}

/** Insert information about file into ucache (no file data inserted)
 * Returns pointer to mtbl on success.
 * 
 * Returns NIL if necessary data structures could not be aquired from the free
 * lists or through an eviction policy (meaning references are held).
 */
static struct mem_table_s *insert_file(uint32_t fs_id, uint64_t handle)
{
    if(DBG)fprintf(out, "trying to insert file...\n");
    struct file_table_s *ftbl = &(ucache->ftbl);
    struct file_ent_s *current;     /* Current ptr for iteration */
    uint16_t free_fent = NIL;       /* Index of next free fent */
    /* index into file hash table */
    unsigned int index = handle % FILE_TABLE_HASH_MAX;
    if(DBG)fprintf(out, "\thashed index: %u\n", index);
    current = &(ftbl->file[index]);
    /* Get free mtbl */
    uint32_t free_mtbl_blk = NIL;
    uint16_t free_mtbl_ent = NIL; 
    /* Create free mtbls if none are available */
    if(get_next_free_mtbl(&free_mtbl_blk, &free_mtbl_ent) != 1)
    {   
        if((int16_t)ucache->ftbl.free_blk == NIL) 
        {
            /* Evict a block from mtbl with most mem entries */
            struct mem_table_s *max_mtbl;
            locate_max_mtbl(&max_mtbl);
            if(DBG)fprintf(out, "\tget_free_mtbl returned NIL\n");
            if(DBG)fprintf(out, "\tevicting blk from max mtbl: has %d entries\n"
                                                        ,max_mtbl->num_blocks);
            evict_LRU(max_mtbl);
        }
        /* Intitialize remaining blocks' memory tables */
        if((int16_t)ucache->ftbl.free_blk != NIL)
        {
            if(DBG)fprintf(out, "\tadding memory tables to free block\n");
            add_free_mtbls(ucache->ftbl.free_blk);
            get_next_free_mtbl(&free_mtbl_blk, &free_mtbl_ent);
        }
    }

    /* Insert at index, relocating data @ index if occupied */
    if(((int32_t)current->mtbl_blk != NIL) && 
        ((int16_t)current->mtbl_ent != NIL)) 
    {
        if(DBG)fprintf(out, "\tmust relocate head data\n");
        /* get free file entry and update ftbl */
        free_fent = get_free_fent();
        if((int16_t)free_fent != NIL)
        {
            /* copy data from 1 struct to the other */
            ftbl->file[free_fent] = *current;   
            /* point new head's "next" to former head */
            current->next = free_fent;  
        }
        else
        {   /* No free file entries available: policy? */
            if(DBG)fprintf(out, "\tno free file entries\n");
            /* Attempt to evict a file in a chain corresponding to the hashed 
             * index into the ftbl.
             * Return NIL if it isn't possible.
             */
            /* Stop when the file has been evicted or end of chain reached */
            int file_evicted = evict_file(index);
            int i;
            for(i = index; !file_done(i) && (!file_evicted); i = 
                                            file_next(ftbl,index))
            {
                i = ftbl->file[i].next;
                current = &(ftbl->file[i]);
                file_evicted = evict_file((unsigned int)i);
            }
            if(!file_evicted)
            {
                /* Could not get file entry */
                return (struct mem_table_s *)NIL;
            }
        }
    }
    else
    {
        if(DBG)fprintf(out, "\tno head data @ index\n");
    }
    /* Insert file data @ index */
    current->tag_id = fs_id;
    current->tag_handle = handle;
    /* Update fent with it's new mtbl: blk and ent */
    current->mtbl_blk = free_mtbl_blk;
    current->mtbl_ent = free_mtbl_ent;
    /* Initialize Memory Table */
    init_memory_table(free_mtbl_blk, free_mtbl_ent);
    if(DBG)fprintf(out, "\trecieved free memory table: 0X%X\n", 
        (unsigned int)&(ucache->b[current->mtbl_blk].mtbl[current->mtbl_ent]));
    return &(ucache->b[current->mtbl_blk].mtbl[current->mtbl_ent]);
}

/** Remove file entry and memory table of file identified by parameters
 * Returns 1 following removal
 * Returns NIL if file is referenced or if the file could not be located.
 */
static int remove_file(uint32_t fs_id, uint64_t handle)
{
    if(DBG)fprintf(out, "trying to remove file...\n");
    uint32_t file_mtbl_blk = NIL;
    uint16_t file_mtbl_ent = NIL;
    uint16_t file_ent_index = NIL;
    uint16_t file_ent_prev_index = NIL;

    struct file_table_s *ftbl = &(ucache->ftbl);
    struct mem_table_s *mtbl = lookup_file(fs_id, handle, &file_mtbl_blk, 
                    &file_mtbl_ent, &file_ent_index, &file_ent_prev_index);

    if(mtbl->ref_cnt != 0)
    {
        if(DBG)fprintf(out, "\tremoval failure: ref_cnt==%d\n", mtbl->ref_cnt);
        return NIL;
    }

    /* Verify we've recieved the necessary info */
    if(((int32_t)file_mtbl_blk == NIL) || ((int16_t)file_mtbl_ent == NIL))
    {
        if(DBG)fprintf(out, "\tremoval failure: no mtbl matching file\n");
        return NIL;
    }

    /* Free the mtbl */
    put_free_mtbl(mtbl, &(ftbl->file[file_ent_index]));

    /* Free the file entry */
    ftbl->file[file_ent_prev_index].next = ftbl->file[file_ent_index].next;
    put_free_fent(file_ent_index);

    if(DBG)fprintf(out, "\tremoval sucessful\n");
    return(1);
}

/** Lookup the memory location of a block of data in cache that is identified 
 * by the mtbl and offset parameters.
 *
 * If located, returns a pointer to memory where the desired block of data is 
 * stored. Otherwise, NIL is returned.
 *
 * Additional parameters (references) may be used that will be set to values 
 * pertaining to the memory entry's location. If NULLs are passed in place of 
 * these parameters, then they will not be set.
 */
static void *lookup_mem(struct mem_table_s *mtbl, 
                    uint64_t offset, 
                    uint32_t *item_index,
                    uint16_t *mem_ent_index,
                    uint16_t *mem_ent_prev_index)
{
    if(DBG)fprintf(out, "performing ment lookup...\n");

    /* index into mem hash table */
    unsigned int index = offset % MEM_TABLE_HASH_MAX;
    if(DBG)fprintf(out, "\toffset: 0X%llX hashes to index: %u\n", offset, 
                                                              index);

    struct mem_ent_s *current = &(mtbl->mem[index]);

    /* previous, current, next memory entry index in mtbl */
    int16_t p = NIL;
    int16_t c = index;
    int16_t n = current->next;  

    while(1)
    {
        if(offset == current->tag)
        {
            if(DBG)fprintf(out, "\tmatch located: 0X%llX == 0X%llX\n", offset, 
                                                            current->tag);

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
            if((int16_t)current->next == NIL)
            {
                if(DBG)fprintf(out, "\tmemory entry not found\n");
                if(DBG)fprintf(out, "\tno more & no match: 0X%llX != 0X%llX\n", 
                                                         offset, current->tag);
                return (struct mem_table_s *)NIL;
            }
            else
            {
                if(DBG)fprintf(out, "\tno match, iterating: 0X%llX != 0X%llX\n", 
                                                          offset, current->tag);
                /* Iterate */
                current = &(mtbl->mem[current->next]);
                p = c; 
                c = n; 
                n = current->next;
            }
        }
    }
}

/** Update the provided mtbl's LRU doubly-linked list by placing the memory 
 * entry, identified by the provided index, at the head of the list (lru_first).
 *
 */
static void update_lru(struct mem_table_s *mtbl, uint16_t index)
{
            if(DBG)fprintf(out, "updating lru...\n");
            /* Do not place an entry with index < MEM_TABLE_HASH_MAX because 
             * they must remain the heads of the hash table chains
             */
            if((int16_t)index < MEM_TABLE_HASH_MAX)
            {
                if(DBG)fprintf(out, 
                        "\t%d<MEM_TABLE_HASH_MAX, not adding to LRU\n", index);
                return;
            }
  
            /* First memory entry used becomes the head and tail of the list */
            if(((int16_t)mtbl->lru_first == NIL) && 
                    ((int16_t)mtbl->lru_last == NIL))
            {
                if(DBG)fprintf(out, "\tsetting lru first and last\n");
                mtbl->lru_first = index;
                mtbl->lru_last = index;
                mtbl->mem[index].lru_prev = NIL;
                mtbl->mem[index].lru_next = NIL;
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
                {   /* Must be 2nd unique memory entry */
                    if(DBG)fprintf(out, "\tinserting second record in LRU\n");
                    /* point tail.prev to new */
                    mtbl->mem[mtbl->lru_last].lru_prev = index; 
                    /* point new.prev to NIL */  
                    mtbl->mem[index].lru_prev = NIL;
                    /* point the new.next to the tail */      
                    mtbl->mem[index].lru_next = mtbl->lru_last;
                    /* point the head to the new */  
                    mtbl->lru_first = index;                    
                }
            }
            /* 3rd+ Memory Entry */
            else
            {
                if(DBG)fprintf(out, "\trepairing previous LRU links and "
                                            "inserting record in LRU\n");
                /* repair links */  
                if((int16_t)mtbl->mem[index].lru_prev != NIL)
                {
                    mtbl->mem[mtbl->mem[index].lru_prev].lru_next = 
                        mtbl->mem[index].lru_next;
                }
                if((int16_t)mtbl->mem[index].lru_next != NIL)
                {
                    mtbl->mem[mtbl->mem[index].lru_next].lru_prev =     
                        mtbl->mem[index].lru_prev;
                }
                /* update nodes own link */
                mtbl->mem[mtbl->lru_first].lru_prev = index; 
                mtbl->mem[index].lru_next = mtbl->lru_first;
                mtbl->mem[index].lru_prev = NIL;
                /* Finally, establish this entry as the first on LRU list */
                mtbl->lru_first = index;
            }
}

/** Searches the ftbl for the mtbl with the most entries.
 * Returns the number of memory entries the max mtbl has. The double ptr 
 * parameter is used to store a reference to the mtbl pointer with the most 
 * memory entries. 
 */
static int locate_max_mtbl(struct mem_table_s **mtbl)
{
    if(DBG)fprintf(out, "locating mtbl with most entries...\n");
    struct file_table_s *ftbl = &(ucache->ftbl);
    uint32_t index_of_max_blk = NIL;
    uint16_t index_of_max_ent = NIL;
    uint16_t value_of_max = 0;
    /* Iterate over file hash table indices */
    int i;
    for(i = 0; i < FILE_TABLE_HASH_MAX; i++)
    {
        if(DBG)fprintf(out, "\texamining ftbl index %d\n", i);
        /* Iterate over hash table chain */
        int j;
        for(j = i; !file_done(j); j = file_next(ftbl, j))
        {
            struct file_ent_s *fent = &(ftbl->file[j]);
            if(DBG)fprintf(out, "\t(blk, ent)=(%d, %d)\n", fent->mtbl_blk,
                                                    (int16_t)fent->mtbl_ent);
            //TODO: this might can be removed: test it
            if(((int32_t)fent->mtbl_blk == NIL) || 
                    ((int16_t)fent->mtbl_ent==NIL))
            {
                break;
            }
            /* Examine the mtbl's value of num_blocks to see if it's the 
             * greatest. 
             */
            struct mem_table_s *current_mtbl = &(ucache->b[fent->mtbl_blk].
                                                        mtbl[fent->mtbl_ent]);
            if(current_mtbl->num_blocks > value_of_max)
            {
                if(DBG)fprintf(out, "\tmax updated @ %d\n", j);
                *mtbl = current_mtbl; /* Set the parameter to this mtbl */
                index_of_max_blk = fent->mtbl_blk;
                index_of_max_ent = fent->mtbl_ent;
                value_of_max = (*mtbl)->num_blocks;
            }
        }
    }
    return value_of_max;
}

/** Evicts the LRU memory entry from the tail (lru_last) of the provided 
 * mtbl's LRU list.
 * Returns 1 on success; 0 on failure, meaning there was no lru
 */
static int evict_LRU(struct mem_table_s *mtbl)
{
    if(DBG)fprintf(out, "evicting LRU...\n");
    if(mtbl->num_blocks != 0)
    {
        remove_mem(mtbl, mtbl->mem[mtbl->lru_last].tag);
        return 1;
    }
    else
    {   /*  Worst Case  */
        if(DBG)fprintf(out, "\tno LRU on this mtbl\n");
        return 0;
    }
}


/** Used to obtain a block for storage of data identified by the offset 
 * parameter and maintained in the mtbl at the memory entry identified by the 
 * index parameter.
 *
 * If a free block could be aquired, returns the memory address of the block 
 * just inserted. Otherwise, returns NIL.
 */
static void *set_item(struct mem_table_s *mtbl, 
                    uint64_t offset, 
                    uint16_t index)
{   
        int16_t free_blk = get_free_blk();
        /* No Free Blocks Available */
        if(free_blk == NIL)
        {
            if(DBG)fprintf(out, "\tget_free_blk returned NIL, attempting "
                                                      "removal of LRU\n");
            //TODO this probably wont work since we cannot evict a block that is current referenced
            evict_LRU(mtbl); 
            free_blk = get_free_blk();
        }
        
        /* After Eviction Routine - No Free Blocks Available, Evict from mtbl 
         * with the most memory entries (and therefore blocks) 
         */
        if(free_blk == NIL)   
        {
            struct mem_table_s *max_mtbl;
            locate_max_mtbl(&max_mtbl);
            if(DBG)fprintf(out, "\tget_free_blk returned NIL, evicting a block "
                  "from max mtbl which has %d entries\n", max_mtbl->num_blocks);
            evict_LRU(max_mtbl);
            free_blk = get_free_blk();
        }
        /* A Free Block is Avaiable for Use */
        if(free_blk != NIL)
        {
            mtbl->num_blocks++;
            update_lru(mtbl, index);
            if(DBG)fprintf(out, "\tsuccessfully inserted memory entry @ blk: "
                                                  "%d\n", (int16_t) free_blk);
            /* set item to block number */
            mtbl->mem[index].tag = offset;
            mtbl->mem[index].item = free_blk;
            /* add block index to head of dirty list */
            if(DBG)fprintf(out, "\tadding memory entry to dirty list\n");
            mtbl->mem[index].dirty_next = mtbl->dirty_list;
            mtbl->dirty_list = index;
            /* Return the address of the block where data is stored */
            return (void *)&(ucache->b[free_blk]); 
        }
    return (void *)(NIL);
}

/** Requests a location in memory to place the data identified by the mtbl and 
 * offset parameters. Also inserts the necessary info into the mtbl.
 *
 */
static void *insert_mem(struct mem_table_s *mtbl, uint64_t offset, 
                                              uint32_t *block_ndx)
{
    if(DBG)fprintf(out, "trying to insert mem...\n");
    void* rc = 0;

    /* Index into mem hash table */
    unsigned int index = offset % MEM_TABLE_HASH_MAX;
    if(DBG)fprintf(out, "\toffset: 0X%llX hashes to index: %u\n",
                                                  offset, index);

    struct mem_ent_s *current = &(mtbl->mem[index]);
    /* Lookup first */
    void *returnValue = lookup_mem(mtbl, offset, block_ndx, NULL, NULL);
    if((int)returnValue != NIL)
    {
        if(DBG)fprintf(out, "\tblock for this offset already exists @ 0X%X",
                                                 (unsigned int)returnValue);
        /* Already exists in mtbl so just return a ptr to the blk */
        return returnValue;
    }

    /* Entry doesn't exist, insertion required */
    if(DBG)fprintf(out, "\tlookup returned NIL\n");
    int mentIndex = 0;
    if((int64_t)mtbl->mem[index].tag != NIL)
    {
        /* If head occupied, need to get free ment */
        mentIndex = get_free_ment(mtbl);
        if(mentIndex == NIL)
        {   /* No free ment available, so attempt eviction, and try again */
            if(DBG)fprintf(out, "\tno ment\n");
            evict_LRU(mtbl);
            mentIndex = get_free_ment(mtbl);
        }
        /* Procede with memory insertion if eviction was successful */
        if(mentIndex != NIL)
        {   
            if(DBG)fprintf(out, "\tfound free memory entry @ index = %d\n",
                                                                mentIndex);
            /* Insert directly after head of chain */
            uint16_t next_ment = current->next;
            current->next = mentIndex;
            mtbl->mem[mentIndex].next = next_ment;
            rc = set_item(mtbl, offset, mentIndex);
            if((int32_t)rc != NIL)
            {
                *block_ndx = mtbl->mem[mentIndex].item;
                return rc;      
            }
            else
            {
                return (void *)NIL;   
            } 
        }
        /* Eviction Failed */
        else
        {
            return (void *)NULL;
        }
    }
    /* Head vacant. No need to iterate to next in chain, just use head */
    rc = set_item(mtbl, offset, index);
    if((int32_t)rc != NIL)
    {
        *block_ndx = mtbl->mem[mentIndex].item;
        return rc;      
    }
    else
    {
        return (void *)NIL;   
    } 
}

/** Removes all table info regarding the block identified by the mtbl and
 * offset, provided the mtbl has a reference count of 0 and the entry exists. 
 *
 * On success returns 1, on failure returns 0.
 *
 */
static int remove_mem(struct mem_table_s *mtbl, uint64_t offset)
{
    if(DBG)fprintf(out, "trying to remove memory entry...\n");
    /* Some Indices */
    uint32_t item_index = NIL;
    uint16_t mem_ent_index = NIL;
    uint16_t mem_ent_prev_index = NIL;


    /* Previous, Current, Next */
    uint16_t p = NIL;
    uint16_t c = NIL;
    uint16_t n = NIL;   

    /* Check reference count */
    if(mtbl->ref_cnt != 0)
    {
        if(DBG)fprintf(out, "removal failure: ref_cnt==%d\n", mtbl->ref_cnt);
        return 0;
    }
    void *retValue = lookup_mem(mtbl, offset, &item_index, &mem_ent_index, 
                                                     &mem_ent_prev_index);
    /* Verify we've recieved the necessary info */
    if((int)retValue == NIL)
    {
        if(DBG)fprintf(out, "\tremoval failure: memory entry not found "
                                                      "matching offset");
        return 0;
    }
    /* Remove from LRU */
    /* Update each of the adjacent nodes' link */
    if((int16_t)mtbl->mem[mem_ent_index].lru_prev != NIL)
    {
        mtbl->mem[mtbl->mem[mem_ent_index].lru_prev].lru_next = 
                             mtbl->mem[mem_ent_index].lru_next;
    }
    if((int16_t)mtbl->mem[mem_ent_index].lru_next != NIL)
    {
        mtbl->mem[mtbl->mem[mem_ent_index].lru_next].lru_prev = 
                             mtbl->mem[mem_ent_index].lru_prev;
    }
    if(mem_ent_index == mtbl->lru_first)
    {   
        /* Is node the head? */
        mtbl->lru_first = mtbl->mem[mem_ent_index].lru_next;    
    }
    if(mem_ent_index == mtbl->lru_last)
    {   
        /* Is node the tail? */
        mtbl->lru_last = mtbl->mem[mem_ent_index].lru_prev;
    }
    /* Remove from dirty list if Dirty */
    if(DBG)fprintf(out, "\tremoving from dirty list\n");

    c = mtbl->dirty_list;
    n = mtbl->mem[c].dirty_next;

    while(c != mem_ent_index)
    {
        p = c;
        c = n;
        n = mtbl->mem[c].dirty_next;
        if((int16_t)c == NIL)
        {
                break;
        }
    }
    if((int16_t)c != NIL)
    {   
        /* If memory entry was located on the dirty_list */
        mtbl->mem[p].dirty_next = mtbl->mem[c].dirty_next;
        mtbl->mem[c].dirty_next = NIL;
    }
    /* Add memory block back to free list */
    put_free_blk(item_index);
    /* Repair link */
    if((int16_t)mem_ent_prev_index != NIL)
    {
        mtbl->mem[mem_ent_prev_index].next = mtbl->mem[mem_ent_index].next;
    }
    /* Newly free mem entry becomes new head of free mem entry list if index 
     * is less than hash table max 
     */
    put_free_ment(mtbl, mem_ent_index);
    mtbl->num_blocks--;
    if(DBG)fprintf(out, "\tmemory entry removal sucessful\n");
    return 1;
}

/*  The following two functions are provided for error checking purposes.
static void print_lru(struct mem_table_s *mtbl)
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

static void print_dirty(struct mem_table_s *mtbl)
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
 */

/*  End of Internal Only Functions    */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
