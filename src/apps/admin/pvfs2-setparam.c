#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "pvfs2.h"
#include "pvfs2-mgmt.h"
#include "bmi.h"
#include "pint-uid-mgmt.h"
#include "pint-util.h"
#include "pint-cached-config.h"

#define UID_HISTORY_MAX_SECS 4294967295UL /* max uint32_t val */
#define UID_SERV_LIST_SIZE 64            /* maximum servers to get stats from */

struct options
{
    uint32_t history;
    uint64_t local_history_size;
    uint64_t local_interval;
    uint64_t local_key_count;
    char **server_list;
    int server_count;
    PVFS_fs_id fs_id;
};

static struct options *parse_args(int argc, char *argv[]);
static void usage(int argc, char *argv[]);
static void cleanup(struct options *ptr, PVFS_BMI_addr_t *addr_array);

int main(int argc, char *argv[])
{
    PVFS_credential creds;
    PVFS_fs_id cur_fs;
    PVFS_BMI_addr_t *addr_array, server_addr;
    struct PVFS_mgmt_setparam_value *hist_test = (struct PVFS_mgmt_setparam_value*)malloc(sizeof(struct PVFS_mgmt_setparam_value));
    struct PVFS_mgmt_setparam_value *interval_test = (struct PVFS_mgmt_setparam_value*)malloc(sizeof(struct PVFS_mgmt_setparam_value));
    struct PVFS_mgmt_setparam_value *count_test = (struct PVFS_mgmt_setparam_value*)malloc(sizeof(struct PVFS_mgmt_setparam_value));
    PVFS_error_details setparam_details;
    struct options *prog_opts = NULL;
    int ret = 0;
    int i;

    /* parse command line arguments */ 
    prog_opts = parse_args(argc, argv);
    if (!prog_opts)
    {
        fprintf(stderr, "Unable to allocate memory for command line args\n");
        exit(EXIT_FAILURE);
    }

    /*assigning test values to counters*/
    if(prog_opts->local_history_size)
    	hist_test->u.value = prog_opts->local_history_size; 
    else
    	hist_test->u.value = 6; 

    if(prog_opts->local_interval)
    	interval_test->u.value = prog_opts->local_interval; 
    else
    	interval_test->u.value = 1000;
 
    if(prog_opts->local_key_count)
    	count_test->u.value = prog_opts->local_key_count;  //setting key count 
    else
    	count_test->u.value = 20;  //setting key count 


    if (!(prog_opts->history))
    {
        prog_opts->history = UID_HISTORY_MAX_SECS;
    }

    ret = PVFS_util_init_defaults();
    if (ret < 0)
    {
        PVFS_perror("PVFS_util_init_defaults", ret);
        return (-1);
    }

    PVFS_util_gen_credential_defaults(&creds);

    
    /* get a default fsid or use the one given by the user*/
    if (prog_opts->fs_id == -1)
    {
        ret = PVFS_util_get_default_fsid(&cur_fs);
        if (ret < 0)
        {
            PVFS_perror("PVFS_util_get_default_fsid", ret);
            return (-1);
        }
    }
    else
    {
        cur_fs = prog_opts->fs_id;
    }

    /*if user specifies servers, allocate memory for the BMI addrs and
    then translate the server strings to BMI addrs*/
    if (prog_opts->server_count)
    {
        /*allocate memory for our BMI addresses and fill them in*/
        addr_array = (PVFS_BMI_addr_t *)malloc(prog_opts->server_count *
                                           sizeof(PVFS_BMI_addr_t));
        if (!addr_array)
        {
            fprintf(stderr, "Unable to allocate memory for BMI addrs\n");
            exit(EXIT_FAILURE);
        }

        for (i = 0; i < prog_opts->server_count; i ++)
        {
            ret = BMI_addr_lookup(&server_addr, prog_opts->server_list[i]);
            if (ret < 0)
            {
                PVFS_perror("BMI_addr_lookup", ret);
                return (-1);
            }
            addr_array[i] = server_addr;
        }
    }
    else
    {
        /*else, user specified no servers, so a list will be built*/
        ret = PVFS_mgmt_count_servers(cur_fs, PINT_SERVER_TYPE_ALL,
                                      &(prog_opts->server_count));
        if (ret < 0)
        {
            PVFS_perror("PVFS_mgmt_count_servers", ret);
            return (-1);
        }

        /*allocate memory for the number of BMI addrs found*/
        addr_array = (PVFS_BMI_addr_t *)malloc(prog_opts->server_count *
                                           sizeof(PVFS_BMI_addr_t));
        if (!addr_array)
        {
            fprintf(stderr, "Unable to allocate memory for BMI addrs\n");
            exit(EXIT_FAILURE);
        }

        /*retrieve the list of BMI addrs for the list of servers*/
        ret = PVFS_mgmt_get_server_array(cur_fs, PINT_SERVER_TYPE_ALL,
					addr_array,
					&(prog_opts->server_count));
        if (ret < 0)
        {
            PVFS_perror("PVFS_mgmt_get_server_array", ret);
            return (-1);
        }

        /*use reverse lookups so the server URI's can be displayed to the user */
        for (i = 0; i < prog_opts->server_count; i++)
        {
            prog_opts->server_list[i] = strdup(BMI_addr_rev_lookup(addr_array[i]));
        }
    }

    /* retrieve the parameters from the servers, checking for any errors */
    ret = PVFS_mgmt_setparam_list(cur_fs,
			     &creds,
			     PVFS_SERV_PARAM_SET_HISTORY, 
			     hist_test,
                             addr_array,
                             prog_opts->server_count,
			     &setparam_details,
			     NULL);		//assigning history_size
    if (ret < 0)
    {
        PVFS_perror("PVFS_mgmt_setparam_list", ret);
        return (-1);
    }
    else
    {
        printf("History value has been set in server to:%d\n", (int)hist_test->u.value);
    }
    ret = PVFS_mgmt_setparam_list(cur_fs,
			     &creds,
			     PVFS_SERV_PARAM_SET_INTERVAL, 
			     interval_test,
                             addr_array,
                             prog_opts->server_count,
			     &setparam_details,
			     NULL);		//controlling key count
    if (ret < 0)
    {
        PVFS_perror("PVFS_mgmt_setparam_list", ret);
        return (-1);
    }
    else
    {
        printf("Interval value has been set in server to:%d\n", (int)interval_test->u.value);
    }
    ret = PVFS_mgmt_setparam_list(cur_fs,
			     &creds,
			     PVFS_SERV_PARAM_SET_KEY_COUNT, 
			     count_test,
                             addr_array,
                             prog_opts->server_count,
			     &setparam_details,
			     NULL);		//assigning interval
    if (ret < 0)
    {
        PVFS_perror("PVFS_mgmt_setparam_list", ret);
        return (-1);
    }
    else
    {
        printf("Count value has been set in server to:%d\n", (int)count_test->u.value);
    }
    printf("\nFSID: %d\n", cur_fs);

    /* memory cleanup */
    cleanup(prog_opts, addr_array);

    return 0;
}

/* parse_args()
 *
 * parses command line arguments and returns pointer to program options
 */
static struct options *parse_args(int argc, char *argv[])
{
    char flags[] = "s:h:f:k:o:i:t";
    int one_opt = 0;
    struct options *tmp_opts = NULL;
    int server_cnt = 0;
    int i;

    /* allocate memory for the program options */
    tmp_opts = (struct options *)malloc(sizeof(struct options));
    if (!tmp_opts)
    {
        return NULL;
    }
    memset(tmp_opts, 0, sizeof(struct options));

    /* allocate memory for storing pointers to server addrs */
    tmp_opts->server_list = (char **)malloc(UID_SERV_LIST_SIZE * sizeof(char *));
    for (i = 0; i < UID_SERV_LIST_SIZE; i++)
    {
        tmp_opts->server_list[i] = NULL;
    }

    tmp_opts->fs_id = -1;

    /* parse args using getopt() */
    while((one_opt = getopt(argc, argv, flags)) != EOF)
    {
        switch(one_opt)
        {
            case('s'):
                if (server_cnt == UID_SERV_LIST_SIZE)
                {
                    fprintf(stderr, "Server limit exceded, using first %d servers\n",                                   UID_SERV_LIST_SIZE);
                    break;
                }
                if (server_cnt > UID_SERV_LIST_SIZE)
                {
                    break;
                }
                tmp_opts->server_list[server_cnt] = strdup(optarg);
                server_cnt++;
                break;
            case('t'):
                tmp_opts->history = atoi(optarg);
                if (tmp_opts->history < 1)
                {
                    usage(argc, argv);
                    exit(EXIT_FAILURE);
                }
                break;
            case('o'):
                tmp_opts->local_history_size = atoi(optarg);
                if (tmp_opts->local_history_size < 1)
                {
                    usage(argc, argv);
                    exit(EXIT_FAILURE);
                }
                break;
            case('i'):
                tmp_opts->local_interval = atoi(optarg);
                if (tmp_opts->local_interval < 1)
                {
                    usage(argc, argv);
                    exit(EXIT_FAILURE);
                }
                break;
            case('k'):
                tmp_opts->local_key_count = atoi(optarg);
                if (tmp_opts->local_key_count < 1)
                {
                    usage(argc, argv);
                    exit(EXIT_FAILURE);
                }
                break;
            case('f'):
                tmp_opts->fs_id = atoi(optarg);
                if (tmp_opts->fs_id < 0)
                {
                    usage(argc, argv);
                    exit(EXIT_FAILURE);
                }
                break;
            case('h'):
                usage(argc, argv);
                exit(EXIT_SUCCESS);
            case('?'):
                usage(argc, argv);
                exit(EXIT_FAILURE);
        }
    }

    tmp_opts->server_count = server_cnt;

    return tmp_opts;
}

/* usage()
 *
 * displays proper program usage to the user
 */
static void usage(int argc, char *argv[])
{
    fprintf(stderr, "\n");
    fprintf(stderr, "Usage : %s [-s server] [-t history] [-o set_history_size] [-i set_interval] [-k set_key_count] [-f fs_id]\n", argv[0]);
    fprintf(stderr, "Example: %s -s tcp://127.0.0.1:3334 -t 60 -z 4 -i 500 -k 16 -f 135161\n", argv[0]);
    fprintf(stderr, "\nOPTIONS:\n");
    fprintf(stderr, "\n-s\t specify a server address, e.g. tcp://127.0.0.1:3334\n");
    fprintf(stderr, "\t multiple servers can be specified by repeating -s option\n");
    fprintf(stderr, "\t if no servers are specified, a list will be generated\n");
    fprintf(stderr, "\n-t\t history  measured in seconds (must be > 0)\n");
    fprintf(stderr, "\t if no history is specified, all uid history is returned\n");
    fprintf(stderr, "\n-o\t Set the history value in the server.\n");
    fprintf(stderr, "\n-i\t Set the interval value in the server.\n");
    fprintf(stderr, "\t the above mentioned interval value is in ms\n");
    fprintf(stderr, "\n-k\t Set the key count value in the server.\n");
    fprintf(stderr, "\n-f\t specify a PVFS_fs_id\n");
    fprintf(stderr, "\t if not specified, a default fs_id is found\n");
    fprintf(stderr, "\n-h\t display program usage\n\n");
    return;
}

/* cleanup() 
 *
 * This function frees all memory used by this application
 */
static void cleanup(struct options *opts, PVFS_BMI_addr_t *addr_array)
{
    int i;

    for (i = 0; i < UID_SERV_LIST_SIZE; i++)
    {
        if (opts->server_list[i] == NULL)
        {
            break;
        }
        free(opts->server_list[i]);
    }
    free(opts->server_list);
    free(opts);
    free(addr_array);
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
