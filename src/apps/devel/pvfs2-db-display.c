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
    char dbpath[PATH_MAX];
    char hexdir[PATH_MAX];
    int verbose;
} options_t;

static options_t opts;

int open_db( DB **db_p, char *path, int type, int flags);
void close_db( DB *db_p );
void iterate_database(DB *db_p, void (*print)(DBT key, DBT val) );
void print_collection( DBT key, DBT val );
void print_storage( DBT key, DBT val );
void print_dspace( DBT key, DBT val );
void print_keyval( DBT key, DBT val );
void print_collection_attr( DBT key, DBT val );
void print_help(char *progname);
void print_ds_type( PVFS_ds_type type );
int process_args(int argc, char ** argv);

int main( int argc, char **argv )
{
    DB *db_p = NULL;
    DB_ENV *dbe_p = NULL;
    char *path = NULL;
    u_int32_t db_flags = DB_RDONLY|DB_THREAD, 
              env_flags = DB_CREATE | DB_INIT_MPOOL,
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
        return -1;
    }

    /* Open the environment. */
    ret = dbe_p->open(dbe_p,
                      opts.dbpath,
                      env_flags,
                      0);
    if (ret != 0) 
    {
        printf("Environment open failed: %s", db_strerror(ret));
        return -1;
    } 

    /* open and print each database */

    /* collection database */
    sprintf(path, "%s/%s", opts.dbpath, COLLECTION_FILE );
    ret = open_db( &db_p, path, type, db_flags);
    if (ret == 0) 
    {
        printf("Collection Database\n");
        iterate_database(db_p, &print_collection );
        close_db(db_p);
    }
    
    /* storage database */
    memset(path, path_len, sizeof(char));
    sprintf(path, "%s/%s", opts.dbpath, STORAGE_FILE );
    ret = open_db( &db_p, path, type, db_flags);
    if (ret == 0) 
    {
        printf("Storage Database\n");
        iterate_database(db_p, &print_storage );
        close_db(db_p);
    }

    /* dspace database */
    memset(path, path_len, sizeof(char));
    sprintf(path, "%s/%s/%s", opts.dbpath, opts.hexdir, DATASPACE_FILE );
    ret = open_db( &db_p, path, type, db_flags);
    if (ret == 0) 
    {
        printf("Dataspace Database\n");
        iterate_database(db_p, &print_dspace );
        close_db(db_p);
    }

    /* keyval database */
    memset(path, path_len, sizeof(char));
    sprintf(path, "%s/%s/%s", opts.dbpath, opts.hexdir, KEYVAL_FILE );
    ret = open_db( &db_p, path, type, db_flags);
    if (ret == 0) 
    {
        printf("Keyval Database\n");
        iterate_database(db_p, &print_keyval );
        close_db(db_p);
    }

    /* collection attribute database */
    memset(path, path_len, sizeof(char));
    sprintf(path, "%s/%s/%s", opts.dbpath, opts.hexdir, COLLECTION_ATTR_FILE );
    ret = open_db( &db_p, path, type, db_flags);
    if (ret == 0) 
    {
        printf("Collection Attributes Database\n");
        iterate_database(db_p, &print_collection_attr );
        close_db(db_p);
    }

    dbe_p->close(dbe_p, 0);

    free(path);
    return 0;
}

int open_db( DB **db_p, char *path, int type, int flags)
{
    int ret = 0;

    ret = db_create(db_p, NULL, 0);
    if (ret != 0) 
    {
        close_db( *db_p );
        printf("Couldn't create db_p for %s: %s\n", path, db_strerror(ret));
        return ret;
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

void iterate_database(DB *db_p, void (*print)(DBT key, DBT val) )
{
    int ret = 0;
    DBC *dbc_p = NULL;
    DBT key, val;

    ret = db_p->cursor(db_p, NULL, &dbc_p, 0);
    if( ret != 0 )
    {
        printf("Unable to open cursor to print db: %s\n", 
               db_strerror(ret));
        return;
    }
 
    memset(&key, 0, sizeof(key));
    memset(&val, 0, sizeof(val));
 
    printf("-------- Start database --------\n");
    while ((ret = dbc_p->c_get(dbc_p, &key, &val, DB_NEXT)) == 0)
    {
        print( key, val );
        memset(key.data, 0, key.size);
        memset(val.data, 0, val.size);
    }

    if( ret != DB_NOTFOUND )
    {
        printf("**** an error occurred (%s) ****\n", db_strerror(ret));
    }
    printf("-------- End database --------\n\n");

    dbc_p->c_close( dbc_p );
    return;
}

void print_collection( DBT key, DBT val )
{
    char *k;
    int32_t v;
    k = key.data;
    v = *(int32_t *)val.data;
    printf("(%s)(%d) -> (%d)(%d)\n", k, key.size, v, val.size);
    return;
}

void print_storage( DBT key, DBT val )
{
    char *k;
    int32_t v;
    k = key.data;
    v = *(int32_t *)val.data;
    printf("(%s)(%d) -> (%d)(%d)\n", k, key.size, v, val.size);
    return;
}

void print_dspace( DBT key, DBT val )
{
    uint64_t k;
    struct PVFS_ds_attributes_s *v;

    k = *(uint64_t *)key.data;
    v = val.data;

    printf("(%llu)(%d) -> ", llu(k), key.size);
 
    print_ds_type( v->type );

    printf("(fsid: %d)(handle: %llu)(uid: %u)(gid: %u)"
           "(perm: %u)(ctime: %llu)(mtime: %llu)(atime: %llu)(%d)\n", 
           v->fs_id, llu(v->handle), v->uid, v->gid, v->mode,
           llu(v->ctime), llu(v->mtime), llu(v->atime), val.size);

    /* union elements are not printed */
    return;
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
        kh = *(uint64_t *)k->key;
        printf("(%llu)(%d) -> ", llu(kh), key.size);
    }
    else
    {
        printf("(%s)(%d) -> ", k->key,  key.size);
    }

    if( strncmp(k->key, "dh", 3) == 0 || strncmp(k->key, "de", 3) == 0 )
    {
        vh = *(uint64_t *)val.data;
        printf("(%llu)(%d)\n", llu(vh), val.size);
    }

    else if( strncmp(k->key, "md", 3) == 0 )
    {
       /* just print the name of the distribution, the rest is extra.
        * the PINT_dist struct is packed/encoded before writing to db. that
        * means the first uint32_t bytes are the length of the string, skip
        * it. */
       char *dname = val.data + sizeof(uint32_t);
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
/*
 * not implemented
    elseif( strncmp(k->key. "st", 3) == 0 )
    {

    }

    elseif( strncmp(k->key. "de", 3) == 0 )
    {

    }

    elseif( strncmp(k->key. "mh", 3) == 0 )
    {

    }
 * not implemented
*/
    else
    {
        /* just print out the size of the data, try not to segfault */
        printf("(%d)\n",  val.size);
    }

    return;
}

void print_collection_attr( DBT key, DBT val )
{
    char *k, *vs;
    uint64_t vu;
    k = key.data;
    printf("(%s)(%d) -> ", k, key.size);
    if( val.size == 8 )
    {
        vu = *(uint64_t *)val.data;
        printf("(%llu)(%d)\n", llu(vu), val.size);
    }
    else
    {
        vs = val.data;
        printf("(%s)(%d)\n", vs, val.size);
    }
    return;
}

int process_args(int argc, char ** argv)
{
    int ret = 0, option_index = 0;
    static struct option long_opts[] =
    {
        {"help",0,0,0},
        {"verbose",0,0,0},
        {"dbpath",1,0,0},
        {"hexdir",1,0,0},
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
                                                                                            case 1: /* verbose */
                opts.verbose = 1; 
                break;

            case 2: /*dbpath */ 
                strncpy(opts.dbpath, optarg, PATH_MAX);
                break;
            case 3: /* hexdir */
                strncpy(opts.hexdir, optarg, PATH_MAX);
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

    return 0;
}

void print_help(char *progname)
{
    fprintf(stderr, 
            "\nThis utility is used to display the contents of a single\n"
            "server's Berkeley DB databases in a manner useful for PVFS \n"
            "development.\n");
    fprintf(stderr, "\nUsage:\t\t%s --dbpath <path> --hexdir <hexdir>", 
            progname);
    fprintf(stderr, "\nExample:\t%s --dbpath /tmp/pvfs2-space --hexdir "
                    "4e3f77a5\n", progname);
    fprintf(stderr, "\nOptions:\n"
                    "\t--verbose\t\tEnable verbose output\n"
                    "\t--help\t\t\tThis message.\n"
                    "\t--dbpath <path>\t\tThe path of the server's "
                    "StorageSpace. The path\n\t\t\t\tshould contain "
                    "collections.db and \n\t\t\t\tstorage_attributes.db\n"
                    "\t--hexdir <dir>\t\tThe directory in dbpath that "
                    "contains\n\t\t\t\tcollection_attributes.db, "
                    "dataspace_attrbutes.db\n\t\t\t\tand keyval.db\n\n");
    return;
}

void print_ds_type( PVFS_ds_type type )
{
    switch( type )
    {
        case PVFS_TYPE_NONE: 
            printf("(type: none)");
            break;
        case PVFS_TYPE_METAFILE: 
            printf("(type: metafile)");
            break;
        case PVFS_TYPE_DATAFILE: 
            printf("(type: datafile)");
            break;
        case PVFS_TYPE_DIRECTORY: 
            printf("(type: directory)");
            break;
        case PVFS_TYPE_SYMLINK:
             printf("(type: symlink)");
            break;
        case PVFS_TYPE_DIRDATA: 
            printf("(type: dirdata)");
            break;
        case PVFS_TYPE_INTERNAL: 
            printf("(type: internal)");
            break;
        default: 
            printf("type: unknown");
            break;
    }
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

