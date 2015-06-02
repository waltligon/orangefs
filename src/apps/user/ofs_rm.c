/*
 * (C) 2014 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <errno.h>
#include <fts.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include "orange.h"
#if 0
#include <pvfs2-types.h>
#include <usrint.h>
#include <posix-pvfs.h>
#include <recursive-remove.h>
#endif

/* optional parameters, filled in by parse_args() */
struct options
{
    int force;
    int interactive;
    int recursive;
    int verbose;
    int debug;
    int num_files;
    char **filenames;
};

static int parse_args(int argc, char **argv, struct options *opts);
static void usage(int argc, char **argv);

int main(int argc, char **argv)
{
    int ret = EXIT_FAILURE;
    FTS *fs;
    FTSENT *node;
    unsigned char error_seen = 0;
    struct options user_opts = {0, 0, 0, 0, 0, 0, NULL};
    
    /* look at command line arguments */
    ret = parse_args(argc, argv, &user_opts);
    if(ret < 0)
    {
        fprintf(stderr, "Error: failed to parse command line arguments.\n");
        if(user_opts.filenames)
        {
            free(user_opts.filenames);
        }
        return(-1);
    }

    fs = fts_open(user_opts.filenames, FTS_COMFOLLOW | FTS_PHYSICAL , NULL);
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
    /* process all items in the list to remove */
    while((node = fts_read(fs)) != NULL)
    {
        if (user_opts.debug)
        {
            printf("accpath = %s path = %s level = %d\n",
                   node->fts_accpath, node->fts_path, node->fts_level);
        }
        switch(node->fts_info)
        {
        case FTS_D : /* preorder dir */
            if (!user_opts.recursive)
            {
                error_seen = 1;
                fprintf(stderr,
                        "pvfs2-rm: cannot remove '%s': Is a directory\n",
                        node->fts_path);
            }
            break;
        case FTS_DP : /* postorder dir */
            if (user_opts.recursive)
            {
                if (user_opts.interactive)
                {
                    char response = 0, c = 0;
                    printf("Remove directory %s? (y/n)", node->fts_accpath);
                    fflush(stdout);
                    response = fgetc(stdin);
                    while((c = fgetc(stdin)) != '\n');
                    if (response != 'y' && response != 'Y')
                    {
                        break;
                    }
                }
                if (user_opts.verbose)
                {
                    printf("Removing directory %s\n", node->fts_accpath);
                }
                ret = rmdir(node->fts_accpath);
                if (ret < 0)
                {
                    if ((errno == ENOENT) && !user_opts.force)
                    {
                        break;
                    }
                    perror("rmdir");
                    error_seen = 1;
                    fprintf(stderr,
                            "rmdir failed on path: %s\n",
                            node->fts_accpath);
                }
                break;
            }
            break;
        case FTS_F : /* reg file */
        case FTS_SL : /* sym link */
        case FTS_SLNONE : /* sym link - no target */
            if (user_opts.interactive)
            {
                char response = 0, c;
                printf("Remove file (or link) %s? (y/n)", node->fts_accpath);
                fflush(stdout);
                response = fgetc(stdin);
                while((c = fgetc(stdin)) != '\n');
                if (response != 'y' && response != 'Y')
                {
                    break;
                }
            }
            if (user_opts.verbose)
            {
                printf("Removing file (or link) %s\n", node->fts_accpath);
            }
            ret = unlink(node->fts_accpath);
            if(ret < 0)
            {
                if ((errno == ENOENT) && !user_opts.force)
                {
                    break;
                }
                perror("unlink");
                error_seen = 1;
                fprintf(stderr,
                        "unlink failed on path: %s\n",
                        node->fts_accpath);
                error_seen = 1;
            }
            break;
        case FTS_DNR : /* can't read dir */
            /* skip with msg */
            error_seen = 1;
            fprintf(stderr,
                    "Cannot read directory %s - skipping\n",
                    node->fts_accpath);
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

main_out:

    fts_close(fs);

    if(error_seen)
    {
        ret = EXIT_FAILURE;
    }
    else
    {
        ret = EXIT_SUCCESS;
    }
    if(user_opts.filenames)
    {
        free(user_opts.filenames);
    }
    return ret;
}

/* parse_args()
 *
 * parses command line arguments
 *
 * Returns 0 on success and exits with EXIT_FAILURE on failure.
 */
static int parse_args(int argc, char **argv, struct options *user_opts_p)
{
    int one_opt = 0;
    char flags[] = "vVdirf?";
    opterr = 0;

    while((one_opt = getopt(argc, argv, flags)) != -1)
    {
        switch(one_opt)
        {
            case('V'):
                printf("%s\n", PVFS2_VERSION);
                break;
            case('v'):
                user_opts_p->verbose = 1;
                break;
            case('d'):
                user_opts_p->debug = 1;
                break;
            case('i'):
                if (!user_opts_p->force)
                {
                    user_opts_p->interactive = 1;
                }
                break;
            case('f'):
                user_opts_p->force = 1;
                user_opts_p->interactive = 0;
                break;
            case('r'):
                user_opts_p->recursive = 1;
                break;
            case('?'):
                usage(argc, argv);
                exit(EXIT_FAILURE);
        }
    }

    if (optind < argc)
    {
        int i = 0;
        user_opts_p->num_files = argc - optind;
        user_opts_p->filenames = malloc((argc - optind + 1) * sizeof(char *));
        while (optind < argc)
        {
            user_opts_p->filenames[i] = argv[optind];
            optind++;
            i++;
        }
        user_opts_p->filenames[i] = NULL;
    }
    else
    {
        usage(argc, argv);
        exit(EXIT_FAILURE);
    }
    return 0;
}


static void usage(int argc, char **argv)
{
    fprintf(stderr, "Usage: %s [-vVidrf] pvfs2_filename[s]\n", argv[0]);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

