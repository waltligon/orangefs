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

/* from src/io/trove/trove-dbpf/dbpf.h */
enum dbpf_key_type
{
    DBPF_DIRECTORY_ENTRY_TYPE = 'd',
    DBPF_ATTRIBUTE_TYPE = 'a',
    DBPF_COUNT_TYPE = 'c'
};

/* from src/io/trove/trove-dbpf/dbpf-keyval.c */
struct dbpf_keyval_db_entry
{
    TROVE_handle handle;
    char type;
    char key[DBPF_MAX_KEY_LENGTH];
};

typedef struct
{
    char dbpath[PATH_MAX];
    char hexdir[PATH_MAX];
    int verbose;
} options_t;

/* globals */
static options_t opts;
int hex = 0;

int open_db( DB **db_p, char *path, int type, int set_keyval_compare, int flags);
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
int PINT_trove_dbpf_keyval_compare(DB * dbp, const DBT * a, const DBT * b);

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
    ret = open_db( &db_p, path, type, 0, db_flags);
    if (ret == 0) 
    {
        printf("Collection Database\n");
        iterate_database(db_p, &print_collection );
        close_db(db_p);
    }
    
    /* storage database */
    memset(path, path_len, sizeof(char));
    sprintf(path, "%s/%s", opts.dbpath, STORAGE_FILE );
    ret = open_db( &db_p, path, type, 0, db_flags);
    if (ret == 0) 
    {
        printf("Storage Database\n");
        iterate_database(db_p, &print_storage );
        close_db(db_p);
    }

    /* dspace database */
    memset(path, path_len, sizeof(char));
    sprintf(path, "%s/%s/%s", opts.dbpath, opts.hexdir, DATASPACE_FILE );
    ret = open_db( &db_p, path, type, 0, db_flags);
    if (ret == 0) 
    {
        printf("Dataspace Database\n");
        iterate_database(db_p, &print_dspace );
        close_db(db_p);
    }

    /* keyval database */
    memset(path, path_len, sizeof(char));
    sprintf(path, "%s/%s/%s", opts.dbpath, opts.hexdir, KEYVAL_FILE );
    ret = open_db( &db_p, path, type, 1, db_flags);
    if (ret == 0) 
    {
        printf("Keyval Database\n");
        iterate_database(db_p, &print_keyval );
        close_db(db_p);
    }

    /* collection attribute database */
    memset(path, path_len, sizeof(char));
    sprintf(path, "%s/%s/%s", opts.dbpath, opts.hexdir, COLLECTION_ATTR_FILE );
    ret = open_db( &db_p, path, type, 0, db_flags);
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
    if (hex) 
        printf("(%s)(%d) -> (%x) (%d)\n", k, key.size, v, val.size);
    else
        printf("(%s)(%d) -> (%d) (%d)\n", k, key.size, v, val.size);
    return;
}

void print_storage( DBT key, DBT val )
{
    char *k;
    int32_t v;
    k = key.data;
    v = *(int32_t *)val.data;
    if (hex) 
        printf("(%s)(%d) -> (%x) (%d)\n", k, key.size, v, val.size);
    else
        printf("(%s)(%d) -> (%d) (%d)\n", k, key.size, v, val.size);
    return;
}

void print_dspace( DBT key, DBT val )
{
    PVFS_handle *k;
    struct PVFS_ds_attributes_s *v;

    time_t r_ctime, r_mtime, r_atime, r_ntime;
    char ctimeStr[1024], mtimeStr[1024], atimeStr[1024], ntimeStr[1024];

    k = (PVFS_handle *)key.data;
    v = val.data;

    if (v->ctime != 0)
    {
       r_ctime = (time_t) v->ctime;
       ctime_r(&r_ctime, ctimeStr);
       ctimeStr[strlen(ctimeStr)-1] = '\0';
    }
    else
    {
        strcpy(ctimeStr, "");
    }

    if (v->mtime != 0)
    {
        r_mtime = (time_t) v->mtime;
        ctime_r(&r_mtime, mtimeStr);
        mtimeStr[strlen(mtimeStr)-1] = '\0';
    }
    else
    {
        strcpy(mtimeStr, "");
    }

    if (v->atime != 0)
    {
        r_atime = (time_t) v->atime;
        ctime_r(&r_atime, atimeStr);
        atimeStr[strlen(atimeStr)-1] = '\0';
    }
    else
    {
        strcpy(atimeStr, "");
    }

    if (v->ntime != 0)
    {
        r_ntime = (time_t) v->ntime;
        ctime_r(&r_ntime, ntimeStr);
        ntimeStr[strlen(ntimeStr)-1] = '\0';
    }
    else
    {
        strcpy(ntimeStr, "");
    }

    printf("(%s)(%d) -> ", PVFS_OID_str(k), key.size);

    print_ds_type( v->type );

    if (hex)
    {
        printf("(fsid: %d)(handle: %s)(uid: %u)(gid: %u)"
           "(perm: %u)(ctime: %s)(mtime: %s)(atime: %s)(ntime: %s)\n",
           v->fs_id, PVFS_OID_str(&v->handle), v->uid, v->gid, v->mode,
           ctimeStr, mtimeStr, atimeStr, ntimeStr);
    }
    else
    {
         printf("(fsid: %d)(handle: %s)(uid: %u)(gid: %u)"
           "(perm: %u)(ctime: %s)(mtime: %s)(atime: %s)(ntime: %s)",
           v->fs_id, PVFS_OID_str(&v->handle), v->uid, v->gid, v->mode,
           ctimeStr, mtimeStr, atimeStr, ntimeStr);
    }

    /* union elements */
    switch (v->type)
    {
        case PVFS_TYPE_METAFILE:
            printf("(dfile_count: %u)(sid_count: %u)(dist_size: %u) (%d)\n",
                    v->u.metafile.dfile_count,
                    v->u.metafile.sid_count,
                    v->u.metafile.dist_size,
                    val.size);
            break;

        case PVFS_TYPE_DIRECTORY:
            printf("(dirent_count: %llu)"
                   "(tree_height: %d)(dirdata_count: %d)(sid_count: %d)"
                   "(bitmap_size: %d)(split_size: %d)(server_no: %d)"
                   "(branch_level: %d) (%d)\n",
                    llu(v->u.directory.dirent_count),
                    v->u.directory.tree_height,
                    v->u.directory.dirdata_count,
                    v->u.directory.sid_count,
                    v->u.directory.bitmap_size,
                    v->u.directory.split_size,
                    v->u.directory.server_no,
                    v->u.directory.branch_level,
                    val.size);
            break;

        case PVFS_TYPE_DATAFILE:
            printf("(bsize: %llu) (%d)\n", llu(v->u.datafile.b_size), val.size);
            break;

        case PVFS_TYPE_DIRDATA:
            printf("(dirent_count: %llu)"
                   "(tree_height: %d)(dirdata_count: %d)(sid_count: %d)"
                   "(bitmap_size: %d)(split_size: %d)(server_no: %d)"
                   "(branch_level: %d) (%d)\n",
                    llu(v->u.dirdata.dirent_count),
                    v->u.dirdata.tree_height,
                    v->u.dirdata.dirdata_count,
                    v->u.dirdata.sid_count,
                    v->u.dirdata.bitmap_size,
                    v->u.dirdata.split_size,
                    v->u.dirdata.server_no,
                    v->u.dirdata.branch_level,
                    val.size);
            break;

        default:
            printf("(%d)\n", val.size);
            break;
    }

    return;
}

void print_keyval( DBT key, DBT val )
{
    struct dbpf_keyval_db_entry *k;
    uint64_t kh;


    k = key.data;
    printf("(%s)", PVFS_OID_str(&k->handle));
    printf("(%c)", k->type);

    switch (k->type)
    {
        case DBPF_DIRECTORY_ENTRY_TYPE:
        {
            PVFS_handle *handle = val.data;
            printf("(%s)(%d) -> (%s) (%d)\n",
                   k->key,
                   key.size,
                   PVFS_OID_str(handle),
                   val.size);
        }
            break;

        case DBPF_ATTRIBUTE_TYPE:
            /* datafile handle */
            if( strncmp(k->key, "dh", 3) == 0)
            {
                PVFS_handle *handle = val.data;

                printf("(/dh)(%d) -> ", key.size);
                while ((char *)handle - (char *)val.data < val.size)
                {
                    printf("(%s)", PVFS_OID_str(handle));
                    handle++;
                }
                printf(" (%d)\n", val.size);
            }
            /* metafile dist */
            else if( strncmp(k->key, "md", 3) == 0 )
            {
                /* just print the name of the distribution, the rest is extra.
                 * the PINT_dist struct is packed/encoded before writing to db.
                 * that means the first uint32_t bytes are the length of the
                 * string, skip it.
                 */
                char *dname = (char *)val.data + sizeof(uint32_t);
                printf("(md)(%d) -> (%s) (%d)\n", key.size, dname, val.size );
            }
            /* symlink target */
            else if( strncmp(k->key, "st", 3) == 0 )
            {
                printf("(st)(%d) -> (%s) (%d)\n",
                       key.size,
                       (char *) val.data,
                       val.size);
            }
            /* metafile layout */
            else if( strncmp(k->key, "ml", 3) == 0 )
            {
                int32_t layout;

                printf("(ml)(%d) -> ", key.size);

                /* just print the name of the layout, the rest is extra. */
                layout = *(int32_t *)val.data;
                switch (layout)
                {
                    case PVFS_SYS_LAYOUT_NONE:
                        printf("(PVFS_SYS_LAYOUT_NONE) (%d)\n",
                               (int)sizeof(int32_t));
                        break;
                    case PVFS_SYS_LAYOUT_ROUND_ROBIN:
                        printf("(PVFS_SYS_LAYOUT_ROUND_ROBIN) (%d)\n",
                               (int)sizeof(int32_t));
                        break;
                    case PVFS_SYS_LAYOUT_RANDOM:
                         printf("(PVFS_SYS_LAYOUT_RANDOM) (%d)\n",
                               (int)sizeof(int32_t));
                        break;
                    case PVFS_SYS_LAYOUT_LIST:
                        printf("(PVFS_SYS_LAYOUT_LIST) (%d)\n",
                               (int)sizeof(int32_t));
                        break;
                    default:
                        printf("(unrecognized: %d) (%d)\n",
                               *(uint32_t *)val.data,
                               (int)sizeof(int32_t));
                        break;
                 }
            }
            /* num dfiles req */
            else if( strncmp(k->key, "nd", 3) == 0 )
            {
                printf("(nd)(%d) -> (%d) (%d)\n",
                       key.size,
                       *(uint32_t *)val.data,
                       val.size);
            }
            /* dist directory attr */
            else if( strncmp(k->key, "/dda", 5) == 0 )
            {
                PVFS_dist_dir_attr *dist_dir_attr =
                                   (PVFS_dist_dir_attr *) val.data;

                if (val.size == sizeof(PVFS_dist_dir_attr))
                {
                    printf("(/dda)(%d) -> (%d)(%d)(%d)(%d)(%d)(%d)(%d) (%d)\n",
                        key.size,
                        dist_dir_attr->tree_height,
                        dist_dir_attr->dirdata_count,
                        dist_dir_attr->sid_count,
                        dist_dir_attr->bitmap_size,
                        dist_dir_attr->split_size,
                        dist_dir_attr->server_no,
                        dist_dir_attr->branch_level,
                        val.size);
                }
                else
                {
                    printf("(/dda)(%d) -> "
                           "Invalid size for distributed directory attributes."
                           "(%d)\n",
                           key.size,
                           val.size);
                }
            }
            /* dist directory handles */
            else if( strncmp(k->key, "/ddh", 5) == 0 )
            {
                PVFS_handle *handle = val.data;

                printf("(/ddh)(%d) -> ", key.size);
                while ((char *)handle - (char *)val.data < val.size)
                {
                    printf("(%s)", PVFS_OID_str(handle));
                    handle++;
                }
                printf(" (%d)\n", val.size);
            }
            /* dist directory bitmap */
            else if( strncmp(k->key, "/ddb", 5) == 0 )
            {
                int i;
                unsigned char *c = NULL;

                printf("(/ddb)(%d) -> ", key.size);
                for(i = val.size - 4; i >= 0 ; i--)
                {
                    c = ((unsigned char *)val.data + i);
                    printf(" %02x %02x %02x %02x", c[3], c[2], c[1], c[0]);
                }
                printf(" (%d)\n", val.size);
            }
            else if (key.size == 17)
            {
                char *tmp;
                tmp = k->key;
                kh = *(uint64_t *)tmp;
                printf("(%llu)(%d) -> (%d)\n", llu(kh), key.size, val.size);
            }
            else
            {
                printf("unrecognized attribute record type: %s\n", k->key);
            }
            break;

        case DBPF_COUNT_TYPE:
            printf("()(%d) -> (%llu) (%d)\n",
                   key.size,
                   llu(*(uint64_t *)val.data),
                   val.size);
            break;

        default:
            printf("unrecognized record type: %c\n", k->type);
            break;
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
        if (hex)
            printf("(%llx) (%d)\n", llu(vu), val.size);
        else
            printf("(%llu) (%d)\n", llu(vu), val.size);
    }
    else if (val.size == 16)
    {
        printf("(%s) (%d)\n", PVFS_OID_str((PVFS_OID *)val.data), val.size);
    }
    else
    {
        /* assume it is a string */
        vs = val.data;
        printf("(%s) (%d)\n", vs, val.size);
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
        {"hexhandles",0,0,0},
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
            case 4: /* hexhandles */
                hex = 1;
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
                    "\t--hexhandles\t\tPrint handles in hex\n"
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

#define DBPF_KEYVAL_DB_ENTRY_KEY_SIZE(_size) \
    (_size - sizeof(TROVE_handle))

int PINT_trove_dbpf_keyval_compare(
    DB * dbp, const DBT * a, const DBT * b)
{
    struct dbpf_keyval_db_entry db_entry_a;
    struct dbpf_keyval_db_entry db_entry_b;

    memcpy(&db_entry_a, a->data, sizeof(struct dbpf_keyval_db_entry));
    memcpy(&db_entry_b, b->data, sizeof(struct dbpf_keyval_db_entry));

    if(PVFS_OID_cmp(&db_entry_a.handle, &db_entry_b.handle))
    {
        return (PVFS_OID_cmp(&db_entry_a.handle, &db_entry_b.handle)) ? -1 : 1;
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

