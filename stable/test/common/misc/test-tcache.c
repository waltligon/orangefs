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
static void check_param(char *parameter_name, int param, int expected_value);

#define TEST_TIMEOUT_MSEC     4000
#define TEST_NUM_ENTRIES        30
#define TEST_HARD_LIMIT         10
#define TEST_SOFT_LIMIT          5
#define TEST_ENABLE              1
#define TEST_RECLAIM_PERCENTAGE 50


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
    int tmp_count = 0;

    if(argc != 1)
    {
        usage(argc, argv);
        return(-1);
    }

    /* initialize */
    printf("Initializing cache... ");
    test_tcache = PINT_tcache_initialize(foo_compare_key_entry,
        foo_hash_key,
        foo_free_payload,
        -1);
    if(!test_tcache)
    {
        fprintf(stderr, "PINT_tcache_initialize failure.\n");
        return(-1);
    }
    printf("Done.\n");

    /* set parameters */
    printf("Setting all TCACHE parameters... ");
    param = TEST_TIMEOUT_MSEC;
    ret = PINT_tcache_set_info(test_tcache, TCACHE_TIMEOUT_MSECS, param);
    assert(ret == 0);
    param = TEST_NUM_ENTRIES;
    ret = PINT_tcache_set_info(test_tcache, TCACHE_NUM_ENTRIES, param);
    assert(ret == -PVFS_EINVAL);
    param = TEST_HARD_LIMIT;
    ret = PINT_tcache_set_info(test_tcache, TCACHE_HARD_LIMIT, param);
    assert(ret == 0);
    param = TEST_SOFT_LIMIT;
    ret = PINT_tcache_set_info(test_tcache, TCACHE_SOFT_LIMIT, param);
    assert(ret == 0);
    param = TEST_ENABLE;
    ret = PINT_tcache_set_info(test_tcache, TCACHE_ENABLE, param);
    assert(ret == 0);
    param = TEST_RECLAIM_PERCENTAGE;
    ret = PINT_tcache_set_info(test_tcache, TCACHE_RECLAIM_PERCENTAGE, param);
    assert(ret == 0);
    printf("Done.\n");

    /* read current parameters */
    printf("Reading all TCACHE parameters... ");
    ret = PINT_tcache_get_info(test_tcache, TCACHE_TIMEOUT_MSECS, &param);
    assert(ret == 0);
    check_param("TCACHE_TIMEOUT_MSECS", param, TEST_TIMEOUT_MSEC);
    ret = PINT_tcache_get_info(test_tcache, TCACHE_NUM_ENTRIES, &param);
    assert(ret == 0);
    check_param("TCACHE_NUM_ENTRIES", param, 0);
    ret = PINT_tcache_get_info(test_tcache, TCACHE_HARD_LIMIT, &param);
    assert(ret == 0);
    check_param("TCACHE_HARD_LIMIT", param, TEST_HARD_LIMIT);
    ret = PINT_tcache_get_info(test_tcache, TCACHE_SOFT_LIMIT, &param);
    assert(ret == 0);
    check_param("TCACHE_SOFT_LIMIT", param, TEST_SOFT_LIMIT);
    ret = PINT_tcache_get_info(test_tcache, TCACHE_ENABLE, &param);
    assert(ret == 0);
    check_param("TCACHE_ENABLE", param, TEST_ENABLE);
    ret = PINT_tcache_get_info(test_tcache, TCACHE_RECLAIM_PERCENTAGE, &param);
    assert(ret == 0);
    check_param("TCACHE_RECLAIM_PERCENTAGE", param, TEST_RECLAIM_PERCENTAGE);
    printf("Done.\n");

    /* insert some entries */
    for(i=0; i< 3; i++)
    {
        tmp_payload = (struct foo_payload*)malloc(sizeof(struct
            foo_payload));
        assert(tmp_payload);
        tmp_payload->key = i;
        tmp_payload->value = i;

        printf("Inserting [%d]... ", i);
        ret = PINT_tcache_insert_entry(test_tcache, &i, tmp_payload,
            &tmp_count);
        if(ret < 0)
        {
            PVFS_perror("\nPINT_tcache_insert", ret);
            return(-1);
        }
        printf("Done.\n");
        sleep(1);
    }

    /* lookup all three */
    for(i=0; i<3; i++)
    {
        printf("Looking up valid entry [%d]... ", i);
        ret = PINT_tcache_lookup(test_tcache, &i, &test_entry, &status);
        if(ret < 0)
        {
            PVFS_perror("\nPINT_tcache_lookup", ret);
            return(-1);
        }
        if(status != 0)
        {
            PVFS_perror("\nPINT_tcache_lookup status", status);
            return(-1);
        }
        tmp_payload = test_entry->payload;
        check_param("tcache value", tmp_payload->value, i);
        printf("Done.\n");
    }

    /* sleep until at least first expires */
    sleep(2);
    i=0;
    printf("Looking up expired entry [%d]... ", i);
    ret = PINT_tcache_lookup(test_tcache, &i, &test_entry, &status);
    if(ret < 0)
    {
        PVFS_perror("\nPINT_tcache_lookup", ret);
        return(-1);
    }
    if(status != -PVFS_ETIME) /* The status should be EXPIRED */
    {
        PVFS_perror("\nPINT_tcache_lookup status", status);
        return(-1);
    }
    tmp_payload = test_entry->payload;
    check_param("tcache value", tmp_payload->value, i);
    printf("Done.\n");

    i=2;
    printf("Looking up valid entry [%d]... ", i);
    ret = PINT_tcache_lookup(test_tcache, &i, &test_entry, &status);
    if(ret < 0)
    {
        PVFS_perror("\nPINT_tcache_lookup", ret);
        return(-1);
    }
    if(status != 0)
    {
        PVFS_perror("\nPINT_tcache_lookup status", status);
        return(-1);
    }
    tmp_payload = test_entry->payload;
    check_param("tcache value", tmp_payload->value, i);
    printf("Done.\n");

    /* try destroying an entry */
    printf("Destroying an entry...\n");
    ret = PINT_tcache_delete(test_tcache, test_entry);
    if(ret < 0)
    {
        PVFS_perror("PINT_tcache_delete", ret);
        return(-1);
    }
    printf("Done.\n");

    i=2;
    printf("Looking up destroyed entry [%d]... ", i);
    ret = PINT_tcache_lookup(test_tcache, &i, &test_entry, &status);
    if(ret != -PVFS_ENOENT)
    {
        PVFS_perror("\nPINT_tcache_lookup", ret);
        return(-1);
    }
    printf("Done.\n");

    i=200;
    printf("Looking up entry that never existed [%d]... ", i);
    ret = PINT_tcache_lookup(test_tcache, &i, &test_entry, &status);
    if(ret != -PVFS_ENOENT)
    {
        PVFS_perror("\nPINT_tcache_lookup", ret);
        return(-1);
    }
    printf("Done.\n");

    /* All entries will be expired after sleep*/
    printf("Sleeping 5 seconds to expire all entries... ");
    sleep(5);
    printf("Done.\n");

    /* insert a new entry */
    i=3;
    tmp_payload = (struct foo_payload*)malloc(sizeof(struct
        foo_payload));
    assert(tmp_payload);
    tmp_payload->key = i;
    tmp_payload->value = i;
    printf("Inserting [%d]... ", i);
    ret = PINT_tcache_insert_entry(test_tcache, &i, tmp_payload,
        &tmp_count);
    if(ret < 0)
    {
        PVFS_perror("PINT_tcache_insert", ret);
        return(-1);
    }
    printf("Done.\n");

    /* reclaim. There should be 3 entries... Soft limit is 5, reclaim percentage
     * is 50. We should reclaim (soft limit) * (reclaim percentage), so we should
     * reclaim 2 entries, which leaves us with 1 entry
     */
    printf("Issuing a reclaim...");
    ret = PINT_tcache_reclaim(test_tcache, &tmp_count);
    if(ret < 0)
    {
        PVFS_perror("PINT_tcache_reclaim", ret);
        return(-1);
    }
    ret = PINT_tcache_get_info(test_tcache, TCACHE_NUM_ENTRIES, &param);
    assert(ret == 0);
    check_param("TCACHE_NUM_ENTRIES", param, 1);
    printf("Done.\n");

    /* Check that reclaim removed the expected entries and that only one entry 
     * exists 
     */
    printf("Looking up valid entry [%d]... ", i);
    ret = PINT_tcache_lookup(test_tcache, &i, &test_entry, &status);
    if(ret < 0)
    {
        PVFS_perror("\nPINT_tcache_lookup", ret);
        return(-1);
    }
    if(status != 0)
    {
        PVFS_perror("\nPINT_tcache_lookup status", status);
        return(-1);
    }
    tmp_payload = test_entry->payload;
    check_param("tcache value", tmp_payload->value, i);
    printf("Done.\n");
    
    /* finalize */
    printf("Finalizing cache... ");
    PINT_tcache_finalize(test_tcache);
    printf("Done.\n");

    /* initialize */
    printf("Initializing cache... ");
    test_tcache = PINT_tcache_initialize(foo_compare_key_entry,
        foo_hash_key,
        foo_free_payload,
        -1);
    if(!test_tcache)
    {
        fprintf(stderr, "\nPINT_tcache_initialize failure.\n");
        return(-1);
    }
    printf("Done.\n");

    /* disable */
    printf("Disabling TCACHE...");
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

        printf("Inserting [%d]... ", i);
        ret = PINT_tcache_insert_entry(test_tcache, &i, tmp_payload,
            &tmp_count);
        if(ret < 0)
        {
            PVFS_perror("\nPINT_tcache_insert", ret);
            return(-1);
        }
        printf("Done.\n");
    }

    /* Try and lookup each entry, make sure it doesn't exist */
    for(i=0; i<3; i++)
    {
        printf("Looking up invalid entry [%d]... ", i);
        ret = PINT_tcache_lookup(test_tcache, &i, &test_entry, &status);
        if(ret != -PVFS_ENOENT)
        {
            PVFS_perror("\nPINT_tcache_lookup", ret);
            return(-1);
        }
        printf("Done.\n");
    }
    
    /* enable */
    printf("Enable TCACHE... ");
    param = 1;
    ret = PINT_tcache_set_info(test_tcache, TCACHE_ENABLE, param);
    assert(ret == 0);
    printf("Done.\n");

    /* set parameters */
    printf("Setting all TCACHE parameters... ");
    param = TEST_TIMEOUT_MSEC;
    ret = PINT_tcache_set_info(test_tcache, TCACHE_TIMEOUT_MSECS, param);
    assert(ret == 0);
    param = TEST_NUM_ENTRIES;
    ret = PINT_tcache_set_info(test_tcache, TCACHE_NUM_ENTRIES, param);
    assert(ret == -PVFS_EINVAL);
    param = TEST_HARD_LIMIT;
    ret = PINT_tcache_set_info(test_tcache, TCACHE_HARD_LIMIT, param);
    assert(ret == 0);
    param = TEST_SOFT_LIMIT;
    ret = PINT_tcache_set_info(test_tcache, TCACHE_SOFT_LIMIT, param);
    assert(ret == 0);
    param = TEST_ENABLE;
    ret = PINT_tcache_set_info(test_tcache, TCACHE_ENABLE, param);
    assert(ret == 0);
    param = TEST_RECLAIM_PERCENTAGE;
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

        printf("Inserting [%d]... ", i);
        ret = PINT_tcache_insert_entry(test_tcache, &i, tmp_payload,
            &tmp_count);
        if(ret < 0)
        {
            PVFS_perror("\nPINT_tcache_insert", ret);
            return(-1);
        }
        printf("Done.\n");
    }

    /* All entries should be expired after sleep */
    printf("Sleeping 5 seconds to expire all entries... ");
    sleep(5);
    printf("Done.\n");

    /* check num_entries */
    printf("Checking for 5 entries...");
    ret = PINT_tcache_get_info(test_tcache, TCACHE_NUM_ENTRIES, &param);
    assert(ret == 0);
    check_param("TCACHE_NUM_ENTRIES", param, 5);
    printf("Done.\n");

    /* insert some entries */
    for(i=5; i< 6; i++)
    {
        tmp_payload = (struct foo_payload*)malloc(sizeof(struct
            foo_payload));
        assert(tmp_payload);
        tmp_payload->key = i;
        tmp_payload->value = i;

        printf("Inserting [%d]... ", i);
        /* Soft limit is 5, insert of 6th entry should cause a  reclaim. 5 
         * expired entries exist. Reclaim of 2 entries should occur 
         */
        ret = PINT_tcache_insert_entry(test_tcache, &i, tmp_payload,
            &tmp_count);
        if(ret < 0)
        {
            PVFS_perror("\nPINT_tcache_insert", ret);
            return(-1);
        }
        printf("Done.\n");
    }

    /* check num_entries. Reclaim should have purged 2 entries */
    printf("Checking RECLAIM occurred during last insert. Should contain 4 entries... ");
    ret = PINT_tcache_get_info(test_tcache, TCACHE_NUM_ENTRIES, &param);
    assert(ret == 0);
    check_param("TCACHE_NUM_ENTRIES", param, 4);
    printf("Done.\n");

    /* insert some entries */
    for(i=6; i< 20; i++)
    {
        tmp_payload = (struct foo_payload*)malloc(sizeof(struct
            foo_payload));
        assert(tmp_payload);
        tmp_payload->key = i;
        tmp_payload->value = i;

        printf("Inserting [%d]... ", i);
        /* Inserting should cause reclaims to happen. 3 entries (3-5) are 
         * expired and should be purged. HARD_LIMIT is 10, so only the last
         * 10 entries should exist 
         */
        ret = PINT_tcache_insert_entry(test_tcache, &i, tmp_payload,
            &tmp_count);
        if(ret < 0)
        {
            PVFS_perror("\nPINT_tcache_insert", ret);
            return(-1);
        }
        printf("Done.\n");
    }

    /* check num_entries */
    ret = PINT_tcache_get_info(test_tcache, TCACHE_NUM_ENTRIES, &param);
    assert(ret == 0);
    check_param("TCACHE_NUM_ENTRIES", param, TEST_HARD_LIMIT);

    /* Check to make sure the first 10 entries do NOT exist */
    /* Try and lookup each entry, make sure it doesn't exist */
    for(i=0; i<10; i++)
    {
        printf("Looking up invalid entry [%d]... ", i);
        ret = PINT_tcache_lookup(test_tcache, &i, &test_entry, &status);
        if(ret != -PVFS_ENOENT)
        {
            PVFS_perror("\nPINT_tcache_lookup", ret);
            return(-1);
        }
        printf("Done.\n");
    }

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

static void check_param(char *parameter_name, int param, int expected_value)
{
    if(param != expected_value)
    {
        fprintf(stderr, "\n%s does not match expected result\n"
                        "\t%s = %d\n"
                        "\texpected value = %d\n",
                        parameter_name,
                        parameter_name,
                        param,
                        expected_value);
    }
}
/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

