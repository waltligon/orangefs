/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* ofs_cp: 
 *     copy a file from a unix or OFS file system to a unix or OFS file
 *     system.
 */

#include "orange.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <getopt.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fts.h>
#include <time.h>
#include <libgen.h>

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

#define OFS_COPY_BUFSIZE_DEFAULT (10 * 1024 * 1024 )

/* optional parameters, filled in by parse_args() */
struct cp_options
{
    int strip_size;
    int num_datafiles;
    int buf_size;
    int debug;
    int show_timings;
    int copy_to_dir;
    int copy_1to1;
    int verbose;
    int recursive;
    int preserve;
    int mode;
    int times;
    int total_written;
    int layout;
    char *server_list;
    char *srcfile;
    char *destfile;
    char *created;
    char **srcv;
};

static char dest_path_buffer[PATH_MAX];

static PVFS_hint hints = NULL;

static int parse_args(int argc, char *argv[], struct cp_options *user_opts);
static void usage(int argc, char **argv);
static double Wtime(void);
static void print_timings(double time, int64_t total);
static int copy_file(char *srcfile,
                     char *destfile,
                     void *buffer,
                     struct cp_options *user_opts);
static void edit_dest_path(char *dst_name,
                           char *src_path,
                           int path_size,
                           int level,
                           struct cp_options *user_opts);

int main (int argc, char **argv)
{
    int ret = 0;
    struct cp_options user_opts;
    double time1 = 0, time2 = 0;
    char link_buf[PATH_MAX];
    char *dest_path = NULL; /* the path to the destination dir for all cp */
    char *dest_name = NULL; /* the file name of a dest for a specific cp */
    struct stat sbuf;
    void *buffer = NULL;
    FTS *fs;
    FTSENT *node;

    memset((void *)&user_opts, 0, sizeof(struct cp_options));

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
            printf("copying %s ...\n", user_opts.srcfile);
        }
        if (user_opts.debug)
        {
            fprintf(stderr, "copy single file %s -> %s\n", 
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
        int adj; /* level adjustment for various situations */

        if (user_opts.debug)
        {
            fprintf(stderr,
                    "path = %s level = %d\n",
                    node->fts_path,
                    node->fts_level);
        }
        /* corrects for dir copied to non-existing path */
        adj = (user_opts.copy_1to1 && user_opts.created) ? -1 : 0;
        /* corrects for slash on end of dir path */
        adj += (node->fts_path[node->fts_pathlen - 1] == '/') ? 1 : 0;
        /* This builds the complete destination path and file name */
        edit_dest_path(dest_name,
                       node->fts_path,
                       node->fts_pathlen,
                       node->fts_level + adj,
                       &user_opts);

        switch(node->fts_info)
        {
        case FTS_D : /* preorder dir */
            if (user_opts.recursive)
            {
                if (!(user_opts.copy_1to1 && user_opts.created) ||
                     node->fts_level > 0)
                {
                    /* create the directory */
                    if (user_opts.debug || user_opts.verbose)
                    {
                        fprintf(stderr, "making directory %s ...\n", dest_path);
                        ret = 0;
                    }
                    ret = mkdir(dest_path, 0700);
                    if (ret < 0)
                    {
                        fprintf(stderr,
                                "Cannot create directory %s\n",
                                dest_path);
                    }
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

            if ((user_opts.mode || user_opts.preserve || user_opts.times) &&
                (!user_opts.copy_1to1 || node->fts_level > 0))
            {
                /* preserve permissions */
                if (user_opts.debug)
                {
                    fprintf(stderr, "chmoding directory %s ...\n", dest_path);
                    ret = 0;
                }

                if (!user_opts.times && 
                    (pvfs_valid_path(node->fts_accpath) > 0))
                {
                    /* this is faster by skiping times and sizes */
                    ret = pvfs_stat_mask(node->fts_accpath,
                                         &sbuf,
                                         PVFS_ATTR_SYS_CP);
                }
                else
                {
                    ret = stat(node->fts_accpath, &sbuf);
                }
                if (ret)
                {
                    perror("stat");
                    goto main_out;
                }

                if (user_opts.mode)
                {
                    ret = chmod(dest_path, sbuf.st_mode);
                    if (ret)
                    {
                        perror("chmod");
                        goto main_out;
                    }
                }

                if (user_opts.preserve)
                {
                    ret = chown(dest_path, sbuf.st_uid, sbuf.st_gid);
                    if (ret)
                    {
                        perror("chown");
                        goto main_out;
                    }
                }

                if (user_opts.times)
                {
                    struct utimbuf times;
             
                    times.actime = sbuf.st_atime;
                    times.modtime = sbuf.st_mtime;
            
                    ret = utime(dest_path, &times);
                    if (ret < 0)
                    {
                        perror("utime");
                        goto main_out;
                    }
                }
            }
            break;
        case FTS_F : /* reg file */
            /* copy the file */
            if (user_opts.verbose)
            {
                fprintf(stderr, "copying %s ...\n", node->fts_path);
            }
            if (user_opts.debug)
            {
                char *ret;
                char cwd[PATH_MAX];
                ret = getcwd(cwd, PATH_MAX);
                if (!ret)
                {
                    cwd[0] = 0;
                }
                fprintf(stderr, "copying file %s in %s to %s\n",
                       node->fts_accpath,
                       cwd,
                       dest_path);
                ret = 0;
            }
            ret = copy_file(node->fts_accpath, dest_path, buffer, &user_opts);
            if (ret < 0)
            {
                fprintf(stderr,
                        "Error copying file %s\n",
                        node->fts_accpath);
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

/* this function takes src_path and removes the last n segments and
 * then copies them to dst_name (which already has the dest_dir).  n is
 * derived from level.  works a little differently for dirs and files at
 * level 0.
 */
static void edit_dest_path(char *dst_name,
                          char *src_path,
                          int path_len,
                          int level,
                          struct cp_options *user_opts)
{
    int slashcnt = 0;
    int pc = 0;
    int nc = 0;

    if (user_opts->debug)
    {
        fprintf(stderr,
                "edit_dest_path name:%s path:%s len:%d level:%d\n",
                dst_name,
                src_path,
                path_len,
                level);
    }

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
    if (user_opts->debug)
    {
        fprintf(stderr,
                "        edit_dest_path dst_name:%s\n",
                dst_name);
    }
}

static int copy_file(char *srcfile,
                     char *destfile,
                     void *buffer,
                     struct cp_options *user_opts)
{
    int ret = 0;
    int read_size = 0, write_size = 0;
    int src = -1, dst = -1;
    struct stat sbuf;
    PVFS_hint hints = PVFS_HINT_NULL;
    int open_flags = O_CREAT | O_TRUNC | O_RDWR;

    /* PVFS_hint_import_env(&hints); */
    
    src = open(srcfile, O_RDONLY);
    if (src == -1)
    {
        perror("open src");
        /* fprintf(stderr, "Could not open source file %s\n", srcfile); */
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

    if (user_opts->layout > 1 && user_opts->layout <= PVFS_SYS_LAYOUT_MAX)
    {
        int list_len = 0;
        if (user_opts->layout != PVFS_SYS_LAYOUT_LIST ||
            (user_opts->server_list &&
             (list_len = strlen(user_opts->server_list)) >= 1))
        {
            PVFS_hint_add(&hints,
                          PVFS_HINT_LAYOUT_NAME,
                          sizeof(int),
                          &user_opts->layout);
            if (user_opts->layout == PVFS_SYS_LAYOUT_LIST)
            {
                PVFS_hint_add(&hints,
                              PVFS_HINT_SERVERLIST_NAME,
                              list_len + 1,
                              user_opts->server_list);
            }
        }
        open_flags |= O_HINTS;
    }

    dst = open(destfile, open_flags, 0600, hints);
    if (dst == -1)
    {
        perror("open dst");
        /* fprintf(stderr, "Could not open dest file %s\n", destfile); */
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

    /* preserve permissions and/or owner */
    if (user_opts->mode || user_opts->preserve || user_opts->times)
    {
        if (user_opts->debug)
        {
            fprintf(stderr, "preserving file metadata\n");
        }
        if (!user_opts->times &&
            (pvfs_valid_fd(src) > 0))
        {
            /* this is faster by skiping times and sizes */
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

        if (user_opts->mode) /* preserve permissions */
        {
            if (user_opts->debug)
            {
                fprintf(stderr, "         preserving file permissions\n");
            }
            ret = fchmod(dst, sbuf.st_mode);
            if (ret < 0)
            {
                perror("fchmod");
                goto err_out;
            }
        }

        if (user_opts->preserve)
        {
            if (user_opts->debug)
            {
                fprintf(stderr, "         preserving file owner/group\n");
            }
            ret = fchown(dst, sbuf.st_uid, sbuf.st_gid);
            if (ret < 0)
            {
                /* note this should only work if root */
                perror("fchown");
                goto err_out;
            }
        }

        if (user_opts->times)
        {
            struct utimbuf times;

            if (user_opts->debug)
            {
                fprintf(stderr, "         preserving file A/M times\n");
            }

            times.actime = sbuf.st_atime;
            times.modtime = sbuf.st_mtime;

            ret = utime(destfile, &times);
            if (ret < 0)
            {
                perror("utime");
                goto err_out;
            }
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
static int parse_args(int argc, char *argv[], struct cp_options *user_opts)
{
    const char flags[] = "hmptDTVvrs:n:b:l:L:";
    int one_opt = 0;
    struct stat s_sbuf, d_sbuf;
    int ret = -1, s_ret = -1, d_ret = -1;
    int index = 1; /* stand-in for optind */

    static struct option lopt[] = {
        {"mode", 0, NULL, 'm'},
        {"preserve", 0, NULL, 'p'},
        {"times", 0, NULL, 't'},
        {"Debug", 0, NULL, 'D'},
        {"TIming", 0, NULL, 'T'},
        {"strip-size", 0, NULL, 's'},
        {"num-datafiles", 0, NULL, 'n'},
        {"buffer-size", 0, NULL, 'b'},
        {"layout", 0, NULL, 'l'},
        {"server-list", 0, NULL, 'L'},
        {"recursive", 0, NULL, 'r'},
        {"help", 0, NULL, 'h'},
        {"Version", 0, NULL, 'V'},
        {"verbose", 0, NULL, 'v'},
        {NULL, 0, NULL, 0}
    };

    opterr = 0;

    memset(user_opts, 0, sizeof(struct cp_options));

    /* fill in defaults */
    user_opts->strip_size = -1;
    user_opts->num_datafiles = -1;
    user_opts->buf_size = OFS_COPY_BUFSIZE_DEFAULT;

    /* look at command line arguments */
    while((one_opt = getopt_long(argc, argv, flags, lopt, NULL)) != -1)
    {
        index++;
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
            case('t'):
                user_opts->times = 1;
                break;
            case('m'):
                user_opts->mode = 1;
                break;
            case('D'):
                user_opts->debug = 1;
                break;
            case('T'):
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
                index++;
                break;
            case('n'):
                ret = sscanf(optarg, "%d", &user_opts->num_datafiles);
                if(ret < 1)
                {
                    return(-1);
                }
                index++;
                break;
            case('b'):
                ret = sscanf(optarg, "%d", &user_opts->buf_size);
                if(ret < 1)
                {
                    return(-1);
                }
                index++;
                break;
            case('l'):
                ret = sscanf(optarg, "%d", &user_opts->layout);
                if(ret < 1)
                {
                    return(-1);
                }
                index++;
                break;
            case('L'):
                user_opts->server_list = strdup(optarg);
                if (!user_opts->server_list)
                {
                    return(-1);
                }
                index++;
                break;
            case('h'):
            default:
                goto exit_err;
                break;
        }
    }

    /* optind not working for some weird reason */

    if(argc - index < 2)
    {
        /* need at least two items on command line */
        goto exit_err;
    }

    /* stat the last argument (dest).
     * if it exists and is a directory, then copy-to-dir
     * if it exists and is a file, then copy-to-file
     * if it does not exist, then  
     *       if there are two arguments, check type of first
     *             if it does not exist, error
     *             if it exists then dest is type of source - create dest
     *       if there are more then two arguments, copy-to-dir - create dest
     */

    /* Stat Destination File */
    if (pvfs_valid_path(argv[argc - 1]) > 0)
    {
        d_ret = pvfs_stat_mask(argv[argc - 1], &d_sbuf, PVFS_ATTR_SYS_CP);
    }
    else
    {
        d_ret = stat(argv[argc - 1], &d_sbuf);
    }
    if (d_ret && (errno != ENOENT || argc - index > 2))
    {
        /* error: failed to find dest file or wrong type */
        perror("stat dest");
        goto exit_err;
    }
    /* clear errno */
    errno = 0;

    /* Stat Source File */
    if (pvfs_valid_path(argv[argc - 2]) > 0)
    {
        s_ret = pvfs_stat_mask(argv[argc - 2], &s_sbuf, PVFS_ATTR_SYS_CP);
    }
    else
    {
        s_ret = stat(argv[argc - 2], &s_sbuf);
    }
    if (s_ret)
    {
        /* even ENOENT is a problem for the source */
        /* error: failed to find source file or wrong type */
        perror("stat src");
        goto exit_err;
    }
    if (argc - index == 2)
    {
        user_opts->copy_1to1 = 1;
    }
    else
    {
        user_opts->copy_1to1 = 0;
    }

    /* could be file to file, dir to dir, or file to dir */
    if (!d_ret && S_ISDIR(d_sbuf.st_mode))
    {
        user_opts->copy_to_dir = 1;
    }
    else if (!d_ret && S_ISREG(d_sbuf. st_mode))
    {
        user_opts->copy_to_dir = 0; /* copy to file */
    }
    else if (d_ret) /* assume errno was ENOENT */
    {
        /* dest dir or file does not exist */
        if (!user_opts->copy_1to1)
        {
            /* many to one, ERROR */
            errno = EBADF;
            perror("cp");
            goto exit_err;
        }
        else
        {
            /* make destination same as source */
            /* we know s_ret is 0 from above */
            if (S_ISDIR(s_sbuf.st_mode))
            {
                if (user_opts->recursive)
                {
                    user_opts->copy_to_dir = 1;
                }
                /* create dest dir */
                s_ret = mkdir(argv[argc - 1], s_sbuf.st_mode);
                user_opts->created = argv[argc - 1];
                if (s_ret)
                {
                    perror("mkdir");
                    goto exit_err;
                }
            }
            else if (S_ISREG(s_sbuf.st_mode))
            {
                user_opts->copy_to_dir = 0;
                /* create dest file */
                s_ret = creat(argv[argc - 1], s_sbuf.st_mode);
                user_opts->created = argv[argc - 1];
                if (s_ret < 0)
                {
                    perror("creat");
                    goto exit_err;
                }
                close(s_ret);
            }
            else
            {
                /* not a file or dir */
                errno = EINVAL;
                perror("cp");
                goto exit_err;
            }
        }
    }
    else
    {
        /* not a file or dir or an error on stat */
        perror("stat dest");
        goto exit_err;
    }

    /* These are passed to FTS for copy_to_dir */
    user_opts->srcv = argv + index;

    /* for copy to dir we will set up the destination path in
     * dest_path_buffer here and the target path is formed by adding the
     * source path into it for each file in edit_dest_path()
     */
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
            char *ret;
            int len;
            /* relative path */
            ret = getcwd(dest_path_buffer, PATH_MAX);
            if (!ret)
            {
                perror("getcwd");
                goto exit_err;
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
        /* This is for one-to-one file case just set up the arguments as
         * provided on the command line
         */
        user_opts->srcfile = strdup(argv[argc - 2]);
        user_opts->destfile = strdup(argv[argc - 1]);
    }

    return(0);

exit_err:

    if (user_opts->created)
    {
        if (user_opts->copy_to_dir)
        {
            rmdir(user_opts->created);
        }
        else
        {
            unlink(user_opts->created);
        }
    }
    usage(argc, argv);
    exit(EXIT_FAILURE);
}

static void usage(int argc, char **argv)
{
    fprintf(stderr, 
        "Usage: %s OPTS src_file dest_file\n", argv[0]);
    fprintf(stderr, 
        "Or:    %s OPTS src_file(s) dest_dir\n", argv[0]);
    fprintf(stderr, "Where OPTS is one or more of:"
        "\n-s <strip_size>           size of access to PVFS2 volume"
        "\n-n <num_datafiles>        number of PVFS2 datafiles to use"
        "\n-b <buffer_size in bytes> how much data to read/write at once"
        "\n-l <layout number>        layout algorithm to use"
        "\n-L <colon delimited ints> list of servers for LIST layout"
        "\n-r                        recursively copy directories"
        "\n-m                        preserve mode of the files"
        "\n-p                        preserve owner of the files (requires root)"
        "\n-t                        preserve atime/mtime of the files"
        "\n-v                        verbose - print path of files as the are copied"
        "\n-D                        print program debugging information"
        "\n-T                        print some timing information"
        "\n-?                        print this message"
        "\n-V                        print version number and exit\n");
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
