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
                   const char             * pszRelativeFile, 
                   const PVFS_fs_id         fs_id, 
                   const PVFS_credentials * credentials,
                   const struct options   * opts);
void print_stats(const PVFS_object_ref * ref,
                 const char            * pszName,
                 const char            * pszRelativeName,
                 const PVFS_sys_attr   * attr);

int main(int argc, char **argv)
{
   int               ret          = -1,
                     i            =  0;
   char           ** ppszPvfsPath = NULL;
   PVFS_fs_id     *  pfs_id       = NULL;
   PVFS_credentials  credentials;
   struct options    user_opts;

   /* Initialize any memory */
   memset(&user_opts,   0, sizeof(user_opts));
   memset(&credentials, 0, sizeof(credentials));
   
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
   
   /* Allocate space to hold the relative pvfs2 path & fs_id for each 
    * requested file 
    */
   ppszPvfsPath = (char **)calloc(user_opts.nNumFiles, sizeof(char *));
   
   if(ppszPvfsPath == NULL)
   {
      fprintf(stderr, "Unable to allocate memory\n");
      return(-1);
   }
   
    /* Allocate enough space to hold file system id for each directory */
   pfs_id = (PVFS_fs_id *)calloc(user_opts.nNumFiles, sizeof(PVFS_fs_id));
   
   if(pfs_id == NULL)
   {
      fprintf(stderr, "Unable to allocate memory\n");
      return(-1);
   }
   
   
   for(i = 0; i < user_opts.nNumFiles; i++)
   {
      ppszPvfsPath[i] = (char *)calloc(PVFS_NAME_MAX, sizeof(char));
      if(ppszPvfsPath[i] == NULL)
      {
         fprintf(stderr, "Unable to allocate memory\n");
         return(-1);
      }
   }

   ret = PVFS_util_init_defaults();
   if(ret < 0)
   {
      PVFS_perror("PVFS_util_init_defaults", ret);
      return(-1);
   }
   
   /* Let's verify that all the given files reside on a PVFS2 filesytem */
   for(i = 0; i < user_opts.nNumFiles; i++)
   {
      ret = PVFS_util_resolve(user_opts.pszFiles[i], 
                              &pfs_id[i], 
                              ppszPvfsPath[i], 
                              PVFS_NAME_MAX);

      if (ret < 0)
      {
         fprintf(stderr, "Error: could not find file system for %s\n", 
                 user_opts.pszFiles[i]);
         return(-1);
      }
   }

   /* We will re-use the same credentials for each call */
   PVFS_util_gen_credentials(&credentials);

   for(i = 0; i < user_opts.nNumFiles; i++)
   {
      ret = do_stat(user_opts.pszFiles[i], 
                    ppszPvfsPath[i], 
                    pfs_id[i], 
                    &credentials,
                    &user_opts);
      if(ret != 0)
      {
         fprintf(stderr, "Error stating [%s]\n", user_opts.pszFiles[i]);
      }
   }

   PVFS_sys_finalize();

   /* Deallocate any allocated memory */
   if(user_opts.pszFiles != NULL)
   {
      free(user_opts.pszFiles);
   }
   
   if(ppszPvfsPath != NULL)
   {
       for(i=0;i<user_opts.nNumFiles;i++)
       {
          if(ppszPvfsPath[i] != NULL)
          {
             free(ppszPvfsPath[i]);
          }
       }
   
      free(ppszPvfsPath);
   }
   
   if(pfs_id != NULL)
   {
      free(pfs_id);
   }

   return(0);
}

static int do_stat(const char             * pszFile,
                   const char             * pszRelativeFile, 
                   const PVFS_fs_id         fs_id, 
                   const PVFS_credentials * credentials,
                   const struct options   * opts)
{
   int                  ret = 0;
   PVFS_sysresp_lookup  lk_response;
   PVFS_object_ref      ref;
   PVFS_sysresp_getattr getattr_response;

   /* Initialize memory */
   memset(&lk_response,     0, sizeof(lk_response));
   memset(&ref,             0, sizeof(ref));
   memset(&getattr_response,0, sizeof(getattr_response));
   

   /* Do we want to follow if the file is a symbolic link */
   if(opts->nFollowLink)
   {
      ret = PVFS_sys_lookup(fs_id, 
                            (char *) pszRelativeFile, 
                            (PVFS_credentials *) credentials, 
                            &lk_response, 
                            PVFS2_LOOKUP_LINK_FOLLOW);
   }
   else
   {
      ret = PVFS_sys_lookup(fs_id, 
                            (char *) pszRelativeFile, 
                            (PVFS_credentials *) credentials, 
                            &lk_response, 
                            PVFS2_LOOKUP_LINK_NO_FOLLOW);
   }
   
   if(ret < 0)
   {
      if(opts->nVerbose)
      {
         fprintf(stderr, "PVFS_sys_lookup call on [%s]\n", pszRelativeFile);
      }
      PVFS_perror("PVFS_sys_lookup", ret);
      return -1;
   }

   ref.handle = lk_response.ref.handle;
   ref.fs_id  = fs_id;
   
   ret = PVFS_sys_getattr(ref, 
                          PVFS_ATTR_SYS_ALL,
                          (PVFS_credentials *) credentials, 
                          &getattr_response);

   if(ret < 0)
   {                          
      PVFS_perror("PVFS_sys_getattr", ret);
      return -1;
   }

   /* Display the attributes for the file */
   print_stats(&ref,
               pszFile, 
               pszRelativeFile, 
               &(getattr_response.attr));
   
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
    char * cur_option   = NULL;

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

void print_stats(const PVFS_object_ref * ref,
                 const char            * pszName,
                 const char            * pszRelativeName,
                 const PVFS_sys_attr   * attr)
{
   char a_time[100] = "", 
        m_time[100] = "",  
        c_time[100] = "";
   struct passwd * user;
   struct group  * group;

   fprintf(stdout, "-------------------------------------------------------\n");
   fprintf(stdout, "  File Name     : %s\n",  pszName);
   fprintf(stdout, "  Relative Name : %s\n",  pszRelativeName);
   fprintf(stdout, "  fs ID         : %d\n",  ref->fs_id);
   fprintf(stdout, "  Handle        : %llu\n", llu(ref->handle));
   fprintf(stdout, "  Mask          : %o\n",  attr->mask);
   if(attr->mask & PVFS_ATTR_SYS_PERM)
   {
      fprintf(stdout, "  Permissions   : %o\n",  attr->perms);
   }

   /* Print the type of object */
   if(attr->mask & PVFS_ATTR_SYS_TYPE)
   {
      if(attr->objtype & PVFS_TYPE_METAFILE)
      {
         fprintf(stdout, "  Type          : Regular File\n");
      }
      else if(attr->objtype & PVFS_TYPE_DIRECTORY)
      {
         fprintf(stdout, "  Type          : Directory\n");
      }
      else if(attr->objtype & PVFS_TYPE_SYMLINK)
      {
         fprintf(stdout, "  Type          : Symbolic Link\n");
         if(attr->mask &  PVFS_ATTR_SYS_LNK_TARGET)
         {
            fprintf(stdout, "  Link Target   : %s\n", attr->link_target);
         }
      }
   }

   if(attr->mask & PVFS_ATTR_SYS_SIZE)
   {
      /* If the size of a directory object is zero, let's default the size to 
       * 4096. This is what the kernel module does, and is the default directory
       * size on an EXT3 system
       */
      if( (attr->size == 0) && 
          (attr->objtype & PVFS_TYPE_DIRECTORY))
      {
         fprintf(stdout, "  Size          : 4096\n");
      }
      else
      {
         fprintf(stdout, "  Size          : %lld\n",      lld(attr->size));
      }

   }
   if(attr->mask & PVFS_ATTR_SYS_UID)
   {
      user  = getpwuid(attr->owner);
      fprintf(stdout, "  Owner         : %d (%s)\n",  
              attr->owner, 
              (user ? user->pw_name : "UNKNOWN"));
   }      
   if(attr->mask & PVFS_ATTR_SYS_GID)
   {
      group = getgrgid(attr->group);
      fprintf(stdout, "  Group         : %d (%s)\n",  
              attr->group, 
              (group ? group->gr_name : "UNKNOWN"));
   }
   if(attr->mask & PVFS_ATTR_SYS_ATIME)
   {
      sprintf(a_time, "%s", ctime((const time_t *)&(attr)->atime));
      a_time[strlen(a_time)-1] = 0;
      fprintf(stdout, "  atime         : %llu (%s)\n", llu(attr->atime), a_time);
   }
   if(attr->mask & PVFS_ATTR_SYS_MTIME)
   {
      sprintf(m_time, "%s", ctime((const time_t *)&(attr)->mtime));
      m_time[strlen(m_time)-1] = 0;
      fprintf(stdout, "  mtime         : %llu (%s)\n", llu(attr->mtime), m_time);
   }
   if(attr->mask & PVFS_ATTR_SYS_CTIME)
   {
      sprintf(c_time, "%s", ctime((const time_t *)&(attr)->ctime));
      c_time[strlen(c_time)-1] = 0;
      fprintf(stdout, "  ctime         : %llu (%s)\n", llu(attr->ctime), c_time);
   }
   
   /* dfile_count is only valid for a file. For a given file, it tells how many
    *  datafiles there are
    */
   if( (attr->mask & PVFS_ATTR_SYS_DFILE_COUNT) &&
       (attr->mask & PVFS_TYPE_METAFILE))
   {
      fprintf(stdout, "  datafiles     : %d\n", attr->dfile_count);
   }
   /* dirent_count is only valid on directories */
   if( (attr->mask & PVFS_ATTR_SYS_DIRENT_COUNT) &&
       (attr->mask & PVFS_TYPE_DIRECTORY))
   {
      fprintf(stdout, "  dir entries   : %llu\n", llu(attr->dirent_count));
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
