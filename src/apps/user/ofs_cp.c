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
#include <getopt.h>

#include "pvfs2.h"
#include "str-utils.h"
#include "pint-sysint-utils.h"
#include "pvfs2-internal.h"
#include "pvfs2-hint.h"
#include "pvfs2-usrint.h"

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
    UNIX_FILE = 1,
    PVFS2_FILE 
};

enum open_type {
    OPEN_SRC,
    OPEN_DEST
};

typedef struct pvfs2_file_object_s {
    PVFS_fs_id fs_id;
    PVFS_object_ref ref;
    char pvfs2_path[PVFS_NAME_MAX+1];	
    char user_path[PVFS_NAME_MAX+1];
    PVFS_sys_attr attr;
    PVFS_permissions perms;
    int fd;
    int mode;
} pvfs2_file_object;

typedef struct unix_file_object_s {
    int fd;
    int mode;
    char path[NAME_MAX+1];
} unix_file_object;

typedef struct file_object_s {
    int fs_type;
    union {
	unix_file_object ufs;
	pvfs2_file_object pvfs2;
    } u;
} file_object;

static PVFS_hint hints = NULL;

static struct options* parse_args(int argc, char* argv[]);
static void usage(int argc, char** argv);
static double Wtime(void);
static void print_timings( double time, int64_t total);
static int resolve_filename(file_object *obj, char *filename);
static int generic_open(file_object *obj, PVFS_credential *credentials, 
	int nr_datafiles, PVFS_size strip_size, char *srcname, int open_type);
static size_t generic_read(file_object *src, char *buffer, 
	int64_t offset, size_t count, PVFS_credential *credentials);
static size_t generic_write(file_object *dest, char *buffer, 
	int64_t offset, size_t count, PVFS_credential *credentials);
static int generic_cleanup(file_object *src, file_object *dest,
                           PVFS_credential *credentials);
/*static void make_attribs(PVFS_sys_attr *attr,
                         PVFS_credential *credentials, 
                         int nr_datafiles, int mode);*/

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
    PVFS_credential credentials;

    user_opts = parse_args(argc, argv);
    if (!user_opts)
    {
	fprintf(stderr, "Error, failed to parse command line arguments\n");
	return(-1);
    }

    PVFS_hint_import_env(& hints);
    
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

    ret = PVFS_util_gen_credential_defaults(&credentials);    
    if (ret < 0)
    {
        PVFS_perror("PVFS_util_gen_credential", ret);
        goto main_out;
    }

    ret = generic_open(&src, &credentials, 0, 0, NULL, OPEN_SRC);
    if (ret < 0)
    {
	fprintf(stderr, "Could not open source %s\n", user_opts->srcfile);
	goto main_out;
    }

    ret = generic_open(&dest, &credentials, user_opts->num_datafiles, user_opts->strip_size,
                       user_opts->srcfile, OPEN_DEST);

    if (ret < 0)
    {
	fprintf(stderr, "Could not open dest %s\n", user_opts->destfile);
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
    
    PVFS_hint_free(hints);
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
		ret = sscanf(optarg, SCANF_lld, (SCANF_lld_type *)&tmp_opts->strip_size);
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
	"\n-b <buffer_size in bytes>\thow much data to read/write at once"
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
	int64_t offset, size_t count, PVFS_credential *credentials)
{
    int ret = -1;

    if(src->fs_type == UNIX_FILE)
	return (read(src->u.ufs.fd, buffer, count));
    else
    {
	return (pvfs_read(src->u.pvfs2.fd, buffer, count));
    }
    return (ret);
}

/* write 'count' bytes from 'buffer' into (unix or pvfs2) file 'dest' */
static size_t generic_write(file_object *dest, char *buffer, 
    int64_t offset, size_t count, PVFS_credential *credentials)
{
    int ret = -1;

    if (dest->fs_type == UNIX_FILE)
	return(write(dest->u.ufs.fd, buffer, count));
    else
    {	
	return(pvfs_write(dest->u.pvfs2.fd, buffer, count));
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

static int generic_open(file_object *obj, PVFS_credential *credentials,
                        int nr_datafiles, PVFS_size strip_size, 
                        char *srcname, int open_type)
{
    struct stat stat_buf;
    //int ret = -1;
    //char *entry_name;		    /* name of the pvfs2 file */
    //char str_buf[PVFS_NAME_MAX];    /* basename of pvfs2 file */
 
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
	    fprintf(stderr, "could not open unix %s\n", obj->u.ufs.path);
	    return (-1);
	}
    }
    else
    {
	memset(&stat_buf, 0, sizeof(struct stat));
	pvfs_stat(obj->u.pvfs2.user_path, &stat_buf);
	if(open_type == OPEN_SRC)
	{
		if(S_ISDIR(stat_buf.st_mode))
		{
			fprintf(stderr, "Source cannot be a directory\n");
			return(-1);
		}
		obj->u.pvfs2.fd = pvfs_open(obj->u.pvfs2.user_path, O_RDONLY, hints);
		obj->u.pvfs2.mode = (int)stat_buf.st_mode;
	}
	else
	{
		if (S_ISDIR(stat_buf.st_mode))
		{
			if (srcname)
                	{
                                if(obj->u.pvfs2.user_path[strlen(obj->u.pvfs2.user_path)-1] != '/')
                                {
                                    strncat(obj->u.pvfs2.user_path, "/", NAME_MAX);
                                }
                                strncat(obj->u.pvfs2.user_path, basename(srcname), NAME_MAX);
                	}
			else
			{
		    		fprintf(stderr, "cannot find name for "
                            		"destination. giving up\n");
		    		return(-1);
			}
	    	}
	    	obj->u.pvfs2.fd = pvfs_open(obj->u.pvfs2.user_path,
				   O_WRONLY|O_CREAT|O_LARGEFILE|O_TRUNC, 0666, hints);
	}
	if (obj->u.pvfs2.fd < 0)
	{
	    perror("open");
	    fprintf(stderr, "could not open pvfs %s\n", obj->u.pvfs2.user_path);
	    return (-1);
	}
    }
    return 0;
}

static int generic_cleanup(file_object *src, file_object *dest,
                           PVFS_credential *credentials)
{
    /* preserve permissions doing a pvfs2 => unix copy */
    if ((src->fs_type == PVFS2_FILE) &&
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
    if ((src->fs_type == PVFS2_FILE) && (dest->fs_type == PVFS2_FILE))
    {
        PVFS_sys_setattr(dest->u.pvfs2.ref, src->u.pvfs2.attr, credentials, hints);
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

/*void make_attribs(PVFS_sys_attr *attr, PVFS_credential *credentials,
                  int nr_datafiles, int mode)
{
    attr->owner = credentials->userid; 
    attr->group = credentials->group_array[0];
    attr->perms = PVFS_util_translate_mode(mode, 0);
    attr->mask = (PVFS_ATTR_SYS_ALL_SETABLE);
    attr->dfile_count = nr_datafiles;

    if (attr->dfile_count > 0)
    {
	attr->mask |= PVFS_ATTR_SYS_DFILE_COUNT;
    }
}*/    

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
