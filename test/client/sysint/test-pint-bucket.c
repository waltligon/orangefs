/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>

#include <pint-bucket.h>
#include <gossip.h>

/* this is a test program that exercises the bucket interface and
 * demonstrates how to use it.
 */
int main(int argc, char **argv)	
{
#if 0
	int ret = -1;
	int num_meta_servers = 0;
	int num_io_servers = 0;
	char big_buff[256];
	bmi_addr_t test_server_addr;
	int count = 0;
	PVFS_handle test_bucket;
	PVFS_handle test_mask;

	bmi_addr_t meta_server_addr;
	PVFS_handle meta_server_bucket;
	PVFS_handle meta_server_mask;

	bmi_addr_t io_server_addr_array[2];
	PVFS_handle io_server_bucket_array[2];
	PVFS_handle io_server_mask;

	/* these are things we must retrieve from the server and or pvfstab
	 * in order to load new mappings into the bucket interface:
	 */
	/*****************************/
	char meta_mapping[] = "foo";
	char io_mapping[] = "bar";
	PVFS_handle handle_mask = 0;
	PVFS_fs_id fsid = 3;
	/*****************************/

	/* set debugging stuff */
	gossip_enable_stderr();
	gossip_set_debug_mask(0, 0);

	/* start up BMI just so that we an resolve server addresses correctly */
	ret = BMI_initialize("bmi_tcp", NULL, 0);
	if(ret < 0)
	{
		fprintf(stderr, "BMI_initialize failure.\n");
		return(-1);
	}

	/* start up the interface */
	ret = PINT_bucket_initialize();
	if(ret < 0)
	{
		fprintf(stderr, "PINT_bucket_initialize() failure.\n");
		return(-1);
	}

	/* load up a mapping */
	/* NOTE: this ignores the meta_mapping and io_mapping for now */
/* FIXME: Commented out while transitioning to handle mappings... */
/* 	ret = PINT_bucket_load_mapping(meta_mapping, 1, io_mapping, 1, */
/* 		handle_mask, fsid); */
/* FIXME: Commented out while transitioning to handle mappings... */
	if(ret < 0)
	{
		fprintf(stderr, "PINT_bucket_load_mapping() failure.\n");
		return(-1);
	}

	/* check the number of servers we have available now */
	ret = PINT_bucket_get_num_meta(fsid, &num_meta_servers);
	if(ret < 0)
	{
		fprintf(stderr, "PINT_bucket_get_num_meta() failure.\n");
		return(-1);
	}

	printf("OUTPUT OF TEST:\n");
	printf("***************************************\n");

	printf("\n");
	printf("Number of meta servers available: %d\n", num_meta_servers);

	ret = PINT_bucket_get_num_io(fsid, &num_io_servers);
	if(ret < 0)
	{
		fprintf(stderr, "PINT_bucket_get_num_io() failure.\n");
		return(-1);
	}

	printf("Number of I/O servers available: %d\n", num_io_servers);

	/* find out which meta server should be used next for placing meta
	 * objects
	 */
	ret = PINT_bucket_get_next_meta(fsid, &meta_server_addr,
		&meta_server_bucket, &meta_server_mask);
	if(ret < 0)
	{
		fprintf(stderr, "PINT_bucket_get_next_meta() failure.\n");
		return(-1);
	}
	
	printf("\n");
	printf("Next meta server: addr: %ld, bucket: %ld, mask: %ld\n", 
		(long)meta_server_addr, (long)meta_server_bucket,
		(long)meta_server_mask);


	/* find out which I/O servers should be used next for placing new file data
	 * objects
	 */
	ret = PINT_bucket_get_next_io(fsid, 2, io_server_addr_array,
		io_server_bucket_array, &io_server_mask);
	if(ret < 0)
	{
		fprintf(stderr, "Pint_bucket_get_next_io() failure.\n");
		return(-1);
	}
	
	printf("\n");
	printf("Asked for two I/O servers and got the following:\n");
	printf("I/O server 0: addr: %ld, bucket: %ld, mask: %ld\n",
		(long)io_server_addr_array[0], (long)io_server_bucket_array[0],
		(long)io_server_mask);
	printf("I/O server 1: addr: %ld, bucket: %ld, mask: %ld\n",
		(long)io_server_addr_array[1], (long)io_server_bucket_array[1],
		(long)io_server_mask);

	/* retrieve the string (BMI URL) associated with a bucket */
	ret = PINT_bucket_get_server_name(big_buff, 256, meta_server_bucket,
		fsid);
	if(ret < 0)
	{
		fprintf(stderr, "PINT_bucket_get_server_name() failure.\n");
		return(-1);
	}

	printf("\n");
	printf("Retrieved string name of server from bucket: %s\n",
		big_buff);
	

	/* try mapping a bucket to a server address */
	ret = PINT_bucket_map_to_server(&test_server_addr,
		meta_server_bucket, fsid);
	if(ret < 0)
	{
		fprintf(stderr, "PINT_bucket_map_to_server() failure.\n");
		return(-1);
	}

	printf("\n");
	if(test_server_addr == meta_server_addr)
	{
		printf("Successfully verified ability to map buckets to servers.\n");
	}
	else
	{
		printf("*** Failed to verify ability to map buckets to servers.\n");
	}
		
	/* try mapping a server to a bucket */
	count = 1;
	ret = PINT_bucket_map_from_server(big_buff, &count, &test_bucket,
		&test_mask);
	if(ret < 0)
	{
		fprintf(stderr, "PINT_bucket_map_from_server failure.\n");
		return(-1);
	}

	if(count == 1 && test_bucket == meta_server_bucket && test_mask ==
		handle_mask)
	{
		printf("Successfully verified ability to map servers to buckets.\n");
	}
	else
	{
		printf("*** Failed to verify ability to map servers to buckets.\n");
	}


	
	/* shut down the interface */
	ret = PINT_bucket_finalize();
	if(ret < 0)
	{
		fprintf(stderr, "PINT_bucket_finalize() failure.\n");
		return(-1);
	}

	BMI_finalize();

	gossip_disable();
#endif
	return(0);
}
