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
#include "pvfs2-internal.h"
#include "pvfs2-usrint.h"

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
    int     init_num_dirdata; /* init num of dirdata handles */
    int     max_num_dirdata; /* max num of dirdata handles */
    int     split_size;
    int     verbose; 
    int     make_parent_dirs; /* Create missing parents */
};

/* Function Prototypes */
static void usage(int argc, char** argv);
static int parse_args(int argc, char** argv, struct options * opts);
static void enable_verbose(struct options * opts);
static void enable_parents(struct options * opts);
static int read_mode(struct options * opts, const char * buffer);
static int read_init_num_dirdata(struct options * opts, const char * buffer);
static int read_max_num_dirdata(struct options * opts, const char * buffer);
static int read_split_size(struct options * opts, const char * buffer);
static int make_directory(const int              mode,
                          const char           * dir,
                          const int              make_parent_dirs,
                          const int              verbose);

int main(int argc, char **argv)
{
    int ret = -1, status = 0; /* Get's set if error */
    int i = 0;
    struct options   user_opts;

    /* Initialize any memory */
    memset(&user_opts,   0, sizeof(user_opts));
    
    /* look at command line arguments */
    ret = parse_args(argc, argv, &user_opts);
    if(ret < 0)
    {
        fprintf(stderr, "Error: failed to parse command line arguments.\n");
        return (-1);
    }

    for(i = 0; i < user_opts.numdirs; i++)
    {
        ret = make_directory(user_opts.mode,
                             user_opts.dir_array[i],
                             user_opts.make_parent_dirs,
                             user_opts.verbose);
        if(ret != 0)
        {
            fprintf(stderr, "cannot create [%s]\n", user_opts.dir_array[i]);
            perror("mkdir failed");
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
     
    return(status);
}

static int make_directory(const int              mode,
                          const char           * dir,
                          const int              make_parent_dirs,
                          const int              verbose)
{
    int ret = 0;
    char parent_dir[PVFS_PATH_MAX] = "";
    char base[PVFS_PATH_MAX]  = "";
    char * parentdir_ptr = NULL;
    char * basename_ptr = NULL;
    struct stat stat_buf;

    memset(&stat_buf, 0, sizeof(struct stat));

    /* Copy the file name into structures to be passed to dirname and basename
    * These calls change the parameter, so we don't want to mess with original
    * TODO: We need to change the PINT_lookup_parent to a API call, and we  
    * need to change this to use it 
    */
    strcpy(parent_dir, dir);
    strcpy(base,  dir);
    
    parentdir_ptr = dirname(parent_dir);
    basename_ptr  = basename(base);
        
    /* Make sure we don't try and create the root directory */
    if( strcmp(basename_ptr, "/") == 0 )
    {
        fprintf(stderr, "directory exists\n");
        return(-1);
    }
    pvfs_stat(parent_dir, &stat_buf);
    if (make_parent_dirs && !S_ISDIR(stat_buf.st_mode))
    {
        ret = make_directory((mode_t) mode, parent_dir, make_parent_dirs, verbose);
    }
    
    

    if(verbose)
    {
        fprintf(stderr, "Attempting to create Directory\n");
        fprintf(stderr, "\t basename_ptr    = [%s]\n", basename_ptr);
        fprintf(stderr, "\t Mode            = [%o]\n", mode);
        fprintf(stderr, "\t DirName         = [%s]\n", dir);

        fprintf(stdout, "Directory Attributes\n");
        fprintf(stdout, "\t perms [%o]\n",  mode);
    }
    
    ret = pvfs_mkdir(dir, (mode_t) mode);
    /*if (ret != 0 && errno == ENOENT && make_parent_dirs)
    {
        ret = make_directory((mode_t) mode, parent_dir, make_parent_dirs, verbose);
    }*/
    if (verbose && ret == 0)
    {
       fprintf(stdout, "Created directory [%s]\n", dir); 
    }
    return ret;
    /* The parent directory did not exist. Let's create the parent directory */
    /*if(ret == -PVFS_ENOENT &&
       make_parent_dirs)
    {
        strcpy(parent_dir, pvfs_path);
        strcpy(realpath,  dir);

        ret = make_directory(credentials,
                             fs_id,
                             mode,
                             init_num_dirdata,
                             max_num_dirdata,
                             split_size,
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
                                  PVFS2_LOOKUP_LINK_FOLLOW, NULL);
        
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
    
    

    ret = PVFS_sys_mkdir(basename_ptr, 
                         parent_ref, 
                         attr,
                         credentials, 
                         &resp_mkdir, NULL);

    if (ret < 0)
    {
        PVFS_perror("PVFS_sys_mkdir", ret);
        return(ret);
    }
    
    return(0);*/
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
    int init_num_dirdata_requested = 0;
    int max_num_dirdata_requested = 0;
    int split_size_requested = 0;
    const char * cur_option = NULL;
    char flags[] = "hm:i:x:s:pvV";  /* Options available on command line */

    static struct option long_opts[] =
    {
        {"help",0,0,0},
        {"version",0,0,0},
        {"verbose",0,0,0},
        {"parents",0,0,0},
        {"split-size",1,0,0},
        {"max-num-dirdata",1,0,0},
        {"init-num-dirdata",1,0,0},
        {"mode",1,0,0},
        {0,0,0,0}
    };

    while((ret = getopt_long_only(argc, argv, flags, long_opts, &option_index)) != -1)
    {
        switch (ret)
        {
            case 0:
                cur_option = long_opts[option_index].name;
   
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
                else if(strcmp("init-num-dirdata", cur_option) == 0)
                {
                    ret = read_init_num_dirdata(opts, optarg);
                    if(ret == 0)
                    {
                        fprintf(stderr, "Unable to read initial number of dirdata handles\n");
                        usage(argc, argv);
                        return(-1);
                    }
                    init_num_dirdata_requested = ret; 
                }
                else if(strcmp("max-num-dirdata", cur_option) == 0)
                {
                    ret = read_max_num_dirdata(opts, optarg);
                    if(ret == 0)
                    {
                        fprintf(stderr, "Unable to read max number of dirdata handles\n");
                        usage(argc, argv);
                        return(-1);
                    }
                    max_num_dirdata_requested = ret; 
                }
                else if(strcmp("split-size", cur_option) == 0)
                {
                    ret = read_split_size(opts, optarg);
                    if(ret == 0)
                    {
                        fprintf(stderr, "Unable to read split size\n");
                        usage(argc, argv);
                        return(-1);
                    }
                    split_size_requested = ret; 
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

            case 'i': /* --init-num-dirdata */
                ret = read_init_num_dirdata(opts, optarg);
                if(ret == 0)
                {
                    fprintf(stderr, "Unable to read initial number of dirdata handles\n");
                    usage(argc, argv);
                    return(-1);
                }
                init_num_dirdata_requested = ret;
                break;

            case 'p': /* --parents */ 
                enable_parents(opts);
                break;

            case 's': /* --split-size */
                ret = read_split_size(opts, optarg);
                if(ret == 0)
                {
                    fprintf(stderr, "Unable to read split size\n");
                    usage(argc, argv);
                    return(-1);
                }
                split_size_requested = ret;
                break;

            case 'V': /* --verbose */ 
                enable_verbose(opts);
                break;
                   
            case 'v': /* --version */
                printf("%s\n", PVFS2_VERSION);
                exit(0);
                break;

            case 'x': /* --max-num-dirdata */
                ret = read_max_num_dirdata(opts, optarg);
                if(ret == 0)
                {
                    fprintf(stderr, "Unable to read max number of dirdata handles\n");
                    usage(argc, argv);
                    return(-1);
                }
                max_num_dirdata_requested = ret;
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
        opts->mode = PVFS_util_translate_mode(mode & ~PVFS_util_get_umask(), 0);
    }
    
    if(!init_num_dirdata_requested)
    {           
        opts->init_num_dirdata = 0;
    }
    if(!max_num_dirdata_requested)
    {           
        opts->max_num_dirdata = 0;
    }           
    if(!split_size_requested)
    {           
        opts->split_size = 0;
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
        fprintf(stdout, "\t Verbose             [%d]\n", opts->verbose);
        fprintf(stdout, "\t Mode                [%o]\n", opts->mode);
        fprintf(stdout, "\t Parents             [%d]\n", opts->make_parent_dirs);
        fprintf(stdout, "\t Init Num Dirdata    [%d]\n", opts->init_num_dirdata);
        fprintf(stdout, "\t Max Num Dirdata     [%d]\n", opts->max_num_dirdata);
        fprintf(stdout, "\t Split Size          [%d]\n", opts->split_size);
        fprintf(stdout, "\t Num Dirs            [%d]\n", opts->numdirs);
        for(i=0; i<opts->numdirs; i++)
        {
            fprintf(stdout, "\t Directory #%d = [%s]\n", i, opts->dir_array[i]);
        }   
    }
    return(0);
}

static void usage(int argc, char **argv)
{
    fprintf(stderr, "Usage: %s [OPTION] DIRECTORY\n", argv[0]);
    fprintf(stderr, "Create the DIRECTORY(ies), if they do not already exist.\n\n");

    fprintf(stderr,"  -m, --mode                set permission mode (as in chmod), "
                                                "not rwxrwxrwx - umask\n");
    fprintf(stderr,"  -i, --init-num-dirdata    set initial number of dirdata handles for the directory,\n");
    fprintf(stderr,"  -x, --max-num-dirdata     set maximum number of dirdata handles for the directory,\n");
    fprintf(stderr,"  -s, --split-size          set number of directory entries stored before split,\n");
    fprintf(stderr,"  -p, --parents             make parent directories as needed\n");
    fprintf(stderr,"  -V, --verbose             turns on verbose messages\n");
    fprintf(stderr,"  -v, --version             output version information and exit\n");
    fprintf(stderr,"  -h, --help                print help\n");
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

static int read_init_num_dirdata(struct options * opts, const char * buffer)
{
    int ret = 0;

    ret = sscanf(buffer, "%d", &opts->init_num_dirdata);

    return(ret);
}

static int read_max_num_dirdata(struct options * opts, const char * buffer)
{
    int ret = 0;

    ret = sscanf(buffer, "%d", &opts->max_num_dirdata);

    return(ret);
}

static int read_split_size(struct options * opts, const char * buffer)
{
    int ret = 0;

    ret = sscanf(buffer, "%d", &opts->split_size);

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
