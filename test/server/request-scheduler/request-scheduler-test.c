/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* test program for the request scheduler API */

#include <stdio.h>
#include <assert.h>

#include "request-scheduler.h"
#include "pvfs2-req-proto.h"
#include "gossip.h"
#include "pvfs2-debug.h"

int main(
    int argc,
    char **argv)
{
    int ret;
    struct PVFS_server_req req_array[4];
    req_sched_id id_array[4];
    req_sched_id id_arrayB[4];
    struct PVFS_server_req io_req_array[4];
    req_sched_id io_id_array[4];
    req_sched_id io_id_arrayB[4];
    int count = 0;
    int status = 0;
    req_sched_id timer_id_array[2];

    /* setup some requests to test */
    req_array[0].op = PVFS_SERV_GETATTR;
    req_array[0].u.getattr.handle = 5;

    req_array[1].op = PVFS_SERV_SETATTR;
    req_array[1].u.setattr.handle = 5;

    req_array[2].op = PVFS_SERV_GETATTR;
    req_array[2].u.getattr.handle = 6;

    req_array[3].op = PVFS_SERV_SETATTR;
    req_array[3].u.setattr.handle = 5;

    io_req_array[0].op = PVFS_SERV_IO;
    io_req_array[0].u.io.handle = 5;
    io_req_array[1].op = PVFS_SERV_IO;
    io_req_array[1].u.io.handle = 5;
    io_req_array[2].op = PVFS_SERV_IO;
    io_req_array[2].u.io.handle = 5;
    io_req_array[3].op = PVFS_SERV_IO;
    io_req_array[3].u.io.handle = 5;

    /* turn on gossip for the scheduler */
    gossip_enable_stderr();
    gossip_set_debug_mask(1, REQ_SCHED_DEBUG);

    /* initialize scheduler */
    ret = PINT_req_sched_initialize();
    if (ret < 0)
    {
	fprintf(stderr, "Error: initialize failure.\n");
	return (-1);
    }

    /* try to schedule first request- it should proceed */
    ret = PINT_req_sched_post(&(req_array[0]), 0, NULL, &(id_array[0]));
    if (ret != 1)
    {
	fprintf(stderr, "Error: 1st post should immediately complete.\n");
	return (-1);
    }

    /* try to schedule second request- it should queue up */
    ret = PINT_req_sched_post(&(req_array[1]), 0, NULL, &(id_array[1]));
    if (ret != 0)
    {
	fprintf(stderr, "Error: 2nd post should queue.\n");
	return (-1);
    }

    /* schedule two I/O requests */
    ret = PINT_req_sched_post(&(io_req_array[1]), 0, NULL, &(io_id_array[1]));
    if (ret != 0)
    {
	fprintf(stderr, "Error: 1st I/O req should queue.\n");
	return (-1);
    }
    ret = PINT_req_sched_post(&(io_req_array[0]), 0, NULL, &(io_id_array[0]));
    if (ret != 0)
    {
	fprintf(stderr, "Error: 1st I/O req should queue.\n");
	return (-1);
    }

    /* try to schedule third request- it should proceed */
    ret = PINT_req_sched_post(&(req_array[2]), 0, NULL, &(id_array[2]));
    if (ret != 1)
    {
	fprintf(stderr, "Error: 3rd post should immediately complete.\n");
	return (-1);
    }

    /* try to schedule fourth request- it should queue up */
    ret = PINT_req_sched_post(&(req_array[3]), 0, NULL, &(id_array[3]));
    if (ret != 0)
    {
	fprintf(stderr, "Error: 4th post should queue.\n");
	return (-1);
    }

	/*********************************************************/

    /* test the second one and make sure it doesn't finish */
    ret = PINT_req_sched_test(id_array[1], &count, NULL, &status);
    if (ret != 0 || count != 0)
    {
	fprintf(stderr, "Error: test of 2nd request failed.\n");
	return (-1);
    }

    /* unpost the 2nd request */
    ret = PINT_req_sched_unpost(id_array[1], NULL);
    if (ret != 0)
    {
	fprintf(stderr, "Error: upost failure.\n");
	return (-1);
    }

    /* complete the first request */
    ret = PINT_req_sched_release(id_array[0], NULL, &(id_arrayB[0]));
    if (ret != 1)
    {
	fprintf(stderr, "Error: release didn't immediately complete.\n");
	return (-1);
    }

    /* 4th request should still block on i/o requests */
    ret = PINT_req_sched_test(id_array[3], &count, NULL, &status);
    if (ret != 0 || count != 0)
    {
	fprintf(stderr, "Error: test of 4th request failed.\n");
	return (-1);
    }

    /* see if the first two i/o requests are ready */
    /* test out of order, to make sure that works */
    ret = PINT_req_sched_test(io_id_array[1], &count, NULL, &status);
    if (ret != 1 || count != 1 || status != 0)
    {
	fprintf(stderr, "Error: test of 2nd io request failed.\n");
	return (-1);
    }
    ret = PINT_req_sched_test(io_id_array[0], &count, NULL, &status);
    if (ret != 1 || count != 1 || status != 0)
    {
	fprintf(stderr, "Error: test of 2nd io request failed.\n");
	return (-1);
    }

    /* 4th request should still block on i/o requests */
    ret = PINT_req_sched_test(id_array[3], &count, NULL, &status);
    if (ret != 0 || count != 0)
    {
	fprintf(stderr, "Error: test of 4th request failed.\n");
	return (-1);
    }

    /* release the first two io requests */
    ret = PINT_req_sched_release(io_id_array[1], NULL, &(io_id_arrayB[1]));
    if (ret != 1)
    {
	fprintf(stderr, "Error: release didn't immediately complete.\n");
	return (-1);
    }
    ret = PINT_req_sched_release(io_id_array[0], NULL, &(io_id_arrayB[0]));
    if (ret != 1)
    {
	fprintf(stderr, "Error: release didn't immediately complete.\n");
	return (-1);
    }

    /* now the 4th request should be ready to go */
    ret = PINT_req_sched_test(id_array[3], &count, NULL, &status);
    if (ret != 1 || count != 1 || status != 0)
    {
	fprintf(stderr, "Error: test of 4th request failed.\n");
	return (-1);
    }

    /* complete the 3rd and 4th requests */
    ret = PINT_req_sched_release(id_array[2], NULL, &(id_arrayB[2]));
    if (ret != 1)
    {
	fprintf(stderr, "Error: release didn't immediately complete.\n");
	return (-1);
    }
    ret = PINT_req_sched_release(id_array[3], NULL, &(id_arrayB[3]));
    if (ret != 1)
    {
	fprintf(stderr, "Error: release didn't immediately complete.\n");
	return (-1);
    }

    /* schedule two more I/O requests, should both immediately complete */
    ret = PINT_req_sched_post(&(io_req_array[2]), 0, NULL, &(io_id_array[2]));
    if (ret != 1)
    {
	fprintf(stderr, "Error: 3rd I/O req should complete.\n");
	return (-1);
    }
    ret = PINT_req_sched_post(&(io_req_array[3]), 0, NULL, &(io_id_array[3]));
    if (ret != 1)
    {
	fprintf(stderr, "Error: 4th I/O req should complete.\n");
	return (-1);
    }

    /* release last two i/o requests */
    ret = PINT_req_sched_release(io_id_array[3], NULL, &(io_id_arrayB[3]));
    if (ret != 1)
    {
	fprintf(stderr, "Error: release didn't immediately complete.\n");
	return (-1);
    }
    ret = PINT_req_sched_release(io_id_array[2], NULL, &(io_id_arrayB[2]));
    if (ret != 1)
    {
	fprintf(stderr, "Error: release didn't immediately complete.\n");
	return (-1);
    }

    /* try a simple timer case */
    ret = PINT_req_sched_post_timer(1500, NULL, &(timer_id_array[0]));
    if (ret != 0)
    {
	fprintf(stderr, "Error: post timer weirdness.\n");
	return (-1);
    }
    ret = PINT_req_sched_post_timer(1000, NULL, &(timer_id_array[1]));
    if (ret != 0)
    {
	fprintf(stderr, "Error: post timer weirdness.\n");
	return (-1);
    }

    do
    {
	count = 2;
	ret = PINT_req_sched_testworld(&count, timer_id_array, NULL, &status);
    } while (ret == 0 && count == 0);

    assert(ret == 1 && count == 1 && status == 0);
    if (ret < 0 || status != 0)
    {
	fprintf(stderr, "Error: test failure.\n");
    }
    printf("Done 1.\n");

    do
    {
	count = 2;
	ret = PINT_req_sched_testworld(&count, timer_id_array, NULL, &status);
    } while (ret == 0 && count == 0);

    assert(ret == 1 && count == 1 && status == 0);
    if (ret < 0 || status != 0)
    {
	fprintf(stderr, "Error: test failure.\n");
    }
    printf("Done 2.\n");

    /* shut down scheduler */
    ret = PINT_req_sched_finalize();
    if (ret < 0)
    {
	fprintf(stderr, "Error: finalize failure.\n");
	return (-1);
    }

    return (0);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
