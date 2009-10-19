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

struct getvalue
{
    int meta_count;
    uint32_t count;
    uint32_t query_count;
    uint32_t query_type;
    char *pvfs_root;
    PVFS_ds_keyval key;
    PVFS_ds_keyval val;
    PVFS_keyval_query *query_p;
    PVFS_object_ref obj;
    PVFS_sysresp_getvalue *resp_p;
};

struct dbpf_keyval_db_entry
{
    TROVE_handle handle;
    char key[PVFS_NAME_MAX];
};

static int pvfs2_setup_query(struct getvalue *state);
static int pvfs2_setup_msg(struct getvalue *state);
static int pvfs2_send_msg(struct getvalue *state);
static int pvfs2_cleanup(struct getvalue *state);

static int setup_query_struct(PVFS_keyval_query *q, uint32_t c);
static void usage(int argc, char** argv);
static struct getvalue* parse_args(int argc, char* argv[]);

int main(int argc, char **argv)
{
    int ret =0;
    struct getvalue *state;

    /* look at command line arguments */
    state = parse_args(argc, argv);
    if(state==NULL)
    {
          fprintf(stderr, "Error: failed to parse command line arguments.\n");
          return(-1);
    }
    if( (ret = PVFS_util_init_defaults()) < 0 )
    {
          PVFS_perror("PVFS_util_init_defaults", ret);
          return(ret);
    }
    
    state->obj.fs_id = -1;
    state->obj.handle = -1;

    if( (ret = PVFS_util_get_fsid(state->pvfs_root, &(state->obj.fs_id) )) < 0 )
    {
        PVFS_perror("PVFS_util_get_fsid", ret);
        return ret;
    }
    printf("Got FSID: %d\n", state->obj.fs_id);

    if( state->obj.fs_id == -1 )
    {
        ret = PVFS_EINVAL;
        PVFS_perror("Invalid PVFS2 mount specified", ret);
        return(ret);
    }

    PINT_cached_config_get_root_handle(state->obj.fs_id, &(state->obj.handle));

    printf("Searching %s for %s/%s, fs_id: %d, root handle: %llu\n", 
        state->pvfs_root, (char *)state->key.buffer, 
        (char *)state->val.buffer, state->obj.fs_id, llu(state->obj.handle));

    ret = pvfs2_setup_query( state );
    ret = pvfs2_setup_msg( state );

    printf("resp_p: %p, query_p: %p\n", state->resp_p, state->query_p);

    ret = pvfs2_send_msg( state );
    ret = pvfs2_cleanup( state );

    PVFS_sys_finalize();
    return ret;
}

static int pvfs2_setup_query(struct getvalue *state)
{
    uint32_t i=0;

    /* handle simple query to start */
    state->query_count = 3;

    if( (state->query_p = 
        calloc( state->query_count, sizeof(PVFS_keyval_query))) == 0 )
    {
        PVFS_perror("PVFS_sys_getvalue", PVFS_ENOMEM);
        return PVFS_ENOMEM;
    }

    for( i=0; i < state->query_count; i++ )
    {
        setup_query_struct(&(state->query_p[i]), state->count);
    }

    state->query_p[0].oper = state->query_type;
    state->query_p[1].oper = PVFS_KEYVAL_QUERY_NOOP;
    state->query_p[2].oper = PVFS_KEYVAL_QUERY_NOOP;

    memcpy( state->query_p[1].query.buffer, state->key.buffer,
             state->key.buffer_sz);
    memcpy( state->query_p[2].query.buffer, state->val.buffer,
             state->val.buffer_sz);
    state->query_p[1].query.buffer_sz = state->key.buffer_sz;
    state->query_p[2].query.buffer_sz = state->val.buffer_sz;

    return 0;
}

/* pvfs2_setup_msg()
 */
static int pvfs2_setup_msg(struct getvalue *state)
{
    int ret = 0, i = 0, j = 0;
    ret = PINT_cached_config_count_servers( state->obj.fs_id,
        PINT_SERVER_TYPE_META, &(state->meta_count) );

    if( state->meta_count < 1 )
    {
        gossip_err("Number of meta servers incorect. ret=%d, count=%d\n",
                   ret, state->meta_count);
        return -1;
    }

    printf("Number of metadata servers: %d\n", state->meta_count);
    if( (state->resp_p = calloc(state->meta_count, 
                                sizeof(PVFS_sysresp_getvalue))) == 0 )
    {
        PVFS_perror("PVFS_sys_getvalue", PVFS_ENOMEM);
        return -PVFS_ENOMEM;
    }

    /* setup response struct for each server */
    for( i=0; i < state->meta_count; i++ )
    {
        printf("i: %d, resp_p: %p\n", i, &(state->resp_p[i]));
    
        state->resp_p[i].query_count = 0;
        state->resp_p[i].dirent_count = 0;

        if( (state->resp_p[i].dirent_p = 
            calloc(state->count, sizeof(PVFS_dirent))) == 0 )
        {
            PVFS_perror("PVFS_sys_getvalue", PVFS_ENOMEM);
            return -PVFS_ENOMEM;
        }

        if( (state->resp_p[i].query_p = 
            calloc(state->query_count, sizeof(PVFS_keyval_query)))==0)
        {
            PVFS_perror("PVFS_sys_getvalue", PVFS_ENOMEM);
            return -PVFS_ENOMEM;
        }
        
        for( j=0; j < state->query_count; j++ )
        {
            setup_query_struct(&(state->resp_p[i].query_p[j]), state->count);
        }
    }
    return 0;
}

static int pvfs2_send_msg(struct getvalue *state)
{
    int ret=0, done=0, lpath_len=0, i=0, j=0;
    PVFS_credentials creds;
    char *local_path = NULL;

    PVFS_util_gen_credentials(&creds);

    printf("Asking for %u records from %d servers\n", state->count, 
        state->meta_count );
    while( done < state->meta_count)
    {
        done = 0;

        /* zero appropriate fields between iterations */
        for( i=0; i < state->meta_count; i++ )
        {
            for( j=0; j < state->query_count; j++ )
            {
                if(state->resp_p[i].query_p[j].match)
                {
                    PVFS_handle *temp = state->resp_p[i].query_p[j].match;
                    memset(temp, 0, state->count * sizeof(PVFS_handle));
                    memset(&(state->resp_p[i].query_p[j]), 0, 
                        sizeof(PVFS_keyval_query));
                    state->resp_p[i].query_p[j].match = temp;
                }
            }
            memset(state->resp_p[i].dirent_p, 0, 
                (state->count * sizeof(PVFS_dirent)));
        }

        ret = PVFS_sys_getvalue(state->obj, &creds, state->query_count, 
                                 state->query_p, state->resp_p, NULL);

        if( ret != 0 )
        {
            printf("Error code return: %d\n", ret);
            return ret;
        }

        for( i = 0; i < state->meta_count; i++ )
        {
            printf("Meta (%d) returned %d records, token (%llu), "
                   "dirent count: %u\n", i, state->resp_p[i].query_p[0].count, 
                    llu(state->resp_p[i].query_p[0].token),
                    state->resp_p[i].dirent_count);

            for( j=0; j < state->resp_p[i].query_p[0].count; j++ )
            {

                printf("\tHandle: %llu\n", 
                    state->resp_p[i].query_p[0].match[j]);
            }

             /* have to prepend local mount point to path returned
             * from server */
            for( j = 0; j < state->resp_p[i].dirent_count; j++ )
            {
                lpath_len = strlen(state->resp_p[i].dirent_p[j].d_name) +
                         strlen(state->pvfs_root) + 1;
                if( ( local_path = realloc(local_path, lpath_len+1) ) == 0 )
                {   
                    printf("malloc error\n");
                    return -1;
                }

                memset(local_path, 0, lpath_len+1);
                strncpy(local_path, state->pvfs_root, strlen(state->pvfs_root));
                strncpy(local_path+strlen(state->pvfs_root ), 
                      state->resp_p[i].dirent_p[j].d_name, 
                      strlen(state->resp_p[i].dirent_p[j].d_name) );
           
                printf("\t%s (handle: %llu) (next token: %llu)\n",
                    local_path, llu(state->resp_p[i].dirent_p[j].handle),
                    llu(state->resp_p[i].query_p[0].token));
            }
            done += (state->resp_p[i].query_p[0].token == PVFS_ITERATE_END)?1:0;
        }
    }
    free(local_path);
    return 0;
}


/* cleanup */
static int pvfs2_cleanup(struct getvalue *state)
{
    int i=0, j=0;
    /* free, free, free */
    for( i=0; i < state->meta_count; i++ )
    {
        /*
        for( j=0; j < state->query_count; j++ )
        {
            if( state->resp_p[i].query_p[j].match )
                free(state->resp_p[i].query_p[j].match);
            if( state->resp_p[i].query_p[j].query.buffer )
                free(state->resp_p[i].query_p[j].query.buffer);
        }
        free(state->resp_p[i].query_p);
        free(state->resp_p[i].dirent_p);
        */
    }

    free(state->query_p);
    //free(state->resp_p);
    free(state->pvfs_root);
    free(state->key.buffer);
    free(state->val.buffer);
    free(state);
    return 0;
}

/* parse_args()
 *
 * parses command line arguments
 *
 * returns pointer to options structure on success, NULL on failure
 */
static struct getvalue* parse_args(int argc, char* argv[])
{
    char flags[] = "cpk:v:n:q:";
    int one_opt = 0;

    struct getvalue* tmp_state = NULL;

    if( argc < 9 )
    {
        usage(argc, argv);
    }
    /* create storage for the command line options */
    if( (tmp_state = calloc(1, sizeof(struct getvalue))) == 0)
    {
	return(NULL);
    }

    tmp_state->pvfs_root = strdup(argv[argc-1]);

    /* look at command line arguments */
    while((one_opt = getopt(argc, argv, flags)) != EOF)
    {
	switch(one_opt){
            case 'c':
                tmp_state->query_type |= PVFS_KEYVAL_QUERY_NORM;
                break;
            case 'k':
                tmp_state->key.buffer = strdup(optarg);
                tmp_state->key.buffer_sz = strlen(tmp_state->key.buffer) + 1;
                break;
            case 'v':
                tmp_state->val.buffer = strdup(optarg);
                tmp_state->val.buffer_sz = strlen(tmp_state->val.buffer) + 1;
                break;
            case 'n':
                tmp_state->count = atoi( optarg );
                break;
            case 'p':
                tmp_state->query_type |= PVFS_KEYVAL_RESULT_NO_PATHS;
                break;
            case 'q':
                if( strncmp( "LT", optarg, 2) == 0 )
                { tmp_state->query_type |= PVFS_KEYVAL_QUERY_LT; }
                else if( strncmp( "LE", optarg, 2) == 0 )
                { tmp_state->query_type |= PVFS_KEYVAL_QUERY_LE; }
                else if( strncmp( "EQ", optarg, 2) == 0 )
                { tmp_state->query_type |= PVFS_KEYVAL_QUERY_EQ; }
                else if( strncmp( "GE", optarg, 2) == 0 )
                { tmp_state->query_type |= PVFS_KEYVAL_QUERY_GE; }
                else if( strncmp( "GT", optarg, 2) == 0 )
                { tmp_state->query_type |= PVFS_KEYVAL_QUERY_GT; }
                else if( strncmp( "NT", optarg, 2) == 0 )
                { tmp_state->query_type |= PVFS_KEYVAL_QUERY_NT; }
                else if( strncmp( "PEQ", optarg, 3) == 0 )
                { tmp_state->query_type |= PVFS_KEYVAL_QUERY_PEQ; }
                else
                {
                    printf("?: %s\n", optarg);
		    usage(argc, argv);
		    exit(EXIT_FAILURE);
                }
                break;
	    case '?':
                printf("?: %d\n", one_opt);
		usage(argc, argv);
		exit(EXIT_FAILURE);
	}
    }
    
    if( tmp_state->key.buffer == NULL)
    {
      fprintf(stderr, "The key (attribute) to find must be provided");
      usage(argc, argv);
      exit(EXIT_FAILURE);
    }
    if( tmp_state->val.buffer == NULL)
    {
      fprintf(stderr, "The value to find must be provided");
      usage(argc, argv);
      exit(EXIT_FAILURE);
    }
    if( tmp_state->count < 1 )
    {
        fprintf(stderr, "Count must be 1 or greater");
        usage(argc, argv);
        exit(EXIT_FAILURE);
    }
    return(tmp_state);
}

static int setup_query_struct(PVFS_keyval_query *q, uint32_t c)
{

    q->token = PVFS_ITERATE_START;
    q->oper = 0;

    if( (q->query.buffer = calloc(1, VALBUFSZ)) == 0 )
    {
        PVFS_perror("PVFS_sys_getvalue", PVFS_ENOMEM);
        return -PVFS_ENOMEM;
    }
    q->query.buffer_sz = VALBUFSZ;

    q->count = c;
    if( (q > 0) && ((q->match = calloc(c, sizeof(PVFS_handle))) == 0) )
    {
        PVFS_perror("PVFS_sys_getvalue", PVFS_ENOMEM);
        return -PVFS_ENOMEM;
    }
    return 0;
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

