/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <getopt.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>

#include "pvfs2.h"
#include "mkspace.h"

#ifndef PVFS2_VERSION
#define PVFS2_VERSION "Unknown"
#endif

typedef struct
{
    int use_defaults;
    int verbose;
    int coll_id;
    int root_handle;
    int collection_only;
    int delete_storage;
    char meta_ranges[PATH_MAX];
    char data_ranges[PATH_MAX];
    char collection[PATH_MAX];
    char storage_space[PATH_MAX];
} options_t;

static int default_verbose = 0;
static int default_coll_id = 9;
static int default_root_handle = 1048576;
static int default_collection_only = 0;
static char default_meta_ranges[PATH_MAX] = "4-2147483650";
static char default_data_ranges[PATH_MAX] = "2147483651-4294967297";
static char default_collection[PATH_MAX] = "pvfs2-fs";
static char default_storage_space[PATH_MAX] = "/pvfs2-storage-space";

static void print_help(char *progname, options_t *opts);

static int parse_args(int argc, char **argv, options_t *opts)
{
    int ret = 0, option_index = 0;
    char *cur_option = NULL;
    static struct option long_opts[] =
    {
        {"help",0,0,0},
        {"version",0,0,0},
        {"verbose",0,0,0},
        {"defaults",0,0,0},
        {"storage-space",1,0,0},
        {"coll-id",1,0,0},
        {"coll-name",1,0,0},
        {"root-handle",1,0,0},
        {"delete-storage",0,0,0},
        {"meta-handle-range",1,0,0},
        {"data-handle-range",1,0,0},
        {"add-coll",0,0,0},
        {0,0,0,0}
    };

    if (argc == 1)
    {
        print_help(argv[0], opts);
        exit(1);
    }

    while ((ret = getopt_long(argc, argv, "s:c:i:r:vVhadDM:N:",
                              long_opts, &option_index)) != -1)
    {
	switch (ret)
        {
            case 0:
                cur_option = (char *)long_opts[option_index].name;

                if (strcmp("help", cur_option) == 0)
                {
                    goto do_help;
                }
                else if (strcmp("version", cur_option) == 0)
                {
                    goto do_version;
                }
                else if (strcmp("verbose", cur_option) == 0)
                {
                    goto do_verbose;
                }
                else if (strcmp("storage-space", cur_option) == 0)
                {
                    goto do_storage_space;
                }
                else if (strcmp("coll-id", cur_option) == 0)
                {
                    goto do_collection_id;
                }
                else if (strcmp("coll-name", cur_option) == 0)
                {
                    goto do_collection_name;
                }
                else if (strcmp("root-handle", cur_option) == 0)
                {
                    goto do_root_handle;
                }
                else if (strcmp("meta-handle-range", cur_option) == 0)
                {
                    goto do_meta_handle_range;
                }
                else if (strcmp("data-handle-range", cur_option) == 0)
                {
                    goto do_data_handle_range;
                }
                else if (strcmp("add-coll", cur_option) == 0)
                {
                    goto do_add_collection;
                }
                else if (strcmp("defaults", cur_option) == 0)
                {
                    goto do_defaults;
                }
                else if (strcmp("delete-storage", cur_option) == 0)
                {
                    goto do_delete_storage;
                }
                else
                {
                    print_help(argv[0], opts);
                    exit(1);
                }
	    case 'a':
          do_add_collection:
		opts->collection_only = 1;
		break;
	    case 'c':
          do_collection_name:
		strncpy(opts->collection, optarg, PATH_MAX);
		break;
            case 'd':
          do_defaults:
                opts->use_defaults = 1;
                break;
            case 'h':
          do_help:
                print_help(argv[0], opts);
                exit(0);
	    case 'i':
          do_collection_id:
		opts->coll_id = atoi(optarg);
		break;
	    case 'r':
          do_root_handle:
		opts->root_handle = atoi(optarg);
		break;
	    case 'M':
          do_meta_handle_range:
		strncpy(opts->meta_ranges, optarg, PATH_MAX);
		break;
	    case 'N':
          do_data_handle_range:
		strncpy(opts->data_ranges, optarg, PATH_MAX);
		break;
	    case 's':
          do_storage_space:
		strncpy(opts->storage_space, optarg, PATH_MAX);
		break;
	    case 'v':
          do_verbose:
		opts->verbose = PVFS2_MKSPACE_STDERR_VERBOSE;
		break;
            case 'V':
          do_version:
                fprintf(stderr,"%s\n",PVFS2_VERSION);
                exit(0);
            case 'D':
          do_delete_storage:
                opts->delete_storage = 1;
                break;
	    case '?':
                fprintf(stderr, "%s: error: unrecognized "
                        "option '%c'.\n", argv[0], ret);
	    default:
                print_help(argv[0], opts);
		return -1;
	}
    }
    return 0;
}

static void print_options(options_t *opts)
{
    if (opts)
    {
        printf("\tuse all defaults    : %s\n",
               (opts->use_defaults ? "yes" : "no"));
        printf("\tdelete storage      : %s\n",
               (opts->delete_storage ? "yes" : "no"));
        printf("\tverbose             : %s\n",
               (opts->verbose ? "ON" : "OFF"));
        printf("\troot handle         : %d\n", opts->root_handle);
        printf("\tcollection-only mode: %s\n",
               (opts->collection_only ? "ON" : "OFF"));
        printf("\tcollection id       : %d\n", opts->coll_id);
        printf("\tcollection name     : %s\n", opts->collection);
        printf("\tmeta handle ranges  : %s\n", opts->meta_ranges);
        printf("\tdata handle ranges  : %s\n", opts->data_ranges);
        printf("\tstorage space       : %s\n", opts->storage_space);
    }
}

static void print_help(char *progname, options_t *opts)
{
    fprintf(stderr,"usage: %s [OPTION]...\n", progname);
    fprintf(stderr,"This program is useful for creating a new pvfs2 "
            "storage space with a \nsingle collection, or adding a "
            "new collection to an existing storage space.\n\n");
    fprintf(stderr,"The following arguments can be used to "
           "customize your volume:\n");

    fprintf(stderr,"  -a, --add-coll                       "
            "used to add a collection only\n");
    fprintf(stderr,"  -c, --coll-name                      "
            "create collection with the speciifed name\n");
    fprintf(stderr,"  -d, --defaults                       "
            "use all default options (see below)\n");
    fprintf(stderr,"  -D, --delete-storage                 "
            "REMOVE the storage space (unrecoverable!)\n");
    fprintf(stderr,"  -h, --help                           "
            "show this help listing\n");
    fprintf(stderr,"  -i, --coll-id=ID                     "
            "create collection with the specified id\n");
    fprintf(stderr,"  -r, --root-handle=HANDLE             "
            "create collection with this root handle\n");
    fprintf(stderr,"  -M, --meta-handle-range=RANGE        "
            "create collection with the specified\n        "
            "                                meta handle range\n");
    fprintf(stderr,"  -N, --data-handle-range=RANGE        "
            "create collection with the specified\n        "
            "                                data handle range\n");
    fprintf(stderr,"  -s, --storage-space=PATH             "
            "create storage space at this location\n");
    fprintf(stderr,"  -v, --verbose                        "
            "operate in verbose mode\n");
    fprintf(stderr,"  -V, --version                        "
            "print version information and exit\n");

    fprintf(stderr,"\nIf the -d or --defaults option is used, the "
            "following options will be used:\n");
    opts->use_defaults = 1;
    print_options(opts);
}

int main(int argc, char **argv)
{
    int ret = -1;
    options_t opts;
    memset(&opts, 0, sizeof(options_t));

    opts.verbose = default_verbose;
    opts.coll_id = default_coll_id;
    opts.root_handle = default_root_handle;
    opts.collection_only = default_collection_only;
    strncpy(opts.meta_ranges, default_meta_ranges, PATH_MAX);
    strncpy(opts.data_ranges, default_data_ranges, PATH_MAX);
    strncpy(opts.collection, default_collection, PATH_MAX);
    strncpy(opts.storage_space, default_storage_space, PATH_MAX);

    if (parse_args(argc, argv, &opts))
    {
	fprintf(stderr,"%s: error: argument parsing failed; "
                "aborting!\n",argv[0]);
	return -1;
    }

    if (opts.use_defaults)
    {
        opts.verbose = default_verbose;
        opts.coll_id = default_coll_id;
        opts.root_handle = default_root_handle;
        opts.collection_only = default_collection_only;
        strncpy(opts.meta_ranges, default_meta_ranges, PATH_MAX);
        strncpy(opts.data_ranges, default_data_ranges, PATH_MAX);
        strncpy(opts.collection, default_collection, PATH_MAX);
        strncpy(opts.storage_space, default_storage_space, PATH_MAX);
    }

    print_options(&opts);

    if (opts.delete_storage)
    {
        ret = pvfs2_rmspace(opts.storage_space, opts.collection,
                            opts.coll_id, opts.collection_only,
                            opts.verbose);
    }
    else
    {
        ret = pvfs2_mkspace(opts.storage_space, opts.collection,
                            opts.coll_id, opts.root_handle,
                            opts.meta_ranges, opts.data_ranges,
                            opts.collection_only, opts.verbose);
    }
    return ret;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
