#include "pint-sysint.h"
#include "pvfs-helper.h"

pvfs_helper_t pvfs_helper;

int initialize_sysint(void)
{
    int ret = -1;

    memset(&pvfs_helper,0,sizeof(pvfs_helper));

    ret = parse_pvfstab(NULL,&pvfs_helper.mnt);
    if (ret > -1)
    {
        gossip_disable();

        /* init the system interface */
        ret = PVFS_sys_initialize(pvfs_helper.mnt,
                                  &pvfs_helper.resp_init);
        if(ret > -1)
        {
            pvfs_helper.initialized = 1;
            pvfs_helper.num_test_files = NUM_TEST_FILES;
            ret = 0;
        }
        else
        {
            fprintf(stderr, "Error: PVFS_sys_initialize() "
                    "failure. = %d\n", ret);
        }
    }
    else
    {
        fprintf(stderr, "Error: parse_pvfstab() failure.\n");
    }
    return ret;
}

/*
 * helper function to get the root handle
 * fs_id:   fsid of our file system
 *
 * returns:  handle to the root directory
 *      -1 if a problem
 */

PVFS_handle get_root(PVFS_fs_id fs_id)
{
    PVFS_sysreq_lookup req_look;
    PVFS_sysresp_lookup resp_look;
    int ret = -1;

    memset(&req_look, 0, sizeof(req_look));
    memset(&req_look, 0, sizeof(resp_look));

    req_look.credentials.perms = 1877;
    req_look.name = malloc(2);  /*null terminator included */
    req_look.name[0] = '/';
    req_look.name[1] = '\0';
    req_look.fs_id = fs_id;
    printf("looking up the root handle for fsid = %d\n", req_look.fs_id);
    ret = PVFS_sys_lookup(&req_look, &resp_look);
    if (ret < 0)
    {
        printf("Lookup failed with errcode = %d\n", ret);
        return (-1);
    }
    return (PVFS_handle) resp_look.pinode_refn.handle;
}

/*
 * simple helper to make a pvfs2 directory
 *
 * parent:   handle of parent directory
 * fs_id:    fsid of filesystem on which parent dir exists
 * name:     name of directory to create
 *
 * returns a handle to the new directory
 *          -1 if some error happened
 */
PVFS_handle create_dir(PVFS_handle parent,
                       PVFS_fs_id fs_id,
                       char *name)
{
    PVFS_sysreq_mkdir req_mkdir;
    PVFS_sysresp_mkdir resp_mkdir;

    int ret = -1;

    memset(&req_mkdir, 0, sizeof(req_mkdir));
    memset(&resp_mkdir, 0, sizeof(req_mkdir));


    req_mkdir.entry_name = name;
    req_mkdir.parent_refn.handle = parent;
    req_mkdir.parent_refn.fs_id = fs_id;
    req_mkdir.attrmask = ATTR_BASIC;
    req_mkdir.attr.owner = 100;
    req_mkdir.attr.group = 100;
    req_mkdir.attr.perms = 1877;
    req_mkdir.attr.objtype = PVFS_TYPE_DIRECTORY;
    req_mkdir.credentials.perms = 1877;
    req_mkdir.credentials.uid = 100;
    req_mkdir.credentials.gid = 100;

    ret = PVFS_sys_mkdir(&req_mkdir, &resp_mkdir);
    if (ret < 0)
    {
        printf("mkdir failed\n");
        return (-1);
    }
    return (PVFS_handle) resp_mkdir.pinode_refn.handle;
}

/*
 * simple helper to remove a pvfs2 file
 *
 * parent:   handle of parent directory
 * fs_id:    fsid of filesystem on which parent dir exists
 * name:     name of file to remove
 *
 * returns 0 on success.
 *          -1 if some error happened.
 */
int remove_file(PVFS_handle parent,
                       PVFS_fs_id fs_id,
                       char *name)
{
    PVFS_sysreq_remove req_remove;

    int ret = -1;

    memset(&req_remove, 0, sizeof(PVFS_sysreq_remove));

    req_remove.entry_name = name;
    req_remove.parent_refn.handle = parent;
    req_remove.parent_refn.fs_id = fs_id;
    req_remove.credentials.perms = 1877;
    req_remove.credentials.uid = 100;
    req_remove.credentials.gid = 100;

    ret = PVFS_sys_remove(&req_remove);
    if (ret < 0)
    {
        printf("remove failed\n");
        return ret;
    }
    return 0;
}

/*
 * simple helper to remove a pvfs2 dir
 *
 * parent:   handle of parent directory
 * fs_id:    fsid of filesystem on which parent dir exists
 * name:     name of dir to remove
 *
 * returns 0 on success.
 *          -1 if some error happened.
 */
int remove_dir(PVFS_handle parent,
                       PVFS_fs_id fs_id,
                       char *name)
{
    PVFS_sysreq_remove req_remove;

    int ret = -1;

    memset(&req_remove, 0, sizeof(PVFS_sysreq_remove));

    req_remove.entry_name = name;
    req_remove.parent_refn.handle = parent;
    req_remove.parent_refn.fs_id = fs_id;
    req_remove.credentials.perms = 1877;
    req_remove.credentials.uid = 100;
    req_remove.credentials.gid = 100;

    ret = PVFS_sys_remove(&req_remove);
    if (ret < 0)
    {
        printf("remove failed\n");
        return ret;
    }
    return 0;
}

/*
 * simple helper to lookup a handle given a filename
 *
 * parent:   handle of parent directory
 * fs_id:    fsid of filesystem on which parent dir exists
 * name:     name of directory to create
 *
 * returns a handle to the new directory
 *          -1 if some error happened
 */
PVFS_handle lookup_name(char *name,
                       PVFS_fs_id fs_id)
{
    PVFS_sysreq_lookup req_lookup;
    PVFS_sysresp_lookup resp_lookup;

    int ret = -1;

    memset(&req_lookup, 0, sizeof(req_lookup));
    memset(&resp_lookup, 0, sizeof(req_lookup));


    req_lookup.name = name;
    req_lookup.fs_id = fs_id;
    req_lookup.credentials.uid = 100;
    req_lookup.credentials.gid = 100;
    req_lookup.credentials.perms = U_WRITE|U_READ;

    ret = PVFS_sys_lookup(&req_lookup,&resp_lookup);
    if (ret < 0)
    {
       printf("Lookup failed with errcode = %d\n", ret);
       return(-1);
    }

    return (PVFS_handle) resp_lookup.pinode_refn.handle;
}

