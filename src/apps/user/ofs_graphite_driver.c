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


/* these defaults overridden by command line args */
#define MAX_KEY_CNT 18
#define HISTORY 5
#define FREQUENCY 10

#ifndef PVFS2_VERSION
#define PVFS2_VERSION "Unknown"
#endif

/* macros for accessing data returned from server 
 * s is server, h is history sample, c is counter number
 */
#define GETLAST(s,c) (last[(s * (user_opts->keys + 2)) + (c)])
#define LAST(s) GETLAST((s), 0)
#define GETSAMPLE(s,h,c) (perf_matrix[(s)][((h) * (user_opts->keys + 2)) + (c)])
#define SAMPLE(s,h) GETSAMPLE((s), (h), 0)
#define VALID_FLAG(s,h) (GETSAMPLE((s), (h), user_opts->keys) != 0.0)
#define ID(s,h) GETSAMPLE((s), (h), user_opts->keys)
#define START_TIME(s,h) GETSAMPLE((s), (h), user_opts->keys)

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

#define GRAPHITE_PRINT_COUNTER(str, c, s, h)                      \
if (c >= user_opts->keys) break;                                  \
do {                                                              \
    int64_t sample;                                               \
    if (user_opts->debug)                                         \
    {                                                             \
        fprintf(pfile,"DEBUG %s %lld %lld\n", str,                \
        (long long int)GETSAMPLE(s, h, c), (long long int)GETLAST(s, c));  \
    }                                                             \
    if (user_opts->raw)                                           \
    {                                                             \
        sample = GETSAMPLE(s, h, c);                              \
    }                                                             \
    else                                                          \
    {                                                             \
        if (h == 0)                                               \
        {                                                         \
            sample = -GETLAST(s, c);                              \
        }                                                         \
        else                                                      \
        {                                                         \
            sample = -GETSAMPLE(s, h - 1, c);                     \
        }                                                         \
        sample += GETSAMPLE(s, h, c);                             \
    }                                                             \
    if (user_opts->graphite)                                      \
    {                                                             \
        sprintf(graphite_message,                                 \
                "\n%s%s %lld %lld\n",                             \
                samplestr,                                        \
                str,                                              \
                (long long int)sample,                            \
                (long long int)START_TIME(s, h)/1000);            \
        bytes_written = write(graphite_fd,                        \
                graphite_message,                                 \
                strlen(graphite_message) + 1);                    \
        if(bytes_written != strlen(graphite_message) + 1){        \
            fprintf(stderr, "write failed\n");                    \
        }                                                         \
        printf("sent graphite message\n");                        \
    }                                                             \
    if (user_opts->print)                                         \
    {                                                             \
        fprintf(pfile,                                            \
                "%s%s %lld %lld\n",                               \
                samplestr,                                        \
                str,                                              \
                (long long int)sample,                            \
                (long long int)START_TIME(s, h)/1000);            \
    }                                                             \
} while(0);

#if 0

#define PRINT_COUNTER(str, c, s, h)                    \
do {                                                   \
    int64_t sample;                                    \
    if (h == 0)                                        \
    {                                                  \
        sample = -GETLAST(s, c);                       \
    }                                                  \
    else                                               \
    {                                                  \
        sample = -GETSAMPLE(s, h - 1, c);              \
    }                                                  \
    sample += GETSAMPLE(s, h, c);                      \
    fprintf(pfile,                                     \
            "%s%s %lld %lld\n",                        \
            samplestr,                                 \
            str,                                       \
            (unsigned long long int)sample,            \
            (unsigned long long int)START_TIME(s, h)); \
} while(0);

#endif

struct options
{
    char *mnt_point;
    char *graphite_addr;
    char *precursor_string;
    int mnt_point_set;
    int precursor;
    int graphite;
    int print;
    int keys;
    int raw;
    int debug;
    int history;
    int frequency;
};

static struct options *parse_args(int argc, char **argv);
static void usage(int argc, char **argv);
static int graphite_connect(char *);
static void print_sample(char *str, int64_t *samples, int size) GCC_UNUSED;

int main(int argc, char **argv)
{
    int ret = -1;
    PVFS_fs_id cur_fs;
    struct options *user_opts = NULL;
    char pvfs_path[PVFS_NAME_MAX] = {0};
    int s = 0, h = 0;
    PVFS_credential cred;
    int io_server_count = 0;
    int64_t **perf_matrix = NULL;
    uint64_t *end_time_ms_array = NULL;
    uint32_t *next_id_array = NULL;
    PVFS_BMI_addr_t *addr_array = NULL;
    const char **serverstr = NULL;
    FILE *pfile = stdout;
    int64_t *last = NULL;
    int graphite_fd = 0;
    int bytes_written = 0;

    /* look at command line arguments */
    user_opts = parse_args(argc, argv);
    if(!user_opts)
    {
	fprintf(stderr, "Error: failed to parse command line arguments.\n");
	usage(argc, argv);
	return(-1);
    }

    if (user_opts->keys == 0)
    {
        user_opts->keys = MAX_KEY_CNT;
    }
    printf("\nkeys: %d", user_opts->keys);

    if (user_opts->history == 0)
    {
        user_opts->history = HISTORY;
    }
    printf("\nhistory: %d", user_opts->history);

    if (user_opts->frequency == 0)
    {
        user_opts->frequency = HISTORY;
    }
    printf("\nfrequency: %d", user_opts->frequency);
    printf("\n");

    if (user_opts->graphite)
    {
        graphite_fd = graphite_connect(user_opts->graphite_addr);
        if (graphite_fd < 0)
        {
            user_opts->graphite = 0;
            fprintf(stderr,
                    "Tried to open link to Graphite server and failed\n");
        }
    }

    if (!user_opts->print &&
        !user_opts->graphite &&
        !user_opts->debug)
    {
        fprintf(stderr,
                "Neither printing, debugging, nor sending to Graphite\n");
        exit(-1);
    }

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
    memset(perf_matrix, 0, io_server_count * sizeof(int64_t *));
    for(s = 0; s < io_server_count; s++)
    {
	perf_matrix[s] = (int64_t *)malloc((user_opts->keys + 2) * 
                                           user_opts->history *
                                           sizeof(int64_t));
	if (perf_matrix[s] == NULL)
	{
	    perror("malloc");
	    return -1;
	}
        memset(perf_matrix[s], 0, (user_opts->keys + 2) * 
                                  user_opts->history *
                                  sizeof(int64_t));
    }

    /* this array holds the last asmple from each server for continuity */
    last = (int64_t *)malloc((user_opts->keys + 2) *
                             io_server_count *
                             sizeof(int64_t));
    memset(last, 0, (user_opts->keys + 2) * io_server_count * sizeof(int64_t));

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

    /* edit a clean string that represents the server name */
    serverstr = (const char **)malloc(io_server_count * sizeof(char *));
    for (s = 0; s < io_server_count; s++)
    {
        int servertype;
        int n;
        const char *cp;
        char *p, *p2;

        cp = PVFS_mgmt_map_addr(cur_fs, addr_array[s], &servertype);
        p = strdup(cp);

        p2 = strstr(p, "://");
        if (p2)
        {
            p2 += 2;
            *p2 = 0;
            p2++;
        }
        else
        {
            p2 = p;
        }

        n = strcspn(p2, "-,:/");
        p2[n] = 0;

        serverstr[s] = p2;
    }

    /* loop for ever, grabbing stats at regular intervals */
    while (1)
    {
        PVFS_util_refresh_credential(&cred);
	ret = PVFS_mgmt_perf_mon_list(cur_fs,
				      &cred,
				      perf_matrix, 
				      end_time_ms_array,
				      addr_array,
				      next_id_array,
				      io_server_count, 
                                      &user_opts->keys,
				      user_opts->history,
				      NULL,
                                      NULL);
	if (ret < 0)
	{
	    PVFS_perror("PVFS_mgmt_perf_mon_list", ret);
	    return -1;
	}

        //print_sample("sample", perf_matrix[0], user_opts->keys + 2);

	/* printf("\nPVFS2 I/O server counters\n"); 
	 * printf("==================================================\n");
         */
	for (s = 0; s < io_server_count; s++)
	{
            char samplestr[256] = {0}; 
            if(user_opts->precursor == 1){
                strcat(samplestr, user_opts->precursor_string);
           //     samplestr = strdup(user_opts->precursor_string);
            } else {
                strcat(samplestr, "palmetto.");
         //       samplestr = strdup("palmetto.");
            }
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

            memcpy(&last[(s * (user_opts->keys + 2))],
                   &perf_matrix[(s)][((user_opts->history - 1) *
                                      (user_opts->keys + 2))],
                   ((user_opts->keys + 2) * sizeof(uint64_t)));

/* does not seem to work for some reason */
#if 0
            memcpy(&LAST(s),
                   &(SAMPLE(s, h - 1)),
                   ((user_opts->keys + 2) * sizeof(uint64_t)));
#endif
	}
	fflush(stdout);
	sleep(user_opts->frequency);
    }

    PVFS_sys_finalize();

    if (user_opts->graphite)
    {
        close(graphite_fd);
    }

    return(ret);
}


/* parse_args()
 *
 * parses command line arguments
 *
 * returns pointer to options structure on success, NULL on failure
 */
static struct options *parse_args(int argc, char **argv)
{
    char flags[] = "vm:h:f:k:g:prdn:";
    int one_opt = 0;
    struct options *tmp_opts = NULL;

    /* create storage for the command line options */
    tmp_opts = (struct options *) malloc(sizeof(struct options));
    if(tmp_opts == NULL)
    {
	return(NULL);
    }
    memset(tmp_opts, 0, sizeof(struct options));

    /* look at command line arguments */
    opterr = 0; /* getopts should not print error messages */
    while((one_opt = getopt(argc, argv, flags)) != EOF)
    {
	switch(one_opt)
        {
            case('h'):
                tmp_opts->history = atoi(optarg);
                break;
            case('f'):
                tmp_opts->frequency = atoi(optarg);
                break;
            case('k'):
                tmp_opts->keys = atoi(optarg);
                break;
            case('r'):
                tmp_opts->raw = 1;
                break;
            case('d'):
                tmp_opts->debug = 1;
                break;
            case('v'):
                printf("%s\n", PVFS2_VERSION);
                exit(0);
	    case('m'):
		tmp_opts->mnt_point = strdup(optarg);
		if(!tmp_opts->mnt_point)
		{
		    usage(argc, argv);
		    exit(EXIT_FAILURE);
		}
		tmp_opts->mnt_point_set = 1;
		/* TODO: dirty hack... fix later.  The remove_dir_prefix()
		 * function expects some trailing segments or at least
		 * a slash off of the mount point
		 */
                realloc(tmp_opts->mnt_point, strlen(tmp_opts->mnt_point) + 2);
		strcat(tmp_opts->mnt_point, "/");
		break;
            case('n'):
                tmp_opts->precursor_string = strdup(optarg);
		if(!tmp_opts->precursor_string)
		{
		    usage(argc, argv);
		    exit(EXIT_FAILURE);
		}
                tmp_opts->precursor = 1;
                break;
            case('g'):
                tmp_opts->graphite_addr = strdup(optarg);
		if(!tmp_opts->graphite_addr)
		{
		    usage(argc, argv);
		    exit(EXIT_FAILURE);
		}
                tmp_opts->graphite = 1;
                break;
            case('p'):
                tmp_opts->print = 1;
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
    fprintf(stderr,
            "Usage  : %s [-m fs_mount_point] [-g hostname|ipaddr]\n",
            argv[0]);
    fprintf(stderr,
            "Example: %s -m /mnt/pvfs2 -g graphite.clemson.edu\n",
            argv[0]);
    return;
}

static int graphite_connect(char *graphite_addr)
{
    int sockfd;
    int ret;
    int portno = 2003;
    struct sockaddr_in serv_addr;
    struct hostent *server;


    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0)
    {
         printf("Failed to open sock\n");
         return -1;
    }

    server = gethostbyname(graphite_addr);
    if(!server)
    {
         herror(0);
         return -2;
    }

    serv_addr.sin_family = AF_INET;
    memcpy((char *)&serv_addr.sin_addr.s_addr,
            server->h_addr,
            server->h_length);
    serv_addr.sin_port = htons(portno);

    if ((ret = connect(sockfd,
                (struct sockaddr*) &serv_addr,
                sizeof(struct sockaddr))) < 0)
    {
         printf("failed to connect to socket, connect returned: %s\n", strerror(errno));
         return -3;
    }

    return sockfd;
}

static void print_sample(char *str, int64_t *samples, int size)
{
    int s;
    if (str)
    {
        printf("%s ", str);
    }
    for (s = 0; s < size; s++)
    {
        printf("%lld ", (long long int)samples[s]);
    }
    printf("\n");
}


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
