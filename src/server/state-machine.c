/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/*
 * June 2002
 * 
 * State machine going through changes...
 * Initialization functions to go through server_queue
 * with a check_dep() call
 * this will be for all operations  dw
 *
 * Jan 2002
 *
 * This is a basic state machine.
 * This is meant to be a basic framework.
 */ 

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/vfs.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/types.h>
#include <linux/dirent.h>
#include <signal.h>

#include <bmi.h>
#include <gossip.h>
#include <job.h>
#include <pvfs2-debug.h>
#include <pvfs2-storage.h>
#include <assert.h>
#include <PINT-reqproto-encode.h>

#include <state-machine.h>
#include <pvfs2-server.h>

/* This array is used for common key-val pairs for trove =) */

char *TROVE_COMMON_KEYS[6] = {"root_handle","12","metadata","9","dir_ent","8"};

#define ENCODE_TYPE 0

/* Here is the idea...
 * For each state machine, you start with an initial function.
 * Upon posting/completion, you then check return values.
 * A complete state machine with return values should be located
 * in a contiguous region.  
 * Exception: when states are shared between machines. (Damn)
 * Therefore, I guess it really does not matter... but it would be
 * nice if they were as contiguous as possible just to save on the
 * debugging time. dw
*/

extern PINT_state_machine_s getconfig_req_s;
extern PINT_state_machine_s getattr_req_s;
extern PINT_state_machine_s setattr_req_s;
extern PINT_state_machine_s create_req_s;
extern PINT_state_machine_s crdirent_req_s;
extern PINT_state_machine_s mkdir_req_s;

PINT_state_machine_s *PINT_state_array[SERVER_REQ_ARRAY_SIZE] =
{
	NULL, /* 0 */
	NULL,
	&create_req_s,
	NULL,
	NULL,
	NULL, /* 5 */
	NULL,
	&getattr_req_s,
	&setattr_req_s,
	NULL,
	NULL, /* 10 */
	NULL,
	NULL,
	&crdirent_req_s,
	NULL,
	NULL, /* 15 */
	NULL,
	NULL,
	&mkdir_req_s,
	NULL,
	NULL, /* 20 */
	NULL,
	NULL,
	&getconfig_req_s,
	NULL
};



/* Function: PINT_state_machine_initialize_unexpected(s_op,ret)
   Params:   PINT_server_op *s_op
	          job_status_s *ret
   Returns:  int
   Synopsis: Intialize request structure, first location, and call
	          respective init function.
				 
 */
int PINT_state_machine_initialize_unexpected(PINT_server_op *s_op, job_status_s *ret)
{
	struct PINT_decoded_msg buffer;

	PINT_decode(s_op->unexp_bmi_buff->buffer,
					PINT_ENCODE_REQ,
					&buffer,
					s_op->unexp_bmi_buff->addr,
					s_op->unexp_bmi_buff->size,
					&(s_op->enc_type));

	s_op->req  = (struct PVFS_server_req_s *) buffer.buffer;
	assert(s_op->req != 0);
	s_op->addr = s_op->unexp_bmi_buff->addr;
	s_op->tag  = s_op->unexp_bmi_buff->tag;
	s_op->op   = s_op->req->op;
	s_op->location.index = PINT_state_machine_locate(s_op);
	if(!s_op->location.index)
	{
		gossip_err("System not init for function\n");
		return(-1);
	}
	s_op->resp = (struct PVFS_server_resp_s *) \
		BMI_memalloc(s_op->addr,
				sizeof(struct PVFS_server_resp_s),
				BMI_SEND_BUFFER);

	if (!s_op->resp)
	{
		gossip_err("Out of Memory");
		ret->error_code = 1;
		return(-ENOMEM);
	}
	memset(s_op->resp,0,sizeof(struct PVFS_server_resp_s));

	s_op->resp->op = s_op->req->op;

	return(((s_op->location.index - 1)->handler)(s_op,ret));

}


/* Function: PINT_state_machine_init(void)
   Params: None
   Returns: True
   Synopsis: This function is used to initialize the state machine 
				 for operation.
 */

int PINT_state_machine_init(void)
{

	int i=0;
	while (i++ < SERVER_REQ_ARRAY_SIZE)
	{
		if(PINT_state_array[i-1])
			(PINT_state_array[i-1]->init_fun)();
		printf("%d\n",i-1);
	}
	return(0);
	
}

/* Function: PINT_state_machine_halt(void)
   Params: None
   Returns: True
   Synopsis: This function is used to shutdown the state machine 
 */

int PINT_state_machine_halt(void)
{
	return(-1);
}

/* Function: PINT_state_machine_next()
   Params: 
   Returns:   void
   Synopsis:  Runs through a list of return values to find the
	           next function to call.  Calls that function.  Once
				  that function is called, this one exits and we go
				  back to server_daemon.c's while loop.
 */

int PINT_state_machine_next(PINT_server_op *s,job_status_s *r)
{

   int code_val = r->error_code; 
	PINT_state_array_values *loc = s->location.index;

	/* loc is set to the proper return value
	 * update s struct for change
	 * loc + 1 contains a pointer in the array to the next function.  
	 * Following this pointer, then incrementing by one gives us
	 * the return value for the next state.  
	 * The next time this function is called for this server op
	 * we are ready to start our comparisons.  
	 */

	while (loc->retVal != code_val && loc->retVal != DEFAULT_ERROR) 
		loc += 2;

	/* Update the server_op struct to reflect the new location */

	/* NOTE: This remains a pointer pointing to the first return
	 *	      value possibility of the function.
	 */

	s->location.index = (loc + 1)->index + 1;

	/* Call the next function.
	 * NOTE: the function will return back to the original while loop
	 *       in server_daemon.c
	 */

	return(((s->location.index - 1)->handler)(s,r));

}

/* Function: PINT_state_machine_locate(void)
   Params:  
   Returns:  Pointer to the first return value of the initialization
	          function of a state machine
   Synopsis: This function is used to start a state machines execution.
	          If the operation index is null, we have not trapped that yet
	          TODO: Fix that
				 We should also add in some "text" backup if necessary
 */

PINT_state_array_values *PINT_state_machine_locate(PINT_server_op *s_op)
{

	gossip_debug(SERVER_DEBUG,"Locating State machine for %d\n",s_op->op);
	if(PINT_state_array[s_op->op] != NULL)
	{
		/* Return the first return value possible from the init function... =) */
		gossip_debug(SERVER_DEBUG,"Found State Machine %d\n",s_op->op);
		return(((PINT_state_array_values *)PINT_state_array[s_op->op]->state_machine)[0].index + 1);
	}
	gossip_err("State machine not found for operation %d\n",s_op->op);
	return(NULL);
	
}


