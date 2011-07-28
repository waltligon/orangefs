/* 
 * (C) 2011 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#ifndef UCACHE_H
#define UCACHE_H 1

#include <sys/types.h>
#include <sys/shm.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

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
#define SVSHM_MODE   (SHM_R | SHM_W | SHM_R>>3 | SHM_R>>6) 
#define CACHE_FLAGS (SVSHM_MODE | IPC_CREAT)

#define NIL (-1)
#define DBG 0
#define F_EVICT 1   /*  Evict files if necessary    */
#define M_EVICT 1   /*  Evict memory entries if necessary   */

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

/*  Function Declarations   */
static union user_cache_u *get_ucache();
static void add_free_mtbls(int blk);
void ucache_initialize(void);
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
void remove_all_memory_entries(struct mem_table_s *mtbl);
void put_free_mtbl(struct mem_table_s *mtbl, struct file_ent_s *file);
void evict_file(unsigned int index);
static struct mem_table_s *insert_file(uint32_t fs_id, uint64_t handle);
static int remove_file(uint32_t fs_id, uint64_t handle);
static void *lookup_mem(struct mem_table_s *mtbl, 
                    uint64_t offset, 
                    uint32_t *item_index,
                    uint16_t *mem_ent_index,
                    uint16_t *mem_ent_prev_index
);
void update_lru(struct mem_table_s *mtbl, uint16_t index);
static int locate_max_mtbl(struct mem_table_s **mtbl);
void evict_LRU(struct mem_table_s *mtbl);
static void *set_item(struct mem_table_s *mtbl, 
                    uint64_t offset, 
                    uint16_t index
);
static void *insert_mem(struct mem_table_s *mtbl, uint64_t offset);
static int remove_mem(struct mem_table_s *mtbl, uint64_t offset);

/*  Dirty List Iterator */
static int dirty_done(uint16_t index)
{
    return ((int16_t)index==NIL);
}
static int dirty_next(struct mem_table_s *mtbl, uint16_t index)
{
    return mtbl->mem[index].dirty_next;
}

/*  Memory Entry Chain Iterator */
static int ment_done(int index)
{
    return ((int16_t)index==NIL);
}
static int ment_next(struct mem_table_s *mtbl, int index)
{
    return mtbl->mem[index].next;
}

/*  File Entry Chain Iterator   */
static int file_done(int index)
{
    return ((int16_t)index==NIL);
}
static int file_next(struct file_table_s *ftbl, int index)
{
    return ftbl->file[index].next;
}

/*  list printing functions */
void print_lru(struct mem_table_s *mtbl);
void print_dirty(struct mem_table_s *mtbl);

/* externally visible API */
#if 1
typedef uint32_t PVFS_fs_id;
typedef uint64_t PVFS_object_ref;
int ucache_open_file(PVFS_fs_id *fs_id, PVFS_object_ref *handle)
{
    struct mem_table_s *mtbl= lookup_file(
        (uint32_t)(*fs_id), (uint64_t)(*handle), NULL, NULL, NULL, NULL);
    if((int)mtbl==NIL)
    {
        insert_file((uint32_t)*fs_id, (uint64_t)*handle);
    }
    return 1;
}

void *ucache_lookup(PVFS_fs_id *fs_id, PVFS_object_ref *handle, uint64_t offset)
{
    struct mem_table_s *mtbl= lookup_file(
        (uint32_t)(*fs_id), (uint64_t)(*handle), NULL, NULL, NULL, NULL);
    if((int)mtbl!=NIL)
    {
        return(lookup_mem(mtbl, (uint64_t)offset, NULL, NULL, NULL)); 
    }
    return (void *)NIL;
}

void *ucache_insert(PVFS_fs_id *fs_id, PVFS_object_ref *handle, uint64_t offset)
{
    struct mem_table_s *mtbl= lookup_file(
        (uint32_t)(*fs_id), (uint64_t)(*handle), NULL, NULL, NULL, NULL);
    if((int)mtbl==NIL)
    {
        return (void *)NIL;
    }
    else
    {
        remove_mem(mtbl, (uint64_t)offset);
        return insert_mem(mtbl, (uint64_t)offset); 
    }
}

int ucache_remove(PVFS_fs_id *fs_id, PVFS_object_ref *handle, uint64_t offset)
{
    struct mem_table_s *mtbl= lookup_file(
        (uint32_t)(*fs_id), (uint64_t)(*handle), NULL, NULL, NULL, NULL);
    if((int)mtbl!=NIL)
    {
        return remove_mem(mtbl, (uint64_t)offset); 
    }
    return NIL;
}

int ucache_flush(PVFS_fs_id *fs_id, PVFS_object_ref *handle)
{
    struct mem_table_s *mtbl= lookup_file(
        (uint32_t)(*fs_id), (uint64_t)(*handle), NULL, NULL, NULL, NULL);
    if((int)mtbl==NIL)
    {
        return NIL;
    }
    int i;
    for(i=mtbl->dirty_list; !dirty_done(i); i=dirty_next(mtbl, i)){
        struct mem_ent_s *ment = &(mtbl->mem[i]);
        mtbl->mem[i].dirty_next = NIL;
        if((int64_t)ment->tag==NIL || (int32_t)ment->item==NIL){
            break;
        }
        //flush block to disk
    }
    mtbl->dirty_list = NIL;
    return 1;
}

int ucache_close_file(PVFS_fs_id *fs_id, PVFS_object_ref *handle)
{
    uint32_t file_mtbl_blk;
    uint16_t file_mtbl_ent;
    uint16_t file_ent_index;
    uint16_t file_ent_prev_index;
    struct mem_table_s *mtbl= lookup_file(
        (uint32_t)(*fs_id), 
        (uint64_t)(*handle), 
        &file_mtbl_blk, 
        &file_mtbl_ent, 
        &file_ent_index, 
        &file_ent_prev_index);
    if((int)mtbl==NIL)
    {
        return NIL;
    }
    remove_all_memory_entries(mtbl);
    struct file_ent_s *file = &(get_ucache()->ftbl.file[file_ent_index]);
    put_free_mtbl(mtbl, file);
    put_free_fent(file_ent_index);
}
#endif
#endif 

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

