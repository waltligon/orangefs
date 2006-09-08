/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "client.h"
#include "pvfs2-util.h"
#include "pvfs2-mgmt.h"
#include "pvfs2-internal.h"
#include "str-utils.h"
#include "pint-sysint-utils.h"

#define MAX_IO_SIZE     1000*1000
#define SMALL_IO_SIZE   1000

#define DFILE_KEY "system.pvfs2." DATAFILE_HANDLES_KEYSTR
/*
 * for all tests create only 2 datafiles !
 * Create file !
 *tests:
 *  migration starts, do read I/O while migration (should happen concurrently)
 *  migration starts, do write I/O while migration (should resume operation)
 *  do write I/O start migration (should delay migration)
 *  do read I/O start migration (should happen concurrently)
 * 
 *  do also run test for small I/O.
 */
int write_test(PVFS_object_ref pinode_refn, int *io_buffer, int io_size, PVFS_credentials * credentials);
int read_verify_test(PVFS_object_ref pinode_refn, int *io_buffer, int io_size, PVFS_credentials * credentials);

int write_test(PVFS_object_ref pinode_refn, int *io_buffer, int io_size, PVFS_credentials * credentials)
{
    PVFS_sysresp_io resp_io;
    PVFS_Request file_req;
    PVFS_Request mem_req;
    int ret;
    int i;
    
    /* put some data in the buffer so we can verify */
    for (i = 0; i < io_size; i++)
    {
        io_buffer[i] = i;
    }

    /*
       file datatype is tiled, so we can get away with a trivial type
       here
     */
    file_req = PVFS_BYTE;

    ret = PVFS_Request_contiguous(io_size * sizeof(int), PVFS_BYTE, &(mem_req));
    if (ret < 0)
    {
        PVFS_perror("PVFS_request_contiguous failure", ret);
        return (-1);
    }
    
    printf("IO-TEST: performing write on handle: %ld, fs: %d\n",
           (long) pinode_refn.handle, (int) pinode_refn.fs_id);

    ret = PVFS_sys_write(pinode_refn, file_req, 0, (void*)io_buffer, mem_req,
                         credentials, &resp_io, NULL);
    if (ret < 0)
    {
        PVFS_perror("PVFS_sys_write failure", ret);
        return (-1);
    }

    return (int) resp_io.total_completed;
}

int read_verify_test(PVFS_object_ref pinode_refn, int *io_buffer, int io_size, PVFS_credentials * credentials)
{
    PVFS_sysresp_io resp_io;
    PVFS_Request file_req;
    PVFS_Request mem_req;
    int ret;
    int i;
    int errors = 0;
    /* uncomment and try out the readback-and-verify stuff that follows
     * once reading back actually works */
    memset(io_buffer, 0, io_size * sizeof(int));
    
    file_req = PVFS_BYTE;

    ret = PVFS_Request_contiguous(io_size * sizeof(int), PVFS_BYTE, &(mem_req));
    if (ret < 0)
    {
        PVFS_perror("PVFS_request_contiguous failure", ret);
        return (-1);
    }
    
    /* verify */
    printf("IO-TEST: performing read on handle: %ld, fs: %d\n",
           (long) pinode_refn.handle, (int) pinode_refn.fs_id);

    ret = PVFS_sys_read(pinode_refn, file_req, 0, (void*)io_buffer, mem_req,
                        credentials, &resp_io, NULL);
    if (ret < 0)
    {
        PVFS_perror("PVFS_sys_read failure", ret);
        return (-1);
    }
    
    if ((io_size * sizeof(int)) != resp_io.total_completed)
    {
        fprintf(stderr, "Error: SHORT READ! skipping verification...\n");
    }
    else
    {
        for (i = 0; i < io_size; i++)
        {
            if (i != io_buffer[i])
            {
                if ( errors == 0)
                {
                    fprintf(stderr,
                        "error: element %d differs: should be %d, is %d\n", i,
                        i, io_buffer[i]);
                }
                errors++;
            }
        }
        if (errors != 0)
        {
            fprintf(stderr, "ERROR: found %d errors\n", errors);
        }
        else
        {
            printf("IO-TEST: no errors found.\n");
        }
    }
    
    return (int) resp_io.total_completed;
}

int main(
    int argc,
    char **argv)
{
    PVFS_handle handles[1024];
 
    char ** param_servers;
    int param_server_count;
    
    PVFS_sysresp_lookup resp_lk;
    PVFS_sysresp_create resp_cr;
    char *filename = NULL;
    int ret = -1;
    int *io_buffer = NULL;
    int i;
    PVFS_fs_id fs_id;
    char name[512] = { 0 };
    char *entry_name = NULL;
    PVFS_credentials credentials;
    PVFS_object_ref parent_refn;
    PVFS_sys_attr attr;
    PVFS_object_ref pinode_refn;
    PVFS_sysresp_getparent gp_resp;
    PVFS_sysresp_getattr resp_getattr;
    PVFS_handle *dfile_array = NULL;
    
    PVFS_mgmt_op_id op_id;
    PVFS_hint * hints = NULL;
    
    char metadataserver[256];
    PVFS_BMI_addr_t bmi_metadataserver;
    PVFS_BMI_addr_t bmi_olddatataserver;
    PVFS_BMI_addr_t bmi_newdatataserver;
    
    if (argc < 2)
    {
        fprintf(stderr,
                "Usage: %s <file name> <target server 1> ... <target server n> \n",
                argv[0]);
        return (-1);
    }
    param_server_count = argc -2; 
    param_servers = argv + 2;

    /* create a buffer for running I/O on */
    io_buffer = (int *) malloc(MAX_IO_SIZE * sizeof(int));
    if (!io_buffer)
    {
        return (-1);
    }

    ret = PVFS_util_init_defaults();
    if (ret < 0)
    {
        PVFS_perror("PVFS_util_init_defaults", ret);
        return (-1);
    }
    ret = PVFS_util_get_default_fsid(&fs_id);
    if (ret < 0)
    {
        PVFS_perror("PVFS_util_get_default_fsid", ret);
        return (-1);
    }

    if (argv[1][0] == '/')
    {
        snprintf(name, 512, "%s", argv[1]);
    }
    else
    {
        snprintf(name, 512, "/%s", argv[1]);
    }

    PVFS_util_gen_credentials(&credentials);
    
    memset(&gp_resp, 0, sizeof(PVFS_sysresp_getparent));
    ret = PVFS_sys_getparent(fs_id, name, &credentials, &gp_resp, NULL);
    if (ret < 0)
    {
        PVFS_perror("PVFS_sys_getparent failed", ret);
        return ret;
    }
    
    entry_name = rindex(name, (int) '/');
    assert(entry_name);
    entry_name++;
    assert(entry_name);
    
    
lookup_again:
    ret = PVFS_sys_lookup(fs_id, name, &credentials,
                          &resp_lk, PVFS2_LOOKUP_LINK_FOLLOW, NULL);
    if (ret == -PVFS_ENOENT)
    {
        PVFS_hint * hints = NULL;
        char tmp_hint[2048];
        char * tmp_hint_pos = tmp_hint;
        int i;
        
        printf("IO-TEST: lookup failed; creating new file,"
            " this is how it should be.\n\n");

        attr.owner = credentials.uid;
        attr.group = credentials.gid;
        attr.perms = PVFS_U_WRITE | PVFS_U_READ;
        attr.atime = attr.ctime = attr.mtime = time(NULL);
        attr.dfile_count = param_server_count;
        attr.mask = PVFS_ATTR_SYS_ALL_SETABLE | PVFS_ATTR_SYS_DFILE_COUNT;
        
        parent_refn = gp_resp.parent_ref;

        for(i=0; i < param_server_count; i++)
        {
            sprintf(tmp_hint_pos, ",%s", param_servers[i]);
            tmp_hint_pos+=1+strlen(param_servers[i]);
        }
        *tmp_hint_pos = 0;
        
        printf("%d Dataservers selected via hint: %s, file:%s\n",
            param_server_count, tmp_hint+1,entry_name);
        
        PVFS_add_hint(& hints,CREATE_SET_DATAFILE_NODES, tmp_hint+1);
        
        ret = PVFS_sys_create(entry_name, parent_refn, attr,
                              &credentials, NULL, &resp_cr, hints);
        if (ret < 0)
        {
            PVFS_perror("PVFS_sys_create() failure", ret);
            return (-1);
        }

        pinode_refn.fs_id = fs_id;
        pinode_refn.handle = resp_cr.ref.handle;
        
        PVFS_free_hint(& hints);
    }
    else
    {
        printf("IO-TEST: lookup succeeded; deleting file first.\n");

        pinode_refn.fs_id = fs_id;
        pinode_refn.handle = resp_lk.ref.handle;
       
        ret = PVFS_sys_remove(entry_name, gp_resp.parent_ref, &credentials, NULL);
        if (ret < 0)
        {
            PVFS_perror("remove failed ", ret);
            return(-1);
        }
        goto lookup_again;
    }

    /*
	 * carry out I/O operation
	 */

 
    ret = write_test(pinode_refn, io_buffer, MAX_IO_SIZE, & credentials);
    printf("IO-TEST: prewrote %d bytes.\n", ret);
    if( ret != MAX_IO_SIZE * sizeof(int))
    {
        fprintf(stderr, "File size is %d should be %d\n", 
            (ret), (MAX_IO_SIZE * sizeof(int)));
        return(-1);
    }

    ret = read_verify_test(pinode_refn, io_buffer, MAX_IO_SIZE, & credentials);
    printf("IO-TEST: read %d bytes.\n", ret);
    if( ret != MAX_IO_SIZE * sizeof(int))
    {
        fprintf(stderr, "File size is %d should be %d\n", 
            (ret), (MAX_IO_SIZE * sizeof(int)));
        return(-1);
    }
    
    ret = PINT_cached_config_get_server_name(metadataserver, 256, 
        metafile_ref.handle, metafile_ref.fs_id);
    if( ret != 0)
    {
        fprintf(stderr, "Error, could not get metadataserver name\n");
        return (-1);
    }
 
    ret = BMI_addr_lookup(&bmi_metadataserver,metadataserver);
    if (ret < 0)
    {
        fprintf(stderr, "Error, BMI_addr_lookup unsuccessful %s\n",
        metadataserver );
        return(-1);
    }
    
    PVFS_add_hint(& hints, REQUEST_ID, "pvfs2-migrate");
    PVFS_add_hint(& hints, REQUEST_ID, "pvfs2-migrate");    
    /* now think about the migration ! */
    ret = PVFS_imgmt_migrate(pinode_refn.fs_id,& credentials,
        bmi_metadataserver, pinode_refn.handle,
        , , 
        & op_id, hints, NULL);
    
    PVFS_handle     target_datafile,
    
    PVFS_BMI_addr_t source_dataserver,
    PVFS_BMI_addr_t target_dataserver,
    PVFS_free_hint(& hints);
     
    if (ret)
    {
        PVFS_perror_gossip("PVFS_imgmt_migrate call", ret);
        error = ret;
    }
    else
    {
        ret = PVFS_mgmt_wait(op_id, "migrate", &error);
        if (ret)
        {
            PVFS_perror_gossip("PVFS_mgmt_wait call", ret);
            error = ret;
        }
    }

    PVFS_mgmt_release(op_id);
    


    /* test out some of the mgmt functionality */
    ret = PVFS_sys_getattr(pinode_refn, PVFS_ATTR_SYS_ALL_NOSIZE,
                           &credentials, &resp_getattr, NULL);
    if (ret < 0)
    {
        PVFS_perror("PVFS_sys_getattr", ret);
        return (-1);
    }

    printf("Target file had %d datafiles:\n", resp_getattr.attr.dfile_count);

    dfile_array = (PVFS_handle *) malloc(resp_getattr.attr.dfile_count
                                         * sizeof(PVFS_handle));
    if (!dfile_array)
    {
        perror("malloc");
        return (-1);
    }

    ret = PVFS_mgmt_get_dfile_array(pinode_refn, &credentials,
                                    dfile_array, resp_getattr.attr.dfile_count,
                                    NULL);
    if (ret < 0)
    {
        PVFS_perror("PVFS_mgmt_get_dfile_array", ret);
        return (-1);
    }
    for (i = 0; i < resp_getattr.attr.dfile_count; i++)
    {
        printf("%llu\n", llu(dfile_array[i]));
    }

        /**************************************************************
	 * shut down pending interfaces
	 */

    ret = PVFS_sys_finalize();
    if (ret < 0)
    {
        fprintf(stderr, "Error: PVFS_sys_finalize() failed with errcode = %d\n",
                ret);
        return (-1);
    }

    free(filename);
    free(io_buffer);
    return (0);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
