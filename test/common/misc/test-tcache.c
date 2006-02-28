/*
 * Copyright © Acxiom Corporation, 2005
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <assert.h>
#include <unistd.h>

#include "pvfs2.h"
#include "tcache.h"

static void usage(int argc, char** argv);
static int foo_compare_key_entry(void* key, struct qhash_head* link);
static int foo_hash_key(void* key, int table_size);
static int foo_free_payload(void* payload);

/* test payload */
struct foo_payload
{
    int key;
    float value;
};

int main(int argc, char **argv)
{
    struct PINT_tcache* test_tcache;
    int i;
    struct foo_payload* tmp_payload;
    struct PINT_tcache_entry* test_entry = NULL;
    int ret = 0;
    int status = 0;
    unsigned int param = 0;
    int removed = 0;
    int reclaimed = 0;
    
    if(argc != 1)
    {
        usage(argc, argv);
        return(-1);
    }

    /* initialize */
    printf("Initializing cache...\n");
    test_tcache = PINT_tcache_initialize(foo_compare_key_entry,
        foo_hash_key,
        foo_free_payload);
    if(!test_tcache)
    {
        fprintf(stderr, "PINT_tcache_initialize failure.\n");
        return(-1);
    }
    printf("Done.\n");

    /* read current parameters */
    printf("Reading parameters...\n");
    ret = PINT_tcache_get_info(test_tcache, TCACHE_TIMEOUT_MSECS, &param);
    assert(ret == 0);
    printf("timeout_msecs: %d\n", param);
    ret = PINT_tcache_get_info(test_tcache, TCACHE_NUM_ENTRIES, &param);
    assert(ret == 0);
    printf("num_entries: %d\n", param);
    ret = PINT_tcache_get_info(test_tcache, TCACHE_HARD_LIMIT, &param);
    assert(ret == 0);
    printf("hard_limit: %d\n", param);
    ret = PINT_tcache_get_info(test_tcache, TCACHE_SOFT_LIMIT, &param);
    assert(ret == 0);
    printf("soft_limit: %d\n", param);
    ret = PINT_tcache_get_info(test_tcache, TCACHE_ENABLE, &param);
    assert(ret == 0);
    printf("enable: %d\n", param);
    ret = PINT_tcache_get_info(test_tcache, TCACHE_RECLAIM_PERCENTAGE, &param);
    assert(ret == 0);
    printf("reclaim_percentage: %d\n", param);
    printf("Done.\n");

    /* set parameters */
    printf("Setting parameters...\n");
    param = 4000;
    ret = PINT_tcache_set_info(test_tcache, TCACHE_TIMEOUT_MSECS, param);
    assert(ret == 0);
    param = 30;
    ret = PINT_tcache_set_info(test_tcache, TCACHE_NUM_ENTRIES, param);
    assert(ret < 0);
    param = 10;
    ret = PINT_tcache_set_info(test_tcache, TCACHE_HARD_LIMIT, param);
    assert(ret == 0);
    param = 5;
    ret = PINT_tcache_set_info(test_tcache, TCACHE_SOFT_LIMIT, param);
    assert(ret == 0);
    param = 1;
    ret = PINT_tcache_set_info(test_tcache, TCACHE_ENABLE, param);
    assert(ret == 0);
    param = 50;
    ret = PINT_tcache_set_info(test_tcache, TCACHE_RECLAIM_PERCENTAGE, param);
    assert(ret == 0);
    printf("Done.\n");

    /* read current parameters */
    printf("Reading parameters...\n");
    ret = PINT_tcache_get_info(test_tcache, TCACHE_TIMEOUT_MSECS, &param);
    assert(ret == 0);
    printf("timeout_msecs: %d\n", param);
    ret = PINT_tcache_get_info(test_tcache, TCACHE_NUM_ENTRIES, &param);
    assert(ret == 0);
    printf("num_entries: %d\n", param);
    ret = PINT_tcache_get_info(test_tcache, TCACHE_HARD_LIMIT, &param);
    assert(ret == 0);
    printf("hard_limit: %d\n", param);
    ret = PINT_tcache_get_info(test_tcache, TCACHE_SOFT_LIMIT, &param);
    assert(ret == 0);
    printf("soft_limit: %d\n", param);
    ret = PINT_tcache_get_info(test_tcache, TCACHE_ENABLE, &param);
    assert(ret == 0);
    printf("enable: %d\n", param);
    ret = PINT_tcache_get_info(test_tcache, TCACHE_RECLAIM_PERCENTAGE, &param);
    assert(ret == 0);
    printf("reclaim_percentage: %d\n", param);
    printf("Done.\n");


    /* insert some entries */
    for(i=0; i< 3; i++)
    {
        tmp_payload = (struct foo_payload*)malloc(sizeof(struct
            foo_payload));
        assert(tmp_payload);
        tmp_payload->key = i;
        tmp_payload->value = i;

        printf("Inserting %d...\n", i);
        ret = PINT_tcache_insert_entry(test_tcache, &i, 
                                       tmp_payload, &removed);
        if(ret < 0)
        {
            PVFS_perror("PINT_tcache_insert", ret);
            return(-1);
        }
        printf("Done.\n");
        sleep(1);
    }

    /* lookup all three */
    for(i=0; i<3; i++)
    {
        printf("Looking up entry %d\n", i);
        ret = PINT_tcache_lookup(test_tcache, &i, &test_entry, &status);
        if(ret < 0)
        {
            PVFS_perror("PINT_tcache_lookup", ret);
            return(-1);
        }
        if(status != 0)
        {
            PVFS_perror("PINT_tcache_lookup status", status);
            return(-1);
        }
        printf("Done.\n");
        tmp_payload = test_entry->payload;
        printf("Value: %f\n", tmp_payload->value);
    }

    /* sleep until at least first expires */
    sleep(2);
    i=0;
    printf("Looking up expired entry %d\n", i);
    ret = PINT_tcache_lookup(test_tcache, &i, &test_entry, &status);
    if(ret < 0)
    {
        PVFS_perror("PINT_tcache_lookup", ret);
        return(-1);
    }
    if(status != -PVFS_ETIME)
    {
        PVFS_perror("PINT_tcache_lookup status", status);
        return(-1);
    }
    printf("Done.\n");
    tmp_payload = test_entry->payload;
    printf("Value: %f\n", tmp_payload->value);

    i=2;
    printf("Looking up valid entry %d\n", i);
    ret = PINT_tcache_lookup(test_tcache, &i, &test_entry, &status);
    if(ret < 0)
    {
        PVFS_perror("PINT_tcache_lookup", ret);
        return(-1);
    }
    if(status != 0)
    {
        PVFS_perror("PINT_tcache_lookup status", status);
        return(-1);
    }
    printf("Done.\n");
    tmp_payload = test_entry->payload;
    printf("Value: %f\n", tmp_payload->value);

    /* try destroying an entry */
    printf("Destroying an entry...\n");
    ret = PINT_tcache_purge(test_tcache, test_entry);
    if(ret < 0)
    {
        PVFS_perror("PINT_tcache_purge", ret);
        return(-1);
    }
    printf("Done.\n");

    i=2;
    printf("Looking up destroyed entry %d\n", i);
    ret = PINT_tcache_lookup(test_tcache, &i, &test_entry, &status);
    if(ret == 0)
    {
        PVFS_perror("PINT_tcache_lookup", ret);
        return(-1);
    }
    printf("Done.\n");

    i=200;
    printf("Looking up entry that never existed %d\n", i);
    ret = PINT_tcache_lookup(test_tcache, &i, &test_entry, &status);
    if(ret == 0)
    {
        PVFS_perror("PINT_tcache_lookup", ret);
        return(-1);
    }
    printf("Done.\n");

    sleep(5);

    /* insert a new entry */
    i=3;
    tmp_payload = (struct foo_payload*)malloc(sizeof(struct
        foo_payload));
    assert(tmp_payload);
    tmp_payload->key = i;
    tmp_payload->value = i;
    printf("Inserting %d...\n", i);
    ret = PINT_tcache_insert_entry(test_tcache, &i, 
                                   tmp_payload, &removed);
    if(ret < 0)
    {
        PVFS_perror("PINT_tcache_insert", ret);
        return(-1);
    }
    printf("Done.\n");

    /* reclaim */
    ret = PINT_tcache_reclaim(test_tcache, &reclaimed);
    if(ret < 0)
    {
        PVFS_perror("PINT_tcache_reclaim", ret);
        return(-1);
    }

    /* finalize */
    printf("Finalizing cache...\n");
    PINT_tcache_finalize(test_tcache);
    printf("Done.\n");

    /* initialize */
    printf("Initializing cache...\n");
    test_tcache = PINT_tcache_initialize(foo_compare_key_entry,
        foo_hash_key,
        foo_free_payload);
    if(!test_tcache)
    {
        fprintf(stderr, "PINT_tcache_initialize failure.\n");
        return(-1);
    }
    printf("Done.\n");

    /* disable */
    printf("Disabling...\n");
    param = 0;
    ret = PINT_tcache_set_info(test_tcache, TCACHE_ENABLE, param);
    assert(ret == 0);
    printf("Done.\n");

    /* insert some entries */
    for(i=0; i< 3; i++)
    {
        tmp_payload = (struct foo_payload*)malloc(sizeof(struct
            foo_payload));
        assert(tmp_payload);
        tmp_payload->key = i;
        tmp_payload->value = i;

        printf("Inserting %d...\n", i);
        ret = PINT_tcache_insert_entry(test_tcache, &i, 
                                       tmp_payload, &removed);
        if(ret < 0)
        {
            PVFS_perror("PINT_tcache_insert", ret);
            return(-1);
        }
        printf("Done.\n");
        sleep(1);
    }

    /* enable */
    printf("Enabling...\n");
    param = 1;
    ret = PINT_tcache_set_info(test_tcache, TCACHE_ENABLE, param);
    assert(ret == 0);
    printf("Done.\n");

    /* set parameters */
    printf("Setting parameters...\n");
    param = 4000;
    ret = PINT_tcache_set_info(test_tcache, TCACHE_TIMEOUT_MSECS, param);
    assert(ret == 0);
    param = 30;
    ret = PINT_tcache_set_info(test_tcache, TCACHE_NUM_ENTRIES, param);
    assert(ret < 0);
    param = 10;
    ret = PINT_tcache_set_info(test_tcache, TCACHE_HARD_LIMIT, param);
    assert(ret == 0);
    param = 5;
    ret = PINT_tcache_set_info(test_tcache, TCACHE_SOFT_LIMIT, param);
    assert(ret == 0);
    param = 1;
    ret = PINT_tcache_set_info(test_tcache, TCACHE_ENABLE, param);
    assert(ret == 0);
    param = 50;
    ret = PINT_tcache_set_info(test_tcache, TCACHE_RECLAIM_PERCENTAGE, param);
    assert(ret == 0);
    printf("Done.\n");



    /* insert some entries */
    for(i=0; i< 5; i++)
    {
        tmp_payload = (struct foo_payload*)malloc(sizeof(struct
            foo_payload));
        assert(tmp_payload);
        tmp_payload->key = i;
        tmp_payload->value = i;

        printf("Inserting %d...\n", i);
        ret = PINT_tcache_insert_entry(test_tcache, &i, 
                                       tmp_payload, &removed);
        if(ret < 0)
        {
            PVFS_perror("PINT_tcache_insert", ret);
            return(-1);
        }
        printf("Done.\n");
    }

    sleep(5);

    /* check num_entries */
    ret = PINT_tcache_get_info(test_tcache, TCACHE_NUM_ENTRIES, &param);
    assert(ret == 0);
    printf("num_entries: %d\n", param);

    /* insert some entries */
    for(i=5; i< 6; i++)
    {
        tmp_payload = (struct foo_payload*)malloc(sizeof(struct
            foo_payload));
        assert(tmp_payload);
        tmp_payload->key = i;
        tmp_payload->value = i;

        printf("Inserting %d...\n", i);
        ret = PINT_tcache_insert_entry(test_tcache, &i, 
                                       tmp_payload, &removed);
        if(ret < 0)
        {
            PVFS_perror("PINT_tcache_insert", ret);
            return(-1);
        }
        printf("Done.\n");
    }

    /* check num_entries */
    ret = PINT_tcache_get_info(test_tcache, TCACHE_NUM_ENTRIES, &param);
    assert(ret == 0);
    printf("num_entries: %d\n", param);

    /* insert some entries */
    for(i=6; i< 20; i++)
    {
        tmp_payload = (struct foo_payload*)malloc(sizeof(struct
            foo_payload));
        assert(tmp_payload);
        tmp_payload->key = i;
        tmp_payload->value = i;

        printf("Inserting %d...\n", i);
        ret = PINT_tcache_insert_entry(test_tcache, &i, 
                                       tmp_payload, &removed);
        if(ret < 0)
        {
            PVFS_perror("PINT_tcache_insert", ret);
            return(-1);
        }
        printf("Done.\n");
    }

    /* check num_entries */
    ret = PINT_tcache_get_info(test_tcache, TCACHE_NUM_ENTRIES, &param);
    assert(ret == 0);
    printf("num_entries: %d\n", param);

    return(0);
}

static void usage(int argc, char** argv)
{
    fprintf(stderr, "\n");
    fprintf(stderr, "Usage  : %s\n", argv[0]);
    return;
}

static int foo_compare_key_entry(void* key, struct qhash_head* link)
{
    int* real_key = (int*)key;
    struct foo_payload* tmp_payload = NULL;
    struct PINT_tcache_entry* tmp_entry = NULL;

    tmp_entry = qlist_entry(link, struct PINT_tcache_entry, hash_link);
    assert(tmp_entry);

    tmp_payload = (struct foo_payload*)tmp_entry->payload;
    if(*real_key == tmp_payload->key)
    {
        return(1);
    }

    return(0);
}

static int foo_hash_key(void* key, int table_size)
{
    int* real_key = (int*)key;
    int tmp_ret = 0;

    tmp_ret = (*real_key)%table_size;
    return(tmp_ret);
}

static int foo_free_payload(void* payload)
{
    free(payload);
    return(0);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

