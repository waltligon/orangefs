#include "pvfs-helper.h"
#include <test-pvfs-datatype-hvector.h>
#include <stdio.h>

extern pvfs_helper_t pvfs_helper;

int test_pvfs_datatype_hvector(MPI_Comm *mycomm, int myid, char *buf, void *params)
{
    int ret = -1, i = 0, j = 0, num_ok = 0;
    PVFS_credentials credentials;
    PVFS_sysresp_lookup resp_lk;
    PVFS_Request req_io;
    PVFS_sysresp_io resp_io;
    char filename[MAX_TEST_PATH_LEN];
    char io_buffer[TEST_PVFS_DATA_SIZE];

    debug_printf("test_pvfs_datatype_hvector called\n");

    memset(&req_io,0,sizeof(PVFS_Request));
    memset(&resp_io,0,sizeof(PVFS_sysresp_io));

    if (!pvfs_helper.initialized)
    {
        debug_printf("test_pvfs_datatype_config cannot be initialized!\n");
        return ret;
    }

    for(i = 0; i < TEST_PVFS_DATA_SIZE; i++)
    {
        io_buffer[i] = ((i % 26) + 65);
    }

    credentials.uid = 100;
    credentials.gid = 100;
    credentials.perms = (PVFS_U_WRITE | PVFS_U_READ);

    for(i = 0; i < pvfs_helper.num_test_files; i++)
    {
        snprintf(filename,MAX_TEST_PATH_LEN,"%s%.5drank%d",
                 TEST_FILE_PREFIX,i,myid);

        memset(&resp_lk,0,sizeof(PVFS_sysresp_lookup));
        ret = PVFS_sys_lookup(pvfs_helper.resp_init.fsid_list[0],
                              filename, credentials, &resp_lk);
        if (ret < 0)
        {
            debug_printf("test_pvfs_datatype_hvector: lookup failed "
                         "on %s\n",filename);
            break;
        }

        /* perform hvector I/O on the file handle */
        ret = PVFS_Request_hvector(TEST_PVFS_DATA_SIZE,sizeof(char),1,
                                   PVFS_BYTE, &req_io);
        if(ret < 0)
        {
            debug_printf("Error: PVFS_Request_hvector() failure.\n");
            break;
        }

        ret = PVFS_sys_write(resp_lk.pinode_refn, req_io, io_buffer,
                             TEST_PVFS_DATA_SIZE, credentials, &resp_io);
        if(ret < 0)
        {
            debug_printf("Error: PVFS_sys_write() failure.\n");
            break;
        }

        debug_printf("test_pvfsdatatype_hvector: wrote %d bytes.\n",
                     (int)resp_io.total_completed);

        /* now try to read the data back */
        memset(io_buffer,0,TEST_PVFS_DATA_SIZE);
        ret = PVFS_sys_read(resp_lk.pinode_refn, req_io, io_buffer,
                            TEST_PVFS_DATA_SIZE, credentials, &resp_io);
        if(ret < 0)
        {
            debug_printf("Error: PVFS_sys_write() failure (2).\n");
            break;
        }

        debug_printf("test_pvfs_datatype_hvector: read %d bytes.\n",
                     (int)resp_io.total_completed);

        /* finally, verify the data */
        for(j = 0; j < TEST_PVFS_DATA_SIZE; j++)
        {
            if (io_buffer[j] != ((i % 26) + 65))
            {
                debug_printf("test_pvfs_datatype_hvector: data "
                             "verification failed\n");
                break;
            }
        }
        if (j != TEST_PVFS_DATA_SIZE)
        {
            break;
        }

        num_ok++;
    }
    return ((num_ok == pvfs_helper.num_test_files) ? 0 : 1);
}
