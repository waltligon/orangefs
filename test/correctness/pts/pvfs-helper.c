#include <time.h>

#include "pint-sysint-utils.h"
#include "pvfs-helper.h"
#include "pvfs2-util.h"

pvfs_helper_t pvfs_helper;

int initialize_sysint(void)
{
    int ret = -1;

    memset(&pvfs_helper,0,sizeof(pvfs_helper));

    ret = PVFS_util_init_defaults();
    if(ret < 0)
    {
	PVFS_perror("PVFS_util_init_defaults", ret);
	return(ret);
    }

    ret = PVFS_util_get_default_fsid(&pvfs_helper.fs_id);
    if(ret < 0)
    {
	PVFS_perror("PVFS_util_get_default_fsid", ret);
	return(ret);
    }

    pvfs_helper.initialized = 1;
    pvfs_helper.num_test_files = NUM_TEST_FILES;

    return 0;
}

int finalize_sysint(void)
{
    int ret = PVFS_sys_finalize();
    pvfs_helper.initialized = 0;
    return ret;
}

/*
 * helper function to fill in the root pinode_refn
 * fs_id:   fsid of our file system
 *
 * returns:  0 on success; 
 *      -1 if a problem
 */
int get_root(PVFS_fs_id fs_id, PVFS_object_ref *pinode_refn)
{
    int ret = -1;
    PVFS_credentials credentials;
    PVFS_sysresp_lookup resp_look;
    char *root = "/";

    if (pinode_refn)
    {
        memset(&resp_look, 0, sizeof(resp_look));

        PVFS_util_gen_credentials(&credentials);

        printf("looking up the root handle for fsid = %d\n", fs_id);
        ret = PVFS_sys_lookup(fs_id, root, credentials,
                              &resp_look, PVFS2_LOOKUP_LINK_NO_FOLLOW);
        if (ret < 0)
        {
            printf("Lookup failed with errcode = %d\n", ret);
        }
        memcpy(pinode_refn, &resp_look.ref,
               sizeof(PVFS_object_ref));
    }
    return ret;
}

int create_dir(PVFS_object_ref parent_refn, char *name,
               PVFS_object_ref *out_refn)
{
    int ret = -1;
    PVFS_sys_attr attr;
    PVFS_credentials credentials;
    PVFS_sysresp_mkdir resp_mkdir;

    memset(&resp_mkdir, 0, sizeof(resp_mkdir));

    PVFS_util_gen_credentials(&credentials);

    attr.owner = credentials.uid;
    attr.group = credentials.gid;
    attr.atime = attr.mtime = attr.ctime = 
	time(NULL);
    attr.perms = (PVFS_U_WRITE | PVFS_U_READ);
    attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;

    ret = PVFS_sys_mkdir(name, parent_refn,
                         attr, credentials, &resp_mkdir);
    if (ret < 0)
    {
        printf("mkdir failed\n");
        return (-1);
    }
    if (out_refn)
    {
        memset(out_refn, 0, sizeof(PVFS_object_ref));
        memcpy(out_refn, &resp_mkdir.ref,
               sizeof(PVFS_object_ref));
    }
    return 0;
}

/*
 * simple helper to remove a pvfs2 file
 *
 * returns 0 on success.
 *          -1 if some error happened.
 */
int remove_file(PVFS_object_ref parent_refn, char *name)
{
    int ret = -1;
    PVFS_credentials credentials;

    PVFS_util_gen_credentials(&credentials);

    ret = PVFS_sys_remove(name, parent_refn, credentials);
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
 * returns 0 on success.
 *          -1 if some error happened.
 */
int remove_dir(PVFS_object_ref parent_refn, char *name)
{
    return remove_file(parent_refn, name);
}

/*
 * simple helper to lookup a handle given a filename
 *
 * returns a handle to the new directory
 *          -1 if some error happened
 */
int lookup_name(PVFS_object_ref pinode_refn, char *name,
                PVFS_object_ref *out_refn)
{
    int ret = -1;
    PVFS_credentials credentials;
    PVFS_sysresp_lookup resp_lookup;

    memset(&resp_lookup, 0, sizeof(resp_lookup));

    PVFS_util_gen_credentials(&credentials);

    ret = PVFS_sys_lookup(pinode_refn.fs_id, name,
                          credentials, &resp_lookup,
                          PVFS2_LOOKUP_LINK_NO_FOLLOW);
    if (ret < 0)
    {
       printf("Lookup failed with errcode = %d\n", ret);
       return(-1);
    }
    if (out_refn)
    {
        memcpy(out_refn, &resp_lookup.ref,
               sizeof(PVFS_object_ref));
    }
    return 0;
}
