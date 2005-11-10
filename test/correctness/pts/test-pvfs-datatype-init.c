/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <time.h>
#include <stdio.h>
#include "pvfs-helper.h"
#include "test-pvfs-datatype-init.h"

/*
  initialize the sysint and create files to be used by subsequent
  tests.
*/
int test_pvfs_datatype_init(
    MPI_Comm *mycomm __unused,
    int myid,
    char *buf __unused,
    void *params)
{
    int ret = -1, i = 0, num_test_files_ok = 0;
    PVFS_sys_attr attr;
    PVFS_credentials credentials;
    PVFS_sysresp_lookup resp_lk;
    PVFS_sysresp_create resp_cr;
    generic_params *args = (generic_params *)params;
    char filename[PVFS_NAME_MAX];

    debug_printf("test_pvfs_datatype_init called\n");

    if (!pvfs_helper.initialized && initialize_sysint())
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

    PVFS_util_gen_credentials(&credentials);

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
        snprintf(filename,PVFS_NAME_MAX,"%s%.5drank%d",
                 TEST_FILE_PREFIX,i,myid);

        ret = PVFS_sys_lookup(pvfs_helper.fs_id,
                              filename, &credentials, &resp_lk,
                              PVFS2_LOOKUP_LINK_NO_FOLLOW);
        if (ret < 0)
        {
            debug_printf("init: lookup failed.  creating new file.\n");

            /* get root handle */
            ret = PVFS_sys_lookup(pvfs_helper.fs_id,
                                  "/", &credentials, &resp_lk,
                                  PVFS2_LOOKUP_LINK_NO_FOLLOW);
            if ((ret < 0) || (!resp_lk.ref.handle))
            {
                debug_printf("Error: PVFS_sys_lookup() failed to find "
                             "root handle.\n");
                break;
            }


            attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;
            attr.owner = credentials.uid;
            attr.group = credentials.gid;
            attr.perms = 1877;
	    attr.atime = attr.mtime = attr.ctime = 
		time(NULL);

            ret = PVFS_sys_create(&(filename[1]),resp_lk.ref,
                                  attr, &credentials, NULL, &resp_cr);
            if ((ret < 0) || (!resp_cr.ref.handle))
            {
                debug_printf("Error: PVFS_sys_create() failure.\n");
                break;
            }
            debug_printf("Created file %s\n",&(filename[1]));
            debug_printf("Got handle %lld.\n", lld(resp_cr.ref.handle));
            num_test_files_ok++;
        }
        else
        {
            debug_printf("lookup succeeded; skipping existing file.\n");
            debug_printf("Got handle %lld.\n", lld(resp_lk.ref.handle));
            num_test_files_ok++;
        }
    }
    return ((num_test_files_ok == pvfs_helper.num_test_files) ? 0 : 1);
}
