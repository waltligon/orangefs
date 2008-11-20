/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* pvfs2-viewdist
 *  View the distribution information of a file	
 *  by using extended attributes!
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libgen.h>
#include <getopt.h>

#define __PINT_REQPROTO_ENCODE_FUNCS_C
#include "pvfs2.h"
#include "str-utils.h"
#include "xattr-utils.h"
#include "pint-sysint-utils.h"
#include "pint-distribution.h"
#include "pvfs2-dist-basic.h"
#include "pvfs2-dist-simple-stripe.h"
#include "pvfs2-dist-varstrip.h"
#include "pint-util.h"
#include "pvfs2-internal.h"

#ifdef HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#endif

#ifdef HAVE_ATTR_XATTR_H
#include <attr/xattr.h>
#endif

struct options 
{
    char *srcfile;
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
static void usage(int argc, char** argv);
static int resolve_filename(file_object *obj, char *filename);
static int generic_open(file_object *obj, PVFS_credentials *credentials);
static int generic_server_location(file_object *obj, PVFS_credentials *creds,
        char **servers, PVFS_handle *handles, int *nservers);

/* metafile distribution */
#define DIST_KEY "system.pvfs2." METAFILE_DIST_KEYSTR
/* datafile handles */
#define DFILE_KEY "system.pvfs2." DATAFILE_HANDLES_KEYSTR

static int generic_dist(file_object *obj, PVFS_credentials *creds,
        char **dist, int *size)
{
    char *buffer = (char *) malloc(4096);
    int ret;

    if (obj->fs_type == UNIX_FILE)
    {
#ifndef HAVE_FGETXATTR_EXTRA_ARGS
        if ((ret = fgetxattr(obj->u.ufs.fd, DIST_KEY, buffer, 4096)) < 0)
#else
        if ((ret = fgetxattr(obj->u.ufs.fd, DIST_KEY, buffer, 4096, 0, 0)) < 0)
#endif
        {
            perror("fgetxattr:");
            return -1;
        }
        *size = ret;
    }
    else
    {
        PVFS_ds_keyval key, val;

        key.buffer = DIST_KEY;
        key.buffer_sz = strlen(DIST_KEY) + 1;
        val.buffer = buffer;
        val.buffer_sz = 4096;
        if ((ret = PVFS_sys_geteattr(obj->u.pvfs2.ref,
                creds, &key, &val, NULL)) < 0)
        {
            PVFS_perror("PVFS_sys_geteattr", ret);
            return -1;
        }
        *size = val.read_sz;
    }
    *dist = buffer;
    return 0;
}

/*
 * nservers is an in-out style parameter
 * servers is allocated memory upto *nservers and each element inside that
 * is allocated internally in this function.
 * callers job is to free up all the memory
 */
static int generic_server_location(file_object *obj, PVFS_credentials *creds,
        char **servers, PVFS_handle *handles, int *nservers)
{
    char *buffer = (char *) malloc(4096);
    int ret, num_dfiles, count;
    PVFS_fs_id fsid;

    if (obj->fs_type == UNIX_FILE)
    {
#ifndef HAVE_FGETXATTR_EXTRA_ARGS
        if ((ret = fgetxattr(obj->u.ufs.fd, DFILE_KEY, buffer, 4096)) < 0)
#else
        if ((ret = fgetxattr(obj->u.ufs.fd, DFILE_KEY, buffer, 4096, 0, 0)) < 0)
#endif
        {
            perror("fgetxattr:");
            return -1;
        }
        fsid = obj->u.ufs.fs_id;
    }
    else
    {
        PVFS_ds_keyval key, val;

        key.buffer = DFILE_KEY;
        key.buffer_sz = strlen(DFILE_KEY) + 1;
        val.buffer = buffer;
        val.buffer_sz = 4096;
        if ((ret = PVFS_sys_geteattr(obj->u.pvfs2.ref,
                creds, &key, &val, NULL)) < 0)
        {
            PVFS_perror("PVFS_sys_geteattr", ret);
            return -1;
        }
        ret = val.read_sz;
        fsid = obj->u.pvfs2.fs_id;
    }
    /*
     * At this point, we know all the dfile handles
     */
    num_dfiles = (ret / sizeof(PVFS_handle));
    count = num_dfiles < *nservers ? num_dfiles : *nservers;
    for (ret = 0; ret < count; ret++)
    {
        PVFS_handle *ptr = (PVFS_handle *) ((char *) buffer + ret * sizeof(PVFS_handle));
        servers[ret] = (char *) calloc(1, PVFS_MAX_SERVER_ADDR_LEN);
        handles[ret] = *ptr;
        if (servers[ret] == NULL)
        {
            break;
        }
        /* ignore any errors */
        PINT_cached_config_get_server_name(
                servers[ret], PVFS_MAX_SERVER_ADDR_LEN,
                *ptr, fsid);
    }
    if (ret != count)
    {
        int j;
        for (j = 0; j < ret; j++)
        {
            free(servers[j]);
            servers[j] = NULL;
        }
        return -1;
    }
    *nservers = count;
    return 0;
}

int main(int argc, char ** argv)
{
    struct options* user_opts = NULL;
    file_object src;
    PINT_dist *dist;
    char *dist_buf = NULL;
    int dist_size;
    int64_t ret;
    PVFS_credentials credentials;
    char *servers[256];
    PVFS_handle handles[256];
    char metadataserver[256];
    int i, nservers = 256;

    memset(&dist, 0, sizeof(dist));
    user_opts = parse_args(argc, argv);
    if (!user_opts)
    {
	fprintf(stderr, "Error, failed to parse command line arguments\n");
	return(-1);
    }

    ret = PVFS_util_init_defaults();
    if (ret < 0)
    {
	PVFS_perror("PVFS_util_init_defaults", ret);
	return(-1);
    }
    memset(&src, 0, sizeof(src));

    resolve_filename(&src,  user_opts->srcfile);

    PVFS_util_gen_credentials(&credentials);

    ret = generic_open(&src, &credentials);
    if (ret < 0)
    {
	fprintf(stderr, "Could not open %s\n", user_opts->srcfile);
	goto main_out;
    }

    ret = generic_dist(&src, &credentials, &dist_buf, &dist_size);
    if (ret < 0)
    {
        fprintf(stderr, "Could not read distribution information!\n");
        goto main_out;
    }
    ret = generic_server_location(&src, &credentials, servers, handles, &nservers);
    if (ret < 0)
    {
        fprintf(stderr, "Could not read server location information!\n");
        goto main_out;
    }
    /* okay now print out by deserializing the buffer */
    PINT_dist_decode(&dist, dist_buf);
    printf("dist_name = %s\n", dist->dist_name);
    printf("dist_params:\n%s\n", dist->methods->params_string(dist->params));
    PINT_dist_free(dist);


    ret = PINT_cached_config_get_server_name(metadataserver, 256,
        src.u.pvfs2.ref.handle, src.u.pvfs2.ref.fs_id);
    if( ret != 0)
    {
        fprintf(stderr, "Error, could not get metadataserver name\n");
        return (-1);
    }
    printf("Metadataserver: %s\n", metadataserver);

    printf("Number of datafiles/servers = %d\n", nservers);
    for (i = 0; i < nservers; i++)
    {
        printf("Datafile %d - %s, handle: %llu (%08llx.bstream)\n", i, servers[i],
            llu(handles[i]), llu(handles[i]));
        free(servers[i]);
    }
main_out:
    PVFS_sys_finalize();
    free(user_opts);
    return(ret);
}

/* parse_args()
 *
 * parses command line arguments
 *
 * returns pointer to options structure on success, NULL on failure
 */
static struct options* parse_args(int argc, char* argv[])
{
    char flags[] = "vf:";
    int one_opt = 0;

    struct options* tmp_opts = NULL;

    /* create storage for the command line options */
    tmp_opts = (struct options*)malloc(sizeof(struct options));
    if(!tmp_opts){
	return(NULL);
    }
    memset(tmp_opts, 0, sizeof(struct options));

    /* look at command line arguments */
    while((one_opt = getopt(argc, argv, flags)) != EOF)
    {
	switch(one_opt){
            case('v'):
                printf("%s\n", PVFS2_VERSION);
                exit(0);
            case('f'):
                tmp_opts->srcfile = optarg;
                break;
	    case('?'):
		usage(argc, argv);
		exit(EXIT_FAILURE);
	}
    }
    if (tmp_opts->srcfile == NULL)
    {
        usage(argc, argv);
        exit(EXIT_FAILURE);
    }
    return(tmp_opts);
}

static void usage(int argc, char** argv)
{
    fprintf(stderr, 
	"Usage: %s ARGS \n", argv[0]);
    fprintf(stderr, "Where ARGS is one or more of"
	"\n-f <file name>\t\t\tPVFS2 file pathname"
	"\n-v\t\t\t\tprint version number and exit\n");
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

        if (resp_getattr.attr.objtype != PVFS_TYPE_METAFILE)
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

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
