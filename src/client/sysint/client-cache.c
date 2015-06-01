#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>

#include "client-cache.h"

client_cache_t cc;

int init_cache(
    uint64_t byte_limit,
    uint64_t block_size,
    uint16_t fent_limit,
    uint16_t fent_ht_limit)
{
    int ret = 0;
    int num_blocks = 0;
    int i = 0;
    uint16_t *temp = NULL;
    printf("%s\n", __func__);

    /* Store limits w/ cc */
    cc.fent_limit = fent_limit;
    cc.fent_ht_limit = fent_ht_limit;
    //cc.ment_limit = ment_limit;
    //cc.ment_ht_limit = ment_ht_limit;

    /* Allocate memory for blocks. */
    num_blocks = byte_limit / block_size;
    printf("%s: num_blocks = %d\n", __func__, num_blocks);
    if(num_blocks == 0)
    {
        fprintf(stderr, "%s: WARN num_blocks is ZERO!\n", __func__);
        return 0;
    }
    cc.blks = calloc(1, num_blocks * block_size);
    if(cc.blks == NULL)
    {
        fprintf(stderr, "%s: ERROR allocating memory for blks!\n", __func__);
        return -1;
    }
    printf("%s: cacheable bytes = %llu = %Lf MiB\n",
           __func__,
           (long long unsigned int) (num_blocks * block_size),
           ((long double) ((num_blocks * block_size)) / (1024.0 * 1024.0)));

    /* Allocate memory for fents */
    cc.ftbl.fents = calloc(1, fent_limit * sizeof(cc_fent_t));
    if(cc.ftbl.fents == NULL)
    {
        fprintf(stderr, "%s: ERROR allocating memory for fents!\n", __func__);
        return -1;
    }
    printf("%s: fents bytes = %llu\n",
           __func__,
           (long long unsigned int) fent_limit * sizeof(cc_fent_t));

    /* Allocate memory for fents ht */
    cc.ftbl.fents_ht = calloc(1, fent_ht_limit * sizeof(uint16_t));
    if(cc.ftbl.fents_ht == NULL)
    {
        fprintf(stderr,
                "%s: ERROR allocating memory for fents_ht!\n",
                __func__);
        return -1;
    }

    /* Set all hash table buckets to NIL16 */
    for(i = 0, temp = cc.ftbl.fents_ht; i < fent_ht_limit; i++, temp++)
    {
        *temp = NIL16;
    }
    printf("%s: fents_ht bytes = %llu\n",
           __func__,
           (long long unsigned int) fent_ht_limit * sizeof(uint16_t));

    return 0;
}

int finalize_cache(void)
{
    int ret = 0;
    int i = 0;
    printf("%s\n", __func__);

    /* Flush all dirty blocks */
    /* TODO */

    /* Free blks memory region */
    if(cc.blks != NULL)
    {
        free(cc.blks);
    }

    /* Free fents memory region: */
    if(cc.ftbl.fents != NULL)
    {
        /* TODO delve into fent and free underlying allocated structs like ments */

        free(cc.ftbl.fents);
    }

    /* Free fents_ht memory region: */
    if(cc.ftbl.fents_ht != NULL)
    {
        free(cc.ftbl.fents_ht);
    }

    /* For every bucket, walk the list and...*/
        /* Free ments memory region */
        /* For every bucket, walk the list and */
#if 0
    ret = pthread_rwlock_destroy(rwlockp);
    if(ret != 0)
    {
        fprintf(stderr,
                "%s: pthread_rwlock_destroy returned %d\n",
                __func__,
                ret);
        return -1;
    }
#endif
    return 0;
}

int main(int argc, char** argv)
{
    int ret = 0;
    printf("%s\n", __func__);
    printf("FYI: sizeof(pthread_rwlock_t) = %zu\n", sizeof(pthread_rwlock_t));
    ret = init_cache(BYTE_LIMIT, BLOCK_SIZE_B, FENT_LIMIT, FENT_HT_LIMIT);
    if(ret != 0)
    {
        fprintf(stderr, "%s: init_cache returned %d\n", __func__, ret);
        finalize_cache();
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
