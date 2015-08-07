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
#include <getopt.h>
#include "orange.h"

/* optional parameters, filled in by parse_args() */
struct rm_options
{
    int force;
    int interactive;
    int recursive;
    int verbose;
    int debug;
    int num_files;
    char **filenames;
};

static int parse_args(int argc, char **argv, struct rm_options *opts);
static void usage(int argc, char **argv);

int main(int argc, char **argv)
{
    int ret = EXIT_FAILURE;
    FTS *fs;
    FTSENT *node;
    unsigned char error_seen = 0;
    struct rm_options user_opts = {0, 0, 0, 0, 0, 0, NULL};
    
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
            fprintf(stderr, "%s: %s is not a removable file, directory, or symbolic link\n", argv[0], node->fts_path);
            usage(argc, argv);
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
static int parse_args(int argc, char **argv, struct rm_options *user_opts_p)
{
    int one_opt = 0;
    char flags[] = "vVdirfh";
    int index = 1; /* stand in for optind */

    static struct option lopt[] = {
        {"recursive", 0, NULL, 'r'},
        {"interactive", 0, NULL, 'i'},
        {"force", 0, NULL, 'f'},
        {"help", 0, NULL, 'h'},
        {"Version", 0, NULL, 'V'},
        {"verbose", 0, NULL, 'v'},
        {"debug", 0, NULL, 'd'},
        {NULL, 0, NULL, 0}
    };

    opterr = 0;

    while((one_opt = getopt_long(argc, argv, flags, lopt, NULL)) != -1)
    {
        index++;
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
            case('h'):
            default:
                usage(argc, argv);
                exit(EXIT_FAILURE);
                break;
        }
    }

    /* optind is broken, use index */
    if (index < argc)
    {
        /* there are arguments - file names - to process */
        int i = 0;
        user_opts_p->num_files = argc - index;
        user_opts_p->filenames = malloc((argc - index + 1) * sizeof(char *));
        while (index < argc)
        {
            user_opts_p->filenames[i] = argv[index];
            index++;
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
    fprintf(stderr, "Usage: %s [-?vVidrf] pvfs2_filename[s]\n", argv[0]);
    fprintf(stderr, "\t-h\thelp - print this message\n");
    fprintf(stderr, "\t-v\tverbose - print informative messages\n");
    fprintf(stderr, "\t-V\tVersion - print Version info\n");
    fprintf(stderr, "\t-i\tinteractive - ask before removing each item\n");
    fprintf(stderr, "\t-f\tforce - print nothing\n");
    fprintf(stderr, "\t-d\tdebug - print debugging info\n");
    fprintf(stderr, "\t-r\trecursive - remove directories and their contents\n");
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

