/*
 * (C) 2004 Clemson University and The University of Chicago
 * 
 * See COPYING in top-level directory.
 */

#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <getopt.h>

#include "pvfs2.h"
#include "str-utils.h"
#include "pint-sysint-utils.h"
#include "pint-util.h"
#include "pvfs2-internal.h"
#include "pvfs2-req-proto.h"
#include "client-state-machine.h"
#include "server-config-mgr.h"

#define __PINT_REQPROTO_ENCODE_FUNCS_C

#define KEYBUFSZ 1024
#define VALBUFSZ 4096

struct opts
{
    PVFS_ds_keyval key;
    PVFS_ds_keyval val;
    char *pvfs_root;
    uint32_t count;
    uint32_t query;
};

struct dbpf_keyval_db_entry
{
    TROVE_handle handle;
    char key[PVFS_NAME_MAX];
};

static int pvfs2_getval(PVFS_object_ref obj, 
                        struct opts *opt, 
                        const PVFS_credentials *creds);

static void usage(int argc, char** argv);
static struct opts* parse_args(int argc, char* argv[]);

int main(int argc, char **argv)
{
    int ret =0;
    struct opts *user_opts;
    PVFS_object_ref ref;
    PVFS_credentials credentials;

    /* look at command line arguments */
    user_opts = parse_args(argc, argv);
    if(user_opts==NULL)
    {
          fprintf(stderr, "Error: failed to parse command line arguments.\n");
          return(-1);
    }
    if( (ret = PVFS_util_init_defaults()) < 0 )
    {
          PVFS_perror("PVFS_util_init_defaults", ret);
          return(ret);
    }
    
    memset(&ref, 0, sizeof(PVFS_object_ref));
    ref.fs_id = -1;
    ref.handle = -1;

    if( (ret = PVFS_util_get_fsid( user_opts->pvfs_root, &(ref.fs_id) )) < 0 )
    {
        PVFS_perror("PVFS_util_get_fsid", ret);
        return ret;
    }
    printf("Got FSID: %d\n", ref.fs_id);

    if( ref.fs_id == -1 )
    {
        ret = PVFS_EINVAL;
        PVFS_perror("Invalid PVFS2 mount specified", ret);
        return(ret);
    }

    PINT_cached_config_get_root_handle(ref.fs_id, &(ref.handle));

    printf("Searching %s for %s/%s, fs_id: %d, root handle: %llu\n", 
        user_opts->pvfs_root, (char *)user_opts->key.buffer, 
        (char *)user_opts->val.buffer, ref.fs_id, llu(ref.handle));

    PVFS_util_gen_credentials(&credentials);
    ret = pvfs2_getval( ref, user_opts, &credentials);
    PVFS_sys_finalize();
    free(user_opts->pvfs_root);
    free(user_opts->key.buffer);
    free(user_opts->val.buffer);
    free(user_opts);
    return(ret);
}

/* pvfs2_getval()
 *
 * changes the mode of the given file to the given permissions
 *
 * returns zero on success and negative one on failure
 */
static int pvfs2_getval(PVFS_object_ref obj, struct opts *opt, 
                        const PVFS_credentials *creds) 
{
    int ret = -1, lpath_len = 0, i = 0, j = 0, srv_count=0, all_end=0;
    char *local_path = NULL;
    struct PINT_client_getvalue_sm resp;
    struct dbpf_keyval_db_entry *key_entry;
    PVFS_ds_position *token = NULL;
    PVFS_sysresp_getvalue *resp_getvalue;
    PVFS_dirent d;

    memset(&resp, 0, sizeof(struct PINT_client_getvalue_sm));
    memset(&d, 0, sizeof(PVFS_dirent));

    ret = PINT_cached_config_count_servers( obj.fs_id,
        PINT_SERVER_TYPE_META, &(srv_count) );

    if( srv_count < 1 )
    {
        gossip_err("Number of meta servers incorect. ret=%d, count=%d\n",
                   ret, srv_count);
    }
    printf("Number of servers: %d\n", srv_count);

    if( (token = calloc(srv_count, sizeof(PVFS_ds_position)) ) == 0 )
    {
        gossip_debug(GOSSIP_CLIENT_DEBUG, "[GETVALUE]: malloc failure for "
                     " token\n");
        PVFS_perror("PVFS_sys_getvalue", PVFS_ENOMEM);
        return -PVFS_ENOMEM;
    }

    for( i = 0; i < srv_count; i++ )
    {
        token[i] = PVFS_ITERATE_START;
    }

    if( (resp_getvalue = calloc(srv_count, sizeof(PVFS_sysresp_getvalue)))
        == 0 )
    {
        PVFS_perror("PVFS_sys_getvalue", PVFS_ENOMEM);
        return -PVFS_ENOMEM;
    }

    for( i = 0; i < srv_count; i++ )
    {
        resp_getvalue[i].token = 0;
        resp_getvalue[i].count = 0;
        resp_getvalue[i].match_count = 0;

        if( (resp_getvalue[i].key = 
             (PVFS_ds_keyval *)calloc(opt->count, sizeof(PVFS_ds_keyval)))==0 )
        {
            PVFS_perror("PVFS_sys_getvalue", PVFS_ENOMEM);
            return -PVFS_ENOMEM;
        }

        if( (resp_getvalue[i].val = 
             (PVFS_ds_keyval *)calloc(opt->count, sizeof(PVFS_ds_keyval)))==0 )
        {
            PVFS_perror("PVFS_sys_getvalue", PVFS_ENOMEM);
            return -PVFS_ENOMEM;
        }
        
        if( (resp_getvalue[i].dirent = 
             calloc(opt->count, sizeof(PVFS_dirent)))==0 )
        {
            PVFS_perror("PVFS_sys_getvalue", PVFS_ENOMEM);
            return -PVFS_ENOMEM;
        }

        for( j = 0; j < opt->count; j++ )
        {
            resp_getvalue[i].key[j].buffer = calloc( 1, KEYBUFSZ);
            resp_getvalue[i].val[j].buffer = calloc( 1, VALBUFSZ);
            resp_getvalue[i].key[j].buffer_sz = KEYBUFSZ;
            resp_getvalue[i].val[j].buffer_sz = VALBUFSZ;
        }
    }

    printf("Asking for %u records\n", opt->count );

    all_end = 0;
    while( all_end < srv_count)
    {
        all_end = 0;
        /* zero appropriate fields between iterations */
        for( i=0; i < srv_count; i++ )
        {
            for( j=0; j < opt->count; j++ )
            {
                memset(resp_getvalue[i].key[j].buffer, 0, KEYBUFSZ);
                memset(resp_getvalue[i].val[j].buffer, 0, VALBUFSZ);
                memset(&(resp_getvalue[i].dirent[j]), 0, sizeof(PVFS_dirent));
            }
        }

        ret += PVFS_sys_getvalue(obj, token, creds,  &(opt->key), &(opt->val), 
            opt->query, opt->count, resp_getvalue, NULL);

        for( i=0; i < srv_count; i++ )
        {
            printf("Call returned, meta server %d says the  query "
                   "matched %d records\n", i, resp_getvalue[i].match_count);
        }

        all_end = 0;
        for( i = 0; i < srv_count; i++ )
        {
            token[i] = resp_getvalue[i].token;
            printf("Meta (%d) returned %d records, token (%llu)\n", i, 
                    resp_getvalue[i].count, llu(resp_getvalue[i].token));

             /* have to prepend local mount point to path returned
             * from server */
            for( j = 0; j < resp_getvalue[i].count; j++ )
            {
                lpath_len = strlen(resp_getvalue[i].dirent[j].d_name) +
                         strlen(opt->pvfs_root) + 1;
                if( ( local_path = realloc(local_path, lpath_len+1) ) == 0 )
                {   
                    printf("malloc error\n");
                    return -1;
                }

                memset(local_path, 0, lpath_len+1);
                strncpy(local_path, opt->pvfs_root, strlen(opt->pvfs_root));
                strncpy(local_path+strlen(opt->pvfs_root ), 
                      resp_getvalue[i].dirent[j].d_name, 
                      strlen(resp_getvalue[i].dirent[j].d_name) );
           
                key_entry = resp_getvalue[i].key[j].buffer;
                printf("\t%s (handle: %llu) (next token: %llu) (%s->%s) \n",
                    local_path, llu(key_entry->handle), 
                    token[i], key_entry->key, 
                    (char *)resp_getvalue[i].val[j].buffer);
            }
            all_end += (token[i] == PVFS_ITERATE_END)? 1 : 0;
        }
    }

    for( i=0; i < srv_count; i++ )
    {
        for( j=0; j < opt->count; j++ )
        {
            free(resp_getvalue[i].key[j].buffer);
            free(resp_getvalue[i].val[j].buffer);
        }

        free(resp_getvalue[i].key);
        free(resp_getvalue[i].val);
        free(resp_getvalue[i].dirent);
    }

    free(local_path);
    free(resp_getvalue);
    free(token);

    return 0;
}

/* parse_args()
 *
 * parses command line arguments
 *
 * returns pointer to options structure on success, NULL on failure
 */
static struct opts* parse_args(int argc, char* argv[])
{
    char flags[] = "cpk:v:n:q:";
    int one_opt = 0;

    struct opts* tmp_opts = NULL;

    if( argc < 9 )
    {
        usage(argc, argv);
    }
    /* create storage for the command line options */
    tmp_opts = (struct opts*)malloc(sizeof(struct opts));
    if(!tmp_opts){
	return(NULL);
    }
    memset(tmp_opts, 0, sizeof(struct opts));

    /* fill in defaults */
    memset(&tmp_opts->key, 0, sizeof(PVFS_ds_keyval));
    memset(&tmp_opts->val, 0, sizeof(PVFS_ds_keyval));
    tmp_opts->pvfs_root = strdup(argv[argc-1]);

    /* look at command line arguments */
    while((one_opt = getopt(argc, argv, flags)) != EOF)
    {
	switch(one_opt){
            case 'c':
                tmp_opts->query |= PVFS_KEYVAL_QUERY_NORM;
                break;
            case 'k':
                tmp_opts->key.buffer = strdup(optarg);
                tmp_opts->key.buffer_sz = strlen(tmp_opts->key.buffer) + 1;
                break;
            case 'v':
                tmp_opts->val.buffer = strdup(optarg);
                tmp_opts->val.buffer_sz = strlen(tmp_opts->val.buffer) + 1;
                break;
            case 'n':
                tmp_opts->count = atoi( optarg );
                break;
            case 'p':
                tmp_opts->query |= PVFS_KEYVAL_RESULT_NO_PATHS;
                break;
            case 'q':
                if( strncmp( "LT", optarg, 2) == 0 )
                { tmp_opts->query |= PVFS_KEYVAL_QUERY_LT; }
                else if( strncmp( "LE", optarg, 2) == 0 )
                { tmp_opts->query |= PVFS_KEYVAL_QUERY_LE; }
                else if( strncmp( "EQ", optarg, 2) == 0 )
                { tmp_opts->query |= PVFS_KEYVAL_QUERY_EQ; }
                else if( strncmp( "GE", optarg, 2) == 0 )
                { tmp_opts->query |= PVFS_KEYVAL_QUERY_GE; }
                else if( strncmp( "GT", optarg, 2) == 0 )
                { tmp_opts->query |= PVFS_KEYVAL_QUERY_GT; }
                else if( strncmp( "NT", optarg, 2) == 0 )
                { tmp_opts->query |= PVFS_KEYVAL_QUERY_NT; }
                else if( strncmp( "PEQ", optarg, 3) == 0 )
                { tmp_opts->query |= PVFS_KEYVAL_QUERY_PEQ; }
                else
                {
                    printf("?: %s\n", optarg);
		    usage(argc, argv);
		    exit(EXIT_FAILURE);
                }
                break;
	    case('?'):
                printf("?: %d\n", one_opt);
		usage(argc, argv);
		exit(EXIT_FAILURE);
	}
    }
    
    if( tmp_opts->key.buffer == NULL)
    {
      fprintf(stderr, "The key (attribute) to find must be provided");
      usage(argc, argv);
      exit(EXIT_FAILURE);
    }
    if( tmp_opts->val.buffer == NULL)
    {
      fprintf(stderr, "The value to find must be provided");
      usage(argc, argv);
      exit(EXIT_FAILURE);
    }
    if( tmp_opts->count < 1 )
    {
        fprintf(stderr, "Count must be 1 or greater");
        usage(argc, argv);
        exit(EXIT_FAILURE);
    }
    return(tmp_opts);
}


static void usage(int argc, char** argv)
{
    fprintf(stderr, 
        "Usage: %s -k <key> -v <val> -n <num> [-c] [-p] -q [LT|LE|EQ|GE|GT|NT|"
        "PEQ] <pvfs fs root>\n\n\t c - disable case sensitivity in queries\n\t"
        "p - disable path resolution after completing query\n", 
        argv[0]);
    return;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=4 sts=4 sw=4 expandtab
 */

