/*
 * (C) 2014 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include "orange.h"
#include "recursive-remove.h"

/* optional parameters, filled in by parse_args() */
struct options
{
    int force;
    int recursive;
    int num_files;
    char **filenames;
};

static int parse_args(int argc, char **argv, struct options *opts);
static void usage(int argc, char **argv);

int main(int argc, char **argv)
{
    int ret = EXIT_FAILURE;
    int i = 0;
    unsigned char error_seen = 0;
    struct options user_opts = {0, 0, 0, 0};
    
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

    /* Remove each specified file (and dirs w/ -r) */
    for (i = 0; i < user_opts.num_files; ++i)
    {
        struct stat buf;
        char *working_path = user_opts.filenames[i];

        /* TODO: use different version of stat if we want to also work with
         * non-pvfs2 paths. */
        /* Calling stat in this way only asks for the type of the object at
         * working_path. */
        ret = pvfs_lstat_mask(working_path, &buf, PVFS_ATTR_SYS_TYPE);
        if(ret < 0)
        {
            if(!user_opts.force)
            {
                fprintf(stderr,
                        "pvfs2-rm: cannot remove '%s': "
                        "No such file or directory\n",
                        working_path);
                error_seen = 1;
            }
            continue;
        }
        if(S_ISDIR(buf.st_mode))
        {
            RR_PRINT("working path is directory: %s\n", working_path);
            
            /* If recursive isn't specified, then throw error. */
            /* Note: if other paths are specified (and they are files)
             * then they should be removed. 1 should still be returned
             * to indicate an error was encountered (removing the dir
             * since recursive wasn't specified). */
            if(user_opts.recursive)
            {
                ret = recursive_delete_dir(working_path);
                if(ret < 0)
                {
                    fprintf(stderr,
                            "recursive_delete_dir failed on path: %s\n",
                            working_path);
                    error_seen = 1;
                }
            }
            else
            {
                fprintf(stderr,
                        "pvfs2-rm: cannot remove '%s': Is a directory\n",
                        working_path);
                error_seen = 1;
            }
        }
        else
        {
            ret = unlink(working_path);
            if(ret < 0)
            {
                perror("unlink failed: ");
                fprintf(stderr, "unlink failed on path: %s\n", working_path);
                error_seen = 1;
            }
        }
    }
    
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
    char flags[] = "rf?";
    opterr = 0;

    while((one_opt = getopt(argc, argv, flags)) != -1)
    {
        switch(one_opt)
        {
            case('f'):
                user_opts_p->force = 1;
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
        user_opts_p->filenames = malloc((argc - optind) * sizeof(char*));
        while (optind < argc)
        {
            user_opts_p->filenames[i] = argv[optind];
            optind++;
            i++;
        }
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
    fprintf(stderr, "Usage: %s [-rf] pvfs2_filename[s]\n", argv[0]);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

