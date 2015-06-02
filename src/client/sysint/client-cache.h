#ifndef CLIENT_CACHE_H
#define CLIENT_CACHE_H 1

#include <stdint.h>
#include <pthread.h>

#define PVFS2_SIZEOF_VOIDP 64
#if (PVFS2_SIZEOF_VOIDP == 32)
# define NILP NIL32
#elif (PVFS2_SIZEOF_VOIDP == 64)
# define NILP NIL64
#endif

#define BLOCK_SIZE_K 256
#define BLOCK_SIZE_B (BLOCK_SIZE_K * 1024)
#define BYTE_LIMIT (MENT_LIMIT * BLOCK_SIZE_B)

#define FENT_LIMIT 256
#define FENT_HT_LIMIT 29
#define MENT_LIMIT 256
#define MENT_HT_LIMIT 29

#define NIL16 0XFFFF

typedef struct cc_ment_s
{
    uint64_t tag;           /* offset of data block in file */
    uint16_t item;          /* index of cache block with data */
    uint16_t next;          /* use for hash table chain */
    uint16_t dirty_next;    /* if dirty used in dirty list */
    uint16_t lru_prev;      /* used in lru list */
    uint16_t lru_next;      /* used in lru list */
} cc_ment_t;

typedef struct cc_mtbl_s
{
    uint16_t *ments_ht;         /* Hash table for ments indexes */
    cc_ment_t *ments;           /* all ments */
    uint16_t num_blocks;        /* number of used blocks in this mtbl */
    uint16_t free_ment;         /* index of next free mem entry */
    uint16_t lru_first;         /* index of first block on lru list */
    uint16_t lru_last;          /* index of last block on lru list */
    uint16_t dirty_first;       /* index of first dirty block */
    uint16_t ref_cnt;           /* number of client threads using this file */
} cc_mtbl_t;

typedef struct cc_fent_s
{
    cc_mtbl_t mtbl;
    uint64_t file_size; /* Largest I/O offset seen by cache... */
    uint64_t file_handle;
    uint32_t fsid;
    uint16_t next; /* next fent in ht chain and free fents LL */
    uint16_t index; /* this fent's index in ftbl.fents */
    uint16_t ru_prev;      /* used in lru list */
    uint16_t ru_next;      /* used in lru list */
} cc_fent_t;

typedef struct cc_ftbl_s
{
    uint16_t *fents_ht;     /* Hash table for fent indexes */
    cc_fent_t *fents;       /* All fents */
    uint16_t free_fent;     /* Index of the next free file entry */
    uint16_t mru;           /* Index of first fent on lru list */
    uint16_t lru;           /* Index of last fent on lru list */
} cc_ftbl_t;

typedef struct client_cache_s
{
    cc_ftbl_t ftbl;
    void *blks;
    uint16_t free_blk;
    /* Maintain limits in cache struct */
    uint64_t cache_size;
    uint64_t block_size;
    uint16_t num_blocks;
    uint16_t fent_limit;
    uint16_t fent_ht_limit;
    uint16_t fent_size;
    uint16_t ment_limit;
    uint16_t ment_ht_limit;
    uint16_t ment_size;
} client_cache_t;

typedef struct free_block_s
{
    uint16_t next; /* Index of next free block in LL */
} free_block_t;

/* pthread_rwlock_t rwlock; */
#endif /* CLIENT_CACHE_H */