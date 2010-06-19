/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* pvfs2-tio
 *  Threaded I/O benchmark to exercise the aio code paths on server.
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
#include <pthread.h>
#include <aio.h>

#define __PINT_REQPROTO_ENCODE_FUNCS_C
#include "pvfs2.h"
#include "str-utils.h"
#include "pint-sysint-utils.h"
#include "pint-util.h"
#include "pvfs2-internal.h"
#include "quicklist.h"
#include "quickhash.h"

struct options 
{
    char *srcfile;
    int num_io;
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

static file_object src;
static char buf[2097152];

typedef struct {
    int index;
    PVFS_id_gen_t tag;
    PVFS_sys_op_id op_id;
    PVFS_Request   file_req;
    PVFS_Request   mem_req;
    PVFS_sysresp_io resp_io;
    file_object *src;
    char  *buffer;
    int64_t offset;
    int64_t count;
    PVFS_credentials credentials;
    struct aiocb acb;
    struct qlist_head hash_link;
} io_request;

static PVFS_error post_generic_write(io_request *req);
static PVFS_error post_generic_read(io_request *req);
#define NUM_IO 5

io_request vfs_request[NUM_IO];
/* this hashtable is used to keep track of operations in progress */
#define DEFAULT_OPS_IN_PROGRESS_HTABLE_SIZE 67
static int hash_key(void *key, int table_size);
static int hash_key_compare(void *key, struct qlist_head *link);
static struct qhash_table *s_ops_in_progress_table = NULL;

static int hash_key(void *key, int table_size)
{
    PVFS_id_gen_t tag = *((PVFS_id_gen_t *)key);
    return (tag % table_size);
}

static int hash_key_compare(void *key, struct qlist_head *link)
{
    io_request *vfs_request = NULL;
    PVFS_id_gen_t tag = *((PVFS_id_gen_t *)key);

    vfs_request = qlist_entry(link, io_request, hash_link);
    assert(vfs_request);

    return ((vfs_request->tag == tag) ? 1 : 0);
}

static int initialize_ops_in_progress_table(void)
{
    if (!s_ops_in_progress_table)
    {
        s_ops_in_progress_table = qhash_init(
            hash_key_compare, hash_key,
            DEFAULT_OPS_IN_PROGRESS_HTABLE_SIZE);
    }
    return (s_ops_in_progress_table ? 0 : -PVFS_ENOMEM);
}

static void finalize_ops_in_progress_table(void)
{
    int i = 0;
    struct qlist_head *hash_link = NULL;

    if (s_ops_in_progress_table)
    {
        for(i = 0; i < s_ops_in_progress_table->table_size; i++)
        {
            do
            {
                hash_link = qhash_search_and_remove_at_index(
                    s_ops_in_progress_table, i);

            } while(hash_link);
        }
        qhash_finalize(s_ops_in_progress_table);
        s_ops_in_progress_table = NULL;
    }
}

static PVFS_error remove_op_from_op_in_progress_table(
    io_request *vfs_request)
{
    PVFS_error ret = -PVFS_EINVAL;
    struct qlist_head *hash_link = NULL;
    io_request *tmp_vfs_request = NULL;

    if (vfs_request)
    {
        hash_link = qhash_search_and_remove(
            s_ops_in_progress_table, (void *)(&vfs_request->tag));
        if (hash_link)
        {
            tmp_vfs_request = qhash_entry(
                hash_link, io_request, hash_link);
            assert(tmp_vfs_request);
            assert(tmp_vfs_request == vfs_request);
            ret = 0;
        }
    }
    return ret;
}

static PVFS_error add_op_to_op_in_progress_table(
    io_request *vfs_request)
{
    PVFS_error ret = -PVFS_EINVAL;

    if (vfs_request)
    {
        qhash_add(s_ops_in_progress_table,
                  (void *)(&vfs_request->tag),
                  &vfs_request->hash_link);
        ret = 0;
    }
    return ret;
}
static long tag = 100;

static void post_op(io_request *req)
{
    static int off_count = 0;
    int rd_wr = 1;
    req->offset = (off_count++ * 2097152);
    req->tag = tag++;
    req->src = &src;
    req->buffer = buf;
    req->count = 2097152;
    PVFS_util_gen_credentials(&req->credentials);
    if (rd_wr == 0) 
        post_generic_read(req);
    else
        post_generic_write(req);
    add_op_to_op_in_progress_table(req);
}

#define MAX_OPS 64

static void do_pvfs_io(int num_io)
{
    PVFS_error ret;
    int i, count = num_io;
    io_request *all_request[MAX_OPS], *req;
    PVFS_sys_op_id op_id_array[MAX_OPS];
    int error_code_array[MAX_OPS], op_count = 0;
    PVFS_size total = 0;

    printf("doing %d I/O 5 at a time\n", num_io);
    for (i = 0; i < NUM_IO; i++) {
        vfs_request[i].index = i;
    }

    for (i = 0; i < NUM_IO; i++) {
        post_op(&vfs_request[i]);
    }
    while (1)
    {
        op_count = MAX_OPS;
        memset(error_code_array, 0, MAX_OPS * sizeof(int));
        memset(all_request, 0, MAX_OPS * sizeof(io_request *));
        ret = PVFS_sys_testsome(
                op_id_array, &op_count, (void *) all_request,
                error_code_array, 10);
        for (i = 0; i < op_count; i++)
        {
            req = all_request[i];
            if (req->op_id != op_id_array[i])
                continue;
            if (req->resp_io.total_completed != 2097152)
            {
                fprintf(stderr, "Short I/O\n");
            }
            total += req->resp_io.total_completed;
            ret = remove_op_from_op_in_progress_table(req);
            if (ret)
            {
                PVFS_perror_gossip("Failed to remove op in progress from table\n", ret);
            }
            count--;
            if (count > 0)
                post_op(req);
        }
        if (count <= 0)
            break;
    }
    printf("Total I/O staged %Ld\n", lld(total));
    return;
}

static void do_unix_io(int num_io)
{
    int count;
    long ret;
    int status[NUM_IO];
    int flag, i;

    count = num_io;
    ret = 0;
    flag = -1;

    for (i = 0; i < NUM_IO; i++) {
        vfs_request[i].index = i;
    }
    for (i = 0; i < NUM_IO; i++) {
        status[i] = EINPROGRESS;
        post_op(&vfs_request[i]);
    }
    while (1)
    {
        long ret1;
        flag = -1;
        for (i = 0; i < NUM_IO; i++)
        {
            if (status[i] == EINPROGRESS)
            {
                ret1 = aio_error(&vfs_request[i].acb);
                if (ret1 != 0)
                {
                    /* if op in progress, we have to skip it */
                    if (ret1 == EINPROGRESS)
                    {
                        /* we have not completed */
                        flag = 1;
                        continue;
                    }
                    else
                    {
                        printf("I/O on %d failed: %s\n", i, strerror(errno));
                        status[i] = -1;
                    }
                }
                else
                {
                    /* we have completed */
                    ret += aio_return(&vfs_request[i].acb);
                    if (flag < 0)
                    {
                        flag = 0;
                    }
                    remove_op_from_op_in_progress_table(&vfs_request[i]);
                    count--;
                    if (count > 0)
                    {
                        status[i] = EINPROGRESS;
                        post_op(&vfs_request[i]);
                    }
                }
            }
        }
        if (count <= 0)
            break;
    }
    return;
}

static void do_io(int num_io)
{
    if(src.fs_type == UNIX_FILE)
        do_unix_io(num_io);
    else
        do_pvfs_io(num_io);
    return;
}

int main(int argc, char ** argv)
{
    struct options* user_opts = NULL;
    int64_t ret;
    PVFS_credentials credentials;

    initialize_ops_in_progress_table();
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

    do_io(user_opts->num_io);
main_out:
    PVFS_sys_finalize();
    finalize_ops_in_progress_table();
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
    char flags[] = "f:i:";
    int one_opt = 0;

    struct options* tmp_opts = NULL;

    /* create storage for the command line options */
    tmp_opts = (struct options*)malloc(sizeof(struct options));
    if(!tmp_opts){
	return(NULL);
    }
    memset(tmp_opts, 0, sizeof(struct options));
    tmp_opts->num_io = 10000;

    /* look at command line arguments */
    while((one_opt = getopt(argc, argv, flags)) != EOF)
    {
	switch(one_opt) {
            case 'i':
                tmp_opts->num_io = atoi(optarg);
                break;
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
        obj->u.ufs.fd = open(obj->u.ufs.path, O_RDWR);
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
        ret = PVFS_sys_getattr(ref, PVFS_ATTR_SYS_ALL,
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

/* read 'count' bytes from a (unix or pvfs2) file 'src', placing the result in
 * 'buffer' */
static PVFS_error post_generic_read(io_request *req)
{
    if(req->src->fs_type == UNIX_FILE)
    {
        req->acb.aio_fildes = req->src->u.ufs.fd;
        req->acb.aio_lio_opcode = LIO_READ;
        req->acb.aio_reqprio = 0;
        req->acb.aio_offset = req->offset;
        req->acb.aio_buf = req->buffer;
        req->acb.aio_nbytes = req->count;
        req->acb.aio_sigevent.sigev_notify = SIGEV_NONE; 
	return (aio_read(&req->acb));
    }
    else
    {
        PVFS_error ret;
	req->file_req = PVFS_BYTE;
	ret = PVFS_Request_contiguous(req->count, PVFS_BYTE, &req->mem_req);
	if (ret < 0)
	{
	    fprintf(stderr, "Error: PVFS_Request_contiguous failure\n");
	    return (ret);
	}
	ret = PVFS_isys_io(req->src->u.pvfs2.ref, req->file_req, req->offset,
		req->buffer, req->mem_req, &req->credentials, &req->resp_io,
                PVFS_IO_READ, &req->op_id, NULL, req);
	if (ret != 0)
	{
            PVFS_perror("PVFS_isys_io", ret);
            PVFS_Request_free(&req->mem_req);
	    return ret;
	} 
        return 0;
    }
}

/* write 'count' bytes to a (unix or pvfs2) file 'src', taking the result from
 * 'buffer' */
static PVFS_error post_generic_write(io_request *req)
{
    if(req->src->fs_type == UNIX_FILE)
    {
        req->acb.aio_fildes = req->src->u.ufs.fd;
        req->acb.aio_lio_opcode = LIO_WRITE;
        req->acb.aio_reqprio = 0;
        req->acb.aio_offset = req->offset;
        req->acb.aio_buf = req->buffer;
        req->acb.aio_nbytes = req->count;
        req->acb.aio_sigevent.sigev_notify = SIGEV_NONE; 
	return (aio_write(&req->acb));
    }
    else
    {
        PVFS_error ret;
	req->file_req = PVFS_BYTE;
	ret = PVFS_Request_contiguous(req->count, PVFS_BYTE, &req->mem_req);
	if (ret < 0)
	{
	    fprintf(stderr, "Error: PVFS_Request_contiguous failure\n");
	    return (ret);
	}
	ret = PVFS_isys_io(req->src->u.pvfs2.ref, req->file_req, req->offset,
		req->buffer, req->mem_req, &req->credentials, &req->resp_io,
                PVFS_IO_WRITE, &req->op_id, NULL, req);
	if (ret != 0)
	{
            PVFS_perror("PVFS_isys_io", ret);
            PVFS_Request_free(&req->mem_req);
	    return ret;
	} 
        return 0;
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
