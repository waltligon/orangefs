#include "pvfs-helper.h"
#include <test-pvfs-datatype-vector.h>
#include <stdio.h>

extern pvfs_helper_t pvfs_helper;

int test_pvfs_datatype_vector(MPI_Comm *mycomm, int myid, char *buf, void *params)
{
    int ret = -1, i = 0, j = 0, num_ok = 0;
    PVFS_credentials credentials;
    PVFS_sysresp_lookup resp_lk;
    PVFS_Request req_io;
    PVFS_sysresp_io resp_io;
    char filename[PVFS_NAME_MAX];
    char io_buffer[TEST_PVFS_DATA_SIZE];

    debug_printf("test_pvfs_datatype_vector called\n");

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

    for(i = 0; i < pvfs_helper.num_test_files; i++)
    {
        snprintf(filename,PVFS_NAME_MAX,"%s%.5drank%d",
                 TEST_FILE_PREFIX,i,myid);

        memset(&resp_lk,0,sizeof(PVFS_sysresp_lookup));
        ret = PVFS_sys_lookup(pvfs_helper.resp_init.fsid_list[0],
                              filename, credentials, &resp_lk);
        if (ret < 0)
        {
            debug_printf("test_pvfs_datatype_vector: lookup failed "
                         "on %s\n",filename);
            break;
        }

        /* perform vector I/O on the file handle */
        ret = PVFS_Request_vector(TEST_PVFS_DATA_SIZE,sizeof(char),1,
                                  PVFS_BYTE,&req_io);
        if(ret < 0)
        {
            debug_printf("Error: PVFS_Request_vector() failure.\n");
            break;
        }

	/* TODO: use memory datatype when ready */
        ret = PVFS_sys_write(resp_lk.pinode_refn, req_io, 0, io_buffer,
                             NULL, credentials, &resp_io);
        if(ret < 0)
        {
            debug_printf("Error: PVFS_sys_write() failure.\n");
            break;
        }

        debug_printf("test_pvfsdatatype_vector: wrote %d bytes.\n",
                     (int)resp_io.total_completed);

        /* now try to read the data back */
        memset(io_buffer,0,TEST_PVFS_DATA_SIZE);
	/* TODO: use memory datatype when ready */
        ret = PVFS_sys_read(resp_lk.pinode_refn, req_io, 0, io_buffer,
                            NULL, credentials, &resp_io);
        if(ret < 0)
        {
            debug_printf("Error: PVFS_sys_read() failure (2).\n");
            break;
        }

        debug_printf("test_pvfs_datatype_vector: read %d bytes.\n",
                     (int)resp_io.total_completed);

        /* finally, verify the data */
        for(j = 0; j < TEST_PVFS_DATA_SIZE; j++)
        {
            if (io_buffer[j] != ((j % 26) + 65))
            {
                debug_printf("test_pvfs_datatype_vector: data "
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
