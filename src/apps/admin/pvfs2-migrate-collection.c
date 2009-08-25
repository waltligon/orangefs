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
#include "pint-util.h"

#ifndef PVFS2_VERSION
#define PVFS2_VERSION "Unknown"
#endif

typedef struct
{
    char fs[100];
    int fs_set;
    int all_set;
    int cleanup_set;
    char alias[100];
    int alias_set;
    char fs_conf[PATH_MAX];
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
    char* storage_space, TROVE_coll_id coll_id, char* coll_name,
    char* ver_string, int ver_string_max);
static int remove_collection_entry(char* storage_space, char* collname);

int migrate_collection(void * config, void * sconfig);
void fs_config_dummy_free(void *);
int recursive_rmdir(char* dir);

/* functions specific to reading 0.0.1 collections */
static int src_get_version_0_0_1(
    char* storage_space, TROVE_coll_id coll_id, 
    char* ver_string, int ver_string_max);
static int translate_0_0_1(
    char* storage_space, char* old_coll_path, 
    char* coll_name, TROVE_coll_id coll_id);
static int translate_coll_eattr_0_0_1(
    char* old_coll_path, TROVE_coll_id coll_id, char* coll_name,
    TROVE_context_id trove_context);
static int translate_dspace_attr_0_0_1(
    char* old_coll_path, TROVE_coll_id coll_id, char* coll_name,
    TROVE_context_id trove_context);
static int translate_keyvals_0_0_1(
    char* old_coll_path, TROVE_coll_id coll_id, char* coll_name,
    TROVE_context_id trove_context);
static int translate_bstreams_0_0_1(
    char* storage_space, char* old_coll_path, 
    TROVE_coll_id coll_id, char* coll_name,
    TROVE_context_id trove_context);
static int translate_keyval_db_0_0_1(
    TROVE_coll_id coll_id, char* full_db_path, 
    TROVE_handle handle, char* coll_name, 
    TROVE_context_id trove_context);
static int translate_dist_0_0_1(
    PINT_dist * dist);
static int translate_keyval_key_0_0_1(TROVE_keyval_s * keyval, DBT * db_key);

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
#define DBPF_GET_KEYVAL_DBNAME_0_0_1(__b, __pm, __collpath, __cid, __handle)  \
do {                                                                          \
  snprintf(__b, __pm, "/%s/%s/%.8llu/%08llx.keyval", __collpath,              \
  KEYVAL_DIRNAME_0_0_1,                                                       \
  llu(DBPF_KEYVAL_GET_BUCKET_0_0_1(__handle, __cid)), llu(__handle));         \
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

static options_t opts;


int main(int argc, char **argv)
{
    int ret = -1;

    /* all parameters read in from fs.conf */
    struct server_configuration_s server_config;
    PINT_llist_p fs_configs;
    char *server_alias;
    
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

    if(opts.alias_set)
    {
        server_alias = opts.alias;
    }
    else
    {
        server_alias = PINT_util_guess_alias();
    }

    ret = PINT_parse_config(&server_config, opts.fs_conf, server_alias);
    if(ret < 0)
    {
        gossip_err("Error: Please check your config files.\n");
        if(!opts.alias_set)
        {
            free(server_alias);
        }
        return -1;
    }

    if(!opts.alias_set)
    {
        free(server_alias);
    }

    if(opts.all_set)
    {
        /* get all the collection ids from the fs config */
        fs_configs = PINT_config_get_filesystems(&server_config);
        
    }
    else
    {
        /* get the collection id from the specified fs name */
        PVFS_fs_id fs_id = PINT_config_get_fs_id_by_fs_name(
            &server_config, opts.fs);
        fs_configs = PINT_llist_new();
        PINT_llist_add_to_head(
            fs_configs, 
            (void *)PINT_config_find_fs_id(&server_config, fs_id));
    }

    ret = PINT_llist_doall_arg(fs_configs, migrate_collection, &server_config);
    if(ret < 0)
    {
        PINT_config_release(&server_config);
        if(!opts.all_set)
        {
            PINT_llist_free(fs_configs, fs_config_dummy_free);
        }

        return(-1);
    }
   
    return 0;
}

int migrate_collection(void * config, void * sconfig)
{
    char old_coll_path[PATH_MAX];
    char version[256];
    int ret;

    struct filesystem_configuration_s * fs_config = 
        (struct filesystem_configuration_s *) config;
    struct server_configuration_s * server_config =
        (struct server_configuration_s *) sconfig;

    memset(version, 0, 256);
    /* find version of source storage space */
    ret = src_get_version(
        server_config->storage_path, 
        fs_config->coll_id, 
        fs_config->file_system_name,
        version, 254);
    if(ret < 0)
    {
        fprintf(stderr, 
                "Error: failed to read version of src storage space\n"
                "       for filesystem: %s (%08x)\n",
                fs_config->file_system_name, fs_config->coll_id);
        return ret;
    }
        
    /* call the appropriate translation routine based on the version */
    if(strncmp(version, "0.0.1", 5) == 0)
    {
        sprintf(old_coll_path, "%s/%08x-old-%s",
                server_config->storage_path, 
                fs_config->coll_id, version);

        ret = access(old_coll_path, F_OK);
        if(ret == 0)
        {
            if(opts.cleanup_set)
            {
                /* user asked to remove this old collection instead
                 * of creating it
                 */
                if(verbose) printf("VERBOSE Removing old collection at: %s\n",
                                   old_coll_path);
                ret = recursive_rmdir(old_coll_path);
                if(ret < 0)
                {
                    fprintf(stderr, 
                            "Error: failed to remove %s\n", 
                            old_coll_path);
                    return -1;
                }
                return 0;
            }

            if(verbose) printf("VERBOSE %s already exists.\n", 
                               old_coll_path);
            fprintf(stderr, 
                    "Error: unable to confirm availability of backup subdirectory (%s)\n"
                    "       for migration of fs: %s (%08x).\n", 
                    old_coll_path,
                    fs_config->file_system_name,
                    fs_config->coll_id);
            fprintf(stderr, 
                    "Error: please make sure that the migration "
                    "has not already been performed.\n");
            return -1;
        }

        ret = translate_0_0_1(
            server_config->storage_path, old_coll_path, 
            fs_config->file_system_name, 
            fs_config->coll_id);
        if(ret < 0)
        {
            fprintf(stderr, 
                    "Error: failed to translate from %s collection\n"
                    "       for fs: %s (%08x).\n",
                    version, fs_config->file_system_name, fs_config->coll_id);
            return -1;
        }

        if(opts.cleanup_set)
        {
            /* user asked to remove this old collection instead
             * of creating it
             */
            if(verbose) printf("VERBOSE Removing old collection at: %s\n",
                               old_coll_path);
            ret = unlink(old_coll_path);
            if(ret < 0)
            {
                perror("unlink");
                return -1;
            }
        }
    }
    else
    {
        if(opts.cleanup_set)
        {
            /* user asked to remove this old collection instead
             * of creating it, but we don't know what the version
             * is anymore
             */
            DIR * storage_dir;
            struct dirent * next_dirent;
            char collname[PATH_MAX];
            int collname_length;
            int removed_olddirs = 0;

            collname_length = sprintf(collname, "%08x-old", fs_config->coll_id);

            storage_dir = opendir(server_config->storage_path);
            if(!storage_dir)
            {
                fprintf(stderr, "Error: failed to open directory: %s\n",
                        server_config->storage_path);
                return -1;
            }

            while((next_dirent = readdir(storage_dir)) != NULL)
            {
                int d_namelen = strlen(next_dirent->d_name);
                if(collname_length < d_namelen &&
                   strncmp(next_dirent->d_name, collname, collname_length) == 0)
                {
                    char old_coll_path[PATH_MAX];

                    sprintf(old_coll_path, "%s/%s",
                            server_config->storage_path, next_dirent->d_name);

                    /* found an old version, delete it */
                    if(verbose) 
                        printf("VERBOSE Removing old collection at: %s\n",
                               old_coll_path);
                    ret = recursive_rmdir(old_coll_path);
                    if(ret < 0)
                    {
                        fprintf(
                            stderr, 
                            "Error: failed to remove old collection at: %s\n",
                            old_coll_path);
                        closedir(storage_dir);
                        return -1;
                    }
                    removed_olddirs = 1;
                }
            }

            if(removed_olddirs == 0)
            {
                printf("\nWARNING: No old collections with name \"%s\" "
                       "were found to cleanup.\n",
                       fs_config->file_system_name);
            }

            closedir(storage_dir);
        }
        else
        {
            /* complain if we don't recognize the version */
            fprintf(stderr, 
                    "Error: unknown collection version: %s\n"
                    "       for fs: %s (%08x).\n", 
                    version, fs_config->file_system_name, fs_config->coll_id);
            return -1;
        }
    }

    return 0;
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
    static struct option long_opts[] =
    {
        {"help",0,0,0},
        {"verbose",0,0,0},
        {"version",0,0,0},
        {"fs",1,0,0},
        {"all",0,0,0},
        {"alias",1,0,0},
        {"cleanup",0,0,0},
        {0,0,0,0}
    };

    memset(opts, 0, sizeof(options_t));

    while ((ret = getopt_long(argc, argv, "",
                              long_opts, &option_index)) != -1)
    {
	switch (option_index)
        {
            case 0: /* help */
                print_help(argv[0]);
                exit(0);

            case 1: /* verbose */
                verbose = 1;
                break;

            case 2: /* version */
                fprintf(stderr,"%s\n",PVFS2_VERSION);
                exit(0);

            case 3: /* fs */
                    strncpy(opts->fs, optarg, 99);
                    opts->fs_set = 1;
                    break;

            case 4: /* all */
                    opts->all_set = 1;
                    break;

            case 5: /* alias */
                    strncpy(opts->alias, optarg, 99);
                    opts->alias_set = 1;
                    break;

            case 6: /* cleanup */
                    opts->cleanup_set = 1;
                    break;
	    default:
                print_help(argv[0]);
		return(-1);
	}
        option_index = 0;
    }

    /* one of the two options -fs or -all must be set. */
    if (!opts->fs_set && !opts->all_set)
    {
        print_help(argv[0]);
        return(-1);
    }

    /* only one of the two options -fs or -all can be set. */
    if (opts->fs_set && opts->all_set)
    {
        print_help(argv[0]);
        return(-1);
    }

    if(argc < optind)
    {
        /* missing fs.conf */
        print_help(argv[0]);
        return(-1);
    }
    strcpy(opts->fs_conf, argv[optind++]);

    return 0;
}

/**
 * Prints help for command line arguments
 */
static void print_help(
    char *progname) /**< executable name */
{
    fprintf(stderr,"\nusage: %s \\\n\t\t[OPTIONS] <global_config_file>\n", progname);
    fprintf(stderr,"\nThis utility will migrate a PVFS2 collection from an old version\n"
           "to the most recent version.\n\n");
    fprintf(stderr,"One of the following arguments is required:\n");
    fprintf(stderr,"--------------\n");
    fprintf(stderr,"  --fs=<fs name>     "
            "name of the file system to migrate\n");
    fprintf(stderr,"  --all              "
            "migrate all collections in the filesystem config\n");
    fprintf(stderr, "\n");
    fprintf(stderr,"The following arguments are optional:\n");
    fprintf(stderr,"--------------\n");
    fprintf(stderr,"  --cleanup          "
            "remove the old collection\n");
    fprintf(stderr,
            "  --alias            Specify the alias for this server.\n"
            "                     The migration tool tries to guess the\n"
            "                     alias based on the hostname if none is specified.\n");
    fprintf(stderr,"  --verbose          "
            "print verbose messages during execution\n");
    fprintf(stderr,"  --help             "
            "show this help listing\n");
    fprintf(stderr,"  --version          "
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
    char* storage_space,       /**< path to storage space */
    TROVE_coll_id coll_id,     /**< collection id */
    char* coll_name,           /**< collection name */
    char* ver_string,          /**< version in string format */
    int ver_string_max)        /**< maximum size of version string */
{
    int ret = -1;


    ret = src_get_version_0_0_1(
        storage_space, coll_id, ver_string, ver_string_max);

    if(ret != 0)
    {
        fprintf(stderr, 
                "Error: all known collection version checks "
                "failed for \ncollection %s (%08x) in storage space %s\n", 
                coll_name, coll_id, storage_space);
    }

    return(ret);
}


/**
 * Reads the version number from a 0.0.1 DBPF collection
 * \return 0 on succes, -1 on failure
 */
static int src_get_version_0_0_1(
    char* storage_space,   /**< path to storage space */
    TROVE_coll_id coll_id, /**< collection id */
    char* ver_string,      /**< version in string format */
    int ver_string_max)    /**< maximum size of version string */
{
    char coll_db[PATH_MAX];
    int ret;
    DB *dbp;
    DBT key, data;

    sprintf(coll_db, "%s/%08x/collection_attributes.db", 
            storage_space, coll_id);

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
    char* storage_space,   /**< path to storage space */
    char* old_coll_path,   /**< path to old collection */
    char* coll_name,       /**< collection name */
    TROVE_coll_id coll_id) /**< collection id in string format */
{
    int ret = -1;
    /* choose a handle range big enough to encompass anything pvfs2-genconfig
     * will create
     */
    char handle_range[] = "4-64000000000";
    TROVE_op_id op_id;
    TROVE_context_id trove_context = -1;
    char current_path[PATH_MAX];

    /* rename old collection */
    snprintf(current_path, PATH_MAX, "%s/%08x", storage_space, coll_id);

    if(access(current_path, F_OK) != 0)
    {
        fprintf(stderr, 
                "Error: could not find old collection: %s\n"
                "       fs: %s (%08x)\n",
                old_coll_path, coll_name, coll_id);
        return -1;
    }
                        
    if(verbose) printf("VERBOSE Renaming old collection.\n");
    ret = rename(current_path, old_coll_path);
    if(ret < 0)
    {
        perror("rename");
        return(-1);
    }

    ret = remove_collection_entry(storage_space, coll_name);
    if(ret < 0)
    {
        fprintf(stderr, "Error: failed to remove collection entry: %s\n",
                coll_name);
        return(-1);
    }
    
    /* create new collection */
    /* NOTE: deliberately not specifying root handle; it will get translated
     * later as a normal directory if applicable
     */
    if(verbose) 
        printf("VERBOSE Creating temporary collection to migrate to.\n");
    ret = pvfs2_mkspace(
        storage_space, 
        coll_name,
        coll_id, 
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
        pvfs2_rmspace(storage_space, coll_name, coll_id, 1, 0);
        return(-1);
    }

    /* initialize trove and lookup collection */
    ret = trove_initialize(
        TROVE_METHOD_DBPF, NULL, storage_space, 0);
    if (ret < 0)
    {
        PVFS_perror("trove_initialize", ret);
        if(verbose) printf("VERBOSE Destroying temporary collection.\n");
        pvfs2_rmspace(storage_space, coll_name, coll_id, 1, 0);
        return(-1);
    }
    ret = trove_collection_lookup(
        TROVE_METHOD_DBPF, coll_name, &coll_id, NULL, &op_id);
    if (ret != 1)
    {   
        fprintf(stderr, "Error: failed to lookup new collection.\n");
        if(verbose) printf("VERBOSE Destroying temporary collection.\n");
        pvfs2_rmspace(storage_space, coll_name, coll_id, 1, 0);
        return -1; 
    }   

    ret = trove_open_context(coll_id, &trove_context);
    if (ret < 0)
    {
        PVFS_perror("trove_open_context", ret);
        return(-1);
    }

    /* convert collection xattrs */
    ret = translate_coll_eattr_0_0_1(
        old_coll_path, coll_id, coll_name, trove_context);
    if(ret < 0)
    {
        fprintf(stderr, "Error: failed to migrate collection extended attributes.\n");
        if(verbose) printf("VERBOSE Destroying temporary collection.\n");
        pvfs2_rmspace(storage_space, coll_name, coll_id, 1, 0);
        return(-1);
    }

    /* convert dspace attrs */
    ret = translate_dspace_attr_0_0_1(
        old_coll_path, coll_id, coll_name, trove_context);
    if(ret < 0)
    {
        fprintf(stderr, "Error: failed to migrate dspace attributes.\n");
        if(verbose) printf("VERBOSE Destroying temporary collection.\n");
        pvfs2_rmspace(storage_space, coll_name, coll_id, 1, 0);
        return(-1);
    }

    /* convert dspace keyvals */
    ret = translate_keyvals_0_0_1(
        old_coll_path, coll_id, coll_name, trove_context);
    if(ret < 0)
    {
        fprintf(stderr, "Error: failed to migrate keyvals.\n");
        if(verbose) printf("VERBOSE Destroying temporary collection.\n");
        pvfs2_rmspace(storage_space, coll_name, coll_id, 1, 0);
        return(-1);
    }

    /* at this point, we are done with the Trove API */
    trove_close_context(coll_id, trove_context);
    trove_finalize(TROVE_METHOD_DBPF);
    PINT_dist_finalize();

    /* convert bstreams */
    ret = translate_bstreams_0_0_1(
        storage_space, old_coll_path, coll_id, coll_name, trove_context);
    if(ret < 0)
    {
        fprintf(stderr, "Error: failed to migrate bstreams.\n");
        if(verbose) printf("VERBOSE Destroying temporary collection.\n");
        pvfs2_rmspace(storage_space, coll_name, coll_id, 1, 0);
        return(-1);
    }

    printf("Migration successful.\n");

    if(!opts.cleanup_set)
    {
        printf("===================================================================\n");
        printf("IMPORTANT!!! IMPORTANT!!! IMPORTANT!!! IMPORTANT!!!\n");
        printf("Please delete the old collection once you have tested and confirmed\n");
        printf("the results of the migration.\n");
        printf("Command: \"pvfs2-migrate-collection -cleanup <fs config> <server config>\"\n");
        printf("===================================================================\n");
    }

    return(0);
}

static int remove_collection_entry(char* storage_space, char* collname)
{
    char collections_db[PATH_MAX];
    DB * dbp;
    DBT key, data;
    int ret = 0;
    TROVE_coll_id coll_id;

    sprintf(collections_db, "%s/collections.db", storage_space);

    ret = access(collections_db, F_OK);
    if(ret == -1 && errno == ENOENT)
    {
        fprintf(stderr, "Error: could not find %s.\n", collections_db);
        fprintf(stderr, "Error: src directory is not a known format.\n");
        return ret;
    }
    else if(ret == -1)
    {
        fprintf(stderr, "access(%s): %s\n", collections_db, strerror(errno));
        return -1;
    }

    ret = db_create(&dbp, NULL, 0);
    if(ret != 0)
    {
        fprintf(stderr, "Error: db_create: %s.\n", db_strerror(ret));
        return -1;
    }

    ret = dbp->open(dbp,
#ifdef HAVE_TXNID_PARAMETER_TO_DB_OPEN
                    NULL,
#endif
                    collections_db,
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

    key.data = collname;
    key.size = strlen(collname) + 1;
    data.data = &coll_id;
    data.ulen = sizeof(coll_id);
    data.flags = DB_DBT_USERMEM;

    ret = dbp->get(dbp, NULL, &key, &data, 0);
    if (ret != 0)
    {
        fprintf(stderr, "Error: dbp->get: %s\n", db_strerror(ret));
        return -1;
    }

    ret = dbp->del(dbp, NULL, &key, 0);
    if (ret != 0)
    {
        fprintf(stderr, "Error: dbp->del: %s\n", db_strerror(ret));
        return -1;
    }

    ret = dbp->sync(dbp, 0);
    if (ret != 0)
    {
        fprintf(stderr, "Error: dbp->sync: %s\n", db_strerror(ret));
        return -1;
    }

    dbp->close(dbp, 0);
    return 0;
}

/**
 * Migrates collection xattrs from a 0.0.1 DBPF collection
 * \return 0 on succes, -1 on failure
 */
static int translate_coll_eattr_0_0_1(
    char* old_coll_path,            /**< path to old trove collection */
    TROVE_coll_id coll_id,          /**< collection id in string format */
    char* coll_name,                /**< name of collection */
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

    sprintf(coll_db, "%s/collection_attributes.db", old_coll_path);
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
        if(ret == 0 && strncmp(key.data, "trove-dbpf-version", 
                               strlen("trove-dbpf-version")) != 0)
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
                coll_id,
                &t_key,
                &t_val,
                0,
                NULL,
                trove_context,
                &op_id);
            while (ret == 0)
            {
                ret = trove_dspace_test(
                    coll_id, op_id, trove_context, &count, NULL, NULL,
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
    char* old_coll_path,            /**< path to old collection */
    TROVE_coll_id coll_id,          /**< collection id */
    char* coll_name,                /**< name of collection */
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

    sprintf(attr_db, "%s/dataspace_attributes.db", old_coll_path);
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
                coll_id, &extent_array, &new_handle,
                tmp_attr->type, NULL,
                (TROVE_SYNC | TROVE_FORCE_REQUESTED_HANDLE),
                NULL, trove_context, &op_id, NULL);

            while (ret == 0)
            {
                ret = trove_dspace_test(coll_id, op_id, trove_context,
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
            new_attr.u.metafile.dfile_count = tmp_attr->dfile_count;
            new_attr.u.metafile.dist_size = tmp_attr->dist_size;
            
            /* write the attributes into the new collection */
            state = 0;
            ret = trove_dspace_setattr(coll_id,
                                       *tmp_handle,
                                       &new_attr,
                                       TROVE_SYNC,
                                       NULL,
                                       trove_context,
                                       &op_id, NULL);
            while (ret == 0)
            {
                ret = trove_dspace_test( 
                    coll_id, op_id, trove_context, &count, NULL, NULL,
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
    char* old_coll_path,            /**< path to old collection */
    TROVE_coll_id coll_id,          /**< collection id */
    char* coll_name,                /**< name of collection */
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
        snprintf(bucket_dir, PATH_MAX, "%s/keyvals/%.8d", old_coll_path, i);
        
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
                snprintf(keyval_db, PATH_MAX, "%s/keyvals/%.8d/%s",
                    old_coll_path, i, tmp_ent->d_name);
                /* translate each keyval db to new format */
                ret = translate_keyval_db_0_0_1(
                    coll_id, keyval_db, tmp_handle,
                    coll_name, trove_context);
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

static int translate_keyval_key_0_0_1(TROVE_keyval_s * keyval, DBT * db_key)
{
    if(!strncmp(db_key->data, "root_handle", strlen("root_handle")))
    {
        keyval->buffer = ROOT_HANDLE_KEYSTR;
        keyval->buffer_sz = ROOT_HANDLE_KEYLEN;
    }
    else if(!strncmp(db_key->data, "dir_ent", strlen("dir_ent")))
    {
        keyval->buffer = DIRECTORY_ENTRY_KEYSTR;
        keyval->buffer_sz = DIRECTORY_ENTRY_KEYLEN;
    }
    else if(!strncmp(db_key->data, 
                     "datafile_handles", strlen("datafile_handles")))
    {
        keyval->buffer = DATAFILE_HANDLES_KEYSTR;
        keyval->buffer_sz = DATAFILE_HANDLES_KEYLEN;
    }
    else if(!strncmp(db_key->data, "metafile_dist", strlen("metafile_dist")))
    {
        keyval->buffer = METAFILE_DIST_KEYSTR;
        keyval->buffer_sz = METAFILE_DIST_KEYLEN;
    }
    else if(!strncmp(db_key->data, "symlink_target", strlen("symlink_target")))
    {
        keyval->buffer = SYMLINK_TARGET_KEYSTR;
        keyval->buffer_sz = SYMLINK_TARGET_KEYLEN;
    }
    else
    {
        return -1;
    }

    return 0;
}

/**
 * Migrates a single keyval db from a 0.0.1 DBPF collection
 * \return 0 on succes, -1 on failure
 */
static int translate_keyval_db_0_0_1(
    TROVE_coll_id coll_id,          /**< collection id */
    char* full_db_path,             /**< fully resolved path to db file */
    TROVE_handle handle,            /**< handle of the object */
    char* coll_name,                /**< name of collection */
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

    if(verbose) 
        printf("VERBOSE Migrating keyvals for handle: %llu\n", llu(handle));

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
            PVFS_ds_flags trove_flags = TROVE_SYNC;
            memset(&t_key, 0, sizeof(t_key));
            memset(&t_val, 0, sizeof(t_val));
            if(translate_keyval_key_0_0_1(&t_key, &key) < 0)
            {
                /* assume its a component name of a directory entry */
                t_key.buffer = key.data;
                t_key.buffer_sz = key.size;
		t_val.buffer = data.data;
		t_val.buffer_sz = data.size;
                trove_flags |= TROVE_KEYVAL_HANDLE_COUNT;
                trove_flags |= TROVE_NOOVERWRITE;
            }
            else if(!strncmp(t_key.buffer, "md", 2)) /* metafile_dist */
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
                coll_id, handle, &t_key, &t_val, trove_flags, 0, NULL,
                trove_context, &op_id, NULL);

            while (ret == 0)
            {   
                ret = trove_dspace_test(
                    coll_id, op_id, trove_context, &count, NULL, NULL,
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
    char* old_coll_path,            /**< path to old collection */
    TROVE_coll_id coll_id,          /**< collection id */
    char* new_name,                 /**< name of collection */
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
        snprintf(bucket_dir, PATH_MAX, "%s/bstreams/%.8d", 
                 old_coll_path, i);

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

                snprintf(bstream_file, PATH_MAX, "%s/bstreams/%.8d/%s",
                    old_coll_path, i, tmp_ent->d_name);
                snprintf(new_bstream_file, PATH_MAX, "%s/%08x/bstreams/%.8d/%s",
                    storage_space, coll_id, i, tmp_ent->d_name);
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

void fs_config_dummy_free(void * dummy) { }

int recursive_rmdir(char* dir)
{
    DIR * dirh;
    struct dirent * dire;
    struct stat statbuf;
    char fname[PATH_MAX];
    int ret;
    
    dirh = opendir(dir);
    if(!dirh)
    {
        fprintf(stderr, "Error: failed to open dir: %s\n", dir);
        return -1;
    }

    while((dire = readdir(dirh)) != NULL)
    {
        if(strcmp(dire->d_name, ".") == 0 ||
           strcmp(dire->d_name, "..") == 0)
        {
            continue;
        }

        sprintf(fname, "%s/%s", dir, dire->d_name);
        ret = stat(fname, &statbuf);
        if(ret == -1)
        {
            perror("stat");
            return -1;
        }

        if(S_ISDIR(statbuf.st_mode))
        {
            ret = recursive_rmdir(fname);
            if(ret < 0)
            {
                return ret;
            }
        }
        else if(S_ISREG(statbuf.st_mode))
        {
            unlink(fname);
        }
    }
    
    ret = closedir(dirh);
    if(ret < 0)
    {
        perror("closedir");
        return -1;
    }

    ret = rmdir(dir);
    if(ret < 0)
    {
        perror("rmdir");
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
