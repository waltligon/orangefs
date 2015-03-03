/*
 * (C) 2010 Clemson University
 *
 * See COPYING in top-level directory.
 */

/** \file
 * Utility for displaying PVFS Berkeley DBs in textual form for debugging
 *   */

/* This is a kludge to keep calloc and free from being redefined
   to our PINT_ versions. That way we can keep from having a depency
   on the pvfs library. */
#define PINT_MALLOC_H

#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <db.h>

#include "pvfs2-types.h"
#include "trove-types.h"
#include "pvfs2-storage.h"
#include "pvfs2-internal.h"

#define COLLECTION_FILE         "collections.db"
#define STORAGE_FILE            "storage_attributes.db"
#define DATASPACE_FILE          "dataspace_attributes.db"
#define KEYVAL_FILE             "keyval.db"
#define COLLECTION_ATTR_FILE    "collection_attributes.db"

/* from src/io/trove/trove-dbpf/dbpf-keyval.c and include/pvfs2-types.h */
#define DBPF_MAX_KEY_LENGTH 256

/* from src/io/trove/trove-dbpf/dbpf-keyval.c */
struct dbpf_keyval_db_entry
{
    TROVE_handle handle;
    char key[DBPF_MAX_KEY_LENGTH];
};

typedef struct
{
    int remove;
    char dbpath[PATH_MAX];
    char hexdir[PATH_MAX];
    char host[PATH_MAX];
} options_t;

/* globals */
static options_t opts;

int open_db( DB **db_p, char *path, int type, int set_keyval_compare, int flags);
void close_db( DB *db_p );
int find_pool_keys(DB *db_p_coll_attr, DB *db_p_keyval);
void remove_preallocated_handles(DB *db_p_keyval, PVFS_handle pool_handle);
void print_help(char *progname);
int process_args(int argc, char ** argv);
int PINT_trove_dbpf_keyval_compare(DB * dbp, const DBT * a, const DBT * b);
void print_keyval( DBT key, DBT val );

#define TROVE_db_cache_size_bytes 1610612736

int main( int argc, char **argv )
{
    DB *db_p_coll_attr = NULL, *db_p_keyval = NULL;
    DB_ENV dbe;
    DB_ENV *dbe_p = &dbe;
    char *path = NULL;
    u_int32_t db_flags = DB_THREAD,
              env_flags = DB_INIT_MPOOL,
              type = DB_UNKNOWN;
    int ret, path_len; 

    if( (ret = process_args( argc, argv)) != 0 )
    {
          return ret;
    }

    /* allocate space for the longest path */
    path_len = strlen(opts.dbpath) + strlen(opts.hexdir) + 
               strlen(COLLECTION_ATTR_FILE) + 3; /* padding for / and \0 */
    path = calloc( path_len, sizeof(char) );
    if( path == NULL )
    {
        printf("Error allocating path, exiting\n");
        return ENOMEM;
    }

    ret = db_env_create(&dbe_p, 0);
    if (ret != 0) 
    {
        printf("Error creating env handle: %s\n", db_strerror(ret));
        return ret;
    }

    /* Open the environment. */
    ret = dbe_p->open(dbe_p,
                      opts.dbpath,
                      env_flags,
                      0);
    if (ret != 0) 
    {
        printf("Environment open failed: %s", db_strerror(ret));
        return ret;
    } 

    /* Open collection attribute database */
    memset(path, path_len, sizeof(char));
    sprintf(path, "%s/%s/%s", opts.dbpath, opts.hexdir, COLLECTION_ATTR_FILE );
    ret = open_db( &db_p_coll_attr, path, type, 0, db_flags);
    if (ret != 0)
    {
        printf("Unable to open collection attributes database: %s\n",
                db_strerror(ret));
        return ret;
    }

    /* Open keyval database */
    memset(path, path_len, sizeof(char));
    sprintf(path, "%s/%s/%s", opts.dbpath, opts.hexdir, KEYVAL_FILE );
    ret = open_db( &db_p_keyval, path, type, 1, db_flags);
    if (ret != 0)
    {
        printf("Unable to open keyval database: %s\n", db_strerror(ret));
        return ret;
    }

    find_pool_keys(db_p_coll_attr, db_p_keyval);

    /* Close the databases. */
    close_db(db_p_keyval);
    close_db(db_p_coll_attr);

    dbe_p->close(dbe_p, 0);

    free(path);
    return 0;
}

int open_db( DB **db_p, char *path, int type, int set_keyval_compare, int flags)
{
    int ret = 0;

    ret = db_create(db_p, NULL, 0);
    if (ret != 0) 
    {
        close_db( *db_p );
        printf("Couldn't create db_p for %s: %s\n", path, db_strerror(ret));
        return ret;
    }

    if (set_keyval_compare)
    {
        (*db_p)->set_bt_compare((*db_p), PINT_trove_dbpf_keyval_compare);
    }

    ret = (*db_p)->open(*db_p, NULL, path, NULL, type, flags, 0 );
    if (ret != 0) 
    {
        close_db( *db_p );
        printf("Couldn't open %s: %s\n", path, db_strerror(ret));
        return ret;
    }
    return ret;
}

void close_db( DB *db_p )
{
    int ret = 0;
    if( db_p )
    {
        ret = db_p->close(db_p, 0);
    }

    if (ret != 0) 
    {
        printf("Couldn't close db_p: %s\n", db_strerror(ret));
    }
    return;
}

int find_pool_keys(DB *db_p_coll_attr, DB *db_p_keyval)
{
    int ret = 0;
    DBC *dbc_p = NULL;
    int i;
    char type_string[11] = { 0 }; /* 32 bit type only needs 10 digits */
    PVFS_ds_type type;
    DBT key, val;
    int key_size;
    char *key_string = NULL;
    PVFS_handle pool_handle = PVFS_HANDLE_NULL;

    ret = db_p_coll_attr->cursor(db_p_coll_attr, NULL, &dbc_p, 0);
    if( ret != 0 )
    {
        printf("Unable to open cursor: %s\n", db_strerror(ret));
        return ret;
    }

    for (i = 0, type = 1 << i; type < PVFS_TYPE_INTERNAL; i++, type = 1 << i)
    {
        snprintf(type_string, 11, "%u", type);
        key_size = strlen(opts.host) + strlen(type_string) +
                        strlen("precreate-pool-") + 2;
        key_string = malloc(key_size);
        if(! key_string)
        {
            printf("Unable to allocate memory for pool name.\n");
            return ret;
        }
        snprintf(key_string, key_size, "precreate-pool-%s-%s",
                 opts.host, type_string);

        memset(&key, 0, sizeof(key));
        key.data = key_string;
        key.size = key.ulen = key_size;
        key.flags = DB_DBT_USERMEM;

        memset(&val, 0, sizeof(val));
        val.data = &pool_handle;
        val.ulen = sizeof(pool_handle);
        val.flags = DB_DBT_USERMEM;

        ret = dbc_p->c_get(dbc_p, &key, &val, DB_SET);
        if (ret != 0)
        {
            printf("Unable to retrieve pool handle: %s\n", db_strerror(ret));
            return ret;
        }

        /* Delete the handles in the range. */
        remove_preallocated_handles(db_p_keyval, pool_handle);
    }
    return(0);
}

void print_keyval( DBT key, DBT val )
{
    struct dbpf_keyval_db_entry *k;
    uint64_t vh, kh;
    uint32_t vi;


    k = key.data;
    printf("(%llu)", llu(k->handle));
    if( key.size == 8 )
    {
        printf("()(%d) -> ", key.size);
    }
    else if( key.size == 16 )
    {
        char *tmp;
        tmp = k->key;
        kh = *(uint64_t *)tmp;
        printf("(%llu)(%d) -> ", llu(kh), key.size);
    }
    else
    {
        printf("(%s)(%d) -> ", k->key,  key.size);
    }

    if( strncmp(k->key, "dh", 3) == 0 || strncmp(k->key, "de", 3) == 0 )
    {
        int s = 0;
        while(s < val.size )
        {
            vh = *(uint64_t *)((unsigned char *)val.data + s);
            printf("(%llu)", llu(vh));
            s += sizeof(TROVE_handle);
        }
        printf("(%d)\n", val.size);

    }

    else if( strncmp(k->key, "md", 3) == 0 )
    {
       /* just print the name of the distribution, the rest is extra.
        * the PINT_dist struct is packed/encoded before writing to db. that
        * means the first uint32_t bytes are the length of the string, skip
        * it. */
       char *dname = (char *)val.data + sizeof(uint32_t);
       printf("(%s)(%d)\n", dname, val.size );
    }

    else if( strlen(k->key) > 2 && val.size == 8 )
    {
        /* should be cases of filename to handle */
        vh = *(uint64_t *)val.data;
        printf("(%llu)(%d)\n", llu(vh), val.size );
    }

    else if( (key.size == 8 || key.size == 16 ) && val.size == 4 )
    {
        vi = *(uint32_t *)val.data;
        printf("(%u)(%d)\n", vi, val.size );
    }
    else
    {
        /* just print out the size of the data, try not to segfault */
        printf("(%d)\n",  val.size);
    }

    return;
}

void remove_preallocated_handles(DB *db_p_keyval, PVFS_handle pool_handle)
{
    int ret = 0;
    DBC *dbc_p = NULL;
    DBT key, val;
    struct dbpf_keyval_db_entry key_entry;
    struct dbpf_keyval_db_entry *key_ptr;

    ret = db_p_keyval->cursor(db_p_keyval, NULL, &dbc_p, 0);
    if( ret != 0 )
    {
        printf("Unable to open cursor: %s\n", db_strerror(ret));
        return;
    }
 
    memset(&key, 0, sizeof(key));
    memset(&key_entry, 0, sizeof(key_entry));
    key_entry.handle = pool_handle;
    key.data = &key_entry;
    key.size = key.ulen = sizeof(TROVE_handle);
    key.flags = DB_DBT_USERMEM;
 
    memset(&val, 0, sizeof(val));

    ret = dbc_p->c_get(dbc_p, &key, &val, DB_SET);
    if (ret != 0)
    {
        printf("Unable to remove count of handles in pool %llu: %s\n",
               llu(pool_handle), db_strerror(ret));
        return;
    }   
    if (key.size != 8 || val.size != 4 || key_entry.handle != pool_handle)
    {
        printf("Problem removing count of handles in pool %llu\n",
                llu(pool_handle)); 
        return;
    }
    printf("Removing: ");
    print_keyval( key, val );
    if (opts.remove)
    {
        ret = dbc_p->c_del(dbc_p, 0);
        if (ret != 0)
        {
            key_ptr = (struct dbpf_keyval_db_entry *) key.data;

            printf("Unable to remove count record for pool %llu: %s\n",
                    llu(key_ptr->handle), db_strerror(ret));
            return;
        }
    }

    memset(&key, 0, sizeof(key));
    memset(&val, 0, sizeof(val));
    ret = dbc_p->c_get(dbc_p, &key, &val, DB_NEXT);
    key_ptr = (struct dbpf_keyval_db_entry *) key.data;
    while (ret == 0 && key.size == 16 && val.size == 0 &&
           key_ptr->handle == pool_handle)
    {
        printf("Removing: ");
        print_keyval( key, val );
        if (opts.remove)
        {
            ret = dbc_p->c_del(dbc_p, 0);
            if (ret != 0)
            {
                printf("*** REMOVE FAILED ***\n");
            }
        }
        memset(&key, 0, sizeof(key));
        memset(&val, 0, sizeof(val));
        ret = dbc_p->c_get(dbc_p, &key, &val, DB_NEXT);
        key_ptr = (struct dbpf_keyval_db_entry *) key.data;
    }
/*
printf("ret=%d, key.size=%u, val.size=%u, key_ptr->handle=%llu, handle=%llu\n",
ret, key.size, val.size, llu(key_ptr->handle), llu(*((PVFS_handle *) key_ptr->key)));
*/

    dbc_p->c_close( dbc_p );
    return;
}

int process_args(int argc, char ** argv)
{
    int ret = 0, option_index = 0;
    static struct option long_opts[] =
    {
        {"help",0,0,0},
        {"remove",0,0,0},
        {"dbpath",1,0,0},
        {"hexdir",1,0,0},
        {"host",1,0,0},
        {0,0,0,0}
    };

    memset(&opts, 0, sizeof(options_t));

    while ((ret = getopt_long(argc, argv, "", long_opts, &option_index)) != -1)
    {
        switch (option_index)
        {
            case 0: /* help */ 
                print_help(argv[0]); 
                exit(0);

            case 1: /* remove */
                opts.remove = 1; 
                break;

            case 2: /*dbpath */ 
                strncpy(opts.dbpath, optarg, PATH_MAX);
                break;

            case 3: /* hexdir */
                strncpy(opts.hexdir, optarg, PATH_MAX);
                break;

            case 4: /* host */
                strncpy(opts.host, optarg, PATH_MAX);
                break;

            default:
                print_help(argv[0]);
                return -1;
        }
        option_index = 0;
    }

    if( strncmp( opts.dbpath,"",PATH_MAX ) == 0 )
    {
        fprintf(stderr, "\nError: --dbpath option must be given.\n");
        print_help(argv[0]);
        return -1;
    }

    if( strncmp( opts.hexdir,"",PATH_MAX ) == 0 )
    {
        fprintf(stderr, "\nError: --hexdir option must be given.\n");
        print_help(argv[0]);
        return -1;
    }

    if (! opts.host)
    {
        fprintf(stderr, "\nError: --host option must be given.\n");
        print_help(argv[0]);
        return -1;
    }

    return 0;
}

void print_help(char *progname)
{
    fprintf(stderr, 
            "\nThis utility is used to remove handles from a preallocation pool.\n");
    fprintf(stderr, "\nUsage:\t\t%s --dbpath <path> --hexdir <hexdir> \\\n"
                    "\t\t\t--host <name>\n",
            progname);
    fprintf(stderr, "\nExample:\t%s --dbpath /tmp/pvfs2-space --hexdir 4e3f77a5 \\\n"
                    "\t\t\t--host tcp://devorange6:3397\n",
            progname);
    fprintf(stderr, "\nOptions:\n"
                    "\t--remove\t\tRemove preallocated handles\n"
                    "\t--help\t\t\tThis message.\n"
                    "\t--dbpath <path>\t\tThe path of the server's "
                    "StorageSpace. The path\n\t\t\t\tshould contain "
                    "collections.db and \n\t\t\t\tstorage_attributes.db\n"
                    "\t--hexdir <dir>\t\tThe directory in dbpath that "
                    "contains\n\t\t\t\tcollection_attributes.db, "
                    "dataspace_attrbutes.db\n\t\t\t\tand keyval.db\n"
                    "\t--host<name>\t\thost whose preallocated "
                    "handles should be removed\n");
    return;
}

#define DBPF_KEYVAL_DB_ENTRY_KEY_SIZE(_size) \
    (_size - sizeof(TROVE_handle))

int PINT_trove_dbpf_keyval_compare(
    DB * dbp, const DBT * a, const DBT * b)
{
    struct dbpf_keyval_db_entry db_entry_a;
    struct dbpf_keyval_db_entry db_entry_b;

    memcpy(&db_entry_a, a->data, sizeof(struct dbpf_keyval_db_entry));
    memcpy(&db_entry_b, b->data, sizeof(struct dbpf_keyval_db_entry));

    if(db_entry_a.handle != db_entry_b.handle)
    {
        return (db_entry_a.handle < db_entry_b.handle) ? -1 : 1;
    }

    if(a->size > b->size)
    {
        return 1;
    }

    if(a->size < b->size)
    {
        return -1;
    }

    /* must be equal */
    return (memcmp(db_entry_a.key, db_entry_b.key,
                    DBPF_KEYVAL_DB_ENTRY_KEY_SIZE(a->size)));
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

