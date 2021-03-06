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


static char data_path[PATH_MAX] = "/tmp/pvfs2-test-space";
static char meta_path[PATH_MAX] = "/tmp/pvfs2-test-space";
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

    ret = parse_args(argc, argv);
    if (ret < 0) {
	fprintf(stderr,
		"%s: error: argument parsing failed; aborting!\n",
		argv[0]);
	return -1;
    }

    /* initialize trove, verifying storage space exists */
    ret = trove_initialize(
      TROVE_METHOD_DBPF, NULL, data_path, meta_path, 0);
    if (ret < 0) 
    {
        printf("Error from trove_initialize is %d.\n",ret);
	fprintf(stderr,
		"%s: error: trove initialize failed; aborting!\n",
		argv[0]);
	return -1;
    }

    if (verbose) fprintf(stderr,
			 "%s: info: initialized with storage spaces '%s' and '%s'.\n",
			 argv[0],
			 data_path, meta_path);

    /* if no collection was specified, simply print out the collections and exit */
    if (!got_collection) {
	ret = print_collections();
	if (ret != 0) {
	    fprintf(stderr,
		    "%s: error: collection iterate failed; aborting!\n",
		    argv[0]);
	    trove_finalize(TROVE_METHOD_DBPF);
	    return -1;
	}
	trove_finalize(TROVE_METHOD_DBPF);
	return 0;
    }

    /* a collection was specified.
     * - look up the collection
     * - find the root handle (or maybe show all the collection attribs?)
     * - print out information on the dataspaces in the collection
     */

    ret = trove_collection_lookup(TROVE_METHOD_DBPF,
                                  collection,
				  &coll_id,
				  NULL,
				  &op_id);
    if (ret != 1) {
	fprintf(stderr,
		"%s: error: collection lookup failed for collection '%s'; aborting!.\n",
		argv[0],
		collection);
	trove_finalize(TROVE_METHOD_DBPF);
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
    key.buffer = ROOT_HANDLE_KEYSTR;
    key.buffer_sz = ROOT_HANDLE_KEYLEN;
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
		"Storage space %s and %s, collection %s (coll_id = %d, "
                "*** no root_handle found ***):\n",
		data_path,
		meta_path,
		collection,
		coll_id);
    }
    else {
	fprintf(stdout,
		"Storage space %s and %s, collection %s (coll_id = %d, "
                "root_handle = 0x%08llx):\n",
		data_path,
		meta_path,
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
    trove_finalize(TROVE_METHOD_DBPF);

    return 0;
}

static int parse_args(int argc, char **argv)
{
    int c;

    while ((c = getopt(argc, argv, "s:m:c:d:kvh")) != EOF) {
	switch (c) {
	    case 's':
		strncpy(data_path, optarg, PATH_MAX - 1);
		break;
            case 'm':
		strncpy(meta_path, optarg, PATH_MAX - 1);
                break;
	    case 'c': /* collection */
		got_collection = 1;
		strncpy(collection, optarg, PATH_MAX - 1);
		break;
	    case 'k':
		print_keyvals = 1;
		break;
	    case 'd':
		/* TODO: USE BIGGER VALUE */
		got_dspace_handle = 1;
		dspace_handle = strtol(optarg, NULL, 16);
                break;
	    case 'v':
		verbose = 1;
		break;
	    case '?':
	    default:
		fprintf(stderr, "%s: error: unrecognized option '%c'.\n", argv[0], c);
	    case 'h':
		fprintf(stderr,
			"usage: pvfs2-showcoll [-s data_storage_space] [-m metadata storage space] [-c collection_name] [-d dspace_handle] [-v] [-k] [-h]\n");
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
			       &op_id, NULL);
    while (ret == 0) {
	ret = trove_dspace_test(
            coll_id, op_id, trove_context, &opcount, NULL, NULL, &state,
            TROVE_DEFAULT_TEST_TIMEOUT);
    }
    if (ret != 1) return -1;
		
    fprintf(stdout,
	    "\t0x%08llx/%llu (dspace_getattr output: type = %s, b_size = %lld)\n",
	    llu(handle),llu(handle),
	    type_to_string(ds_attr.type),
	    (ds_attr.type == PVFS_TYPE_DATAFILE) ? lld(ds_attr.u.datafile.b_size) : 0);

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
    static char in[] = "internal";
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
        case PVFS_TYPE_INTERNAL:
            return in;
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

    key.buffer    = malloc(256);
    key.buffer_sz = 256;
    key.read_sz   = 0;
    val.buffer    = malloc(65536);
    val.buffer_sz = 65536;
    val.read_sz   = 0;

    if (key.buffer)
        memset(key.buffer,0,256);
    if (val.buffer)
        memset(val.buffer,0,65536);

    if ( !(key.buffer && val.buffer) )
    {
        if (key.buffer)
           free(key.buffer);
        if (val.buffer)
           free(val.buffer);
        printf("%s: Unable to allocate memory.\n",__func__);
        return -1;
    }


    pos = TROVE_ITERATE_START;
    count = 1;

    while (count > 0) {
	int opcount;
        printf("%s:calling trove_keyval_iterate for %llu.\n"
              ,__func__
              ,llu(handle));
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
				   &op_id, NULL);

	while (ret == 0) ret = trove_dspace_test(
            coll_id, op_id, trove_context, &opcount, NULL, NULL, &state,
            TROVE_DEFAULT_TEST_TIMEOUT);
	if (ret != 1) return -1;

        printf("%s: count=%d\n",__func__,count);

	if (count > 0) print_keyval_pair(&key, &val, type, 65536);

        /* re-initialize key val */
        memset(key.buffer,0,256);
        memset(val.buffer,0,65536);
        key.buffer_sz = 256;
        val.buffer_sz = 65536;
        key.read_sz = 0;
        val.read_sz = 0;
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

    for (i = 0; i < count && i < 10; i++) fprintf(stdout, "\n\t\t\t\t0x%08llx(%llu)", llu(h_p[i]), llu(h_p[i]));

    if (i == 10) fprintf(stdout, "...\n");
    else fprintf(stdout, "\n");
}

static int print_keyval_pair(TROVE_keyval_s *key_p,
			     TROVE_keyval_s *val_p,
			     TROVE_ds_type type,
			     int sz)
{
    int key_printable = 0, val_printable = 0;

    if (isprint(((char *)key_p->buffer)[0])) key_printable = 1;
    if (isprint(((char *)val_p->buffer)[0])) val_printable = 1;

    if (key_printable && key_p->buffer_sz >= 64)
    {
        memset(&((char *)key_p->buffer)[64],0,1);
    }

    if (val_printable && val_p->buffer_sz >= 64)
    {
        memset(&((char *)key_p->buffer)[64],0,1);
    }

    if (!strncmp(key_p->buffer, "metadata", 9) && val_p->read_sz == sizeof(struct PVFS_object_attr)) {
	fprintf(stdout,
		"\t\t'%s' (%d): '%s' (%d) as PVFS_object_attr = ",
		(char *) key_p->buffer,
		key_p->read_sz,
		(char *) val_p->buffer,
		val_p->read_sz);
	print_object_attributes((struct PVFS_object_attr *) val_p->buffer);
    }
    else if (!strncmp(key_p->buffer, "dh", 17) && val_p->read_sz % sizeof(PVFS_handle) == 0) {
	fprintf(stdout,
		"\t\t'%s' (%d): '%s' (%d) as handles = ",
		(char *) key_p->buffer,
		key_p->read_sz,
		val_printable ? (char *) val_p->buffer : "",
		val_p->read_sz);
	print_datafile_handles((PVFS_handle *) val_p->buffer, val_p->read_sz / sizeof(PVFS_handle));
    }
    else if (type == PVFS_TYPE_DIRECTORY && !strncmp(key_p->buffer, "de", 3)) {
	fprintf(stdout,
		"\t\t'%s' (%d): '%s' (%d) as a handle = 0x%08llx(%llu)\n",
		(char *) key_p->buffer,
		key_p->read_sz,
		val_printable ? (char *) val_p->buffer : "",
		val_p->read_sz,
		llu(*(TROVE_handle *) val_p->buffer),
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
    else if (key_printable && !strncmp((char *)key_p->buffer,"user.pvfs2.meta_hint",20))
    {
        fprintf(stdout,
                "\t\t'%s' (%d): 0x%08llX (%d)\n"
                ,(char *)key_p->buffer
                ,(int)strlen((char*)key_p->buffer)
                ,*(unsigned long long *)val_p->buffer
                ,(int)sizeof(unsigned long));
    }
    else if (key_printable && !strncmp((char *)key_p->buffer,"user.pvfs2.mirror.mode",22))
    {
        fprintf(stdout,
                "\t\t'%s' (%d): %d (%d)\n"
                ,(char *)key_p->buffer
                ,(int)strlen((char*)key_p->buffer)
                ,*(unsigned int *)val_p->buffer
                ,(int)sizeof(unsigned int));
    }
    else if (key_printable && !strncmp((char *)key_p->buffer,"user.pvfs2.mirror.copies",24))
    {
        fprintf(stdout,
                "\t\t'%s' (%d): %d (%d)\n"
                ,(char *)key_p->buffer
                ,(int)strlen((char*)key_p->buffer)
                ,*(unsigned int *)val_p->buffer
                ,(int)sizeof(unsigned int));
    }
    else if (key_printable && !strncmp((char *)key_p->buffer,"user.pvfs2.mirror.handles",25))
    {
        fprintf(stdout,
                "\t\t'%s' (%d): '' (%d) as handles:"
                ,(char *)key_p->buffer
                ,(int)strlen((char*)key_p->buffer)
                ,(int)val_p->read_sz);
	print_datafile_handles((PVFS_handle *) val_p->buffer, val_p->read_sz / sizeof(PVFS_handle));
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
    TROVE_coll_id coll_id[32];
    TROVE_keyval_s name[32];
    int i;

    count = 32;
    for (i = 0; i < 32; i++)
    {
        char *coll_name;
        coll_name = malloc(PATH_MAX);
        if (coll_name == NULL)
            return -1;
        memset(coll_name,0,PATH_MAX);
        name[i].buffer    = coll_name;
        name[i].buffer_sz = PATH_MAX;
        name[i].read_sz   = 0;
    }

    fprintf(stdout, "Storage space %s and %s collections:\n",
	    data_path, meta_path);

    ret = trove_collection_iterate(TROVE_METHOD_DBPF, name, coll_id, &count,
        0 /* flags */, 0 /* vtag */, NULL /* user ptr */, &op_id);
    if (ret != 1) 
    {
        for (i = 0; i < 32; i++)
        {
            free(name[i].buffer);
        }
        return -1;
    }

    for (i = 0; i < count; i++)
    {
        fprintf(stdout, "\t%s (coll_id = %d)\n", (char *)name[i].buffer,
            coll_id[i]);
    }

    for (i = 0; i < 32; i++)
    {
        free(name[i].buffer);
    }

    fprintf(stdout, "\n");
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
