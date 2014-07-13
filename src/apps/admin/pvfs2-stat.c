/*
 * Copyright Acxiom Corporation, 2005
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <pwd.h>
#include <grp.h>
#include <assert.h>
#include <getopt.h>

#include "pvfs2.h"
#include "pvfs2-internal.h"
#include "pvfs2-usrint.h"

/* We need to set some limit, I suppose */
#define MAX_NUM_FILES 100 

#ifndef PVFS2_VERSION
#define PVFS2_VERSION "Unknown"
#endif

/* parameters, filled in by parse_args() */
struct options
{
    int     nVerbose;
    int     nFollowLink;
    char ** pszFiles;
    int     nNumFiles;
};

/* Function Prototypes */
static void usage(int argc, char** argv);
static int parse_args(int argc, char** argv, struct options * opts);
static void enable_verbose(struct options * opts);
static void enable_dereference(struct options * opts);
static int do_stat(const char             * pszFile,
                   const struct options   * opts);
void print_stats(const char  *pszName,
                 struct stat *stat_buf);

int main(int argc, char **argv)
{
   int               ret          = -1,
                     ret_agg      =  0,
                     i            =  0;
   struct options    user_opts;

   /* Initialize any memory */
   memset(&user_opts,   0, sizeof(user_opts));
   
   ret = parse_args(argc, argv, &user_opts);
   if(ret < 0)
   {
      fprintf(stderr, "Error: failed to parse command line arguments.\n");
      usage(argc, argv);
      return(-1);
   }

   if(user_opts.nVerbose)
   {
      fprintf(stdout, "Starting pvfs2-stat\n");
   }
   
   ret = PVFS_util_init_defaults();
   if(ret < 0)
   {
      PVFS_perror("PVFS_util_init_defaults", ret);
      return(-1);
   }
   
   for(i = 0; i < user_opts.nNumFiles; i++)
   {
      ret = do_stat(user_opts.pszFiles[i], 
                    &user_opts);
      if(ret != 0)
      {
         fprintf(stderr, "Error stating [%s]\n", user_opts.pszFiles[i]);
      }
      ret_agg |= ret;
   }

   PVFS_sys_finalize();

   /* Deallocate any allocated memory */
   if(user_opts.pszFiles != NULL)
   {
      free(user_opts.pszFiles);
   }
   
   return(ret_agg);
}

static int do_stat(const char             * pszFile,
                   const struct options   * opts)
{
    int                  ret = 0;
    struct stat stat_buf;
   /* Do we want to follow if the file is a symbolic link */
    //TODO: Is this usage of stat correct?
   if(opts->nFollowLink)
   {
        ret = pvfs_stat(pszFile, &stat_buf);
   }
   else
   {
        ret = pvfs_lstat(pszFile, &stat_buf);
   }
   
   if(ret < 0)
   {                          
      perror("pvfs_stat");
      return -1;
   }

   /* Display the attributes for the file */
   print_stats(pszFile, 
               &stat_buf);
   
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
    int    i            = 0, 
           ret          = 0, 
           option_index = 0;
    const char * cur_option   = NULL;

    static struct option long_opts[] =
    {
        {"help",0,0,0},
        {"version",0,0,0},
        {"verbose",0,0,0},
        {"dereference",0,0,0},
        {0,0,0,0}
    };

   while((ret = getopt_long_only(argc, argv, "VL", long_opts, &option_index)) != -1)
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
               else if(strcmp("dereference", cur_option) == 0)
               {
                  enable_dereference(opts);
               }
               else if (strcmp("version", cur_option) == 0)
               {
                  printf("%s\n", PVFS2_VERSION);
                  exit(0);
               }
               else
               {
                  usage(argc, argv);
                  exit(0);
               }
               break;

         case 'V': /* --verbose     */ 
                  enable_verbose(opts);
                  break;
                   
         case 'L': /* --dereference */
                  enable_dereference(opts);
                  break;
         
         case '?': 
                  usage(argc, argv);
                  exit(0);

         default:
                  usage(argc, argv);
                  exit(0);
      }
   }

   /* We processed all arguments, so let's figure out how many files the user
    * wants to stat, and allocate enough space to hold them, barring they haven't
    * exceeded the limit. 
    */
   opts->nNumFiles = argc - optind;
   
   /* Validation to make sure we have at least one file to check */
   if(opts->nNumFiles <= 0)
   {
      fprintf(stderr, "No filename(s)\n");
      usage(argc, argv);
      exit(0);
   }

   /* Validation to make sure we haven't exceeded */
   if(opts->nNumFiles > MAX_NUM_FILES)
   {
      fprintf(stderr, "Filename limit of [%d] exceeded. [%d] file entered\n", 
              MAX_NUM_FILES,
              opts->nNumFiles);
      usage(argc, argv);
      exit(0);
   }
   
   /* Allocate memory to hold the filenames */
   opts->pszFiles = (char **)calloc(opts->nNumFiles, sizeof(char *));
   
   if(opts->pszFiles == NULL)
   {
      fprintf(stderr, "Memory allocation failed\n");
      exit(0);
   }

   /* Loop through arguments and capture the file names */
   for(i = optind; i < argc; i++)
   {
      opts->pszFiles[i-optind] = argv[i];
   }
   
   return(0);
}

static void enable_verbose(struct options * opts)
{
   opts->nVerbose = 1;  
}

static void enable_dereference(struct options * opts)
{
   opts->nFollowLink = 1;  
}

void print_stats(const char  * pszName,
                 struct stat *stat_buf)
{
    char a_time[100] = "", 
         m_time[100] = "",  
         c_time[100] = "";
    struct passwd * user;
    struct group  * group;

    fprintf(stdout, "-------------------------------------------------------\n");
    fprintf(stdout, "  File Name     : %s\n",  pszName);
    
    fprintf(stdout, "  Permissions   : %04o\n", stat_buf->st_mode & 07777);

    /* Print the type of object */
    if(S_ISREG(stat_buf->st_mode))
    {
        fprintf(stdout, "  Type          : Regular File\n");
    }
    else if(S_ISDIR(stat_buf->st_mode))
    {
        fprintf(stdout, "  Type          : Directory\n");
    }
    else if(S_ISLNK(stat_buf->st_mode))
    {
        fprintf(stdout, "  Type          : Symbolic Link\n");
        /* TODO: How to get link target from stat*/
        /*
        if(attr->mask &  PVFS_ATTR_SYS_LNK_TARGET)
        {
            fprintf(stdout, "  Link Target   : %s\n", attr->link_target);
        }*/
    }
    
    /* Print size for non-block and character devices */
    if(!S_ISCHR(stat_buf->st_mode) && !S_ISBLK(stat_buf->st_mode))
    {
        /* If the size of a directory object is zero, let's default the size to 
         * 4096. This is what the kernel module does, and is the default directory
         * size on an EXT3 system
         */
        if( (stat_buf->st_size == 0) && 
            S_ISDIR(stat_buf->st_mode))
        {
            fprintf(stdout, "  Size          : 4096\n");
        }
        else
        {
            fprintf(stdout, "  Size          : %lld\n",      lld(stat_buf->st_size));
        }

    }
    
    if(stat_buf->st_uid)
    {
        user  = getpwuid(stat_buf->st_uid);
        fprintf(stdout, "  Owner         : %d (%s)\n",  
                stat_buf->st_uid, 
                (user ? user->pw_name : "UNKNOWN"));
    }
          
    if(stat_buf->st_gid)
    {
        group = getgrgid(stat_buf->st_gid);
        fprintf(stdout, "  Group         : %d (%s)\n",  
                stat_buf->st_gid, 
                (group ? group->gr_name : "UNKNOWN"));
    }
    
    if(stat_buf->st_atime)
    {
        time_t a_tmp = stat_buf->st_atime;
        sprintf(a_time, "%s", ctime((const time_t *)&a_tmp));
        a_time[strlen(a_time)-1] = 0;
        fprintf(stdout, "  atime         : %llu (%s)\n", llu(stat_buf->st_atime), a_time);
    }
    if(stat_buf->st_mtime)
    {
        time_t m_tmp = stat_buf->st_mtime;
        sprintf(m_time, "%s", ctime((const time_t *)&m_tmp));
        m_time[strlen(m_time)-1] = 0;
        fprintf(stdout, "  mtime         : %llu (%s)\n", llu(stat_buf->st_mtime), m_time);
    }
    if(stat_buf->st_ctime)
    {
        time_t c_tmp = stat_buf->st_ctime;
        sprintf(c_time, "%s", ctime((const time_t *)&c_tmp));
        c_time[strlen(c_time)-1] = 0;
        fprintf(stdout, "  ctime         : %llu (%s)\n", llu(stat_buf->st_ctime), c_time);
    }
   
}

static void usage(int argc, char** argv)
{
    fprintf(stderr,"Usage: %s [OPTION]... [FILE]...\n", argv[0]); 
    fprintf(stderr,"Display FILE(s) status \n\n");
    fprintf(stderr,"  -L, --dereference   follow links\n");
    fprintf(stderr,"  -V, --verbose       turns on verbose messages\n");
    fprintf(stderr,"      --help          display this help and exit\n");
    fprintf(stderr,"      --version       output version information and exit\n");
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
