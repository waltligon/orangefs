#ifndef CLIENT_CACHE_H
#define CLIENT_CACHE_H 1

#include <stdint.h>
#include <pthread.h>
#include "pvfs2-types.h"
#include "gen-locks.h"

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

#define NIL8  0XFF
#define NIL16 0XFFFF

/* #define INLINE inline */
#define INLINE

typedef enum
{
    CLIENT_CACHE_NONE               =       0,
    CLIENT_CACHE_ENABLED            = (1 << 0),
    CLIENT_CACHE_DEFAULT_CACHE      = (1 << 1),
    CLIENT_CACHE_FOR_CLIENT_CORE    = (1 << 2)
} PINT_client_cache_flag;

typedef struct cc_ment_s
{
    uint64_t tag;           /* offset of data block in file */
    uint16_t blk_index;     /* index of cache block with data */
    uint16_t index;         /* this ment's index in mtbl.ments */
    uint16_t prev;          /* previous ment in ht chain */
    uint16_t next;          /* next ment in ht chain */
    uint16_t ru_prev;       /* used in ru list */
    uint16_t ru_next;       /* used in ru list */
    uint16_t dirty_prev;    /* previous ment index in dirty list */
    uint16_t dirty_next;    /* next ment index in dirty list */
    uint16_t dirty;         /* 1 if dirty, 0 if clean. */
} cc_ment_t;

typedef struct cc_mtbl_s
{
    uint16_t *ments_ht;         /* Hash table for ments indexes */
    cc_ment_t *ments;           /* all ments */
    uint64_t max_offset_seen;   /* Largest I/O offset seen by cache */
    uint16_t num_blks;          /* number of used blocks in this mtbl */
    uint16_t free_ment;         /* index of next free mem entry */
    uint16_t mru;               /* index of first block on lru list */
    uint16_t lru;               /* index of last block on lru list */
    uint16_t dirty_first;       /* index of first dirty block */
    uint16_t ref_cnt;           /* number of client threads using this file */
    uint16_t fent_index;        /* the fent this mtbl is associated with */
    //uint16_t ment_limit;      /* we could support custom limits per file */
    //uint16_t ment_ht_limit;   /* we could support custom limits per file */
} cc_mtbl_t;

typedef struct cc_fent_s
{
    cc_mtbl_t mtbl;
    uint64_t file_handle;
    int32_t fsid;
    PVFS_uid uid;           /* uid copied from PVFS_credential */
    PVFS_gid gid;           /* gid copied from PVFS_credential group_array */
    uint16_t prev;          /* prev fent in ht chain. */
    uint16_t next;          /* next fent in ht chain and free fents LL */
    uint16_t index;         /* this fent's index in ftbl.fents */
    uint16_t ru_prev;       /* used in lru list */
    uint16_t ru_next;       /* used in lru list */
} cc_fent_t;

typedef struct cc_ftbl_s
{
    uint16_t *fents_ht; /* Hash table for fent indexes */
    cc_fent_t *fents;   /* All fents */
    uint16_t free_fent; /* Index of the next free file entry */
    uint16_t mru;       /* Index of first fent on lru list */
    uint16_t lru;       /* Index of last fent on lru list */
} cc_ftbl_t;

typedef struct client_cache_s
{
    cc_ftbl_t ftbl;
    void *blks;
    gen_mutex_t mutex;
    PINT_client_cache_flag status;
    uint16_t free_blk;
    /* Maintain limits/settings in cache struct */
    uint64_t cache_size;
    uint64_t blk_size;
    uint16_t num_blks;
    uint16_t fent_limit;
    uint16_t fent_ht_limit;
    uint16_t fent_size;
    uint16_t ment_limit;
    uint16_t ment_ht_limit;
    uint16_t ment_size;
} client_cache_t;

typedef struct cc_free_block_s
{
    uint16_t next;  /* Index of next free block in LL */
} cc_free_block_t;

void PINT_client_cache_debug_flags(PINT_client_cache_flag flags);
cc_fent_t *PINT_client_cache_fent_insert(const uint64_t fhandle,
                                         const int32_t fsid,
                                         const PVFS_uid uid,
                                         const PVFS_gid gid,
                                         const gen_lock_hint_t lock_hint);
cc_fent_t *PINT_client_cache_fent_lookup(const uint64_t fhandle,
                                         const int32_t fsid,
                                         const PVFS_uid uid,
                                         const PVFS_gid gid,
                                         const gen_lock_hint_t lock_hint);
int PINT_client_cache_finalize(void);
int PINT_client_cache_initialize(
    PINT_client_cache_flag flags,
    uint64_t cache_size,
    uint64_t block_size,
    uint16_t fent_limit,
    uint16_t fent_ht_limit,
    uint16_t ment_limit,
    uint16_t ment_ht_limit);
int PINT_client_cache_initialize_defaults(void);
cc_ment_t *PINT_client_cache_ment_lookup(cc_mtbl_t *mtblp,
                                         const int64_t tag,
                                         const gen_lock_hint_t lock_hint);

extern client_cache_t cc;

#endif /* CLIENT_CACHE_H */