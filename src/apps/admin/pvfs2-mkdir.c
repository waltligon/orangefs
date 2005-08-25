/*
 * (C) 2004 Clemson University and The University of Chicago
 * 
 * Changes by Acxiom Corporation to add support for mode, multiple directory
 * creation, and parent creation.
 * Copyright Acxiom Corporation, 2005.
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
#include <libgen.h>

#include "pvfs2.h"
#include "str-utils.h"
#include "pint-sysint-utils.h"

#ifndef PVFS2_VERSION
#define PVFS2_VERSION "Unknown"
#endif

/* We need to set some limit, I suppose */
#define MAX_NUM_DIRECTORIES 100 

/* optional parameters, filled in by parse_args() */
struct options
{
    char ** dir_array;  /* array of directories to create */
    int     numdirs; /* number of directories to create */
    int     mode;    /* mode of directories */
    int     verbose; 
    int     make_parent_dirs; /* Create missing parents */
};

/* Function Prototypes */
static void usage(int argc, char** argv);
static int parse_args(int argc, char** argv, struct options * opts);
static void enable_verbose(struct options * opts);
static void enable_parents(struct options * opts);
static int read_mode(struct options * opts, const char * buffer);
static int make_directory(PVFS_credentials     * credentials,
                          const PVFS_fs_id       fs_id,
                          const int              mode,
                          const char           * dir,
                          const char           * pvfs_path,
                          const int              make_parent_dirs,
                          const int              verbose);

int main(int argc, char **argv)
{
    int ret = -1, status = 0; /* Get's set if error */
    int i = 0;
    char           **pvfs_path = NULL;
    PVFS_fs_id      *pfs_id = NULL;
    struct options   user_opts;
    PVFS_credentials credentials;

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

    /* Allocate space to hold the relative pvfs2 path & fs_id for each 
     * requested file 
     */
    pvfs_path = (char **)calloc(user_opts.numdirs, sizeof(char *));

    if(pvfs_path == NULL)
    {
        fprintf(stderr, "Unable to allocate memory\n");
        return(-1);
    }

    for(i = 0; i < user_opts.numdirs; i++)
    {
        pvfs_path[i] = (char *)calloc(PVFS_NAME_MAX, sizeof(char));
        if(pvfs_path[i] == NULL)
        {
            fprintf(stderr, "Unable to allocate memory\n");
            return(-1);
        }
    }

    /* Allocate enough space to hold file system id for each directory */
    pfs_id = (PVFS_fs_id *)calloc(user_opts.numdirs, sizeof(PVFS_fs_id));

    if(pfs_id == NULL)
    {
        fprintf(stderr, "Unable to allocate memory\n");
        return(-1);
    }
 
    ret = PVFS_util_init_defaults();
    if (ret < 0)
    {
        PVFS_perror("PVFS_util_init_defaults", ret);
        return (-1);
    }

    /* Let's verify that all the given files reside on a PVFS2 filesytem */
    for(i = 0; i < user_opts.numdirs; i++)
    {
        ret = PVFS_util_resolve(user_opts.dir_array[i], 
                                &pfs_id[i], 
                                pvfs_path[i], 
                                PVFS_NAME_MAX);
 
        if (ret < 0)
        {
            fprintf(stderr, "Error: could not find file system for %s\n",
                    user_opts.dir_array[i]);
            PVFS_sys_finalize();
            return(-1);
       }
    }

    /* We will re-use the same credentials for each call */
    PVFS_util_gen_credentials(&credentials);

    for(i = 0; i < user_opts.numdirs; i++)
    {
        ret = make_directory(&credentials,
                             pfs_id[i],
                             user_opts.mode,
                             user_opts.dir_array[i],
                             pvfs_path[i],
                             user_opts.make_parent_dirs,
                             user_opts.verbose);
        if(ret != 0)
        {
            fprintf(stderr, "cannot create [%s]\n", user_opts.dir_array[i]);
            status = -1;
        }
    }

    /* TODO: need to free the request descriptions */
    PVFS_sys_finalize();

    /* Deallocate any allocated memory */
    if(user_opts.dir_array != NULL)
    {
        free(user_opts.dir_array);
    }
    
    
    if(pvfs_path != NULL)
    {
        for(i=0;i<user_opts.numdirs;i++)
        {
            if(pvfs_path[i] != NULL)
            {
                free(pvfs_path[i]);
            }
        }
    
        free(pvfs_path);
    }
    
    if(pfs_id != NULL)
    {
        free(pfs_id);
    }
    
    return(status);
}

static int make_directory(PVFS_credentials     * credentials,
                          const PVFS_fs_id       fs_id,
                          const int              mode,
                          const char           * dir,
                          const char           * pvfs_path,
                          const int              make_parent_dirs,
                          const int              verbose)
{
    int ret = 0;
    char parent_dir[PVFS_NAME_MAX] = "";
    char base[PVFS_NAME_MAX]  = "";
    char realpath[PVFS_NAME_MAX]  = "";
    char * parentdir_ptr = NULL;
    char * basename_ptr = NULL;
    PVFS_sys_attr       attr;
    PVFS_sysresp_lookup resp_lookup;
    PVFS_object_ref     parent_ref;
    PVFS_sysresp_mkdir  resp_mkdir;

    /* Initialize any variables */
    memset(&attr,        0, sizeof(attr));
    memset(&resp_lookup, 0, sizeof(resp_lookup));
    memset(&parent_ref,  0, sizeof(parent_ref));
    memset(&resp_mkdir,  0, sizeof(resp_mkdir));

    /* Copy the file name into structures to be passed to dirname and basename
    * These calls change the parameter, so we don't want to mess with original
    * TODO: We need to change the PINT_lookup_parent to a API call, and we  
    * need to change this to use it 
    */
    strcpy(parent_dir, pvfs_path);
    strcpy(base,  pvfs_path);
    
    parentdir_ptr = dirname(parent_dir);
    basename_ptr  = basename(base);
        
    /* Make sure we don't try and create the root directory */
    if( strcmp(basename_ptr, "/") == 0 )
    {
        fprintf(stderr, "directory exists\n");
        return(-1);
    }
    
    /* Set the attributes for the new directory */
    attr.owner = credentials->uid;
    attr.group = credentials->gid;
    attr.perms = mode;
    attr.atime = time(NULL);
    attr.mtime = attr.atime;
    attr.ctime = attr.atime;
    attr.mask = (PVFS_ATTR_SYS_ALL_SETABLE);
        
    /* Clear out any info from previous calls */
    memset(&resp_lookup,  0, sizeof(resp_lookup));

    ret = PVFS_sys_lookup(fs_id, 
                          parentdir_ptr, 
                          credentials, 
                          &resp_lookup, 
                          PVFS2_LOOKUP_LINK_FOLLOW);

    if( ret < 0 &&
        !make_parent_dirs)
    {
        PVFS_perror("PVFS_sys_lookup", ret);
        return(ret);
    }

    if( ret < 0         && 
        make_parent_dirs && 
        ret != -PVFS_ENOENT)
    {
        PVFS_perror("PVFS_sys_lookup", ret);
        return(ret);
    }
    
    /* The parent directory did not exist. Let's create the parent directory */
    if(ret == -PVFS_ENOENT &&
       make_parent_dirs)
    {
        strcpy(parent_dir, pvfs_path);
        strcpy(realpath,  dir);

        ret = make_directory(credentials,
                             fs_id,
                             mode,
                             dirname(realpath),
                             dirname(parent_dir),
                             make_parent_dirs,
                             verbose);
                             
        if(ret == 0)
        {
            ret = PVFS_sys_lookup(fs_id, 
                                  parentdir_ptr, 
                                  credentials, 
                                  &resp_lookup, 
                                  PVFS2_LOOKUP_LINK_FOLLOW);
        
            if(ret < 0)
            {
                PVFS_perror("PVFS_sys_lookup", ret);
                return(ret);
            }
        }
        else
        {
            return(ret);
        }
    }
    
    parent_ref.handle = resp_lookup.ref.handle;
    parent_ref.fs_id  = resp_lookup.ref.fs_id;

    /* Clear out any info from previous calls */
    memset(&resp_mkdir, 0, sizeof(PVFS_sysresp_mkdir));

    if(verbose)
    {
        fprintf(stderr, "Creating Directory\n");
        fprintf(stderr, "\t basename_ptr = [%s]\n", basename_ptr);
        fprintf(stderr, "\t fs_id       = [%d]\n", fs_id);
        fprintf(stderr, "\t Mode        = [%o]\n", mode);
        fprintf(stderr, "\t DirName     = [%s]\n", dir);
        fprintf(stderr, "\t pvfs path   = [%s]\n", pvfs_path);

        fprintf(stdout, "Directory Attributes\n");
        fprintf(stdout, "\t owner [%d]\n",  attr.owner);
        fprintf(stdout, "\t group [%d]\n",  attr.group);
        fprintf(stdout, "\t perms [%o]\n",  attr.perms);
        fprintf(stdout, "\t atime [%Lu]\n", Lu(attr.atime));
        fprintf(stdout, "\t mtime [%Lu]\n", Lu(attr.mtime));
        fprintf(stdout, "\t ctime [%Lu]\n", Lu(attr.ctime));
    }

    ret = PVFS_sys_mkdir(basename_ptr, 
                         parent_ref, 
                         attr,
                         credentials, 
                         &resp_mkdir);

    if (ret < 0)
    {
        PVFS_perror("PVFS_sys_mkdir", ret);
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
    int i = 0, ret = 0,option_index = 0, mode_requested = 0;
    char * cur_option = NULL;
    char flags[] = "hm:pvV";  /* Options available on command line */

    static struct option long_opts[] =
    {
        {"help",0,0,0},
        {"version",0,0,0},
        {"verbose",0,0,0},
        {"mode",1,0,0},
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
                else if(strcmp("parents", cur_option) == 0)
                {
                    enable_parents(opts);
                }
                else if (strcmp("version", cur_option) == 0)
                {
                    printf("%s\n", PVFS2_VERSION);
                    exit(0);
                }
                else if(strcmp("mode", cur_option) == 0)
                {
                    ret = read_mode(opts, optarg);
                    if(ret == 0)
                    {
                        fprintf(stderr, "Unable to read mode argument\n");
                        usage(argc, argv);
                        return(-1);
                    }
                    mode_requested = 1;
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

            case 'm': /* --mode */
                ret = read_mode(opts, optarg);
                if(ret == 0)
                {
                    fprintf(stderr, "Unable to read mode argument\n");
                    usage(argc, argv);
                    return(-1);
                }
                mode_requested = 1;
                break;

            case 'p': /* --parents */ 
                enable_parents(opts);
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

    /* We processed all arguments, so let's figure out how many directories the 
     * user wants to create, and allocate enough space to hold them, barring 
     * they haven't exceeded the limit. 
     */
    opts->numdirs = argc - optind;

    /* Validation to make sure we have at least one directory to check */
    if(opts->numdirs <= 0)
    {
       fprintf(stderr, "No directories specified(s)\n");
       usage(argc, argv);
       return(-1);
    }
 
    /* Validation to make sure we haven't exceeded */
    if(opts->numdirs > MAX_NUM_DIRECTORIES)
    {
       fprintf(stderr, "Directory limit of [%d] exceeded. [%d] directories entered\n", 
               MAX_NUM_DIRECTORIES,
               opts->numdirs);
       usage(argc, argv);
       return(-1);
    }

    /* Assign a default mode if one is not given */ 
    if(!mode_requested)
    {
        mode_t mode = S_IRWXO | S_IRWXG | S_IRWXU; /* 0777 */
        opts->mode = PVFS2_translate_mode(mode & ~PVFS_util_get_umask());
    }
    
    /* Allocate memory to hold the filenames */
    opts->dir_array = (char **)calloc(opts->numdirs, sizeof(char *));
 
    if(opts->dir_array == NULL)
    {
       fprintf(stderr, "Memory allocation failed\n");
       return(-1);
    }

    /* Loop through arguments and capture the directory names */
    for(i = optind; i < argc; i++)
    {
       opts->dir_array[i-optind] = argv[i];
    }

    if(opts->verbose)
    {
        fprintf(stdout, "Options Specified\n");
        fprintf(stdout, "\t Verbose  [%d]\n", opts->verbose);
        fprintf(stdout, "\t Mode     [%o]\n", opts->mode);
        fprintf(stdout, "\t Num Dirs [%d]\n", opts->numdirs);
        for(i=0; i<opts->numdirs; i++)
        {
            fprintf(stdout, "\t Direcotory #%d = [%s]\n", i, opts->dir_array[i]);
        }   
    }
    return(0);
}

static void usage(int argc, char **argv)
{
    fprintf(stderr, "Usage: %s [OPTION] DIRECTORY\n", argv[0]);
    fprintf(stderr, "Create the DIRECTORY(ies), if they do not already exist.\n\n");

    fprintf(stderr,"  -m, --mode          set permission mode (as in chmod), "
                                          "not rwxrwxrwx - umask\n");
    fprintf(stderr,"  -p, --parents       make parent directories as needed\n");
    fprintf(stderr,"  -V, --verbose       turns on verbose messages\n");
    fprintf(stderr,"  -v, --version       output version information and exit\n");
    fprintf(stderr,"  -h, --help          print help\n");
    return;
}

static void enable_verbose(struct options * opts)
{
   opts->verbose = 1;  
}

static void enable_parents(struct options * opts)
{
   opts->make_parent_dirs = 1;  
}

static int read_mode(struct options * opts, const char * buffer)
{
    int ret = 0;
    
    ret = sscanf(buffer, "%o", &opts->mode);
    
    return(ret);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
