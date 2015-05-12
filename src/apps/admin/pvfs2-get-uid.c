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
    char **server_list;
    int server_count;
    PVFS_fs_id fs_id;
};

static struct options *parse_args(int argc, char *argv[]);
static void usage(int argc, char *argv[]);
static void cleanup(struct options *ptr, PVFS_BMI_addr_t *addr_array,
                    PVFS_uid_info_s **uid_stats);

int main(int argc, char *argv[])
{
    PVFS_credential creds;
    PVFS_fs_id cur_fs;
    PVFS_BMI_addr_t *addr_array, server_addr;
    PVFS_uid_info_s **uid_info_array;
    uint32_t *uid_info_count;
    char uid_timestamp0[64], uid_timestamp[64], curTime[64];
    struct options *prog_opts = NULL;
    int ret = 0;
    int i, j;
    struct timeval currentTime;

    /* parse command line arguments */ 
    prog_opts = parse_args(argc, argv);
    if (!prog_opts)
    {
        fprintf(stderr, "Unable to allocate memory for command line args\n");
        exit(EXIT_FAILURE);
    }

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

    /* get a default fsid or use the one given by the user */
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

    /* if user specifies servers, allocate memory for the BMI addrs and
     * then translate the server strings to BMI addrs
     */
    if (prog_opts->server_count)
    {
        /* allocate memory for our BMI addresses and fill them in */
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
        /* else, user specified no servers, so a list will be built */
        ret = PVFS_mgmt_count_servers(cur_fs, PINT_SERVER_TYPE_ALL,
                                      &(prog_opts->server_count));
        if (ret < 0)
        {
            PVFS_perror("PVFS_mgmt_count_servers", ret);
            return (-1);
        }

        /* allocate memory for the number of BMI addrs found */
        addr_array = (PVFS_BMI_addr_t *)malloc(prog_opts->server_count *
                                           sizeof(PVFS_BMI_addr_t));
        if (!addr_array)
        {
            fprintf(stderr, "Unable to allocate memory for BMI addrs\n");
            exit(EXIT_FAILURE);
        }

        /* retrieve the list of BMI addrs for the list of servers */
        ret = PVFS_mgmt_get_server_array(cur_fs, PINT_SERVER_TYPE_ALL, 
                                            addr_array,
                                            &(prog_opts->server_count));
        if (ret < 0)
        {
            PVFS_perror("PVFS_mgmt_get_server_array", ret);
            return (-1);
        }

        /* use reverse lookups so the server URI's can be displayed to the user */
        for (i = 0; i < prog_opts->server_count; i++)
        {
            prog_opts->server_list[i] = strdup(BMI_addr_rev_lookup(addr_array[i]));
        }
    }

    /* allocate memory to store the uid statistics from the given servers */
    uid_info_array = (PVFS_uid_info_s **)malloc(prog_opts->server_count *
                                          sizeof(PVFS_uid_info_s *));
    if (!uid_info_array)
    {
        fprintf(stderr, "Unable to allocate memory for uid stats array\n");
        exit(EXIT_FAILURE);
    }
    for (i = 0; i < prog_opts->server_count; i++)
    {
        uid_info_array[i] = (PVFS_uid_info_s *)malloc(UID_MGMT_MAX_HISTORY *
                                                sizeof(PVFS_uid_info_s));
        if(!uid_info_array[i])
        {
            fprintf(stderr, "Unable to allocate memory for uid stats array\n");
            exit(EXIT_FAILURE);
        }
    }

    uid_info_count = (uint32_t *)malloc(prog_opts->server_count * sizeof(uint32_t));
    if (!uid_info_count)
    {
        fprintf(stderr, "Memory allocation error, out of memory\n");
    }

    /* retrieve the statistics from the servers, checking for any errors */
    ret = PVFS_mgmt_get_uid_list(cur_fs, &creds, prog_opts->server_count,
                            addr_array, prog_opts->history, uid_info_array,
                            uid_info_count, NULL, NULL);
    if (ret < 0)
    {
        PVFS_perror("PVFS_mgmt_get_uid_list", ret);
        return (-1);
    }

    printf("\nFSID: %d\n", cur_fs);

    /* get a current timestamp for users to compare against */
    PINT_util_get_current_timeval(&currentTime);
    PINT_util_parse_timeval(currentTime, curTime); 
    printf("Current Time: %s\n\n", curTime);

    /* display the uid statistics for each server to the user */
    for (i = 0; i < prog_opts->server_count; i++)
    {
        printf("Server: %s\n", prog_opts->server_list[i]);
        for (j = 0; j < uid_info_count[i]; j++)
        {

            PINT_util_parse_timeval(uid_info_array[i][j].tv0, uid_timestamp0);
            PINT_util_parse_timeval(uid_info_array[i][j].tv, uid_timestamp);
            printf("\tUID: %-10u\tcount: %-10llu\t%s\t%s\n",
                   uid_info_array[i][j].uid,
                   (long long unsigned int)uid_info_array[i][j].count,
                   uid_timestamp0,
                   uid_timestamp);
        }
        printf("\n");
    }

    /* memory cleanup */
    cleanup(prog_opts, addr_array, uid_info_array);

    return 0;
}

/* parse_args()
 *
 * parses command line arguments and returns pointer to program options
 */
static struct options *parse_args(int argc, char *argv[])
{
    char flags[] = "s:t:f:h";
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
    fprintf(stderr, "Usage : %s [-s server] ... [-t history] [-f fs_id]\n", argv[0]);
    fprintf(stderr, "Example: %s -s tcp://127.0.0.1:3334 -t 60 -f 135161\n", argv[0]);
    fprintf(stderr, "\nOPTIONS:\n");
    fprintf(stderr, "\n-s\t specify a server address, e.g. tcp://127.0.0.1:3334\n");
    fprintf(stderr, "\t multiple servers can be specified by repeating -s option\n");
    fprintf(stderr, "\t if no servers are specified, a list will be generated\n");
    fprintf(stderr, "\n-t\t history  measured in seconds (must be > 0)\n");
    fprintf(stderr, "\t if no history is specified, all uid history is returned\n");
    fprintf(stderr, "\n-f\t specify a PVFS_fs_id\n");
    fprintf(stderr, "\t if not specified, a default fs_id is found\n");
    fprintf(stderr, "\n-h\t display program usage\n\n");
    return;
}

/* cleanup() 
 *
 * This function frees all memory used by this application
 */
static void cleanup(struct options *opts, PVFS_BMI_addr_t *addr_array,
                    PVFS_uid_info_s **uid_stats)
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
    for (i = 0; i < opts->server_count; i++)
    {
        free(uid_stats[i]);
    }
    free(opts->server_list);
    free(opts);
    free(addr_array);
    free(uid_stats);
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

