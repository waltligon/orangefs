/*
 * Copyright © Acxiom Corporation, 2006
 *
 * See COPYING in top-level directory.
 */

/** \defgroup migrate pvfs2-migrate-collection utility
 * @{
 */

/** \file
 * Migration utility for updating PVFS2 collections
 */

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <getopt.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <dirent.h>
#include <limits.h>

#include <db.h>

#define __PINT_REQPROTO_ENCODE_FUNCS_C
#include "pvfs2-config.h"
#include "pvfs2.h"
#include "pvfs2-internal.h"
#include "trove.h"
#include "mkspace.h"
#include "pint-distribution.h"
#include "pint-dist-utils.h"

#ifndef PVFS2_VERSION
#define PVFS2_VERSION "Unknown"
#endif

typedef struct
{
    char coll[100];
    int coll_set;
    char storage_space[PATH_MAX];
    int storage_space_set;
} options_t;

/** default size of buffers to use for reading old db keys */
int DEF_KEY_SIZE = 4096;
/** default size of buffers to use for reading old db values */
int DEF_DATA_SIZE = 8192;

int verbose = 0;

/* generic migration functions */
static void print_help(char *progname);
static int parse_args(int argc, char **argv, options_t *opts);
static int src_get_version(
    char* storage_space, char* coll_id, char* ver_string, int ver_string_max);
static int remove_migration_id_mapping(
    char* storage_space, char* coll_name);
static int confirm_coll_name_not_used(char* storage_space, char* coll_name);
static int confirm_subdir_not_used(char* storage_space, char* subdir);
static int get_migration_id(char* storage_space, TROVE_coll_id* out_id);

/* functions specific to reading 0.0.1 collections */
static int src_get_version_0_0_1(
    char* storage_space, char* coll_id, char* ver_string, int ver_string_max);
static int translate_0_0_1(
    char* storage_space, char* coll_id, char* new_name, TROVE_coll_id new_id);
static int translate_coll_eattr_0_0_1(
    char* storage_space, char* coll_id, char* new_name, TROVE_coll_id new_id,
    TROVE_context_id trove_context);
static int translate_dspace_attr_0_0_1(
    char* storage_space, char* coll_id, char* new_name, TROVE_coll_id new_id,
    TROVE_context_id trove_context);
static int translate_keyvals_0_0_1(
    char* storage_space, char* coll_id, char* new_name, TROVE_coll_id new_id,
    TROVE_context_id trove_context);
static int translate_dirdata_sizes_0_0_1(
    char* storage_space, char* coll_id, char* new_name, TROVE_coll_id new_id,
    TROVE_context_id trove_context);
static int translate_bstreams_0_0_1(
    char* storage_space, char* coll_id, char* new_name, TROVE_coll_id new_id,
    TROVE_context_id trove_context);
static int translate_keyval_db_0_0_1(
    char* storage_space, char* coll_id, 
    char* full_db_path, TROVE_handle handle, char* new_name, TROVE_coll_id 
    new_id, TROVE_context_id trove_context);
static int translate_dist_0_0_1(
    PINT_dist * dist);

/** number of keyval buckets used in DBPF 0.0.1 */
#define KEYVAL_MAX_NUM_BUCKETS_0_0_1 32
/** number of bstream buckets used in DBPF 0.0.1 */
#define BSTREAM_MAX_NUM_BUCKETS_0_0_1 64

/** macros to resolve handle to db file name in DBPF 0.0.1 */
#define DBPF_KEYVAL_GET_BUCKET_0_0_1(__handle, __id)                     \
(((__id << ((sizeof(__id) - 1) * 8)) | __handle) %                       \
   KEYVAL_MAX_NUM_BUCKETS_0_0_1)
#define KEYVAL_DIRNAME_0_0_1 "keyvals"
/* arguments are: buf, path_max, stoname, collid, handle */
#define DBPF_GET_KEYVAL_DBNAME_0_0_1(__b, __pm, __stoname, __cid, __handle)  \
do {                                                                         \
  snprintf(__b, __pm, "/%s/%08x/%s/%.8llu/%08llx.keyval", __stoname,           \
  __cid, KEYVAL_DIRNAME_0_0_1,                                               \
  llu(DBPF_KEYVAL_GET_BUCKET_0_0_1(__handle, __cid)), llu(__handle));        \
} while (0)

struct PVFS_ds_storedattr_s_0_0_1
{
    PVFS_fs_id fs_id;
    PVFS_handle handle;
    PVFS_ds_type type;
    PVFS_uid uid;
    PVFS_gid gid;
    PVFS_permissions mode;
    PVFS_time ctime;
    PVFS_time mtime;
    PVFS_time atime;
    uint32_t dfile_count;
    uint32_t dist_size;
};
typedef struct PVFS_ds_storedattr_s_0_0_1 PVFS_ds_storedattr_0_0_1;

int main(int argc, char **argv)
{
    int ret = -1;
    options_t opts;
    char version[256];
    char new_name[] = "pvfs2-migrate-collection-tmp";
    TROVE_coll_id new_id = 7223;
    char subdir[PATH_MAX];

    /* make sure that the buffers we intend to use for reading keys and
     * values is at least large enough to hold the maximum size of xattr keys
     * and values
     */
    if(DEF_KEY_SIZE < PVFS_REQ_LIMIT_KEY_LEN)
    {
        DEF_KEY_SIZE = PVFS_REQ_LIMIT_KEY_LEN;
    }
    if(DEF_DATA_SIZE < PVFS_REQ_LIMIT_VAL_LEN)
    {
        DEF_DATA_SIZE = PVFS_REQ_LIMIT_VAL_LEN;
    }

    if (parse_args(argc, argv, &opts))
    {
	fprintf(stderr,"%s: error: argument parsing failed.\n",
                argv[0]);
	return -1;
    }

    /* make sure that there will not be any collisions in the collection name
     * space 
     */
    ret = confirm_coll_name_not_used(opts.storage_space, new_name);
    if(ret < 0)
    {
        fprintf(stderr, "Error: unable to confirm availability of collection names for migration.\n");
        return(-1);
    }
    sprintf(subdir, "%s.old", opts.coll);
    ret = confirm_subdir_not_used(opts.storage_space, subdir);
    if(ret < 0)
    {
        fprintf(stderr, "Error: unable to confirm availability of backup subdirectory for migration: %s.\n", subdir);
        fprintf(stderr, "Error: please make sure that the migration has not already been performed.\n");
        return(-1);
    }

    /* pick an unused collection id to use for migration */
    ret = get_migration_id(opts.storage_space, &new_id);
    if(ret < 0)
    {
        fprintf(stderr, "Error: unable to get a collection id to use for migration.\n");
        return(-1);
    }

    /* find version of source storage space */
    ret = src_get_version(opts.storage_space, opts.coll, version, 254);
    if(ret < 0)
    {
        fprintf(stderr, "Error: failed to read version of src storage space.\n");
        return(-1);
    }
        
    /* call the appropriate translation routine based on the version */
    if(strcmp(version, "0.0.1") == 0)
    {
        ret = translate_0_0_1(
            opts.storage_space, opts.coll, new_name, new_id);
        if(ret < 0)
        {
            fprintf(stderr, "Error: failed to translate from 0.0.1 collection.\n");
            return(-1);
        }
    }
    else
    {
        /* complain if we don't recognize the version */
        fprintf(stderr, "Error: unknown collection version: %s\n", version);
        return(-1);
    }

    return(0);
}

/**
 * Parses command line arguments
 * \return 0 on succes, -1 on failure
 */
static int parse_args(
    int argc,        /** < argc from main() routine */
    char **argv,     /** < argv from main() routine */
    options_t *opts) /** < parsed command line options */
{
    int ret = 0, option_index = 0;
    char *cur_option = NULL;
    static struct option long_opts[] =
    {
        {"help",0,0,0},
        {"verbose",0,0,0},
        {"version",0,0,0},
        {"collection",1,0,0},
        {"storage-space",1,0,0},
        {0,0,0,0}
    };

    memset(opts, 0, sizeof(options_t));

    while ((ret = getopt_long(argc, argv, "",
                              long_opts, &option_index)) != -1)
    {
	switch (ret)
        {
            case 0:
                cur_option = (char *)long_opts[option_index].name;
                if (strcmp("collection", cur_option) == 0)
                {
                    strncpy(opts->coll, optarg, 99);
                    opts->coll_set = 1;
                    break;
                }
                if (strcmp("storage-space", cur_option) == 0)
                {
                    strncpy(opts->storage_space, optarg, PATH_MAX);
                    opts->storage_space_set = 1;
                    break;
                }
                if (strcmp("verbose", cur_option) == 0)
                {
                    verbose = 1;
                    break;
                }
                if (strcmp("help", cur_option) == 0)
                {
                    print_help(argv[0]);
                    exit(0);
                }
                else if (strcmp("version", cur_option) == 0)
                {
                    fprintf(stderr,"%s\n",PVFS2_VERSION);
                    exit(0);
                }
	    default:
                print_help(argv[0]);
		return(-1);
	}
    }

    if (!opts->coll_set || !opts->storage_space_set)
    {
        print_help(argv[0]);
        return(-1);
    }

    return 0;
}

/**
 * Prints help for command line arguments
 */
static void print_help(
    char *progname) /**< executable name */
{
    fprintf(stderr,"usage: %s [OPTION]...\n", progname);
    fprintf(stderr,"This utility will migrate a PVFS2 collection from an old version\n"
           "to the most recent version.\n\n");
    fprintf(stderr,"The following arguments are required:\n");
    fprintf(stderr,"--------------\n");
    fprintf(stderr,"  --storage-space=<path>:    "
            "location of storage space\n");
    fprintf(stderr,"  --collection=<hex number>: "
            "collection id to be migrated\n");
    fprintf(stderr, "\n");
    fprintf(stderr,"The following arguments are optional:\n");
    fprintf(stderr,"--------------\n");
    fprintf(stderr,"  --verbose:                 "
            "print verbose messages during execution\n");
    fprintf(stderr,"  --help:                    "
            "show this help listing\n");
    fprintf(stderr,"  --version:                 "
            "print version information and exit\n");
    fprintf(stderr, "\n");
    return;
}


/**
 * Retrieves the version number from a trove collection; will try multiple
 * methods if needed
 * \return 0 on succes, -1 on failure
 */
static int src_get_version(
    char* storage_space,  /**< path to trove storage space */
    char* coll_id,        /**< collection id in string format */
    char* ver_string,     /**< version in string format */
    int ver_string_max)   /**< maximum size of version string */
{
    int ret = -1;


    ret = src_get_version_0_0_1(storage_space, coll_id, ver_string, 
        ver_string_max);

    if(ret != 0)
    {
        fprintf(stderr, "Error: all known collection version checks failed for \n"
                "collection %s in storage space %s\n", storage_space, coll_id);
    }

    return(ret);
}


/**
 * Reads the version number from a 0.0.1 DBPF collection
 * \return 0 on succes, -1 on failure
 */
static int src_get_version_0_0_1(
    char* storage_space, /**< path to trove storage space */
    char* coll_id,       /**< collection id in string format */
    char* ver_string,    /**< version in string format */
    int ver_string_max)  /**< maximum size of version string */
{
    char coll_db[PATH_MAX];
    int ret;
    DB *dbp;
    DBT key, data;

    sprintf(coll_db, "%s/%s/collection_attributes.db", storage_space, coll_id);

    /* try to find a collections db */
    ret = access(coll_db, F_OK);
    if(ret == -1 && errno == ENOENT)
    {
        fprintf(stderr, "Error: could not find %s.\n", coll_db);
        fprintf(stderr, "Error: src directory is not a known format.\n");
        return(-1);
    }
    else if(ret == -1)
    {
        fprintf(stderr, "access(%s): %s\n", coll_db, strerror(errno));
        return(-1);
    }

    ret = db_create(&dbp, NULL, 0);
    if(ret != 0)
    {
        fprintf(stderr, "Error: db_create: %s.\n", db_strerror(ret));
        return(-1);
    }
    
    ret = dbp->open(dbp,
#ifdef HAVE_TXNID_PARAMETER_TO_DB_OPEN
                          NULL,
#endif
                          coll_db,
                          NULL,
                          DB_UNKNOWN,
                          0,
                          0);
    if(ret != 0)
    {
        fprintf(stderr, "Error: dbp->open: %s.\n", db_strerror(ret));
        return(-1);
    }

    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));
    key.data = "trove-dbpf-version";
    key.size = strlen("trove-dbpf-version");
    data.data = ver_string;
    data.size = data.ulen = ver_string_max;
    data.flags |= DB_DBT_USERMEM;

    ret = dbp->get(dbp, NULL, &key, &data, 0);
    if(ret != 0)
    {
        fprintf(stderr, "Error: dbp->get: %s\n", db_strerror(ret));
        return(-1);
    }

    dbp->close(dbp, 0);

    return(0);
}

/**
 * Migrates an entire 0.0.1 DBPF collection
 * \return 0 on succes, -1 on failure
 */
static int translate_0_0_1(
    char* storage_space,  /**< path to trove storage space */
    char* coll_id,        /**< collection id in string format */
    char* new_name,       /**< name of new (temporary) collection */
    TROVE_coll_id new_id) /**< id of new (temporary) collection */  
{
    int ret = -1;
    /* choose a handle range big enough to encompass anything pvfs2-genconfig
     * will create
     */
    char handle_range[] = "4-64000000000";
    char* method_name = NULL;
    TROVE_op_id op_id;
    TROVE_context_id trove_context = -1;
    char old_path[PATH_MAX];
    char new_path[PATH_MAX];

    /* create new collection */
    /* NOTE: deliberately not specifying root handle; it will get translated
     * later as a normal directory if applicable
     */
    if(verbose) printf("VERBOSE Creating temporary collection to migrate to.\n");
    ret = pvfs2_mkspace(
        storage_space, 
        new_name,
        new_id, 
        TROVE_HANDLE_NULL,
        handle_range,
        NULL,
        1,
        0);
    if(ret != 0)
    {
        fprintf(stderr, "Error: failed to create new collection.\n");
        return(-1);
    }

    /* initialize distribution infrastructure */
    /* NOTE: server config argument is not required here */
    ret = PINT_dist_initialize(NULL);
    if (ret < 0)
    {
        PVFS_perror("PINT_dist_initialize", ret);
        if(verbose) printf("VERBOSE Destroying temporary collection.\n");
        pvfs2_rmspace(storage_space, new_name, new_id, 1, 0);
        return(-1);
    }

    /* initialize trove and lookup collection */
    ret = trove_initialize(storage_space, 0, &method_name, 0);
    if (ret < 0)
    {
        PVFS_perror("trove_initialize", ret);
        if(verbose) printf("VERBOSE Destroying temporary collection.\n");
        pvfs2_rmspace(storage_space, new_name, new_id, 1, 0);
        return(-1);
    }
    ret = trove_collection_lookup(new_name, &new_id, NULL, &op_id);
    if (ret != 1)
    {   
        fprintf(stderr, "Error: failed to lookup new collection.\n");
        if(verbose) printf("VERBOSE Destroying temporary collection.\n");
        pvfs2_rmspace(storage_space, new_name, new_id, 1, 0);
        return -1; 
    }   

    ret = trove_open_context(new_id, &trove_context);
    if (ret < 0)
    {
        PVFS_perror("trove_open_context", ret);
        return(-1);
    }

    /* convert collection xattrs */
    ret = translate_coll_eattr_0_0_1(storage_space, coll_id, new_name,
        new_id, trove_context);
    if(ret < 0)
    {
        fprintf(stderr, "Error: failed to migrate collection extended attributes.\n");
        if(verbose) printf("VERBOSE Destroying temporary collection.\n");
        pvfs2_rmspace(storage_space, new_name, new_id, 1, 0);
        return(-1);
    }

    /* convert dspace attrs */
    ret = translate_dspace_attr_0_0_1(storage_space, coll_id, new_name,
        new_id, trove_context);
    if(ret < 0)
    {
        fprintf(stderr, "Error: failed to migrate dspace attributes.\n");
        if(verbose) printf("VERBOSE Destroying temporary collection.\n");
        pvfs2_rmspace(storage_space, new_name, new_id, 1, 0);
        return(-1);
    }

    /* convert dspace keyvals */
    ret = translate_keyvals_0_0_1(storage_space, coll_id, new_name,
        new_id, trove_context);
    if(ret < 0)
    {
        fprintf(stderr, "Error: failed to migrate keyvals.\n");
        if(verbose) printf("VERBOSE Destroying temporary collection.\n");
        pvfs2_rmspace(storage_space, new_name, new_id, 1, 0);
        return(-1);
    }

    /* convert dirent_count for each directory */
    ret = translate_dirdata_sizes_0_0_1(storage_space, coll_id, new_name,
        new_id, trove_context);
    if(ret < 0)
    {
        fprintf(stderr, "Error: failed to migrate dirdata sizes.\n");
        if(verbose) printf("VERBOSE Destroying temporary collection.\n");
        pvfs2_rmspace(storage_space, new_name, new_id, 1, 0);
        return(-1);
    }

    /* at this point, we are done with the Trove API */
    trove_close_context(new_id, trove_context);
    trove_finalize();
    PINT_dist_finalize();

    /* convert bstreams */
    ret = translate_bstreams_0_0_1(storage_space, coll_id, new_name,
        new_id, trove_context);
    if(ret < 0)
    {
        fprintf(stderr, "Error: failed to migrate bstreams.\n");
        if(verbose) printf("VERBOSE Destroying temporary collection.\n");
        pvfs2_rmspace(storage_space, new_name, new_id, 1, 0);
        return(-1);
    }

    /* rename old collection */
    snprintf(old_path, PATH_MAX, "%s/%s", storage_space, coll_id);
    snprintf(new_path, PATH_MAX, "%s/%s.old", storage_space, coll_id);
    if(verbose) printf("VERBOSE Renaming old collection.\n");
    ret = rename(old_path, new_path);
    if(ret < 0)
    {
        perror("rename");
        if(verbose) printf("VERBOSE Destroying temporary collection.\n");
        pvfs2_rmspace(storage_space, new_name, new_id, 1, 0);
        return(-1);
    }

    /* rename new collection */
    if(verbose) printf("VERBOSE Renaming migrated collection.\n");
    snprintf(old_path, PATH_MAX, "%s/%.8x", storage_space, new_id);
    snprintf(new_path, PATH_MAX, "%s/%s", storage_space, coll_id);
    ret = rename(old_path, new_path);
    if(ret < 0)
    {
        perror("rename");
        fprintf(stderr, "Error: non recoverable failure while renaming migrated collection.\n");
        return(-1);
    }

    /* remove db entry for temporary collection */
    if(verbose) printf("VERBOSE Removing old reference to migrated collection.\n");
    ret = remove_migration_id_mapping(
        storage_space, new_name);
    if(ret < 0)
    {
        fprintf(stderr, "Warning: non-critical error: failed to remove db entry for temporary collection \"%s\".\n",
            new_name);
    }

    printf("Migration successful.\n");
    printf("===================================================================\n");
    printf("IMPORTANT!!! IMPORTANT!!! IMPORTANT!!! IMPORTANT!!!\n");
    printf("Please delete the old collection once you have tested and confirmed\n");
    printf("the results of the migration.\n");
    printf("Command: \"rm -rf %s/%s.old\"\n", storage_space, coll_id);
    printf("===================================================================\n");

    return(0);
}

/**
 * Migrates collection xattrs from a 0.0.1 DBPF collection
 * \return 0 on succes, -1 on failure
 */
static int translate_coll_eattr_0_0_1(
    char* storage_space,            /**< path to trove storage space */
    char* coll_id,                  /**< collection id in string format */
    char* new_name,                 /**< name of new (temporary) collection */
    TROVE_coll_id new_id,           /**< id of new (temporary) collection */  
    TROVE_context_id trove_context) /**< open trove context */                
{
    int ret = -1;
    char coll_db[PATH_MAX];
    DB *dbp;
    DBT key, data;
    DBC *dbc_p = NULL;
    TROVE_keyval_s t_key;
    TROVE_keyval_s t_val;
    TROVE_op_id op_id;
    int count = 0;
    TROVE_ds_state state;

    sprintf(coll_db, "%s/%s/collection_attributes.db", storage_space, coll_id);
    ret = db_create(&dbp, NULL, 0);
    if(ret != 0)
    {
        fprintf(stderr, "Error: db_create: %s.\n", db_strerror(ret));
        return(-1);
    }
    
    /* open collection_attributes.db from old collection */
    ret = dbp->open(dbp,
#ifdef HAVE_TXNID_PARAMETER_TO_DB_OPEN
                          NULL,
#endif
                          coll_db,
                          NULL,
                          DB_UNKNOWN,
                          0,
                          0);
    if(ret != 0)
    {
        fprintf(stderr, "Error: dbp->open: %s.\n", db_strerror(ret));
        return(-1);
    }

    ret = dbp->cursor(dbp, NULL, &dbc_p, 0);
    if (ret != 0)
    {
        fprintf(stderr, "Error: dbp->cursor: %s.\n", db_strerror(ret));
        dbp->close(dbp, 0);
        return(-1);
    }

    memset(&key, 0, sizeof(key));
    key.data = malloc(DEF_KEY_SIZE);
    if(!key.data)
    {
        perror("malloc");    
        dbc_p->c_close(dbc_p);
        dbp->close(dbp, 0);
        return(-1);
    }
    key.size = key.ulen = DEF_KEY_SIZE;
    key.flags |= DB_DBT_USERMEM;

    memset(&data, 0, sizeof(data));
    data.data = malloc(DEF_DATA_SIZE);
    if(!data.data)
    {
        perror("malloc");    
        free(key.data);
        dbc_p->c_close(dbc_p);
        dbp->close(dbp, 0);
        return(-1);
    }
    data.size = data.ulen = DEF_DATA_SIZE;
    data.flags |= DB_DBT_USERMEM;

    do
    {
        /* iterate through eattr's on the old collection */
        ret = dbc_p->c_get(dbc_p, &key, &data, DB_NEXT);
        if (ret != DB_NOTFOUND && ret != 0)
        {
            fprintf(stderr, "Error: dbc_p->c_get: %s.\n", db_strerror(ret));
            free(data.data);
            free(key.data);
            dbc_p->c_close(dbc_p);
            dbp->close(dbp, 0);
            return(-1);
        }
        /* skip the version attribute- we don't want to copy that one */
        if(ret == 0 && strcmp(key.data, "trove-dbpf-version") != 0)
        {
            if(verbose) printf("VERBOSE Migrating collection eattr: %s\n", (char*)key.data);

            memset(&t_key, 0, sizeof(t_key));
            memset(&t_val, 0, sizeof(t_val));
            t_key.buffer = key.data;
            t_key.buffer_sz = key.size;
            t_val.buffer = data.data;
            t_val.buffer_sz = data.size;

            /* write out new eattr's */
            state = 0;
            ret = trove_collection_seteattr(
                new_id,
                &t_key,
                &t_val,
                0,
                NULL,
                trove_context,
                &op_id);
            while (ret == 0)
            {
                ret = trove_dspace_test(
                    new_id, op_id, trove_context, &count, NULL, NULL,
                    &state, 10);
            }
            if ((ret < 0) || (ret == 1 && state != 0))
            {
                fprintf(stderr, "Error: trove_collection_seteattr failure.\n");
                free(data.data);
                free(key.data);
                dbc_p->c_close(dbc_p);
                dbp->close(dbp, 0);
                return -1; 
            }   
        }
    }while(ret != DB_NOTFOUND);

    free(data.data);
    free(key.data);
    dbc_p->c_close(dbc_p);
    dbp->close(dbp, 0);

    return(0);
}

/**
 * Migrates dspace attrs from a 0.0.1 DBPF collection
 * \return 0 on succes, -1 on failure
 */
static int translate_dspace_attr_0_0_1(
    char* storage_space,            /**< path to trove storage space */
    char* coll_id,                  /**< collection id in string format */
    char* new_name,                 /**< name of new (temporary) collection */
    TROVE_coll_id new_id,           /**< id of new (temporary) collection */  
    TROVE_context_id trove_context) /**< open trove context */                
{
    int ret = -1;
    char attr_db[PATH_MAX];
    DB *dbp;
    DBT key, data;
    DBC *dbc_p = NULL;
    TROVE_op_id op_id;
    int count = 0;
    TROVE_ds_state state;
    TROVE_handle_extent cur_extent;
    TROVE_handle_extent_array extent_array;
    TROVE_handle new_handle;
    TROVE_handle* tmp_handle;
    PVFS_ds_storedattr_0_0_1* tmp_attr;
    TROVE_ds_attributes new_attr;

    sprintf(attr_db, "%s/%s/dataspace_attributes.db", storage_space, coll_id);
    ret = db_create(&dbp, NULL, 0);
    if(ret != 0)
    {
        fprintf(stderr, "Error: db_create: %s.\n", db_strerror(ret));
        return(-1);
    }
    
    /* open dataspace_attributes.db from old collection */
    ret = dbp->open(dbp,
#ifdef HAVE_TXNID_PARAMETER_TO_DB_OPEN
                          NULL,
#endif
                          attr_db,
                          NULL,
                          DB_UNKNOWN,
                          0,
                          0);
    if(ret != 0)
    {
        fprintf(stderr, "Error: dbp->open: %s.\n", db_strerror(ret));
        return(-1);
    }

    ret = dbp->cursor(dbp, NULL, &dbc_p, 0);
    if (ret != 0)
    {
        fprintf(stderr, "Error: dbp->cursor: %s.\n", db_strerror(ret));
        dbp->close(dbp, 0);
        return(-1);
    }

    memset(&key, 0, sizeof(key));
    key.data = malloc(DEF_KEY_SIZE);
    if(!key.data)
    {
        perror("malloc");    
        dbc_p->c_close(dbc_p);
        dbp->close(dbp, 0);
        return(-1);
    }
    key.size = key.ulen = DEF_KEY_SIZE;
    key.flags |= DB_DBT_USERMEM;

    memset(&data, 0, sizeof(data));
    data.data = malloc(DEF_DATA_SIZE);
    if(!data.data)
    {
        perror("malloc");    
        free(key.data);
        dbc_p->c_close(dbc_p);
        dbp->close(dbp, 0);
        return(-1);
    }
    data.size = data.ulen = DEF_DATA_SIZE;
    data.flags |= DB_DBT_USERMEM;

    do
    {
        /* iterate through handles in the old collection */
        ret = dbc_p->c_get(dbc_p, &key, &data, DB_NEXT);
        if (ret != DB_NOTFOUND && ret != 0)
        {
            fprintf(stderr, "Error: dbc_p->c_get: %s.\n", db_strerror(ret));
            free(data.data);
            free(key.data);
            dbc_p->c_close(dbc_p);
            dbp->close(dbp, 0);
            return(-1);
        }
        if(ret == 0)
        {
            tmp_handle = ((PVFS_handle*)key.data);
            tmp_attr = ((PVFS_ds_storedattr_0_0_1*)data.data);

            if(verbose) printf("VERBOSE Migrating attributes for handle: %llu, type: %d\n", 
                llu(*tmp_handle), (int)tmp_attr->type);

            cur_extent.first = cur_extent.last = *tmp_handle;
            extent_array.extent_count = 1;
            extent_array.extent_array = &cur_extent;

            state = 0;
            ret = trove_dspace_create(
                new_id, &extent_array, &new_handle,
                tmp_attr->type, NULL,
                (TROVE_SYNC | TROVE_FORCE_REQUESTED_HANDLE),
                NULL, trove_context, &op_id);

            while (ret == 0)
            {
                ret = trove_dspace_test(new_id, op_id, trove_context,
                                        &count, NULL, NULL, &state,
                                        10);
            }
            if ((ret < 0) || (ret == 1 && state != 0))
            {
                fprintf(stderr, "Error: trove_dspace_create failure.\n");
                free(data.data);
                free(key.data);
                dbc_p->c_close(dbc_p);
                dbp->close(dbp, 0);
                return -1; 
            }
            
            /* convert out of stored format, disregard k_size and b_size
             * (those will be implicitly set later) 
             */
            /* NOTE: we cannot memcpy or use trove_ds_stored_to_attr() macro
             * because the alignment changed after 0.0.1
             */
            new_attr.fs_id = tmp_attr->fs_id;
            new_attr.handle = tmp_attr->handle;
            new_attr.type = tmp_attr->type;
            new_attr.uid = tmp_attr->uid;
            new_attr.gid = tmp_attr->gid;
            new_attr.mode = tmp_attr->mode;
            new_attr.ctime = tmp_attr->ctime;
            new_attr.mtime = tmp_attr->mtime;
            new_attr.atime = tmp_attr->atime;
            new_attr.dfile_count = tmp_attr->dfile_count;
            new_attr.dist_size = tmp_attr->dist_size;
            
            /* write the attributes into the new collection */
            state = 0;
            ret = trove_dspace_setattr(new_id,
                                   *tmp_handle,
                                   &new_attr,
                                   TROVE_SYNC,
                                   NULL,
                                   trove_context,
                                   &op_id);
            while (ret == 0)
            {
                ret = trove_dspace_test( 
                    new_id, op_id, trove_context, &count, NULL, NULL,
                    &state, 10);
            }
            if ((ret < 0) || (ret == 1 && state != 0))
            {
                fprintf(stderr, "Error: trove_dspace_setattr failure.\n");
                free(data.data);
                free(key.data);
                dbc_p->c_close(dbc_p);
                dbp->close(dbp, 0);
                return -1;
            }          
        }
    }while(ret != DB_NOTFOUND);

    free(data.data);
    free(key.data);
    dbc_p->c_close(dbc_p);
    dbp->close(dbp, 0);

    return(0);
}

/**
 * Migrates keyvals from a 0.0.1 DBPF collection
 * \return 0 on succes, -1 on failure
 */
static int translate_keyvals_0_0_1(
    char* storage_space,            /**< path to trove storage space */
    char* coll_id,                  /**< collection id in string format */
    char* new_name,                 /**< name of new (temporary) collection */
    TROVE_coll_id new_id,           /**< id of new (temporary) collection */  
    TROVE_context_id trove_context) /**< open trove context */                
{
    char bucket_dir[PATH_MAX];
    char keyval_db[PATH_MAX];
    int i;
    struct dirent* tmp_ent = NULL;
    DIR* tmp_dir = NULL;
    int ret = -1;
    TROVE_handle tmp_handle;

    /* iterate through bucket dirs */
    for(i = 0; i < KEYVAL_MAX_NUM_BUCKETS_0_0_1; i++)
    {
        snprintf(bucket_dir, PATH_MAX, "%s/%s/keyvals/%.8d", storage_space,
            coll_id, i);
        
        /* printf("VERBOSE Checking %s for keyval files.\n", bucket_dir); */
        tmp_dir = opendir(bucket_dir);
        if(!tmp_dir)
        {
            perror("opendir");
            return(-1);
        }
        /* iterate through xxxx.keyval files */
        while((tmp_ent = readdir(tmp_dir)))
        {
            if(strcmp(tmp_ent->d_name, ".") && strcmp(tmp_ent->d_name, ".."))
            {
                /* scan the handle value out of the file name */
#if SIZEOF_LONG_INT == 4
                ret = sscanf(tmp_ent->d_name, "%llx.keyval",
                    &(llu(tmp_handle)));
#elif SIZEOF_LONG_INT == 8
                ret = sscanf(tmp_ent->d_name, "%lx.keyval",
                    &(tmp_handle));
#else
#error Unexpected sizeof(long int)
#endif
                if(ret != 1)
                {
                    fprintf(stderr, "Error: malformed file name %s in %s\n",
                        tmp_ent->d_name, bucket_dir);
                    closedir(tmp_dir);
                    return(-1);
                }
                snprintf(keyval_db, PATH_MAX, "%s/%s/keyvals/%.8d/%s",
                    storage_space, coll_id, i, tmp_ent->d_name);
                /* translate each keyval db to new format */
                ret = translate_keyval_db_0_0_1(
                    storage_space, coll_id,
                    keyval_db, tmp_handle,
                    new_name, new_id, trove_context);
                if(ret < 0)
                {
                    fprintf(stderr, "Error: failed to migrate %s\n",
                        keyval_db);
                    closedir(tmp_dir);
                    return(-1);
                }
            }
        }
        closedir(tmp_dir);
    }

    return(0);
}

/**
 * Migrates a single keyval db from a 0.0.1 DBPF collection
 * \return 0 on succes, -1 on failure
 */
static int translate_keyval_db_0_0_1(
    char* storage_space,            /**< path to trove storage space */
    char* coll_id,                  /**< collection id in string format */
    char* full_db_path,             /**< fully resolved path to db file */
    TROVE_handle handle,            /**< handle of the object */
    char* new_name,                 /**< name of new (temporary) collection */
    TROVE_coll_id new_id,           /**< id of new (temporary) collection */  
    TROVE_context_id trove_context) /**< open trove context */                
{
    int ret = -1;
    DB *dbp;
    DBT key, data;
    DBC *dbc_p = NULL;
    TROVE_op_id op_id;
    int count = 0;
    TROVE_ds_state state;
    TROVE_keyval_s t_key;
    TROVE_keyval_s t_val;

    if(verbose) printf("VERBOSE Migrating keyvals for handle: %llu\n", llu(handle));

    ret = db_create(&dbp, NULL, 0);
    if(ret != 0)
    {
        fprintf(stderr, "Error: db_create: %s.\n", db_strerror(ret));
        return(-1);
    }
     
    ret = dbp->open(dbp,
#ifdef HAVE_TXNID_PARAMETER_TO_DB_OPEN
                          NULL,
#endif
                          full_db_path,
                          NULL,
                          DB_UNKNOWN,
                          0,
                          0);
    if(ret != 0)
    {
        fprintf(stderr, "Error: dbp->open: %s.\n", db_strerror(ret));
        return(-1);
    }

    ret = dbp->cursor(dbp, NULL, &dbc_p, 0);
    if (ret != 0)
    {
        fprintf(stderr, "Error: dbp->cursor: %s.\n", db_strerror(ret));
        dbp->close(dbp, 0);
        return(-1);
    }

    memset(&key, 0, sizeof(key));
    key.data = malloc(DEF_KEY_SIZE);
    if(!key.data)
    {
        perror("malloc");    
        dbc_p->c_close(dbc_p);
        dbp->close(dbp, 0);
        return(-1);
    }
    key.size = key.ulen = DEF_KEY_SIZE;
    key.flags |= DB_DBT_USERMEM;

    memset(&data, 0, sizeof(data));
    data.data = malloc(DEF_DATA_SIZE);
    if(!data.data)
    {
        perror("malloc");    
        free(key.data);
        dbc_p->c_close(dbc_p);
        dbp->close(dbp, 0);
        return(-1);
    }
    data.size = data.ulen = DEF_DATA_SIZE;
    data.flags |= DB_DBT_USERMEM;

    do
    {
        /* iterate through keys in the old keyval db */
        ret = dbc_p->c_get(dbc_p, &key, &data, DB_NEXT);
        if (ret != DB_NOTFOUND && ret != 0)
        {
            fprintf(stderr, "Error: dbc_p->c_get: %s.\n", db_strerror(ret));
            free(data.data);
            free(key.data);
            dbc_p->c_close(dbc_p);
            dbp->close(dbp, 0);
            return(-1);
        }
        if(ret == 0)
        {
            int tvalbuf_free = 0;
            memset(&t_key, 0, sizeof(t_key));
            memset(&t_val, 0, sizeof(t_val));
            t_key.buffer = key.data;
            t_key.buffer_sz = key.size;
            
            if(!strcmp(t_key.buffer, "metafile_dist"))
            {
                PINT_dist *newdist;
                newdist = data.data;
                
                ret = translate_dist_0_0_1(newdist);
                if(ret != 0)
                {
                    free(data.data);
                    free(key.data);
                    dbc_p->c_close(dbc_p);
                    dbp->close(dbp, 0);
                    return(-1);
                }
                
                t_val.buffer_sz = PINT_DIST_PACK_SIZE(newdist);
                t_val.buffer = malloc(t_val.buffer_sz);
                if(!t_val.buffer)
                {
                    fprintf(stderr, "Error: trove_keyval_write failure.\n");
                    free(data.data);
                    free(key.data);
                    dbc_p->c_close(dbc_p);
                    dbp->close(dbp, 0);
                    return -1; 
                }
                tvalbuf_free = 1;

                PINT_dist_encode(t_val.buffer, newdist);
            } 
            else
            {
                t_val.buffer = data.data;
                t_val.buffer_sz = data.size;
            }
           
            /* write out new keyval pair */
            state = 0;
            ret = trove_keyval_write(
                new_id, handle, &t_key, &t_val, TROVE_SYNC, 0, NULL,
                trove_context, &op_id);

            while (ret == 0)
            {   
                ret = trove_dspace_test(
                    new_id, op_id, trove_context, &count, NULL, NULL,
                    &state, 10);
            }
            
            if(tvalbuf_free)
            {
                tvalbuf_free = 0;
                free(t_val.buffer);
            }

            if ((ret < 0) || (ret == 1 && state != 0))
            {
                fprintf(stderr, "Error: trove_keyval_write failure.\n");
                free(data.data);
                free(key.data);
                dbc_p->c_close(dbc_p);
                dbp->close(dbp, 0);
                return -1; 
            }
        }
    }while(ret != DB_NOTFOUND);

    free(data.data);
    free(key.data);
    dbc_p->c_close(dbc_p);
    dbp->close(dbp, 0);

    return(0);
}

/**
 * Migrates bstream files from a 0.0.1 DBPF collection
 * \return 0 on succes, -1 on failure
 */
static int translate_bstreams_0_0_1(
    char* storage_space,            /**< path to trove storage space */
    char* coll_id,                  /**< collection id in string format */
    char* new_name,                 /**< name of new (temporary) collection */
    TROVE_coll_id new_id,           /**< id of new (temporary) collection */  
    TROVE_context_id trove_context) /**< open trove context */                
{
    char bucket_dir[PATH_MAX];
    char bstream_file[PATH_MAX];
    char new_bstream_file[PATH_MAX];
    int i;
    struct dirent* tmp_ent = NULL;
    DIR* tmp_dir = NULL;
    int ret = -1;

    /* iterate through bucket dirs */
    for(i = 0; i < BSTREAM_MAX_NUM_BUCKETS_0_0_1; i++)
    {
        snprintf(bucket_dir, PATH_MAX, "%s/%s/bstreams/%.8d", storage_space,
            coll_id, i);
        
        /* printf("VERBOSE Checking %s for bstream files.\n", bucket_dir); */
        tmp_dir = opendir(bucket_dir);
        if(!tmp_dir)
        {
            perror("opendir");
            return(-1);
        }
        /* iterate through xxxx.bstream files */
        while((tmp_ent = readdir(tmp_dir)))
        {
            if(strcmp(tmp_ent->d_name, ".") && strcmp(tmp_ent->d_name, ".."))
            {
                if(verbose) printf("VERBOSE Migrating bstream: %s.\n", tmp_ent->d_name);

                snprintf(bstream_file, PATH_MAX, "%s/%s/bstreams/%.8d/%s",
                    storage_space, coll_id, i, tmp_ent->d_name);
                snprintf(new_bstream_file, PATH_MAX, "%s/%.8x/bstreams/%.8d/%s",
                    storage_space, new_id, i, tmp_ent->d_name);
                /* hard link to new location */
                ret = link(bstream_file, new_bstream_file);
                if(ret != 0)
                {
                    perror("link");
                    closedir(tmp_dir);
                    return(-1);
                }
            }
        }
        closedir(tmp_dir);
    }

    return(0);
}

/**
 * Removes the entry for a particular collection from the trove collection db
 * \return 0 on succes, -1 on failure
 */
static int remove_migration_id_mapping(
    char* storage_space, /**< path to trove storage space */
    char* coll_name)     /**< name of collection to remove mapping to */
{
    char coll_db[PATH_MAX];
    int ret;
    DB *dbp;
    DBT key;

    sprintf(coll_db, "%s/collections.db", storage_space);

    ret = db_create(&dbp, NULL, 0);
    if(ret != 0)
    {
        fprintf(stderr, "Error: db_create: %s.\n", db_strerror(ret));
        return(-1);
    }
    
    ret = dbp->open(dbp,
#ifdef HAVE_TXNID_PARAMETER_TO_DB_OPEN
                          NULL,
#endif
                          coll_db,
                          NULL,
                          DB_UNKNOWN,
                          0,
                          0);
    if(ret != 0)
    {
        fprintf(stderr, "Error: dbp->open: %s.\n", db_strerror(ret));
        return(-1);
    }

    memset(&key, 0, sizeof(key));
    key.data = coll_name;
    key.size = strlen(coll_name) + 1;

    ret = dbp->del(dbp, NULL, &key, 0);
    if (ret != 0)
    {
        fprintf(stderr, "Error: dbp->del: %s\n", db_strerror(ret));
        return(-1);
    }

    ret = dbp->sync(dbp, 0);
    if (ret != 0)
    {
        fprintf(stderr, "Error: dbp->sync: %s\n", db_strerror(ret));
        return(-1);
    }

    dbp->close(dbp, 0);
    return(0);
}

/**
 * Checks to confirm that a collection name is not in use
 * \return 0 on succes, -1 on failure
 */
static int confirm_coll_name_not_used(
    char* storage_space, /**< path to trove storage space */
    char* coll_name)     /**< name of collection to check */
{
    char coll_db[PATH_MAX];
    int ret;
    DB *dbp;
    DBT key;
    DBT data;

    sprintf(coll_db, "%s/collections.db", storage_space);

    ret = db_create(&dbp, NULL, 0);
    if(ret != 0)
    {
        fprintf(stderr, "Error: db_create: %s.\n", db_strerror(ret));
        return(-1);
    }
    
    ret = dbp->open(dbp,
#ifdef HAVE_TXNID_PARAMETER_TO_DB_OPEN
                          NULL,
#endif
                          coll_db,
                          NULL,
                          DB_UNKNOWN,
                          0,
                          0);
    if(ret != 0)
    {
        fprintf(stderr, "Error: dbp->open: %s.\n", db_strerror(ret));
        return(-1);
    }

    memset(&key, 0, sizeof(key));
    key.data = coll_name;
    key.size = strlen(coll_name) + 1;
    
    memset(&data, 0, sizeof(data));
    data.data = malloc(DEF_DATA_SIZE);
    if(!data.data)
    {
        perror("malloc");
        dbp->close(dbp, 0);
        return(-1);
    }
    data.size = data.ulen = DEF_DATA_SIZE;
    data.flags |= DB_DBT_USERMEM;

    ret = dbp->get(dbp, NULL, &key, &data, 0);
    if(ret == 0)
    {
        free(data.data);
        dbp->close(dbp, 0);
        if(verbose) printf("VERBOSE removing old temporary collection mapping: %s.\n", coll_name);
        ret = remove_migration_id_mapping(
            storage_space, coll_name);
        if(ret < 0)
        {

        }
        return(ret);
    }

    free(data.data);
    dbp->close(dbp, 0);
    return(0);
}

/**
 * Checks to confirm that a subdirectory does not exist within the storage
 * space
 * \return 0 on succes, -1 on failure
 */
static int confirm_subdir_not_used(
    char* storage_space,  /**< path to trove storage space */
    char* subdir)         /**< name of subdirectory to check */
{
    char full_path[PATH_MAX];
    int ret = -1;

    sprintf(full_path, "%s/%s", storage_space, subdir);

    ret = access(full_path, F_OK);
    if(ret == 0)
    {
        if(verbose) printf("VERBOSE %s already exists.\n", full_path);
        return(-1);
    }

    return(0);
}

/**
 * Finds an unused collection id to use as a temporary collection for
 * migration
 * \return 0 on succes, -1 on failure
 */
static int get_migration_id(
    char* storage_space,   /**< path to trove storage space */
    TROVE_coll_id* out_id) /**< collection id */
{
    int ret = -1;
    char subdir[PATH_MAX];

    for(*out_id = 0x1337; (*out_id) < INT_MAX; (*out_id)++)
    {
        sprintf(subdir, "%.8x", *out_id);
        ret = confirm_subdir_not_used(storage_space, subdir);
        if(ret == 0)
        {
            if(verbose) printf("VERBOSE Using collection id %.8x for migration.\n",
                *out_id);
            return(0);
        }
    }

    fprintf(stderr, "Error: could not find an available collection id.\n");
    return(-1);
}

/**
 * iterate through all of the directories in the old collection and find out
 * how many entries they had in the dirdata object.  Write that value into
 * the new collection (new versions of trove store this explicitly in a db
 * key)
 * \return 0 on success -1 on failure
 */
static int translate_dirdata_sizes_0_0_1(
    char* storage_space, char* coll_id, char* new_name, TROVE_coll_id new_id,
    TROVE_context_id trove_context)
{
    int ret = -1;
    char attr_db[PATH_MAX];
    char dir_db[PATH_MAX];
    char dirdata_db[PATH_MAX];
    DB *dbp;
    DB *dir_dbp;
    DB *dirdata_dbp;
    DBT key, data;
    DBT keyB, dataB;
    DBC *dbc_p = NULL;
    TROVE_op_id op_id;
    TROVE_handle tmp_handle;
    TROVE_handle tmp_dirdata_handle;
    PVFS_ds_storedattr_0_0_1* tmp_attr;
    DB_BTREE_STAT *k_stat_p = NULL;    
    PVFS_size dirent_count;
    unsigned int coll_id_value;
    TROVE_ds_state state;
    TROVE_keyval_s t_key;
    TROVE_keyval_s t_val;
    int count = 0;
    
    /* scan the coll_id value out of the string name */
    ret = sscanf(coll_id, "%x", &coll_id_value);
    if(ret != 1)
    {
        fprintf(stderr, "Error: malformed collection name %s\n",
            coll_id);
        return(-1);
    }

    sprintf(attr_db, "%s/%s/dataspace_attributes.db", storage_space, coll_id);
    ret = db_create(&dbp, NULL, 0);
    if(ret != 0)
    {
        fprintf(stderr, "Error: db_create: %s.\n", db_strerror(ret));
        return(-1);
    }
    
    /* open dataspace_attributes.db from old collection */
    ret = dbp->open(dbp,
#ifdef HAVE_TXNID_PARAMETER_TO_DB_OPEN
                          NULL,
#endif
                          attr_db,
                          NULL,
                          DB_UNKNOWN,
                          0,
                          0);
    if(ret != 0)
    {
        fprintf(stderr, "Error: dbp->open: %s.\n", db_strerror(ret));
        return(-1);
    }

    ret = dbp->cursor(dbp, NULL, &dbc_p, 0);
    if (ret != 0)
    {
        fprintf(stderr, "Error: dbp->cursor: %s.\n", db_strerror(ret));
        dbp->close(dbp, 0);
        return(-1);
    }

    memset(&key, 0, sizeof(key));
    key.data = malloc(DEF_KEY_SIZE);
    if(!key.data)
    {
        perror("malloc");    
        dbc_p->c_close(dbc_p);
        dbp->close(dbp, 0);
        return(-1);
    }
    key.size = key.ulen = DEF_KEY_SIZE;
    key.flags |= DB_DBT_USERMEM;

    memset(&data, 0, sizeof(data));
    data.data = malloc(DEF_DATA_SIZE);
    if(!data.data)
    {
        perror("malloc");    
        free(key.data);
        dbc_p->c_close(dbc_p);
        dbp->close(dbp, 0);
        return(-1);
    }
    data.size = data.ulen = DEF_DATA_SIZE;
    data.flags |= DB_DBT_USERMEM;

    memset(&keyB, 0, sizeof(keyB));
    keyB.size = keyB.ulen = 0;
    keyB.flags |= DB_DBT_USERMEM;

    memset(&dataB, 0, sizeof(dataB));
    dataB.size = dataB.ulen = 0;
    dataB.flags |= DB_DBT_USERMEM;

    do
    {
        /* iterate through handles in the old collection */
        key.size = key.ulen = DEF_KEY_SIZE;
        ret = dbc_p->c_get(dbc_p, &key, &data, DB_NEXT);
        if (ret != DB_NOTFOUND && ret != 0)
        {
            fprintf(stderr, "Error: dbc_p->c_get: %s.\n", db_strerror(ret));
            free(data.data);
            free(key.data);
            dbc_p->c_close(dbc_p);
            dbp->close(dbp, 0);
            return(-1);
        }
        if(ret == 0)
        {
            tmp_handle = *((PVFS_handle*)key.data);
            tmp_attr = ((PVFS_ds_storedattr_0_0_1*)data.data);

            if(tmp_attr->type == PVFS_TYPE_DIRECTORY)
            {
                if(verbose) printf("VERBOSE Migrating dirdata_size for handle: %llu\n", 
                    llu(tmp_handle));
                
                /* find the keyval db for the directory */
                DBPF_GET_KEYVAL_DBNAME_0_0_1(dir_db, (PATH_MAX-1), 
                    storage_space, coll_id_value, tmp_handle);
                
                ret = db_create(&dir_dbp, NULL, 0);
                if(ret != 0)
                {
                    fprintf(stderr, "Error: db_create: %s.\n", db_strerror(ret));
                    free(data.data);
                    free(key.data);
                    dbc_p->c_close(dbc_p);
                    dbp->close(dbp, 0);
                    return(-1);
                }
                
                ret = dir_dbp->open(dir_dbp,
            #ifdef HAVE_TXNID_PARAMETER_TO_DB_OPEN
                                      NULL,
            #endif
                                      dir_db,
                                      NULL,
                                      DB_UNKNOWN,
                                      0,
                                      0);
                if(ret != 0)
                {
                    fprintf(stderr, "Error: dir_dbp->open: %s.\n", db_strerror(ret));
                    free(data.data);
                    free(key.data);
                    dbc_p->c_close(dbc_p);
                    dbp->close(dbp, 0);
                    return(-1);
                }
                    
                /* read out the dirdata handle */
                keyB.data = "dir_ent";
                keyB.size = strlen("dir_ent") + 1;
                dataB.data = &tmp_dirdata_handle;
                dataB.size = dataB.ulen = sizeof(PVFS_handle);
                ret = dir_dbp->get(dir_dbp, NULL, &keyB, &dataB, 0);
                if(ret != 0)
                {
                    fprintf(stderr, "Error: dir_dbp->get: %s\n", db_strerror(ret));
                    free(data.data);
                    free(key.data);
                    dbc_p->c_close(dbc_p);
                    dbp->close(dbp, 0);
                    dir_dbp->close(dbp, 0);
                    return(-1);
                }

                dir_dbp->close(dir_dbp, 0);
                
                /* find the dirdata db for the directory */
                DBPF_GET_KEYVAL_DBNAME_0_0_1(dirdata_db, (PATH_MAX-1), 
                    storage_space, coll_id_value, tmp_dirdata_handle);
                
                ret = db_create(&dirdata_dbp, NULL, 0);
                if(ret != 0)
                {
                    fprintf(stderr, "Error: db_create: %s.\n", db_strerror(ret));
                    free(data.data);
                    free(key.data);
                    dbc_p->c_close(dbc_p);
                    dbp->close(dbp, 0);
                    return(-1);
                }
                
                dirent_count = 0;
                ret = dirdata_dbp->open(dirdata_dbp,
            #ifdef HAVE_TXNID_PARAMETER_TO_DB_OPEN
                                      NULL,
            #endif
                                      dirdata_db,
                                      NULL,
                                      DB_UNKNOWN,
                                      0,
                                      0);
                if(ret == 0)
                {
                    /* found out how many keys were in the dirdata db */
                    ret = dirdata_dbp->stat(dirdata_dbp,
    #ifdef HAVE_TXNID_PARAMETER_TO_DB_STAT
                                     (DB_TXN *) NULL,
    #endif
                                     &k_stat_p,
    #ifdef HAVE_UNKNOWN_PARAMETER_TO_DB_STAT
                                     NULL,
    #endif
                                     0);
                    if(ret != 0)
                    {
                        fprintf(stderr, "Error: dirdata_dbp->stat: %s.\n", db_strerror(ret));
                        free(data.data);
                        free(key.data);
                        dbc_p->c_close(dbc_p);
                        dbp->close(dbp, 0);
                        dirdata_dbp->close(dbp, 0);
                        return(-1);
                    }
                    dirent_count = (PVFS_size) k_stat_p->bt_ndata;
                    free(k_stat_p);

                    dirdata_dbp->close(dirdata_dbp, 0);
                }
                else
                {
                    fprintf(stderr, "WARNING: could not find dirdata object: %s\n", dirdata_db);
                }

                if(verbose) printf("VERBOSE    size: %lld\n", lld(dirent_count)); 

                /* write the dirent_count out into new collection */ 
                memset(&t_key, 0, sizeof(t_key));
                memset(&t_val, 0, sizeof(t_val));
                t_key.buffer = "dirdata_size";
                t_key.buffer_sz = 13;
                t_val.buffer = &dirent_count;
                t_val.buffer_sz = sizeof(PVFS_size);
 
                state = 0;
                ret = trove_keyval_write(
                    new_id, tmp_handle, &t_key, &t_val,
                    TROVE_SYNC, 0, NULL,
                    trove_context, &op_id);
                
                while (ret == 0)
                {   
                    ret = trove_dspace_test(
                        new_id, op_id, trove_context, &count, NULL, NULL,
                        &state, 10);
                }
                if ((ret < 0) || (ret == 1 && state != 0))
                {
                    fprintf(stderr, "Error: trove_keyval_write failure.\n");
                    free(data.data);
                    free(key.data);
                    dbc_p->c_close(dbc_p);
                    dbp->close(dbp, 0);
                    return -1; 
                }
            }
        } 
     }while(ret != DB_NOTFOUND);

    free(data.data);
    free(key.data);
    dbc_p->c_close(dbc_p);
    dbp->close(dbp, 0);

    return(0);
}

/*
 * convert integers to pointers in dist
 * with no copy - dist is modified
 */
int translate_dist_0_0_1(PINT_dist *dist)
{
        if(!dist)
        {
            return 0;
        }
	/* convert ints in dist to pointers */
	dist->dist_name = (char *) dist + (intptr_t) dist->dist_name;
	dist->params = (void *) ((char *) dist + (intptr_t) dist->params);
	/* set methods */
	dist->methods = NULL;
	if (PINT_dist_lookup(dist)) {
	    fprintf(stderr, "Error: %s: lookup dist failed\n", __func__);
            return -1;
	}
        return 0;
}

/* @} */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
