/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include <gossip.h>
#include <job.h>
#include <trove.h>
#include <pvfs2-debug.h>
#include <pvfs2-storage.h>

#include <server-config.h>
#include <memory-cache.h>

enum
{
	PVFS2_DEBUG_SERVER = 32 /* This should come from server.h */
};

void *PINT_fetch_trove_buffer(
	int key_val,
	int buffer_size, 
	void **array_buffers, 
	int array_size,
	int *array_buffer_size,
	job_id_t requesting_id)
{

	/* For right now, lets just allocate and run! */
	TROVE_keyval_s *temp_struct = NULL;

	if(array_buffers)
	{
		gossip_ldebug(PVFS2_DEBUG_SERVER,"Array allocation not yet supported\n");
		assert(0);
	}

	temp_struct = (TROVE_keyval_s *) malloc(sizeof(TROVE_keyval_s));
	temp_struct->buffer = malloc((temp_struct->buffer_sz = buffer_size));

	return temp_struct;

}

void PINT_checkin_trove_buffer(
	TROVE_keyval_s *buffer,
	job_id_t requesting_id
	)
{
	free(buffer->buffer);
	free(buffer);
}
