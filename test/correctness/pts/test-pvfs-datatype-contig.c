#include "pvfs-helper.h"
#include <test-pvfs-datatype-contig.h>
#include <stdio.h>

int test_pvfs_datatype_contig(MPI_Comm *mycomm __unused, int myid, char *buf __unused, void *params __unused)
{
    int ret = -1, i = 0, j = 0, num_ok = 0;
    PVFS_credentials credentials;
    PVFS_sysresp_lookup resp_lk;
    PVFS_sysresp_io resp_io;
    PVFS_Request req_io;
    PVFS_Request req_mem;
    char filename[PVFS_NAME_MAX];
    char io_buffer[TEST_PVFS_DATA_SIZE];

    debug_printf("test_pvfs_datatype_contig called\n");

    memset(&req_io,0,sizeof(PVFS_Request));
    memset(&req_mem,0,sizeof(PVFS_Request));
    memset(&resp_io,0,sizeof(PVFS_sysresp_io));

    if (!pvfs_helper.initialized)
    {
        debug_printf("test_pvfs_datatype_config cannot be initialized!\n");
        return ret;
    }

    for(i = 0; i < TEST_PVFS_DATA_SIZE; i++)
    {
	int ti = (i % 26) + 65;
        io_buffer[i] = (char) ti;
    }

    credentials.uid = getuid();
    credentials.gid = getgid();

    for(i = 0; i < pvfs_helper.num_test_files; i++)
    {
        snprintf(filename,PVFS_NAME_MAX,"%s%.5drank%d",
                 TEST_FILE_PREFIX,i,myid);

        memset(&resp_lk,0,sizeof(PVFS_sysresp_lookup));
        ret = PVFS_sys_lookup(pvfs_helper.fs_id,
                              filename, credentials, &resp_lk,
                              PVFS2_LOOKUP_LINK_NO_FOLLOW);
        if (ret < 0)
        {
            debug_printf("test_pvfs_datatype_contig: lookup failed "
                         "on %s\n",filename);
            break;
        }

        /* perform contig I/O on the file handle */
        ret = PVFS_Request_contiguous(TEST_PVFS_DATA_SIZE,
                                      PVFS_BYTE, &req_io);
        if(ret < 0)
        {
            debug_printf("Error: PVFS_Request_contiguous() failure.\n");
            break;
        }
	ret = PVFS_Request_contiguous(TEST_PVFS_DATA_SIZE,
                                      PVFS_BYTE, &req_mem);
        if(ret < 0)
        {
            debug_printf("Error: PVFS_Request_contiguous() failure.\n");
            break;
        }

        ret = PVFS_sys_write(resp_lk.pinode_refn, req_io, 0, io_buffer,
                             req_mem, credentials, &resp_io);
        if(ret < 0)
        {
            debug_printf("Error: PVFS_sys_write() failure.\n");
            break;
        }

        debug_printf("test_pvfsdatatype_contig: wrote %d bytes.\n",
                     (int)resp_io.total_completed);

        /* now try to read the data back */
        memset(io_buffer,0,TEST_PVFS_DATA_SIZE);
        ret = PVFS_sys_read(resp_lk.pinode_refn, req_io, 0, io_buffer,
                            req_mem, credentials, &resp_io);
        if(ret < 0)
        {
            debug_printf("Error: PVFS_sys_read() failure (2).\n");
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
