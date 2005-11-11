/*
 * Copyright Acxiom Corporation, 2005
 *
 * See COPYING in top-level directory.
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <getopt.h>

#include "pvfs2.h"
#include "str-utils.h"
#include "pint-sysint-utils.h"
#include "pvfs2-internal.h"

#ifndef PVFS2_VERSION
#define PVFS2_VERSION "Unknown"
#endif

/* optional parameters, filled in by parse_args() */
struct options
{
    char  * pszLinkTarget; /* Link Target */
    char  * pszLinkName;   /* Link Name */
    int     nVerbose; 
};

/* Function Prototypes */
static void usage(int argc, char** argv);
static int parse_args(int argc, char** argv, struct options * opts);
static void enable_verbose(struct options * opts);
static int make_link(PVFS_credentials     * pCredentials,
                     const PVFS_fs_id       fs_id,
                     const char           * pszLinkTarget,
                     const char           * pszPvfsPath,
                     const int              nVerbose);

int main(int argc, char **argv)
{
    int                 ret                       = -1;
    char                szPvfsPath[PVFS_NAME_MAX] = "";
    PVFS_fs_id          fs_id                     = 0;
    struct options      user_opts;
    PVFS_credentials    credentials;

    /* Initialize any memory */
    memset(&user_opts,   0, sizeof(user_opts));
    memset(&credentials, 0, sizeof(credentials));
    
    /* look at command line arguments */
    ret = parse_args(argc, argv, &user_opts);
    if(ret < 0)
    {
        fprintf(stderr, "Error: failed to parse command line arguments.\n");
        return (-1);
    }

    ret = PVFS_util_init_defaults();
    if (ret < 0)
    {
        PVFS_perror("PVFS_util_init_defaults", ret);
        return (-1);
    }

    /* Let's verify that all the given files reside on a PVFS2 filesytem */
    ret = PVFS_util_resolve(user_opts.pszLinkName, 
                            &fs_id, 
                            szPvfsPath, 
                            PVFS_NAME_MAX);

    if (ret < 0)
    {
        fprintf(stderr, "Error: could not find file system for %s\n",
                user_opts.pszLinkTarget);
        PVFS_sys_finalize();
        return(-1);
   }

    /* We will re-use the same credentials for each call */
    PVFS_util_gen_credentials(&credentials);

    ret = make_link(&credentials,
                    fs_id,
                    user_opts.pszLinkTarget,
                    szPvfsPath,
                    user_opts.nVerbose);
    if(ret != 0)
    {
        fprintf(stderr, "cannot create softlink [%s] to [%s]\n", 
                user_opts.pszLinkName, 
                user_opts.pszLinkTarget);
        return(-1);
    }

    /* TODO: need to free the request descriptions */
    PVFS_sys_finalize();

    return(0);
}

static int make_link(PVFS_credentials     * pCredentials,
                     const PVFS_fs_id       fs_id,
                     const char           * pszLinkTarget,
                     const char           * pszPvfsPath,
                     const int              nVerbose)
{
    int                  ret                        = 0;
    char                 szBaseName[PVFS_NAME_MAX]  = "";
    PVFS_sys_attr        attr;
    PVFS_sysresp_lookup  resp_lookup;
    PVFS_object_ref      parent_ref;
    PVFS_sysresp_symlink resp_sym;

    /* Initialize any variables */
    memset(&attr,        0, sizeof(attr));
    memset(&resp_lookup, 0, sizeof(resp_lookup));
    memset(&parent_ref,  0, sizeof(parent_ref));
    memset(&resp_sym,    0, sizeof(resp_sym));

    /* Set the attributes for the new directory */
    attr.owner = pCredentials->uid;
    attr.group = pCredentials->gid;
    attr.perms = 0777;              
    attr.atime = time(NULL);
    attr.mtime = attr.atime;
    attr.ctime = attr.atime;
    attr.mask = (PVFS_ATTR_SYS_ALL_SETABLE);

    /* We need to change the PINT_remove_base_dir to an API call (pvfs_util), 
     * and we need to change this to use it 
     */
    ret = PINT_remove_base_dir( (char *) pszPvfsPath, 
                                szBaseName, 
                                sizeof(szBaseName));
    
    if(ret != 0)
    {
        fprintf(stderr, "Cannot retrieve link name for creation on %s\n", pszPvfsPath);
        return(-1);
    }
       
    /* Make sure we don't try and create the root directory */
    if( strcmp(szBaseName, "/") == 0 )
    {
        fprintf(stderr, "directory exists\n");
        return(-1);
    }
    
    /* TODO: We need to change the PINT_lookup_parent to an API call (pvfs_util), 
     * and we need to change this to use it. Maybe send entire parent_refn so
     * that the fs_id is filled in also
     */
    ret = PINT_lookup_parent( (char *) pszPvfsPath, 
                              fs_id, 
                              pCredentials, 
                              &parent_ref.handle);

    if(ret < 0)
    {
        PVFS_perror("PVFS_util_lookup_parent", ret);
        return(-1);
    }
    
    parent_ref.fs_id = fs_id;

    if(nVerbose)
    {
        fprintf(stderr, "Creating Symlink\n");
        fprintf(stderr, "\t szBaseName  = [%s]\n", szBaseName);
        fprintf(stderr, "\t fs_id       = [%d]\n", fs_id);
        fprintf(stderr, "\t Target      = [%s]\n", pszLinkTarget);
        fprintf(stderr, "\t pvfs path   = [%s]\n", pszPvfsPath);

        fprintf(stdout, "Directory Attributes\n");
        fprintf(stdout, "\t owner [%d]\n",  attr.owner);
        fprintf(stdout, "\t group [%d]\n",  attr.group);
        fprintf(stdout, "\t perms [%o]\n",  attr.perms);
        fprintf(stdout, "\t atime [%llu]\n", llu(attr.atime));
        fprintf(stdout, "\t mtime [%llu]\n", llu(attr.mtime));
        fprintf(stdout, "\t ctime [%llu]\n", llu(attr.ctime));
    }

    ret = PVFS_sys_symlink(szBaseName, 
                           parent_ref, 
                           (char *) pszLinkTarget,
                           attr, 
                           pCredentials, 
                           &resp_sym);

    if (ret < 0)
    {
        PVFS_perror("PVFS_sys_symlink", ret);
        return(ret);
    }
    
    return(0);
}

/* parse_args()
 *
 * parses command line arguments
 *
 * returns pointer to options structure on success, NULL on failure
 */
static int parse_args(int argc, char** argv, struct options * opts)
{
    int    ret             = 0, 
           option_index    = 0,
           create_softlink = 0;
    char * cur_option      = NULL;
    char   flags[]         = "hvVs";  /* Options available on command line */

    static struct option long_opts[] =
    {
        {"help",0,0,0},
        {"version",0,0,0},
        {"verbose",0,0,0},
        {0,0,0,0}
    };

    while((ret = getopt_long_only(argc, argv, flags, long_opts, &option_index)) != -1)
    {
        switch (ret)
        {
            case 0:
                cur_option = (char*)long_opts[option_index].name;
   
                if(strcmp("help", cur_option) == 0)
                {
                    usage(argc, argv);
                    exit(0);
                }
                else if(strcmp("verbose", cur_option) == 0)
                {
                    enable_verbose(opts);
                }
                else if (strcmp("version", cur_option) == 0)
                {
                    printf("%s\n", PVFS2_VERSION);
                    exit(0);
                }
                else
                {
                    usage(argc, argv);
                    return -1;
                }
                break;

            case 'h': /* --help */ 
                usage(argc, argv);
                exit(0);
                break;

            case 's': /* --help */ 
                create_softlink = 1;
                break;

            case 'V': /* --verbose */ 
                enable_verbose(opts);
                break;
                   
            case 'v': /* --version */
                printf("%s\n", PVFS2_VERSION);
                exit(0);
                break;

            case '?': 
                usage(argc, argv);
                return -1;
            
            default:
                usage(argc, argv);
                return -1;
        }
    }

    if(!create_softlink)
    {
        fprintf(stderr, "Hard links not supported. Use -s option for softlinks\n");
        usage(argc, argv);
        exit(1);
    }

    /* Validation to make sure we have at least one directory to check */
    if(argc - optind != 2)
    {
       usage(argc, argv);
       return(-1);
    }

    /* We processed all options arguments, so let's get the link target & name */
    opts->pszLinkTarget = argv[optind];
    opts->pszLinkName   = argv[optind+1];

    if(opts->nVerbose)
    {
        fprintf(stdout, "Options Specified\n");
        fprintf(stdout, "\t Verbose     [%d]\n", opts->nVerbose);
        fprintf(stdout, "\t Link Target [%s]\n", opts->pszLinkTarget);
        fprintf(stdout, "\t Link Name   [%s]\n", opts->pszLinkName);
    }

    return(0);
}

static void usage(int argc, char **argv)
{
    fprintf(stderr, "Usage: %s [OPTION] TARGET LINK_NAME\n", argv[0]);
    fprintf(stderr, "Create a link to the specified TARGET with LINK_NAME.\n\n");

    fprintf(stderr,"  -s,                 create softlink\n");
    fprintf(stderr,"  -V, --verbose       turns on verbose messages\n");
    fprintf(stderr,"  -v, --version       output version information and exit\n");
    fprintf(stderr,"  -h, --help          print help\n");
    return;
}

static void enable_verbose(struct options * opts)
{
   opts->nVerbose = 1;  
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
