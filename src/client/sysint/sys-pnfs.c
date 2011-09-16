/*
 * (C) Dean Hildebrand
 * (C) 2011 Omnibond Systema, LLC
 *
 * This state machine injects the required information into the PVFS2
 * client cache to perform I/O for pNFS.
 */

#include "gossip.h"
#include "pvfs2-debug.h"

#ifdef __PVFS2_PNFS_SUPPORT__
PVFS_error PVFS_sys_setlayout(
    PVFS_object_ref ref,
    void* layout,
    PVFS_credentials *credentials)
{
    int ret = -PVFS_EINVAL;

    gossip_debug(GOSSIP_CLIENT_DEBUG, "%s: enter\n", __func__);

    if ((ref.handle == PVFS_HANDLE_NULL) || (ref.fs_id == PVFS_FS_ID_NULL)) 
    {
       gossip_err("%s: invalid (NULL) required argument\n", __func__);
       return ret;
    }

    gossip_debug(GOSSIP_CLIENT_DEBUG, "%s: setlayout on (%d)(%llu)\n",
                 __func__, ref.fs_id, llu(ref.hanlde));
    ret = setlayout_inject(ref, layout);
    return ret;
}

/* Deserialize the layout (dfiles and dist) and set in the
 * cache attribute struct
*/
static int deserialize_layout(char* layout, PVFS_metafile_attr* meta)
{
    int blob_size=0, fs_id=0;

    gossip_debug(GOSSIP_CLIENT_DEBUG, "%s: entern", __func__);

    /* Size of entire opaque object */
    decode_int32_t(&layout, &blob_size);
    /* Size of entire opaque object */
    decode_int32_t(&layout, &fs_id);
    gossip_debug(GOSSIP_CLIENT_DEBUG, "%s: (%d): blob size: %d\n",
                 __func__, fs_id, blob_size);

    /* Deserialize dfile array */
    decode_PVFS_metafile_attr_dfiles(&layout, meta);
    gossip_debug(GOSSIP_CLIENT_DEBUG, "%s: dfile count: %d, dfile[0]: %llu\n",
                 __func__, meta->dfile_count, llu(meta->dfile_array[0]));

    /* Deserialize distribution struct */
    decode_PVFS_metafile_attr_dist(&layout, meta);
    gossip_debug(GOSSIP_CLIENT_DEBUG, "%s: dist_size: %d\n", __func__, 
                meta->dist_size);
    PINT_dist_dump(meta->dist);
    gossip_debug(GOSSIP_CLIENT_DEBUG, "%s: exit\n", __func__);
    return 0;
}

static int setlayout_inject(PVFS_object_ref refn, void* layout)
{
    int ret = -PVFS_EINVAL;
    PVFS_object_attr attr;
    PVFS_size tempsz = 0;

    gossip_debug(GOSSIP_CLIENT_DEBUG, "%s: enter\n", __func__);

    if (layout == NULL)
    {
       gossip_err("%s: layout is null\n", __func__);
       ret = -EIO;
       goto out;
    }

    /* Set (mostly false) attributes on the cached inode */
    attr.mask = (PVFS_ATTR_META_ALL | PVFS_ATTR_COMMON_ALL);
    /*    attr.mask &= !(PVFS_ATTR_SYMLNK_TARGET); */
    attr.objtype = PVFS_TYPE_METAFILE;

    /* Decode the blob layout */
    if ((ret = deserialize_layout((char*)layout, &attr.u.meta)))
    {
       gossip_err("%s: Could not deserialize layout %d!\n", __func__, ret);
       goto out;
    }

    ret = PINT_acache_update(refn, &attr, &tempsz);
    if (ret)
    {
       gossip_err("%s: : Could not set layout in cache %d!\n", __func__, ret);
    }

    PINT_free_object_attr(&attr);
out:
    gossip_debug(GOSSIP_CLIENT_DEBUG, "%s: exit\n", __func__, ret);
    return ret;
}
#endif /* PVFS2_PNFS_SUPPORT */

/*
 * Local variables:
 *  mode: c
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ft=c ts=8 sts=4 sw=4 noexpandtab
 */
