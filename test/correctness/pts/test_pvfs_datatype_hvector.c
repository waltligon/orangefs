#include "pvfs_helper.h"
#include <test_pvfs_datatype_hvector.h>
#include <stdio.h>

extern pvfs_helper_t pvfs_helper;

int test_pvfs_datatype_hvector(MPI_Comm *mycomm, int myid, char *buf, void *params)
{
    int ret = -1, i = 0, j = 0, num_ok = 0;
    PVFS_sysreq_lookup req_lk;
    PVFS_sysresp_lookup resp_lk;
    PVFS_sysreq_io req_io_in;
    PVFS_sysresp_io resp_io_in;
    PVFS_sysreq_io req_io_out;
    PVFS_sysresp_io resp_io_out;
    int io_size = TEST_PVFS_DATA_SIZE;
    char *io_buffer_in  = (char *)0;
    char *io_buffer_out = (char *)0;
    char filename[MAX_TEST_PATH_LEN];

    debug_printf("test_pvfs_datatype_hvector called\n");

    memset(&req_lk,0,sizeof(PVFS_sysreq_lookup));
    memset(&resp_lk,0,sizeof(PVFS_sysresp_lookup));
    memset(&req_io_in,0,sizeof(PVFS_sysreq_io));
    memset(&resp_io_in,0,sizeof(PVFS_sysresp_io));
    memset(&req_io_out,0,sizeof(PVFS_sysreq_io));
    memset(&resp_io_out,0,sizeof(PVFS_sysresp_io));

    io_buffer_in = (char *)malloc(io_size*sizeof(char));
    io_buffer_out = (char *)malloc(io_size*sizeof(char));
    if (!pvfs_helper.initialized || !io_buffer_in || !io_buffer_out)
    {
        debug_printf("test_pvfs_datatype_config cannot be initialized!\n");
        return ret;
    }

    for(i = 0; i < io_size; i++)
    {
        io_buffer_in[i] = ((i % 26) + 65);
        io_buffer_out[i] = 0;
    }

    for(i = 0; i < NUM_TEST_FILES; i++)
    {
        memset(filename,0,MAX_TEST_PATH_LEN);
        snprintf(filename,MAX_TEST_PATH_LEN,"%s%.5d\n",
                 TEST_FILE_PREFIX,i);

        memset(&req_lk,0,sizeof(PVFS_sysreq_lookup));
        req_lk.name = filename;
        req_lk.fs_id = pvfs_helper.resp_init.fsid_list[0];
        req_lk.credentials.uid = 100;
        req_lk.credentials.gid = 100;
        req_lk.credentials.perms = U_WRITE|U_READ;

        ret = PVFS_sys_lookup(&req_lk, &resp_lk);
        if (ret < 0)
        {
            debug_printf("test_pvfs_datatype_hvector: lookup failed "
                         "on %s\n",filename);
            break;
        }

        /* perform hvector I/O on the file handle */
        req_io_out.pinode_refn.fs_id = req_lk.fs_id;
        req_io_out.pinode_refn.handle = resp_lk.pinode_refn.handle;
        req_io_out.credentials.uid = 100;
        req_io_out.credentials.gid = 100;
        req_io_out.credentials.perms = U_WRITE|U_READ;
        req_io_out.buffer = io_buffer_out;
        req_io_out.buffer_size = io_size;

        ret = PVFS_Request_hvector(io_size,sizeof(char),1,
                                  PVFS_BYTE,&(req_io_out.io_req));
        if(ret < 0)
        {
            debug_printf("Error: PVFS_Request_hvector() failure.\n");
            break;
        }

        ret = PVFS_sys_write(&req_io_out, &resp_io_out);
        if(ret < 0)
        {
            debug_printf("Error: PVFS_sys_write() failure.\n");
            break;
        }

        debug_printf("test_pvfsdatatype_hvector: wrote %d bytes.\n",
                     (int)resp_io_out.total_completed);

        /* now try to read the data back */
        req_io_in.pinode_refn.fs_id = req_lk.fs_id;
        req_io_in.pinode_refn.handle = resp_lk.pinode_refn.handle;
        req_io_in.credentials.uid = 100;
        req_io_in.credentials.gid = 100;
        req_io_in.credentials.perms = U_WRITE|U_READ;
        req_io_in.buffer = io_buffer_in;
        req_io_in.buffer_size = io_size;

        ret = PVFS_Request_hvector(io_size,sizeof(char),1,
                                  PVFS_BYTE,&(req_io_in.io_req));
        if(ret < 0)
        {
            debug_printf("Error: PVFS_Request_hvector() failure (2).\n");
            break;
        }

        ret = PVFS_sys_read(&req_io_in, &resp_io_in);
        if(ret < 0)
        {
            debug_printf("Error: PVFS_sys_write() failure (2).\n");
            break;
        }

        debug_printf("test_pvfs_datatype_hvector: read %d bytes.\n",
                     (int)resp_io_in.total_completed);

        /* finally, verify the data */
        for(j = 0; j < io_size; j++)
        {
            if (io_buffer_in[j] != io_buffer_out[j])
            {
                debug_printf("test_pvfs_datatype_hvector: data "
                             "verification failed\n");
                break;
            }
        }
        if (j != io_size)
        {
            break;
        }
        memset(io_buffer_out,0,io_size*sizeof(char));

        num_ok++;
    }
    free(io_buffer_in);
    free(io_buffer_out);
    return ((num_ok == NUM_TEST_FILES) ? 0 : 1);
}
