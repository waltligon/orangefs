#include "pvfs-helper.h"
#include <test-pvfs-datatype-init.h>
#include <stdio.h>

extern pvfs_helper_t pvfs_helper;

/*
  initialize the sysint and create files to be used by subsequent tests.
*/
int test_pvfs_datatype_init(MPI_Comm *mycomm, int myid, char *buf, void *params)
{
    int ret = -1, i = 0, num_test_files_ok;
    uint32_t attrmask;
    PVFS_object_attr attr;
    PVFS_credentials credentials;
    PVFS_sysresp_lookup resp_lk;
    PVFS_sysresp_create resp_cr;
    generic_params *args = (generic_params *)params;
    char filename[MAX_TEST_PATH_LEN];

    debug_printf("test_pvfs_datatype_init called\n");

    if (initialize_sysint() || (!pvfs_helper.initialized))
    {
        debug_printf("initialize_sysint failed\n");
        return ret;
    }
    if (args && args->mode)
    {
        pvfs_helper.num_test_files = args->mode;
        debug_printf("test_pvfs_datatype_init mode is %d\n",
                     args->mode);
    }

    credentials.uid = 100;
    credentials.gid = 100;
    credentials.perms = (PVFS_U_WRITE | PVFS_U_READ);

    /*
      verify that all test files exist.  it's okay if they
      don't exist as we'll try to create them.

      FIXME -- lookup failure
        SHOULD ADJUST THIS AS DISCUSSED WITH PHIL

      this test fails in the following cases:
      - lookup fails for *any* reason
      - create fails
    */
    for(i = 0; i < pvfs_helper.num_test_files; i++)
    {
        snprintf(filename,MAX_TEST_PATH_LEN,"%s%.5drank%d",
                 TEST_FILE_PREFIX,i,myid);

        ret = PVFS_sys_lookup(pvfs_helper.resp_init.fsid_list[0],
                              filename, credentials, &resp_lk);
        if (ret < 0)
        {
            debug_printf("init: lookup failed.  creating new file.\n");

            /* get root handle */
            ret = PVFS_sys_lookup(pvfs_helper.resp_init.fsid_list[0],
                                  "/", credentials, &resp_lk);
            if ((ret < 0) || (!resp_lk.pinode_refn.handle))
            {
                debug_printf("Error: PVFS_sys_lookup() failed to find "
                             "root handle.\n");
                break;
            }


            attrmask = PVFS_ATTR_SYS_ALL_NOSIZE;
            attr.owner = 100;
            attr.group = 100;
            attr.perms = 1877;
            attr.objtype = PVFS_TYPE_METAFILE;
            attr.u.meta.dist = NULL;
            attr.u.meta.nr_datafiles = 1;

            ret = PVFS_sys_create(&(filename[1]),resp_lk.pinode_refn,
                                  attrmask, attr, credentials, &resp_cr);
            if ((ret < 0) || (!resp_cr.pinode_refn.handle))
            {
                debug_printf("Error: PVFS_sys_create() failure.\n");
                break;
            }
            debug_printf("Created file %s\n",&(filename[1]));
            debug_printf("Got handle %Ld.\n",resp_cr.pinode_refn.handle);
            num_test_files_ok++;
        }
        else
        {
            debug_printf("lookup succeeded; skipping existing file.\n");
            debug_printf("Got handle %Ld.\n",resp_lk.pinode_refn.handle);
            num_test_files_ok++;
        }
    }
    return ((num_test_files_ok == pvfs_helper.num_test_files) ? 0 : 1);
}
