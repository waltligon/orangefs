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
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>

#include "pvfs2.h"
#include "pvfs2-mgmt.h"
#include "pvfs2-internal.h"

#define HISTORY 5
#define FREQUENCY 10

#ifndef PVFS2_VERSION
#define PVFS2_VERSION "Unknown"
#endif

#define MAX_KEY_CNT 18
/* macros for accessing data returned from server 
 * s is server, h is history sample, c is counter number
 */
#define GETLAST(s,c) (last[(s * (key_cnt + 2)) + (c)])
#define LAST(s) GETLAST((s), 0)
#define GETSAMPLE(s,h,c) (perf_matrix[(s)][((h) * (key_cnt + 2)) + (c)])
#define SAMPLE(s,h) GETSAMPLE((s), (h), 0)
#define VALID_FLAG(s,h) (GETSAMPLE((s), (h), key_cnt) != 0.0)
#define ID(s,h) GETSAMPLE((s), (h), key_cnt)
#define START_TIME(s,h) GETSAMPLE((s), (h), key_cnt)

#define READ 0
#define WRITE 1
#define METADATA_READ 2
#define METADATA_WRITE 3
#define DSPACE_OPS 4
#define KEYVAL_OPS 5
#define SCHEDULE 6
#define REQUESTS 7
#define SMALL_READS 8
#define SMALL_WRITES 9
#define FLOW_READS 10
#define FLOW_WRITES 11
#define CREATES 12
#define REMOVES 13
#define MKDIRS 14
#define RMDIRS 15
#define GETATTRS 16
#define SETATTRS 17

int key_cnt; /* holds the Number of keys */

#define GRAPHITE_PRINT_COUNTER(str, c, s, h)                    \
do {                                                   \
    int64_t sample;                                    \
    if (h == 0)                                        \
    {                                                  \
        sample = GETLAST(s, c);                        \
    }                                                  \
    else                                               \
    {                                                  \
        sample = GETSAMPLE(s, h - 1, c);               \
    }                                                  \
    sample += GETSAMPLE(s, h, c);                      \
    /* If graphite_addr not set, just print */         \
    if(!!user_opts->graphite_addr[0]){                    \
        graphite_fd = graphite_connect(                \
                user_opts->graphite_addr);             \
        if(graphite_fd <= 0){                          \
            return graphite_fd;                        \
        }                                              \
        sprintf(graphite_message,                      \
            "%s%s %lld %lld\n",                        \
            samplestr,                                 \
            str,                                       \
            (unsigned long long int)sample,            \
            (unsigned long long int)START_TIME(s, h)/1000); \
        write(graphite_fd,                             \
            graphite_message,                          \
            strlen(graphite_message)+1);               \
    }                                                  \
    fprintf(pfile,                                     \
            "%s%s %lld %lld\n",                        \
            samplestr,                                 \
            str,                                       \
            (unsigned long long int)sample,            \
            (unsigned long long int)START_TIME(s, h)/1000); \
    close(graphite_fd);                                \
} while(0);

#define PRINT_COUNTER(str, c, s, h)                    \
do {                                                   \
    int64_t sample;                                    \
    if (h == 0)                                        \
    {                                                  \
        sample = GETLAST(s, c);                        \
    }                                                  \
    else                                               \
    {                                                  \
        sample = GETSAMPLE(s, h - 1, c);               \
    }                                                  \
    sample += GETSAMPLE(s, h, c);                      \
    fprintf(pfile,                                     \
            "%s%s %lld %lld\n",                        \
            samplestr,                                 \
            str,                                       \
            (unsigned long long int)sample,            \
            (unsigned long long int)START_TIME(s, h)); \
} while(0);

struct options
{
    char* mnt_point;
    int mnt_point_set;
    int history;
    int keys;
    char graphite_addr[36];
};

static struct options *parse_args(int argc, char *argv[]);
static void usage(int argc, char **argv);
int graphite_connect(char *);


int main(int argc, char **argv)
{
    int ret = -1;
    PVFS_fs_id cur_fs;
    struct options *user_opts = NULL;
    char pvfs_path[PVFS_NAME_MAX] = {0};
    int s, h;
    PVFS_credential cred;
    int io_server_count;
    int64_t **perf_matrix;
    uint64_t *end_time_ms_array;
    uint32_t *next_id_array;
    PVFS_BMI_addr_t *addr_array;
    const char **serverstr;
    FILE* pfile = stdout;
    int64_t *last = NULL;
    int graphite_fd = 0;

    /* look at command line arguments */
    user_opts = parse_args(argc, argv);
    if(!user_opts)
    {
	fprintf(stderr, "Error: failed to parse command line arguments.\n");
	usage(argc, argv);
	return(-1);
    }
    if (user_opts->history == 0)
    {
        user_opts->history = HISTORY;
    }
    printf("\nhistory: %d\n", user_opts->history);

    ret = PVFS_util_init_defaults();
    if(ret < 0)
    {
	PVFS_perror("PVFS_util_init_defaults", ret);
	return(-1);
    }

    /* translate local path into pvfs2 relative path */
    ret = PVFS_util_resolve(user_opts->mnt_point,
                            &cur_fs,
                            pvfs_path,
                            PVFS_NAME_MAX);
    if(ret < 0)
    {
	PVFS_perror("PVFS_util_resolve", ret);
	return(-1);
    }

    ret = PVFS_util_gen_credential_defaults(&cred);
    if (ret < 0)
    {
        PVFS_perror("PVFS_util_gen_credential", ret);
        return(-1);
    }

    /* count how many I/O servers we have */
    ret = PVFS_mgmt_count_servers(cur_fs,
                                  PVFS_MGMT_IO_SERVER,
	                          &io_server_count);
    if(ret < 0)
    {
	PVFS_perror("PVFS_mgmt_count_servers", ret);
	return(-1);
    }

    /* allocate a 2 dimensional array for statistics */
    perf_matrix = (int64_t **)malloc(io_server_count * sizeof(int64_t *));
    if(!perf_matrix)
    {
	perror("malloc");
	return(-1);
    }
    for(s = 0; s < io_server_count; s++)
    {
	perf_matrix[s] = (int64_t *)malloc((MAX_KEY_CNT + 2) * 
                                           user_opts->history *
                                           sizeof(int64_t));
	if (perf_matrix[s] == NULL)
	{
	    perror("malloc");
	    return -1;
	}
    }

    /* this array holds the last asmple from each server for continuity */
    last = (int64_t *)malloc((MAX_KEY_CNT + 2) *
                             io_server_count *
                             sizeof(int64_t));
    memset(last, 0, (MAX_KEY_CNT + 2) * io_server_count * sizeof(int64_t));

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

    /* build a list of servers to talk to */
    addr_array = (PVFS_BMI_addr_t *)malloc(io_server_count *
                                           sizeof(PVFS_BMI_addr_t));
    printf("built list of servers to talk to\n");
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

    serverstr = (const char **)malloc(io_server_count * sizeof(char *));
    for (s = 0; s < io_server_count; s++)
    {
        int servertype;
        serverstr[s] = PVFS_mgmt_map_addr(cur_fs,
                                          addr_array[s],
                                          &servertype);
    }

    /* loop for ever, grabbing stats at regular intervals */
    while (1)
    {
        PVFS_util_refresh_credential(&cred);
        key_cnt = MAX_KEY_CNT;
	ret = PVFS_mgmt_perf_mon_list(cur_fs,
				      &cred,
				      perf_matrix, 
				      end_time_ms_array,
				      addr_array,
				      next_id_array,
				      io_server_count, 
                                      &key_cnt,
				      user_opts->history,
				      NULL,
                                      NULL);
	if (ret < 0)
	{
	    PVFS_perror("PVFS_mgmt_perf_mon_list", ret);
	    return -1;
	}

	/* printf("\nPVFS2 I/O server counters\n"); 
	 * printf("==================================================\n");
         */
	for (s = 0; s < io_server_count; s++)
	{
            char samplestr[256] = "palmetto.";
            char graphite_message[512] = {0};
            strcat(samplestr, serverstr[s]);
            strcat(samplestr, ".orangefs.");

	    for (h = 0; h < user_opts->history; h++)
	    {
                if (VALID_FLAG(s, h))
                {
                    GRAPHITE_PRINT_COUNTER("read", READ, s, h);
                    GRAPHITE_PRINT_COUNTER("write", WRITE, s, h);
                    GRAPHITE_PRINT_COUNTER("metaread", METADATA_READ, s, h);
                    GRAPHITE_PRINT_COUNTER("metawrite", METADATA_WRITE, s, h);
                    GRAPHITE_PRINT_COUNTER("dspaceops", DSPACE_OPS, s, h);
                    GRAPHITE_PRINT_COUNTER("keyvalops", KEYVAL_OPS, s, h);
                    GRAPHITE_PRINT_COUNTER("scheduled", SCHEDULE, s, h);
                    GRAPHITE_PRINT_COUNTER("requests", REQUESTS, s, h);
                    GRAPHITE_PRINT_COUNTER("smallreads", SMALL_READS, s, h);
                    GRAPHITE_PRINT_COUNTER("smallwrites", SMALL_WRITES, s, h);
                    GRAPHITE_PRINT_COUNTER("flowreads", FLOW_READS, s, h);
                    GRAPHITE_PRINT_COUNTER("flowwrites", FLOW_WRITES, s, h);
                    GRAPHITE_PRINT_COUNTER("creates", CREATES, s, h);
                    GRAPHITE_PRINT_COUNTER("removes", REMOVES, s, h);
                    GRAPHITE_PRINT_COUNTER("mkdirs", MKDIRS, s, h);
                    GRAPHITE_PRINT_COUNTER("rmdir", RMDIRS, s, h);
                    GRAPHITE_PRINT_COUNTER("getattrs", GETATTRS, s, h);
                    GRAPHITE_PRINT_COUNTER("setattrs", SETATTRS, s, h);
                }
            }
            memcpy(&LAST(s),
                   &(SAMPLE(s, h - 1)),
                   ((key_cnt + 2) * sizeof(uint64_t)));
	}
	fflush(stdout);
	sleep(FREQUENCY);
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
    char flags[] = "vm:h:k:g:";
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
            case('h'):
                tmp_opts->history = atoi(optarg);
                break;
            case('k'):
                tmp_opts->keys = atoi(optarg);
                break;
            case('v'):
                printf("%s\n", PVFS2_VERSION);
                exit(0);
	    case('m'):
		len = strlen(optarg) + 1;
		tmp_opts->mnt_point = (char*)malloc(len + 1);
		if(!tmp_opts->mnt_point)
		{
		    free(tmp_opts);
		    return(NULL);
		}
		memset(tmp_opts->mnt_point, 0, len + 1);
		ret = sscanf(optarg, "%s", tmp_opts->mnt_point);
		if(ret < 1){
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
            case('g'):
                if(strlen(optarg) == 0){
                    tmp_opts->graphite_addr[0] = '\0';
                } else {
                    strcpy(tmp_opts->graphite_addr, optarg);
                }
                break;
	    case('?'):
		usage(argc, argv);
		exit(EXIT_FAILURE);
	}
    }

    if (!tmp_opts->mnt_point_set)
    {
	free(tmp_opts);
	return(NULL);
    }

    return(tmp_opts);
}


static void usage(int argc, char **argv)
{
    fprintf(stderr, "\n");
    fprintf(stderr, "Usage  : %s [-m fs_mount_point] [-g hostname|ipaddr]\n", argv[0]);
    fprintf(stderr, "Example: %s -m /mnt/pvfs2 -g [graphite.clemson.edu|130.127.148.159]\n", argv[0]);
    return;
}


int graphite_connect(char *graphite_addr){
    int sockfd;
                int portno = 2003;
                struct sockaddr_in serv_addr;
                struct hostent *server;
          sockfd = socket(AF_INET, SOCK_STREAM, 0);
                if(sockfd < 0){
                        printf("Failed to open sock\n");
                        return -1;
                }
                server = gethostbyname(graphite_addr);
                if(!server){
                        herror(0);
                        return -2;
                }
                serv_addr.sin_family = AF_INET;
                bcopy(server->h_addr, (char *)&serv_addr.sin_addr.s_addr,
                                server->h_length);
                serv_addr.sin_port = htons(portno);
                if(connect(sockfd, (struct sockaddr*) &serv_addr, sizeof(struct sockaddr)) < 0){
                        printf("failed to connect to socket\n");
                        return -3;
                }

    return sockfd;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
