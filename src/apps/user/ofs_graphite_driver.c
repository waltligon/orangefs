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

#define DEBUG 1
/* debugging */
#if defined(DEBUG) && DEBUG != 0
#define dbgprintf printf
#define dbgprintf1 printf
#define dbgprintf2 printf
#define dbgprintf3 printf
#else
#define dbgprintf(s) do{}while(0)
#define dbgprintf1(s,a1) do{}while(0)
#define dbgprintf2(s,a1,a2) do{}while(0)
#define dbgprintf3(s,a1,a2,a3) do{}while(0)
#endif

/* these defaults overridden by command line args */
#define MAX_KEY_COUNTER 23
#define MAX_KEY_TIMER 10
#define HISTORY 10
#define FREQUENCY 10

#ifndef PVFS2_VERSION
#define PVFS2_VERSION "Unknown"
#endif

/* macros for accessing data returned from server 
 * s is server, h is history sample,
 * c is counter number, f is field in counter
 * sample_size is defined at runtime below
 */
#define GETLAST(s,c) (last[(s * sample_size) + (c)])
#define LAST(s) GETLAST((s), 0)

#define GETSAMPLE(s,h,c,f) (perf_matrix[(s)][((h) * sample_size) +  \
                                             ((c) * counter_size) + (f)])
#define SAMPLE(s,h) GETSAMPLE((s), (h), 0, 0)
//#define VALID_FLAG(s,h) (GETSAMPLE((s), (h), user_opts->keys, 0) != 0.0)
#define VALID_FLAG(s,h) 1
#define ID(s,h) GETSAMPLE((s), (h), user_opts->keys, 0)
#define START_TIME(s,h) GETSAMPLE((s), (h), user_opts->keys, 0)

enum PINT_perf_timer_fields
{
    TIMER_SUM = 0,
    TIMER_COUNT = 1,
    TIMER_MINIMUM = 2,
    TIMER_MAXIMUM = 3,
};

#define GRAPHITE_CNT(str, c, s, h)                                \
if (c >= user_opts->keys) break;                                  \
do {                                                              \
    int64_t sample = 0;                                           \
    if (user_opts->debug)                                         \
    {                                                             \
        dbgprintf3("DEBUG %s %lld %lld\n", str,                   \
              (long long int)GETSAMPLE(s, h, c, 0),               \
              (long long int)GETLAST(s, c));                      \
    }                                                             \
    if (user_opts->raw)                                           \
    {                                                             \
        sample = GETSAMPLE(s, h, c, 0);                           \
    }                                                             \
    else                                                          \
    {                                                             \
        if (h == 0)                                               \
        {                                                         \
            sample = -GETLAST(s, c);                              \
        }                                                         \
        else                                                      \
        {                                                         \
            sample = -GETSAMPLE(s, h - 1, c, 0);                  \
        }                                                         \
        sample += GETSAMPLE(s, h, c, 0);                          \
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
        if(bytes_written != strlen(graphite_message) + 1)         \
        {                                                         \
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

#define GRAPHITE_TIMER(str, c, s, h)                              \
if (c >= user_opts->keys) break;                                  \
do {                                                              \
    double  avg = 0.0;                                            \
    int64_t sum = 0;                                              \
    int64_t cnt = 0;                                              \
    int64_t min = 0;                                              \
    int64_t max = 0;                                              \
    sum = GETSAMPLE(s, h, c, TIMER_SUM);                          \
    cnt = GETSAMPLE(s, h, c, TIMER_COUNT);                        \
    min = GETSAMPLE(s, h, c, TIMER_MINIMUM);                      \
    max = GETSAMPLE(s, h, c, TIMER_MAXIMUM);                      \
    if (cnt != 0)                                                 \
    {                                                             \
        avg = (double)sum / cnt;                                  \
    }                                                             \
    if (user_opts->print)                                         \
    {                                                             \
        if (user_opts->raw)                                       \
        {                                                         \
            fprintf(pfile,                                        \
                    "%s%s-sum %lld %lld\n",                       \
                    samplestr,                                    \
                    str,                                          \
                    (long long int)sum,                           \
                    (long long int)START_TIME(s, h)/1000);        \
        }                                                         \
        else                                                      \
        {                                                         \
            fprintf(pfile,                                        \
                    "%s%s-avg %.f %lld\n",                        \
                    samplestr,                                    \
                    str,                                          \
                    avg,                                          \
                    (long long int)START_TIME(s, h)/1000);        \
        }                                                         \
    }                                                             \
    if (user_opts->graphite)                                      \
    {                                                             \
        sprintf(graphite_message,                                 \
                "%s%s-avg %.f %lld\n",                            \
                samplestr,                                        \
                str,                                              \
                avg,                                              \
                (long long int)START_TIME(s, h)/1000);            \
        bytes_written = write(graphite_fd,                        \
                graphite_message,                                 \
                strlen(graphite_message) + 1);                    \
        if(bytes_written != strlen(graphite_message) + 1)         \
        {                                                         \
            fprintf(stderr, "write failed\n");                    \
        }                                                         \
        printf("sent graphite message\n");                        \
    }                                                             \
    if (user_opts->print)                                         \
    {                                                             \
        fprintf(pfile,                                            \
                "%s%s-cnt %lld %lld\n",                           \
                samplestr,                                        \
                str,                                              \
                (long long int)cnt,                               \
                (long long int)START_TIME(s, h)/1000);            \
    }                                                             \
    if (user_opts->graphite)                                      \
    {                                                             \
        sprintf(graphite_message,                                 \
                "%s%s-cnt %lld %lld\n",                           \
                samplestr,                                        \
                str,                                              \
                (long long int)cnt,                               \
                (long long int)START_TIME(s, h)/1000);            \
        bytes_written = write(graphite_fd,                        \
                graphite_message,                                 \
                strlen(graphite_message) + 1);                    \
        if(bytes_written != strlen(graphite_message) + 1)         \
        {                                                         \
            fprintf(stderr, "write failed\n");                    \
        }                                                         \
        printf("sent graphite message\n");                        \
    }                                                             \
    if (user_opts->print)                                         \
    {                                                             \
        fprintf(pfile,                                            \
                "%s%s-min %lld %lld\n",                           \
                samplestr,                                        \
                str,                                              \
                (long long int)min,                               \
                (long long int)START_TIME(s, h)/1000);            \
    }                                                             \
    if (user_opts->graphite)                                      \
    {                                                             \
        sprintf(graphite_message,                                 \
                "%s%s-min %lld %lld\n",                           \
                samplestr,                                        \
                str,                                              \
                (long long int)min,                               \
                (long long int)START_TIME(s, h)/1000);            \
        bytes_written = write(graphite_fd,                        \
                graphite_message,                                 \
                strlen(graphite_message) + 1);                    \
        if(bytes_written != strlen(graphite_message) + 1)         \
        {                                                         \
            fprintf(stderr, "write failed\n");                    \
        }                                                         \
        printf("sent graphite message\n");                        \
    }                                                             \
    if (user_opts->print)                                         \
    {                                                             \
        fprintf(pfile,                                            \
                "%s%s-max %lld %lld\n",                           \
                samplestr,                                        \
                str,                                              \
                (long long int)max,                               \
                (long long int)START_TIME(s, h)/1000);            \
    }                                                             \
    if (user_opts->graphite)                                      \
    {                                                             \
        sprintf(graphite_message,                                 \
                "%s%s-max %lld %lld\n",                           \
                samplestr,                                        \
                str,                                              \
                (long long int)max,                               \
                (long long int)START_TIME(s, h)/1000);            \
        bytes_written = write(graphite_fd,                        \
                graphite_message,                                 \
                strlen(graphite_message) + 1);                    \
        if(bytes_written != strlen(graphite_message) + 1)         \
        {                                                         \
            fprintf(stderr, "write failed\n");                    \
        }                                                         \
        printf("sent graphite message\n");                        \
    }                                                             \
} while(0);

struct options
{
    char *mnt_point;
    char *graphite_addr;
    char *precursor_string;
    int mnt_point_set;
    int single_srv;
    int precursor;
    int graphite;
    int print;
    int ctype; /* counter or timer */
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
    int sample_size = 0;  /* size (in int64_t) of nkeys coounters plus two */
    int counter_size = 0;  /* size (in int64_t) of 1 coounters */
    int srv_index = 0;

    /* look at command line arguments */
    user_opts = parse_args(argc, argv);
    if(!user_opts)
    {
	fprintf(stderr, "Error: failed to parse command line arguments.\n");
	usage(argc, argv);
	return(-1);
    }

    /* compute the number of int64_t in a full sample - all of the keys
     * plus the time stamps.
     */
    if (user_opts->keys == 0)
    {
        if (user_opts->ctype == PINT_PERF_COUNTER)
        {
            user_opts->keys = MAX_KEY_COUNTER;
        }
        else
        {
            user_opts->keys = MAX_KEY_TIMER;
        }
    }
    /* sample_size is number of int64_t sized words (8 bytes per word) */
    if (user_opts->ctype == PINT_PERF_COUNTER)
    {
        counter_size = 1;
    }
    else if (user_opts->ctype == PINT_PERF_TIMER)
    {
        counter_size = sizeof(struct PINT_perf_timer)/sizeof(int64_t);;
    }
    sample_size = (counter_size * user_opts->keys) + 2;
    if (user_opts->print)
    {
        printf("\nkeys: %d", user_opts->keys);
    }

    if (user_opts->history == 0)
    {
        user_opts->history = HISTORY;
    }
    if (user_opts->print)
    {
        printf("\nhistory: %d", user_opts->history);
    }

    if (user_opts->frequency == 0)
    {
        user_opts->frequency = FREQUENCY;
    }
    if (user_opts->print)
    {
        printf("\nfrequency: %d", user_opts->frequency);
        printf("\n");
    }

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

    /* check for single server mode */
    if (user_opts->single_srv != -1)
    {
        if (user_opts->single_srv >= 0 && 
            user_opts->single_srv < io_server_count)
        {
            io_server_count = 1;
            srv_index = user_opts->single_srv;
        }
        else
        {
            fprintf(stderr, "Single server requested is out of range for this file system\n");
            exit(-1);
        }
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
        int server_data_size = (sample_size * user_opts->history) *
                               sizeof(int64_t);

	perf_matrix[s] = (int64_t *)malloc(server_data_size);
	if (perf_matrix[s] == NULL)
	{
	    perror("malloc");
	    return -1;
	}
        memset(perf_matrix[s], 0, server_data_size);
    }

    /* this array holds the last sample from each server for continuity */
    last = (int64_t *)malloc(sample_size * io_server_count * sizeof(int64_t));
    memset(last, 0, (sample_size * io_server_count * sizeof(int64_t)));

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

    /* edit a clean string that represents the server name */
    serverstr = (const char **)malloc(io_server_count * sizeof(char *));
    for (s = srv_index; s < io_server_count; s++)
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
        /* flag to keep us from reporting wild numbers on very first pass */
        static int not_first_pass = 0;

        PVFS_util_refresh_credential(&cred);
        dbgprintf("starting read\n");
	ret = PVFS_mgmt_perf_mon_list(cur_fs,
				      &cred,
				      user_opts->ctype, 
				      perf_matrix, 
				      end_time_ms_array,
				      &addr_array[srv_index],
				      next_id_array,
				      io_server_count, 
                                      &user_opts->keys,   /* in/out */
				      &user_opts->history, /* in/out */
				      NULL,
                                      NULL);
        dbgprintf("read finished\n");
	if (ret < 0)
	{
	    PVFS_perror("PVFS_mgmt_perf_mon_list", ret);
	    return -1;
	}

	for (s = 0; s < io_server_count; s++)
	{
            char graphite_message[512] = {0};
            char samplestr[256] = {0}; 
	    dbgprintf("================================================\n");
            dbgprintf("next server\n");

            /* set up first level name of measurements */
            if(user_opts->precursor == 1)
            {
                strcat(samplestr, user_opts->precursor_string);
            }
            else
            {
                strcat(samplestr, "palmetto.");
            }
            /* add server name */
            strcat(samplestr, serverstr[s]);
            /* next indicate OFS measuremnts */
            strcat(samplestr, ".orangefs.");

            /* now step through data */
	    for (h = 0; h < user_opts->history; h++)
	    {
                dbgprintf("next sample\n");
                if (not_first_pass == 0 && h == 0)
                {
                    dbgprintf("skipping first sample\n");
                    /* skip very first sample of each server because it
                     * will create weird discontinuities in the output
                     */
                    continue;
                }
                if (user_opts->ctype == PINT_PERF_COUNTER)
                {
                    dbgprintf1("counter time-stamp %lld\n",
                               (long long int)START_TIME(s, h));
                    if (VALID_FLAG(s, h))
                    {
                        GRAPHITE_CNT("read", PINT_PERF_READ, s, h);
                        GRAPHITE_CNT("write", PINT_PERF_WRITE, s, h);
                        GRAPHITE_CNT("metaread", PINT_PERF_METADATA_READ, s, h);
                        GRAPHITE_CNT("metawrite", PINT_PERF_METADATA_WRITE, s, h);
                        GRAPHITE_CNT("dspaceops", PINT_PERF_METADATA_DSPACE_OPS, s, h);
                        GRAPHITE_CNT("keyvalops", PINT_PERF_METADATA_KEYVAL_OPS, s, h);
                        GRAPHITE_CNT("scheduled", PINT_PERF_REQSCHED, s, h);
                        GRAPHITE_CNT("requests", PINT_PERF_REQUESTS, s, h);
                        GRAPHITE_CNT("ioreads", PINT_PERF_IOREAD, s, h);
                        GRAPHITE_CNT("iowrites", PINT_PERF_IOWRITE, s, h);
                        GRAPHITE_CNT("smallreads", PINT_PERF_SMALL_READ, s, h);
                        GRAPHITE_CNT("smallwrites", PINT_PERF_SMALL_WRITE, s, h);
                        GRAPHITE_CNT("flowreads", PINT_PERF_FLOW_READ, s, h);
                        GRAPHITE_CNT("flowwrites", PINT_PERF_FLOW_WRITE, s, h);
                        GRAPHITE_CNT("creates", PINT_PERF_CREATE, s, h);
                        GRAPHITE_CNT("removes", PINT_PERF_REMOVE, s, h);
                        GRAPHITE_CNT("mkdirs", PINT_PERF_MKDIR, s, h);
                        GRAPHITE_CNT("rmdir", PINT_PERF_RMDIR, s, h);
                        GRAPHITE_CNT("getattrs", PINT_PERF_GETATTR, s, h);
                        GRAPHITE_CNT("setattrs", PINT_PERF_SETATTR, s, h);
                        GRAPHITE_CNT("io", PINT_PERF_IO, s, h);
                        GRAPHITE_CNT("smallio", PINT_PERF_SMALL_IO, s, h);
                        GRAPHITE_CNT("readdir", PINT_PERF_READDIR, s, h);
                    }
                }
                else if (user_opts->ctype == PINT_PERF_TIMER)
                {
                    dbgprintf1("timer time-stamp %lld\n", 
                               (long long int)START_TIME(s, h));
                    if (VALID_FLAG(s, h))
                    {
                        GRAPHITE_TIMER("lookup-time", PINT_PERF_TLOOKUP, s, h);
                        GRAPHITE_TIMER("create-time", PINT_PERF_TCREATE, s, h);
                        GRAPHITE_TIMER("remove-time", PINT_PERF_TREMOVE, s, h);
                        GRAPHITE_TIMER("mkdir-time", PINT_PERF_TMKDIR, s, h);
                        GRAPHITE_TIMER("rmdir-time", PINT_PERF_TRMDIR, s, h);
                        GRAPHITE_TIMER("getattr-time", PINT_PERF_TGETATTR, s, h);
                        GRAPHITE_TIMER("setattr-time", PINT_PERF_TSETATTR, s, h);
                        GRAPHITE_TIMER("io-time", PINT_PERF_TIO, s, h);
                        GRAPHITE_TIMER("small_io-time", PINT_PERF_TSMALL_IO, s, h);
                        GRAPHITE_TIMER("readdir-time", PINT_PERF_TREADDIR, s, h);
                    }
                }
            }
            dbgprintf("save last sample\n");

            memcpy(&last[s * sample_size],
                   &perf_matrix[s][sample_size * (user_opts->history - 1)],
                   sample_size * sizeof(int64_t));

/* does not seem to work for some reason */
#if 0
            memcpy(&LAST(s),
                   &(SAMPLE(s, h - 1)),
                   ((user_opts->keys + 2) * sizeof(uint64_t)));
#endif
	}
	fflush(stdout);
        if (!(not_first_pass == 0 && h == 1))
        {
            /* don't sleep if first pass and there is only 1 sample */
            dbgprintf("going to sleep\n");
	    sleep(user_opts->frequency);
            dbgprintf("waking up\n");
        }
        not_first_pass = 1;
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
    char flags[] = "vm:s:N:h:f:k:g:prdct";
    int one_opt = 0;
    struct options *tmp_opts = NULL;

    /* create storage for the command line options */
    tmp_opts = (struct options *) malloc(sizeof(struct options));
    if(tmp_opts == NULL)
    {
	return(NULL);
    }
    memset(tmp_opts, 0, sizeof(struct options));
    tmp_opts->single_srv = -1;

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
            case('c'):
                tmp_opts->ctype = PINT_PERF_COUNTER;
                break;
            case('t'):
                tmp_opts->ctype = PINT_PERF_TIMER;
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
                tmp_opts->mnt_point = realloc(tmp_opts->mnt_point,
                                              strlen(tmp_opts->mnt_point) + 2);
		strcat(tmp_opts->mnt_point, "/");
		break;
            case('s'):
                tmp_opts->single_srv = atoi(optarg);
                break;
            case('N'):
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
    fprintf(stderr,
            "\toptions:\n"
                "\t\t-v                   Print version number\n"
                "\t\t-m <fs_mount_point>  Query all servers\n"
                "\t\t-s <single_host>     Query one server\n"
                "\t\t-g <graphite_host>   Output to graphite host\n"
                "\t\t-p <print_data>      Print data to screen\n"
                "\t\t-r <raw_data>        Ouput raw data\n"
                "\t\t-h <history_size>    Read only given number of samples\n"
                "\t\t-k <num_keys>        Read only given number of keys\n"
                "\t\t-c <simple_counters> Get simple counters from server\n"
                "\t\t-t <timers>          Get timers from server\n"
                "\t\t-d                   Print debug messages\n"
                "\t\t ?                   Print usage\n");
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
