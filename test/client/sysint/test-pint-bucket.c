/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>

#include "pvfs2-sysint.h"
#include "pint-sysint-utils.h"
#include "dotconf.h"
#include "trove.h"
#include "server-config.h"
#include "PINT-reqproto-encode.h"
#include "pint-bucket.h"
#include "pvfs2-util.h"

#define MAX_NUM_FS                   67

/* determines how many times to call '_get_next_meta' */
#define NUM_META_SERVERS_TO_QUERY     3

/* determines how many i/o servers to request from '_get_next_io' */
#define NUM_DATA_SERVERS_TO_QUERY     3

/*
  determines which handles to test mappings on.  the handles
  placed below MUST be within a valid range of a filesystem
  in the server's fs.conf for this test to pass.
*/
#define NUM_TEST_HANDLES              5
static PVFS_handle test_handles[NUM_TEST_HANDLES] =
{
    1048494,
    1047881,
    1047871,
    1048531,
    1048525
};

extern job_context_id PVFS_sys_job_context;

/* this is a test program that exercises the bucket interface and
 * demonstrates how to use it.
 */
int main(int argc, char **argv)	
{
    int i = 0, j = 0, k = 0, n = 0, num_file_systems = 0;
    pvfs_mntlist mnt = {0,NULL};
    struct server_configuration_s server_config;
    PINT_llist *cur = NULL;
    struct filesystem_configuration_s *cur_fs = NULL;
    int fs_ids[MAX_NUM_FS] = {0};
    int num_meta_servers = 0, num_data_servers = 0;
    bmi_addr_t addr, m_addr, d_addr[NUM_DATA_SERVERS_TO_QUERY];
    char server_name[PVFS_MAX_SERVER_ADDR_LEN] = {0};
    int test_handles_verified[NUM_TEST_HANDLES] = {0};
    PVFS_handle_extent_array meta_handle_extent_array;
    PVFS_handle_extent_array data_handle_extent_array[NUM_DATA_SERVERS_TO_QUERY];

    if (PVFS_util_parse_pvfstab(&mnt))
    {
        fprintf(stderr, "PVFS_util_parse_pvfstab failure.\n");
        return(-1);
    }

    if (BMI_initialize("bmi_tcp",NULL,0))
    {
        fprintf(stderr, "BMI_initialize failure.\n");
        return(-1);
    }

    if (PINT_encode_initialize())
    {
        fprintf(stderr, "PINT_encoded_initialize failure.\n");
        return(-1);
    }

    if (job_initialize(0))
    {
        fprintf(stderr, "job_initialize failure.\n");
        return(-1);
    }

    if (job_open_context(&PVFS_sys_job_context))
    {
        fprintf(stderr, "job_open_context failure.\n");
        return(-1);
    }

    if (PINT_bucket_initialize())
    {
        fprintf(stderr, "PINT_bucket_initialize() failure.\n");
        return(-1);
    }

    memset(&server_config,0,sizeof(struct server_configuration_s));
    if (PINT_server_get_config(&server_config, mnt))
    {
        fprintf(stderr, "PINT_server_get_config failure.\n");
        return(-1);
    }

    cur = server_config.file_systems;
    while(cur)
    {
        cur_fs = PINT_llist_head(cur);
        if (!cur_fs)
        {
            break;
        }
        printf("Loading mappings of filesystem %s\n",
               cur_fs->file_system_name);
        if (PINT_handle_load_mapping(&server_config,cur_fs))
        {
            fprintf(stderr, "PINT_handle_load_mapping failure.\n");
            return(-1);
        }
        fs_ids[i++] = (int)cur_fs->coll_id;
        cur = PINT_llist_next(cur);
    }

    /* run all pint-bucket tests for each filesystem we know about */
    num_file_systems = PINT_llist_count(server_config.file_systems);
    for(i = 0; i < num_file_systems; i++)
    {
        printf("\nOUTPUT OF TEST (filesystem ID is %d):\n",fs_ids[i]);
        printf("***************************************\n");

        if (PINT_bucket_get_num_meta(fs_ids[i],&num_meta_servers))
        {
            fprintf(stderr, "PINT_bucket_get_num_meta failure.\n");
            return(-1);
        }
        else
        {
            printf("\nNumber of meta servers available: %d\n",
                   num_meta_servers);
        }

        if (PINT_bucket_get_num_io(fs_ids[i],&num_data_servers))
        {
            fprintf(stderr, "PINT_bucket_get_num_io failure.\n");
            return(-1);
        }
        else
        {
            printf("Number of I/O servers available: %d\n",
                   num_data_servers);
        }

        printf("\n");
        for(j = 0; j < NUM_META_SERVERS_TO_QUERY; j++)
        {
            if (PINT_bucket_get_next_meta(&server_config,
                                          fs_ids[i],
                                          &m_addr,
                                          &meta_handle_extent_array))
            {
                fprintf(stderr, "PINT_bucket_get_next_meta failure.\n");
                return(-1);
            }
            else
            {
                printf("\nNext meta server address  : %lu (%d meta ranges)\n",
                       (long)m_addr, meta_handle_extent_array.extent_count);
                for(n = 0; n < meta_handle_extent_array.extent_count; n++)
                {
                    printf("Meta server %d handle range: %Lu-%Lu\n", j,
                           meta_handle_extent_array.extent_array[n].first,
                           meta_handle_extent_array.extent_array[n].last);
                }
            }
        }

        if (PINT_bucket_get_next_io(&server_config, fs_ids[i],
                                    NUM_DATA_SERVERS_TO_QUERY,
                                    d_addr, data_handle_extent_array))
        {
            fprintf(stderr, "PINT_bucket_get_next_io failure.\n");
            return(-1);
        }
        else
        {
            printf("\nAsked for %d I/O servers and got the following:\n",
                   NUM_DATA_SERVERS_TO_QUERY);
            for(j = 0; j < NUM_DATA_SERVERS_TO_QUERY; j++)
            {
                printf("\nI/O server  %d address     : %lu (%d data ranges)\n",
                       j,(long)d_addr[j],
                       data_handle_extent_array[j].extent_count);
                for(n = 0; n < data_handle_extent_array[j].extent_count; n++)
                {
                    printf("Data server %d handle range: %Lu-%Lu\n", n,
                           data_handle_extent_array[j].extent_array[n].first,
                           data_handle_extent_array[j].extent_array[n].last);
                }
            }
        }

        printf("\n");
        for(j = 0; j < NUM_TEST_HANDLES; j++)
        {
            if (PINT_bucket_get_server_name(server_name,PVFS_MAX_SERVER_ADDR_LEN,
                                            test_handles[j],fs_ids[i]))
            {
                printf("Error retrieving name of server managing handle "
                       "%Ld!\n",test_handles[j]);
                printf("** This may be okay if the handle (%Ld) exists "
                       "on a different fs (not %d)\n",
                       test_handles[j],fs_ids[i]);
                continue;
            }
            else
            {
                printf("Retrieved name of server managing handle "
                       "%Ld is %s\n",test_handles[j],server_name);
                test_handles_verified[j]++;
            }
        }

        printf("\n");
        for(j = 0; j < NUM_TEST_HANDLES; j++)
        {
            if (PINT_bucket_map_to_server(&addr,test_handles[j],fs_ids[i]))
            {
                fprintf(stderr, "PINT_bucket_map_to_server failure.\n");
                printf("** This may be okay if the handle (%Ld) exists "
                       "on a different fs (not %d)\n",
                       test_handles[j],fs_ids[i]);
                continue;
            }
            else
            {
                /*
                  make sure the returned address is either a known
                  meta or data server address
                */
                for(k = 0; k < NUM_TEST_HANDLES; k++)
                {
                    if (d_addr[k] == addr)
                    {
                        break;
                    }
                }
                if ((k == NUM_TEST_HANDLES) && (m_addr != addr))
                {
                    printf("*** Failed to verify ability to map servers "
                           "to handles.\n");
                    return(-1);
                }
                else
                {
                    printf("Retrieved address of server managing handle "
                           "%Ld is %lu\n",test_handles[j],(long)addr);
                    test_handles_verified[j]++;
                }
            }
        }

        printf("\n");
        for(j = 0; j < NUM_TEST_HANDLES; j++)
        {
            if (test_handles_verified[j] != 2)
            {
                break;
            }
            test_handles_verified[j] = 0;
        }
        if (j == NUM_TEST_HANDLES)
        {
            printf("Successfully verified ability to map servers to handles.\n");
        }
        else
        {
            printf("** Failed to verify ability to map servers to handles.\n");
            printf("** Handle value %Ld failed -- cannot be mapped.\n",
                   (j ? test_handles[j-1] : test_handles[j]));
            return -1;
        }
    }

    /* Nasty basic block trick, but hey- its just a test program, right? */
    {
	int incount = 0;
	int outcount = 0;
	struct PINT_bucket_server_info* info_array = NULL;
	int ret = -1;
	int num_io = 0;
	int num_meta = 0;
	
	printf("\n");

	ret = PINT_bucket_get_num_io(fs_ids[0], &num_io);
	if(ret < 0)
	{
	    fprintf(stderr, "PINT_bucket_get_num_io() failure.\n");
	    return(-1);
	}
	ret = PINT_bucket_get_num_meta(fs_ids[0], &num_meta);
	if(ret < 0)
	{
	    fprintf(stderr, "PINT_bucket_get_num_meta() failure.\n");
	    return(-1);
	}
	incount = num_meta + num_io;
	info_array = (struct PINT_bucket_server_info*)malloc(incount*
	    sizeof(struct PINT_bucket_server_info));
	if(!info_array)
	{
	    perror("malloc");
	    return(-1);
	}

	ret = PINT_bucket_get_physical_io(&server_config, fs_ids[0],
	    incount, &outcount, info_array);
	if(ret < 0)
	{
	    fprintf(stderr, "PINT_bucket_get_physical_io() failure.\n");
	    return(-1);
	}
	printf("PINT_bucket_get_physical_io() found %d servers.\n", outcount);
	for(j = 0; j < outcount; j++)
	{
	    printf("I/O server %d addr: %lu\n",j,(long)info_array[j].addr);
	}

	ret = PINT_bucket_get_physical_meta(&server_config, fs_ids[0],
	    incount, &outcount, info_array);
	if(ret < 0)
	{
	    fprintf(stderr, "PINT_bucket_get_physical_meta() failure.\n");
	    return(-1);
	}
	printf("PINT_bucket_get_physical_meta() found %d servers.\n", outcount);
	for(j = 0; j < outcount; j++)
	{
	    printf("meta server %d addr: %lu\n",j,(long)info_array[j].addr);
	}

	ret = PINT_bucket_get_physical_all(&server_config, fs_ids[0],
	    incount, &outcount, info_array);
	if(ret < 0)
	{
	    fprintf(stderr, "PINT_bucket_get_physical_all() failure.\n");
	    return(-1);
	}
	printf("PINT_bucket_get_physical_all() found %d servers.\n", outcount);
	for(j = 0; j < outcount; j++)
	{
	    printf("server %d addr: %lu\n",j,(long)info_array[j].addr);
	}

    }

    if (PINT_bucket_finalize())
    {
        fprintf(stderr, "PINT_bucket_finalize() failure.\n");
        return(-1);
    }

    job_finalize();
    BMI_finalize();
    return(0);
}
