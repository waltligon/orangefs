/*
 * (C) 2010 Clemson University
 *
 * See COPYING in top-level directory.
 */

/** \file
 * Utility for displaying PVFS Berkeley DBs in textual form for debugging
 *   */

#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <db.h>

#include "pvfs2-types.h"
#include "trove-types.h"
#include "pvfs2-storage.h"
#include "pvfs2-internal.h"
#include "trove-dbpf/dbpf.h"

#define COLLECTION_FILE         "collections.db"
#define STORAGE_FILE            "storage_attributes.db"
#define DATASPACE_FILE          "dataspace_attributes.db"
#define KEYVAL_FILE             "keyval.db"
#define COLLECTION_ATTR_FILE    "collection_attributes.db"

typedef struct
{
    int remove;
    char dbpath[PATH_MAX];
    char hexdir[PATH_MAX];
    char host[PATH_MAX];
} options_t;

/* globals */
static options_t opts;

int find_pool_keys(dbpf_db *db_p_coll_attr, dbpf_db *db_p_keyval);
void remove_preallocated_handles(dbpf_db *db_p_keyval, PVFS_handle pool_handle);
void print_help(char *progname);
int process_args(int argc, char ** argv);
int PINT_trove_dbpf_keyval_compare(dbpf_db * dbp, const struct dbpf_data * a, const struct dbpf_data * b);
void print_keyval( struct dbpf_data key, struct dbpf_data val );

#define TROVE_db_cache_size_bytes 1610612736

int main( int argc, char **argv )
{
    dbpf_db *db_p_coll_attr = NULL, *db_p_keyval = NULL;
    char *path = NULL;
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

    /* Open collection attribute database */
    memset(path, path_len, sizeof(char));
    sprintf(path, "%s/%s/%s", opts.dbpath, opts.hexdir, COLLECTION_ATTR_FILE );
    ret = dbpf_db_open(path, 0, &db_p_coll_attr, 0, NULL);
    if (ret != 0)
    {
        printf("Unable to open collection attributes database: %s\n",
                strerror(ret));
        return ret;
    }

    /* Open keyval database */
    memset(path, path_len, sizeof(char));
    sprintf(path, "%s/%s/%s", opts.dbpath, opts.hexdir, KEYVAL_FILE );
    ret = dbpf_db_open(path, DBPF_DB_COMPARE_KEYVAL, &db_p_keyval, 0, NULL);
    if (ret != 0)
    {
        printf("Unable to open keyval database: %s\n", strerror(ret));
        return ret;
    }

    find_pool_keys(db_p_coll_attr, db_p_keyval);

    /* Close the databases. */
    dbpf_db_close(db_p_keyval);
    dbpf_db_close(db_p_coll_attr);

    free(path);
    return 0;
}

int find_pool_keys(dbpf_db *db_p_coll_attr, dbpf_db *db_p_keyval)
{
    int ret = 0;
    dbpf_cursor *dbc_p = NULL;
    int i;
    char type_string[11] = { 0 }; /* 32 bit type only needs 10 digits */
    PVFS_ds_type type;
    struct dbpf_data key, val;
    int key_size;
    char *key_string = NULL;
    PVFS_handle pool_handle = PVFS_HANDLE_NULL;

    ret = dbpf_db_cursor(db_p_coll_attr, &dbc_p, 0);
    if( ret != 0 )
    {
        printf("Unable to open cursor: %s\n", strerror(ret));
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

        key.data = key_string;
        key.len = key_size;
        val.data = &pool_handle;
        val.len = sizeof(pool_handle);

        ret = dbpf_db_cursor_get(dbc_p, &key, &val, DBPF_DB_CURSOR_SET, key_size);
        if (ret != 0)
        {
            printf("Unable to retrieve pool handle: %s\n", strerror(ret));
            return ret;
        }

        /* Delete the handles in the range. */
        remove_preallocated_handles(db_p_keyval, pool_handle);
    }
    return(0);
}

void print_keyval( struct dbpf_data key, struct dbpf_data val )
{
    struct dbpf_keyval_db_entry *k;
    uint64_t vh, kh;
    uint32_t vi;


    k = key.data;
    printf("(%llu)", llu(k->handle));
    if( key.len == 8 )
    {
        printf("()(%zu) -> ", key.len);
    }
    else if( key.len == 16 )
    {
        char *tmp;
        tmp = k->key;
        kh = *(uint64_t *)tmp;
        printf("(%llu)(%zu) -> ", llu(kh), key.len);
    }
    else
    {
        printf("(%s)(%zu) -> ", k->key,  key.len);
    }

    if( strncmp(k->key, "dh", 3) == 0 || strncmp(k->key, "de", 3) == 0 )
    {
        int s = 0;
        while(s < val.len )
        {
            vh = *(uint64_t *)((unsigned char *)val.data + s);
            printf("(%llu)", llu(vh));
            s += sizeof(TROVE_handle);
        }
        printf("(%zu)\n", val.len);

    }

    else if( strncmp(k->key, "md", 3) == 0 )
    {
       /* just print the name of the distribution, the rest is extra.
        * the PINT_dist struct is packed/encoded before writing to db. that
        * means the first uint32_t bytes are the length of the string, skip
        * it. */
       char *dname = (char *)val.data + sizeof(uint32_t);
       printf("(%s)(%zu)\n", dname, val.len );
    }

    else if( strlen(k->key) > 2 && val.len == 8 )
    {
        /* should be cases of filename to handle */
        vh = *(uint64_t *)val.data;
        printf("(%llu)(%zu)\n", llu(vh), val.len );
    }

    else if( (key.len == 8 || key.len == 16 ) && val.len == 4 )
    {
        vi = *(uint32_t *)val.data;
        printf("(%u)(%zu)\n", vi, val.len );
    }
    else
    {
        /* just print out the size of the data, try not to segfault */
        printf("(%zu)\n",  val.len);
    }

    return;
}

void remove_preallocated_handles(dbpf_db *db_p_keyval, PVFS_handle pool_handle)
{
    int ret = 0;
    dbpf_cursor *dbc_p = NULL;
    struct dbpf_data key, val;
    struct dbpf_keyval_db_entry key_entry;
    struct dbpf_keyval_db_entry *key_ptr;
    unsigned char data_buf[1024];

    ret = dbpf_db_cursor(db_p_keyval, &dbc_p, 1);
    if( ret != 0 )
    {
        printf("Unable to open cursor: %s\n", strerror(ret));
        return;
    }
 
    memset(&key_entry, 0, sizeof(key_entry));
    key_entry.handle = pool_handle;
    key.data = &key_entry;
    key.len = sizeof(TROVE_handle);
    val.data = data_buf;
    val.len = sizeof data_buf;
 
    ret = dbpf_db_cursor_get(dbc_p, &key, &val, DBPF_DB_CURSOR_SET, sizeof(TROVE_handle));
    if (ret != 0)
    {
        printf("Unable to remove count of handles in pool %llu: %s\n",
               llu(pool_handle), strerror(ret));
        return;
    }   
    if (key.len != 8 || val.len != 4 || key_entry.handle != pool_handle)
    {
        printf("Problem removing count of handles in pool %llu\n",
                llu(pool_handle)); 
        return;
    }
    printf("Removing: ");
    print_keyval( key, val );
    if (opts.remove)
    {
        ret = dbpf_db_cursor_del(dbc_p);
        if (ret != 0)
        {
            key_ptr = (struct dbpf_keyval_db_entry *) key.data;

            printf("Unable to remove count record for pool %llu: %s\n",
                    llu(key_ptr->handle), strerror(ret));
            return;
        }
    }

    key.len = sizeof(TROVE_handle);
    val.len = sizeof data_buf;
    ret = dbpf_db_cursor_get(dbc_p, &key, &val, DBPF_DB_CURSOR_NEXT, sizeof(TROVE_handle));
    key_ptr = (struct dbpf_keyval_db_entry *) key.data;
    while (ret == 0 && key.len == 16 && val.len == 0 &&
           key_ptr->handle == pool_handle)
    {
        printf("Removing: ");
        print_keyval( key, val );
        if (opts.remove)
        {
            ret = dbpf_db_cursor_del(dbc_p);
            if (ret != 0)
            {
                printf("*** REMOVE FAILED ***\n");
            }
        }
        key.len = sizeof(TROVE_handle);
        val.len = sizeof data_buf;
        ret = dbpf_db_cursor_get(dbc_p, &key, &val, DBPF_DB_CURSOR_NEXT, sizeof(TROVE_handle));
        key_ptr = (struct dbpf_keyval_db_entry *) key.data;
    }
/*
printf("ret=%d, key.size=%u, val.size=%u, key_ptr->handle=%llu, handle=%llu\n",
ret, key.size, val.size, llu(key_ptr->handle), llu(*((PVFS_handle *) key_ptr->key)));
*/

    dbpf_db_cursor_close(dbc_p);
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

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

