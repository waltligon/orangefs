/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* pvfs2-cp: 
 * 	copy a file from a unix or PVFS2 file system to a unix or PVFS2 file
 * 	system.  Should replace pvfs2-import and pvfs2-export.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <libgen.h>

#include "pvfs2.h"
#include "str-utils.h"
#include "pint-sysint-utils.h"
#include "pvfs2-internal.h"

/* optional parameters, filled in by parse_args() */
struct options
{
    PVFS_size strip_size;
    int num_datafiles;
    int buf_size;
    char* srcfile;
    char* destfile;
    int show_timings;
};

enum object_type { 
    UNIX_FILE, 
    PVFS2_FILE 
};

enum open_type {
    OPEN_SRC,
    OPEN_DEST
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
static double Wtime(void);
static void print_timings( double time, int64_t total);
static int resolve_filename(file_object *obj, char *filename);
static int generic_open(file_object *obj, PVFS_credentials *credentials, 
	int nr_datafiles, PVFS_size strip_size, char *srcname, int open_type);
static size_t generic_read(file_object *src, char *buffer, 
	int64_t offset, size_t count, PVFS_credentials *credentials);
static size_t generic_write(file_object *dest, char *buffer, 
	int64_t offset, size_t count, PVFS_credentials *credentials);
static int generic_cleanup(file_object *src, file_object *dest,
                           PVFS_credentials *credentials);
static void make_attribs(PVFS_sys_attr *attr,
                         PVFS_credentials *credentials, 
                         int nr_datafiles, int mode);

static int convert_pvfs2_perms_to_mode(PVFS_permissions perms)
{
    int ret = 0, i = 0;
    static int modes[9] =
    {
        S_IXOTH, S_IWOTH, S_IROTH,
        S_IXGRP, S_IWGRP, S_IRGRP,
        S_IXUSR, S_IWUSR, S_IRUSR
    };
    static int pvfs2_modes[9] =
    {
        PVFS_O_EXECUTE, PVFS_O_WRITE, PVFS_O_READ,
        PVFS_G_EXECUTE, PVFS_G_WRITE, PVFS_G_READ,
        PVFS_U_EXECUTE, PVFS_U_WRITE, PVFS_U_READ,
    };

    for(i = 0; i < 9; i++)
    {
        if (perms & pvfs2_modes[i])
        {
            ret |= modes[i];
        }
    }
    return ret;
}

int main (int argc, char ** argv)
{
    struct options* user_opts = NULL;
    double time1=0, time2=0;
    int current_size=0;
    int64_t total_written=0, buffer_size=0;
    file_object src, dest;
    void* buffer = NULL;
    int64_t ret;
    PVFS_credentials credentials;

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
    memset(&dest, 0, sizeof(src));

    resolve_filename(&src,  user_opts->srcfile );
    resolve_filename(&dest, user_opts->destfile);

    PVFS_util_gen_credentials(&credentials);

    ret = generic_open(&src, &credentials, 0, 0, NULL, OPEN_SRC);
    if (ret < 0)
    {
	fprintf(stderr, "Could not open %s\n", user_opts->srcfile);
	goto main_out;
    }

    ret = generic_open(&dest, &credentials, user_opts->num_datafiles, user_opts->strip_size,
                       user_opts->srcfile, OPEN_DEST);

    if (ret < 0)
    {
	fprintf(stderr, "Could not open %s\n", user_opts->destfile);
	goto main_out;
    }

    /* start moving data */
    buffer = malloc(user_opts->buf_size);
    if(!buffer)
    {
	perror("malloc");
	ret = -1;
	goto main_out;
    }

    time1 = Wtime();
    while((current_size = generic_read(&src, buffer, 
		    total_written, user_opts->buf_size, &credentials)) > 0)
    {
	buffer_size = current_size;
	
	ret = generic_write(&dest, buffer, total_written, 
		buffer_size, &credentials);
	if (ret != current_size)
	{
	    if (ret == -1) {
		perror("generic_write");
	    } else {
		fprintf(stderr, "Error in write\n");
	    }
	    ret = -1;
	    goto main_out;
	}
	total_written += current_size;
    }

    time2 = Wtime();

    if (user_opts->show_timings) 
    {
	print_timings(time2-time1, total_written);
    }
    ret = 0;

main_out:
    generic_cleanup(&src, &dest, &credentials);
    PVFS_sys_finalize();
    free(user_opts);
    free(buffer);
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
    /* getopt stuff */
    extern char* optarg;
    extern int optind, opterr, optopt;
    char flags[] = "tvs:n:b:";
    int one_opt = 0;

    struct options* tmp_opts = NULL;
    int ret = -1;

    /* create storage for the command line options */
    tmp_opts = (struct options*)malloc(sizeof(struct options));
    if(!tmp_opts){
	return(NULL);
    }
    memset(tmp_opts, 0, sizeof(struct options));

    /* fill in defaults (except for hostid) */
    tmp_opts->strip_size = -1;
    tmp_opts->num_datafiles = -1;
    tmp_opts->buf_size = 10*1024*1024;

    /* look at command line arguments */
    while((one_opt = getopt(argc, argv, flags)) != EOF)
    {
	switch(one_opt){
            case('v'):
                printf("%s\n", PVFS2_VERSION);
                exit(0);
	    case('t'):
		tmp_opts->show_timings = 1;
		break;
	    case('s'):
		ret = sscanf(optarg, SCANF_lld, &tmp_opts->strip_size);
		if(ret < 1){
		    free(tmp_opts);
		    return(NULL);
		}
		break;
	    case('n'):
		ret = sscanf(optarg, "%d", &tmp_opts->num_datafiles);
		if(ret < 1){
		    free(tmp_opts);
		    return(NULL);
		}
		break;
	    case('b'):
		ret = sscanf(optarg, "%d", &tmp_opts->buf_size);
		if(ret < 1){
		    free(tmp_opts);
		    return(NULL);
		}
		break;
	    case('?'):
		usage(argc, argv);
		exit(EXIT_FAILURE);
	}
    }

    if(optind != (argc - 2))
    {
	usage(argc, argv);
	exit(EXIT_FAILURE);
    }

    /* TODO: should probably malloc and copy instead */
    tmp_opts->srcfile = argv[argc-2];
    tmp_opts->destfile = argv[argc-1];

    return(tmp_opts);
}

static void usage(int argc, char** argv)
{
    fprintf(stderr, 
	"Usage: %s ARGS src_file dest_file\n", argv[0]);
    fprintf(stderr, "Where ARGS is one or more of"
	"\n-s <strip_size>\t\t\tsize of access to PVFS2 volume"
	"\n-n <num_datafiles>\t\tnumber of PVFS2 datafiles to use"
	"\n-b <buffer_size>\t\thow much data to read/write at once"
	"\n-t\t\t\t\tprint some timing information"
	"\n-v\t\t\t\tprint version number and exit\n");
    return;
}

static double Wtime(void)
{
    struct timeval t;
    gettimeofday(&t, NULL);
    return((double)t.tv_sec + (double)(t.tv_usec) / 1000000);
}

static void print_timings( double time, int64_t total)
{
    printf("Wrote %lld bytes in %f seconds. %f MB/seconds\n",
	    lld(total), time, (total/time)/(1024*1024));
}

/* read 'count' bytes from a (unix or pvfs2) file 'src', placing the result in
 * 'buffer' */
static size_t generic_read(file_object *src, char *buffer, 
	int64_t offset, size_t count, PVFS_credentials *credentials)
{
    PVFS_Request mem_req, file_req;
    PVFS_sysresp_io resp_io;
    int ret;

    if(src->fs_type == UNIX_FILE)
	return (read(src->u.ufs.fd, buffer, count));
    else
    {
	file_req = PVFS_BYTE;
	ret = PVFS_Request_contiguous(count, PVFS_BYTE, &mem_req);
	if (ret < 0)
	{
	    fprintf(stderr, "Error: PVFS_Request_contiguous failure\n");
	    return (ret);
	}
	ret = PVFS_sys_read(src->u.pvfs2.ref, file_req, offset,
		buffer, mem_req, credentials, &resp_io);
	if (ret == 0)
	{
            PVFS_Request_free(&mem_req);
	    return (resp_io.total_completed);
	} 
	else 
	    PVFS_perror("PVFS_sys_read", ret);
    }
    return (ret);
}

/* write 'count' bytes from 'buffer' into (unix or pvfs2) file 'dest' */
static size_t generic_write(file_object *dest, char *buffer, 
    int64_t offset, size_t count, PVFS_credentials *credentials)
{
    PVFS_Request mem_req, file_req;
    PVFS_sysresp_io resp_io;
    int ret;

    if (dest->fs_type == UNIX_FILE)
	return(write(dest->u.ufs.fd, buffer, count));
    else
    {
	file_req = PVFS_BYTE;
	ret = PVFS_Request_contiguous(count, PVFS_BYTE, &mem_req);
	if (ret < 0)
	{
	    PVFS_perror("PVFS_Request_contiguous", ret);
	    return(ret);
	}
	ret = PVFS_sys_write(dest->u.pvfs2.ref, file_req, offset,
		buffer, mem_req, credentials, &resp_io);
	if (ret == 0) 
        {
            PVFS_Request_free(&mem_req);
	    return(resp_io.total_completed);
        }
	else
	    PVFS_perror("PVFS_sys_write", ret);
    }
    return ret;
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
 *  . the 'open_type' flag tells us if we can create the file if it does not
 *    exist: if it is the source, then no.  If it is the destination, then we
 *    will.  
 *  . If we are creating the file, nr_datafiles gives us the number of
 *    datafiles to use for the new file.
 *  . If 'srcname' is given, and the file is a directory, we will create a
 *    new file with the basename of srcname in the specified directory 
 */

static int generic_open(file_object *obj, PVFS_credentials *credentials,
                        int nr_datafiles, PVFS_size strip_size, 
                        char *srcname, int open_type)
{
    struct stat stat_buf;
    PVFS_sysresp_lookup resp_lookup;
    PVFS_sysresp_getattr resp_getattr;
    PVFS_sysresp_create resp_create;
    PVFS_object_ref parent_ref;
    PVFS_sys_dist   *new_dist;
    int ret = -1;
    char *entry_name;		    /* name of the pvfs2 file */
    char str_buf[PVFS_NAME_MAX];    /* basename of pvfs2 file */

    if (obj->fs_type == UNIX_FILE)
    {
        memset(&stat_buf, 0, sizeof(struct stat));

        stat(obj->u.ufs.path, &stat_buf);
	if (open_type == OPEN_SRC)
	{
	    if (S_ISDIR(stat_buf.st_mode))
	    {
		fprintf(stderr, "Source cannot be a directory\n");
		return(-1);
	    }
	    obj->u.ufs.fd = open(obj->u.ufs.path, O_RDONLY);
            obj->u.ufs.mode = (int)stat_buf.st_mode;
	}
	else
	{
	    if (S_ISDIR(stat_buf.st_mode))
	    {
		if (srcname)
                {
		    strncat(obj->u.ufs.path, basename(srcname), NAME_MAX);
                }
		else
		{
		    fprintf(stderr, "cannot find name for "
                            "destination. giving up\n");
		    return(-1);
		}
	    }
	    obj->u.ufs.fd = open(obj->u.ufs.path,
                               O_WRONLY|O_CREAT|O_LARGEFILE|O_TRUNC,0666);
	}
	if (obj->u.ufs.fd < 0)
	{
	    perror("open");
	    fprintf(stderr, "could not open %s\n", obj->u.ufs.path);
	    return (-1);
	}
    }
    else
    {
	entry_name = str_buf;
	/* it's a PVFS2 file */
	if (strcmp(obj->u.pvfs2.pvfs2_path, "/") == 0)
	{
	    /* special case: PVFS2 root file system, so stuff the end of
	     * srcfile onto pvfs2_path */
	    char *segp = NULL, *prev_segp = NULL;
	    void *segstate = NULL;
	    
	    /* can only perform this special case if we know srcname */
	    if (srcname == NULL)
	    {
		fprintf(stderr, "unable to guess filename in "
                        "toplevel PVFS2\n");
		return -1;
	    }

	    memset(&resp_lookup, 0, sizeof(PVFS_sysresp_lookup));
	    ret = PVFS_sys_lookup(obj->u.pvfs2.fs_id, obj->u.pvfs2.pvfs2_path,
                                  credentials, &resp_lookup,
                                  PVFS2_LOOKUP_LINK_FOLLOW);
	    if (ret < 0)
	    {
		PVFS_perror("PVFS_sys_lookup", ret);
		return (-1);
	    }
	    parent_ref.handle = resp_lookup.ref.handle;
	    parent_ref.fs_id = resp_lookup.ref.fs_id;

	    while (!PINT_string_next_segment(srcname, &segp, &segstate))
	    {
		prev_segp = segp;
	    }
	    entry_name = prev_segp; /* see... points to basename of srcname */
	}
	else /* given either a pvfs2 directory or a pvfs2 file */
	{
	    /* get the absolute path on the pvfs2 file system */
	    
	    /*parent_ref.fs_id = obj->pvfs2.fs_id; */

	    if (PINT_remove_base_dir(obj->u.pvfs2.pvfs2_path,str_buf, 
                                     PVFS_NAME_MAX))
	    {
		if(obj->u.pvfs2.pvfs2_path[0] != '/')
		{
		    fprintf(stderr, "Error: poorly formatted path.\n");
		}
		fprintf(stderr, "Error: cannot retrieve entry name for "
			"creation on %s\n", obj->u.pvfs2.user_path);
		return(-1);
	    }
	    ret = PINT_lookup_parent(obj->u.pvfs2.pvfs2_path, 
                                     obj->u.pvfs2.fs_id, credentials,
                                     &parent_ref.handle);
	    if (ret < 0)
	    {
		PVFS_perror("PVFS_util_lookup_parent", ret);
		return (-1);
	    }
	    else /* parent lookup succeeded. if the pvfs2 path is just a
		    directory, use basename of src for the new file */
	    {
		int len = strlen(obj->u.pvfs2.pvfs2_path);
		if (obj->u.pvfs2.pvfs2_path[len - 1] == '/')
		{
		    char *segp = NULL, *prev_segp = NULL;
		    void *segstate = NULL;

		    if (srcname == NULL)
		    {
			fprintf(stderr, "unable to guess filename\n");
			return(-1);
		    }
		    while (!PINT_string_next_segment(srcname, 
				&segp, &segstate))
		    {
			prev_segp = segp;
		    }
		    strncat(obj->u.pvfs2.pvfs2_path, prev_segp, PVFS_NAME_MAX);
		    entry_name = prev_segp;
		}
		parent_ref.fs_id = obj->u.pvfs2.fs_id;
	    }
	}

	memset(&resp_lookup, 0, sizeof(PVFS_sysresp_lookup));
	ret = PVFS_sys_ref_lookup(parent_ref.fs_id, entry_name,
                                  parent_ref, credentials, &resp_lookup,
                                  PVFS2_LOOKUP_LINK_FOLLOW);

        if ((ret == 0) && (open_type == OPEN_SRC))
        {
            memset(&resp_getattr, 0, sizeof(PVFS_sysresp_getattr));
            ret = PVFS_sys_getattr(resp_lookup.ref, PVFS_ATTR_SYS_ALL,
                                   credentials, &resp_getattr);
            if (ret)
            {
                fprintf(stderr, "Failed to do pvfs2 getattr on %s\n",
                        entry_name);
                return -1;
            }

            if (resp_getattr.attr.objtype == PVFS_TYPE_SYMLINK)
            {
                free(resp_getattr.attr.link_target);
                resp_getattr.attr.link_target = NULL;
            }
            obj->u.pvfs2.perms = resp_getattr.attr.perms;
            memcpy(&obj->u.pvfs2.attr, &resp_getattr.attr,
                   sizeof(PVFS_sys_attr));
            obj->u.pvfs2.attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;
        }

	/* at this point, we have looked up the file in the parent directory.
	 * . If we found something, and we are the SRC, then we're done. 
	 * . We will maintain the semantic of pvfs2-import and refuse to
	 *   overwrite existing PVFS2 files, so if we found something, and we
	 *   are the DEST, then that's an error.  
	 * . Otherwise, we found nothing and we will create the destination. 
	 */
	if (open_type == OPEN_SRC)
	{
	    if (ret == 0)
	    {
		obj->u.pvfs2.ref = resp_lookup.ref;
		return 0;
	    }
	    else
	    {
		PVFS_perror("PVFS_sys_ref_lookup", ret);
		return (ret);
	    }
	}
	if (open_type == OPEN_DEST)
	{
	    if (ret == 0)
	    {
		fprintf(stderr, "Target file %s already exists\n", entry_name);
		return (-1);
	    } 
	    else 
	    {
                memset(&stat_buf, 0, sizeof(struct stat));

                /* preserve permissions doing a unix => pvfs2 copy */
                stat(srcname, &stat_buf);
		make_attribs(&(obj->u.pvfs2.attr), credentials, nr_datafiles,
                             (int)stat_buf.st_mode);
                if (strip_size > 0) {
                    new_dist = PVFS_sys_dist_lookup("simple_stripe");
                    ret = PVFS_sys_dist_setparam(new_dist, "strip_size", &strip_size);
                    if (ret < 0)
                    {
                       PVFS_perror("PVFS_sys_dist_setparam", ret); 
		       return -1; 
                    }
                }
                else {
                    new_dist=NULL;
                }
            
		ret = PVFS_sys_create(entry_name, parent_ref, 
                                      obj->u.pvfs2.attr, credentials,
                                      new_dist, &resp_create);
		if (ret < 0)
		{
		    PVFS_perror("PVFS_sys_create", ret); 
		    return -1; 
		}
		obj->u.pvfs2.ref = resp_create.ref;
	    }
	}
    }
    return 0;
}

static int generic_cleanup(file_object *src, file_object *dest,
                           PVFS_credentials *credentials)
{
    /* preserve permissions doing a pvfs2 => unix copy */
    if ((src->fs_type != UNIX_FILE) &&
        ((dest->fs_type == UNIX_FILE) && (dest->u.ufs.fd != -1)))
    {
        fchmod(dest->u.ufs.fd,
               convert_pvfs2_perms_to_mode(src->u.pvfs2.perms));
    }

    /* preserve permissions doing a unix => unix copy */
    if ((src->fs_type == UNIX_FILE) &&
        ((dest->fs_type == UNIX_FILE) && (dest->u.ufs.fd != -1)))
    {
        fchmod(dest->u.ufs.fd, src->u.ufs.mode);
    }

    /* preserve permissions doing a pvfs2 => pvfs2 copy */
    if ((src->fs_type != UNIX_FILE) && (dest->fs_type != UNIX_FILE))
    {
        PVFS_sys_setattr(dest->u.pvfs2.ref, src->u.pvfs2.attr, credentials);
    }

    if ((src->fs_type == UNIX_FILE) && (src->u.ufs.fd != -1))
    {
        close(src->u.ufs.fd);
    }

    if ((dest->fs_type == UNIX_FILE) && (dest->u.ufs.fd != -1))
    {
        close(dest->u.ufs.fd);
    }
    return 0;
}

void make_attribs(PVFS_sys_attr *attr, PVFS_credentials *credentials,
                  int nr_datafiles, int mode)
{
    attr->owner = credentials->uid; 
    attr->group = credentials->gid;
    attr->perms = PVFS2_translate_mode(mode);
    attr->atime = time(NULL);
    attr->mtime = attr->atime;
    attr->ctime = attr->atime;
    attr->mask = (PVFS_ATTR_SYS_ALL_SETABLE);
    attr->dfile_count = nr_datafiles;

    if (attr->dfile_count > 0)
    {
	attr->mask |= PVFS_ATTR_SYS_DFILE_COUNT;
    }
}    

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
