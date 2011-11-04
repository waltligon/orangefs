/*
 * (C) 2004 Clemson University and The University of Chicago
 * 
 * See COPYING in top-level directory.
 *
 * 03/19/07 - Added set and get for user.pvfs2.mirror.mode and ..mirror.copies.
 *            Added get for user.pvfs2.mirror.handles and ..mirror.status
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

#include "xattr-utils.h"

#include "pvfs2-mirror.h"

#define VALBUFSZ 1024

/* extended attribute name spaces supported in PVFS2 */
const char *PINT_eattr_namespaces[] =
{
    "system.",
    "user.",
    "trusted.",
    "security.",
    NULL
};

/* optional parameters, filled in by parse_args() */
struct options
{
    PVFS_ds_keyval *key;
    PVFS_ds_keyval *val;
    char* srcfile;
    int get, text, key_count;
};

enum object_type { 
    UNIX_FILE, 
    PVFS2_FILE 
};

typedef struct pvfs2_file_object_s {
    PVFS_fs_id fs_id;
    PVFS_object_ref ref;
    char pvfs2_path[PVFS_NAME_MAX];	
    char user_path[PVFS_NAME_MAX];
    PVFS_sys_attr attr;
    PVFS_permissions perms;
} pvfs2_file_object;

typedef struct unix_file_object_s {
    int fd;
    int mode;
    char path[NAME_MAX];
    PVFS_fs_id fs_id;
} unix_file_object;

typedef struct file_object_s {
    int fs_type;
    union {
	unix_file_object ufs;
	pvfs2_file_object pvfs2;
    } u;
} file_object;

static struct options* parse_args(int argc, char* argv[]);
static int generic_open(file_object *obj, PVFS_credential *credentials);

static int pvfs2_eattr(int get
                      ,file_object      *obj
                      ,PVFS_ds_keyval   *key_p
                      ,PVFS_ds_keyval   *val_p
                      ,PVFS_credential  *creds
                      ,int key_count); 

static void usage(int argc, char** argv);
static int resolve_filename(file_object *obj, char *filename);
static int modify_val(PVFS_ds_keyval *key_p, PVFS_ds_keyval *val_p);
static int permit_set(PVFS_ds_keyval *key_p);
static int eattr_is_prefixed(char* key_name);

PVFS_metafile_hint current_meta_hint={0};

int main(int argc, char **argv)
{
  int ret = 0;
  struct options* user_opts = NULL;
  file_object src;
  PVFS_credential credentials;
  int i;
  PVFS_ds_keyval tmp_val={0};

  memset(&src, 0, sizeof(src));
  /* look at command line arguments */
  user_opts = parse_args(argc, argv);
  if(!user_opts)
  {
    fprintf(stderr, "Error: failed to parse "
            "command line arguments.\n");
    return(-1);
  }

  ret = PVFS_util_init_defaults();
  if(ret < 0)
  {
    PVFS_perror("PVFS_util_init_defaults", ret);
    return(-1);
  }
  resolve_filename(&src, user_opts->srcfile);

  ret = PVFS_util_gen_credential_defaults(&credentials);
  if (ret < 0)
  {
      PVFS_perror("PVFS_util_gen_credential_defaults", ret);
      return(-1);
  }

  ret = generic_open(&src, &credentials);
  if (ret < 0)
  {
      fprintf(stderr, "Could not open %s\n", user_opts->srcfile);
      return -1;
  }

  if (!eattr_is_prefixed(user_opts->key[0].buffer))
  {
      fprintf(stderr, "extended attribute key is not prefixed %s\n"
                    , (char *) user_opts->key[0].buffer);
      return -1;
  }
  if (!user_opts->get)
  {
      if (!permit_set(&user_opts->key[0]))
      {
          fprintf(stderr, "Not permitted to set key %s\n"
                        , (char *) user_opts->key[0].buffer);
          return -1;
      }
      if (strncmp(user_opts->key[0].buffer
                  ,"user.pvfs2.meta_hint"
                  ,user_opts->key[0].buffer_sz) == 0)
      {
         tmp_val.buffer=&current_meta_hint.flags;
         tmp_val.buffer_sz=sizeof(current_meta_hint.flags);
         /*retrieve the current value of meta_hint*/
         ret=pvfs2_eattr(1 /*get*/
                        ,&src
                        ,user_opts->key
                        ,&tmp_val
                        ,&credentials
                        ,1 /*keycount*/);
         if (ret != 0)
         {
            printf("%s does not currently have a meta_hint value (0X%08X).\n"
                  ,user_opts->srcfile
                  ,(unsigned int)current_meta_hint.flags
                  );
         }else{
            printf("%s has a meta_hint value of (0X%08X).\n"
                  ,user_opts->srcfile
                  ,(unsigned int)current_meta_hint.flags
                  );
         }
      }
      if (modify_val(&user_opts->key[0], &user_opts->val[0]) < 0)
      {
          fprintf(stderr, "Invalid value for user-settable attribute %s\n"
                        , (char *) user_opts->key[0].buffer);
          return -1;
      }
  }

    ret = pvfs2_eattr(user_opts->get
                     ,&src
                     ,user_opts->key
                     ,user_opts->val
                     ,&credentials
                     ,user_opts->key_count);
    if ( (ret != 0) && (ret == -PVFS_ENOENT) )
    {
        printf("PVFS_sys_geteattr: no hints defined\n");
        return ret;
    }
    else if (ret != 0) 
    {
        PVFS_perror("PVFS_sys_geteattr",ret); 
        return ret;
    }
    if (user_opts->get && user_opts->text)  
    {
        if (strncmp(user_opts->key[0].buffer
                   ,"user.pvfs2.meta_hint"
                   ,user_opts->key[0].buffer_sz) == 0) {
            PVFS_metafile_hint *hint = 
                            (PVFS_metafile_hint *) user_opts->val[0].buffer;
            printf("Metafile Hints (0X%08X)",(unsigned int)hint->flags);
            if (hint->flags & PVFS_IMMUTABLE_FL) {
                printf(" :immutable file ");
            }
            if (hint->flags & PVFS_APPEND_FL) {
                printf(" :Append-only file ");
            }
            if (hint->flags & PVFS_NOATIME_FL) {
                printf(" :Atime updates disabled");
            }
            if (hint->flags & PVFS_MIRROR_FL) {
		printf("  :Mirroring is enabled");
            }
            printf("\n");
        } else if ( strncmp(user_opts->key[0].buffer
                           ,"user.pvfs2.mirror.handles"
                           ,user_opts->key[0].buffer_sz) == 0)
        {
             PVFS_handle *myHandles = (PVFS_handle *)user_opts->val[0].buffer;
             int copies = *(int *)user_opts->val[1].buffer;
             int dfile_count = src.u.pvfs2.attr.dfile_count;
             for (i=0; i<(copies * dfile_count); i++)
             {
                 printf("Handle(%d):%llu\n",i,llu(myHandles[i]));
             }
        } else if ( strncmp(user_opts->key[0].buffer
                           ,"user.pvfs2.mirror.copies"
                           ,user_opts->key[0].buffer_sz) == 0)
        {
             int *myCopies = (int *)user_opts->val[0].buffer;
             printf("Number of Mirrored Copies : %d\n",*myCopies);
        } else if ( strncmp(user_opts->key[0].buffer
                           ,"user.pvfs2.mirror.status"
                           ,user_opts->key[0].buffer_sz) == 0)
        {
             int copies = *(int *)user_opts->val[1].buffer;
             int dfile_count = src.u.pvfs2.attr.dfile_count;
             PVFS_handle *status = (PVFS_handle *)user_opts->val[0].buffer;
             for (i=0; i<(dfile_count * copies); i++)
                 printf("src handle(%d) : status(%s) : value(%llu)\n"
                       ,i
                       ,status[i]==0?"usable":"UNusable"
                       ,llu(status[i]));
        } else if ( strncmp(user_opts->key[0].buffer
                           ,"user.pvfs2.mirror.mode"
                           ,user_opts->key[0].buffer_sz) == 0)
        {
             printf("Mirroring Mode : ");
             switch(*(MIRROR_MODE *)user_opts->val[0].buffer)
             {
                case NO_MIRRORING :
                {
                    printf("Turned OFF\n");
                    break;
                }
                case MIRROR_ON_IMMUTABLE :
                {
                    printf("Create Mirror when IMMUTABLE is set\n");
                    break;
                }
                default:
                {
                    printf("Unknown mode(%d)\n"
                          ,*(int *)user_opts->val[0].buffer);
                    break;
                }
             }/*end switch*/
        } else {
            printf("key : \"%s\" \tValue : \"%s\"\n",
                    (char *)user_opts->key[0].buffer,
                    (char *)user_opts->val[0].buffer);
        }
    }
  PVFS_sys_finalize();
  return(ret);
}

static int modify_val(PVFS_ds_keyval *key_p, PVFS_ds_keyval *val_p)
{
  /*We don't want these settings to interfere with the mirroring flag.  It is
   *turned on and off with the pvfs2-setmattr and setmattr commands.
  */
    if (strncmp(key_p->buffer,"user.pvfs2.meta_hint"
                             ,key_p->buffer_sz) == 0)
    {
        if (strncmp(val_p->buffer, "+immutable", val_p->buffer_sz) == 0)
        {
            current_meta_hint.flags |= PVFS_IMMUTABLE_FL;
            printf("Adding immutable to meta_hint...(0X%08X)\n"
                  ,(unsigned int)current_meta_hint.flags);
        }
        else if (strncmp(val_p->buffer, "-immutable", val_p->buffer_sz) == 0)
        {
            current_meta_hint.flags &= ~PVFS_IMMUTABLE_FL;
            printf("Removing immutable from meta_hint...(0X%08X)\n"
                  ,(unsigned int)current_meta_hint.flags);
        }
        else if (strncmp(val_p->buffer,"=immutable", val_p->buffer_sz) == 0)
        {
            current_meta_hint.flags = 
             (current_meta_hint.flags & ~ALL_FS_META_HINT_FLAGS) | PVFS_IMMUTABLE_FL;
            printf("Setting meta_hint to immutable only (0X%08X)\n"
                  ,(unsigned int)current_meta_hint.flags);
        } 
        else if (strncmp(val_p->buffer, "+append", val_p->buffer_sz) == 0)
        {
            current_meta_hint.flags |= PVFS_APPEND_FL;
            printf("Adding append to meta_hint...(0X%08X)\n"
                  ,(unsigned int)current_meta_hint.flags);
        }
        else if (strncmp(val_p->buffer, "-append", val_p->buffer_sz) == 0)
        {
            current_meta_hint.flags &= ~PVFS_APPEND_FL;
            printf("Removing append from meta_hint...(0X%08X)\n"
                  ,(unsigned int)current_meta_hint.flags);
        }
        else if (strncmp(val_p->buffer,"=append", val_p->buffer_sz) == 0)
        {
            current_meta_hint.flags = 
             (current_meta_hint.flags & ~ALL_FS_META_HINT_FLAGS) | PVFS_APPEND_FL;
            printf("Setting meta_hint to append only (0X%08X)\n"
                  ,(unsigned int)current_meta_hint.flags);
        }
        else if (strncmp(val_p->buffer, "+noatime", val_p->buffer_sz) == 0)
        {
            current_meta_hint.flags |= PVFS_NOATIME_FL;
            printf("Adding noatime to meta_hint...(0X%08X)\n"
                  ,(unsigned int)current_meta_hint.flags);
        }
        else if (strncmp(val_p->buffer, "-noatime", val_p->buffer_sz) == 0)
        {
            current_meta_hint.flags &= ~PVFS_NOATIME_FL;
            printf("Removing atime from meta_hint...(0X%08X)\n"
                  ,(unsigned int)current_meta_hint.flags);
        }
        else if (strncmp(val_p->buffer,"=noatime", val_p->buffer_sz) == 0)
        {
            current_meta_hint.flags = 
             (current_meta_hint.flags & ~ALL_FS_META_HINT_FLAGS) | PVFS_NOATIME_FL;
            printf("Setting meta_hint to noatime only (0X%08X)\n"
                  ,(unsigned int)current_meta_hint.flags);
        }
        else
        { 
            return -1;
        }
        memcpy(val_p->buffer, &current_meta_hint.flags, sizeof(current_meta_hint.flags));
        val_p->buffer_sz = sizeof(current_meta_hint.flags);
    } else if (strncmp(key_p->buffer,"user.pvfs2.mirror.mode"
                                    ,key_p->buffer_sz) == 0)
    {
       printf("Setting mirror mode to %d\n",*(int *)val_p->buffer);
    } else if (strncmp(key_p->buffer,"user.pvfs2.mirror.copies"
                                    ,key_p->buffer_sz) == 0)
    {
       printf("Setting number of mirrored copies to %d\n"
             ,*(int *)val_p->buffer);
    }

    return 0;
}

static int permit_set(PVFS_ds_keyval *key_p)
{
    if (strncmp(key_p->buffer, "system.", 7) == 0
            || strncmp(key_p->buffer, "trusted.", 8) == 0
            || strncmp(key_p->buffer, "security.", 9) == 0)
        return 0;
    return 1;
}

/* pvfs2_geteattr()
 *
 * changes the mode of the given file to the given permissions
 *
 * returns zero on success and negative one on failure
 */
static int pvfs2_eattr(int get
                      ,file_object      *obj
                      ,PVFS_ds_keyval   *key_p
                      ,PVFS_ds_keyval   *val_p
                      ,PVFS_credential  *creds
                      ,int key_count) 
{
  int ret = -1;

  if (obj->fs_type == UNIX_FILE)
  {
      if (get == 1)
      {
#ifndef HAVE_FGETXATTR_EXTRA_ARGS
        if ((ret = fgetxattr(obj->u.ufs.fd
                            ,key_p->buffer
                            ,val_p->buffer
                            ,val_p->buffer_sz)) < 0)
#else
        if ((ret = fgetxattr(obj->u.ufs.fd
                            ,key_p->buffer
                            ,val_p->buffer
                            ,val_p->buffer_sz 
                            ,0
                            ,0)) < 0)
#endif
        {
            perror("fgetxattr:");
            return -1;
        }
      }
      else
      {
#ifdef HAVE_FSETXATTR
        if ((ret = fsetxattr(obj->u.ufs.fd, key_p->buffer, 
                             val_p->buffer, val_p->buffer_sz, 0
#ifdef HAVE_FSETXATTR_EXTRA_ARGS
                             ,0
#endif
                            )) < 0)
#else
        errno = ENOSYS;
#endif
        {
            perror("fsetxattr:");
            return -1;
        }
      }
  }
  else
  {
      if (get == 1 && key_count == 1)
      {
          ret = PVFS_sys_geteattr(obj->u.pvfs2.ref, creds, key_p, val_p, NULL);
      } else if (get == 1 && key_count == 2)
      {
          PVFS_sysresp_geteattr *resp = malloc(sizeof(*resp));
          if (!resp)
          {
             fprintf(stderr,"Unable to allocate resp structure.\n");
             exit(EXIT_FAILURE);
          }
          memset(resp,0,sizeof(*resp));
          resp->val_array = val_p;
          resp->err_array = malloc(2 * sizeof(PVFS_error));
          if (!resp->err_array)
          {
             fprintf(stderr,"Unable to allocate err_array.\n");
             exit(EXIT_FAILURE);
          }
          memset(resp->err_array,0,sizeof(2 * sizeof(PVFS_error)));
          
          ret = PVFS_sys_geteattr_list(obj->u.pvfs2.ref
                                      ,creds
                                      ,key_count
                                      ,key_p
                                      ,resp
                                      ,NULL );
      } else {
          ret = PVFS_sys_seteattr(obj->u.pvfs2.ref, creds, key_p, val_p, 0, NULL);
      }

      return ret;
  }
  return 0;
}


/* parse_args()
 *
 * parses command line arguments
 *
 * returns pointer to options structure on success, NULL on failure
 */
static struct options* parse_args(int argc, char* argv[])
{
    char flags[] = "k:v:ts";
    int one_opt = 0;

    struct options* tmp_opts = NULL;

    /* create storage for the command line options */
    tmp_opts = (struct options*)malloc(sizeof(struct options));
    if(!tmp_opts){
	return(NULL);
    }
    memset(tmp_opts, 0, sizeof(struct options));

    /*create one key structure*/
    tmp_opts->key = malloc(sizeof(PVFS_ds_keyval));
    if (!tmp_opts->key)
    {
        fprintf(stderr,"Unable to allocate tmp_opts->key.\n");
        exit(EXIT_FAILURE);
    }
    memset(tmp_opts->key,0,sizeof(PVFS_ds_keyval));

    /*create one val structure*/
    tmp_opts->val = malloc(sizeof(PVFS_ds_keyval));
    if (!tmp_opts->val)
    {
        fprintf(stderr,"Unable to allocate tmp_opts->val.\n");
        exit(EXIT_FAILURE);
    }
    memset(tmp_opts->val,0,sizeof(PVFS_ds_keyval));

    /*set default key_count*/
    tmp_opts->key_count = 1;

    /* fill in defaults */
    tmp_opts->srcfile = strdup(argv[argc-1]);
    tmp_opts->get = 1;

    /* look at command line arguments */
    while((one_opt = getopt(argc, argv, flags)) != -1)
    {
	switch(one_opt){
            case 't':
                tmp_opts->text = 1;
                break;
            case 's':
                tmp_opts->get = 0;
                break;
            case 'k':
                tmp_opts->key[0].buffer = strdup(optarg);
                tmp_opts->key[0].buffer_sz = strlen(tmp_opts->key[0].buffer) + 1;
                break;
            case 'v':
                if (strncmp(tmp_opts->key[0].buffer
                           ,"user.pvfs2.mirror.mode"
                           ,tmp_opts->key[0].buffer_sz) == 0 ||
                    strncmp(tmp_opts->key[0].buffer
                           ,"user.pvfs2.mirror.copies"
                           ,tmp_opts->key[0].buffer_sz) == 0)
                { /*convert string argument into numeric argument*/
                  tmp_opts->val[0].buffer = malloc(sizeof(int));
                  if (!tmp_opts->val[0].buffer)
                  {
                     printf("Unable to allocate memory for key value.\n");
                     exit(EXIT_FAILURE);
                  }
                  memset(tmp_opts->val[0].buffer,0,sizeof(int));
                  *(int *)tmp_opts->val[0].buffer = atoi(optarg);
                  tmp_opts->val[0].buffer_sz = sizeof(int);
                  break;
                } else {
                  tmp_opts->val[0].buffer = strdup(optarg);
                  tmp_opts->val[0].buffer_sz = strlen(tmp_opts->val[0].buffer);
                  break;
                }
	    case('?'):
                printf("?\n");
		usage(argc, argv);
		exit(EXIT_FAILURE);
	}
    }

    /*ensure that the given mode is supported by PVFS*/
    if (!tmp_opts->get &&
         strncmp(tmp_opts->key[0].buffer
                 ,"user.pvfs2.mirror.mode"
                 ,tmp_opts->key[0].buffer_sz) == 0)
    {
       if (tmp_opts->val[0].buffer &&
           (*(int *)tmp_opts->val[0].buffer < BEGIN_MIRROR_MODE ||
            *(int *)tmp_opts->val[0].buffer > END_MIRROR_MODE) )
       {
          fprintf(stderr,"Invalid Mirror Mode ==> %d\n"
                         "\tValid Modes\n"
                         "\t1. %d == No Mirroring\n"
                         "\t2. %d == Mirroring on Immutable\n"
                        ,*(int *)tmp_opts->val[0].buffer
                        ,NO_MIRRORING,MIRROR_ON_IMMUTABLE);

          exit(EXIT_FAILURE);
       }
    }

    if (tmp_opts->get == 1)
    {
        /*if user wants mirror.handles or mirror.status, then we must also */
        /*retrieve the number of copies, so we know how to display the     */
        /*information properly.                                            */
        if (strncmp(tmp_opts->key[0].buffer
                   ,"user.pvfs2.mirror.handles"
                   ,tmp_opts->key[0].buffer_sz) == 0 ||
            strncmp(tmp_opts->key[0].buffer
                    ,"user.pvfs2.mirror.status"
                    ,tmp_opts->key[0].buffer_sz) == 0 )
        {
           tmp_opts->key_count = 2;
           PVFS_ds_keyval *myKeys = malloc(tmp_opts->key_count * 
                                           sizeof(PVFS_ds_keyval));
           if (!myKeys)
           {
               fprintf(stderr,"Unable to allocate myKeys.\n");
               exit(EXIT_FAILURE);
           }
           memset(myKeys,0,tmp_opts->key_count*sizeof(PVFS_ds_keyval));
           myKeys[0] = *tmp_opts->key;
           myKeys[1].buffer = strdup("user.pvfs2.mirror.copies");
           myKeys[1].buffer_sz = sizeof("user.pvfs2.mirror.copies");
           free(tmp_opts->key);
           tmp_opts->key = myKeys;
        }/*end if handles or status*/

        

        tmp_opts->val[0].buffer = calloc(1, VALBUFSZ);
        if (!tmp_opts->val[0].buffer)
        {
           fprintf(stderr,"Unable to allocate tmp_opts->val[0].buffer.\n");
           exit(EXIT_FAILURE);
        }
        tmp_opts->val[0].buffer_sz = VALBUFSZ;
        
        if (tmp_opts->key_count == 2)
        {
           PVFS_ds_keyval *myVals = malloc(tmp_opts->key_count * 
                                           sizeof(PVFS_ds_keyval));
           if (!myVals)
           {
               fprintf(stderr,"Unable to allocate myVals.\n");
               exit(EXIT_FAILURE);
           }
           memset(myVals,0,tmp_opts->key_count*sizeof(PVFS_ds_keyval));
           myVals[0] = *tmp_opts->val;
           free(tmp_opts->val);

           myVals[1].buffer = malloc(sizeof(int));
           if (!myVals[1].buffer)
           {
              fprintf(stderr,"Unable to allocate myVals[1].buffer.\n");
              exit(EXIT_FAILURE);
           }
           myVals[1].buffer_sz = sizeof(int);
           tmp_opts->val = myVals;
         }/*end if*/  
    } else {
        if (tmp_opts->val[0].buffer == NULL)
        {
            fprintf(stderr, "Please specify value if setting extended "
                            "attributes\n");
            usage(argc, argv);
            exit(EXIT_FAILURE);
        }
    }
    if (tmp_opts->key[0].buffer == NULL)
    {
        fprintf(stderr, "Please specify key if getting extended attributes\n");
        usage(argc, argv);
        exit(EXIT_FAILURE);
    }
    return(tmp_opts);
}


static void usage(int argc, char** argv)
{
    fprintf(stderr,"Usage: %s -s {set xattrs} -k <key> -v <val> "
                   "-t {print attributes} filename\n",argv[0]);
    return;
}

/* resolve_filename:
 *  given 'filename', find the PVFS2 fs_id and relative pvfs_path.  In case of
 *  error, assume 'filename' is a unix file.
 */
static int resolve_filename(file_object *obj, char *filename)
{
    int ret;

    ret = PVFS_util_resolve(filename, &(obj->u.pvfs2.fs_id),
	    obj->u.pvfs2.pvfs2_path, PVFS_NAME_MAX);
    if (ret < 0)
    {
	obj->fs_type = UNIX_FILE;
        strncpy(obj->u.ufs.path, filename, NAME_MAX);
    } else {
	obj->fs_type = PVFS2_FILE;
	strncpy(obj->u.pvfs2.user_path, filename, PVFS_NAME_MAX);
    }
    return 0;
}

/* generic_open:
 *  given a file_object, perform the apropriate open calls.  
 */
static int generic_open(file_object *obj, PVFS_credential *credentials)
{
    struct stat stat_buf;
    PVFS_sysresp_lookup resp_lookup;
    PVFS_sysresp_getattr resp_getattr;
    PVFS_object_ref ref;
    int ret = -1;

    if (obj->fs_type == UNIX_FILE)
    {
        PINT_statfs_t statfsbuf;
        memset(&stat_buf, 0, sizeof(struct stat));

        stat(obj->u.ufs.path, &stat_buf);
        if (!S_ISREG(stat_buf.st_mode))
        {
            fprintf(stderr, "Not a file!\n");
            return(-1);
        }
        obj->u.ufs.fd = open(obj->u.ufs.path, O_RDONLY);
        obj->u.ufs.mode = (int)stat_buf.st_mode;
	if (obj->u.ufs.fd < 0)
	{
	    perror("open");
	    fprintf(stderr, "could not open %s\n", obj->u.ufs.path);
	    return (-1);
	}
        if (PINT_statfs_fd_lookup(obj->u.ufs.fd, &statfsbuf) < 0)
        {
            perror("fstatfs:");
            fprintf(stderr, "could not fstatfs %s\n", obj->u.ufs.path);
        }
        memcpy(&obj->u.ufs.fs_id, &PINT_statfs_fsid(&statfsbuf), 
               sizeof(PINT_statfs_fsid(&statfsbuf)));
        return 0;
    }
    else
    {
        memset(&resp_lookup, 0, sizeof(PVFS_sysresp_lookup));
        ret = PVFS_sys_lookup(obj->u.pvfs2.fs_id, 
                              (char *) obj->u.pvfs2.pvfs2_path,
                              credentials, 
                              &resp_lookup,
                              PVFS2_LOOKUP_LINK_FOLLOW, NULL);
        if (ret < 0)
        {
            PVFS_perror("PVFS_sys_lookup", ret);
            return (-1);
        }
        ref.handle = resp_lookup.ref.handle;
        ref.fs_id = resp_lookup.ref.fs_id;

        memset(&resp_getattr, 0, sizeof(PVFS_sysresp_getattr));
        ret = PVFS_sys_getattr(ref, PVFS_ATTR_SYS_ALL_NOHINT,
                               credentials, &resp_getattr, NULL);
        if (ret)
        {
            fprintf(stderr, "Failed to do pvfs2 getattr on %s\n",
                    obj->u.pvfs2.pvfs2_path);
            return -1;
        }

        if (resp_getattr.attr.objtype != PVFS_TYPE_METAFILE &&
            resp_getattr.attr.objtype != PVFS_TYPE_DIRECTORY)
        {
            fprintf(stderr, "Not a meta file!\n");
            return -1;
        }
        obj->u.pvfs2.perms = resp_getattr.attr.perms;
        memcpy(&obj->u.pvfs2.attr, &resp_getattr.attr,
               sizeof(PVFS_sys_attr));
        /* we should not modify the returned mask, so we know which data fields
         * in the attribute structure are valid.  I don't see any reason why
         * it is being reset here.
        */
        //obj->u.pvfs2.attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;
        obj->u.pvfs2.ref = ref;
    }
    return 0;
}

static int eattr_is_prefixed(char* key_name)
{
    int i = 0;
    while(PINT_eattr_namespaces[i])
    {
        if(strncmp(PINT_eattr_namespaces[i], key_name,
            strlen(PINT_eattr_namespaces[i])) == 0)
        {
            return(1);
        }
        i++;
    }
    return(0);
}



/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

