/* 
 * (C) 2001 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#include <string.h>
#include <stddef.h>

#include "state-machine.h"
#include "server-config.h"
#include "pvfs2-server.h"
#include "pvfs2-attr.h"
#include "job-consist.h"

#define LOOKUP_BUFF_SZ 5

static int lookup_init(state_action_struct *s_op, job_status_s *ret);
static int lookup_cleanup(state_action_struct *s_op, job_status_s *ret);
static int lookup_check_params(state_action_struct *s_op, job_status_s *ret);
static int lookup_send_bmi(state_action_struct *s_op, job_status_s *ret);
static int lookup_dir_space(state_action_struct *s_op, job_status_s *ret);
static int lookup_key_val(state_action_struct *s_op, job_status_s *ret);
void lookup_init_state_machine(void);

extern PINT_server_trove_keys_s *Trove_Common_Keys;

static char path_delim[] = "/";

PINT_state_machine_s lookup_req_s = 
{
	NULL,
	"lookup",
	lookup_init_state_machine
};

%%

machine lookup(init, dir_space, key_val, check_params, send, cleanup)
{
	state init
	{
		run lookup_init;
		default => key_val;
	}

	state check_params
	{
		run lookup_check_params;
		success => dir_space;
		default => send;
	}

	state key_val
	{
		run lookup_key_val;
		success => check_params;
		default => send;
	}

	state dir_space
	{
		run lookup_dir_space;
		success => key_val;
		default => send;
	}

	state send
	{
		run lookup_send_bmi;
		default => cleanup;
	}

	state cleanup
	{
		run lookup_cleanup;
		default => init;
	}
}

%%

/*
 * Function: lookup_init_state_machine
 *
 * Params:   void
 *
 * Returns:  PINT_state_array_values*
 *
 * Synopsis: Set up the state machine for set_attrib. 
 *           
 */

void lookup_init_state_machine(void)
{

	lookup_req_s.state_machine = lookup;

}

/*
 * Function: lookup_init
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: 
 *           
 */


static int lookup_init(state_action_struct *s_op, job_status_s *ret)
{

	int job_post_ret;
	job_id_t i;

	/* Allocate space for attributes, handles, and path segments */
	s_op->key_a = (PVFS_ds_keyval_s *) malloc(3*sizeof(PVFS_ds_keyval_s));
	s_op->val_a = (PVFS_ds_keyval_s *) malloc(3*sizeof(PVFS_ds_keyval_s));

	s_op->key_a[0].buffer_sz = s_op->key_a[1].buffer_sz = \
		Trove_Common_Keys[DIR_ENT_KEY].size > Trove_Common_Keys[METADATA_KEY].size ? \
		Trove_Common_Keys[DIR_ENT_KEY].size : Trove_Common_Keys[METADATA_KEY].size;

	s_op->val_a[0].buffer_sz = s_op->val_a[1].buffer_sz = \
		sizeof(PVFS_handle) > sizeof(PVFS_object_attr) ? \
		sizeof(PVFS_handle) : sizeof(PVFS_object_attr);

	s_op->key_a[0].buffer = malloc(s_op->key_a[0].buffer_sz);
	s_op->key_a[1].buffer = malloc(s_op->key_a[1].buffer_sz);
	s_op->val_a[0].buffer = malloc(s_op->val_a[0].buffer_sz);
	s_op->val_a[1].buffer = malloc(s_op->val_a[1].buffer_sz);

	s_op->resp->u.lookup_path.handle_array = (PVFS_handle *) BMI_memalloc(s_op->addr,
			LOOKUP_BUFF_SZ*sizeof(PVFS_handle),
			BMI_SEND_BUFFER);
	s_op->resp->u.lookup_path.attr_array = \
		(PVFS_object_attr *) BMI_memalloc(s_op->addr,
													 LOOKUP_BUFF_SZ*sizeof(PVFS_object_attr)+\
													 sizeof(PVFS_datafile_attr),
													 BMI_SEND_BUFFER);
#if 0
	if(s_op->resp)
	{
		BMI_memfree(s_op->addr,
				s_op->resp,
				sizeof(struct PVFS_server_resp_s),
				BMI_SEND_BUFFER);
	}


	s_op->resp = BMI_memalloc(s_op->addr,
			sizeof(struct PVFS_server_resp_s)+\             // Response Structure
			(LOOKUP_BUFFER_SZ*sizeof(PVFS_handle))+\      // Max Number of Handles / Req
			(LOOKUP_BUFFER_SZ*sizeof(PVFS_object_attr))+\ // Max Number of Attr Structs
			sizeof(PVFS_datafile_attr),                   // If d/f, need the extra attrib
			BMI_SEND_BUFFER);
#endif 

	s_op->resp->u.lookup_path.count = -1;

	/* 
		Set up the key val iterate pair.  When we go into a nice loop to find handles,
		it will help the state machine if the value of the handle we are looking for
		is stored in the same place.  So, lets use the starting handle...
	 */
	s_op->val.buffer = &(s_op->req->u.lookup_path.starting_handle);
	s_op->val.buffer_sz = sizeof(PVFS_handle);

	job_post_ret = job_check_consistency(s_op->op,
			s_op->req->u.lookup_path.fs_id,
			s_op->req->u.lookup_path.starting_handle,
			s_op,
			ret,
			&i);

	gossip_ldebug(SERVER_DEBUG,"Init returning: %d\n",job_post_ret);
	return(job_post_ret);

}

/*
 * Function: lookup_check_params
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: We have iterated through the key/val pairs
 *           and should have the metadata and the directory
 *           handle.  First, check the permissions on the directory, 
 *           Then, if they correspond, run key/val iter on the dspace
 *           
 */


static int lookup_check_params(state_action_struct *s_op, job_status_s *ret)
{

	int job_post_ret;
	job_id_t i;
	int k;
	int meta_data_flag,directory_handle_flag; //used to tell us that we have the data

	meta_data_flag=directory_handle_flag=0;

	if (s_op->resp->u.lookup_path.count == -1)
	{
		s_op->resp->u.lookup_path.count++;
	}
	else 
	{
		for(k=0;k<ret->count;k++)
		{
			switch(((char *)(s_op->key_a[k].buffer))[0])
			{
				case 'm':
					memcpy(&(s_op->resp->u.lookup_path.attr_array[s_op->resp->u.lookup_path.count]),\
							s_op->val_a[0].buffer,\
							s_op->val_a[0].buffer_sz);
					meta_data_flag = 1;
					break;
				case 'd':
					s_op->resp->u.lookup_path.handle_array[s_op->resp->u.lookup_path.count] = \
						s_op->req->u.lookup_path.starting_handle;
					s_op->req->u.lookup_path.starting_handle = *((PVFS_handle *)s_op->val_a[0].buffer),
					directory_handle_flag = 1;
				default:
					gossip_lerr("Handle of unknown type found\n");

			}
		}
		if(directory_handle_flag && meta_data_flag)
		{
			s_op->resp->u.lookup_path.count++;
			// CHECK PERMISSIONS HERE on attr_array[count-1];
		}
		else
		{
			if(!directory_handle_flag)
				gossip_lerr("Did not get directory handle\n");
			else
				gossip_lerr("Did not get metadata\n");
			gossip_lerr("We did not get both... fix it\n");
		}
	}
	if(s_op->req->u.lookup_path.path)
	{
		s_op->key.buffer = strtok(s_op->req->u.lookup_path.path,path_delim);
		if(!(s_op->key.buffer_sz = strlen(s_op->key.buffer)))
		{
			s_op->key.buffer = strtok(s_op->req->u.lookup_path.path,path_delim);
			s_op->key.buffer_sz = strlen(s_op->key_a[2].buffer);
		}
	}

	gossip_ldebug(SERVER_DEBUG,"check returning: %d\n",job_post_ret);
	return(job_post_ret);

}

/*
 * Function: lookup_send_bmi
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: 
 *           
 */


static int lookup_send_bmi(state_action_struct *s_op, job_status_s *ret)
{

	int job_post_ret;
	job_id_t i;

	gossip_ldebug(SERVER_DEBUG,"Send BMI\n");
	return(job_post_ret);

}

/*
 * Function: lookup_dir_space
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: 
 *           
 */


static int lookup_dir_space(state_action_struct *s_op, job_status_s *ret)
{

	int job_post_ret;
	job_id_t i;

	gossip_ldebug(SERVER_DEBUG,"Lookup Directory Space\n");
	
	return(job_post_ret);

}

/*
 * Function: lookup_key_val
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: Iterate through the keyval space of starting_handle
 *           
 */


static int lookup_key_val(state_action_struct *s_op, job_status_s *ret)
{

	int job_post_ret;
	job_id_t i;
	PVFS_vtag_s bs;

	job_post_ret = job_trove_keyval_iterate(s_op->req->u.lookup_path.fs_id,
			s_op->req->u.lookup_path.starting_handle,
			1,
			s_op->key_a,
			s_op->val_a,
			2,
			0,
			bs,
			s_op,
			ret,
			&i);

	return(job_post_ret);

}


/*
 * Function: lookup_cleanup
 *
 * Params:   server_op *b, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: free memory and return
 *           
 */


static int lookup_cleanup(state_action_struct *s_op, job_status_s *ret)
{

	if(s_op->key_a)
	{
		if(s_op->key_a[0].buffer)
			free(s_op->key_a[0].buffer);
		if(s_op->key_a[1].buffer)
			free(s_op->key_a[1].buffer);
		if(s_op->key_a[2].buffer)
			free(s_op->key_a[2].buffer);
		free(s_op->key_a);
	}

	if(s_op->val_a)
	{
		if(s_op->val_a[0].buffer)
			free(s_op->val_a[0].buffer);
		if(s_op->val_a[1].buffer)
			free(s_op->val_a[1].buffer);
		if(s_op->val_a[2].buffer)
			free(s_op->val_a[2].buffer);
		free(s_op->val_a);
	}

	if(s_op->resp->u.lookup_path.handle_array)
	{
		free(s_op->resp->u.lookup_path.handle_array);
	}
	if(s_op->resp->u.lookup_path.attr_array)
	{
		free(s_op->resp->u.lookup_path.attr_array);
	}

	if(s_op->resp)
	{
		free(s_op->resp);
	}

	if(s_op->req)
	{
		free(s_op->req);
	}

	free(s_op->unexp_bmi_buff);

	free(s_op);

	return(0);

}


#if 0
static void get_no_of_segments(char *path,int *num)
{
	char *s = path;
	int len = strlen(path);
	int pos = 0;

	for(pos = 0;pos < len;pos++)
	{
		if (s[pos] == '/' && (pos + 1) < len)
			(*num)++;
	}
}
#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
