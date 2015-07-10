/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* pvfs2-cp: 
 * 	copy a file from a unix or PVFS2 file system to a unix or PVFS2 file
 * 	system.  Should replace pvfs2-import and pvfs2-export.
 */

#include "orange.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fts.h>
#include <time.h>
#include <libgen.h>
#include <getopt.h>

#include "pint-sysint-utils.h"

#define PVFS_ATTR_SYS_CP (PVFS_ATTR_SYS_TYPE | \
                          PVFS_ATTR_SYS_PERM | \
                          PVFS_ATTR_SYS_DISTDIR_ATTR | \
                          PVFS_ATTR_SYS_LNK_TARGET | \
                          PVFS_ATTR_SYS_DFILE_COUNT | \
                          PVFS_ATTR_SYS_DIRENT_COUNT | \
                          PVFS_ATTR_SYS_BLKSIZE | \
                          PVFS_ATTR_SYS_UID | \
                          PVFS_ATTR_SYS_GID )

/* optional parameters, filled in by parse_args() */
struct options
{
    int strip_size;
    int num_datafiles;
    int buf_size;
    int debug;
    int show_timings;
    int copy_to_dir;
    int verbose;
    int recursive;
    int preserve;
    int mode;
    int total_written;
    char *srcfile;
    char *destfile;
    char **srcv;
};

static char dest_path_buffer[PATH_MAX];

static PVFS_hint hints = NULL;

static int parse_args(int argc, char *argv[], struct options *user_opts);
static void usage(int argc, char **argv);
static double Wtime(void);
static void print_timings(double time, int64_t total);
static int copy_file(char *srcfile,
                     char *destfile,
                     void *buffer,
                     struct options *user_opts);
static int edit_dest_path(char *dst_name,
                          char *src_path,
                          int path_size,
                          int level);

int main (int argc, char **argv)
{
    int ret = 0;
    struct options user_opts;
    double time1 = 0, time2 = 0;
    char link_buf[PATH_MAX];
    char *dest_path = NULL;
    char *dest_name = NULL;
    struct stat sbuf;
    void *buffer = NULL;
    FTS *fs;
    FTSENT *node;

    ret = parse_args(argc, argv, &user_opts);
    if (ret < 0)
    {
	fprintf(stderr, "Error, failed to parse command line arguments\n");
	return(-1);
    }

    buffer = malloc(user_opts.buf_size);
    if(!buffer)
    {
	perror("malloc");
	ret = -1;
	goto main_out;
    }

    if (user_opts.copy_to_dir)
    {
        dest_path = user_opts.destfile;
        dest_name = dest_path + strnlen(dest_path, PATH_MAX);
    }
    else
    {
        /* simple file copy */
        if (user_opts.verbose)
        {
            printf("%s\n", user_opts.srcfile);
        }
        if (user_opts.debug)
        {
            printf("copy single file %s -> %s\n", 
                   user_opts.srcfile,
                   user_opts.destfile);
            ret = 0;
        }
        else
        {
            ret = copy_file(user_opts.srcfile,
                            user_opts.destfile,
                            buffer,
                            &user_opts);
        }
        goto main_out;
    }

    time1 = Wtime();

    /* copying one or more files to a directory */
    fs = fts_open(user_opts.srcv, FTS_COMFOLLOW|FTS_PHYSICAL, NULL);
    if(fs == NULL)
    {
	perror("fts_open");
	ret = -1;
	goto main_out;
    }
    if (!user_opts.recursive)
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
        if (user_opts.debug)
        {
            printf("path = %s level = %d\n", node->fts_path, node->fts_level);
        }
        edit_dest_path(dest_name,
                       node->fts_path,
                       node->fts_pathlen,
                       node->fts_level);

        switch(node->fts_info)
        {
        case FTS_D : /* preorder dir */
            if (user_opts.recursive)
            {
                /* create the directory */
                if (user_opts.debug)
                {
                    char cwd[PATH_MAX];
                    getcwd(cwd, PATH_MAX);
                    printf("making directory %s\n", dest_path);
                    ret = 0;
                }
                else
                {
                    ret = mkdir(dest_path, 0700);
                }
            }
            else
            {
                fprintf(stderr, "Cannot copy directory %s without -r\n",
                        node->fts_path);
            }
            break;
        case FTS_DP : /* postorder dir */
            if (!user_opts.recursive)
            {
                break;
            }
            /* preserve permissions */
            if (user_opts.debug)
            {
                char cwd[PATH_MAX];
                getcwd(cwd, PATH_MAX);
                printf("chmoding directory %s\n", dest_path);
                ret = 0;
            }
            else
            {
                ret = pvfs_stat_mask(dest_path, &sbuf, PVFS_ATTR_SYS_CP);
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
            }
            break;
        case FTS_F : /* reg file */
            /* copy the file */
            if (user_opts.verbose)
            {
                printf("%s\n", node->fts_path);
            }
            if (user_opts.debug)
            {
                char cwd[PATH_MAX];
                getcwd(cwd, PATH_MAX);
                printf("copying file %s in %s to %s\n",
                       node->fts_accpath,
                       cwd,
                       dest_path);
                ret = 0;
            }
            else
            {
                ret = copy_file(node->fts_accpath,
                                dest_path,
                                buffer,
                                &user_opts);
            }
            break;
        case FTS_SL : /* sym link */
        case FTS_SLNONE : /* sym link - no target */
            /* copy the link */
            ret = readlink(node->fts_name, link_buf, PATH_MAX);
            ret = symlink(link_buf, dest_path);
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
            fprintf(stderr, "%s: %s is unknown file type, not present, or not readable\n", argv[0], node->fts_path);
            usage(argc, argv);
	    ret = -1;
            break;
        }
        if (ret < 0)
        {
            goto main_out;
        }
    }

    fts_close(fs);

    time2 = Wtime();

    if (user_opts.show_timings) 
    {
	print_timings(time2 - time1, user_opts.total_written);
    }
    
    ret = 0;

main_out:

    free(buffer);
    PVFS_hint_free(hints);

    return(ret);
}

/* thiis function takes src_path and removes the last n segments and
 * then copies them to dst_name (which already has the dest_dir).  n is
 * derived from level.  works a little differently for dirs and files at
 * level 0.
 */
static int edit_dest_path(char *dst_name,
                          char *src_path,
                          int path_len,
                          int level)
{
    int slashcnt = 0;
    int pc = 0;
    int nc = 0;

    for (pc = path_len; pc; pc--)
    {
        if (src_path[pc] == '/')
        {
            if (++slashcnt > level)
            {
                break;
            }
        }
    }
    if (pc == 0)
    {
        /* this path doesn't have a slash
         * might be a simple file name on the command line
         */
        dst_name[nc++] = '/';
    }
    while(pc < path_len)
    {
        dst_name[nc++] = src_path[pc++];
    }
    dst_name[nc] = 0;
    return 0;
    
}

static int copy_file(char *srcfile,
                     char *destfile,
                     void *buffer,
                     struct options *user_opts)
{
    int ret = 0;
    int read_size = 0, write_size = 0;
    int src = -1, dst = -1;
    struct stat sbuf;
    PVFS_hint hints = PVFS_HINT_NULL;
    int open_flags = O_CREAT | O_TRUNC | O_RDWR;

    /* PVFS_hint_import_env(&hints); */
    
    src = open(srcfile, O_RDONLY);
    if (src < 0)
    {
	fprintf(stderr, "Could not open source file %s\n", srcfile);
	ret = -1;
	goto err_out;
    }

    if (user_opts->num_datafiles > 0)
    {
        PVFS_hint_add(&hints,
                      PVFS_HINT_DFILE_COUNT_NAME,
                      sizeof(int),
                      &user_opts->num_datafiles);
        open_flags |= O_HINTS;
    }

    if (user_opts->strip_size > 0)
    {
        char strip_size_str[22] = {0};
        PVFS_hint_add(&hints,
                      PVFS_HINT_DISTRIBUTION_NAME,
                      14, /* size of the string including null term */
                      "simple_stripe");
        sprintf(strip_size_str, "strip_size:%d", (int)user_opts->strip_size);
        PVFS_hint_add(&hints,
                      PVFS_HINT_DISTRIBUTION_PV_NAME,
                      22, /* size of the buffer */
                      strip_size_str);
        open_flags |= O_HINTS;
    }

    dst = open(destfile, open_flags, 0600, hints);
    if (dst < 0)
    {
	fprintf(stderr, "Could not open dest file %s\n", destfile);
	ret = -1;
	goto err_out;
    }

    /* start moving data */
    while((read_size = read(src, buffer, user_opts->buf_size)) > 0)
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
	user_opts->total_written += write_size;
    }

    /* preserve permissions and-or owner */
    if ((user_opts->mode || user_opts->preserve) && pvfs_valid_fd(src) > 0)
    {
        ret = pvfs_fstat_mask(src, &sbuf, PVFS_ATTR_SYS_CP);
    }
    else
    {
        ret = fstat(src, &sbuf);
    }
    if (ret < 0)
    {
        perror("fstat");
        goto err_out;
    }

    if (user_opts->mode)
    {
        ret = fchmod(dst, sbuf.st_mode);
        if (ret < 0)
        {
            perror("fchmod");
            goto err_out;
        }
    }

    if (user_opts->preserve)
    {
        ret = fchown(dst, sbuf.st_uid, sbuf.st_gid);
        if (ret < 0)
        {
            /* note this should only work if root */
            perror("fchown");
            goto err_out;
        }
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

    return ret;
}

/* parse_args()
 *
 * parses command line arguments
 *
 * returns pointer to options structure on success, NULL on failure
 */
static int parse_args(int argc, char *argv[], struct options *user_opts)
{
    char flags[] = "?mpdtVvrs:n:b:";
    int one_opt = 0;
    struct stat sbuf;
    int ret = -1;

    opterr = 0;

    memset(user_opts, 0, sizeof(struct options));

    /* fill in defaults (except for hostid) */
    user_opts->strip_size = -1;
    user_opts->num_datafiles = -1;
    user_opts->buf_size = (10 * 1024 * 1024);

    /* look at command line arguments */
    while((one_opt = getopt(argc, argv, flags)) != EOF)
    {
	switch(one_opt)
        {
            case('V'):
                printf("%s\n", PVFS2_VERSION);
                exit(0);
            case('v'):
                user_opts->verbose = 1;
                break;
            case('r'):
                user_opts->recursive = 1;
                break;
            case('p'):
                user_opts->preserve = 1;
                break;
            case('m'):
                user_opts->mode = 1;
                break;
	    case('d'):
		user_opts->debug = 1;
		break;
	    case('t'):
		user_opts->show_timings = 1;
		break;
	    case('s'):
		ret = sscanf(optarg,
                             SCANF_lld,
                             (SCANF_lld_type *)&user_opts->strip_size);
		if(ret < 1)
                {
		    return(-1);
		}
		break;
	    case('n'):
		ret = sscanf(optarg, "%d", &user_opts->num_datafiles);
		if(ret < 1)
                {
		    return(-1);
		}
		break;
	    case('b'):
		ret = sscanf(optarg, "%d", &user_opts->buf_size);
		if(ret < 1)
                {
		    return(-1);
		}
		break;
	    case('?'):
            default:
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
        user_opts->copy_to_dir = 1;
    }
    else if (argc - optind == 2)
    {
        user_opts->copy_to_dir = 0;
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

    user_opts->srcv = argv + optind;

    if (user_opts->copy_to_dir)
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
            if (!user_opts->destfile)
            {
                perror("realpath");
                exit(EXIT_FAILURE);
            }
            strncat(dest_path_buffer, "/", 1);
            len = strnlen(argv[argc - 1], PATH_MAX);
            strncat(dest_path_buffer, argv[argc - 1], len);
            /* code in main ensures there is a trailing slash */
        }
        user_opts->destfile = dest_path_buffer;
        argv[argc - 1] = NULL; /* keeps fts from seeing this */
    }
    else
    {
        user_opts->srcfile = strdup(argv[argc - 2]);
        user_opts->destfile = strdup(argv[argc - 1]);
    }

    return(0);
}

static void usage(int argc, char **argv)
{
    fprintf(stderr, 
	"Usage: %s OPTS src_file dest_file\n", argv[0]);
    fprintf(stderr, 
	"Or:    %s OPTS src_file(s) dest_dir\n", argv[0]);
    fprintf(stderr, "Where OPTS is one or more of:"
	"\n-s <strip_size>\t\t\tsize of access to PVFS2 volume"
	"\n-n <num_datafiles>\t\tnumber of PVFS2 datafiles to use"
	"\n-b <buffer_size in bytes>\thow much data to read/write at once"
        "\n-r\t\t\t\trecursively copy directories"
        "\n-m\t\t\t\tpreserve mode of the files"
        "\n-p\t\t\t\tpreserve owner of the files (requires root)"
        "\n-v\t\t\t\tverbose - print path of files as the are copied"
        "\n-d\t\t\t\tprint program debugging information"
	"\n-t\t\t\t\tprint some timing information"
	"\n-?\t\t\t\tprint this message"
	"\n-V\t\t\t\tprint version number and exit\n");
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
