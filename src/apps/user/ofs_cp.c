/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* pvfs2-cp: 
 * 	copy a file from a unix or PVFS2 file system to a unix or PVFS2 file
 * 	system.  Should replace pvfs2-import and pvfs2-export.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <fts.h>
#include <time.h>
#include <libgen.h>
#include <getopt.h>

#include "orange.h"
#include "pint-sysint-utils.h"

#define PVFS_ATTR_SYS_CP (PVFS_ATTR_SYS_TYPE | PVFS_ATTR_SYS_PERM)

/* optional parameters, filled in by parse_args() */
struct options
{
    PVFS_size strip_size;
    int num_datafiles;
    int buf_size;
    int show_timings;
    int copy_to_dir;
    int verbose;
    int recursive;
    char *srcfile;
    char *destfile;
    char **srcv;
};

static char dest_path_buffer[PATH_MAX];

static PVFS_hint hints = NULL;

static struct options *parse_args(int argc, char *argv[]);
static void usage(int argc, char **argv);
static double Wtime(void);
static void print_timings(double time, int64_t total);
static int copy_file(char *srcfile,
                     char *destfile,
                     void *buffer,
                     int buf_size);

int main (int argc, char **argv)
{
    int ret;
    struct options *user_opts = NULL;
    double time1 = 0, time2 = 0;
    int64_t total_written = 0;
    char link_buf[PATH_MAX];
    char *dest_path = NULL;
    char *dest_name = NULL;
    struct stat sbuf;
    void *buffer = NULL;
    FTS *fs;
    FTSENT *node;

    user_opts = parse_args(argc, argv);
    if (!user_opts)
    {
	fprintf(stderr, "Error, failed to parse command line arguments\n");
	return(-1);
    }

    buffer = malloc(user_opts->buf_size);
    if(!buffer)
    {
	perror("malloc");
	ret = -1;
	goto main_out;
    }

    if (user_opts->copy_to_dir)
    {
        dest_path = user_opts->destfile;
        dest_name = dest_path + strnlen(dest_path, PATH_MAX);
        if (*(dest_name - 1) != '/')
        {
            *dest_name = '/';
            dest_name += 1;
            *dest_name = 0;
        }
    }
    else
    {
        /* simple file copy */
        if (user_opts->verbose)
        {
            printf("%s\n", user_opts->srcfile);
        }
        total_written += copy_file(user_opts->srcfile,
                                   user_opts->destfile,
                                   buffer,
                                   user_opts->buf_size);
        if (total_written >= 0)
        {
            ret = 0;
        }
        else
        {
            ret = -1;
        }
        goto main_out;
    }

    time1 = Wtime();

    /* copying one or more files to a directory */
    fs = fts_open(user_opts->srcv, FTS_COMFOLLOW|FTS_PHYSICAL, NULL);
    if(fs == NULL)
    {
	perror("fts_open");
	ret = -1;
	goto main_out;
    }
    if (!user_opts->recursive)
    {
        /* remove directory contents from the list of items to copy */
        node = fts_children(fs, 0);
        while(node)
        {
            if (node->fts_info == FTS_D)
            {
                fts_set(fs, node, FTS_SKIP);
            }
            node=node->fts_link;
        }
    }
    while((node = fts_read(fs)) != NULL)
    {
        strncpy(dest_name, node->fts_name, node->fts_namelen);
        switch(node->fts_info)
        {
        case FTS_D : /* preorder dir */
            if (user_opts->recursive)
            {
                /* create the directory */
                mkdir(dest_path, 0700);
            }
            else
            {
                fprintf(stderr, "Cannot copy directory %s without -r\n",
                        node->fts_path);
            }
            break;
        case FTS_DP : /* postorder dir */
            if (!user_opts->recursive)
            {
                break;
            }
            /* preserve permissions */
            ret = pvfs_stat_mask(node->fts_name, &sbuf, PVFS_ATTR_SYS_CP);
            if (ret)
            {
                perror("stat");
                goto main_out;
            }

            ret = chmod(dest_path, sbuf.st_mode);
            if (ret)
            {
                perror("chmod");
                goto main_out;
            }
            break;
        case FTS_F : /* reg file */
            /* copy the file */
            if (user_opts->verbose)
            {
                printf("%s\n", node->fts_path);
            }
            total_written += copy_file(node->fts_accpath,
                                       dest_path,
                                       buffer,
                                       user_opts->buf_size);
            break;
        case FTS_SL : /* sym link */
        case FTS_SLNONE : /* sym link - no target */
            /* copy the link */
            readlink(node->fts_name, link_buf, PATH_MAX);
            symlink(link_buf, dest_path);
            break;
        case FTS_DNR : /* can't read dir */
            /* skip with msg */
            fprintf(stderr,
                    "Cannot read directory %s - skipping\n",
                    node->fts_path);
            break;
        case FTS_DC :   /* cycle dir */
        case FTS_DOT :  /* dot dir */
        case FTS_NS :   /* no stat err */
        case FTS_NSOK : /* no stat ok */
        case FTS_ERR :  /* error */
        default:
            fprintf(stderr, "unexpected node type from fts_read\n");
	    ret = -1;
	    goto main_out;
            break;
        }

    }

    fts_close(fs);

    time2 = Wtime();

    if (user_opts->show_timings) 
    {
	print_timings(time2 - time1, total_written);
    }
    
    ret = 0;

main_out:

    free(user_opts);
    free(buffer);
    PVFS_hint_free(hints);

    return(ret);
}

int copy_file(char *srcfile, char *destfile, void *buffer, int buf_size)
{
    int ret = 0;
    int read_size = 0, write_size = 0;
    int64_t total_written = 0;
    int src = -1, dst = -1;
    struct stat sbuf;

    /* PVFS_hint_import_env(&hints); */
    
    src = open(srcfile, O_RDONLY);
    if (src < 0)
    {
	fprintf(stderr, "Could not open source file %s\n", srcfile);
	ret = -1;
	goto err_out;
    }

    /* do these with hints */
    /* user_opts->num_datafiles, user_opts->strip_size, */

    dst = open(destfile, O_CREAT | O_TRUNC | O_RDWR, 0600);
    if (dst < 0)
    {
	fprintf(stderr, "Could not open dest file %s\n", destfile);
	ret = -1;
	goto err_out;
    }

    /* start moving data */
    while((read_size = read(src, buffer, buf_size)) > 0)
    {
	write_size = write(dst, buffer, read_size);
	if (write_size != read_size)
	{
	    if (write_size == -1)
            {
		perror("write");
	    }
            else
            {
		fprintf(stderr, "Error in write\n");
	    }
	    ret = -1;
	    goto err_out;
	}
	total_written += write_size;
    }

    /* preserve permissions */
    if (pvfs_valid_fd(src) > 0)
    {
        ret = pvfs_fstat_mask(src, &sbuf, PVFS_ATTR_SYS_CP);
    }
    else
    {
        ret = fstat(src, &sbuf);
    }
    if (ret)
    {
        perror("fstat");
        goto err_out;
    }

    ret = fchmod(dst, sbuf.st_mode);
    if (ret)
    {
        perror("fchmod");
        goto err_out;
    }

err_out:

    if (dst >= 0)
    {
        close(dst);
    }
    if (src >= 0)
    {
        close(src);
    }

    return total_written;
}

/* parse_args()
 *
 * parses command line arguments
 *
 * returns pointer to options structure on success, NULL on failure
 */
static struct options* parse_args(int argc, char *argv[])
{
    char flags[] = "tVvrs:n:b:";
    int one_opt = 0;
    struct stat sbuf;
    struct options *tmp_opts = NULL;
    int ret = -1;

    /* create storage for the command line options */
    tmp_opts = (struct options *)malloc(sizeof(struct options));
    if(!tmp_opts)
    {
	return(NULL);
    }
    memset(tmp_opts, 0, sizeof(struct options));

    /* fill in defaults (except for hostid) */
    tmp_opts->strip_size = -1;
    tmp_opts->num_datafiles = -1;
    tmp_opts->buf_size = (10 * 1024 * 1024);

    /* look at command line arguments */
    while((one_opt = getopt(argc, argv, flags)) != EOF)
    {
	switch(one_opt)
        {
            case('V'):
                printf("%s\n", PVFS2_VERSION);
                exit(0);
            case('v'):
                tmp_opts->verbose = 1;
                break;
            case('r'):
                tmp_opts->recursive = 1;
                break;
	    case('t'):
		tmp_opts->show_timings = 1;
		break;
	    case('s'):
		ret = sscanf(optarg,
                             SCANF_lld,
                             (SCANF_lld_type *)&tmp_opts->strip_size);
		if(ret < 1)
                {
		    free(tmp_opts);
		    return(NULL);
		}
		break;
	    case('n'):
		ret = sscanf(optarg, "%d", &tmp_opts->num_datafiles);
		if(ret < 1)
                {
		    free(tmp_opts);
		    return(NULL);
		}
		break;
	    case('b'):
		ret = sscanf(optarg, "%d", &tmp_opts->buf_size);
		if(ret < 1)
                {
		    free(tmp_opts);
		    return(NULL);
		}
		break;
	    case('?'):
		usage(argc, argv);
		exit(EXIT_FAILURE);
	}
    }

    if(argc - optind < 2)
    {
        /* need at least two items on command line */
	usage(argc, argv);
	exit(EXIT_FAILURE);
    }

    if (pvfs_valid_path(argv[argc - 1]) > 0)
    {
        ret = pvfs_stat_mask(argv[argc - 1], &sbuf, PVFS_ATTR_SYS_CP);
    }
    else
    {
        ret = stat(argv[argc - 1], &sbuf);
    }
    /* could be file to file or file to dir */
    if (!ret && S_ISDIR(sbuf.st_mode))
    {
        tmp_opts->copy_to_dir = 1;
    }
    else if (argc - optind == 2)
    {
        tmp_opts->copy_to_dir = 0;
    }
    else
    {
        if (ret)
        {
            perror("stat");
        }
	usage(argc, argv);
        exit(EXIT_FAILURE);
    }

    tmp_opts->srcv = argv + optind;

    if (tmp_opts->copy_to_dir)
    {
        if (argv[argc - 1][0] == '/')
        {
            /* absolute path */
            strncpy(dest_path_buffer, argv[argc - 1], PATH_MAX);
            /* code in main ensures there is a trailing slash */
        }
        else
        {
            int len;
            /* relative path */
            getcwd(dest_path_buffer, PATH_MAX);
            if (!tmp_opts->destfile)
            {
                perror("realpath");
                exit(EXIT_FAILURE);
            }
            strncat(dest_path_buffer, "/", 1);
            len = strnlen(argv[argc - 1], PATH_MAX);
            strncat(dest_path_buffer, argv[argc - 1], len);
            /* code in main ensures there is a trailing slash */
        }
        tmp_opts->destfile = dest_path_buffer;
        argv[argc - 1] = NULL; /* keeps fts from seeing this */
    }
    else
    {
        tmp_opts->srcfile = strdup(argv[argc - 2]);
        tmp_opts->destfile = strdup(argv[argc - 1]);
    }

    return(tmp_opts);
}

static void usage(int argc, char **argv)
{
    fprintf(stderr, 
	"Usage: %s ARGS src_file dest_file\n", argv[0]);
    fprintf(stderr, "Where ARGS is one or more of"
	"\n-s <strip_size>\t\t\tsize of access to PVFS2 volume"
	"\n-n <num_datafiles>\t\tnumber of PVFS2 datafiles to use"
	"\n-b <buffer_size in bytes>\thow much data to read/write at once"
	"\n-t\t\t\t\tprint some timing information"
	"\n-v\t\t\t\tprint version number and exit\n");
    return;
}

static double Wtime(void)
{
    struct timeval t;
    gettimeofday(&t, NULL);
    return((double)t.tv_sec + (double)(t.tv_usec) / 1000000);
}

static void print_timings(double time, int64_t total)
{
    printf("Wrote %lld bytes in %f seconds. %f MB/seconds\n",
	    lld(total), time, (total / time) / (1024 * 1024));
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
