/*
 * (C) 2010 Clemson University
 *
 * See COPYING in top-level directory.
 */

/** \file
 * Utility for displaying PVFS Berkeley DBs in textual form for debugging
 *   */

#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>

#include "pvfs2-types.h"
#include "trove-types.h"
#include "pvfs2-storage.h"
#include "pvfs2-internal.h"
#include "trove-dbpf/dbpf.h"
#include "pint-util.h"

#define COLLECTION_FILE         "collections.db"
#define STORAGE_FILE            "storage_attributes.db"
#define DATASPACE_FILE          "dataspace_attributes.db"
#define KEYVAL_FILE             "keyval.db"
#define COLLECTION_ATTR_FILE    "collection_attributes.db"

typedef struct
{
    char dbpath[PATH_MAX];
    char hexdir[PATH_MAX];
    int verbose;
} options_t;

/* globals */
static options_t opts;
int hex = 0;

void iterate_database(dbpf_db *db_p, void (*print)(struct dbpf_data key,
    struct dbpf_data val));
void print_collection( struct dbpf_data key, struct dbpf_data val );
void print_storage( struct dbpf_data key, struct dbpf_data val );
void print_dspace( struct dbpf_data key, struct dbpf_data val );
void print_keyval( struct dbpf_data key, struct dbpf_data val );
void print_collection_attr( struct dbpf_data key, struct dbpf_data val );
void print_help(char *progname);
void print_ds_type( PVFS_ds_type type );
int process_args(int argc, char ** argv);

int main( int argc, char **argv )
{
    dbpf_db *db_p = NULL;
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

    /* open and print each database */

    /* collection database */
    sprintf(path, "%s/%s", opts.dbpath, COLLECTION_FILE );
    ret = dbpf_db_open(path, 0, &db_p, 0, NULL);
    if (ret == 0) 
    {
        printf("Collection Database\n");
        iterate_database(db_p, &print_collection );
        dbpf_db_close(db_p);
    }

    /* collection attribute database */
    memset(path, path_len, sizeof(char));
    sprintf(path, "%s/%s/%s", opts.dbpath, opts.hexdir, COLLECTION_ATTR_FILE );
    ret = dbpf_db_open(path, 0, &db_p, 0, NULL);
    if (ret == 0) 
    {
        printf("Collection Attributes Database\n");
        iterate_database(db_p, &print_collection_attr );
        dbpf_db_close(db_p);
    }
    
    /* storage database */
    memset(path, path_len, sizeof(char));
    sprintf(path, "%s/%s", opts.dbpath, STORAGE_FILE );
    ret = dbpf_db_open(path, 0, &db_p, 0, NULL);
    if (ret == 0) 
    {
        printf("Storage Database\n");
        iterate_database(db_p, &print_storage );
        dbpf_db_close(db_p);
    }

    /* dspace database */
    memset(path, path_len, sizeof(char));
    sprintf(path, "%s/%s/%s", opts.dbpath, opts.hexdir, DATASPACE_FILE );
    ret = dbpf_db_open(path, DBPF_DB_COMPARE_DS_ATTR, &db_p, 0, NULL);
    if (ret == 0) 
    {
        printf("Dataspace Database\n");
        iterate_database(db_p, &print_dspace );
        dbpf_db_close(db_p);
    }

    /* keyval database */
    memset(path, path_len, sizeof(char));
    sprintf(path, "%s/%s/%s", opts.dbpath, opts.hexdir, KEYVAL_FILE );
    ret = dbpf_db_open(path, DBPF_DB_COMPARE_KEYVAL, &db_p, 0, NULL);
    if (ret == 0) 
    {
        printf("Keyval Database\n");
        iterate_database(db_p, &print_keyval );
        dbpf_db_close(db_p);
    }

    free(path);
    return 0;
}

void iterate_database(dbpf_db *db_p, void (*print)(struct dbpf_data key,
    struct dbpf_data val) )
{
    int ret = 0;
    dbpf_cursor *dbc_p = NULL;
    struct dbpf_data key, val;

    ret = dbpf_db_cursor(db_p, &dbc_p, 1);
    if( ret != 0 )
    {
        printf("Unable to open cursor to print db: %s\n", 
               strerror(ret));
        return;
    }

    key.len = 1024; 
    key.data = malloc(1024); 
    val.len = 1024; 
    val.data = malloc(1024); 
    if (!key.data || !val.data)
    {
        printf("Unable to allocate memory\n");
        return;
    }
 
    printf("-------- Start database --------\n");
    while ((ret = dbpf_db_cursor_get(dbc_p, &key, &val, DBPF_DB_CURSOR_NEXT, 1024)) == 0)
    {
        print( key, val );
        key.len = 1024; 
        val.len = 1024; 
    }

    /* making this neg seems right but ... */
    if( ret != -TROVE_ENOENT )
    {
        printf("**** an error occurred (%s) ****\n", strerror(ret));
    }
    printf("-------- End database --------\n\n");

    dbpf_db_cursor_close(dbc_p);
    return;
}

void print_collection( struct dbpf_data key, struct dbpf_data val )
{
    char *k;
    int32_t v;
    k = key.data;
    v = *(int32_t *)val.data;
    if (hex) 
        printf("(%s)(%zu) -> (%x) (%zu)\n", k, key.len, v, val.len);
    else
        printf("(%s)(%zu) -> (%d) (%zu)\n", k, key.len, v, val.len);
    return;
}

void print_storage( struct dbpf_data key, struct dbpf_data val )
{
    char *k;
    int32_t v;
    k = key.data;
    v = *(int32_t *)val.data;
    if (hex) 
        printf("(%s)(%zu) -> (%x) (%zu)\n", k, key.len, v, val.len);
    else
        printf("(%s)(%zu) -> (%d) (%zu)\n", k, key.len, v, val.len);
    return;
}

void print_dspace( struct dbpf_data key, struct dbpf_data val )
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
        r_mtime = PINT_util_mkversion_time(r_mtime);
        if (r_mtime == 0)
        {
            r_mtime = (time_t) v->mtime;
        }

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

    printf("(%s)(%zu) -> ", PVFS_OID_str(k), key.len);

    print_ds_type( v->type );

    printf("(fsid: %d)(handle: %s)(uid: %u)(gid: %u)"
       "(perm: %o)(ctime: %s)(mtime: %s)(atime: %s)(ntime: %s)"
       "(meta_sid_count %d)",
       v->fs_id, PVFS_OID_str(&v->handle), v->uid, v->gid, v->mode,
       ctimeStr, mtimeStr, atimeStr, ntimeStr,v->meta_sid_count);

    /* union elements */
    switch (v->type)
    {
        case PVFS_TYPE_METAFILE:
            printf("(dfile_count: %u)(sid_count: %u)(dist_size: %u) (%zu)\n",
                    v->u.metafile.dfile_count,
                    v->u.metafile.sid_count,
                    v->u.metafile.dist_size,
                    val.len);
            break;

        case PVFS_TYPE_DIRECTORY:
            printf("(dirent_count: %llu)"
                   "(tree_height: %d)"
                   "(dirdata_count: %d)"
                   "(dirdata_min: %d)"
                   "(dirdata_max: %d)"
                   "(bitmap_size: %d)"
                   "(split_size: %d)"
                   "(branch_level: %d)"
                   "(hint_dist_name_len: %d)"
                   "(hint_dist_params_len: %d)"
                   "(hint_dfile_count: %d)"
                   "(hint_dfile_sid_count: %d)"
                   "(hint_layout_algorithm: %d)"
                   "(hint_layout_list_cnt: %d)"
                   "(hint_dirdata_min: %d)"
                   "(hint_dirdata_max: %d)"
                   "(hint_split_size: %d)"
                   "(hint_dir_layout_algorithm: %d)"
                   "(hint_dir_layout_list_cnt: %d)"
                   "(%zu)\n",
                    llu(v->u.directory.dirent_count),
                    v->u.directory.tree_height,
                    v->u.directory.dirdata_count,
                    v->u.directory.dirdata_min,
                    v->u.directory.dirdata_max,
                    v->u.directory.bitmap_size,
                    v->u.directory.split_size,
                    v->u.directory.branch_level,
                    v->u.directory.hint_dist_name_len,
                    v->u.directory.hint_dist_params_len,
                    v->u.directory.hint_dfile_count,
                    v->u.directory.hint_dfile_sid_count,
                    v->u.directory.hint_layout_algorithm,
                    v->u.directory.hint_layout_list_cnt,
                    v->u.directory.hint_dirdata_min,
                    v->u.directory.hint_dirdata_max,
                    v->u.directory.hint_split_size,
                    v->u.directory.hint_dir_layout_algorithm,
                    v->u.directory.hint_dir_layout_list_cnt,
                    val.len);
            break;

        case PVFS_TYPE_DATAFILE:
            printf("(bsize: %llu) (%zu)\n", llu(v->u.datafile.b_size), val.len);
            break;

        case PVFS_TYPE_DIRDATA:
            printf("(dirent_count: %llu)"
                   "(tree_height: %d)(dirdata_count: %d)"
                   "(bitmap_size: %d)(split_size: %d)(server_no: %d)"
                   "(branch_level: %d) (%zu)\n",
                    llu(v->u.dirdata.dirent_count),
                    v->u.dirdata.tree_height,
                    v->u.dirdata.dirdata_count,
                    v->u.dirdata.bitmap_size,
                    v->u.dirdata.split_size,
                    v->u.dirdata.server_no,
                    v->u.dirdata.branch_level,
                    val.len);
            break;

        default:
            printf("(%zu)\n", val.len);
            break;
    }

    return;
}

void print_keyval( struct dbpf_data key, struct dbpf_data val )
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
            printf("(%s)(%zu) -> ", k->key, key.len);
            while ((char *)handle - (char *)val.data < val.len)
            {
                printf("(%s)", PVFS_OID_str(handle));
                handle++;
            }
            printf(" (%zu)\n", val.len);
        }
            break;

        case DBPF_ATTRIBUTE_TYPE:
            /* datafile handle */
            if( strncmp(k->key, "dh", 3) == 0)
            {
                PVFS_handle *handle = val.data;

                printf("(dh)(%zu) -> ", key.len);
                while ((char *)handle - (char *)val.data < val.len)
                {
                    printf("(%s)", PVFS_OID_str(handle));
                    handle++;
                }
                printf(" (%zu)\n", val.len);
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
                printf("(md)(%zu) -> (%s) (%zu)\n", key.len, dname, val.len );
            }
            /* symlink target */
            else if( strncmp(k->key, "st", 3) == 0 )
            {
                printf("(st)(%zu) -> (%s) (%zu)\n",
                       key.len,
                       (char *) val.data,
                       val.len);
            }
            /* metafile layout */
            else if( strncmp(k->key, "ml", 3) == 0 )
            {
                int32_t layout;

                printf("(ml)(%zu) -> ", key.len);

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
                        printf("(unrecognized layout: %d) (%d)\n",
                               *(uint32_t *)val.data,
                               (int)sizeof(int32_t));
                        break;
                 }
            }
            /* num dfiles req */
            else if( strncmp(k->key, "nd", 3) == 0 )
            {
                printf("(nd)(%zu) -> (%d) (%zu)\n",
                       key.len,
                       *(uint32_t *)val.data,
                       val.len);
            }
            /* parent handle */
            else if( strncmp(k->key, "pa", 3) == 0)
            {
                PVFS_handle *handle = val.data;

                printf("(pa)(%zu) -> ", key.len);
                while ((char *)handle - (char *)val.data < val.len)
                {
                    printf("(%s)", PVFS_OID_str(handle));
                    handle++;
                }
                printf(" (%zu)\n", val.len);
            }
            /* dist directory attr */
            else if( strncmp(k->key, "/dda", 5) == 0 )
            {
                PVFS_dist_dir_attr *dist_dir_attr =
                                   (PVFS_dist_dir_attr *) val.data;

                if (val.len == sizeof(PVFS_dist_dir_attr))
                {
                    printf("(/dda)(%zu) -> "
                        "(%d)(%d)(%d)(%d)(%d)(%d)(%d) (%zu)\n",
                        key.len,
                        dist_dir_attr->tree_height,
                        dist_dir_attr->dirdata_count,
                        dist_dir_attr->sid_count,
                        dist_dir_attr->bitmap_size,
                        dist_dir_attr->split_size,
                        dist_dir_attr->server_no,
                        dist_dir_attr->branch_level,
                        val.len);
                }
                else
                {
                    printf("(/dda)(%zu) -> "
                           "Invalid size for distributed directory attributes."
                           "(%zu)\n",
                           key.len,
                           val.len);
                }
            }
            /* dist directory handles */
            else if( strncmp(k->key, "/ddh", 5) == 0 )
            {
                PVFS_handle *handle = val.data;

                printf("(/ddh)(%zu) -> ", key.len);
                while ((unsigned char *)handle -
                       (unsigned char *)val.data < val.len)
                {
                    printf("(%s)", PVFS_OID_str(handle));
                    handle++;
                }
                printf(" (%zu)\n", val.len);
            }
            /* dist directory bitmap */
            else if( strncmp(k->key, "/ddb", 5) == 0 )
            {
                int i;
                unsigned char *c = NULL;

                printf("(/ddb)(%zu) -> ", key.len);
                for(i = val.len - 4; i >= 0 ; i--)
                {
                    c = ((unsigned char *)val.data + i);
                    printf(" %02x %02x %02x %02x", c[3], c[2], c[1], c[0]);
                }
                printf(" (%zu)\n", val.len);
            }
            else if (key.len == 17)
            {
                char *tmp;
                tmp = k->key;
                kh = *(uint64_t *)tmp;
                printf("(%llu)(%zu) -> (%zu)\n", llu(kh), key.len, val.len);
            }
            else if (strncmp(k->key, "user.", 5) == 0)
            {
                int i;
                char *dat = (char *)val.data;
                for(i = 0; i < val.len; i++)
                {
                    if (!isprint(dat[i]))
                    {
                        break;
                    }
                }
                if (i == val.len - 1)
                {
                    /* string will drop out on the null terminator */
                    printf("(%s)(%d) -> (%s)(%d)\n", k->key, (int)key.len,
                           dat, (int)val.len);
                }
                else if (i == val.len)
                {
                    /* unterminated string  - we will zero the
                     * last char and print what we can*/
                    dat[val.len - 1] = 0;
                    printf("(%s)(%d) -> (%s ...)(%d)\n", k->key, (int)key.len,
                           dat, (int)val.len);
                }
                else
                {
                    /* not string */
                    printf("(%s)(%d) -> (0x%x ...)(%d)\n", k->key, (int)key.len,
                           *(int *)dat, (int)val.len);
                }
            }
            else
            {
                printf("unrecognized attribute record type: %s val size %d\n",
                       k->key, (int)val.len);
            }
            break;

        case DBPF_COUNT_TYPE:
            printf("()(%zu) -> (%llu) (%zu)\n",
                   key.len,
                   llu(*(uint32_t *)val.data),
                   val.len);
            break;

        default:
            printf("unrecognized record type: %c\n", k->type);
            break;
    }
    return;
}

void print_collection_attr( struct dbpf_data key, struct dbpf_data val )
{
    char *k, *vs;
    uint64_t vu;
    k = key.data;
    printf("(%s)(%zu) -> ", k, key.len);
    if( val.len == 8 )
    {
        vu = *(uint64_t *)val.data;
        if (hex)
            printf("(%llx) (%zu)\n", llu(vu), val.len);
        else
            printf("(%llu) (%zu)\n", llu(vu), val.len);
    }
    else if (val.len == 16)
    {
        printf("(%s) (%zu)\n", PVFS_OID_str((PVFS_OID *)val.data), val.len);
    }
    else
    {
        /* assume it is a string */
        vs = val.data;
        printf("(%s) (%zu)\n", vs, val.len);
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
                strncpy(opts.dbpath, optarg, PATH_MAX-1);
                break;
            case 3: /* hexdir */
                strncpy(opts.hexdir, optarg, PATH_MAX-1);
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

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

