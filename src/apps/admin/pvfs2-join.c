/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* pvfs2-join
 *  join two (or multiple) files into one file
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

int debug = 0;
int rmsrc2 = 0; 

struct options 
{
    char *srcfile1;
    char *srcfile2;
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
static int generic_get_server_location(file_object *obj, PVFS_credentials *creds,
        char **servers, PVFS_handle *handles, int *nservers);
static int generic_set_server_location(file_object *obj, PVFS_credentials *creds,
                                       char **servers1, char **servers2, PVFS_handle *handles1, PVFS_handle *handles2, int *nservers1, int *nservers2);

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
static int generic_get_server_location(file_object *obj, 
                                       PVFS_credentials *creds,
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

static int generic_set_server_location(file_object *obj, PVFS_credentials *creds, char **servers1, char **servers2, PVFS_handle *handles1, PVFS_handle *handles2, int *nservers1, int *nservers2)
{
    int ret;
    PVFS_ds_keyval key, val;
    int flags = 0;
    PVFS_handle *buffer = (PVFS_handle *) malloc(sizeof(PVFS_handle)*(*nservers1+*nservers2));
    memset(buffer, 0, sizeof(PVFS_handle)*(*nservers1+*nservers2));

    memcpy(buffer, handles1, sizeof(PVFS_handle)*(*nservers1));
    memcpy((char *)buffer+sizeof(PVFS_handle)*(*nservers1), handles2, sizeof(PVFS_handle)*(*nservers2));
    key.buffer = DFILE_KEY;
    key.buffer_sz = strlen(DFILE_KEY) + 1;
    val.buffer = buffer;
    /* NOTE: sizeof(PVFS_handle) => 8 */
    val.buffer_sz = (sizeof(PVFS_handle)*(*nservers1+*nservers2));
    // TODO: replace flag doesn't work
    //       TROVE:DBPF:Berkeley DB: DB->get: DB_NOTFOUND: No matching key/data pair found
    flags |= PVFS_XATTR_REPLACE;
    ret = PVFS_sys_seteattr(obj->u.pvfs2.ref, creds, &key, &val, flags, NULL);
    
    if (ret < 0)
    {
        PVFS_perror("PVFS_sys_seteattr", ret);
        return -1;
    }

    return 0;
}

int main(int argc, char ** argv)
{
    struct options* user_opts = NULL;
    file_object src1, src2;
    PINT_dist *dist1, *dist2;
    char *dist_buf1 = NULL, *dist_buf2 = NULL;
    int dist_size1, dist_size2;
    int64_t ret, num_segs;
    PVFS_credentials credentials;
    char *servers1[256], *servers2[256];
    PVFS_handle handles1[256], handles2[256];
    char metadataserver[256], directory[PVFS_NAME_MAX];
    int i, nservers1 = 256, nservers2 = 256;
    PVFS_sysresp_lookup resp_lookup;
    PVFS_object_ref parent_ref;
    char filename[PVFS_SEGMENT_MAX];

    memset(&dist1, 0, sizeof(dist1));
    memset(&dist2, 0, sizeof(dist2));

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
    memset(&src1, 0, sizeof(src1));
    memset(&src2, 0, sizeof(src2));

    resolve_filename(&src1,  user_opts->srcfile1);
    resolve_filename(&src2,  user_opts->srcfile2);

    PVFS_util_gen_credentials(&credentials);

    ret = generic_open(&src1, &credentials);
    if (ret < 0)
    {
	fprintf(stderr, "Could not open %s\n", user_opts->srcfile1);
	goto main_out;
    }

    ret = generic_open(&src2, &credentials);
    if (ret < 0)
    {
	fprintf(stderr, "Could not open %s\n", user_opts->srcfile2);
	goto main_out;
    }

    ret = generic_dist(&src1, &credentials, &dist_buf1, &dist_size1);
    if (ret < 0)
    {
        fprintf(stderr, "Could not read distribution information!\n");
        goto main_out;
    }
    ret = generic_dist(&src2, &credentials, &dist_buf2, &dist_size2);
    if (ret < 0)
    {
        fprintf(stderr, "Could not read distribution information!\n");
        goto main_out;
    }

    ret = generic_get_server_location(&src1, &credentials, servers1, handles1, &nservers1);
    if (ret < 0)
    {
        fprintf(stderr, "Could not read server location information!\n");
        goto main_out;
    }
    
    ret = generic_get_server_location(&src2, &credentials, servers2, handles2, &nservers2);
    if (ret < 0)
    {
        fprintf(stderr, "Could not read server location information!\n");
        goto main_out;
    }

    /* okay now print out by deserializing the buffer */
    PINT_dist_decode(&dist1, dist_buf1);
    if (debug)
    {
        printf("file1 = %s\n", src1.u.pvfs2.user_path);
        printf("dist_name = %s\n", dist1->dist_name);
        printf("dist_params: %s\n", dist1->methods->params_string(dist1->params));
    }
    PINT_dist_free(dist1);

    /* okay now print out by deserializing the buffer */
    PINT_dist_decode(&dist2, dist_buf2);
    if (debug)
    {
        printf("file2 = %s\n", src2.u.pvfs2.user_path);
        printf("dist_name = %s\n", dist2->dist_name);
        printf("dist_params: %s\n", dist2->methods->params_string(dist2->params));

    }
    PINT_dist_free(dist2);

    ret = PINT_cached_config_get_server_name(metadataserver, 256,
        src1.u.pvfs2.ref.handle, src1.u.pvfs2.ref.fs_id);
    if( ret != 0)
    {
        fprintf(stderr, "Error, could not get metadataserver name\n");
        return (-1);
    }
    if (debug) printf("file1: Metadataserver: %s\n", metadataserver);

    ret = PINT_cached_config_get_server_name(metadataserver, 256,
        src2.u.pvfs2.ref.handle, src2.u.pvfs2.ref.fs_id);
    if( ret != 0)
    {
        fprintf(stderr, "Error, could not get metadataserver name\n");
        return (-1);
    }
    if (debug) printf("file2: Metadataserver: %s\n", metadataserver);

    if (debug) printf("file1: Number of datafiles/servers = %d\n", nservers1);
    for (i = 0; i < nservers1; i++)
    {
        printf("Datafile %d - %s, handle: %llu (%08llx.bstream)\n", i, servers1[i],
            llu(handles1[i]), llu(handles1[i]));
        free(servers1[i]);
    }

    if (debug) printf("file2: Number of datafiles/servers = %d\n", nservers2);
    for (i = 0; i < nservers2; i++)
    {
        printf("Datafile %d - %s, handle: %llu (%08llx.bstream)\n", i, servers2[i],
            llu(handles2[i]), llu(handles2[i]));
        free(servers2[i]);
    }

    /* now join file1 and file2 */
    ret = generic_set_server_location(&src1, &credentials, servers1, servers2, handles1, handles2, &nservers1, &nservers2);
    if (ret < 0)
    {
        fprintf(stderr, "Could not set server location information!\n");
        goto main_out;
    }

    /* now it's time to update dfile_count attribute */
    if (debug) printf("dfile_count (file1) = %d\n", src1.u.pvfs2.attr.dfile_count);
    src1.u.pvfs2.attr.dfile_count = nservers1+nservers2;
    // TODO: current mask is temporary solution?
    src1.u.pvfs2.attr.mask = (PVFS_ATTR_SYS_ALL_TIMES);
    ret = PVFS_sys_setattr(src1.u.pvfs2.ref, src1.u.pvfs2.attr, &credentials, NULL);
    if (ret)
    {
        PVFS_perror("PVFS_sys_setattr", ret);
        ret = -1;
        goto main_out;
    }

    /* TODO: not working properly yet */
    /* since file2 is of no use, remove it from direntry */
    if (rmsrc2 == 1)
    {
        ret = PINT_get_base_dir(user_opts->srcfile2, directory, PVFS_NAME_MAX);
        if (ret)
        {
            PVFS_perror("PINT_get_base_dir", ret);
            ret = -1;
            goto main_out;
        }

        if(debug)
            printf("directory=%s\n", directory);

        num_segs = PINT_string_count_segments(src2.u.pvfs2.user_path);
        ret = PINT_get_path_element(src2.u.pvfs2.user_path, num_segs-1,
                                    filename, PVFS_SEGMENT_MAX);

        memset(&resp_lookup, 0, sizeof(PVFS_sysresp_lookup));
        ret = PVFS_sys_lookup(src2.u.pvfs2.fs_id, directory,
                              &credentials, &resp_lookup, 
                              PVFS2_LOOKUP_LINK_NO_FOLLOW, NULL);
        if (ret)
        {
            PVFS_perror("PVFS_sys_lookup", ret);
            ret = -1;
            goto main_out;
        }

        parent_ref = resp_lookup.ref;
        ret = PVFS_sys_remove(filename, parent_ref,
                              &credentials, NULL);
        if (ret)
        {
            fprintf(stderr, "Error: An error occurred while "
                    "removing %s\n", user_opts->srcfile2);
            PVFS_perror("PVFS_sys_remove", ret);
            ret = -1;
            goto main_out;
        }
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
    char flags[] = ":dr";
    int one_opt = 0, i = 0;
    extern char *optarg;
    extern int optind, optopt;

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
            case('d'):
                debug = 1;
                break;
            case('r'):
                rmsrc2 = 1;
                break;
	    case('?'):
		usage(argc, argv);
		exit(EXIT_FAILURE);
	}
    }

    /* TODO: use common way of parsing regular argument */
    for( ; optind < argc; optind++)
    {
        if (i==0)
            tmp_opts->srcfile1 = argv[optind];
        else if (i==1)
            tmp_opts->srcfile2 = argv[optind];
        i++;
    }

    if (tmp_opts->srcfile1 == NULL || tmp_opts->srcfile2 == NULL)
    {
        usage(argc, argv);
        exit(EXIT_FAILURE);
    }

    //printf("src file1=%s, file2=%s\n", tmp_opts->srcfile1, tmp_opts->srcfile2);
    return(tmp_opts);
}

static void usage(int argc, char** argv)
{
    fprintf(stderr, 
	"Usage: %s ARGS \n", argv[0]);
    fprintf(stderr, "Where ARGS is one or more of"
	"\n src1 src2 \t\t\tPVFS2 file pathname"
	"\n-v\t\t\t\t turn-on debug mode"
        "\n-r\t\t\t\t remove src2\n");
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
            PVFS_perror("PVFS_sys_getattr", ret);
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
