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

#include "pvfs2.h"
#include "str-utils.h"
#include "pint-sysint-utils.h"

#ifndef PVFS2_VERSION
#define PVFS2_VERSION "Unknown"
#endif

/* optional parameters, filled in by parse_args() */
struct options
{
    PVFS_permissions perms;
    int target_count;
    char** destfiles;
};

static struct options* parse_args(int argc, char* argv[]);
int pvfs2_chmod(PVFS_permissions perms, char *destfile);
static void usage(int argc, char** argv);
int check_perm(char c);

int main(int argc, char **argv)
{
  int ret = 0;
  struct options* user_opts = NULL;
  int i;

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

  /*
   * for each file the user specified
   */
  for (i = 0; i < user_opts->target_count; i++) {
    ret = pvfs2_chmod(user_opts->perms,user_opts->destfiles[i]);
    if (ret != 0) {
      break;
    }
    /* TODO: need to free the request descriptions */
  }
  PVFS_sys_finalize();
  return(ret);
}

/* pvfs2_chmod()
 *
 * changes the mode of the given file to the given permissions
 *
 * returns zero on success and negative one on failure
 */
int pvfs2_chmod (PVFS_permissions perms, char *destfile) {
  int ret = -1;
  char str_buf[PVFS_NAME_MAX] = {0};
  char pvfs_path[PVFS_NAME_MAX] = {0};
  PVFS_fs_id cur_fs;
  PVFS_sysresp_lookup resp_lookup;
  PVFS_object_ref parent_ref;
  PVFS_credentials credentials;
  PVFS_sysresp_getattr resp_getattr;
  PVFS_sys_attr old_attr;
  PVFS_sys_attr new_attr;
  uint32_t attrmask;
  /* translate local path into pvfs2 relative path */
  ret = PVFS_util_resolve(destfile,&cur_fs, pvfs_path, PVFS_NAME_MAX);
  if(ret < 0)
  {
    PVFS_perror("PVFS_util_resolve", ret);
    return -1;
  }

  PVFS_util_gen_credentials(&credentials);

  /* this if-else statement just pulls apart the pathname into its
   * parts....I think...this should be a function somewhere
   */
  if (strcmp(pvfs_path,"/") == 0)
  {
    memset(&resp_lookup, 0, sizeof(PVFS_sysresp_lookup));
    ret = PVFS_sys_lookup(cur_fs, pvfs_path,
                          &credentials, &resp_lookup,
                          PVFS2_LOOKUP_LINK_FOLLOW);
    if (ret < 0)
    {
      PVFS_perror("PVFS_sys_lookup", ret);
      return -1;
    }
    parent_ref.handle = resp_lookup.ref.handle;
    parent_ref.fs_id = resp_lookup.ref.fs_id;
  }
  else
  {
    /* get the absolute path on the pvfs2 file system */
    if (PINT_remove_base_dir(pvfs_path,str_buf,PVFS_NAME_MAX))
    {
      if (pvfs_path[0] != '/')
      {
        fprintf(stderr, "Error: poorly formatted path.\n");
      }
      fprintf(stderr, "Error: cannot retrieve entry name for "
              "creation on %s\n",pvfs_path);
      return -1;
    }

    ret = PINT_lookup_parent(pvfs_path, cur_fs, &credentials, 
                                  &parent_ref.handle);
    if(ret < 0)
    {
      PVFS_perror("PINT_lookup_parent", ret);
      return -1;
    }
    else
    {
      parent_ref.fs_id = cur_fs;
    }
  }
  memset(&resp_lookup, 0, sizeof(PVFS_sysresp_lookup));

  ret = PVFS_sys_ref_lookup(parent_ref.fs_id, str_buf,
                            parent_ref, &credentials, &resp_lookup,
                            PVFS2_LOOKUP_LINK_NO_FOLLOW);
  if (ret != 0)
  {
    fprintf(stderr, "Target '%s' does not exist!\n", str_buf);
    return -1;
  }
  memset(&resp_getattr,0,sizeof(PVFS_sysresp_getattr));
  attrmask = (PVFS_ATTR_SYS_ALL_SETABLE);
    
  ret = PVFS_sys_getattr(resp_lookup.ref,attrmask,&credentials,&resp_getattr);
  if (ret < 0) 
  {
    PVFS_perror("PVFS_sys_getattr",ret);
    return -1;
  }
  old_attr = resp_getattr.attr;
  new_attr=old_attr;

  new_attr.perms = perms;
  new_attr.mask = PVFS_ATTR_SYS_PERM;
 
  ret = PVFS_sys_setattr(resp_lookup.ref,new_attr,&credentials);
  if (ret < 0) 
  {
    PVFS_perror("PVFS_sys_setattr",ret);
    return -1;
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
    /* getopt stuff */
    extern char* optarg;
    extern int optind, opterr, optopt;
    char flags[] = "v";
    int one_opt = 0;
    int user_perms = 0;
    int group_perms = 0;
    int other_perms = 0;
    int i;

    struct options* tmp_opts = NULL;

    /* create storage for the command line options */
    tmp_opts = (struct options*)malloc(sizeof(struct options));
    if(!tmp_opts){
	return(NULL);
    }
    memset(tmp_opts, 0, sizeof(struct options));

    /* fill in defaults */
    tmp_opts->perms = 0;
    tmp_opts->target_count = 0;
    tmp_opts->destfiles = NULL;

    if (argc <= 2) {
      usage(argc,argv);
      exit(0);
    } 

    /* look at command line arguments */
    while((one_opt = getopt(argc, argv, flags)) != EOF)
    {
	switch(one_opt){
            case('v'):
                printf("%s\n", PVFS2_VERSION);
                exit(0);
	    case('?'):
                printf("?\n");
		usage(argc, argv);
		exit(EXIT_FAILURE);
	}
    }
    if (strlen(argv[optind]) != 3) {
        usage (argc, argv);
        exit(EXIT_FAILURE);
    }
    user_perms = check_perm(argv[optind][0]);
    group_perms = check_perm(argv[optind][1]);
    other_perms = check_perm(argv[optind][2]);

    if (user_perms == -1 || group_perms == -1 || other_perms == -1) {
        usage(argc,argv);
        exit(EXIT_FAILURE);
    }
    tmp_opts->perms=(user_perms << 6) | (group_perms << 3) | (other_perms << 0);

    optind = optind + 1;
    tmp_opts->target_count = argc-optind;
    tmp_opts->destfiles=(char **)malloc(sizeof(char *)*(tmp_opts->target_count));
    for (i = 0; i < tmp_opts->target_count; i++) {
      char *cur_arg_str = argv[optind+i];
      int length = strlen(cur_arg_str);
      tmp_opts->destfiles[i] = (char *)malloc(sizeof(char)*(length+1));
      strncpy(tmp_opts->destfiles[i],cur_arg_str,length+1);
    }
   
    return(tmp_opts);
}


static void usage(int argc, char** argv)
{
    fprintf(stderr,"Usage: %s [-v] mode filename(s)\n",argv[0]);
    fprintf(stderr,"    mode - of the form UGO in octal notation\n");
    fprintf(stderr,"       U - the user permissions for the file(s)\n");
    fprintf(stderr,"       G - the group permissions for the file(s)\n");
    fprintf(stderr,"       O - the other permissions for the file(s)\n");
    fprintf(stderr,"    -v - print program version and terminate.\n");
    return;
}
int check_perm(char c) {
    switch (c) {
      case '0': return 0;
      case '1': return 1;
      case '2': return 2;
      case '3': return 3;
      case '4': return 4;
      case '5': return 5;
      case '6': return 6;
      case '7': return 7;
      default: return -1;
   }
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

