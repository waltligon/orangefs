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

static int verbose = 0;
static int new_coll_id = 9;
static int new_root_handle = 1048576;
static char ranges[PATH_MAX] = "1047000-1049000";
static char collection[PATH_MAX] = "pvfs2-fs";
static char storage_space[PATH_MAX] = "/tmp/pvfs2-test-space";

static void print_help(char *progname);
static int parse_args(int argc, char **argv);

int main(int argc, char **argv)
{
    if (parse_args(argc, argv) < 0)
    {
	fprintf(stderr,"%s: error: argument parsing failed; "
                "aborting!\n",argv[0]);
	return -1;
    }
    return pvfs2_mkspace(storage_space,
                         collection,
                         new_coll_id,
                         new_root_handle,
                         ranges,
                         0,
                         verbose);
}

static void print_help(char *progname)
{
    fprintf(stderr,"%s version %s\n", progname, PVFS2_VERSION);
    fprintf(stderr,"usage: %s [-s storage_space] [-c collection_name] "
            "[-i coll_id] [-r root_handle] [-R handle_range] [-v]\n",
            progname);
    fprintf(stderr, "\tdefault storage space is '%s'.\n", storage_space);
    fprintf(stderr, "\tdefault initial collection is '%s'.\n", collection);
    fprintf(stderr, "\tdefault collection id is '%d'.\n", new_coll_id);
    fprintf(stderr, "\tdefault root handle is '%d'.\n", new_root_handle);
    fprintf(stderr, "\tdefault handle range is '%s'.\n", ranges);
    fprintf(stderr, "\t'-v' turns on verbose output.\n");
}

static int parse_args(int argc, char **argv)
{
    int c;

    while ((c = getopt(argc, argv, "s:c:i:r:R:vh")) != EOF) {
	switch (c) {
	    case 's':
		strncpy(storage_space, optarg, PATH_MAX);
		break;
	    case 'c': /* collection */
		strncpy(collection, optarg, PATH_MAX);
		break;
	    case 'i':
		new_coll_id = atoi(optarg);
		break;
	    case 'r':
		new_root_handle = atoi(optarg);
		break;
	    case 'R':
		strncpy(ranges,optarg, PATH_MAX);
		break;
	    case 'v':
		verbose = 1;
		break;
            case 'h':
                print_help(argv[0]);
                break;
	    case '?':
                fprintf(stderr, "%s: error: unrecognized "
                        "option '%c'.\n", argv[0], c);
	    default:
                print_help(argv[0]);
		return -1;
	}
    }
    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
