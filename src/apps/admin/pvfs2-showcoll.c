/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "pvfs2-config.h"

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <getopt.h>
#include <limits.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include "pvfs2.h"
#include "trove.h"
#include "pvfs2-attr.h"
#include "pvfs2-internal.h"

/* declare the strnlen prototype */
size_t strnlen(const char *s, size_t limit);

static char storage_space[PATH_MAX] = "/tmp/pvfs2-test-space";
static char collection[PATH_MAX];
static int verbose = 0, got_collection = 0, print_keyvals = 0, got_dspace_handle = 0;
TROVE_handle dspace_handle;

static int parse_args(int argc,
		      char **argv);
static int print_collections(void);
static int print_dspaces(TROVE_coll_id coll_id,
			 TROVE_handle root_handle,
                         TROVE_context_id trove_context,
			 int no_root_handle);
static int print_dspace(TROVE_coll_id coll_id,
			TROVE_handle handle,
                        TROVE_context_id trove_context);
static int print_dspace_keyvals(TROVE_coll_id coll_id,
				TROVE_handle handle,
                                TROVE_context_id trove_context,
				TROVE_ds_type type);
static char *type_to_string(TROVE_ds_type type);
static int print_keyval_pair(TROVE_keyval_s *key,
			     TROVE_keyval_s *val,
			     TROVE_ds_type type,
			     int sz);
static void print_object_attributes(struct PVFS_object_attr *a_p);
static void print_datafile_handles(PVFS_handle *h_p,
				   int sz);

int main(int argc, char **argv)
{
    int ret, count, no_root_handle = 0;
    TROVE_op_id op_id;
    TROVE_coll_id coll_id;
    TROVE_handle root_handle;
    TROVE_ds_state state;
    TROVE_keyval_s key, val;
    TROVE_context_id trove_context = -1;
    char *method_name;
    char root_handle_string[] = "root_handle"; /* TODO: DEFINE ELSEWHERE? */

    ret = parse_args(argc, argv);
    if (ret < 0) {
	fprintf(stderr,
		"%s: error: argument parsing failed; aborting!\n",
		argv[0]);
	return -1;
    }

    /* initialize trove, verifying storage space exists */
    ret = trove_initialize(storage_space, 0, &method_name, 0);
    if (ret < 0) {
	fprintf(stderr,
		"%s: error: trove initialize failed; aborting!\n",
		argv[0]);
	return -1;
    }

    if (verbose) fprintf(stderr,
			 "%s: info: initialized with storage space '%s'.\n",
			 argv[0],
			 storage_space);

    /* if no collection was specified, simply print out the collections and exit */
    if (!got_collection) {
	ret = print_collections();
	if (ret != 0) {
	    fprintf(stderr,
		    "%s: error: collection iterate failed; aborting!\n",
		    argv[0]);
            trove_close_context(coll_id, trove_context);
	    trove_finalize();
	    return -1;
	}
        trove_close_context(coll_id, trove_context);
	trove_finalize();
	return 0;
    }

    /* a collection was specified.
     * - look up the collection
     * - find the root handle (or maybe show all the collection attribs?)
     * - print out information on the dataspaces in the collection
     */

    ret = trove_collection_lookup(collection,
				  &coll_id,
				  NULL,
				  &op_id);
    if (ret != 1) {
	fprintf(stderr,
		"%s: error: collection lookup failed for collection '%s'; aborting!.\n",
		argv[0],
		collection);
	trove_finalize();
	return -1;
    }

    if (verbose) fprintf(stderr,
			 "%s: info: found collection '%s'.\n",
			 argv[0],
			 collection);


    ret = trove_open_context(coll_id, &trove_context);
    if (ret < 0)
    {
        fprintf(stderr, "trove_open_context failed\n");
        return -1;
    }

    /* find root handle */
    key.buffer = root_handle_string;
    key.buffer_sz = strlen(root_handle_string) + 1;
    val.buffer = &root_handle;
    val.buffer_sz = sizeof(root_handle);
    ret = trove_collection_geteattr(coll_id,
				    &key,
				    &val,
				    0,
				    NULL,
                                    trove_context,
				    &op_id);

    while (ret == 0) {
	ret = trove_dspace_test(
            coll_id, op_id, trove_context, &count, NULL, NULL, &state,
            TROVE_DEFAULT_TEST_TIMEOUT);
    }
    if (ret != 1) {
	if (verbose) fprintf(stderr,
			     "%s: warning: collection geteattr (for root handle) failed; aborting!\n",
			     argv[0]);
	no_root_handle = 1;
    }

    /* TODO: NEED ITERATE FOR EATTRS? */

    /* TODO: GET A COUNT OF DATASPACES? */

    /* print basic stats on collection */
    if (no_root_handle) {
	fprintf(stdout,
		"Storage space %s, collection %s (coll_id = %d, "
                "*** no root_handle found ***):\n",
		storage_space,
		collection,
		coll_id);
    }
    else {
	fprintf(stdout,
		"Storage space %s, collection %s (coll_id = %d, "
                "root_handle = 0x%08llx):\n",
		storage_space,
		collection,
		coll_id,
		llu(root_handle));
    }

    if (got_dspace_handle)
    {
        ret = print_dspace(coll_id, dspace_handle, trove_context);
    }
    else
    {
        ret = print_dspaces(coll_id, root_handle,
                            trove_context, no_root_handle);
    }

    trove_close_context(coll_id, trove_context);
    trove_finalize();

    return 0;
}

static int parse_args(int argc, char **argv)
{
    int c;

    while ((c = getopt(argc, argv, "s:c:d:kvh")) != EOF) {
	switch (c) {
	    case 's':
		strncpy(storage_space, optarg, PATH_MAX);
		break;
	    case 'c': /* collection */
		got_collection = 1;
		strncpy(collection, optarg, PATH_MAX);
		break;
	    case 'k':
		print_keyvals = 1;
		break;
	    case 'd':
		/* TODO: USE BIGGER VALUE */
		got_dspace_handle = 1;
		dspace_handle = strtol(optarg, NULL, 16);
	    case 'v':
		verbose = 1;
		break;
	    case '?':
	    default:
		fprintf(stderr, "%s: error: unrecognized option '%c'.\n", argv[0], c);
	    case 'h':
		fprintf(stderr,
			"usage: pvfs2-showcoll [-s storage_space] [-c collection_name] [-d dspace_handle] [-v] [-k] [-h]\n");
		fprintf(stderr, "\tdefault storage space is '/tmp/pvfs2-test-space'.\n");
		fprintf(stderr, "\t'-v' turns on verbose output.\n");
		fprintf(stderr, "\t'-k' prints data in keyval spaces.\n");
		fprintf(stderr, "\t'-d' prints data for a single dspace only, given a handle (in hex).\n");
		fprintf(stderr, "\t'-h' prints this message.\n");
		fprintf(stderr, "\n\tWithout a collection name, a list of collections will be printed.\n");
		if (c == 'h') exit(0);
		return -1;
	}
    }
    return 0;
}

static int print_dspaces(TROVE_coll_id coll_id,
			 TROVE_handle root_handle,
                         TROVE_context_id trove_context,
			 int no_root_handle)
{
    int ret, count;
    TROVE_ds_position pos;
    TROVE_handle harray[64];
    TROVE_op_id op_id;
    TROVE_ds_state state;

    pos = TROVE_ITERATE_START;
    count = 64;

    while (count > 0) {
	int opcount;

	ret = trove_dspace_iterate_handles(coll_id,
					   &pos,
					   harray,
					   &count,
					   0 /* flags */,
					   NULL /* vtag */,
					   NULL /* user ptr */,
                                           trove_context,
					   &op_id);
	while (ret == 0) ret = trove_dspace_test(
            coll_id, op_id, trove_context, &opcount, NULL, NULL, &state,
            TROVE_DEFAULT_TEST_TIMEOUT);
	if (ret != 1) return -1;

	if (count > 0) {
	    int i;
	    for (i = 0; i < count; i++) {
		ret = print_dspace(coll_id, harray[i], trove_context);
	    }
	}
    }

    return 0;
}

static int print_dspace(TROVE_coll_id coll_id,
			TROVE_handle handle,
                        TROVE_context_id trove_context)
{
    int ret, opcount;
    TROVE_ds_attributes_s ds_attr;
    TROVE_op_id op_id;
    TROVE_ds_state state;

    ret = trove_dspace_getattr(coll_id,
			       handle,
			       &ds_attr,
			       0 /* flags */,
			       NULL /* user ptr */,
                               trove_context,
			       &op_id);
    while (ret == 0) {
	ret = trove_dspace_test(
            coll_id, op_id, trove_context, &opcount, NULL, NULL, &state,
            TROVE_DEFAULT_TEST_TIMEOUT);
    }
    if (ret != 1) return -1;
		
    fprintf(stdout,
	    "\t0x%08llx (dspace_getattr output: type = %s, b_size = %lld, k_size = %lld)\n",
	    llu(handle),
	    type_to_string(ds_attr.type),
	    lld(ds_attr.b_size),
	    lld(ds_attr.k_size));

    if (print_keyvals) {
	ret = print_dspace_keyvals(coll_id, handle,
                                   trove_context, ds_attr.type);
	if (ret != 0) return -1;
    }

    return 0;
}

static char *type_to_string(TROVE_ds_type type)
{
    static char mf[] = "metafile";
    static char df[] = "datafile";
    static char sl[] = "symlink";
    static char di[] = "directory";
    static char dd[] = "dirdata";
    static char un[] = "unknown";

    switch (type) {
	case PVFS_TYPE_METAFILE:
	    return mf;
	case PVFS_TYPE_DATAFILE:
	    return df;
	case PVFS_TYPE_DIRDATA:
	    return dd;
	case PVFS_TYPE_SYMLINK:
	    return sl;
	case PVFS_TYPE_DIRECTORY:
	    return di;
	default:
	    return un;
    }
}

static int print_dspace_keyvals(TROVE_coll_id coll_id,
				TROVE_handle handle,
                                TROVE_context_id trove_context,
				TROVE_ds_type type)
{
    int ret, count;
    TROVE_ds_position pos;
    TROVE_keyval_s key, val;
    TROVE_op_id op_id;
    TROVE_ds_state state;

    key.buffer    = malloc(65536);
    key.buffer_sz = 65536;
    val.buffer    = malloc(65536);
    val.buffer_sz = 65536;

    pos = TROVE_ITERATE_START;
    count = 1;

    while (count > 0) {
	int opcount;
	ret = trove_keyval_iterate(coll_id,
				   handle,
				   &pos,
				   &key,
				   &val,
				   &count,
				   0 /* flags */,
				   NULL /* vtag */,
				   NULL /* user ptr */,
                                   trove_context,
				   &op_id);

	while (ret == 0) ret = trove_dspace_test(
            coll_id, op_id, trove_context, &opcount, NULL, NULL, &state,
            TROVE_DEFAULT_TEST_TIMEOUT);
	if (ret != 1) return -1;

	if (count > 0) print_keyval_pair(&key, &val, type, 65536);
    }

    free(key.buffer);
    free(val.buffer);

    return 0;
}

static void print_object_attributes(struct PVFS_object_attr *a_p)
{
    fprintf(stdout,
	    "(owner = %d, group = %d, perms = %o, objtype = %s)\n",
	    a_p->owner,
	    a_p->group,
	    a_p->perms,
	    type_to_string(a_p->objtype));
}

static void print_datafile_handles(PVFS_handle *h_p,
				   int count)
{
    int i;

    for (i = 0; i < count && i < 10; i++) fprintf(stdout, "0x%08llx ", llu(h_p[i]));

    if (i == 10) fprintf(stdout, "...\n");
    else fprintf(stdout, "\n");
}

static int print_keyval_pair(TROVE_keyval_s *key_p,
			     TROVE_keyval_s *val_p,
			     TROVE_ds_type type,
			     int sz)
{
    int key_printable = 0, val_printable = 0;

    if (isprint(((char *)key_p->buffer)[0]) && (strnlen(key_p->buffer, sz) < 64)) key_printable = 1;
    if (isprint(((char *)val_p->buffer)[0]) && (strnlen(val_p->buffer, sz) < 64)) val_printable = 1;

    if (!strncmp(key_p->buffer, "metadata", 9) && val_p->read_sz == sizeof(struct PVFS_object_attr)) {
	fprintf(stdout,
		"\t\t'%s' (%d): '%s' (%d) as PVFS_object_attr = ",
		(char *) key_p->buffer,
		key_p->read_sz,
		(char *) val_p->buffer,
		val_p->read_sz);
	print_object_attributes((struct PVFS_object_attr *) val_p->buffer);
    }
    else if (!strncmp(key_p->buffer, "datafile_handles", 17) && val_p->read_sz % sizeof(PVFS_handle) == 0) {
	fprintf(stdout,
		"\t\t'%s' (%d): '%s' (%d) as handles = ",
		(char *) key_p->buffer,
		key_p->read_sz,
		(char *) val_p->buffer,
		val_p->read_sz);
	print_datafile_handles((PVFS_handle *) val_p->buffer, val_p->read_sz / sizeof(PVFS_handle));
    }
    else if (type == PVFS_TYPE_DIRECTORY && !strncmp(key_p->buffer, "dir_ent", 8)) {
	fprintf(stdout,
		"\t\t'%s' (%d): '%s' (%d) as a handle = 0x%08llx\n",
		(char *) key_p->buffer,
		key_p->read_sz,
		(char *) val_p->buffer,
		val_p->read_sz,
		llu(*(TROVE_handle *) val_p->buffer));
    }
    else if (type == PVFS_TYPE_DIRDATA && val_p->read_sz == 8) {
	fprintf(stdout,
		"\t\t'%s' (%d): '%s' (%d) as a handle = 0x%08llx\n",
		(char *) key_p->buffer,
		key_p->read_sz,
		(char *) val_p->buffer,
		val_p->read_sz,
		llu(*(TROVE_handle *) val_p->buffer));
    }
    else if (key_printable && val_printable) {
	fprintf(stdout,
		"\t\t'%s' (%d): '%s' (%d)\n",
		(char *) key_p->buffer,
		key_p->read_sz,
		(char *) val_p->buffer,
		val_p->read_sz);
    }
    else if (key_printable && !val_printable) {
	fprintf(stdout,
		"\t\t'%s' (%d): <data> (%d)\n",
		(char *) key_p->buffer,
		key_p->read_sz,
		val_p->read_sz);
    }
    else {
	fprintf(stdout,
		"\t\t<data> (%d): <data> (%d)\n",
		key_p->read_sz,
		val_p->read_sz);
    }
    return 0;
}

static int print_collections(void)
{
    int ret, count;
    TROVE_op_id op_id;
    TROVE_coll_id coll_id;
    TROVE_keyval_s name;
    TROVE_ds_position pos;
    char *coll_name;

    coll_name = malloc(PATH_MAX);
    if (coll_name == NULL) return -1;

    name.buffer    = coll_name;
    name.buffer_sz = PATH_MAX;
    count = 1;
    pos = TROVE_ITERATE_START;

    fprintf(stdout, "Storage space %s collections:\n", storage_space);

    while (count > 0) {
	ret = trove_collection_iterate(&pos,
				       &name,
				       &coll_id,
				       &count,
				       0 /* flags */,
				       0 /* vtag */,
				       NULL /* user ptr */,
				       &op_id);
	if (ret != 1) {
	    free(coll_name);
	    return -1;
	}
	
	if (count > 0) fprintf(stdout,
			       "\t%s (coll_id = %d)\n",
			       coll_name,
			       coll_id);
    }

    fprintf(stdout, "\n");
    free(coll_name);
    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
