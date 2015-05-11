/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <getopt.h>

#include "bmi.h"
#include "pvfs2.h"
#include "pvfs2-mgmt.h"
#include "pvfs2-internal.h"

#define HISTORY 1
#define CMD_BUF_SIZE 256

/* these defines should match the defines in include/pvfs2-mgmt.h */
#define OID_READ ".1.3.6.1.4.1.7778.0"
#define OID_WRITE ".1.3.6.1.4.1.7778.1"
#define OID_MREAD ".1.3.6.1.4.1.7778.2"
#define OID_MWRITE ".1.3.6.1.4.1.7778.3"
#define OID_DSPACE ".1.3.6.1.4.1.7778.4"
#define OID_KEYVAL ".1.3.6.1.4.1.7778.5"
#define OID_REQSCHED ".1.3.6.1.4.1.7778.6"
#define OID_REQUESTS ".1.3.6.1.4.1.7778.7"
#define OID_SMALL_READ ".1.3.6.1.4.1.7778.8"
#define OID_SMALL_WRITE ".1.3.6.1.4.1.7778.9"
#define OID_FLOW_READ ".1.3.6.1.4.1.7778.10"
#define OID_FLOW_WRITE ".1.3.6.1.4.1.7778.11"
#define OID_REQ_CREATE ".1.3.6.1.4.1.7778.12"
#define OID_REQ_REMOVE ".1.3.6.1.4.1.7778.13"
#define OID_REQ_MKDIR ".1.3.6.1.4.1.7778.14"
#define OID_REQ_RMDIR ".1.3.6.1.4.1.7778.15"
#define OID_REQ_GETATTR ".1.3.6.1.4.1.7778.16"
#define OID_REQ_SETATTR ".1.3.6.1.4.1.7778.17"

#define INT_TYPE "INTEGER"
#define CNT_TYPE "COUNTER"

#ifndef PVFS2_VERSION
#define PVFS2_VERSION "Unknown"
#endif

struct MGMT_perf_iod
{
    const char *key_oid;
    const char *key_type;
    int key_number;
    const char *key_name;
};

/* this table needs to match the list of keys in pvfs2-mgmt.h */
static struct MGMT_perf_iod key_table[] = 
{
   {OID_READ, CNT_TYPE, PINT_PERF_READ, "Bytes Read"},
   {OID_WRITE, CNT_TYPE, PINT_PERF_WRITE, "Bytes Written"},
   {OID_MREAD, CNT_TYPE, PINT_PERF_METADATA_READ, "Metadata Read Ops"},
   {OID_MWRITE, CNT_TYPE, PINT_PERF_METADATA_WRITE, "Metadata Write Ops"},
   {OID_DSPACE, CNT_TYPE, PINT_PERF_METADATA_DSPACE_OPS, "Metadata DSPACE Ops"},
   {OID_KEYVAL, CNT_TYPE, PINT_PERF_METADATA_KEYVAL_OPS, "Metadata KEYVAL Ops"},
   {OID_REQSCHED, INT_TYPE, PINT_PERF_REQSCHED, "Requests Active"},
   {OID_REQUESTS, CNT_TYPE, PINT_PERF_REQUESTS, "Requests Received"},
   {OID_SMALL_READ, CNT_TYPE, PINT_PERF_SMALL_READ, "Bytes Read by Small_IO"},
   {OID_SMALL_WRITE, CNT_TYPE, PINT_PERF_SMALL_WRITE, "Bytes Written by Small_IO"},
   {OID_FLOW_READ, CNT_TYPE, PINT_PERF_FLOW_READ, "Bytes Read by Flow"},
   {OID_FLOW_WRITE, CNT_TYPE, PINT_PERF_FLOW_WRITE, "Bytes Written by Flow"},
   {OID_REQ_CREATE, CNT_TYPE, PINT_PERF_CREATE, "create requests called"},
   {OID_REQ_REMOVE, CNT_TYPE, PINT_PERF_REMOVE, "remove requests called"},
   {OID_REQ_MKDIR, CNT_TYPE, PINT_PERF_MKDIR, "mkdir requests called"},
   {OID_REQ_RMDIR, CNT_TYPE, PINT_PERF_RMDIR, "rmdir requests called"},
   {OID_REQ_GETATTR, CNT_TYPE, PINT_PERF_GETATTR, "getattr requests called"},
   {OID_REQ_SETATTR, CNT_TYPE, PINT_PERF_SETATTR, "setattr requests called"},
   {NULL, NULL, -1, NULL}   /* this halts the key count */
};

struct options
{
    char* mnt_point;
    int mnt_point_set;
    char* server_addr;
    int server_addr_set;
};

static struct options* parse_args(int argc, char* argv[]);
static void usage(int argc, char** argv);

int main(int argc, char **argv)
{
    int ret = -1;
    char *retc = NULL;
    PVFS_fs_id cur_fs;
    struct options* user_opts = NULL;
    char pvfs_path[PVFS_NAME_MAX] = {0};
    int i, k;
    PVFS_credential creds;
    int io_server_count;
    int64_t **perf_matrix;
    uint64_t* end_time_ms_array;
    uint32_t* next_id_array;
    PVFS_BMI_addr_t *addr_array, server_addr;
    char *cmd_buffer = (char *)malloc(CMD_BUF_SIZE);
    int max_keys, key_count;
    time_t snaptime = 0;

    /* look at command line arguments */
    user_opts = parse_args(argc, argv);
    if (!user_opts)
    {
        fprintf(stderr, "Error: failed to parse command line arguments.\n");
        usage(argc, argv);
        return(-1);
    }

    ret = PVFS_util_init_defaults();
    if (ret < 0)
    {
        PVFS_perror("PVFS_util_init_defaults", ret);
        return(-1);
    }

    ret = PVFS_util_gen_credential_defaults(&creds);
    if (ret < 0)
    {
        PVFS_perror("PVFS_util_gen_credential_defaults", ret);
        return(-1);
    }

    if (user_opts->server_addr_set)
    {
        if (PVFS_util_get_default_fsid(&cur_fs) < 0)
        {
            /* Can't find a file system */
            fprintf(stderr, "Error: failed to find a file system.\n");
            usage(argc, argv);
            return(-1);
        }
        if (user_opts->server_addr &&
                (BMI_addr_lookup (&server_addr, user_opts->server_addr) == 0))
        {
            /* set up single server */
            addr_array = (PVFS_BMI_addr_t *)malloc(sizeof(PVFS_BMI_addr_t));
            addr_array[0] = server_addr;
            io_server_count = 1;
        }
        else
        {
            /* bad argument - address not found */
            fprintf(stderr, "Error: failed to parse server address.\n");
            usage(argc, argv);
            return(-1);
        }
    }
    else
    {
        /* will sample all servers */
        /* translate local path into pvfs2 relative path */
        ret = PVFS_util_resolve(user_opts->mnt_point,
                                &cur_fs, pvfs_path, PVFS_NAME_MAX);
        if (ret < 0)
        {
            PVFS_perror("PVFS_util_resolve", ret);
            return(-1);
        }

        /* count how many I/O servers we have */
        ret = PVFS_mgmt_count_servers(cur_fs,
                                      PVFS_MGMT_IO_SERVER,
                                      &io_server_count);
        if (ret < 0)
        {
            PVFS_perror("PVFS_mgmt_count_servers", ret);
	        return(-1);
        }
    
        /* build a list of servers to talk to */
        addr_array = (PVFS_BMI_addr_t *)
	    malloc(io_server_count * sizeof(PVFS_BMI_addr_t));
        if (addr_array == NULL)
        {
	        perror("malloc");
	        return -1;
        }
        ret = PVFS_mgmt_get_server_array(cur_fs,
                                         PVFS_MGMT_IO_SERVER,
				                         addr_array,
				                         &io_server_count);
        if (ret < 0)
        {
	        PVFS_perror("PVFS_mgmt_get_server_array", ret);
	        return -1;
        }
    }

    /* count keys */
    for (max_keys = 0; key_table[max_keys].key_number >= 0; max_keys++);

    /* allocate a 2 dimensional array for statistics */
    perf_matrix = (int64_t **)malloc(io_server_count * sizeof(int64_t *));
    if (!perf_matrix)
    {
        perror("malloc");
        return(-1);
    }
    for(i = 0; i < io_server_count; i++)
    {
	    perf_matrix[i] = (int64_t *)malloc(HISTORY * (max_keys + 2) *
                                           sizeof(int64_t));
	    if (perf_matrix[i] == NULL)
	    {
	        perror("malloc");
	        return -1;
	    }
    }

    /* allocate an array to keep up with what iteration of statistics
     * we need from each server 
     */
    next_id_array = (uint32_t *) malloc(io_server_count * sizeof(uint32_t));
    if (next_id_array == NULL)
    {
	     perror("malloc");
	     return -1;
    }
    memset(next_id_array, 0, io_server_count * sizeof(uint32_t));

    /* allocate an array to keep up with end times from each server */
    end_time_ms_array = (uint64_t *)malloc(io_server_count * sizeof(uint64_t));
    if (end_time_ms_array == NULL)
    {
	    perror("malloc");
	    return -1;
    }
    memset(end_time_ms_array, 0, io_server_count * sizeof(uint64_t));


    /* loop for ever, grabbing stats when requested */
    while (1)
    {
        int srv = 0;
        const char *returnType = NULL; 
        int64_t returnValue = 0;
        /* wait for a request from SNMP driver */
        retc = fgets(cmd_buffer, CMD_BUF_SIZE, stdin);
        if (!retc)
        {
            /* error on read */
            return -1;
        }

        /* if PING output PONG */
        if (!strncasecmp(cmd_buffer, "PING", 4))
        {
            fprintf(stdout,"PONG\n");
	        fflush(stdout);
            continue;
        }

        /* try to parse GET command */
        if (!strncasecmp(cmd_buffer, "GET", 3))
        {
            char *c;
            /* found GET read OID */
            retc = fgets(cmd_buffer, CMD_BUF_SIZE, stdin);
            if (!retc)
            {
                /* error on read */
                return -1;
            }
            /* replace newlines with null char */
            for(c = cmd_buffer; *c != '\0'; c++)
            {
                if (*c == '\n')
                {
                    *c = '\0';
                }
            }
            /* this is a valid measurement */
            for(k = 0;
                k < max_keys && strcmp(cmd_buffer, key_table[k].key_oid);
                k++);
            /* out of for loop k equals selected key */
            if (k < max_keys)
            {
                returnType = key_table[k].key_type;
            }
            else
            {
                /* invalid command */
                fprintf(stdout,"NONE\n");
                fflush(stdout);
                continue;
            }
        }
        else
        {
            /* bad command */
            fprintf(stdout, "NONE\n");
            fflush(stdout);
            continue;
        }

        /* good command - read counters */
        if (time(NULL) - snaptime > 60)
        {
            snaptime = time(NULL);
            key_count = max_keys;
	        ret = PVFS_mgmt_perf_mon_list(cur_fs,
				                          &creds,
				                          perf_matrix, 
				                          end_time_ms_array,
				                          addr_array,
				                          next_id_array,
				                          io_server_count, 
                                          &key_count,
				                          HISTORY,
				                          NULL,
                                          NULL);
	        if (ret < 0)
	        {
	            PVFS_perror("PVFS_mgmt_perf_mon_list", ret);
	            return -1;
	        }
            returnValue = perf_matrix[srv][key_table[k].key_number];
        }

        fprintf(stdout, "%s\n%llu\n", returnType, llu(returnValue));
        fflush(stdout);
        /* return to top for next command */
    }

    PVFS_sys_finalize();

    return(ret);
}

/* parse_args()
 *
 * parses command line arguments
 *
 * returns pointer to options structure on success, NULL on failure
 */
static struct options* parse_args(int argc, char* argv[])
{
    char flags[] = "vm:s:";
    int one_opt = 0;
    int len = 0;

    struct options *tmp_opts = NULL;
    int ret = -1;

    /* create storage for the command line options */
    tmp_opts = (struct options *) malloc(sizeof(struct options));
    if(tmp_opts == NULL)
    {
	    return(NULL);
    }
    memset(tmp_opts, 0, sizeof(struct options));

    /* look at command line arguments */
    while((one_opt = getopt(argc, argv, flags)) != EOF)
    {
	    switch(one_opt)
        {
            case('v'):
                printf("%s\n", PVFS2_VERSION);
                exit(0);
	        case('m'):
                /* we need to add a '/' to the end so cannot strdup */
		        len = strlen(optarg)+1;
		        tmp_opts->mnt_point = (char*)malloc(len+1);
		        if(!tmp_opts->mnt_point)
		        {
		            free(tmp_opts);
		            return(NULL);
		        }
		        memset(tmp_opts->mnt_point, 0, len+1);
		        ret = sscanf(optarg, "%s", tmp_opts->mnt_point);
		        if(ret < 1)
                {
		            free(tmp_opts);
		            return(NULL);
		        }
		        /* TODO: dirty hack... fix later.  The remove_dir_prefix()
		        * function expects some trailing segments or at least
		        * a slash off of the mount point
		        */
		        strcat(tmp_opts->mnt_point, "/");
		        tmp_opts->mnt_point_set = 1;
		        break;
	        case('s'):
                tmp_opts->server_addr = strdup(optarg);
                if (!tmp_opts->server_addr)
                {
                    free(tmp_opts);
                    return NULL;
                }
		        tmp_opts->server_addr_set = 1;
		        break;
	        case('?'):
		        usage(argc, argv);
		        exit(EXIT_FAILURE);
	    }
    }

    if (!(tmp_opts->mnt_point_set || tmp_opts->server_addr_set))
    {
	    free(tmp_opts);
	    return(NULL);
    }

    return(tmp_opts);
}


static void usage(int argc, char **argv)
{
    fprintf(stderr, "\n");
    fprintf(stderr, "Usage  : %s [-m fs_mount_point]\n", argv[0]);
    fprintf(stderr, "Example: %s -m /mnt/pvfs2\n", argv[0]);
    fprintf(stderr, "Usage  : %s [-s bmi_address_string]\n", argv[0]);
    fprintf(stderr, "Example: %s -s tcp://localhost:3334\n", argv[0]);
    fprintf(stderr, "OID_READ .1.3.6.1.4.1.7778.0\n");
    fprintf(stderr, "OID_WRITE .1.3.6.1.4.1.7778.1\n");
    fprintf(stderr, "OID_MREAD .1.3.6.1.4.1.7778.2\n");
    fprintf(stderr, "OID_MWRITE .1.3.6.1.4.1.7778.3\n");
    fprintf(stderr, "OID_DSPACE .1.3.6.1.4.1.7778.4\n");
    fprintf(stderr, "OID_KEYVAL .1.3.6.1.4.1.7778.5\n");
    fprintf(stderr, "OID_REQSCHED .1.3.6.1.4.1.7778.6\n");
    fprintf(stderr, "OID_REQUESTS .1.3.6.1.4.1.7778.7\n");
    fprintf(stderr, "OID_SMALL_READ .1.3.6.1.4.1.7778.8\n");
    fprintf(stderr, "OID_SMALL_WRITE .1.3.6.1.4.1.7778.9\n");
    fprintf(stderr, "OID_FLOW_READ .1.3.6.1.4.1.7778.10\n");
    fprintf(stderr, "OID_FLOW_WRITE .1.3.6.1.4.1.7778.11\n");
    fprintf(stderr, "OID_REQ_CREATE .1.3.6.1.4.1.7778.12\n");
    fprintf(stderr, "OID_REQ_REMOVE .1.3.6.1.4.1.7778.13\n");
    fprintf(stderr, "OID_REQ_MKDIR .1.3.6.1.4.1.7778.14\n");
    fprintf(stderr, "OID_REQ_RMDIR .1.3.6.1.4.1.7778.15\n");
    fprintf(stderr, "OID_REQ_GETATTR .1.3.6.1.4.1.7778.16\n");
    fprintf(stderr, "OID_REQ_SETATTR .1.3.6.1.4.1.7778.17\n");
    return;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=4 sts=4 sw=4 expandtab
 */
