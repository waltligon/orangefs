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

#include <ucache.h>

/* Global Variables */
static FILE *out;                   /* For Logging Purposes */

union user_cache_u *ucache = 0;
//static uint32_t ucache_blk_cnt = 0;

ucache_lock_t *ucache_locks = 0; /* will refer to the shmem of all ucache locks */
ucache_lock_t *ucache_lock = 0;  /* Global Lock maintaining concurrency */
ucache_lock_t *ucache_block_lock = 0;

uint64_t ucache_hits;
uint64_t ucache_misses;
uint64_t ucache_pseudo_misses; /* Chose not to cache */

/* Internal Only Function Declarations */

/* Initialization */
static void add_mtbls(uint16_t blk);
static void init_memory_table(uint16_t blk, uint16_t ent);

/* Gets */
static uint16_t get_next_free_mtbl(uint16_t *free_mtbl_blk, uint16_t *free_mtbl_ent);
static uint16_t get_free_fent(void);
static uint16_t get_free_ment(struct mem_table_s *mtbl);
static uint16_t get_free_blk(void);

/* Puts */
static void put_free_mtbl(struct mem_table_s *mtbl, struct file_ent_s *file);
static void put_free_fent(uint16_t fent);
static void put_free_ment(struct mem_table_s *mtbl, uint16_t ent);
static void put_free_blk(uint16_t blk);

/* File Entry Chain Iterator */
static uint16_t file_done(uint16_t index);
static uint16_t file_next(struct file_table_s *ftbl, uint16_t index);

/* Memory Entry Chain Iterator */
static uint16_t ment_done(uint16_t index);
static uint16_t ment_next(struct mem_table_s *mtbl, uint16_t index);

/* Dirty List Iterator */
static uint16_t dirty_done(uint16_t index);
static uint16_t dirty_next(struct mem_table_s *mtbl, uint16_t index);

/* File and Memory Insertion */
static struct mem_table_s *insert_file(uint32_t fs_id, uint64_t handle);
static void *insert_mem(struct mem_table_s *mtbl, uint64_t offset, 
                                             uint16_t *block_ndx);
static void *set_item(struct mem_table_s *mtbl,uint64_t offset, uint16_t index);

/* File and Memory Lookup */
static struct mem_table_s *lookup_file(
    uint32_t fs_id, 
    uint64_t handle,
    uint16_t *file_mtbl_blk,    /* Can be NULL if not desired */
    uint16_t *file_mtbl_ent,  
    uint16_t *file_ent_index,
    uint16_t *file_ent_prev_index
);
static void *lookup_mem(struct mem_table_s *mtbl, 
                    uint64_t offset, 
                    uint16_t *item_index,
                    uint16_t *mem_ent_index,
                    uint16_t *mem_ent_prev_index
);

/* File and Memory Entry Removal */
/* static uint32_t remove_file(uint32_t fs_id, uint64_t handle); */
static void remove_all_memory_entries(struct mem_table_s *mtbl);
static int remove_mem(struct mem_table_s *mtbl, uint64_t offset);

/* Eviction Utilities */
static int evict_file(uint16_t index);
static uint16_t locate_max_mtbl(struct mem_table_s **mtbl);
static void update_LRU(struct mem_table_s *mtbl, uint16_t index);
static int evict_LRU(struct mem_table_s *mtbl);

/* List Printing Functions */ 
void print_LRU(struct mem_table_s *mtbl);
/* static void print_dirty(struct mem_table_s *mtbl);
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
 */
int ucache_initialize(void)
{
    int rc = 0;
    int i = 0;
    int id1;

    /* Aquire ptr to shared memory for ucache_locks */
    id1 = shmget(ftok(GET_KEY_FILE, 'a'), 0, CACHE_FLAGS);
    if (id1 < 0)
    {
        return -1;
    }
    ucache_locks = shmat(id1, NULL, AT_FLAGS);
    if (!ucache_locks)
    {
        return -1;
    }
    /* Global Cache lock stored in first LOCK_SIZE position */
    ucache_lock = ucache_locks; 

    /* Initialize Global Cache Lock */
    rc = lock_init(ucache_lock);
    if (rc < 0)
    {
        return -1;
    }
    rc = lock_lock(ucache_lock);
    if (rc < 0)
    {
        return -1;
    }

    /* The next BLOCKS_IN_CACHE number of block locks follow the global lock */
    ucache_block_lock = ucache_locks + 1;
    /* Initialize Block Level Locks */
    for(i = 0; i < BLOCKS_IN_CACHE; i++)
    {
        lock_init(get_block_lock(i));
        if (rc < 0)
        {
            rc = -1;
            goto errout;
        }
    }

    /* Aquire ptr to shared memory for ucache */
    uint32_t key;
    uint32_t id;
    char *key_file_path;
    /*  Direct output   */
    if (!out)
    {
        out = stdout;
    }

    /* set up shared memory region */
    key_file_path = GET_KEY_FILE;
    key = ftok(key_file_path, PROJ_ID);
    id = shmget(key, 0, CACHE_FLAGS);
    if (id < 0)
    {
        rc = -1;
        goto errout;
    }
    ucache = shmat(id, NULL, AT_FLAGS);
    if (!ucache)
    {
        rc = -1;
        goto errout;
    }
    memset(ucache, 0, CACHE_SIZE);
    if (DBG)
    {
        fprintf(out, "key:\t\t\t0X%X\n", key);
        fprintf(out, "id:\t\t\t%d\n", id);
        fprintf(out, "ucache ptr:\t\t0X%lX\n", (long int)ucache);
    }
    /* initialize mtbl free list table */
    ucache->ftbl.free_mtbl_blk = NIL;
    ucache->ftbl.free_mtbl_ent = NIL;
    add_mtbls(0);
    /* set up list of free blocks */
    ucache->ftbl.free_blk = 1;
    for (i = 1; i < (BLOCKS_IN_CACHE - 1); i++)
    {
        ucache->b[i].mtbl[0].free_list_blk = i + 1;
    }
    ucache->b[BLOCKS_IN_CACHE - 1].mtbl[0].free_list_blk = NIL;
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
    rc = 0;

errout:
    lock_unlock(ucache_lock);
    return rc;
}

int ucache_open_file(PVFS_fs_id *fs_id,
                     PVFS_handle *handle, 
                     struct mem_table_s **mtbl)
{
    lock_lock(ucache_lock);
    *mtbl = lookup_file((uint32_t)(*fs_id), (uint64_t)(*handle), NULL, NULL, NULL,
                                                                          NULL);
    if(*mtbl == (struct mem_table_s *)NIL)
    {
        *mtbl = insert_file((uint32_t)*fs_id, (uint64_t)*handle);
        if(*mtbl == (struct mem_table_s *)NIL)
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
                                                      uint16_t *block_ndx)
{
    lock_lock(ucache_lock);
    struct mem_table_s *mtbl = lookup_file(
        (uint32_t)(*fs_id), (uint64_t)(*handle), NULL, NULL, NULL, NULL);
    if(mtbl != (struct mem_table_s *)NIL)
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
                                                        uint16_t *block_ndx)
{
    lock_lock(ucache_lock);
    struct mem_table_s *mtbl = lookup_file(
        (uint32_t)(*fs_id), (uint64_t)(*handle), NULL, NULL, NULL, NULL);
    if(mtbl == (struct mem_table_s *)NIL)
    {
        lock_unlock(ucache_lock);
        return (void *)NIL;
    }
    else
    {
        if(remove_mem(mtbl, offset) == NIL)
        {
            lock_unlock(ucache_lock);
            return (void *)NIL;
        }
        //print_LRU(mtbl);
        char * retVal = insert_mem(mtbl, offset, block_ndx);
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
    if(mtbl != (struct mem_table_s *)NIL)
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
    PVFS_fs_id *fs_id = &(pd->s->pvfs_ref.fs_id);
    PVFS_handle *handle = &(pd->s->pvfs_ref.handle);
    lock_lock(ucache_lock);
    struct mem_table_s *mtbl = lookup_file((uint32_t)(*fs_id), 
                 (uint64_t)(*handle), NULL, NULL, NULL, NULL);
    if(mtbl == (struct mem_table_s *)NIL)
    {
        return NIL;
    }
    uint16_t i;
    for(i = mtbl->dirty_list; !dirty_done(i); i = dirty_next(mtbl, i))
    {
        struct mem_ent_s *ment = &(mtbl->mem[i]);
        mtbl->mem[i].dirty_next = NIL;
        if(ment->tag == (uint64_t)NIL || ment->item == (uint16_t)NIL)
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
        iocommon_readorwrite_nocache(2, pd, ment->tag, 
                     &(ucache->b[ment->item].mblk[0]),
                                          mreq, freq);
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
    uint16_t file_mtbl_blk;
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
    if(mtbl == (struct mem_table_s *)NIL)
    {
        lock_unlock(ucache_lock);
        return NIL;
    }
    remove_all_memory_entries(mtbl);
    struct file_ent_s *file = &(ucache->ftbl.file[file_ent_index]);
    put_free_mtbl(mtbl, file); //something broken on this line
    put_free_fent(file_ent_index);
    lock_unlock(ucache_lock);
    return 1;
}

/** Decrements the reference count of a particular mtbl.
int ucache_dec_ref_cnt(struct mem_table_s * mtbl)
{
    int rc;
    lock_lock(ucache_lock);
    //decrement ref_cnt of mtbl
    if(mtbl->ref_cnt != 0)
    {
        mtbl->ref_cnt--;
    }
    rc = mtbl->ref_cnt;
    lock_unlock(ucache_lock); 
    return rc;
}
*/

/** Increments the reference count of a particular mtbl.
int ucache_inc_ref_cnt(struct mem_table_s * mtbl)
{
    int rc = 0;
    lock_lock(ucache_lock);
    //increment ref_cnt of mtbl
    mtbl->ref_cnt++;
    rc = mtbl->ref_cnt;
    lock_unlock(ucache_lock); 
    return rc;
}
*/

/** Dumps all cache related information. */
void ucache_info(FILE *out, char *flags)
{
    /* Decide what to show */
    char show_all=0;
    char show_sum=0;
    char show_verbose=0;
    int char_ndx;
    for (char_ndx=0; char_ndx<strlen(flags); char_ndx++)
    {
        char c = flags[char_ndx];
        switch(c)
        {
            case 'a':
                show_all=1;
                break;
            case 's':
                show_sum=1;             
                break;
            case 'v':
                show_verbose=1;
        }
    }

    int attempts = ucache_hits + ucache_misses;
    float percentage = 0;
    if(attempts)
    {
        percentage = (float)(ucache_hits / attempts);
    }

    if(show_sum)
    {
        fprintf(out, 
            "user cache statistics:\n"
            "\tucache_hits=\t%llu\n"
            "\tucache_misses=\t%llu\n"
            "\thit percentage=\t%f\n"
            "\tucache_pseudo_misses=\t%llu\n",
            ucache_hits, 
            ucache_misses, 
            percentage, 
            ucache_pseudo_misses
        );
    }

    if(show_all && show_verbose)
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
    fprintf(out, "ucache_lock ptr:\t\t0X%lX\n", (long int)ucache_lock);

    /* ucache Shared Memory Info */
    fprintf(out, "ucache ptr:\t\t0X%lX\n", (long int)ucache);

    /* FTBL Info */
    struct file_table_s *ftbl = &(ucache->ftbl);
    fprintf(out, "ftbl ptr:\t\t0X%lX\n", (long int)&(ucache->ftbl));
    fprintf(out, "free_blk = %hu\n", ftbl->free_blk);
    fprintf(out, "free_mtbl_blk = %hu\n", ftbl->free_mtbl_blk);
    fprintf(out, "free_mtbl_ent = %hu\n", ftbl->free_mtbl_ent);
    fprintf(out, "free_list = %hu\n", ftbl->free_list);

    /* Other Free Blocks */
    fprintf(out, "\nIterating Over Free Blocks:\n\n");
    uint16_t i;
    for(i = (uint16_t)ftbl->free_blk; i != (uint16_t)NIL; i = ucache->b[i].mtbl[0].
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
    while(current_blk != (uint16_t)NIL)
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
    for(current_fent = ftbl->free_list; (int16_t)current_fent != NIL; 
                        current_fent = ftbl->file[current_fent].next)
    {
        //fprintf(out, "free file entry: index = %d\n", (int16_t)current_fent);
    }
    fprintf(out, "End of Free File Entry List\n\n");

    fprintf(out, "Iterating Over File Entries in Hash Table:\n\n");
    /* iterate over file table entries */
    for(i = 0; i < FILE_TABLE_HASH_MAX; i++)
    {
        if((ftbl->file[i].tag_handle != (uint64_t)NIL) && 
               (ftbl->file[i].tag_handle != (uint64_t)0))
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

                struct mem_table_s * mtbl = &(ucache->b[fent->mtbl_blk].
                                                    mtbl[fent->mtbl_ent]);

                fprintf(out, "\tMTBL LRU List ****************\n");
                //print_LRU(mtbl);

                fprintf(out, "\tMTBL INFO ********************\n");
                fprintf(out, "\tnum_blocks = %hu\n", mtbl->num_blocks);
                fprintf(out, "\tfree_list = %hu\n", mtbl->free_list); 
                fprintf(out, "\tfree_list_blk = %hu\n", mtbl->free_list_blk);
                fprintf(out, "\tlru_first = %hu\n", mtbl->lru_first);
                fprintf(out, "\tlru_last = %hu\n", mtbl->lru_last);
                fprintf(out, "\tdirty_list = %hu\n", mtbl->dirty_list);
                fprintf(out, "\tref_cnt = %hu\n\n", mtbl->ref_cnt);

                /* Iterate Over Memory Entries */
                uint16_t k;
                for(k = 0; k < MEM_TABLE_HASH_MAX; k++)
                {
                    if((int64_t)mtbl->mem[k].tag != NIL)
                    {
                        uint16_t l;
                        for(l = k; !ment_done(l); l = ment_next(mtbl, l)){
                            struct mem_ent_s * ment = &(mtbl->mem[l]);
                            fprintf(out, "\t\tMEMORY ENTRY INDEX %hd **********"
                                                              "*********\n", l);
                            fprintf(out, "\t\ttag = 0X%llX\n", 
                                         (uint64_t)ment->tag);
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
                        if(mtbl->num_blocks != 0)
                        {
                            fprintf(out, "\tvacant memory entry @ index = %d\n",
                                                                             k);
                        }
                    }
                }
            }
            fprintf(out, "End of chain @ Hash Table Index %hu\n\n", i);
        }
        else
        {
            fprintf(out, "vacant file entry @ index = %hu\n", i);
        }
    }
    }
}

/** Returns a pointer to the block level lock corresponding to the block_index.
    If the index is out of range, then 0 is returned.
 */
ucache_lock_t *get_block_lock(uint16_t block_index)
{
    /* TODO: check if this returns the appropriate block */
    if(block_index >= BLOCKS_IN_CACHE)
    {
        return (ucache_lock_t *)0;
    }
    return (ucache_block_lock + block_index);
}


/* Beginning of internal only (static) functions */

/** Initializes the proper lock based on the LOCK_TYPE */
int lock_init(ucache_lock_t * lock)
{
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

/** Upon successful completion, returns zero 
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
/***************************************** End of Externally Visible API */

/* Dirty List Iterator */
/** Returns true if current index is NIL, otherwise, returns 0 */
static uint16_t dirty_done(uint16_t index)
{
    return (index == NIL);
}

/** Returns the next index in the dirty list for the provided mtbl and index */
static uint16_t dirty_next(struct mem_table_s *mtbl, uint16_t index)
{
    return mtbl->mem[index].dirty_next;
}

/*  Memory Entry Chain Iterator */
/** Returns true if current index is NIL, otherwise, returns 0 */
static uint16_t ment_done(uint16_t index)
{
    return ((int16_t)index == NIL);
}

/** Returns the next index in the memory entry chain for the provided mtbl 
 * and index. 
 */
static uint16_t ment_next(struct mem_table_s *mtbl, uint16_t index)
{
    return mtbl->mem[index].next;
}

/*  File Entry Chain Iterator   */
/** Returns true if current index is NIL, otherwise, returns 0 */
static uint16_t file_done(uint16_t index)
{
    return ((int16_t)index == NIL);
}

/** Returns the next index in the file entry chain for the provided mtbl 
 * and index. 
 */
static uint16_t file_next(struct file_table_s *ftbl, uint16_t index)
{
    return ftbl->file[index].next;
}

/**This function should only be called when the ftbl has no free mtbls. 
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
    b->mtbl[i].free_list_blk = NIL;
    b->mtbl[i].free_list = NIL;
    ftbl->free_mtbl_blk = blk;
    ftbl->free_mtbl_ent = start_mtbl;   
}

/** Initializes a mtbl which is a hash table of memory entries.
 * The mtbl will be located at the provided entry index within 
 * the provided block.
 */
static void init_memory_table(uint16_t blk, uint16_t ent)
{
    uint16_t i;
    //assert((int16_t)blk != NIL);
    struct mem_table_s *mtbl = &(ucache->b[blk].mtbl[ent]);
    mtbl->lru_first = NIL;
    mtbl->lru_last = NIL;
    mtbl->dirty_list = NIL;
    mtbl->num_blocks = 0;
    mtbl->ref_cnt = 0;
    mtbl->free_list_blk = NIL;
    /* set up hash table */
    for (i = 0; i < MEM_TABLE_HASH_MAX; i++)
    {
        mtbl->mem[i].item = NIL;
        mtbl->mem[i].tag = NIL;
        mtbl->mem[i].next = NIL;
        mtbl->mem[i].lru_prev = NIL;
        mtbl->mem[i].lru_next = NIL;
        mtbl->mem[i].dirty_next = NIL;
    }
    /* set up list of free hash table entries */
    mtbl->free_list = MEM_TABLE_HASH_MAX;
    for (i = MEM_TABLE_HASH_MAX; i < (MEM_TABLE_ENTRY_COUNT - 1); i++)
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
    uint16_t desired_blk = ftbl->free_blk;
    if((int16_t)desired_blk != NIL)
    {  
        /* Update the head of the free block list */ 
        /* Use mtbl index zero since free_blks have no ititialized mem tables */
        ftbl->free_blk = ucache->b[desired_blk].mtbl[0].free_list_blk; 
        return desired_blk;
    }
    return NIL;

}

/** Accepts an index corresponding to a block that is put back on the file 
 * table free list.
 */
static void put_free_blk(uint16_t blk)
{
    if(DBG)fprintf(out, "freeing blk @ index = %hu\n", blk);
    struct file_table_s *ftbl = &(ucache->ftbl);
    /* set the block's next value to the current head of the block free list */
    ucache->b[blk].mtbl[0].free_list_blk = ftbl->free_blk;
    /* blk is now the head of the ftbl blk free list */
    ftbl->free_blk = blk;
}

/** Consults the file table to retrieve an index corresponding to a file entry
 * If available, returns the file entry index, otherwise returns NIL.
 */
static uint16_t get_free_fent(void)
{
    if(DBG)fprintf(out, "trying to get free file entry...\n");
    struct file_table_s *ftbl = &(ucache->ftbl);
    uint16_t entry = ftbl->free_list;
    if((int16_t)entry != NIL)
    {
        ftbl->free_list = ftbl->file[entry].next;
        ftbl->file[entry].next = NIL;
        if(DBG)fprintf(out, "\tfree file entry index = %hu\n", entry);
        return entry;
    }
    else
    {
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
static void put_free_fent(uint16_t fent)
{
    if(DBG)fprintf(out, "freeing file entry = %hu\n", fent);
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
static uint16_t get_free_ment(struct mem_table_s *mtbl)
{
    uint16_t ment = mtbl->free_list;
    if((int16_t)ment != NIL)
    {
        mtbl->free_list = mtbl->mem[ment].next;
        mtbl->mem[ment].next = NIL;
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
static void put_free_ment(struct mem_table_s *mtbl, uint16_t ent)
{
    if(DBG)fprintf(out, "freeing memory entry = %hu\n", ent);
    /* Reset ment values */
    mtbl->mem[ent].tag = NIL;
    mtbl->mem[ent].item = NIL;
    mtbl->mem[ent].dirty_next = NIL;
    mtbl->mem[ent].lru_next = NIL;
    mtbl->mem[ent].lru_prev = NIL;
    if(ent >= MEM_TABLE_HASH_MAX)
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
    uint16_t *file_mtbl_blk,
    uint16_t *file_mtbl_ent,
    uint16_t *file_ent_index,
    uint16_t *file_ent_prev_index
)
{
    if(DBG)fprintf(out, "performing lookup...\n");

    /* Index into file hash table */
    uint16_t index = handle % FILE_TABLE_HASH_MAX;
    if(DBG)fprintf(out, "\thashed index: %d\n", index);

    struct file_table_s *ftbl = &(ucache->ftbl);
    struct file_ent_s *current = &(ftbl->file[index]);

    /* previous, current, next fent index */
    uint16_t p = NIL;
    uint16_t c = index;
    uint16_t n = current->next;

    while(1)
    {
        if(DBG)fprintf(out, "\tp=%hu\tc=%hu\tn=%hu\n", p, c, n);
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
                p=c; 
                c=n; 
                n=current->next;
                if(DBG)
                {
                    fprintf(out, "\tIterating across the chain, next=%d\n",
                                                   (int16_t)current->next);    
                }
            }

        }
    }    
}

/** Function that locates the next free mtbl.
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
        if(((int16_t)*free_mtbl_blk == NIL) || 
             ((int16_t)*free_mtbl_ent == NIL))
        { 
            return NIL;
        }

        /* Update ftbl to contain new next free mtbl */
        ftbl->free_mtbl_blk = ucache->b[*free_mtbl_blk].mtbl[*free_mtbl_ent].
                                                                free_list_blk;
        ftbl->free_mtbl_ent = ucache->b[*free_mtbl_blk].mtbl[*free_mtbl_ent].
                                                                    free_list;

        /* Set free info to NIL */
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

    uint16_t i;
    for(i = 0; i < MEM_TABLE_HASH_MAX; i++)
    {
        if(DBG)fprintf(out, "\tremoving memory entry %hu\n", i);
        uint16_t j;
        for(j = i; !ment_done(j); j = ment_next(mtbl, j))
        {
            /* Current Memory Entry */
            struct mem_ent_s *ment = &(mtbl->mem[j]);
            if(DBG)fprintf(out, "\t(tag, item)=(%lld, %hu)\n", 
                    (long long int)ment->tag, ment->item);
            /*  Account for empty head of ment chain    */
            if(((int64_t)ment->tag == NIL) || ((int16_t)ment->item == NIL))
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
    uint16_t tmp_blk = ucache->ftbl.free_mtbl_blk;
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
static int evict_file( uint16_t index)
{
    if(DBG)fprintf(out, "EVICTING FILE AND ITS MTBL IF NOT REFERENCED\n");
    assert((int16_t)index != NIL);
    if(DBG)fprintf(out, "evicting data @ index %hu...\n", index);
    struct file_ent_s *file = &(ucache->ftbl.file[index]);
    assert(((int16_t)file->mtbl_blk != NIL) && ((int16_t)file->mtbl_ent != NIL));
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
    if(DBG)fprintf(out, "EVICTION SUCCESSFUL\n");
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
    uint16_t index = handle % FILE_TABLE_HASH_MAX;
    if(DBG)fprintf(out, "\thashed index: %u\n", index);
    current = &(ftbl->file[index]);
    /* Get free mtbl */
    uint16_t free_mtbl_blk = NIL;
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
            if(DBG)fprintf(out, "\tevicting blk from max mtbl: has %hu" 
                                   " entries\n", max_mtbl->num_blocks);
            if(DBG)print_LRU(max_mtbl);
            evict_LRU(max_mtbl);
        }
        /* Intitialize remaining blocks' memory tables */
        if((int16_t)ucache->ftbl.free_blk != NIL)
        {
            if(DBG)fprintf(out, "\tadding memory tables to free block\n");
            int16_t free_blk = get_free_blk(); 
            add_mtbls(free_blk);
            get_next_free_mtbl(&free_mtbl_blk, &free_mtbl_ent);
        }
    }

    /* Insert at index, relocating data @ index if occupied */
    if(((int16_t)current->mtbl_blk != NIL) && 
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
            uint16_t i;
            for(i = index; !file_done(i) && (!file_evicted); i = 
                                            file_next(ftbl,index))
            {
                i = ftbl->file[i].next;
                current = &(ftbl->file[i]);
                file_evicted = evict_file(i);
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
    //assert(((int16_t)free_mtbl_blk != NIL) && ((int16_t)free_mtbl_ent != NIL));
    current->mtbl_blk = free_mtbl_blk;
    current->mtbl_ent = free_mtbl_ent;
    /* Initialize Memory Table */
    init_memory_table(free_mtbl_blk, free_mtbl_ent);
    if(DBG)fprintf(out, "\trecieved free memory table: 0X%lX\n", 
        (long int)&(ucache->b[current->mtbl_blk].mtbl[current->mtbl_ent]));
    return (struct mem_table_s*)&(ucache->b[current->mtbl_blk].mtbl[current->mtbl_ent]);
}

/** Remove file entry and memory table of file identified by parameters
 * Returns 1 following removal
 * Returns NIL if file is referenced or if the file could not be located.
 */
/*
static int remove_file(uint32_t fs_id, uint64_t handle)
{
    if(DBG)fprintf(out, "trying to remove file...\n");
    uint16_t file_mtbl_blk = NIL;
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

    // Verify we've recieved the necessary info
    if((file_mtbl_blk == (uint16_t)NIL) || (file_mtbl_ent == (uint16_t)NIL))
    {
        if(DBG)fprintf(out, "\tremoval failure: no mtbl matching file\n");
        return NIL;
    }

    // Free the mtbl
    put_free_mtbl(mtbl, &(ftbl->file[file_ent_index]));

    // Free the file entry
    ftbl->file[file_ent_prev_index].next = ftbl->file[file_ent_index].next;
    put_free_fent(file_ent_index);

    if(DBG)fprintf(out, "\tremoval sucessful\n");
    return(1);
}
*/

/** Lookup the memory location of a block of data in cache that is identified 
 * by the mtbl and offset parameters.
 *
 * If located, returns a pointer to memory where the desired block of data is 
 * stored. Otherwise, NIL is returned.
 *
 * pertaining to the memory entry's location. If NULLs are passed in place of 
 * these parameters, then they will not be set.
 */
static void *lookup_mem(struct mem_table_s *mtbl, 
                    uint64_t offset, 
                    uint16_t *item_index,
                    uint16_t *mem_ent_index,
                    uint16_t *mem_ent_prev_index)
{
    if(DBG)fprintf(out, "performing ment lookup...\n");

    /* index into mem hash table */
    uint16_t index = offset % MEM_TABLE_HASH_MAX;
    if(DBG)fprintf(out, "\toffset: 0X%llX hashes to index: %u\n", (long long int)offset, 
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
            if(DBG)fprintf(out, "\tmatch located: 0X%llX == 0X%llX\n", 
                  (long long int)offset, (long long int)current->tag);

            /* If parameters !NULL, set their values */
            if(item_index != NULL)
            {
                *item_index = current->item;
                assert((int16_t)*item_index != NIL);
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
                           (long long int)offset, (long long int)current->tag);
                return (struct mem_table_s *)NIL;
            }
            else
            {
                if(DBG)fprintf(out, "\tno match, iterating: 0X%llX != 0X%llX\n", 
                            (long long int)offset, (long long int)current->tag);
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
static void update_LRU(struct mem_table_s *mtbl, uint16_t index)
{
    if(DBG)fprintf(out, "updating lru with index = %hu\n", index);
    /* Do not place an entry with index < MEM_TABLE_HASH_MAX because 
     * they must remain the heads of the hash table chains
     */
/*    if(index < MEM_TABLE_HASH_MAX)
    {
        if(DBG)fprintf(out, 
            "\t%hu<MEM_TABLE_HASH_MAX, not adding to LRU\n", index);
            return;
    }
*/

    if(DBG)print_LRU(mtbl);  

    /* First memory entry used becomes the head and tail of the list */
    if(((int16_t)mtbl->lru_first == NIL) && 
        ((int16_t)mtbl->lru_last == NIL))
    {
        if(DBG)fprintf(out, "\tsetting lru first and last\n");
        mtbl->lru_first = index;
        assert((int16_t)index != NIL);
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
        {   
            /* Must be 2nd unique memory entry */
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
        
        /* repair links  
        if((int16_t)mtbl->mem[index].lru_prev != NIL)
        {
            mtbl->mem[mtbl->mem[index].lru_prev].lru_next = 
                mtbl->mem[index].lru_next;
        }
        if((int16_t)mtbl->mem[index].lru_next != NIL)
        {
            mtbl->mem[mtbl->mem[index].lru_next].lru_prev =     
                mtbl->mem[index].lru_prev;
        }*/


        /* update nodes own link */
        mtbl->mem[mtbl->lru_first].lru_prev = index; 
        mtbl->mem[index].lru_next = mtbl->lru_first;
        mtbl->lru_first = index;
        mtbl->mem[index].lru_prev = NIL;

        /* Finally, establish this entry as the first on LRU list */
        mtbl->lru_first = index;
    }
    
    assert((int16_t) mtbl->lru_last != NIL);
    if(DBG)print_LRU(mtbl);
}

/** Searches the ftbl for the mtbl with the most entries.
 * Returns the number of memory entries the max mtbl has. The double ptr 
 * parameter is used to store a reference to the mtbl pointer with the most 
 * memory entries. 
 */
static uint16_t locate_max_mtbl(struct mem_table_s **mtbl)
{
    if(DBG)fprintf(out, "locating mtbl with most entries...\n");
    struct file_table_s *ftbl = &(ucache->ftbl);
    uint16_t index_of_max_blk = NIL;
    uint16_t index_of_max_ent = NIL;
    uint16_t value_of_max = 0;
    /* Iterate over file hash table indices */
    uint16_t i;
    for(i = 0; i < FILE_TABLE_HASH_MAX; i++)
    {
//        if(DBG)fprintf(out, "\texamining ftbl index %hu\n", i);
        /* Iterate over hash table chain */
        uint16_t j;
        for(j = i; !file_done(j); j = file_next(ftbl, j))
        {
            struct file_ent_s *fent = &(ftbl->file[j]);
//            if(DBG)fprintf(out, "\t(blk, ent)=(%hu, %hu)\n", fent->mtbl_blk,
//                                                            fent->mtbl_ent);
            //TODO: this might can be removed: test it
            if(((int16_t)fent->mtbl_blk == NIL) || 
                    ((int16_t)fent->mtbl_ent == NIL))
            {
                break;
            }
            /* Examine the mtbl's value of num_blocks to see if it's the 
             * greatest. 
             */
            struct mem_table_s *current_mtbl = &(ucache->b[fent->mtbl_blk].
                                                     mtbl[fent->mtbl_ent]);
            if(current_mtbl->num_blocks >= value_of_max)
            {
//                if(DBG)fprintf(out, "\tmax updated @ %d\n", j);
                *mtbl = current_mtbl; /* Set the parameter to this mtbl */
                index_of_max_blk = fent->mtbl_blk;
                index_of_max_ent = fent->mtbl_ent;
                value_of_max = (*mtbl)->num_blocks;
            }
        }
    }
    if(DBG)fprintf(out, "max:\tblk = %d\tent = %d\tcnt = %d\n", 
             index_of_max_blk, index_of_max_ent, value_of_max);
    return value_of_max;
}

/** Evicts the LRU memory entry from the tail (lru_last) of the provided 
 * mtbl's LRU list.
 * Returns 1 on success; 0 on failure, meaning there was no lru
 */
static int evict_LRU(struct mem_table_s *mtbl)
{
    if(DBG)fprintf(out, "evicting LRU...\n");
    
    if(mtbl->num_blocks != 0 && (int16_t)mtbl->lru_last != NIL)
    {
        //fprintf(out, "\tattempting to evict LRU %hu\n", mtbl->lru_last);
        remove_mem(mtbl, mtbl->mem[mtbl->lru_last].tag);
        return 1;
    }
    else
    {   /*  Worst Case  */
        if(DBG)fprintf(out, "\tno LRU on this mtbl\n");
        if(DBG)fprintf(out, "\tmtbl->num_blocks = %hu\n", mtbl->num_blocks);
        if(DBG)fprintf(out, "\tmtbl->lru_last = %hu\n", mtbl->lru_last);
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
        if(DBG)fprintf(out, "set item index = %hu\n", index);   
        uint16_t free_blk = get_free_blk();
        /* No Free Blocks Available */
        if((int16_t)free_blk == NIL)
        {
            if(DBG)fprintf(out, "\tget_free_blk returned NIL, attempting "
                                                      "removal of LRU\n");
            //TODO this probably wont work since we cannot evict a block that is current referenced
            //aquire lock and check before removing???
            if(DBG)print_LRU(mtbl);
            evict_LRU(mtbl); 
            free_blk = get_free_blk();
        }
        
        /* After Eviction Routine - No Free Blocks Available, Evict from mtbl 
         * with the most memory entries 
         */
        if((int16_t)free_blk == NIL)   
        {
            struct mem_table_s *max_mtbl;
            int ment_count = 0;
            ment_count = locate_max_mtbl(&max_mtbl);
            assert(ment_count != 0);
            assert((int16_t)max_mtbl->lru_last != NIL);
            if(DBG)fprintf(out, "ment_count = %d\n", ment_count);
            if(ment_count < 1)
            {
                fprintf(out, "ment_count < 1");
                goto errout;
            }
            if(DBG)fprintf(out, "\tget_free_blk returned NIL, evicting a block "
                  "from max mtbl which has %hu entries\n", max_mtbl->num_blocks);
            //print_LRU(max_mtbl);
            int rc = evict_LRU(max_mtbl);
            assert(rc != 0);
            free_blk = get_free_blk();
        }
        /* A Free Block is Avaiable for Use */
        if((int16_t)free_blk != NIL)
        {
            mtbl->num_blocks++;
            update_LRU(mtbl, index);
            //print_LRU(mtbl);
            if(DBG)fprintf(out, "\tsuccessfully inserted memory entry @ blk: "
                                                  "%hu\n", free_blk);
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
errout:
    return (void *)(NIL);
}

/** Requests a location in memory to place the data identified by the mtbl and 
 * offset parameters. Also inserts the necessary info into the mtbl.
 *
 */
static void *insert_mem(struct mem_table_s *mtbl, uint64_t offset, 
                                              uint16_t *block_ndx)
{
    if(DBG)fprintf(out, "trying to insert mem...\n");
    void* rc = 0;

    /* Index into mem hash table */
    uint16_t index = offset % MEM_TABLE_HASH_MAX;
    if(DBG)fprintf(out, "\toffset: 0X%llX hashes to index: %u\n",
                                   (long long int)offset, index);

    struct mem_ent_s *current = &(mtbl->mem[index]);
    /* Lookup first */
    void *returnValue = lookup_mem(mtbl, offset, block_ndx, NULL, NULL);
    if(returnValue != (void *)NIL)
    {
        if(DBG)fprintf(out, "\tblock for this offset already exists @ 0X%lX",
                                                       (long int)returnValue);
        /* Already exists in mtbl so just return a ptr to the blk */
        return returnValue;
    }

    /* Entry doesn't exist, insertion required */
    if(DBG)fprintf(out, "\tlookup returned NIL\n");
    uint16_t mentIndex = 0;
    int evict_rc = 0;
    if((int64_t)mtbl->mem[index].tag != NIL)
    {
        /* If head occupied, need to get free ment */
        if(DBG)fprintf(out, "\thead occupied\n");
        mentIndex = get_free_ment(mtbl);
        if((int16_t)mentIndex == NIL)
        {   /* No free ment available, so attempt eviction, and try again */
            if(DBG)fprintf(out, "\tno ment\n");
            evict_rc = evict_LRU(mtbl);
            assert(evict_rc != 0);
            mentIndex = get_free_ment(mtbl);
        }
        /* Procede with memory insertion if ment aquired */
        if((int16_t)mentIndex != NIL)
        {   
            if(DBG)fprintf(out, "\tfound free memory entry @ index = %d\n",
                                                                mentIndex);
            /* Insert directly after head of chain */
            uint16_t next_ment = current->next;
            current->next = mentIndex;
            mtbl->mem[mentIndex].next = next_ment;
            rc = set_item(mtbl, offset, mentIndex); 
            mtbl->mem[index].next = mentIndex; /* link head to new ment, containing previous head */
            if(rc != (void *)NIL)
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
    if(rc != (void *)NIL)
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
    uint16_t item_index = NIL; /* index of cached block */
    uint16_t mem_ent_index = NIL;
    uint16_t mem_ent_prev_index = NIL;

    /* Check reference count */
    if(mtbl->ref_cnt != 0)
    {
        if(DBG)fprintf(out, "removal failure: ref_cnt==%d\n", mtbl->ref_cnt);
        return 0;
    }
    void *retValue = lookup_mem(mtbl, offset, &item_index, &mem_ent_index, 
                                                     &mem_ent_prev_index);
    if(DBG)fprintf(out, "retValue = 0X%lX\n", (long unsigned int) retValue);
    /* Verify we've recieved the necessary info */
    if(retValue == (void *)NIL)
    {
        if(DBG)fprintf(out, "\tremoval failure: memory entry not found "
                                                   "matching offset\n");
        return 0;
    }
        /* Update First and Last...First */
        if(mem_ent_index == mtbl->lru_first)
        {
            /* Is node the head? */
            mtbl->lru_first = mtbl->mem[mem_ent_index].lru_next;
        }
        if(mem_ent_index == mtbl->lru_last)
        {
            /* Is node the tail? */
            //print_LRU(mtbl);
            //fprintf(out, "index.prev = %hu\n", mtbl->mem[mem_ent_index].lru_prev);
            //assert((int16_t)mtbl->mem[mem_ent_index].lru_prev!=NIL);
            mtbl->lru_last = mtbl->mem[mem_ent_index].lru_prev;
        }
    
        /* Remove from LRU */
        /* Update each of the adjacent nodes' link */
        uint16_t lru_prev = mtbl->mem[mem_ent_index].lru_prev;
        if((int16_t)lru_prev != NIL)
        {
            mtbl->mem[lru_prev].lru_next = mtbl->mem[mem_ent_index].lru_next;
        }
        uint16_t lru_next = mtbl->mem[mem_ent_index].lru_next;
        if((int16_t)lru_next != NIL)
        {
            mtbl->mem[lru_next].lru_prev = mtbl->mem[mem_ent_index].lru_prev;
        }

    /* Remove from dirty list if Dirty */
    /* Previous, Current, Next */
    uint16_t p = NIL;
    uint16_t c = NIL;
    uint16_t n = NIL;
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
        if((int16_t)p != NIL)
        {
            mtbl->mem[p].dirty_next = mtbl->mem[c].dirty_next;
        }
        if(c == mtbl->dirty_list)
        {
            mtbl->dirty_list = mtbl->mem[c].dirty_next;
        }
        mtbl->mem[c].dirty_next = NIL;
    }
    
    /* Add memory block back to free list */
    assert((int16_t) item_index != NIL);
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

/* The following two functions are provided for error checking purposes. */
void print_LRU(struct mem_table_s *mtbl)
{
    fprintf(out, "\tprinting lru list:\n");
    fprintf(out, "\t\tmru: %hu\n", mtbl->lru_first);
    uint16_t current = mtbl->mem[mtbl->lru_first].lru_next; 
    do
    {
        fprintf(out, "\t\t%hu\n", current);
        current = mtbl->mem[current].lru_next;
    } while((int16_t)current != mtbl->lru_last && (int16_t)current!=NIL);
    fprintf(out, "\t\tlru: %hu\n", mtbl->lru_last);
}

/*
static void print_dirty(struct mem_table_s *mtbl)
{
    if(DBG)fprintf(out, "\tprinting dirty list:\n");
    if(DBG)fprintf(out, "\t\tdirty_list head: %hu\n",\
                                    mtbl->dirty_list);
    uint16_t current= mtbl->dirty_list;
    while((int16_t)current != NIL)
    {
        if(DBG)fprintf(out, "\t\tcurrent: %d\tnext: %hu\n", 
                   current, mtbl->mem[current].dirty_next); 
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
