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
    int     nDfiles;
};

/* Function Prototypes */
static void usage(int argc, char** argv);
static int parse_args(int argc, char** argv, struct options *opts);
static void enable_verbose(struct options *opts);
static void enable_dereference(struct options *opts);
static void enable_dfiles(struct options *opts);

static int do_stat(const char             *pszFile,
                   const char             *pszRelativeFile, 
                   const PVFS_fs_id        fs_id, 
                   const PVFS_credential  *credentials,
                   const struct options   *opts);

void print_stats(const PVFS_object_ref *ref,
                 const char            *pszName,
                 const char            *pszRelativeName,
                 const PVFS_sys_attr   *attr,
                 const struct options  *opts);

int main(int argc, char **argv)
{
   int               ret          = -1,
                     ret_agg      =  0,
                     i            =  0;
   char           ** ppszPvfsPath = NULL;
   PVFS_fs_id     *  pfs_id       = NULL;
   PVFS_credential   credentials;
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

      if (strlen(ppszPvfsPath[i]) == 0)
      {
          memcpy(ppszPvfsPath[i], "/", strlen("/"));
      }
   }

   /* We will re-use the same credentials for each call */
   ret = PVFS_util_gen_credential_defaults(&credentials);
   if (ret < 0)
   {
       PVFS_perror("PVFS_util_gen_credential_defaults", ret);
       return(-1);
   }

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
      ret_agg |= ret;
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

   return(ret_agg);
}

/** do_stat
 *
 * This is where the actual work gets done.  First look up the path,
 * then do a getattr on it.
 */
static int do_stat(const char             * pszFile,
                   const char             * pszRelativeFile, 
                   const PVFS_fs_id         fs_id, 
                   const PVFS_credential  * credentials,
                   const struct options   * opts)
{
   int                  ret = 0;
   PVFS_sysresp_lookup  lk_response = {};
   PVFS_object_ref      ref = {};
   PVFS_sysresp_getattr getattr_response = {};
   PVFS_fs_id           symlink_target_fs_id = 0;
   char                 symlink_target_path[PVFS_NAME_MAX] = {};
   char                 error_string[256] = {0};

   lk_response.error_path = calloc(1,PVFS_NAME_MAX);
   if ( !lk_response.error_path )
   {
      printf("Error allocating memory for error_path\n");
      return(-PVFS_ENOMEM); 
   }
   lk_response.error_path_size = PVFS_NAME_MAX;  
 
   /* Do we want to follow if the file is a symbolic link */
   if(opts->nFollowLink)
   {
      ret = PVFS_sys_lookup(fs_id, 
                            (char *) pszRelativeFile, 
                            credentials, 
                            &lk_response, 
                            PVFS2_LOOKUP_LINK_FOLLOW,
                            NULL);
     if (ret == -PVFS_ENOTPVFS)
     {
        /* At this point, the target of the symlink was defined with an
         * absolute path, so we must "resolve" the path to determine if
         * it is a PVFS path.  If so, then we execute another lookup,
         * and so on and so on and so on.
         */
next_target:
        ret = PVFS_util_resolve(lk_response.error_path,
                                &symlink_target_fs_id, 
                                symlink_target_path, 
                                PVFS_NAME_MAX);
        if (ret)
        {
           sprintf(error_string,"Cannot follow symbolic link. Target path[%s] is NOT in an OrangeFS filesystem",lk_response.error_path);
           perror(error_string);
           printf("Run pvfs2-stat without the -L option to see information about the symbolic link\n");
           return(-1);
        }

        memset(&lk_response.ref,0,sizeof(lk_response.ref));
        memset(lk_response.error_path,0,PVFS_NAME_MAX);

        ret = PVFS_sys_lookup(symlink_target_fs_id,
                              symlink_target_path,
                              (PVFS_credential *)credentials,
                              &lk_response,
                              PVFS2_LOOKUP_LINK_FOLLOW,
                              NULL);
        if (ret == -PVFS_ENOTPVFS)
        {
           printf("Following another symbolic link [%s]\n",symlink_target_path);

           goto next_target;
        }
        if (ret < 0)
        {
            printf("Error(%d) looking up target path (%s)\n",ret,symlink_target_path);
            return (-1);
        }
     }
   }
   else
   {
      ret = PVFS_sys_lookup(fs_id, 
                            (char *) pszRelativeFile, 
                            credentials, 
                            &lk_response, 
                            PVFS2_LOOKUP_LINK_NO_FOLLOW,
                            NULL);
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

   if (opts->nVerbose)
   {
      fprintf(stderr, "PVFS_sys_lookup call on (%s)\n",pszRelativeFile);
   }

   ref = lk_response.ref;

   ret = PVFS_sys_getattr(ref, 
                          PVFS_ATTR_SYS_ALL_NOHINT,
                          credentials, 
                          &getattr_response,
                          NULL);

   if(ret < 0)
   {                          
      PVFS_perror("PVFS_sys_getattr", ret);
      return -1;
   }


   /* Display the attributes for the file */
   print_stats(&ref,
               pszFile, 
               pszRelativeFile, 
               &(getattr_response.attr),
               opts);
   
   return(0);
}

/* parse_args()
 *
 * parses command line arguments
 *
 * returns pointer to options structure on success, NULL on failure
 */
static int parse_args(int argc, char **argv, struct options *opts)
{
    int    i            = 0, 
           ret          = 0, 
           option_index = 0;

    const char *flags = "vVLD?";

    static struct option long_opts[] =
    {
        {"help",0,0,'?'},
        {"version",0,0,'v'},
        {"verbose",0,0,'V'},
        {"dereference",0,0,'L'},
        {"dfiles",0,0,'D'},
        {0,0,0,0}
    };

   while((ret = getopt_long(argc, argv, flags, long_opts, &option_index)) != -1)
   {
      switch (ret)
      {
   
         case 'v': /* --version     */ 
                  printf("%s\n", PVFS2_VERSION);
                  exit(0);

         case 'V': /* --verbose     */ 
                  enable_verbose(opts);
                  break;
                   
         case 'L': /* --dereference */
                  enable_dereference(opts);
                  break;
         
         case 'D': /* --dfiles */
                  enable_dfiles(opts);
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

static void enable_dfiles(struct options * opts)
{
   opts->nDfiles = 1;  
}

void print_stats(const PVFS_object_ref *ref,
                 const char            *pszName,
                 const char            *pszRelativeName,
                 const PVFS_sys_attr   *attr,
                 const struct options  *opts)
{
   char a_time[100] = ""; 
   char m_time[100] = "";  
   char c_time[100] = "";
   char n_time[100] = "";
   struct passwd * user;
   struct group  * group;

   fprintf(stdout, "-------------------------------------------------------\n");
   fprintf(stdout, "  File Name     : %s\n",  pszName);
   fprintf(stdout, "  Relative Name : %s\n",  pszRelativeName);
   fprintf(stdout, "  fs ID         : %d\n",  ref->fs_id);
   fprintf(stdout, "  Handle        : %s\n",  PVFS_OID_str(&ref->handle));
   fprintf(stdout, "  Mask          : %x\n",  attr->mask);
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
      else if ((attr->size == 0) &&
              (attr->objtype & PVFS_TYPE_SYMLINK))
      {
         fprintf(stdout, "  Size          : %zu\n",strlen(attr->link_target));
      }
      else
      {
         fprintf(stdout, "  Size          : %lld\n",      lld(attr->size));
      }

   }
   else
   {  /* a size wasn't returned by getattr */
      switch (attr->objtype)
      {
         case PVFS_TYPE_DIRECTORY:
         {
            fprintf(stdout, "  Size          : 4096\n");
            break;
         }
         case PVFS_TYPE_METAFILE:
         {
            fprintf(stdout, "  Size          : none (datafiles not yet created)\n");
            break;
         }
         default:
         {
            fprintf(stdout, "  Size          : none\n");
            break;
         }
      }/*end switch*/
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
      time_t a_tmp = attr->atime;
      char *ts = ctime((const time_t *)&a_tmp);
      if (ts)
      {
          sprintf(a_time, "%s", ts);
          a_time[strlen(a_time)-1] = 0;
          fprintf(stdout, "  atime         : %llu (%s)\n",
                  llu(attr->atime), a_time);
      }
   }
   if(attr->mask & PVFS_ATTR_SYS_MTIME)
   {
      time_t m_tmp = attr->mtime;
      char *ts = ctime((const time_t *)&m_tmp);
      if (ts)
      {
          sprintf(m_time, "%s", ts);
          m_time[strlen(m_time)-1] = 0;
          fprintf(stdout, "  mtime         : %llu (%s)\n",
                  llu(attr->mtime), m_time);
      }
   }
   if(attr->mask & PVFS_ATTR_SYS_CTIME)
   {
      time_t c_tmp = attr->ctime;
      char *ts = ctime((const time_t *)&c_tmp);
      if (ts)
      {
          sprintf(c_time, "%s", ts);
          c_time[strlen(c_time)-1] = 0;
          fprintf(stdout, "  ctime         : %llu (%s)\n",
                  llu(attr->ctime), c_time);
      }
   }
   if(attr->mask & PVFS_ATTR_SYS_NTIME)
   {
      time_t n_tmp = attr->ntime;
      char *ts = ctime((const time_t *)&n_tmp);
      if (ts)
      {
          sprintf(n_time, "%s", ts);
          n_time[strlen(n_time)-1] = 0;
          fprintf(stdout, "  ntime         : %llu (%s)\n",
                  llu(attr->ntime), n_time);
      }
   }
   
   /* dfile_count is only valid for a file. For a given file, it tells how many
    *  datafiles there are
    */
   if( (attr->mask & PVFS_ATTR_SYS_DFILE_COUNT) &&
       (attr->objtype == PVFS_TYPE_METAFILE))
   {
      fprintf(stdout, "  datafiles     : %d\n", attr->dfile_count);
   }

#if 0
   if( (attr->mask & PVFS_ATTR_SYS_DFILE_COUNT) &&
       (attr->objtype == PVFS_TYPE_METAFILE) &&
       opts->nDfiles)
   {
      int i;
      fprintf(stdout, "  dfile handles : ");
      for(i = 0; i < attr->dfile_count; i++)
      {
         fprintf(stdout, "%llu ", llu(attr->dfile_array[i]));
      }
      fprintf(stdout, "\n");
   }
#endif

   if( (attr->mask & PVFS_ATTR_SYS_BLKSIZE) &&
       (attr->objtype == PVFS_TYPE_METAFILE))
   {
      fprintf(stdout, "  blksize       : %lld\n", lld(attr->blksize));
   }

   /* dirent_count is only valid on directories */
   if( (attr->mask & PVFS_ATTR_SYS_DIRENT_COUNT) &&
       (attr->objtype == PVFS_TYPE_DIRECTORY))
   {
      fprintf(stdout, "  dir entries   : %llu\n", llu(attr->dirent_count));
   }

   if( (attr->mask & PVFS_ATTR_SYS_DISTDIR_ATTR) &&
       (attr->objtype == PVFS_TYPE_DIRECTORY))
   {
      fprintf(stdout, "  dirdata count : %d\n", attr->distr_dir_servers_max);
   }

   if ((attr->mask & PVFS_ATTR_SYS_TYPE) && 
          (attr->objtype & PVFS_TYPE_METAFILE))
   {
       if (attr->flags == 0)
           fprintf(stdout, "  flags         : none");
       else
           fprintf(stdout, "  flags         : ");
       if (attr->flags & PVFS_IMMUTABLE_FL)
           fprintf(stdout, "immutable, ");
       if (attr->flags & PVFS_APPEND_FL)
           fprintf(stdout, "append-only, ");
       if (attr->flags & PVFS_NOATIME_FL)
           fprintf(stdout, "noatime ");
       fprintf(stdout, "\n");
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
