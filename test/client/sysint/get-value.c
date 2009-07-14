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

#define __PINT_REQPROTO_ENCODE_FUNCS_C
#include "pvfs2.h"
#include "str-utils.h"
#include "pint-sysint-utils.h"
#include "pint-util.h"
#include "pvfs2-internal.h"
#include "pvfs2-req-proto.h"
#include "client-state-machine.h"
#include "server-config-mgr.h"

#define KEYBUFSZ 1024
#define VALBUFSZ 4096

struct opts
{
    PVFS_ds_keyval key;
    PVFS_ds_keyval val;
    char *pvfs_root;
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
    if(!user_opts)
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

    if (ret != 0) 
        return ret;
    
    PVFS_sys_finalize();
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
    int ret = -1, lpath_len = 0;
    char *local_path = NULL;
    struct PINT_client_getvalue_sm resp;
    struct dbpf_keyval_db_entry *key_entry;
    PVFS_ds_position token = PVFS_ITERATE_START;
    PVFS_sysresp_getvalue resp_getvalue;
    PVFS_dirent d;

    memset(&resp, 0, sizeof(struct PINT_client_getvalue_sm));
    memset(&resp_getvalue, 0, sizeof(struct PINT_client_getvalue_sm));
    memset(&d, 0, sizeof(PVFS_dirent));

    resp_getvalue.key = malloc( sizeof(PVFS_ds_keyval));
    resp_getvalue.val = malloc( sizeof(PVFS_ds_keyval));

    resp_getvalue.key->buffer = malloc( KEYBUFSZ + 1);
    resp_getvalue.val->buffer = malloc( VALBUFSZ + 1);

    resp_getvalue.key->buffer_sz = KEYBUFSZ + 1;
    resp_getvalue.val->buffer_sz = VALBUFSZ + 1;

    printf("\nMatching files:\n");
    while( token != PVFS_ITERATE_END )
    {
        ret = PVFS_sys_getvalue(obj, token, creds,  &d, &(opt->key), 
                                &(opt->val), &resp_getvalue, NULL);
        if (ret < 0)
        {
            PVFS_perror("PVFS_sys_getvalue", ret);
            return -1;
        }
        else
        {
            token = resp_getvalue.token;

            /* have to prepend local mount point to path returned
             * from server */
            lpath_len = strlen(resp_getvalue.dirent.d_name) +
                        strlen(opt->pvfs_root) + 1;
            local_path = malloc(lpath_len);
            memset(local_path, 0, lpath_len);
            strncpy( local_path, opt->pvfs_root, strlen(opt->pvfs_root ));
            strncpy( local_path+strlen(opt->pvfs_root ), 
                     resp_getvalue.dirent.d_name, 
                     strlen(resp_getvalue.dirent.d_name) );
               
            key_entry = resp_getvalue.key->buffer;
            printf("\t%s (handle: %llu) (next token: %llu) (%s->%s) \n",
                   local_path, llu(key_entry->handle), 
                   resp_getvalue.token, key_entry->key, 
                   (char *)opt->val.buffer);
        }
    }
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
    char flags[] = "k:v:";
    int one_opt = 0;

    struct opts* tmp_opts = NULL;

    if( argc < 5 )
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
            case 'k':
                tmp_opts->key.buffer = strdup(optarg);
                tmp_opts->key.buffer_sz = strlen(tmp_opts->key.buffer) + 1;
                break;
            case 'v':
                tmp_opts->val.buffer = strdup(optarg);
                tmp_opts->val.buffer_sz = strlen(tmp_opts->val.buffer) + 1;
                break;
	    case('?'):
                printf("?\n");
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
    return(tmp_opts);
}


static void usage(int argc, char** argv)
{
    fprintf(stderr,"Usage: %s -k <key> -v <val> <pvfs fs root>\n",argv[0]);
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

