/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* This file contains a test harness for the system interface.  It is 
 * implemented at the "job" level.
 */

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <job.h>
/* REMOVED BY PHIL WHEN MOVING TO NEW TREE */
#if 0
#include <old-job.h>
#endif
#include <bmi.h>
#include <llist.h>
#include <pvfs2-req-proto.h>

/* ADDED BY PHIL WHEN MOVING TO NEW TREE */
/* THIS USED TO BE IN OLD_JOB.H */
/********************************************************/
/* this is a hack.  This stuff is just in here to make this test harness
 * work on top of an older test harness...
 */

typedef int32_t PINT_job_type_t;
typedef int32_t PINT_job_state_t;
typedef int32_t PINT_priority_t;

/* types of BMI operations */
enum
{
	BMI_OP_SEND = 1,
	BMI_OP_RECV = 2
};

/* types of jobs we support */
enum
{
	BMI_JOB,
	BMI_UNEXP_JOB,
	FLOW_JOB,
	STO_JOB
};

/* possible job states */
enum
{
	JOB_DONE,
	JOB_ERROR,
	JOB_ABORTED
};

/* BMI descriptor */
struct bmi_descriptor
{
	bmi_flag_t op_type;     /* send or receive */
	bmi_addr_t address;     /* host address */
	void* buffer;           /* buffer to send or receive into */
	bmi_size_t size;        /* size of buffer */ 
	bmi_flag_t buffer_flag; /* was it pre-allocated or not? */
	bmi_msg_tag_t tag;      /* BMI message tag */
	bmi_error_code_t error_code;      /* resulting status */
	bmi_flag_t send_unexpected; /* send this in unexpected mode */
};

/* storage interface descriptor */
struct storage_descriptor
{
	/* TODO: this needs to be filled in later */
	void* foo;
};

/* Note that flow descriptors and BMI unexpected message descriptors are
 * already defined within their respective interfaces.
 */

/* job structure */
struct PINT_job
{
	PINT_job_type_t type;   /* type of operation (flow, bmi, etc.) */
	PINT_priority_t priority; /* priority of this operation */
	struct PINT_job* next;       /* pointer to next operation */
	struct PINT_job* concurrent; /* pointer to concurrent operation */
	union                   /* descriptors for each type of operation */
	{
		struct bmi_descriptor bmi_d;
		struct BMI_unexpected_info bmi_unexp_d;
		struct flow_descriptor flow_d;
		struct storage_descriptor sto_d;
	}u;
	PVFS_error local_status;  /* error information for this single job */
	PVFS_error overall_status; /* duplicates status return from test() */
	void* caller_data;      /* external caller data */
	void* job_data;         /* internal job interface data */
	struct qlist_head queue_link; /* handle for queue insertion */
};
typedef struct PINT_job PINT_job_t;

/* interface functions */
int PINT_job_initialize(int flags);
int PINT_job_finalize(void);
PINT_job_t* PINT_job_create(PINT_job_type_t type);
int PINT_job_recreate(PINT_job_t* PINT_job, PINT_job_type_t type);
int PINT_job_free(PINT_job_t* job);
int PINT_job_post(PINT_job_t* job);
int PINT_job_test(PINT_job_t* job_to_test, int* outcount, PINT_job_state_t*
	state);
int PINT_job_testwait(PINT_job_t* job_to_test, int* outcount, PINT_job_state_t*
	state);
int PINT_job_testglobal(int incount, PINT_job_t** job_array, int* outcount, 
	PINT_job_state_t* state_array);
int PINT_job_testglobalwait(int incount, PINT_job_t** job_array, int* outcount, 
	PINT_job_state_t* state_array);



/********************************************************/

/* keeps up with whether bmi is initialized or not */
static int bmi_initialized = 0;
/* keeps up with whether job interface is initialized or not */
static int job_initialized = 0;

/* keep track of BMI addresses that have been resolved */
struct dummy_addr
{
	char string[256];
	bmi_addr_t addr;
};
static llist_p dummy_addr_list = NULL;
static bmi_addr_t last_addr = 3;

static int waitsome_flag = 0;

/* keep track of fake meta objects that we will handle */
struct dummy_meta
{
	char file_name[256];
	PVFS_handle handle;
	PVFS_fs_id fsid;
	PVFS_object_attr attr;
	PVFS_bitfield mask;
	PVFS_attr_extended extended;
#if 0
	/* REMOVED BY PHIL WHEN MOVING TO NEW TREE */
	PVFS_dist dist;
#endif

};
static llist_p dummy_meta_list = NULL;
static int64_t last_handle = 15;
static PVFS_handle last_dir_handle = 25;
/* For statfs */
static int filetotal = 10;
//static PVFS_fs_id fs_id = 666;
static int block_free = 1000;
static int block_size = 4096;
static int block_total = 900;
static int filefree = 7;

/* keep track of created directories */
struct dummy_dir
{
	char dir_name[256];
	char par_dir_name[256];
	PVFS_handle handle;
	PVFS_handle bucket;
	PVFS_fs_id fs_id;
	PVFS_handle parent_handle;
	PVFS_object_attr attr;
	PVFS_bitfield mask;
	PVFS_dirent *buf; 
	PVFS_count32 len;
};
static llist_p dummy_dir_list = NULL;
static llist_p dummy_parent_dir_list = NULL;

/* keep track of jobs that have been posted */
static llist_p dummy_job_list = NULL;

/* keep track of pending requests */
static llist_p dummy_pending_req_list = NULL;

/* structure used to search pending_req list */
struct pending_req_key
{
	bmi_addr_t addr;
	bmi_msg_tag_t tag;
};

static int get_next_segment(char *input, char **output, int *start);
static int dummy_complete_job_testwait(PINT_job_t* job_to_test);
static int cmp_ptr(void* foo1, void* foo2);
static int dummy_work_job_testwait(PINT_job_t* job_to_test, int* outcount, 
	PINT_job_state_t* state);
static int cmp_addr(void* foo1, void* foo2);
static int cmp_req_ack(void* foo1, void* foo2);
static int cmp_dirname(void* key, void* foo);
static int cmp_dir_parenthandle(void* key, void* foo);
static int handle_lookup_path_op(PINT_job_t* pending_request,\
		PINT_job_t* response);
static int handle_getattr_op(PINT_job_t* pending_request, PINT_job_t* response);
static int handle_setattr_op(PINT_job_t* pending_request, PINT_job_t* response);
static int handle_mkdir_op(PINT_job_t* pending_request, PINT_job_t* response);
static int handle_crdirent_op(PINT_job_t* pending_request, PINT_job_t* 
	response);
static int handle_rmdir_op(PINT_job_t* pending_request, PINT_job_t* response);
static int handle_rmdirent_op(PINT_job_t* pending_request, 
	PINT_job_t* response);
static int handle_readdir_op(PINT_job_t* pending_request, PINT_job_t* response);
static int handle_statfs_op(PINT_job_t* pending_request, PINT_job_t* response);
static int handle_iostatfs_op(PINT_job_t* pending_request, 
	PINT_job_t* response);
static int handle_create_op(PINT_job_t* pending_request, PINT_job_t* response);
static int handle_getconfig_op(PINT_job_t* pending_request,
		PINT_job_t* response);
/*************************************************************8
 * BMI PORTION
 */

/* BMI_initialize()
 *
 * Initializes the BMI interface
 *
 * returns 0 on success, -errno on failure
 */
int BMI_initialize(const char* module_string, const char* listen_addr,
   bmi_flag_t flags)
{

	if(!module_string || listen_addr || flags)
	{
		return(-EINVAL);
	}

	if(bmi_initialized)
	{
		return(-EBUSY);
	}

	dummy_addr_list = llist_new();
	if(!dummy_addr_list)
	{	
		return(-ENOMEM);
	}

	bmi_initialized = 1;

	return(0);
}

/* BMI_finalize()
 *
 * shuts down the BMI interface
 *
 * returns 0 on success, -errno on failure
 */
int BMI_finalize(void)
{

	if(!bmi_initialized)
	{
		return(-EBUSY);
	}

	if(dummy_addr_list)
	{
		llist_free(dummy_addr_list, free);
	}

	bmi_initialized = 0;

	return(0);

}

/* BMI_memalloc()
 *
 * allocates optimized BMI memory
 *
 * returns pointer to buffer on success, NULL on failure
 */
void* BMI_memalloc(bmi_addr_t addr, bmi_size_t size, bmi_flag_t
   send_recv)
{
	if(!addr)
	{
		return(NULL);
	}

	return(malloc(size));

}

/* BMI_memfree()
 *
 * frees memory allocated with BMI_memalloc
 *
 * returns 0 on success, -errno on failure
 */
int BMI_memfree(bmi_addr_t addr, void* buffer, bmi_size_t size, bmi_flag_t send_recv)
{
	if(!addr || !buffer)
	{
		return(-EINVAL);
	}

	free(buffer);
	return(0);
}

/* BMI_addr_lookup()
 *
 * resolves the string rep. of a host address into a BMI opaque address
 * type
 *
 * returns 0 on success, -errno on failure
 */
int BMI_addr_lookup(bmi_addr_t* new_addr, const char* id_string)
{
	struct dummy_addr* addr_foo =  NULL;
	int ret = -1;

	if(!new_addr || !id_string)
	{
		return(-EINVAL);
	}

	addr_foo = malloc(sizeof(struct dummy_addr));
	if(!addr_foo)
	{
		return(-ENOMEM);
	}

	strcpy(addr_foo->string, id_string);
	last_addr++;
	addr_foo->addr = last_addr;

	ret = llist_add(dummy_addr_list, addr_foo);
	if(ret != 0)
	{
		free(addr_foo);
		return(ret);
	}

	*new_addr = last_addr;

	return(0);
}

/*************************************************************8
 * CURRENT JOB PORTION
 */

/* job_initialize()
 *
 * start up the job interface
 *
 * returns 0 on success, -errno on failure
 */
int job_initialize(
   int flags)
{
	return(PINT_job_initialize(flags));
}

/* job_finalize()
 *
 * shuts down the job interface 
 *
 * returns 0 on success, -errno on failure
 */
int job_finalize(
	void)
{
	return(PINT_job_finalize());
}

/* job_bmi_send()
 *
 * posts a bmi send operation through the job interface
 *
 * returns 0 on success, 1 on immediate completion, -errno on failure
 */
int job_bmi_send(
	bmi_addr_t addr,
	void* buffer,
	bmi_size_t size,
	bmi_msg_tag_t tag,
	bmi_flag_t buffer_flag,
	bmi_flag_t send_unexpected,
	void* user_ptr,
	job_status_s* out_status_p,
	job_id_t* id)
{
	PINT_job_t* new_job = NULL;

	/* create the old style job structure */
	new_job = PINT_job_create(BMI_JOB);
	if(!new_job)
	{
		return(-ENOMEM);
	}

	/* create an opaque id */
	id_gen_fast_register(id, new_job);

	/* fill in the rest of the job fields */
	new_job->caller_data = user_ptr;
	new_job->u.bmi_d.op_type = BMI_OP_SEND;
	new_job->u.bmi_d.address = addr;
	new_job->u.bmi_d.buffer = buffer;
	new_job->u.bmi_d.size = size;
	new_job->u.bmi_d.buffer_flag = buffer_flag;
	new_job->u.bmi_d.tag = tag;
	new_job->u.bmi_d.send_unexpected = send_unexpected;

	return(PINT_job_post(new_job));
}

/* job_bmi_recv()
 *
 * posts a bmi recv job 
 *
 * returns 0 on success, 1 on immediate completion, -errno on failure
 */
int job_bmi_recv(
	bmi_addr_t addr,
	void* buffer,
	bmi_size_t size,
	bmi_msg_tag_t tag,
	bmi_flag_t buffer_flag,
	void* user_ptr,
	job_status_s* out_status_p,
	job_id_t* id)
{
	PINT_job_t* new_job = NULL;

	/* create the old style job structure */
	new_job = PINT_job_create(BMI_JOB);
	if(!new_job)
	{
		return(-ENOMEM);
	}

	/* create an opaque id */
	id_gen_fast_register(id, new_job);

	/* fill in the rest of the job fields */
	new_job->caller_data = user_ptr;
	new_job->u.bmi_d.op_type = BMI_OP_RECV;
	new_job->u.bmi_d.address = addr;
	new_job->u.bmi_d.buffer = buffer;
	new_job->u.bmi_d.size = size;
	new_job->u.bmi_d.buffer_flag = buffer_flag;
	new_job->u.bmi_d.tag = tag;

	return(PINT_job_post(new_job));
}


/* job_wait()
 *
 * briefly blocking check for completion of a particular job
 *
 * returns 0 on success, -errno on failure
 */
int job_wait(
	job_id_t id,
	int* out_count_p,
	void** returned_user_ptr_p,
	job_status_s* out_status_p)
{

	PINT_job_t* test_job = NULL;
	int ret = -1;
	PINT_job_state_t state;

	/* find out which old style job structure this id corresponds to */
	test_job = id_gen_fast_lookup(id);

	/* call old test function */
	ret = PINT_job_testwait(test_job, out_count_p, &state);

	if(ret < 0)
	{
		return(ret);
	}

	/* if we got something, then fill in the new style status and user
	 * pointer
	 */
	if((*out_count_p) == 1)
	{
		if (returned_user_ptr_p)
			*returned_user_ptr_p = test_job->caller_data;
		out_status_p->error_code = state;
		if(test_job->type == BMI_JOB)
		{
			out_status_p->actual_size = test_job->u.bmi_d.size;
		}
		else
		{
			fprintf(stderr, "ERROR! UNSUPPORTED JOB TYPE IN TEST.\n");
			return(-EINVAL);
		}
	}

	return(0);
}

/* job_waitsome()
 *
 * briefly blocking check for completion of one or more of a set of jobs
 *
 * returns 0 on success -errno on failure
 */
int job_waitsome(
	job_id_t* id_array,
	int* inout_count_p,
	int* out_index_array,
	void** returned_user_ptr_array,
	job_status_s* out_status_array_p)
{
	int done_count = 0;
	int i=0,j = 0;
	int ret=-1;
	int tmp_count = 0;
	void* tmp_ptr = NULL;
	job_status_s tmp_status;

	/* NOTE: this particular part of the test harness is _not_ a perfect
	 * emulation.  It will always try to handle the id_array in order.
	 * Therefore it will break if the completion of one job depends on
	 * the completion of a later job.  This should not happen in the
	 * typical client cases, however.
	 */

	/* NOTE: for now this function will alternate between trying to
	 * complete all of the jobs in question or trying to complete half of
	 * the jobs in question; just for variety of behavior
	 */

	if(waitsome_flag == 0)
	{
		waitsome_flag = 1;
	}
	else
	{
		waitsome_flag = 0;
		/* half the number of jobs to work on, even if it results in no
		 * work occurring.
		 */
		*inout_count_p = (*inout_count_p)/2;
	}
	 
	/* iterate through each job that we are going to look at */
	for(i= 0,j=0; j<(*inout_count_p); i++)
	{
		if(id_array[i])
		{
			/* call the normal waitsome function */
			ret = job_wait(id_array[i], &tmp_count, &tmp_ptr, &tmp_status); 
			if(ret < 0)
			{
				/* critical error */
				return(ret);
			}
			if(tmp_count == 1)
			{
				/* this job completed */
				out_index_array[done_count] = i;
				if (returned_user_ptr_array)
					returned_user_ptr_array[done_count] = tmp_ptr;
				out_status_array_p[done_count] = tmp_status;
				done_count++;
			}
			j++;
		}
	}
	
	/* announce how many items completed in total */
	*inout_count_p = done_count;

	return(0);
}

/*************************************************************8
 * OLD JOB PORTION
 */

int PINT_job_initialize(int flags)
{
	char filename1[] = "parl";
	char filename2[] = "harish";
	char filename3[] = "file1";
	struct dummy_meta* meta_foo1 = NULL,*meta_foo2 = NULL,*meta_foo3 = NULL;
	int ret = -1;
	int my_uid = 13053;
	int my_gid = 17454;

	if(job_initialized)
	{
		return(-EBUSY);
	}

	dummy_meta_list = llist_new();
	if(!dummy_meta_list)
	{	
		return(-ENOMEM);
	}

	/* put some fake files into the meta list */
	/* file 1 */
	meta_foo1 = malloc(sizeof(struct dummy_meta));
	if(!meta_foo1)
	{
		llist_free(dummy_meta_list, free);
		return(-ENOMEM);
	}
	last_handle++;
	meta_foo1->handle = last_handle;
	strcpy(meta_foo1->file_name, filename1);
	meta_foo1->attr.owner = my_uid;
	meta_foo1->attr.group = my_gid;
	meta_foo1->attr.perms = 63; 
	meta_foo1->attr.objtype = ATTR_META; 
	//meta_foo1->attr.u.meta.size = 245;
	meta_foo1->mask = 71;

	/* file 2 */
	meta_foo2 = malloc(sizeof(struct dummy_meta));
	if (!meta_foo2)
	{
		llist_free(dummy_meta_list, free);
		return(-ENOMEM);
	}
	last_handle++;
	meta_foo2->handle = last_handle;
	strcpy(meta_foo2->file_name,filename2);
	meta_foo2->attr.owner = my_uid;
	meta_foo2->attr.group = my_gid;
	meta_foo2->attr.perms = 63;
	meta_foo2->attr.objtype = ATTR_META; 
	meta_foo2->mask = 71;

	/* file 3 */
	meta_foo3 = malloc(sizeof(struct dummy_meta));
	if (!meta_foo3)
	{
		llist_free(dummy_meta_list, free);
		return(-ENOMEM);
	}
	last_handle++;
	meta_foo3->handle = last_handle;
	strcpy(meta_foo3->file_name,filename3);
	meta_foo3->attr.owner = my_uid;
	meta_foo3->attr.group = my_gid;
	meta_foo3->attr.perms = 63;
	meta_foo3->attr.objtype = ATTR_META; 
	meta_foo3->mask = 71;

	/* Add these files to the meta_list */
	/* file 1 */
	ret = llist_add(dummy_meta_list, meta_foo1);
	if(ret != 0)
	{
		llist_free(dummy_meta_list, free);
		return(ret);
	}
	/* file 2 */
	ret = llist_add(dummy_meta_list, meta_foo2);
	if(ret != 0)
	{
		llist_free(dummy_meta_list, free);
		return(ret);
	}
	/* file 3 */
	ret = llist_add(dummy_meta_list, meta_foo3);
	if(ret != 0)
	{
		llist_free(dummy_meta_list, free);
		return(ret);
	}

	/* create a list to keep track of created dirs */
	dummy_dir_list = llist_new();
	if(!dummy_dir_list)
	{
		llist_free(dummy_dir_list, free);
		return(-ENOMEM);
	}

	/* create a list to keep track of parent dirs */
	dummy_parent_dir_list = llist_new();
	if(!dummy_parent_dir_list)
	{
		llist_free(dummy_parent_dir_list, free);
		return(-ENOMEM);
	}

	/* create a list to keep track of posted jobs */
	dummy_job_list = llist_new();
	if(!dummy_job_list)
	{
		llist_free(dummy_meta_list, free);
		return(-ENOMEM);
	}

	/* create a list to keep track of pending requests */
	dummy_pending_req_list = llist_new();
	if(!dummy_pending_req_list)
	{
		llist_free(dummy_meta_list, free);
		llist_free(dummy_job_list, free);
		return(-ENOMEM);
	}

	job_initialized = 1;

	return(0);
}

/* PINT_job_finalize()
 *
 * shuts down the job interface
 *
 * returns 0 on success, -errno on failure
 */
int PINT_job_finalize(void)
{
	int job_count = 0;

	if(!job_initialized)
	{
		return(-EALREADY);
	}

	job_count = llist_count(dummy_job_list);
	if(job_count > 0)
	{
		printf("%d JOBS LEFT PENDING!\n", job_count);
	}

	llist_free(dummy_dir_list, free);
	llist_free(dummy_parent_dir_list, free);
	llist_free(dummy_meta_list, free);
	llist_free(dummy_job_list, free);
	llist_free(dummy_pending_req_list, free);
	

	job_initialized = 0;

	return(0);
}


/* PINT_job_create()
 * 
 * Creates a new job structure.  May be linked onto an existing job
 * structure if desired.
 *
 * returns pointer to structure on success, NULL on failure
 */
PINT_job_t* PINT_job_create(PINT_job_type_t type)
{
	/* this is taken directly from the real job implementation */

	struct PINT_job* new_job = NULL;

	/* go ahead and create enough room for internal data as well */
	new_job = (struct PINT_job*)malloc(sizeof(struct PINT_job));
	if(!new_job)
	{
		return(NULL);
	}

	memset(new_job, 0, (sizeof(struct PINT_job)));

	new_job->type = type;

	/* set pointer to internal data */
	new_job->job_data = (void*)((unsigned long)new_job + sizeof(struct
		PINT_job));

	return(new_job);

}

/* PINT_job_free()
 * 
 * Frees an existing job structure
 *
 * returns 0 on success, -errno on failure
 */
int PINT_job_free(PINT_job_t* job)
{

	free(job);

	return(0);
}


/* PINT_job_post()
 * 
 * Posts a job and any other jobs that are linked off of it in a single
 * function call.
 *
 * returns 0 on success, -errno on failure
 */
int PINT_job_post(PINT_job_t* job)
{
	int ret =-1;

	if(!job)
	{
		return(-EINVAL);
	}

	/* just stick it into a list for now; we will do the real work during
	 * the test functions 
	 */

	while(job)
	{
		ret = llist_add(dummy_job_list, job);
		if(ret != 0)
		{
			return(ret);
		}

		if(job->concurrent)
		{
			job = job->concurrent;
		}
		else
		{
			job = job->next;
		}
	}

	return(0);
}


/* PINT_job_testwait()
 * 
 * Checks for completion of a particular collection of jobs.  This
 * version is allowed to do work or at least stall momentarily before
 * returning.  Can be called safely within a tight loop.
 *
 * returns 0 on success, -errno on failure
 */
int PINT_job_testwait(PINT_job_t* job_to_test, int* outcount, 
	PINT_job_state_t* state)
{

	int complete_the_job = 0;
	PINT_job_t* job_holder = job_to_test;
	int ret = -1;

	/* TODO */
	/* probably need some logic to randomly decide how this function will
	 * react each time it is called.  for now it always completes on the
	 * first try.
	 */
	complete_the_job = 1;

	if(complete_the_job)
	{
		while(job_to_test)
		{
			ret = dummy_complete_job_testwait(job_to_test);
			if(ret < 0)
			{
				return(ret);
			}

			if(job_to_test->local_status != JOB_DONE)
			{
				job_holder->overall_status = job_to_test->local_status;
				*state = job_to_test->local_status;
				*outcount = 1;
				return(0);
			}

			if(job_to_test->concurrent)
			{
				job_to_test = job_to_test->concurrent;
			}
			else if(job_to_test->next)
			{
				job_to_test = job_to_test->next;
			}
			else
			{
				/* we are done */
				job_holder->overall_status = JOB_DONE;
				*state = JOB_DONE;
				*outcount = 1;
				return(0);
			}
		}
	}
	else
	{
		return(dummy_work_job_testwait(job_to_test, outcount, state));
	}

	/* should not reach here */
	return(-ENOSYS);
}


/* dummy_work_job_testwait()
 *
 * pretends to do work on a job
 *
 * returns 0 on success, -errno on failure
 */
static int dummy_work_job_testwait(PINT_job_t* job_to_test, int* outcount, 
	PINT_job_state_t* state)
{
	PINT_job_t* job_foo = NULL;

	/* make sure that we do know about the job */
	job_foo = llist_search(dummy_job_list, job_to_test, cmp_ptr);
	if(!job_foo)
	{
		return(-EINVAL);
	}

	sleep(1);
	*outcount = 0;

	return(0);
}

/* cmp_ptr()
 * 
 * stupid pointer comparison for llist_search()
 *
 * returns 0 if match, 1 otherwise
 */
static int cmp_ptr(void* foo1, void* foo2)
{
	if(foo1 == foo2)
	{
		return(0);
	}

	return(1);
}

/* cmp_addr()
 * 
 * bmi_addr comparison for llist_search()
 *
 * returns 0 if match, 1 otherwise
 */
static int cmp_addr(void* key, void* foo)
{
	bmi_addr_t addr_key = *((bmi_addr_t*)key);
	struct dummy_addr* dummy1 = foo;

	if(addr_key == dummy1->addr)
	{
		return(0);
	}

	return(1);
}

/* cmp_dirname()
 * 
 * compares a dir_name against the meta objects we know about
 *
 * returns 0 if match, 1 otherwise
 */
static int cmp_dirname(void* key, void* foo)
{
	char* parent_name = (char*)key;
	struct dummy_dir* dummy = (struct dummy_dir*)foo;

	if(!strcmp(parent_name, dummy->par_dir_name))
	{
		return(0);
	}

	return(1);
}

/* cmp_meta_filename()
 * 
 * compares a file_name against the meta objects we know about
 *
 * returns 0 if match, 1 otherwise
 */
static int cmp_meta_filename(void* key, void* foo)
{
	char* file_name = (char*)key;
	struct dummy_meta* dummy = (struct dummy_meta*)foo;

	if(!strcmp(file_name, dummy->file_name))
	{
		return(0);
	}

	return(1);
}


/* cmp_meta_filehandle()
 * 
 * compares a file_handle against the meta objects we know about
 *
 * returns 0 if match, 1 otherwise
 */
static int cmp_meta_filehandle(void* key, void* foo)
{
	int64_t *file_handle = (int64_t *)key;
	struct dummy_meta* dummy = (struct dummy_meta*)foo;

	if (*file_handle == dummy->handle)
	{
		return(0);
	}

	return(1);
}

/* cmp_dir_filehandle()
 * 
 * compares a file_handle against the dir objects we know about
 *
 * returns 0 if match, 1 otherwise
 */
static int cmp_dir_filehandle(void* key, void* foo)
{
	int64_t *file_handle = (int64_t *)key;
	struct dummy_dir* dummy = (struct dummy_dir*)foo;

	if (*file_handle == dummy->handle)
	{
		return(0);
	}

	return(1);
}

/* cmp_dir_parenthandle()
 * 
 * compares a parent_handle against the dir objects we know about
 *
 * returns 0 if match, 1 otherwise
 */
static int cmp_dir_parenthandle(void* key, void* foo)
{
	int64_t *file_handle = (int64_t *)key;
	struct dummy_dir* dummy = (struct dummy_dir*)foo;

	if (*file_handle == dummy->parent_handle)
	{
		return(0);
	}

	return(1);
}

/* cmp_req_ack()
 * 
 * compares the address and tag to match a request with it's ack
 *
 * returns 0 if match, 1 otherwise
 */
static int cmp_req_ack(void* key, void* foo)
{
	struct pending_req_key* key_foo = (struct pending_req_key*)key;
	PINT_job_t* job_foo = (PINT_job_t*)foo;

	if(key_foo->addr == job_foo->u.bmi_d.address && key_foo->tag ==
		job_foo->u.bmi_d.tag)
	{
		return(0);
	}

	return(1);
}


/* dummy_complete_job_testwait()
 *
 * finish a pending job
 *
 * returns 0 on success, -errno on failure
 */
static int dummy_complete_job_testwait(PINT_job_t* job_to_test)
{
	PINT_job_t* job_foo = NULL;
	PINT_job_t* job_pending = NULL;
	struct dummy_addr* addr_foo = NULL;
	int ret = -1;
	struct pending_req_key key;
	struct PVFS_server_req_s * tmp_request = NULL;

	/* set the BMI state to error unless we return successfully */
	job_to_test->u.bmi_d.error_code = -EPROTO;

	/* first of all, make sure that we have the job */
	job_foo = llist_rem(dummy_job_list, job_to_test, cmp_ptr);
	if(!job_foo)
	{
		return(-EINVAL);
	}

	/* do some checks on the job structure */
	if(job_foo->type != BMI_JOB)
	{
		/* the job is poorly formatted */
		job_foo->local_status = JOB_ERROR;
		return(0);
	}

	/* verify that this is being sent to a reasonable address */
	addr_foo  = llist_search(dummy_addr_list,
		&(job_foo->u.bmi_d.address), cmp_addr);
	if(!addr_foo)
	{
		/* the address is bad */
		job_foo->local_status = JOB_ERROR;
		return(0);
	}

	/* if this is a send operation, just move this job to the pending
	 * request list 
	 */
	if(job_foo->u.bmi_d.op_type == BMI_OP_SEND)
	{
		ret = llist_add(dummy_pending_req_list, job_foo);
		if(ret != 0)
		{
			job_foo->local_status = JOB_ERROR;
			return(0);
		}
		/* If no ack jobs clear pending_req_list as jobs
		 * in list will be deallocated further ahead 
		 */
		/* The code below was added by Harish for funcs
		 * without any response structs. This assumes that
		 * acks structs are chained to the req structs so
		 * commented for now as no chaining enables now!!! 
		 */
		/*cnt_jobs = llist_count(dummy_pending_req_list);
		if (cnt_jobs > 0 && !job_foo->next)
		{
			key.addr = job_foo->u.bmi_d.address;
			key.tag = job_foo->u.bmi_d.tag;

			job_pending = llist_rem(dummy_pending_req_list, &key, cmp_req_ack);
			if(!job_pending)
			{
				// this job doesn't match a pending request 
				job_foo->local_status = JOB_ERROR;
				return(0);
			}
		}*/
		/* we are done */
		job_foo->local_status = JOB_DONE;
		return(0);
	}

	if(job_foo->u.bmi_d.op_type != BMI_OP_RECV)
	{
		/* what kind of op is this? */
		return(-EINVAL);
	}

	/* if this is a receive operation, find the pending req that matches
	 * it, then pass it along to the proper function to actually handle
	 * the operation 
	 */
	key.addr = job_foo->u.bmi_d.address;
	key.tag = job_foo->u.bmi_d.tag;

	job_pending = llist_rem(dummy_pending_req_list, &key, cmp_req_ack);
	if(!job_pending)
	{
		/* this job doesn't match a pending request */
		job_foo->local_status = JOB_ERROR;
		return(0);
	}
		
	tmp_request = job_pending->u.bmi_d.buffer;
	switch(tmp_request->op)
	{
		case PVFS_SERV_LOOKUP_PATH:
			ret = handle_lookup_path_op(job_pending, job_foo);
			break;
		case PVFS_SERV_GETATTR:
			ret = handle_getattr_op(job_pending, job_foo);
			break;
		case PVFS_SERV_SETATTR:
			ret = handle_setattr_op(job_pending, job_foo);
			break;
		case PVFS_SERV_MKDIR:
			ret = handle_mkdir_op(job_pending, job_foo);
			break;
		case PVFS_SERV_CREATEDIRENT:
			ret = handle_crdirent_op(job_pending, job_foo);
			break;
		case PVFS_SERV_RMDIR:
			ret = handle_rmdir_op(job_pending, NULL);
			break;
		case PVFS_SERV_RMDIRENT:
			ret = handle_rmdirent_op(job_pending, job_foo);
			break;
		case PVFS_SERV_READDIR:
			ret = handle_readdir_op(job_pending, job_foo);
			break;
		case PVFS_SERV_STATFS:
			ret = handle_statfs_op(job_pending, job_foo);
			break;
		case PVFS_SERV_IOSTATFS:
			ret = handle_iostatfs_op(job_pending, job_foo);
			break;
		case PVFS_SERV_CREATE:
			ret = handle_create_op(job_pending, job_foo);
			break;
		case PVFS_SERV_GETCONFIG:
			ret = handle_getconfig_op(job_pending, job_foo);
			break;
			/* TODO: add more ops here as the sysint grows */
		default:
			/* what is this? */
			ret = -EINVAL;
	}

	if(ret == 0)
	{
		job_foo->local_status = JOB_DONE;
	}
	else
	{
		job_foo->local_status = JOB_ERROR;
	}
	/*free(job_pending);*/
	
	return(0);
}

/* handle_lookup_path_op()
 *
 * responds to lookup requests 
 *
 * returns 0 on success, -errno on failure
 */
static int handle_lookup_path_op(PINT_job_t* pending_request,\
		PINT_job_t* response)
{
	struct PVFS_server_resp_s* serv_ack = response->u.bmi_d.buffer;
	struct PVFS_server_req_s* serv_req = pending_request->u.bmi_d.buffer;
	char* req_name = (char*)((char*)serv_req + (sizeof(struct\
					PVFS_server_req_s)));
	struct dummy_meta* dummy = NULL;
	char *segment = NULL;
	PVFS_handle bkt = 5, temp_handle = 0;
	int count = 0,i = 0,ret = 0,start = 0,maskbits = 4;

	serv_ack->op = PVFS_SERV_LOOKUP_PATH;
	serv_ack->rsize = serv_req->rsize;

	/* How many path segments? */
	count = llist_count(dummy_meta_list);

	/* check if the handle array is already allocated */
	if (!serv_ack->u.lookup_path.handle_array)
	{
		return(-ENOMEM);	
	}

	/* check if the attribute array is already allocated */
	if (!serv_ack->u.lookup_path.attr_array)
	{
		/* free the handle array */
		return(-ENOMEM);

	}
	
	/* Return the values for whole path */
	for(i = 0; i < count;i++)
	{
		ret = get_next_segment(req_name,&segment,&start);
		if (ret < 0)
		{
			return(-1);
		}

		/* find out if we know about this file */
		dummy = llist_search(dummy_meta_list, segment, cmp_meta_filename);
		if(!dummy)
		{
			/* didn't find it */
			/* TODO: how do we represent this?  a "null" handle value? */
			serv_ack->u.lookup_path.handle_array[i] = 0;
		}
		else
		{
			temp_handle = bkt << (64 - maskbits);
			temp_handle = temp_handle + dummy->handle;
			serv_ack->u.lookup_path.handle_array[i] = temp_handle;
			serv_ack->u.lookup_path.attr_array[i] = dummy->attr;
		}
		free(segment);
	}
	serv_ack->u.lookup_path.count = count;

	return(0);
}

/* handle_getattr_op()
 *
 * responds to getattr requests 
 *
 * returns 0 on success, -errno on failure
 */
static int handle_getattr_op(PINT_job_t* pending_request, PINT_job_t* response)
{
	
	struct PVFS_server_resp_s* serv_ack = response->u.bmi_d.buffer;
	struct PVFS_server_req_s* serv_req = pending_request->u.bmi_d.buffer;
	PVFS_handle req_handle = serv_req->u.getattr.handle & 127;
	int type = serv_req->u.getattr.attrmask;
	struct dummy_meta* dummy = NULL;
	struct dummy_dir *dum_dir = NULL;

	serv_ack->op = PVFS_SERV_GETATTR;
	serv_ack->rsize = sizeof(struct PVFS_server_resp_s);

	/* find out if we know about this file */
	if (req_handle < 25) {	
		dummy = llist_search(dummy_meta_list, &req_handle, cmp_meta_filehandle);
		if(!dummy)
		{
			/* didn't find it */
			/* TODO: how do we represent this?  a "null" handle value? */
			serv_ack->u.getattr.attr.owner = 0;
			serv_ack->u.getattr.attr.group = 0;
			serv_ack->u.getattr.attr.perms = 0;
		}
		else
		{
			serv_ack->u.getattr.attr.owner = 0;
			serv_ack->u.getattr.attr.group = 0;
			serv_ack->u.getattr.attr.perms = 0;
			if (serv_req->u.getattr.attrmask && ATTR_BASIC)
			{
				serv_ack->u.getattr.attr.owner = dummy->attr.owner; 
				serv_ack->u.getattr.attr.group = dummy->attr.group; 
				serv_ack->u.getattr.attr.perms = dummy->attr.perms; 
				serv_ack->u.getattr.attr.objtype = dummy->attr.objtype; 
			}
			/*if (serv_req->u.getattr.attrmask && ATTR_SIZE)
			{
				serv_ack->u.getattr.attr.u.meta.size = dummy->attr.u.meta.size; 
			}*/	
		}
	}	
	else { 
		//for directories as handle starts from 25

		dum_dir = llist_search(dummy_dir_list, &req_handle, cmp_dir_filehandle);
		if(!dum_dir)
		{
			/* didn't find it */
			/* TODO: how do we represent this?  a "null" handle value? */
			serv_ack->u.getattr.attr.owner = 0;
			serv_ack->u.getattr.attr.group = 0;
			serv_ack->u.getattr.attr.perms = 0;
		}
		else
		{
			serv_ack->u.getattr.attr.owner = 0;
			serv_ack->u.getattr.attr.group = 0;
			serv_ack->u.getattr.attr.perms = 0;
			if (serv_req->u.getattr.attrmask && ATTR_BASIC)
			{
				serv_ack->u.getattr.attr.owner = dum_dir->attr.owner; 
				serv_ack->u.getattr.attr.group = dum_dir->attr.group; 
				serv_ack->u.getattr.attr.perms = dum_dir->attr.perms; 
				serv_ack->u.getattr.attr.objtype = dum_dir->attr.objtype; 
			}
			/*if (serv_req->u.getattr.attrmask && ATTR_SIZE)
			{
				serv_ack->u.getattr.attr.u.meta.size = dummy->attr.u.meta.size; 
			}*/	
		}
	}

	return(0);
}

/* handle_setattr_op()
 *
 * responds to setattr requests 
 *
 * returns 0 on success, -errno on failure
 */
static int handle_setattr_op(PINT_job_t* pending_request, PINT_job_t* response)
{
	struct PVFS_server_req_s* serv_req = pending_request->u.bmi_d.buffer;
	int64_t req_handle = serv_req->u.setattr.handle & 127;
	struct dummy_meta* dummy = NULL;

	/* find out if we know about this file */
	dummy = llist_search(dummy_meta_list, &req_handle, cmp_meta_filehandle);
	if(!dummy)
	{
		/* didn't find it */
		return(-1);
	}
	else
	{
		if (serv_req->u.setattr.attrmask && ATTR_BASIC)
		{
			dummy->attr.owner = serv_req->u.setattr.attr.owner; 
			dummy->attr.group = serv_req->u.setattr.attr.group; 
			dummy->attr.objtype = serv_req->u.setattr.attr.objtype;
			dummy->attr.perms = serv_req->u.setattr.attr.perms; 
		}
		if (serv_req->u.setattr.attrmask && ATTR_META)
		{
		}
		if (serv_req->u.setattr.attrmask && ATTR_SIZE)
		{
			//dummy->attr.u.meta.size = serv_req->u.setattr.attr.u.meta.size; 
		}	
	}

	return(0);
}

/* handle_mkdir_op()
 *
 * responds to mkdir requests 
 *
 * returns 0 on success, -errno on failure
 */
static int handle_mkdir_op(PINT_job_t* pending_request, PINT_job_t* response)
{
	struct PVFS_server_resp_s* serv_ack = response->u.bmi_d.buffer;
	struct PVFS_server_req_s* serv_req = pending_request->u.bmi_d.buffer;
	char* req_name = (char*)((char*)serv_req + (sizeof(struct\
		PVFS_server_req_s)));
	struct dummy_dir *dum_dir = NULL; 
	PVFS_handle temp_handle = 0;
	int ret = -1,i = 0,maskbits = 4;

	serv_ack->op = PVFS_SERV_MKDIR;
	serv_ack->rsize = sizeof(struct PVFS_server_resp_s);

	/* Alloc memory for dir entry */
	dum_dir = (struct dummy_dir *)malloc(sizeof(struct dummy_dir));
	if (!dum_dir)
	{
		return(-ENOMEM);
	}
	/* Fill in the directory entry */
	dum_dir->dir_name[0] = '\0';
	dum_dir->handle = last_dir_handle++;
	dum_dir->parent_handle = dum_dir->handle - 1;
	dum_dir->bucket = serv_req->u.mkdir.bucket >> (64 - maskbits); 
	dum_dir->fs_id = serv_req->u.mkdir.fs_id;
	dum_dir->attr = serv_req->u.mkdir.attr;
	dum_dir->mask = serv_req->u.mkdir.attrmask;
	/*
	for(i = cnt - 1;i >= 0;i--)
	{
		if (req_name[i] == '/')
		{
			if (i != 0)
			{
				strncpy(dum_dir->par_dir_name,req_name,i);
				dum_dir->par_dir_name[i] = '\0';
				break;
			}
			else if (i == 0)
			{
				strncpy(dum_dir->par_dir_name,"/",1);
				dum_dir->par_dir_name[1] = '\0';
			}

		 }
	 }
	*/
	/* create dir entries */
	dum_dir->buf = malloc(20* sizeof(PVFS_dirent));
	if (!dum_dir->buf)
	{
		printf("Unable to allocate buffer for dir entries\n");
		return(-ENOMEM);
	}
	for (i = 0;i < 20; i++)
	{
		/*dum_dir->buf[i].pinode_no.handle = i + 100;*/
		strncpy(dum_dir->buf[i].d_name,"/pvfs2/src/client",17);
		dum_dir->buf[i].d_name[17] = '\0';
	}
	dum_dir->len = 20 * sizeof(PVFS_dirent);
	/* add to list of directories */
	ret = llist_add(dummy_dir_list, dum_dir);
	if(ret != 0)
	{
		return(ret);
	}
	
	/* Return a 64 bit handle with the bucket embedded */
	temp_handle = serv_req->u.mkdir.bucket;
	serv_ack->u.mkdir.handle = temp_handle + dum_dir->handle;

	return(0);
}

/* handle_crdirent_op()
 *
 * responds to createdirent requests 
 *
 * returns 0 on success, -errno on failure
 */
static int handle_crdirent_op(PINT_job_t* pending_request, PINT_job_t* response)
{
	struct PVFS_server_req_s* serv_req = pending_request->u.bmi_d.buffer;
	char* req_name = (char*)((char*)serv_req + (sizeof(struct
		PVFS_server_req_s)));
	int ret = 0,name_sz = strlen(req_name);
	PVFS_handle parent_handle = serv_req->u.crdirent.parent_handle & 127;
	PVFS_fs_id fsid = serv_req->u.crdirent.fs_id;
	struct dummy_dir *dummy = NULL;

	//find out if we know about this dir 
	//ob_foo = llist_rem(dummy_job_list, job_to_test, cmp_ptr);
	dummy = llist_rem(dummy_dir_list, &parent_handle, cmp_dir_parenthandle);
	if(!dummy)
	{
		// didn't find it 
		// TODO: how do we represent this?  a "null" handle value? 
		return(-1);
	}
	strncpy(dummy->dir_name,req_name,name_sz);
	dummy->dir_name[name_sz] = '\0';
	//add the changed directory entry to the list
	ret = llist_add(dummy_dir_list, dummy);
	if(ret != 0)
	{
		return(ret);
	}

	return(0);
}

/* handle_rmdir_op()
 *
 * responds to rmdir requests 
 *
 * returns 0 on success, -errno on failure
 */
static int handle_rmdir_op(PINT_job_t* pending_request, PINT_job_t* response)
{
	struct PVFS_server_req_s* serv_req = pending_request->u.bmi_d.buffer;
	struct dummy_dir *dum_dir = NULL; 
	PVFS_handle req_handle = serv_req->u.rmdir.handle & 127;
	int ret = -1;

	/* add to list of directories */
	dum_dir = llist_search(dummy_dir_list, &req_handle, cmp_dir_filehandle);
	if(!dum_dir)
	{
		return(ret);
	}

	return(0);

}

/* handle_rmdirent_op()
 *
 * responds to rmdirent requests 
 *
 * returns 0 on success, -errno on failure
 */
static int handle_rmdirent_op(PINT_job_t* pending_request, PINT_job_t* response)
{
	struct PVFS_server_req_s* serv_req = pending_request->u.bmi_d.buffer;
	struct PVFS_server_resp_s* serv_ack = response->u.bmi_d.buffer;
	struct dummy_dir *dum_dir = NULL; 
	PVFS_handle parent_handle = serv_req->u.rmdirent.parent_handle & 127;
	PVFS_handle tmp_handle = 0, bkt = 1, maskbits = 4;
	int ret = -1;

	/* add to list of directories */
	dum_dir = llist_search(dummy_dir_list, &parent_handle,cmp_dir_parenthandle);
	if(!dum_dir)
	{
		return(ret);
	}
	tmp_handle = bkt << (64 - maskbits);
	serv_ack->u.rmdirent.entry_handle = tmp_handle + dum_dir->handle;
	
	return(0);

}

/* handle_readdir_op()
 *
 * responds to readdir requests 
 *
 * returns 0 on success, -errno on failure
 */
static int handle_readdir_op(PINT_job_t* pending_request, PINT_job_t* response)
{
	
	struct PVFS_server_resp_s* serv_ack = response->u.bmi_d.buffer;
	struct PVFS_server_req_s* serv_req = pending_request->u.bmi_d.buffer;
	struct dummy_dir* dummy = NULL;
	int64_t req_handle = serv_req->u.readdir.handle & 127;
	int len = 0,count = 0,token = 0;

	serv_ack->op = PVFS_SERV_READDIR;
	serv_ack->rsize = sizeof(struct PVFS_server_resp_s);

	/* Set the token and count */
	count = serv_req->u.readdir.pvfs_dirent_count;
	token = serv_req->u.readdir.token;
	/* find out if we know about this file */
	dummy = llist_search(dummy_dir_list, &req_handle, cmp_dir_filehandle);
	if(!dummy)
	{
		/* didn't find it */
		/* TODO: how do we represent this?  a "null" handle value? */
		return(-EINVAL);
	}
	else
	{
		/* Dealloc the ack buffer and reallocate */
		//BMI_memfree(response->u.bmi_d.address,serv_ack,BMI_OP_RECV);
		
		/* Check if requested no. of entries available */
		if (token + count > 20)
			count = 20 - token;
		len = count * sizeof(PVFS_dirent);
		/*response->u.bmi_d.size = sizeof(struct PVFS_server_resp_s) +
			count * sizeof(PVFS_dirent);*/
		/*response->u.bmi_d.buffer = BMI_memalloc(serv_req->u.readdir.server,
				response->u.bmi_d.size, BMI_OP_RECV);
		if(!response->u.bmi_d.buffer)
		{
			return(-ENOMEM);
		}*/
		/*serv_ack = (struct PVFS_server_resp_s *)response->u.bmi_d.buffer;*/

		/* Set up a readdir ack */
		/* Copy entries to response structure */
		memcpy(serv_ack->u.readdir.pvfs_dirent_array,&dummy->buf[token],len);
		serv_ack->u.readdir.pvfs_dirent_count = count;
		serv_ack->u.readdir.token = token + count;
	}

	return(0);
}

/* handle_statfs_op()
 *
 * responds to metaserver statfs requests 
 *
 * returns 0 on success, -errno on failure
 */
static int handle_statfs_op(PINT_job_t* pending_request, PINT_job_t* response)
{
	
	struct PVFS_server_resp_s* serv_ack = response->u.bmi_d.buffer;

	serv_ack->op = PVFS_SERV_STATFS;
	serv_ack->rsize = sizeof(struct PVFS_server_resp_s);

	/* Set the id and filecount */
	serv_ack->u.statfs.stat.u.mstat.filetotal = filetotal + 5;

	return(0);
}

/* handle_iostatfs_op()
 *
 * responds to ioserver statfs requests 
 *
 * returns 0 on success, -errno on failure
 */
static int handle_iostatfs_op(PINT_job_t* pending_request, PINT_job_t* response)
{
	
	struct PVFS_server_resp_s* serv_ack = response->u.bmi_d.buffer;

	serv_ack->op = PVFS_SERV_IOSTATFS;
	serv_ack->rsize = sizeof(struct PVFS_server_resp_s);

	serv_ack->u.statfs.stat.u.iostat.blkfree = block_free + 20;
	serv_ack->u.statfs.stat.u.iostat.blksize = block_size + 20;
	serv_ack->u.statfs.stat.u.iostat.blktotal = block_total + 20;
	serv_ack->u.statfs.stat.u.iostat.filetotal = filetotal + 5;
	serv_ack->u.statfs.stat.u.iostat.filefree = filefree + 5;

	return(0);
}

/* handle_create_op()
 *
 * responds to create requests 
 *
 * returns 0 on success, -errno on failure
 */
static int handle_create_op(PINT_job_t* pending_request, PINT_job_t* response)
{
	
	struct PVFS_server_resp_s* serv_ack = response->u.bmi_d.buffer;
	struct PVFS_server_req_s* serv_req = pending_request->u.bmi_d.buffer;
	struct dummy_meta* dummy = NULL, *my_meta = NULL;
	char *req_name = (char*)((char*)serv_req + (sizeof(struct
	PVFS_server_req_s)));
	int64_t req_handle = 0, ret = 0, i = 0, cnt = 0;
	struct dummy_dir *dum_dir = NULL;

	//if (!serv_req->u.create.handles)
	if (!serv_req)
	{
		req_name = (char*)((char*)serv_req + (sizeof(struct
		PVFS_server_req_s)));
		/* I/O server request */
		/* find out if we know about this file */
		dummy = llist_search(dummy_meta_list, req_name, cmp_meta_filename);
		if(!dummy)
		{
			/* didn't find it */
			/* TODO: how do we represent this?  a "null" handle value? */
			serv_ack->u.create.handle = last_handle + 10;
		}
		else
		{
			serv_ack->u.create.handle = dummy->handle;
		}
		serv_ack->op = PVFS_SERV_CREATE;
		serv_ack->rsize = sizeof(struct PVFS_server_resp_s);

	}
	else
	{
		//req_handle = serv_req->u.create.handles[0];
		dummy = llist_search(dummy_dir_list, &req_handle, cmp_dir_filehandle);
		if(!dummy)
		{
			/* didn't find it */
			/* put some fake files into the meta list */
			my_meta = malloc(sizeof(struct dummy_meta));
			if(!my_meta)
			{
				llist_free(dummy_meta_list, free);
				return(-ENOMEM);
			}
			strcpy(my_meta->file_name, req_name);
			my_meta->file_name[strlen(req_name)] = '\0';
			/*my_meta->attr.owner = my_uid;
			my_meta->attr.group = my_gid;
			my_meta->attr.perms = 24; 
			my_meta->attr.size = 245;
			my_meta->mask = 71;*/
			//my_meta->attr = serv_req->u.create.attr;

			ret = llist_add(dummy_meta_list, my_meta);
			if(ret != 0)
			{
				free(my_meta);
				return(ret);
			}
			/* Alloc memory for dir entry */
			dum_dir = (struct dummy_dir *)malloc(sizeof(struct dummy_dir));
			if (!dum_dir)
			{
				return(-ENOMEM);
			}
			/* Fill in the directory entry */
			strncpy(dum_dir->dir_name,req_name,strlen(req_name));
			dum_dir->dir_name[strlen(req_name)] = '\0';
			dum_dir->par_dir_name[0] = '\0';
			dum_dir->handle = last_handle + 10;
			//dum_dir->attr = serv_req->u.create.attr;
			//dum_dir->mask = serv_req->u.create.attrmask;
			cnt = strlen(req_name);
			for(i = cnt - 1;i >= 0;i--)
			{
				if (req_name[i] == '/')
				{
					if (i != 0)
					{
						strncpy(dum_dir->par_dir_name,req_name,i);
						dum_dir->par_dir_name[i] = '\0';
						break;
					}
					else if (i == 0)
					{
						strncpy(dum_dir->par_dir_name,"/",1);
						dum_dir->par_dir_name[1] = '\0';
					}

		 		}
	 		}
			/* add to list of directories */
			ret = llist_add(dummy_dir_list, dum_dir);
			if(ret != 0)
			{
				return(ret);
			}
	
			serv_ack->op = PVFS_SERV_CREATE;
			serv_ack->rsize = sizeof(struct PVFS_server_resp_s);

			serv_ack->u.create.handle = req_handle;
		}
	}

	return(0);
}

/* handle_getconfig_op()
 *
 * responds to getconfig requests 
 *
 * returns 0 on success, -errno on failure
 */
static int handle_getconfig_op(PINT_job_t* pending_request,PINT_job_t* response)
{
	struct PVFS_server_resp_s* serv_ack = response->u.bmi_d.buffer;
	struct PVFS_server_req_s* serv_req = pending_request->u.bmi_d.buffer;
	char meta[] = "tcp://www.yahoo.com:3000 1-2;gm://athena:2700 3-4;tcp://darkstar:4000 5-8";
	char io[] = "tcp://www.charon.com:3000 1-2;gm://crack:2700 3-4;tcp://ecstasy:4000 5-8";
	PVFS_handle bucket = 3, maskbits = 4;

	serv_ack->op = PVFS_SERV_GETCONFIG;

	if (!strcmp(serv_req->u.getconfig.fs_name,"/pvfs"))
	{
		//server mount point is /pvfs 
		serv_ack->u.getconfig.fs_id = 1;
		serv_ack->u.getconfig.root_handle = bucket << (64 - maskbits);
		serv_ack->u.getconfig.meta_server_count = 3;
		serv_ack->u.getconfig.io_server_count = 3;
		serv_ack->u.getconfig.maskbits = 4;
		// Fill in the meta server list  
		strncpy(&(serv_ack->u.getconfig.meta_server_mapping[0]),meta,\
				strlen(meta));
		serv_ack->u.getconfig.meta_server_mapping[strlen(meta)] = '\0';
		// Fill in the I/O server list  
		strncpy(&(serv_ack->u.getconfig.io_server_mapping[0]),io,\
				strlen(io));
		serv_ack->u.getconfig.io_server_mapping[strlen(io)] = '\0';

		//serv_ack->u.getconfig.strsize = pos;
		//serv_ack->u.getconfig.strsize = pos;
	}
	else {

		//server mount point is /pvfs 
		serv_ack->u.getconfig.fs_id = -1;
		serv_ack->u.getconfig.root_handle = -1;
		serv_ack->u.getconfig.meta_server_count = -1;
		serv_ack->u.getconfig.meta_server_mapping = NULL;
		serv_ack->u.getconfig.io_server_mapping = NULL;
	}

	return(0);
}

/* get_next_segment
 *
 * gets next path segment given full pathname
 *
 * returns 0 on success, -errno on failure
 */
static int get_next_segment(char *input, char **output, int *start)
{
	char *p1 = NULL,*p2 = NULL;
	char *s = (char *)&input[*start];
	int size = 0;

	/* Search for start of segment */
	p1 = index(s,'/');
	if (p1 != NULL)
	{
		/* Search for end of segment */
		p2 = index(p1 + 1,'/');
		if (p2 != NULL)
			size = p2 - p1;
		else
			size = &(input[strlen(input)]) - p1;
	}
	else
	{
		size = strlen(s) + 1;
	}

	/* In case we have no segment e.g "/" */
	if (size - 1)
	{
		/* Set up the segment to be returned */
		*output = (char *)malloc(size);
		strncpy(*output, p1 + 1, size - 1);
		(*output)[size - 1] = '\0';
		*start += size;
	}
	else
	{
		*output = NULL;
		*start = -1;
	}

	return(0);
}

