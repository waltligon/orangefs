/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup pvfs2linux
 *
 *  Linux VFS directory operations.
 */

#include "pvfs2-kernel.h"
#include "pvfs2-bufmap.h"
#include "pvfs2-sysint.h"
#include "pvfs2-internal.h"

typedef struct 
{
    int buffer_index;
    pvfs2_readdir_response_t readdir_response;
    void *dents_buf;
} readdir_handle_t;

/* decode routine needed by kmod to make sense of the shared page for readdirs */
static long decode_dirents(char *ptr, pvfs2_readdir_response_t *readdir) 
{
    int i;
    pvfs2_readdir_response_t *rd = (pvfs2_readdir_response_t *) ptr;
    char *buf = ptr;
    char **pptr = &buf;

    readdir->token = rd->token;
    readdir->pvfs_dirent_outcount = rd->pvfs_dirent_outcount;
    readdir->dirent_array = kmalloc(readdir->pvfs_dirent_outcount *
                                    sizeof(*readdir->dirent_array), GFP_KERNEL);
    if (readdir->dirent_array == NULL)
    {
        return -ENOMEM;
    }
    *pptr += offsetof(pvfs2_readdir_response_t, dirent_array);
    for (i = 0; i < readdir->pvfs_dirent_outcount; i++)
    {
        dec_string(pptr, &readdir->dirent_array[i].d_name, &readdir->dirent_array[i].d_length);
        readdir->dirent_array[i].handle = *(int64_t *) *pptr;
        *pptr += 8;
    }
    return ((unsigned long) *pptr - (unsigned long) ptr);
}

static long readdir_handle_ctor(readdir_handle_t *rhandle, void *buf, int buffer_index)
{
    long ret;

    if (buf == NULL)
    {
        gossip_err("Invalid NULL buffer specified in readdir_handle_ctor\n");
        return -ENOMEM;
    }
    if (buffer_index < 0)
    {
        gossip_err("Invalid buffer index specified in readdir_handle_ctor\n");
        return -EINVAL;
    }
    rhandle->buffer_index = buffer_index;
    rhandle->dents_buf = buf;
    if ((ret = decode_dirents(buf, &rhandle->readdir_response)) < 0)
    {
        gossip_err("Could not decode readdir from buffer %ld\n", ret);
        readdir_index_put(rhandle->buffer_index);
        rhandle->buffer_index = -1;
        gossip_debug(GOSSIP_DIR_DEBUG, "vfree %p\n", buf);
        vfree(buf);
        rhandle->dents_buf = NULL;
    }
    return ret;
}

static void readdir_handle_dtor(readdir_handle_t *rhandle)
{
    if (rhandle == NULL)
    {
        return;
    }
    if (rhandle->readdir_response.dirent_array)
    {
        kfree(rhandle->readdir_response.dirent_array);
        rhandle->readdir_response.dirent_array = NULL;
    }
    if (rhandle->buffer_index >= 0)
    {
        readdir_index_put(rhandle->buffer_index);
        rhandle->buffer_index = -1;
    }
    if (rhandle->dents_buf)
    {
        gossip_debug(GOSSIP_DIR_DEBUG, "vfree %p\n", rhandle->dents_buf);
        vfree(rhandle->dents_buf);
        rhandle->dents_buf = NULL;
    }
    return;
}

/** Read directory entries from an instance of an open directory.
 *
 * \note This routine was converted for the readdir to iterate change
 *       in "struct file_operations". "converted" mostly amounts to
 *       changing occurrences of "readdir" and "filldir" in the
 *       comments to "iterate" and "dir_emit". Also readdir calls
 *       were changed to dir_emit calls. pvfs2_readdir seems to work,
 *       but needs to be scrutinized...
 *
 * \param dir_emit callback function called for each entry read.
 *
 * \retval <0 on error
 * \retval 0  when directory has been completely traversed
 * \retval >0 if we don't call dir_emit for all entries
 *
 * \note If the dir_emit call-back returns non-zero, then iterate should
 *       assume that it has had enough, and should return as well.
 */
static int pvfs2_readdir(
    struct file *file,
    struct dir_context *ctx)
{
    int ret = 0, buffer_index, token_set = 0;
    PVFS_ds_position pos = 0;
    ino_t ino = 0;
    struct dentry *dentry = file->f_dentry;
    pvfs2_kernel_op_t *new_op = NULL;
    pvfs2_inode_t *pvfs2_inode = PVFS2_I(dentry->d_inode);
    int buffer_full = 0;
    readdir_handle_t rhandle;
    int i = 0, len = 0;
    ino_t current_ino = 0;
    char *current_entry = NULL;
    long bytes_decoded;

    gossip_ldebug(GOSSIP_DIR_DEBUG,"Entering %s.\n",__func__);

    gossip_ldebug(GOSSIP_DIR_DEBUG,"%s: file->f_pos:%lld\n",__func__,lld(file->f_pos));

    pos = (PVFS_ds_position)file->f_pos;

    /* are we done? */
    if (pos == PVFS_READDIR_END)
    {
        gossip_debug(GOSSIP_DIR_DEBUG, 
                     "Skipping to graceful termination path since we are done\n");
        return (0);
    }

    gossip_debug(GOSSIP_DIR_DEBUG, "pvfs2_readdir called on %s (pos=%llu)\n",
                 dentry->d_name.name, llu(pos));

    rhandle.buffer_index = -1;
    rhandle.dents_buf = NULL;
    memset(&rhandle.readdir_response, 0, sizeof(rhandle.readdir_response));

    new_op = op_alloc(PVFS2_VFS_OP_READDIR);
    if (!new_op)
    {
       return (-ENOMEM);
    }

    new_op->uses_shared_memory = 1;

    if (pvfs2_inode && (pvfs2_inode->refn.handle != PVFS_HANDLE_NULL)
                    && ( pvfs2_inode->refn.fs_id != PVFS_FS_ID_NULL)  )
    {
        new_op->upcall.req.readdir.refn = pvfs2_inode->refn;
        gossip_debug(GOSSIP_DIR_DEBUG,"%s: upcall.req.readdir.refn.handle:%llu\n"
                                     ,__func__
                                     ,llu(new_op->upcall.req.readdir.refn.handle));
    }
    else
    {
#if defined(HAVE_IGET5_LOCKED) || defined(HAVE_IGET4_LOCKED)
        gossip_lerr("Critical error: i_ino cannot be relied on when using iget4/5\n");
        op_release(new_op);
        return -EINVAL;
#endif
	new_op->upcall.req.readdir.refn.handle = get_handle_from_ino(dentry->d_inode);
	new_op->upcall.req.readdir.refn.fs_id  = PVFS2_SB(dentry->d_inode->i_sb)->fs_id;
        gossip_debug(GOSSIP_DIR_DEBUG,"%s: upcall.req.readdir.refn.handle:%llu\n"
                                     ,__func__
                                     ,llu(new_op->upcall.req.readdir.refn.handle));
    }

    new_op->upcall.req.readdir.max_dirent_count = MAX_DIRENT_COUNT_READDIR;

    /* NOTE:
     * the position we send to the readdir upcall is out of sync with file->f_pos 
     * since pvfs2 doesn't include the "." and ".." entries that are added below.  
     */
    new_op->upcall.req.readdir.token = (pos == 0 ? PVFS_READDIR_START : pos);

get_new_buffer_index:
    ret = readdir_index_get(&buffer_index);
    if (ret < 0)
    {
        gossip_lerr("pvfs2_readdir: readdir_index_get() failure (%d)\n", ret);
        op_release(new_op);
        return(ret);
    }
    new_op->upcall.req.readdir.buf_index = buffer_index;

    ret = service_operation( new_op, 
                             "pvfs2_readdir", 
                             get_interruptible_flag(dentry->d_inode));

    gossip_debug(GOSSIP_DIR_DEBUG, "Readdir downcall status is %d.  ret:%d\n",
		                    new_op->downcall.status,ret);

    if ( ret == -EAGAIN && op_state_purged(new_op) )
    {
       /* readdir shared memory aread has been wiped due to pvfs2-client-core restarting, so
        * we must get a new index into the shared memory.
        */
       gossip_debug(GOSSIP_DIR_DEBUG,"%s: Getting new buffer_index for retry of readdir..\n",__func__);
       goto get_new_buffer_index;
    }

    if ( ret == -EIO && op_state_purged(new_op) )
    {
        /* pvfs2-client is down.  Readdir shared memory area has been wiped clean.  No need to "put"
         * back the buffer_index.
         */
        gossip_err("%s: Client is down.  Aborting readdir call. \n",__func__);
        op_release(new_op);
        return (ret);
    }

    if ( ret < 0  || new_op->downcall.status != 0 )
    {
         gossip_debug(GOSSIP_DIR_DEBUG,
                      "Readdir request failed.  Status:%d\n",
                      new_op->downcall.status);
         readdir_index_put(buffer_index);
         op_release(new_op);
         return ( (ret < 0 ? ret : new_op->downcall.status) );
    }

    if ( (bytes_decoded = readdir_handle_ctor(&rhandle, 
                                              new_op->downcall.trailer_buf,
                                              buffer_index))  <  0 )
    { 
       gossip_err("pvfs2_readdir: Could not decode trailer buffer "
                  " into a readdir response %d\n", ret);
       ret = bytes_decoded;
       readdir_index_put(buffer_index);
       op_release(new_op);
       return(ret);
    }

    if (bytes_decoded != new_op->downcall.trailer_size)
    {
        gossip_err("pvfs2_readdir: # bytes "
                   "decoded (%ld) != trailer size (%ld)\n",
                    bytes_decoded, (long) new_op->downcall.trailer_size);
        ret = -EINVAL;
        readdir_handle_dtor(&rhandle);
        op_release(new_op);
        return (ret);
    }

    if (pos == 0)
    {
       token_set = 1;
       ino = get_ino_from_handle(dentry->d_inode);
       gossip_debug(GOSSIP_DIR_DEBUG,"%s: calling dir_emit of \".\" with pos = %llu\n"
                                    ,__func__
                                    ,llu(pos));
       if ( (ret=dir_emit(ctx,".",1,ino,DT_DIR)) < 0)
       {
          readdir_handle_dtor(&rhandle);
          op_release(new_op);
          return(ret);
       }
       ctx->pos++;
       gossip_ldebug(GOSSIP_DIR_DEBUG,"%s: file->f_pos:%lld\n",__func__,lld(file->f_pos));
       pos++;
    }

    if (pos == 1)
    {
       token_set = 1;
       ino = get_parent_ino_from_dentry(dentry);
       gossip_debug(GOSSIP_DIR_DEBUG,"%s: calling dir_emit of \"..\" with pos = %llu\n"
                                    ,__func__
                                    ,llu(pos));
       if ( (ret=dir_emit(ctx,"..",2,ino,DT_DIR)) < 0)
       {
          readdir_handle_dtor(&rhandle);
          op_release(new_op);
          return(ret);
       }
       ctx->pos++;
       gossip_ldebug(GOSSIP_DIR_DEBUG,"%s: file->pos:%lld\n",__func__,lld(file->f_pos));
       pos++;
    }

    for (i = 0; i < rhandle.readdir_response.pvfs_dirent_outcount; i++)
    {
                  len = rhandle.readdir_response.dirent_array[i].d_length;
        current_entry = rhandle.readdir_response.dirent_array[i].d_name;
        current_ino   = pvfs2_handle_to_ino( rhandle.readdir_response.dirent_array[i].handle);

        gossip_debug(GOSSIP_DIR_DEBUG, 
                    "calling dir_emit for %s with len %d, pos %ld\n",
                     current_entry, len, (unsigned long) pos);
        if ( (ret=dir_emit(ctx,current_entry,len,current_ino,DT_UNKNOWN)) < 0)
        {
           gossip_debug(GOSSIP_DIR_DEBUG, "dir_emit() failed. ret:%d\n",ret);
           if (token_set && (i < 2))
           {
              gossip_err("dir_emit failed on one of the first two true PVFS directory entries.\n");
              gossip_err("Duplicate entries may appear.\n");
           }
           buffer_full = 1;
           break;
        }
       ctx->pos++;
        gossip_ldebug(GOSSIP_DIR_DEBUG,"%s: file->pos:%lld\n",__func__,lld(file->f_pos));
        
        pos++;
    }
    
    /* For the first time around, use the token returned by the readdir response */
    if (token_set == 1) 
    {
       /* this means that all of the dir_emit calls succeeded */
       if (i == rhandle.readdir_response.pvfs_dirent_outcount)
       {
           file->f_pos = rhandle.readdir_response.token;
       }
       else 
       {
           /* this means a dir_emit call failed */
           if(rhandle.readdir_response.token == PVFS_READDIR_END)
           {
             /* If PVFS hit end of directory, then there is no
              * way to do math on the token that it returned.
              * Instead we go by the f_pos but back up to account for
              * the artificial . and .. entries.  The fact that
              * "token_set" is non zero indicates that we are on
              * the first iteration of getdents(). 
              */
              file->f_pos -= 3;
           }
           else
           {
              file->f_pos = rhandle.readdir_response.token - 
                            (rhandle.readdir_response.pvfs_dirent_outcount - i + 1);
           }
           gossip_debug(GOSSIP_DIR_DEBUG, "at least one dir_emit call failed.  "
                                          "Setting f_pos to: %lld\n"
                                         , lld(file->f_pos));
       }
    }/*end if token_set to 1*/
            
    /* did we hit the end of the directory? */
    if(rhandle.readdir_response.token == PVFS_READDIR_END && !buffer_full)
    {
       gossip_debug(GOSSIP_DIR_DEBUG,
                    "End of dir detected; setting f_pos to PVFS_READDIR_END.\n");
       file->f_pos = PVFS_READDIR_END;
    }

    gossip_debug(GOSSIP_DIR_DEBUG,"pos = %llu, file->f_pos is %lld\n",
                                  llu(pos),
                                  lld(file->f_pos));

    if (ret == 0)
    {
        gossip_debug(GOSSIP_DIR_DEBUG, 
                     "pvfs2_readdir about to update_atime %p\n", 
                     dentry->d_inode);

        SetAtimeFlag(pvfs2_inode);
        dentry->d_inode->i_atime = CURRENT_TIME;
        mark_inode_dirty_sync(dentry->d_inode);
    }

    readdir_handle_dtor(&rhandle);
    op_release(new_op);

    gossip_debug(GOSSIP_DIR_DEBUG, "pvfs2_readdir returning %d\n",ret);

    return (ret);
}/*end pvfs2_readdir*/

/** PVFS2 implementation of VFS directory operations */
struct file_operations pvfs2_dir_operations =
{
    .read = generic_read_dir,
    .iterate = pvfs2_readdir,
    .open = pvfs2_file_open,
    .release = pvfs2_file_release,
};

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
