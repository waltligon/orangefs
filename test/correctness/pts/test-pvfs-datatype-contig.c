#include "pvfs-helper.h"
#include <test-pvfs-datatype-contig.h>
#include <stdio.h>

extern pvfs_helper_t pvfs_helper;

int test_pvfs_datatype_contig(MPI_Comm *mycomm, int myid, char *buf, void *params)
{
    int ret = -1, i = 0, j = 0, num_ok = 0;
    PVFS_sysreq_lookup req_lk;
    PVFS_sysresp_lookup resp_lk;
    PVFS_sysreq_io req_io;
    PVFS_sysresp_io resp_io;
    char filename[MAX_TEST_PATH_LEN];
    char io_buffer[TEST_PVFS_DATA_SIZE];

    debug_printf("test_pvfs_datatype_contig called\n");

    memset(&req_io,0,sizeof(PVFS_sysreq_io));
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

    for(i = 0; i < pvfs_helper.num_test_files; i++)
    {
        snprintf(filename,MAX_TEST_PATH_LEN,"%s%.5drank%d",
                 TEST_FILE_PREFIX,i,myid);

        memset(&req_lk,0,sizeof(PVFS_sysreq_lookup));
        memset(&resp_lk,0,sizeof(PVFS_sysresp_lookup));
        req_lk.name = filename;
        req_lk.fs_id = pvfs_helper.resp_init.fsid_list[0];
        req_lk.credentials.uid = 100;
        req_lk.credentials.gid = 100;
        req_lk.credentials.perms = U_WRITE|U_READ;

        ret = PVFS_sys_lookup(&req_lk, &resp_lk);
        if (ret < 0)
        {
            debug_printf("test_pvfs_datatype_contig: lookup failed "
                         "on %s\n",filename);
            break;
        }

        /* perform contig I/O on the file handle */
        req_io.pinode_refn.fs_id = req_lk.fs_id;
        req_io.pinode_refn.handle = resp_lk.pinode_refn.handle;
        req_io.credentials.uid = 100;
        req_io.credentials.gid = 100;
        req_io.credentials.perms = U_WRITE|U_READ;
        req_io.buffer = io_buffer;
        req_io.buffer_size = TEST_PVFS_DATA_SIZE;

        ret = PVFS_Request_contiguous(TEST_PVFS_DATA_SIZE,
                                      PVFS_BYTE,&(req_io.io_req));
        if(ret < 0)
        {
            debug_printf("Error: PVFS_Request_contiguous() failure.\n");
            break;
        }

        ret = PVFS_sys_write(&req_io, &resp_io);
        if(ret < 0)
        {
            debug_printf("Error: PVFS_sys_write() failure.\n");
            break;
        }

        debug_printf("test_pvfsdatatype_contig: wrote %d bytes.\n",
                     (int)resp_io.total_completed);

        /* now try to read the data back */
        memset(io_buffer,0,TEST_PVFS_DATA_SIZE);
        ret = PVFS_sys_read(&req_io, &resp_io);
        if(ret < 0)
        {
            debug_printf("Error: PVFS_sys_write() failure (2).\n");
            break;
        }

        debug_printf("test_pvfs_datatype_contig: read %d bytes.\n",
                     (int)resp_io.total_completed);

        /* finally, verify the data */
        for(j = 0; j < TEST_PVFS_DATA_SIZE; j++)
        {
            if (io_buffer[j] != ((j % 26) + 65))
            {
                debug_printf("test_pvfs_datatype_contig: data "
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
