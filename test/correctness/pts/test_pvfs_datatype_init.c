#include "pvfs_helper.h"
#include <test_pvfs_datatype_init.h>
#include <stdio.h>

extern pvfs_helper_t pvfs_helper;
extern char *pvfs_test_files[NUM_TEST_FILES];

/*
  initialize the sysint and create files to be used by subsequent tests.
*/
int test_pvfs_datatype_init(MPI_Comm *mycomm, int myid, char *buf, void *params)
{
    int ret = -1, i = 0, num_test_files_ok;
    PVFS_sysreq_lookup req_lk;
    PVFS_sysresp_lookup resp_lk;
    PVFS_sysreq_create req_cr;
    PVFS_sysresp_create resp_cr;

    debug_printf("test_pvfs_datatype_init called\n");

    if (initialize_sysint() || (!pvfs_helper.initialized))
    {
        debug_printf("initialize_sysint failed\n");
        return ret;
    }

    /*
      verify that all test files exist.  it's okay if they
      don't exist as we'll try to create them.

      FIXME -- lookup failure
        SHOULD ADJUST THIS AS DISCUSSED WITH PHIL

      this test fails in the following cases:
      - lookup fails for *any* reason
      - create fails
    */
    for(i = 0; i < NUM_TEST_FILES; i++)
    {
        memset(&req_lk,0,sizeof(PVFS_sysreq_lookup));
        req_lk.name = pvfs_test_files[i];
        req_lk.fs_id = pvfs_helper.resp_init.fsid_list[0];
        req_lk.credentials.uid = 100;
        req_lk.credentials.gid = 100;
        req_lk.credentials.perms = U_WRITE|U_READ;

        ret = PVFS_sys_lookup(&req_lk, &resp_lk);
        if (ret < 0)
        {
            debug_printf("init: lookup failed.  creating new file.\n");

            /* get root handle */
            req_lk.name = "/";
            req_lk.fs_id = pvfs_helper.resp_init.fsid_list[0];
            req_lk.credentials.uid = 100;
            req_lk.credentials.gid = 100;
            req_lk.credentials.perms = U_WRITE|U_READ;

            ret = PVFS_sys_lookup(&req_lk, &resp_lk);
            if(ret < 0)
            {
                debug_printf("Error: PVFS_sys_lookup() failed to find "
                             "root handle.\n");
                break;
            }

            /* create new file */

            /*
              TODO: I'm not setting the attribute mask...
              not real sure what's supposed to happen there
            */
            req_cr.attr.owner = 100;
            req_cr.attr.group = 100;
            req_cr.attr.perms = U_WRITE|U_READ;
            req_cr.attr.u.meta.nr_datafiles = 1;
            req_cr.attr.u.meta.dist = NULL;
            req_cr.parent_refn.handle = resp_lk.pinode_refn.handle;
            req_cr.parent_refn.fs_id = req_lk.fs_id;
            /* leave off beginning slash */
            req_cr.entry_name = &(pvfs_test_files[i][1]);
            req_cr.credentials.uid = 100;
            req_cr.credentials.gid = 100;
            req_cr.credentials.perms = U_WRITE|U_READ;

            ret = PVFS_sys_create(&req_cr, &resp_cr);
            if(ret < 0)
            {
                debug_printf("Error: PVFS_sys_create() failure.\n");
                break;
            }
            num_test_files_ok++;
        }
        else
        {
            debug_printf("lookup succeeded; skipping existing file.\n");
            num_test_files_ok++;
        }
    }
    return ((num_test_files_ok == NUM_TEST_FILES) ? 0 : 1);
}
