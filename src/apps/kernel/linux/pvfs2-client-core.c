/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <errno.h>
#include <sys/resource.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>

#include "pvfs2.h"
#include "gossip.h"
#include "pint-dev.h"
#include "job.h"
#include "acache.h"
#include "pint-dev-shared.h"
#include "pvfs2-dev-proto.h"
#include "pvfs2-util.h"
#include "pint-bucket.h"
#include "pvfs2-sysint.h"
#include "client-state-machine.h"

#ifdef USE_MMAP_RA_CACHE
#include "mmap-ra-cache.h"
#endif

/*
  an arbitrary limit to the max number of operations we'll support in
  flight at once, and the max number of items we can write into the
  device file as a response
*/
#define MAX_NUM_OPS                 64
#define MAX_LIST_SIZE      MAX_NUM_OPS

#define REMOUNT_PENDING     0xFFEEFF33

/*
  the block size to report in statfs as the blocksize (i.e. the
  optimal i/o transfer size); regardless of this value, the fragment
  size (underlying fs block size) in the kernel is fixed at 1024
*/
#define STATFS_DEFAULT_BLOCKSIZE PVFS2_BUFMAP_DEFAULT_DESC_SIZE

/* small default attribute cache timeout; effectively disabled */
#define ACACHE_TIMEOUT_MS 0

/*
  default timeout value to wait for completion of in progress
  operations
*/
#define PVFS2_CLIENT_DEFAULT_TEST_TIMEOUT_MS 10

/*
  this client core *requires* pthreads now, regardless of if the pvfs2
  system interface has threading enabled or not.  we need it for async
  remounts on restart to retrieve our dynamic mount information (if
  any) from the kernel, which means we call a blocking ioctl that must
  be serviced by our regular handlers.  to do both, we use a thread
  for the blocking ioctl.
*/
static pthread_t remount_thread;
static pthread_mutex_t remount_mutex = PTHREAD_MUTEX_INITIALIZER;
static int remount_complete = 0;

/* used for generating unique dynamic mount point names */
static int dynamic_mount_id = 1;

typedef struct
{
    int is_dev_unexp;
    pvfs2_upcall_t in_upcall;
    pvfs2_downcall_t out_downcall;

    job_status_s jstat;
    struct PINT_dev_unexp_info info;

    PVFS_sys_op_id op_id;

#ifdef USE_MMAP_RA_CACHE
    void *io_tmp_buf;
#endif
    void *io_kernel_mapped_buf;

    int was_handled_inline;

    union
    {
        PVFS_sysresp_lookup lookup;
        PVFS_sysresp_create create;
        PVFS_sysresp_symlink symlink;
        PVFS_sysresp_getattr getattr;
        PVFS_sysresp_mkdir mkdir;
        PVFS_sysresp_readdir readdir;
        PVFS_sysresp_statfs statfs;
        PVFS_sysresp_io io;
    } response;

} vfs_request_t;

static job_context_id s_client_dev_context;
static int s_client_is_processing = 1;
static struct PVFS_dev_map_desc s_io_desc;

/* used only for deleting all allocated vfs_request objects */
vfs_request_t *s_vfs_request_array[MAX_NUM_OPS] = {NULL};

int write_device_response(
    void *buffer_list,
    int *size_list,
    int list_size,
    int total_size,
    PVFS_id_gen_t tag,
    job_id_t *job_id,
    job_status_s *jstat,
    job_context_id context);

#define write_inlined_device_response(vfs_request)            \
do {                                                          \
    void *buffer_list[MAX_LIST_SIZE];                         \
    int size_list[MAX_LIST_SIZE];                             \
    int list_size = 0, total_size = 0;                        \
                                                              \
    buffer_list[0] = &vfs_request->out_downcall;              \
    size_list[0] = sizeof(pvfs2_downcall_t);                  \
    total_size = sizeof(pvfs2_downcall_t);                    \
    list_size = 1;                                            \
    ret = write_device_response(                              \
        buffer_list,size_list,list_size, total_size,          \
        (PVFS_id_gen_t)vfs_request->info.tag,                 \
        &vfs_request->op_id,                                  \
        &vfs_request->jstat, s_client_dev_context);           \
    if (ret < 0)                                              \
    {                                                         \
        gossip_err("write_device_response failed (tag=%lu)\n",\
                   (unsigned long)vfs_request->info.tag);     \
    }                                                         \
    vfs_request->was_handled_inline = 1;                      \
} while(0)

static void client_core_sig_handler(int signum)
{
    s_client_is_processing = 0;
}

void *exec_remount(void *ptr)
{
    pthread_mutex_lock(&remount_mutex);
    /*
      when the remount mutex is unlocked, tell the kernel to remount
      any file systems that may have been mounted previously, which
      will fill in our dynamic mount information by triggering mount
      upcalls for each fs mounted by the kernel at this point
     */
    if (PINT_dev_remount())
    {
        gossip_err("*** Failed to remount filesystems!\n");
    }

    remount_complete = 1;
    pthread_mutex_unlock(&remount_mutex);

    return NULL;
}

static int post_lookup_request(vfs_request_t *vfs_request)
{
    int ret = -PVFS_EINVAL;

    gossip_debug(
        GOSSIP_CLIENT_DEBUG,
        "Got a lookup request for %s (fsid %d | parent %Lu)\n",
        vfs_request->in_upcall.req.lookup.d_name,
        vfs_request->in_upcall.req.lookup.parent_refn.fs_id,
        Lu(vfs_request->in_upcall.req.lookup.parent_refn.handle));

    ret = PVFS_isys_ref_lookup(
        vfs_request->in_upcall.req.lookup.parent_refn.fs_id,
        vfs_request->in_upcall.req.lookup.d_name,
        vfs_request->in_upcall.req.lookup.parent_refn,
        &vfs_request->in_upcall.credentials,
        &vfs_request->response.lookup,
        vfs_request->in_upcall.req.lookup.sym_follow,
        &vfs_request->op_id, (void *)vfs_request);

    if (ret < 0)
    {
        gossip_debug(
            GOSSIP_CLIENT_DEBUG,
            "Failed to lookup %s on fsid %d (ret=%d)!\n",
            vfs_request->in_upcall.req.lookup.d_name,
            vfs_request->in_upcall.req.lookup.parent_refn.fs_id, ret);
    }
    return ret;
}

static int post_create_request(vfs_request_t *vfs_request)
{
    int ret = -PVFS_EINVAL;

    gossip_debug(
        GOSSIP_CLIENT_DEBUG,
        "Got a create request for %s (fsid %d | parent %Lu)\n",
        vfs_request->in_upcall.req.create.d_name,
        vfs_request->in_upcall.req.create.parent_refn.fs_id,
        Lu(vfs_request->in_upcall.req.create.parent_refn.handle));

    ret = PVFS_isys_create(
        vfs_request->in_upcall.req.create.d_name,
        vfs_request->in_upcall.req.create.parent_refn,
        vfs_request->in_upcall.req.create.attributes,
        &vfs_request->in_upcall.credentials, NULL,
        &vfs_request->response.create,
        &vfs_request->op_id, (void *)vfs_request);

    if (ret < 0)
    {
        PVFS_perror_gossip("File creation failed", ret);
    }
    return ret;
}

static int post_symlink_request(vfs_request_t *vfs_request)
{
    int ret = -PVFS_EINVAL;

    gossip_debug(
        GOSSIP_CLIENT_DEBUG,
        "Got a symlink request from %s (fsid %d | parent %Lu) to %s\n",
        vfs_request->in_upcall.req.sym.entry_name,
        vfs_request->in_upcall.req.sym.parent_refn.fs_id,
        Lu(vfs_request->in_upcall.req.sym.parent_refn.handle),
        vfs_request->in_upcall.req.sym.target);

    ret = PVFS_isys_symlink(
        vfs_request->in_upcall.req.sym.entry_name,
        vfs_request->in_upcall.req.sym.parent_refn,
        vfs_request->in_upcall.req.sym.target,
        vfs_request->in_upcall.req.sym.attributes,
        &vfs_request->in_upcall.credentials,
        &vfs_request->response.symlink,
        &vfs_request->op_id, (void *)vfs_request);

    if (ret < 0)
    {
        PVFS_perror_gossip("Symlink creation failed", ret);
    }
    return ret;
}

static int post_getattr_request(vfs_request_t *vfs_request)
{
    int ret = -PVFS_EINVAL;

    gossip_debug(
        GOSSIP_CLIENT_DEBUG,
        "got a getattr request for fsid %d | handle %Lu\n",
        vfs_request->in_upcall.req.getattr.refn.fs_id,
        Lu(vfs_request->in_upcall.req.getattr.refn.handle));

    ret = PVFS_isys_getattr(
        vfs_request->in_upcall.req.getattr.refn,
        PVFS_ATTR_SYS_ALL,
        &vfs_request->in_upcall.credentials,
        &vfs_request->response.getattr,
        &vfs_request->op_id, (void *)vfs_request);

    if (ret < 0)
    {
        PVFS_perror_gossip("Getattr failed", ret);
    }
    return ret;
}

static int post_setattr_request(vfs_request_t *vfs_request)
{
    int ret = -PVFS_EINVAL;

    gossip_debug(
        GOSSIP_CLIENT_DEBUG,
        "got a setattr request for fsid %d | handle %Lu\n",
        vfs_request->in_upcall.req.setattr.refn.fs_id,
        Lu(vfs_request->in_upcall.req.setattr.refn.handle));

    ret = PVFS_isys_setattr(
        vfs_request->in_upcall.req.setattr.refn,
        vfs_request->in_upcall.req.setattr.attributes,
        &vfs_request->in_upcall.credentials,
        &vfs_request->op_id, (void *)vfs_request);

    if (ret < 0)
    {
        PVFS_perror_gossip("Setattr failed", ret);
    }
    return ret;
}

static int post_remove_request(vfs_request_t *vfs_request)
{
    int ret = -PVFS_EINVAL;

    gossip_debug(
        GOSSIP_CLIENT_DEBUG,
        "Got a remove request for %s under fsid %d and "
        "handle %Lu\n", vfs_request->in_upcall.req.remove.d_name,
        vfs_request->in_upcall.req.remove.parent_refn.fs_id,
        Lu(vfs_request->in_upcall.req.remove.parent_refn.handle));

    ret = PVFS_isys_remove(
        vfs_request->in_upcall.req.remove.d_name,
        vfs_request->in_upcall.req.remove.parent_refn,
        &vfs_request->in_upcall.credentials,
        &vfs_request->op_id, (void *)vfs_request);

    if (ret < 0)
    {
        PVFS_perror_gossip("Remove failed",ret);
    }
    return ret;
}

static int post_mkdir_request(vfs_request_t *vfs_request)
{
    int ret = -PVFS_EINVAL;

    gossip_debug(
        GOSSIP_CLIENT_DEBUG,
        "Got a mkdir request for %s (fsid %d | parent %Lu)\n",
        vfs_request->in_upcall.req.mkdir.d_name,
        vfs_request->in_upcall.req.mkdir.parent_refn.fs_id,
        Lu(vfs_request->in_upcall.req.mkdir.parent_refn.handle));

    ret = PVFS_isys_mkdir(
        vfs_request->in_upcall.req.mkdir.d_name,
        vfs_request->in_upcall.req.mkdir.parent_refn,
        vfs_request->in_upcall.req.mkdir.attributes,
        &vfs_request->in_upcall.credentials,
        &vfs_request->response.mkdir,
        &vfs_request->op_id, (void *)vfs_request);

    if (ret < 0)
    {
        PVFS_perror_gossip("Mkdir failed", ret);
    }
    return ret;
}

static int post_readdir_request(vfs_request_t *vfs_request)
{
    int ret = -PVFS_EINVAL;

    gossip_debug(
        GOSSIP_CLIENT_DEBUG, "Got a readdir request for fsid %d | "
        "parent %Lu (token is %d)\n",
        vfs_request->in_upcall.req.readdir.refn.fs_id,
        Lu(vfs_request->in_upcall.req.readdir.refn.handle),
        vfs_request->in_upcall.req.readdir.token);

    ret = PVFS_isys_readdir(
        vfs_request->in_upcall.req.readdir.refn,
        vfs_request->in_upcall.req.readdir.token,
        vfs_request->in_upcall.req.readdir.max_dirent_count,
        &vfs_request->in_upcall.credentials,
        &vfs_request->response.readdir,
        &vfs_request->op_id, (void *)vfs_request);

    if (ret < 0)
    {
        PVFS_perror_gossip("Readdir failed", ret);
    }
    return ret;
}

static int post_rename_request(vfs_request_t *vfs_request)
{
    int ret = -PVFS_EINVAL;

    gossip_debug(
        GOSSIP_CLIENT_DEBUG,
        "Got a rename request for %s under fsid %d and "
        "handle %Lu to be %s under fsid %d and handle %Lu\n",
        vfs_request->in_upcall.req.rename.d_old_name,
        vfs_request->in_upcall.req.rename.old_parent_refn.fs_id,
        Lu(vfs_request->in_upcall.req.rename.old_parent_refn.handle),
        vfs_request->in_upcall.req.rename.d_new_name,
        vfs_request->in_upcall.req.rename.new_parent_refn.fs_id,
        Lu(vfs_request->in_upcall.req.rename.new_parent_refn.handle));

    ret = PVFS_isys_rename(
        vfs_request->in_upcall.req.rename.d_old_name,
        vfs_request->in_upcall.req.rename.old_parent_refn,
        vfs_request->in_upcall.req.rename.d_new_name,
        vfs_request->in_upcall.req.rename.new_parent_refn,
        &vfs_request->in_upcall.credentials,
        &vfs_request->op_id, (void *)vfs_request);

    if (ret < 0)
    {
        PVFS_perror_gossip("Rename failed", ret);
    }
    return ret;
}

static int post_truncate_request(vfs_request_t *vfs_request)
{
    int ret = -PVFS_EINVAL;

    gossip_debug(
        GOSSIP_CLIENT_DEBUG, "Got a truncate request for %Lu under "
        "fsid %d to be size %Ld\n",
        Lu(vfs_request->in_upcall.req.truncate.refn.handle),
        vfs_request->in_upcall.req.truncate.refn.fs_id,
        Ld(vfs_request->in_upcall.req.truncate.size));

    ret = PVFS_isys_truncate(
        vfs_request->in_upcall.req.truncate.refn,
        vfs_request->in_upcall.req.truncate.size,
        &vfs_request->in_upcall.credentials,
        &vfs_request->op_id, (void *)vfs_request);

    if (ret < 0)
    {
        PVFS_perror_gossip("Truncate failed", ret);
    }
    return ret;
}

#define generate_upcall_mntent(mntent, in_upcall, mount)              \
do {                                                                  \
    /*                                                                \
      generate a unique dynamic mount point; the id will be passed to \
      the kernel via the downcall so we can match it with a proper    \
      unmount request at unmount time.  if we're unmounting, use the  \
      passed in id from the upcall                                    \
    */                                                                \
    if (mount)                                                        \
        snprintf(buf, PATH_MAX, "<DYNAMIC-%d>", dynamic_mount_id);    \
    else                                                              \
        snprintf(buf, PATH_MAX, "<DYNAMIC-%d>",                       \
                 in_upcall.req.fs_umount.id);                        \
                                                                      \
    mntent.mnt_dir = strdup(buf);                                     \
    if (!mntent.mnt_dir)                                              \
    {                                                                 \
        ret = -PVFS_ENOMEM;                                           \
        goto fail_downcall;                                           \
    }                                                                 \
                                                                      \
    gossip_debug(GOSSIP_CLIENT_DEBUG, "Using %s Point %s\n",          \
                 (mount ? "Mount" : "Unmount"), mntent.mnt_dir);      \
                                                                      \
    if (mount)                                                        \
        ptr = rindex(in_upcall.req.fs_mount.pvfs2_config_server,     \
                     (int)'/');                                       \
    else                                                              \
        ptr = rindex(in_upcall.req.fs_umount.pvfs2_config_server,    \
                     (int)'/');                                       \
                                                                      \
    if (!ptr)                                                         \
    {                                                                 \
        gossip_err("Configuration server MUST be of the form "        \
                   "protocol://address/fs_name\n");                   \
        ret = -PVFS_EINVAL;                                           \
        goto fail_downcall;                                           \
    }                                                                 \
    *ptr = '\0';                                                      \
    ptr++;                                                            \
                                                                      \
    if (mount)                                                        \
        mntent.pvfs_config_server = strdup(                           \
            in_upcall.req.fs_mount.pvfs2_config_server);             \
    else                                                              \
        mntent.pvfs_config_server = strdup(                           \
            in_upcall.req.fs_umount.pvfs2_config_server);            \
                                                                      \
    if (!mntent.pvfs_config_server)                                   \
    {                                                                 \
        ret = -PVFS_ENOMEM;                                           \
        goto fail_downcall;                                           \
    }                                                                 \
                                                                      \
    gossip_debug(                                                     \
        GOSSIP_CLIENT_DEBUG, "Got Configuration Server: %s "          \
        "(len=%d)\n", mntent.pvfs_config_server,                      \
        strlen(mntent.pvfs_config_server));                           \
                                                                      \
    mntent.pvfs_fs_name = strdup(ptr);                                \
    if (!mntent.pvfs_config_server)                                   \
    {                                                                 \
        ret = -PVFS_ENOMEM;                                           \
        goto fail_downcall;                                           \
    }                                                                 \
                                                                      \
    gossip_debug(                                                     \
        GOSSIP_CLIENT_DEBUG, "Got FS Name: %s (len=%d)\n",            \
        mntent.pvfs_fs_name, strlen(mntent.pvfs_fs_name));            \
                                                                      \
    mntent.encoding = ENCODING_DEFAULT;                               \
    mntent.flowproto = FLOWPROTO_DEFAULT;                             \
                                                                      \
    /* also fill in the fs_id for umount */                           \
    if (!mount)                                                       \
        mntent.fs_id = in_upcall.req.fs_umount.fs_id;                \
                                                                      \
    ret = 0;                                                          \
} while(0)

static int service_fs_mount_request(vfs_request_t *vfs_request)
{
    int ret = -PVFS_ENODEV;
    struct PVFS_sys_mntent mntent;
    PVFS_handle root_handle;
    char *ptr = NULL;
    char buf[PATH_MAX] = {0};

    /*
      since we got a mount request from the vfs, we know that some
      mntent entries are not filled in, so add some defaults here
      if they weren't passed in the options.
    */
    memset(&mntent, 0, sizeof(struct PVFS_sys_mntent));
        
    gossip_debug(
        GOSSIP_CLIENT_DEBUG,
        "Got an fs mount request via host %s\n",
        vfs_request->in_upcall.req.fs_mount.pvfs2_config_server);

    generate_upcall_mntent(mntent, vfs_request->in_upcall, 1);

    ret = PVFS_sys_fs_add(&mntent);
    if (ret < 0)
    {
      fail_downcall:
        gossip_err(
            "Failed to mount via host %s\n",
            vfs_request->in_upcall.req.fs_mount.pvfs2_config_server);

        PVFS_perror_gossip("Mount failed", ret);

        vfs_request->out_downcall.type = PVFS2_VFS_OP_FS_MOUNT;
        vfs_request->out_downcall.status = ret;
    }
    else
    {
        /*
          before sending success response we need to resolve the root
          handle, given the previously resolved fs_id
        */
        ret = PINT_bucket_get_root_handle(mntent.fs_id, &root_handle);
        if (ret)
        {
            gossip_err("Failed to retrieve root handle for "
                       "resolved fs_id %d\n", mntent.fs_id);
            goto fail_downcall;
        }
            
        gossip_debug(GOSSIP_CLIENT_DEBUG,
                     "FS mount got root handle %Lu on fs id %d\n",
                     root_handle, mntent.fs_id);

        vfs_request->out_downcall.type = PVFS2_VFS_OP_FS_MOUNT;
        vfs_request->out_downcall.status = 0;
        vfs_request->out_downcall.resp.fs_mount.fs_id = mntent.fs_id;
        vfs_request->out_downcall.resp.fs_mount.root_handle =
            root_handle;
        vfs_request->out_downcall.resp.fs_mount.id = dynamic_mount_id++;
    }

    PVFS_sys_free_mntent(&mntent);

    write_inlined_device_response(vfs_request);
    return 0;
}

static int service_fs_umount_request(vfs_request_t *vfs_request)
{
    int ret = -PVFS_ENODEV;
    struct PVFS_sys_mntent mntent;
    char *ptr = NULL;
    char buf[PATH_MAX] = {0};

    /*
      since we got a umount request from the vfs, we know that
      some mntent entries are not filled in, so add some defaults
      here if they weren't passed in the options.
    */
    memset(&mntent, 0, sizeof(struct PVFS_sys_mntent));

    gossip_debug(
        GOSSIP_CLIENT_DEBUG,
        "Got an fs umount request via host %s\n",
        vfs_request->in_upcall.req.fs_umount.pvfs2_config_server);

    generate_upcall_mntent(mntent, vfs_request->in_upcall, 0);

    ret = PVFS_sys_fs_remove(&mntent);
    if (ret < 0)
    {
      fail_downcall:
        gossip_err(
            "Failed to umount via host %s\n",
            vfs_request->in_upcall.req.fs_umount.pvfs2_config_server);

        PVFS_perror("Umount failed", ret);

        vfs_request->out_downcall.type = PVFS2_VFS_OP_FS_UMOUNT;
        vfs_request->out_downcall.status = ret;
    }
    else
    {
        gossip_debug(GOSSIP_CLIENT_DEBUG, "FS umount ok\n");

        vfs_request->out_downcall.type = PVFS2_VFS_OP_FS_MOUNT;
        vfs_request->out_downcall.status = 0;
    }

    PVFS_sys_free_mntent(&mntent);

    write_inlined_device_response(vfs_request);
    return 0;
}

static int service_statfs_request(vfs_request_t *vfs_request)
{
    int ret = -PVFS_EINVAL;

    gossip_debug(
        GOSSIP_CLIENT_DEBUG, "Got a statfs request for fsid %d\n",
        vfs_request->in_upcall.req.statfs.fs_id);

    ret = PVFS_sys_statfs(
        vfs_request->in_upcall.req.statfs.fs_id,
        &vfs_request->in_upcall.credentials,
        &vfs_request->response.statfs);

    vfs_request->out_downcall.status = ret;
    vfs_request->out_downcall.type = vfs_request->in_upcall.type;

    if (ret < 0)
    {
        PVFS_perror_gossip("Statfs failed", ret);
    }
    else
    {
        PVFS_sysresp_statfs *resp = &vfs_request->response.statfs;

        vfs_request->out_downcall.resp.statfs.block_size =
            STATFS_DEFAULT_BLOCKSIZE;
        vfs_request->out_downcall.resp.statfs.blocks_total = (long)
            (resp->statfs_buf.bytes_total /
             vfs_request->out_downcall.resp.statfs.block_size);
        vfs_request->out_downcall.resp.statfs.blocks_avail = (long)
            (resp->statfs_buf.bytes_available /
             vfs_request->out_downcall.resp.statfs.block_size);
        /*
          these values really represent handle/inode counts
          rather than an accurate number of files
        */
        vfs_request->out_downcall.resp.statfs.files_total = (long)
            resp->statfs_buf.handles_total_count;
        vfs_request->out_downcall.resp.statfs.files_avail = (long)
            resp->statfs_buf.handles_available_count;
    }

    write_inlined_device_response(vfs_request);
    return 0;
}

#ifdef USE_MMAP_RA_CACHE
static int post_io_readahead_request(vfs_request_t *vfs_request)
{
    int ret = -PVFS_EINVAL, val = 0;
    PVFS_Request file_req = PVFS_BYTE;
    PVFS_Request mem_req;
    void *buf = NULL;

    gossip_debug(GOSSIP_MMAP_RCACHE_DEBUG,
                 "post_io_readahead_request called\n");

    assert((vfs_request->in_upcall.req.io.buf_index > -1) &&
           (vfs_request->in_upcall.req.io.buf_index <
            PVFS2_BUFMAP_DESC_COUNT));

    vfs_request->io_tmp_buf = malloc(
        vfs_request->in_upcall.req.io.readahead_size);
    if (!vfs_request->io_tmp_buf)
    {
        return -PVFS_ENOMEM;
    }

    if ((vfs_request->in_upcall.req.io.io_type == PVFS_IO_READ) &&
        vfs_request->in_upcall.req.io.readahead_size)
    {
        /* check readahead cache first */
        val = pvfs2_mmap_ra_cache_get_block(
            vfs_request->in_upcall.req.io.refn,
            vfs_request->in_upcall.req.io.offset,
            vfs_request->in_upcall.req.io.count,
            vfs_request->io_tmp_buf);

        if (val == 0)
        {
            goto mmap_ra_cache_hit;
        }
        else if (val == -2)
        {
            /* check if we should flush stale cache data */
            pvfs2_mmap_ra_cache_flush(
                vfs_request->in_upcall.req.io.refn);

            free(vfs_request->io_tmp_buf);
            vfs_request->io_tmp_buf = NULL;
            return ret;
        }
    }
    else
    {
        free(vfs_request->io_tmp_buf);
        vfs_request->io_tmp_buf = NULL;
        return ret;
    }

    /* make the full-blown readahead sized request */
    ret = PVFS_Request_contiguous(
        vfs_request->in_upcall.req.io.readahead_size,
        PVFS_BYTE, &mem_req);

    assert(ret == 0);

    ret = PVFS_isys_io(
        vfs_request->in_upcall.req.io.refn, file_req, 0,
        vfs_request->io_tmp_buf, mem_req,
        &vfs_request->in_upcall.credentials,
        &vfs_request->response.io,
        vfs_request->in_upcall.req.io.io_type,
        &vfs_request->op_id, (void *)vfs_request);

    if (ret < 0)
    {
        PVFS_perror_gossip("File I/O failed", ret);
    }
    return 0;

    /* on cache hits, we return immediately with the cached data */
  mmap_ra_cache_hit:

    vfs_request->out_downcall.type = PVFS2_VFS_OP_FILE_IO;
    vfs_request->out_downcall.status = 0;
    vfs_request->out_downcall.resp.io.amt_complete =
        vfs_request->in_upcall.req.io.count;

    /* get a shared kernel/userspace buffer for the I/O transfer */
    buf = PINT_dev_get_mapped_buffer(
        &s_io_desc, vfs_request->in_upcall.req.io.buf_index);
    assert(buf);

    /* copy cached data into the shared user/kernel space */
    memcpy(buf, vfs_request->io_tmp_buf,
           vfs_request->in_upcall.req.io.count);
    free(vfs_request->io_tmp_buf);
    vfs_request->io_tmp_buf = NULL;

    write_inlined_device_response(vfs_request);
    return 0;
}
#endif /* USE_MMAP_RA_CACHE */

static int post_io_request(vfs_request_t *vfs_request)
{
    int ret = -PVFS_EINVAL;
    PVFS_Request file_req = PVFS_BYTE;
    PVFS_Request mem_req;

#ifdef USE_MMAP_RA_CACHE
    if ((vfs_request->in_upcall.req.io.readahead_size ==
         PVFS2_MMAP_RACACHE_FLUSH))
    {
        pvfs2_mmap_ra_cache_flush(
            vfs_request->in_upcall.req.io.refn);
    }
    else if ((vfs_request->in_upcall.req.io.offset == (loff_t)0) &&
             (vfs_request->in_upcall.req.io.readahead_size > 0) &&
             (vfs_request->in_upcall.req.io.readahead_size <
              PVFS2_MMAP_RACACHE_MAX_SIZE))
    {
        ret = post_io_readahead_request(vfs_request);

        /*
          if the readahead request succeeds, return.  otherwise
          fallback to normal posting/servicing
        */
        if (ret == 0)
        {
            return ret;
        }
    }
#endif /* USE_MMAP_RA_CACHE */

    ret = PVFS_Request_contiguous(
        (int32_t)vfs_request->in_upcall.req.io.count,
        PVFS_BYTE, &mem_req);
    assert(ret == 0);

    assert((vfs_request->in_upcall.req.io.buf_index > -1) &&
           (vfs_request->in_upcall.req.io.buf_index <
            PVFS2_BUFMAP_DESC_COUNT));

    /* get a shared kernel/userspace buffer for the I/O transfer */
    vfs_request->io_kernel_mapped_buf = PINT_dev_get_mapped_buffer(
        &s_io_desc, vfs_request->in_upcall.req.io.buf_index);
    assert(vfs_request->io_kernel_mapped_buf);

    ret = PVFS_isys_io(
        vfs_request->in_upcall.req.io.refn, file_req,
        vfs_request->in_upcall.req.io.offset, 
        vfs_request->io_kernel_mapped_buf, mem_req,
        &vfs_request->in_upcall.credentials,
        &vfs_request->response.io,
        vfs_request->in_upcall.req.io.io_type,
        &vfs_request->op_id, (void *)vfs_request);

    if (ret < 0)
    {
        PVFS_perror_gossip("File I/O failed", ret);
    }
    return ret;
}

#ifdef USE_MMAP_RA_CACHE
static int service_mmap_ra_flush_request(vfs_request_t *vfs_request)
{
    int ret = -PVFS_EINVAL;

    gossip_debug(
        GOSSIP_CLIENT_DEBUG,
        "Got a mmap racache flush request for %Lu under fsid %d\n",
        Lu(vfs_request->in_upcall.req.ra_cache_flush.refn.handle),
        vfs_request->in_upcall.req.ra_cache_flush.refn.fs_id);

    /* flush associated cached data if any */
    pvfs2_mmap_ra_cache_flush(vfs_request->in_upcall.req.io.refn);

    /* we need to send a blank success response */
    vfs_request->out_downcall.type = PVFS2_VFS_OP_MMAP_RA_FLUSH;
    vfs_request->out_downcall.status = 0;

    write_inlined_device_response(vfs_request);
    return 0;
}
#endif

int write_device_response(
    void *buffer_list,
    int *size_list,
    int list_size,
    int total_size,
    PVFS_id_gen_t tag,
    job_id_t *job_id,
    job_status_s *jstat,
    job_context_id context)
{
    int ret = -1;
    int outcount = 0;

    if (buffer_list && size_list && list_size &&
        total_size && (list_size < MAX_LIST_SIZE))
    {
        ret = job_dev_write_list(buffer_list, size_list, list_size,
                                 total_size, tag, PINT_DEV_EXT_ALLOC,
                                 NULL, 0, jstat, job_id, context);
        if (ret < 0)
        {
            PVFS_perror("job_dev_write_list()", ret);
            return ret;
        }
        else if (ret == 0)
        {
	    ret = job_test(*job_id, &outcount, NULL, jstat, -1, context);
            if (ret < 0)
            {
                PVFS_perror("job_test()", ret);
                return ret;
            }
        }

        if (jstat->error_code != 0)
        {
            PVFS_perror("job_bmi_write_list() error code",
                        jstat->error_code);
            ret = -1;
        }
    }
    return ret;
}

static inline void copy_dirents_to_downcall(vfs_request_t *vfs_request)
{
    int i = 0, len = 0;

    vfs_request->out_downcall.resp.readdir.token =
        vfs_request->response.readdir.token;

    for(; i < vfs_request->response.readdir.pvfs_dirent_outcount; i++)
    {
        vfs_request->out_downcall.resp.readdir.refn[i].handle =
            vfs_request->response.readdir.dirent_array[i].handle;
        vfs_request->out_downcall.resp.readdir.refn[i].fs_id =
            vfs_request->in_upcall.req.readdir.refn.fs_id;

        len = strlen(
            vfs_request->response.readdir.dirent_array[i].d_name);
        vfs_request->out_downcall.resp.readdir.d_name_len[i] = len;

        strncpy(
            &vfs_request->out_downcall.resp.readdir.d_name[i][0],
            vfs_request->response.readdir.dirent_array[i].d_name, len);

        vfs_request->out_downcall.resp.readdir.dirent_count++;
    }

    if (vfs_request->out_downcall.resp.readdir.dirent_count !=
        vfs_request->response.readdir.pvfs_dirent_outcount)
    {
        gossip_err("Error! readdir counts don't match! (%d != %d)\n",
                   vfs_request->out_downcall.resp.readdir.dirent_count,
                   vfs_request->response.readdir.pvfs_dirent_outcount);
    }

    /* free sysresp dirent array */
    free(vfs_request->response.readdir.dirent_array);
    vfs_request->response.readdir.dirent_array = NULL;
}

static inline void package_downcall_members(
    vfs_request_t *vfs_request, int error_code)
{
    assert(vfs_request);

    vfs_request->out_downcall.status = error_code;
    vfs_request->out_downcall.type = vfs_request->in_upcall.type;

    switch(vfs_request->in_upcall.type)
    {
        case PVFS2_VFS_OP_LOOKUP:
            if (error_code)
            {
                vfs_request->out_downcall.resp.lookup.refn.handle =
                    PVFS_HANDLE_NULL;
                vfs_request->out_downcall.resp.lookup.refn.fs_id =
                    PVFS_FS_ID_NULL;
            }
            else
            {
                vfs_request->out_downcall.resp.lookup.refn =
                    vfs_request->response.lookup.ref;
            }
            break;
        case PVFS2_VFS_OP_CREATE:
            if (error_code)
            {
                vfs_request->out_downcall.resp.create.refn.handle =
                    PVFS_HANDLE_NULL;
                vfs_request->out_downcall.resp.create.refn.fs_id =
                    PVFS_FS_ID_NULL;
            }
            else
            {
                vfs_request->out_downcall.resp.create.refn =
                    vfs_request->response.create.ref;
            }
            break;
        case PVFS2_VFS_OP_SYMLINK:
            if (error_code)
            {
                vfs_request->out_downcall.resp.sym.refn.handle =
                    PVFS_HANDLE_NULL;
                vfs_request->out_downcall.resp.sym.refn.fs_id =
                    PVFS_FS_ID_NULL;
            }
            else
            {
                vfs_request->out_downcall.resp.sym.refn =
                    vfs_request->response.symlink.ref;
            }
            break;
        case PVFS2_VFS_OP_GETATTR:
            if (error_code == 0)
            {
                PVFS_sys_attr *attr = &vfs_request->response.getattr.attr;

                vfs_request->out_downcall.resp.getattr.attributes =
                    vfs_request->response.getattr.attr;

                /*
                  free allocated attr memory if required; to avoid
                  copying the embedded link_target string inside the
                  sys_attr object passed down into the vfs, we
                  explicitly copy the link target (if any) into a
                  reserved string space in the getattr downcall object
                */
                if ((attr->objtype == PVFS_TYPE_SYMLINK) &&
                    (attr->mask & PVFS_ATTR_SYS_LNK_TARGET))
                {
                    assert(attr->link_target);

                    snprintf(
                        vfs_request->out_downcall.resp.getattr.link_target,
                        PVFS2_NAME_LEN, "%s", attr->link_target);

                    free(attr->link_target);
                }
            }
            break;
        case PVFS2_VFS_OP_SETATTR:
            break;
        case PVFS2_VFS_OP_REMOVE:
            break;
        case PVFS2_VFS_OP_MKDIR:
            if (error_code)
            {
                vfs_request->out_downcall.resp.mkdir.refn.handle =
                    PVFS_HANDLE_NULL;
                vfs_request->out_downcall.resp.mkdir.refn.fs_id =
                    PVFS_FS_ID_NULL;
            }
            else
            {
                vfs_request->out_downcall.resp.mkdir.refn =
                    vfs_request->response.mkdir.ref;
            }
            break;
        case PVFS2_VFS_OP_READDIR:
            if (error_code)
            {
                vfs_request->out_downcall.resp.readdir.dirent_count = 0;
            }
            else
            {
                copy_dirents_to_downcall(vfs_request);
            }
            break;
        case PVFS2_VFS_OP_RENAME:
            break;
        case PVFS2_VFS_OP_TRUNCATE:
            break;
        case PVFS2_VFS_OP_FILE_IO:
            if (error_code == 0)
            {
#ifdef USE_MMAP_RA_CACHE
                if ((vfs_request->in_upcall.req.io.io_type ==
                     PVFS_IO_READ) && vfs_request->io_tmp_buf)
                {
                    void *buf = NULL;
                    /*
                      now that we've read the data, insert it into the
                      mmap_ra_cache here
                    */
                    pvfs2_mmap_ra_cache_register(
                        vfs_request->in_upcall.req.io.refn,
                        vfs_request->io_tmp_buf,
                        vfs_request->in_upcall.req.io.readahead_size);

                    /*
                      get a shared kernel/userspace buffer for the I/O
                      transfer
                    */
                    buf = PINT_dev_get_mapped_buffer(
                        &s_io_desc, vfs_request->in_upcall.req.io.buf_index);
                    assert(buf);

                    /* copy cached data into the shared user/kernel space */
                    memcpy(buf, vfs_request->io_tmp_buf,
                           vfs_request->in_upcall.req.io.count);
                    free(vfs_request->io_tmp_buf);
                    vfs_request->io_tmp_buf = NULL;

                    vfs_request->out_downcall.resp.io.amt_complete =
                        vfs_request->in_upcall.req.io.count;
                }
                else
                {
                    assert(vfs_request->io_tmp_buf == NULL);
                    vfs_request->out_downcall.resp.io.amt_complete =
                        (size_t)vfs_request->response.io.total_completed;
                }
#else
                assert(vfs_request->io_tmp_buf == NULL);
                vfs_request->out_downcall.resp.io.amt_complete =
                    (size_t)vfs_request->response.io.total_completed;
#endif
            }
            break;
        default:
            gossip_err("Completed upcall of unknown type %x!\n",
                       vfs_request->in_upcall.type);
            break;
    }
}

static inline int repost_unexp_vfs_request(
    vfs_request_t *vfs_request, char *completion_handle_desc)
{
    int ret = -PVFS_EINVAL;

    assert(vfs_request);

    PINT_dev_release_unexpected(&vfs_request->info);
    PINT_sys_release(vfs_request->op_id);

    memset(vfs_request, 0, sizeof(vfs_request_t));
    vfs_request->is_dev_unexp = 1;

    ret = PINT_sys_dev_unexp(&vfs_request->info, &vfs_request->jstat,
                             &vfs_request->op_id, vfs_request);
    if (ret < 0)
    {
        PVFS_perror_gossip("PINT_sys_dev_unexp()", ret);
    }
    else
    {
        gossip_debug(GOSSIP_CLIENT_DEBUG, "[*] reposted unexpected "
                     "request [%p] due to %s\n", vfs_request,
                     completion_handle_desc);
    }
    return ret;
}

static inline int handle_unexp_vfs_request(vfs_request_t *vfs_request)
{
    int ret = -PVFS_EINVAL;

    assert(vfs_request);

    if (vfs_request->jstat.error_code)
    {
        PVFS_perror_gossip("job error code",
                           vfs_request->jstat.error_code);
        ret = vfs_request->jstat.error_code;
        goto repost_op;
    }

    gossip_debug(
        GOSSIP_CLIENT_DEBUG, "Got device request message: "
        "size: %d, tag: %lu, payload: %p, op_type: %d\n",
        vfs_request->info.size,
        (unsigned long)vfs_request->info.tag,
        vfs_request->info.buffer,
        vfs_request->in_upcall.type);

    if (vfs_request->info.size >= sizeof(pvfs2_upcall_t))
    {
        memcpy(&vfs_request->in_upcall, vfs_request->info.buffer,
               sizeof(pvfs2_upcall_t));
    }
    else
    {
        gossip_err("Error! Short read from device; aborting!\n");
        ret = -PVFS_EIO;
        goto repost_op;
    }

    if (!remount_complete &&
        (vfs_request->in_upcall.type != PVFS2_VFS_OP_FS_MOUNT))
    {
        gossip_debug(
            GOSSIP_CLIENT_DEBUG, "Got an upcall operation of "
            "type %x before mounting.  ignoring.\n",
            vfs_request->in_upcall.type);
        /*
          if we don't have any mount information yet, just discard the
          op, causing a kernel timeout/retry
        */
        ret = REMOUNT_PENDING;
        goto repost_op;
    }

    gossip_debug(GOSSIP_CLIENT_DEBUG,
                 "[0] handling new unexp vfs_request %p\n",
                 vfs_request);

    switch(vfs_request->in_upcall.type)
    {
        case PVFS2_VFS_OP_LOOKUP:
            ret = post_lookup_request(vfs_request);
            break;
        case PVFS2_VFS_OP_CREATE:
            ret = post_create_request(vfs_request);
            break;
        case PVFS2_VFS_OP_SYMLINK:
            ret = post_symlink_request(vfs_request);
            break;
        case PVFS2_VFS_OP_GETATTR:
            ret = post_getattr_request(vfs_request);
            break;
        case PVFS2_VFS_OP_SETATTR:
            ret = post_setattr_request(vfs_request);
            break;
        case PVFS2_VFS_OP_REMOVE:
            ret = post_remove_request(vfs_request);
            break;
        case PVFS2_VFS_OP_MKDIR:
            ret = post_mkdir_request(vfs_request);
            break;
        case PVFS2_VFS_OP_READDIR:
            ret = post_readdir_request(vfs_request);
            break;
        case PVFS2_VFS_OP_RENAME:
            ret = post_rename_request(vfs_request);
            break;
        case PVFS2_VFS_OP_TRUNCATE:
            ret = post_truncate_request(vfs_request);
            break;
            /*
              NOTE: mount, umount and statfs are blocking
              calls that are serviced inline.
            */
        case PVFS2_VFS_OP_FS_MOUNT:
            ret = service_fs_mount_request(vfs_request);
            break;
        case PVFS2_VFS_OP_FS_UMOUNT:
            ret = service_fs_umount_request(vfs_request);
            break;
        case PVFS2_VFS_OP_STATFS:
            ret = service_statfs_request(vfs_request);
            break;
            /*
              if the mmap-readahead-cache is enabled and we
              get a cache hit for data, the io call is
              blocking and handled inline
            */
        case PVFS2_VFS_OP_FILE_IO:
            ret = post_io_request(vfs_request);
            break;
#ifdef USE_MMAP_RA_CACHE
            /*
              if the mmap-readahead-cache is enabled, cache
              flushes are handled inline
            */
        case PVFS2_VFS_OP_MMAP_RA_FLUSH:
            ret = service_mmap_ra_flush_request(vfs_request);
            break;
#endif
        case PVFS2_VFS_OP_INVALID:
        default:
            gossip_err(
                "Got an unrecognized vfs operation of "
                "type %x.\n", vfs_request->in_upcall.type);
            break;
    }

  repost_op:
    /*
      check if we need to repost the operation (in case of failure or
      inlined handling/completion
    */
    switch(ret)
    {
        case 0:
        {
            /*
              if we've already completed the operation, just repost
              the unexp request
            */
            if (vfs_request->was_handled_inline)
            {
                ret = repost_unexp_vfs_request(
                    vfs_request, "inlined completion");
            }
            else
            {
                /*
                  otherwise, we've just properly posted a non-blocking
                  op; mark it as no longer a dev unexp msg
                */
                vfs_request->is_dev_unexp = 0;
            }
        }
        break;
        case REMOUNT_PENDING:
            ret = repost_unexp_vfs_request(
                vfs_request, "mount pending");
            break;
        default:
            PVFS_perror_gossip("Operation failed", ret);
            ret = repost_unexp_vfs_request(
                vfs_request, "failure");
            break;
    }
    return ret;
}

int process_vfs_requests(void)
{
    int ret = 0, op_count = 0, i = 0;
    vfs_request_t *vfs_request = NULL;
    vfs_request_t *vfs_request_array[MAX_NUM_OPS] = {NULL};
    PVFS_sys_op_id op_id_array[MAX_NUM_OPS];
    int error_code_array[MAX_NUM_OPS] = {0};
    void *buffer_list[MAX_LIST_SIZE];
    int size_list[MAX_LIST_SIZE];
    int list_size = 0, total_size = 0;

    gossip_debug(GOSSIP_CLIENT_DEBUG, "process_vfs_requests called\n");

    /* allocate and post all of our initial unexpected vfs requests */
    for(i = 0; i < MAX_NUM_OPS; i++)
    {
        vfs_request = (vfs_request_t *)malloc(sizeof(vfs_request_t));
        assert(vfs_request);

        s_vfs_request_array[i] = vfs_request;

        memset(vfs_request, 0, sizeof(vfs_request_t));
        vfs_request->is_dev_unexp = 1;

        ret = PINT_sys_dev_unexp(
            &vfs_request->info, &vfs_request->jstat,
            &vfs_request->op_id, vfs_request);

        if (ret < 0)
        {
	    PVFS_perror_gossip("PINT_sys_dev_unexp()", ret);
            return -PVFS_ENOMEM;
        }
    }

    /*
      signal the remount thread to go ahead with the remount attempts
      since we're ready to handle requests now
    */
    pthread_mutex_unlock(&remount_mutex);

    while(s_client_is_processing)
    {
        op_count = MAX_NUM_OPS;
        memset(error_code_array, 0, (MAX_NUM_OPS * sizeof(int)));
        memset(vfs_request_array, 0,
               (MAX_NUM_OPS * sizeof(vfs_request_t *)));

        ret = PINT_sys_testsome(
            op_id_array, &op_count, (void **)vfs_request_array,
            error_code_array, PVFS2_CLIENT_DEFAULT_TEST_TIMEOUT_MS);

        /*
          FIXME: NEED TO TRACK IN PROGRESS TAGS AND MAKE SURE WE'RE
          NOT ADDING ONE FOR AN OP ALREADY IN PROGRESS!!

          also, this helps us also in that we can track in progress
          I/O operations so that when we get a signal (priority
          upcall?) from the kernel to cancel it, we can quickly have
          access to the job_id/fs_id in it to pass to
          PINT_client_io_cancel (or whatever).  HASH ON TAG?
        */
        for(i = 0; i < op_count; i++)
        {
            vfs_request = vfs_request_array[i];
            assert(vfs_request);
            assert(vfs_request->op_id == op_id_array[i]);

            /* check if this is a new dev unexp request */
            if (vfs_request->is_dev_unexp)
            {
                /*
                  NOTE: possible optimization -- if we detect that
                  we're about to handle an inlined/blocking operation,
                  make sure all non-inline ops are posted beforehand
                  so that the sysint test() calls from the blocking
                  operation handling can be making progress on the
                  other ops in progress
                */
                ret = handle_unexp_vfs_request(vfs_request);
                assert(ret == 0);
            }
            else
            {
                /*
                  if this is not a dev unexp msg, it's a non-blocking
                  sysint operation that has just completed
                */
                assert(vfs_request->in_upcall.type);

                package_downcall_members(
                    vfs_request, error_code_array[i]);

                buffer_list[0] = &vfs_request->out_downcall;
                size_list[0] = sizeof(pvfs2_downcall_t);
                total_size = sizeof(pvfs2_downcall_t);
                list_size = 1;

                ret = write_device_response(
                    buffer_list,size_list,list_size, total_size,
                    (PVFS_id_gen_t)vfs_request->info.tag,
                    &vfs_request->op_id, &vfs_request->jstat,
                    s_client_dev_context);

                if (ret < 0)
                {
                    gossip_err("write_device_response failed (tag=%lu)\n",
                               (unsigned long)vfs_request->info.tag);
                }

                ret = repost_unexp_vfs_request(
                    vfs_request, "normal completion");

                assert(ret == 0);
            }
        }
    }

    gossip_debug(GOSSIP_CLIENT_DEBUG, "process_vfs_requests returning\n");
    return 0;
}

int main(int argc, char **argv)
{
    int ret = 0, i = 0;
/*    struct rlimit lim = {0,0}; */

    /* set rlimit to prevent core files */
/*     ret = setrlimit(RLIMIT_CORE, &lim); */
/*     if (ret < 0) */
/*     { */
/* 	fprintf(stderr, "setrlimit system call failed (%d); " */
/*                 "continuing", ret); */
/*     } */

    signal(SIGINT, client_core_sig_handler);

    /*
      initialize pvfs system interface

      NOTE: we do not rely on a pvfstab file at all in here, as
      mounting a pvfs2 volume through the kernel interface now
      requires you to specify a server and fs name in the form of:

      protocol://server/fs_name

      At kernel mount time, we dynamically resolve and add the file
      system mount information to the pvfs2 system interface
    */
    ret = PVFS_sys_initialize(GOSSIP_NO_DEBUG);
    if (ret < 0)
    {
        return ret;
    }

#ifdef USE_MMAP_RA_CACHE
    pvfs2_mmap_ra_cache_initialize();
#endif

    PINT_acache_set_timeout(ACACHE_TIMEOUT_MS);

    ret = PINT_dev_initialize("/dev/pvfs2-req", 0);
    if (ret < 0)
    {
	PVFS_perror("PINT_dev_initialize", ret);
	return -PVFS_EDEVINIT;
    }

    /* setup a mapped region for I/O transfers */
    memset(&s_io_desc, 0 , sizeof(struct PVFS_dev_map_desc));
    ret = PINT_dev_get_mapped_region(&s_io_desc, PVFS2_BUFMAP_TOTAL_SIZE);
    if (ret < 0)
    {
	PVFS_perror("PINT_dev_get_mapped_region", ret);
	return ret;
    }

    ret = job_open_context(&s_client_dev_context);
    if (ret < 0)
    {
	PVFS_perror("device job_open_context failed", ret);
	return ret;
    }

    /*
      lock the remount mutex to make sure the remount isn't started
      until we're ready
    */
    pthread_mutex_lock(&remount_mutex);

    if (pthread_create(&remount_thread, NULL, exec_remount, NULL))
    {
	gossip_err("Cannot create remount thread!");
        return -1;
    }

    ret = process_vfs_requests();
    if (ret)
    {
	gossip_err("Failed to process vfs requests!");
    }

    /* join remount thread; should be long done by now */
    if (remount_complete)
    {
        pthread_join(remount_thread, NULL);
    }
    else
    {
        pthread_cancel(remount_thread);
    }

    /* free all allocated resources */
    for(i = 0; i < MAX_NUM_OPS; i++)
    {
        PINT_dev_release_unexpected(&s_vfs_request_array[i]->info);
        PINT_sys_release(s_vfs_request_array[i]->op_id);
        free(s_vfs_request_array[i]);
    }

    job_close_context(s_client_dev_context);

#ifdef USE_MMAP_RA_CACHE
    pvfs2_mmap_ra_cache_finalize();
#endif

    PINT_dev_finalize();
    PINT_dev_put_mapped_region(&s_io_desc);

    if (PVFS_sys_finalize())
    {
        gossip_err("Failed to finalize PVFS\n");
        return 1;
    }

    gossip_debug(GOSSIP_CLIENT_DEBUG, "%s terminating\n", argv[0]);
    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
