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

#include "xattr-utils.h"

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
    PVFS_ds_keyval key;
    PVFS_ds_keyval val;
    char* srcfile;
    int get, text;
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
static int generic_open(file_object *obj, PVFS_credentials *credentials);
static int pvfs2_eattr(int get, file_object *, PVFS_ds_keyval *key_p,
        PVFS_ds_keyval *val_p, PVFS_credentials *creds);
static void usage(int argc, char** argv);
static int resolve_filename(file_object *obj, char *filename);
static int modify_val(PVFS_ds_keyval *key_p, PVFS_ds_keyval *val_p);
static int permit_set(PVFS_ds_keyval *key_p);
static int eattr_is_prefixed(char* key_name);

int main(int argc, char **argv)
{
  int ret = 0;
  struct options* user_opts = NULL;
  file_object src;
  PVFS_credentials credentials;

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

  PVFS_util_gen_credentials(&credentials);
  ret = generic_open(&src, &credentials);
  if (ret < 0)
  {
      fprintf(stderr, "Could not open %s\n", user_opts->srcfile);
      return -1;
  }
  if (!eattr_is_prefixed(user_opts->key.buffer))
  {
      fprintf(stderr, "extended attribute key is not prefixed %s\n", (char *) user_opts->key.buffer);
      return -1;
  }
  if (!user_opts->get)
  {
      if (!permit_set(&user_opts->key))
      {
          fprintf(stderr, "Not permitted to set key %s\n", (char *) user_opts->key.buffer);
          return -1;
      }
      if (modify_val(&user_opts->key, &user_opts->val) < 0)
      {
          fprintf(stderr, "Invalid value for user-settable hint %s, %s\n", (char *) user_opts->key.buffer, (char *) user_opts->val.buffer);
          return -1;
      }
  }

    ret = pvfs2_eattr(user_opts->get, &src, &user_opts->key, &user_opts->val, &credentials);
    if (ret != 0) 
    {
        return ret;
    }
    if (user_opts->get && user_opts->text)  
    {
        if (strncmp(user_opts->key.buffer, "user.pvfs2.meta_hint", SPECIAL_METAFILE_HINT_KEYLEN) == 0) {
            PVFS_metafile_hint *hint = (PVFS_metafile_hint *) user_opts->val.buffer;
            printf("Metafile hints: ");
            if (hint->flags & PVFS_IMMUTABLE_FL) {
                printf("immutable file ");
            }
            if (hint->flags & PVFS_APPEND_FL) {
                printf("Append-only file ");
            }
            if (hint->flags & PVFS_NOATIME_FL) {
                printf("Atime updates disabled.");
            }
            printf("\n");
        } else {
            printf("key:%s Value:\n%s\n",
                    (char *)user_opts->key.buffer,
                    (char *)user_opts->val.buffer);
        }
    }
  PVFS_sys_finalize();
  return(ret);
}

static int modify_val(PVFS_ds_keyval *key_p, PVFS_ds_keyval *val_p)
{
    if (strncmp(key_p->buffer, "user.pvfs2.meta_hint", SPECIAL_METAFILE_HINT_KEYLEN) == 0)
    {
        PVFS_metafile_hint hint;
        memset(&hint, 0, sizeof(hint));
        if (strncmp(val_p->buffer, "+immutable", 10) == 0)
            hint.flags |= PVFS_IMMUTABLE_FL;
        else if (strncmp(val_p->buffer, "-immutable", 10) == 0)
            hint.flags &= ~PVFS_IMMUTABLE_FL;
        else if (strncmp(val_p->buffer, "+append", 7) == 0)
            hint.flags |= PVFS_APPEND_FL;
        else if (strncmp(val_p->buffer, "-append", 7) == 0)
            hint.flags &= ~PVFS_APPEND_FL;
        else if (strncmp(val_p->buffer, "+noatime", 8) == 0)
            hint.flags |= PVFS_NOATIME_FL;
        else if (strncmp(val_p->buffer, "-noatime", 8) == 0)
            hint.flags &= ~PVFS_NOATIME_FL;
        else 
            return -1;
        memcpy(val_p->buffer, &hint, sizeof(hint));
        val_p->buffer_sz = sizeof(hint);
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
static int pvfs2_eattr(int get, file_object *obj, PVFS_ds_keyval *key_p,
        PVFS_ds_keyval *val_p, PVFS_credentials *creds) 
{
  int ret = -1;

  if (obj->fs_type == UNIX_FILE)
  {
      if (get == 1)
      {
#ifndef HAVE_FGETXATTR_EXTRA_ARGS
        if ((ret = fgetxattr(obj->u.ufs.fd, key_p->buffer, val_p->buffer, val_p->buffer_sz)) < 0)
#else
        if ((ret = fgetxattr(obj->u.ufs.fd, key_p->buffer, val_p->buffer, val_p->buffer_sz, 0, 0)) < 0)
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
      if (get == 1)
      {
          ret = PVFS_sys_geteattr(obj->u.pvfs2.ref, creds, key_p, val_p, NULL);
      }
      else {
          ret = PVFS_sys_seteattr(obj->u.pvfs2.ref, creds, key_p, val_p, 0, NULL);
      }

      if (ret < 0)
      {
          PVFS_perror("PVFS_sys_geteattr", ret);
          return -1;
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

    /* fill in defaults */
    memset(&tmp_opts->key, 0, sizeof(PVFS_ds_keyval));
    memset(&tmp_opts->val, 0, sizeof(PVFS_ds_keyval));
    tmp_opts->srcfile = strdup(argv[argc-1]);
    tmp_opts->get = 1;

    /* look at command line arguments */
    while((one_opt = getopt(argc, argv, flags)) != EOF)
    {
	switch(one_opt){
            case 't':
                tmp_opts->text = 1;
                break;
            case 's':
                tmp_opts->get = 0;
                break;
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
    if (tmp_opts->get == 1)
    {
        tmp_opts->val.buffer = calloc(1, VALBUFSZ);
        tmp_opts->val.buffer_sz = VALBUFSZ;
        if (tmp_opts->val.buffer == NULL)
        {
            fprintf(stderr, "Could not allocate val\n");
            exit(EXIT_FAILURE);
        }
    }
    else {
        if (tmp_opts->val.buffer == NULL)
        {
            fprintf(stderr, "Please specify value if setting extended attributes\n");
            usage(argc, argv);
            exit(EXIT_FAILURE);
        }
    }
    if (tmp_opts->key.buffer == NULL)
    {
        fprintf(stderr, "Please specify key if getting extended attributes\n");
        usage(argc, argv);
        exit(EXIT_FAILURE);
    }
    return(tmp_opts);
}


static void usage(int argc, char** argv)
{
    fprintf(stderr,"Usage: %s -s {set xattrs} -k <key> -v <val> -t {print attributes} filename\n",argv[0]);
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
static int generic_open(file_object *obj, PVFS_credentials *credentials)
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
        obj->u.pvfs2.attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;
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

