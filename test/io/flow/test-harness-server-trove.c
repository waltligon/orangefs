/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* this is a simple test harness that operates on top of the flow
 * interface 
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <time.h>

#include "gossip.h"
#include "flow.h"
#include "flowproto-support.h"
#include "pvfs-distribution.h"
#include "pvfs2-request.h"

#include "trove.h"

#define ROOT_HANDLE_STRING "root_handle"

static int block_on_flow(
    flow_descriptor * flow_d);
static double Wtime(
    void);
int path_lookup(
    TROVE_coll_id coll_id,
    TROVE_context_id trove_context,
    char *path,
    TROVE_handle * out_handle_p);

enum
{
    TEST_SIZE = 1024 * 1024 * 20,
    SSPACE_SIZE = 64,
    FS_SIZE = 64,
    PATH_SIZE = 256,
    FS_COLL_ID = 9,
    ADMIN_COLL_ID = 10
};

enum
{
    TROVE_TEST_DIR = 1,
    TROVE_TEST_FILE = 2,
    TROVE_TEST_BSTREAM = 3
};

char storage_space[SSPACE_SIZE] = "/tmp/pvfs2-test-space";
char file_system[FS_SIZE] = "pvfs2-fs";
char path_to_file[PATH_SIZE] = "/bar";
TROVE_handle requested_file_handle = 4095;

int main(
    int argc,
    char **argv)
{
    int ret = -1;
    int outcount = 0, count;
    struct BMI_unexpected_info request_info;
    flow_descriptor *flow_d = NULL;
    double time1, time2;
    int i;
    PINT_Request *req;
    char path_name[PATH_SIZE];
    TROVE_op_id op_id;
    TROVE_coll_id coll_id;
    TROVE_handle file_handle, parent_handle;
    TROVE_ds_state state;
    char *method_name, *file_name;
    TROVE_keyval_s key, val;
    bmi_context_id context;
    TROVE_context_id trove_context;
    PVFS_handle_extent cur_extent;
    PVFS_handle_extent_array extent_array;

	/*************************************************************/
    /* initialization stuff */

    /* set debugging level */
    gossip_enable_stderr();
    gossip_set_debug_mask(
        0, (GOSSIP_FLOW_PROTO_DEBUG | GOSSIP_BMI_DEBUG_TCP));

    /* start up BMI */
    ret = BMI_initialize("bmi_tcp", "tcp://NULL:3335", BMI_INIT_SERVER);
    if (ret < 0)
    {
	fprintf(stderr, "BMI init failure.\n");
	return (-1);
    }

    ret = BMI_open_context(&context);
    if (ret < 0)
    {
	fprintf(stderr, "BMI_open_context() failure.\n");
	return (-1);
    }

    ret = trove_initialize(storage_space, 0, &method_name, 0);
    if (ret < 0)
    {
	fprintf(stderr, "initialize failed: run trove-mkfs first.\n");
	return -1;
    }

    /* initialize the flow interface */
    ret = PINT_flow_initialize("flowproto_multiqueue", 0);
    if (ret < 0)
    {
	fprintf(stderr, "flow init failure.\n");
	return (-1);
    }

    /* try to look up collection used to store file system */
    ret = trove_collection_lookup(file_system, &coll_id, NULL, &op_id);
    if (ret < 0)
    {
	fprintf(stderr, "collection lookup failed.\n");
	return -1;
    }

    ret = trove_open_context(coll_id, &trove_context);
    if (ret < 0)
    {
	fprintf(stderr, "TROVE_open_context() failure.\n");
	return (-1);
    }

    /* find the parent directory name */
    strcpy(path_name, path_to_file);
    for (i = strlen(path_name); i >= 0; i--)
    {
	if (path_name[i] != '/')
	    path_name[i] = '\0';
	else
	    break;
    }
    file_name = path_to_file + strlen(path_name);
    printf("path is %s\n", path_name);
    printf("file is %s\n", file_name);

    /* find the parent directory handle */
    ret = path_lookup(coll_id, trove_context, path_name, &parent_handle);
    if (ret < 0)
    {
	return -1;
    }

    file_handle = 0;


    cur_extent.first = cur_extent.last = requested_file_handle;
    extent_array.extent_count = 1;
    extent_array.extent_array = &cur_extent;
    ret = trove_dspace_create(coll_id,
			      &extent_array,
			      &file_handle,
			      TROVE_TEST_FILE,
			      NULL, 
			      TROVE_FORCE_REQUESTED_HANDLE,
			      NULL, trove_context, &op_id);
    while (ret == 0)
	ret =
	    trove_dspace_test(coll_id, op_id, trove_context, &count, NULL, NULL,
			      &state, TROVE_DEFAULT_TEST_TIMEOUT);
    if (ret < 0)
    {
	fprintf(stderr, "dspace create failed.\n");
	return -1;
    }

    /* TODO: set attributes of file? */

    /* add new file name/handle pair to parent directory */
    key.buffer = file_name;
    key.buffer_sz = strlen(file_name) + 1;
    val.buffer = &file_handle;
    val.buffer_sz = sizeof(file_handle);
    ret =
	trove_keyval_write(coll_id, parent_handle, &key, &val, 0, NULL, NULL,
			   trove_context, &op_id);
    while (ret == 0)
	ret =
	    trove_dspace_test(coll_id, op_id, trove_context, &count, NULL, NULL,
			      &state, TROVE_DEFAULT_TEST_TIMEOUT);
    if (ret < 0)
    {
	fprintf(stderr, "keyval write failed.\n");
	return -1;
    }


    /* wait for an initial communication via BMI */
    /* we don't give a crap about that message except that it tells us
     * where to find the client 
     */
    do
    {
	ret = BMI_testunexpected(1, &outcount, &request_info, 10);
    } while (ret == 0 && outcount == 0);
    if (ret < 0 || request_info.error_code != 0)
    {
	fprintf(stderr, "waitunexpected failure.\n");
	return (-1);
    }
    free(request_info.buffer);

	/******************************************************/
    /* setup request/dist stuff */

    /* request description */
    /* just want one contiguous region */
    ret = PVFS_Request_contiguous(TEST_SIZE, PVFS_BYTE, &req);
    if (ret < 0)
    {
	fprintf(stderr, "PVFS_Request_contiguous() failure.\n");
	return (-1);
    }


	/******************************************************/
    /* setup communicaton stuff */

    /* create a flow descriptor */
    flow_d = PINT_flow_alloc();
    if (!flow_d)
    {
	fprintf(stderr, "flow_alloc failed.\n");
	return (-1);
    }

    /* file data */
    flow_d->file_data.fsize = TEST_SIZE;
    flow_d->file_data.server_nr = 0;
    flow_d->file_data.server_ct = 1;
    flow_d->file_data.extend_flag = 1;
    flow_d->file_data.dist = PVFS_dist_create("basic_dist");
    if (!flow_d->file_data.dist)
    {
	fprintf(stderr, "Error: failed to create dist.\n");
	return (-1);
    }
    ret = PINT_Dist_lookup(flow_d->file_data.dist);
    if (ret != 0)
    {
	fprintf(stderr, "Error: failed to lookup dist.\n");
	return (-1);
    }
    flow_d->file_req = req;
    flow_d->tag = 0;
    flow_d->user_ptr = NULL;
    flow_d->aggregate_size = TEST_SIZE;

    /* fill in flow details */
    flow_d->src.endpoint_id = BMI_ENDPOINT;
    flow_d->src.u.bmi.address = request_info.addr;
    flow_d->dest.endpoint_id = TROVE_ENDPOINT;
    flow_d->dest.u.trove.handle = file_handle;
    flow_d->dest.u.trove.coll_id = coll_id;

	/***************************************************
	 * test bmi to file (analogous to a client side write)
	 */

    time1 = Wtime();
    ret = block_on_flow(flow_d);
    if (ret < 0)
    {
	return (-1);
    }
    time2 = Wtime();

#if 0
    printf("Server bw (recv): %f MB/sec\n",
	   ((TEST_SIZE) / ((time2 - time1) * 1000000.0)));
#endif

	/*******************************************************/
    /* final cleanup and output */

    PINT_flow_free(flow_d);

    /* shut down flow interface */
    ret = PINT_flow_finalize();
    if (ret < 0)
    {
	fprintf(stderr, "flow finalize failure.\n");
	return (-1);
    }

    /* shut down BMI */
    BMI_close_context(context);
    BMI_finalize();

    trove_close_context(coll_id, trove_context);
    trove_finalize();

    gossip_disable();
    return (0);
}

static int done_flag = 0;
static void callback_fn(flow_descriptor* flow_d)
{
    done_flag = 1;
    return;
}

static int block_on_flow(
    flow_descriptor * flow_d)
{
    int ret = -1;
    struct timespec req = {0, 1000};

    flow_d->callback = callback_fn;
    ret = PINT_flow_post(flow_d);
    if (ret == 1)
    {
	return (0);
    }
    if (ret < 0)
    {
	fprintf(stderr, "flow post failure.\n");
	return (ret);
    }

    while(!done_flag)
    {
	nanosleep(&req, NULL);
    }

    if (flow_d->state != FLOW_COMPLETE)
    {
	fprintf(stderr, "flow finished in error state: %d\n", flow_d->state);
	return (-1);
    }
    return (0);
}

static double Wtime(
    void)
{
    struct timeval t;

    gettimeofday(&t, NULL);
    return ((double) t.tv_sec + (double) t.tv_usec / 1000000);
}

int path_lookup(
    TROVE_coll_id coll_id,
    TROVE_context_id trove_context,
    char *path,
    TROVE_handle * out_handle_p)
{
    int ret, count;
    TROVE_ds_state state;
    TROVE_keyval_s key, val;
    TROVE_op_id op_id;
    TROVE_handle handle;

    char root_handle_string[] = ROOT_HANDLE_STRING;

    /* get root */
    key.buffer = root_handle_string;
    key.buffer_sz = strlen(root_handle_string) + 1;
    val.buffer = &handle;
    val.buffer_sz = sizeof(handle);

    ret = trove_collection_geteattr(coll_id, &key, &val, 0,
				    NULL, trove_context, &op_id);
    while (ret == 0)
	ret =
	    trove_dspace_test(coll_id, op_id, trove_context, &count, NULL, NULL,
			      &state, TROVE_DEFAULT_TEST_TIMEOUT);
    if (ret < 0)
    {
	fprintf(stderr, "collection geteattr (for root handle) failed.\n");
	return -1;
    }

    /* TODO: handle more than just a root handle! */

    *out_handle_p = handle;
    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
