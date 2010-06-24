/* ZOID implementation of a BMI method -- I/O node server side */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <fcntl.h>
#include <mqueue.h>
#include <sys/mman.h>
#include <unistd.h>

#include <bmi-method-support.h>
#include <bmi-method-callback.h>
#include <gen-locks.h>
#include <id-generator.h>
#include <op-list.h>

#include "zoid.h"
#include "zbmi_pool.h"
#include "zbmi_protocol.h"

/* method_op_p's method_data points to this structure on the server side.  */
struct zoid_server_method_data
{
    void* tmp_buffer; /* Used with BMI_EXT_ALLOC to store the address of the
			 temporary shared memory buffer, NULL otherwise.  */
    int zoid_buf_id; /* 0 if the operation has not yet been sent to ZOID.  */
    gen_mutex_t post_mutex; /* Used to resolve the race between a post
			       call and testcontext.  */
};

#define METHOD_DATA(op) ((struct zoid_server_method_data*)(op)->method_data)

/* Describes a request with BMI_EXT_ALLOC pending for a temporary memory
   buffer.  */
struct no_mem_descriptor
{
    struct no_mem_descriptor* next;
    struct no_mem_descriptor* prev;

    bmi_size_t total_size;
    method_op_p op;
};

/* Control queue to the zbmi plugin  in the ZOID daemon.  */
static mqd_t zbmi_control_queue = -1;

/* Reply queues from the zbmi plugin.  */
#define ZBMI_QUEUES_LEN_INIT 10
static gen_mutex_t zbmi_queues_mutex = GEN_MUTEX_INITIALIZER;
static mqd_t* zbmi_queues = NULL;
static char* zbmi_queues_inuse = NULL; /* Whether a particular zbmi_queues
					  entry is currently in use.  */
static int zbmi_queues_len = 0; /* Length of zbmi_queues and
				   zbmi_queues_inuse.  */
static int zbmi_queues_used = 0; /* Count of initialized zbmi_queues
				    entries.  */

/* An array of client addresses.  */
#define CLIENTS_LEN_INC 10
static gen_mutex_t clients_mutex = GEN_MUTEX_INITIALIZER;
static bmi_method_addr_p* clients_addr = NULL;
static int clients_len = 0;

/* Shared memory buffer between the ZBMI plugin an us.  */
static char* zbmi_shm = NULL;
static char *zbmi_shm_eager, *zbmi_shm_rzv;

static int zbmi_shm_size_total;
static int zbmi_shm_size_eager;
static struct ZBMIEagerDesc* eager_desc;

/* Queue of operations with BMI_EXT_ALLOC buffers pending for a temporary
   memory buffer, sorted by descending total_size.  */
static struct no_mem_descriptor *no_mem_queue_first = NULL;
/* Only valid if no_mem_queue_first != NULL.  */
static struct no_mem_descriptor *no_mem_queue_last;
/* Protects access to the above queue.  */
static gen_mutex_t no_mem_queue_mutex = GEN_MUTEX_INITIALIZER;

/* Queue of failed/canceled operations (that the ZOID server doesn't know
   about).  */
static op_list_p error_ops;
static gen_mutex_t error_ops_mutex = GEN_MUTEX_INITIALIZER;


static mqd_t get_zoid_reply_queue(int* queue_id);
static void release_zoid_reply_queue(int queue_id);
static bmi_method_addr_p get_client_addr(int zoid_addr);
static int enqueue_no_mem(method_op_p op, bmi_size_t total_size);
static int send_post_cmd(method_op_p op, int not_immediate, int* length);


/* These symbols come from external libraries that would need to be linked
   even on the client-side, even though the client never actually invokes
   them.  */
typeof(shm_open) shm_open __attribute__((weak));
typeof(shm_unlink) shm_unlink __attribute__((weak));


/* Invoked on BMI_initialize.  */
int
BMI_zoid_server_initialize(void)
{
    struct ZBMIControlInitCmd cmd;
    struct ZBMIControlInitResp resp;
    int shm_fd;
    mqd_t reply_queue;
    int queue_id;

    /* Connect to the ZBMI plugin in the ZOID daemon.  */
    while ((zbmi_control_queue = mq_open(ZBMI_CONTROL_QUEUE_NAME, O_WRONLY))
	   < 0)
    {
	if (errno == ENOENT)
	{
	    /* Probably the ZOID server is not running yet...  */
	    sleep(1);
	}
	else
	{
	    perror("connect to ZOID");
	    return -BMI_EINVAL;
	}
    }

    /* This will initialize all the queue structures first.  */
    if ((reply_queue = get_zoid_reply_queue(&queue_id)) < 0)
	return reply_queue;

    /* Initial handshake.  */

    cmd.command_id = ZBMI_CONTROL_INIT;
    cmd.queue_id = queue_id;

    if (mq_send(zbmi_control_queue, (void*)&cmd, sizeof(cmd), 0) < 0)
    {
	perror("mq_send");
	release_zoid_reply_queue(queue_id);
	return -BMI_EINVAL;
    }

    if (mq_receive(reply_queue, (void*)&resp, ZBMI_MAX_MSG_SIZE, NULL)
	!= sizeof(resp))
    {
	perror("mq_receive");
	release_zoid_reply_queue(queue_id);
	return -BMI_EINVAL;
    }

    release_zoid_reply_queue(queue_id);

    zbmi_shm_size_total = resp.shm_size_total;
    zbmi_shm_size_eager = resp.shm_size_eager;

    /* Open the shared memory area.  */

    if ((shm_fd = shm_open(ZBMI_SHM_NAME, O_RDWR, 0)) < 0)
    {
	perror("ZBMI shm_open");
	return -BMI_ENOMEM;
    }

    if ((zbmi_shm = mmap(NULL, zbmi_shm_size_total, PROT_READ | PROT_WRITE,
			 MAP_SHARED, shm_fd, 0)) == MAP_FAILED)
    {
	perror("mmap");
	close(shm_fd);
	return -BMI_ENOMEM;
    }

    close(shm_fd);

    /* The shared memory buffer starts with an eager section, which is
       managed by the ZBMI ZOID plugin.  */
    zbmi_shm_eager = zbmi_shm;
    /* Eager section starts with a descriptor.  */
    eager_desc = (struct ZBMIEagerDesc*)zbmi_shm_eager;
    /* The rendezvous buffers part is initialized and managed by us.  */
    zbmi_shm_rzv = zbmi_shm + zbmi_shm_size_eager;
    zbmi_pool_init(zbmi_shm_rzv, zbmi_shm_size_total - zbmi_shm_size_eager);

    if (!(error_ops = op_list_new()))
	return -BMI_ENOMEM;

    return 0;
}

/* Invoked on BMI_finalize.  */
int
BMI_zoid_server_finalize(void)
{
    int i;

    if (error_ops)
    {
	op_list_cleanup(error_ops);
	error_ops = NULL;
    }

    zbmi_pool_fini();

    munmap(zbmi_shm, zbmi_shm_size_total);

    /* FIXME!  Send some sort of a FINI message first?  */
    for (i = 0; i < zbmi_queues_used; i++)
    {
	char queue_name[256];
	mq_close(zbmi_queues[i]);
	sprintf(queue_name, ZBMI_REPLY_QUEUE_TEMPLATE, i);
	mq_unlink(queue_name);
    }
    free(zbmi_queues_inuse);
    free(zbmi_queues);
    zbmi_queues_len = zbmi_queues_used = 0;
    mq_close(zbmi_control_queue);

    return 0;
}

/* Invoked on BMI_memalloc.  Because of the shared memory buffer, it is
   important for the performance that applications use it.  */
void*
BMI_zoid_server_memalloc(bmi_size_t size)
{
    return zbmi_pool_malloc(size);
}

/* Invoked on BMI_memfree.  */
void
BMI_zoid_server_memfree(void* buffer)
{
    struct no_mem_descriptor* desc;

    zbmi_pool_free(buffer);

    /* Once some memory has been freed, go over the queue of requests waiting
       for memory and try to satisfy any of them.  */
    gen_mutex_lock(&no_mem_queue_mutex);

    for (desc = no_mem_queue_first; desc;)
    {
	struct no_mem_descriptor* desc_next = desc->next;
	void* buf;

	if ((buf = BMI_zoid_server_memalloc(desc->total_size)))
	{
	    method_op_p op = desc->op;

	    METHOD_DATA(op)->tmp_buffer = buf;

	    if (op->send_recv == BMI_SEND)
	    {
		/* Copy the data to the temporary buffer.  */
		void *buf_cur = buf;
		int i;

		for (i = 0; i < op->list_count; i++)
		{
		    memcpy(buf_cur, op->buffer_list[i], op->size_list[i]);
		    buf_cur += op->size_list[i];
		}
	    }

	    /* Remove the request from the list, as we have succeeded in
	       allocating memory for it.  */
	    if (desc->prev)
		desc->prev->next = desc->next;
	    if (desc->next)
		desc->next->prev = desc->prev;
	    if (no_mem_queue_first == desc)
		no_mem_queue_first = no_mem_queue_first->next;
	    if (no_mem_queue_last == desc)
		no_mem_queue_last = no_mem_queue_last->prev;
	    free(desc);

	    /* Post it as "non-immediate" since we do not have facilities
	       here to manage immediate completions.  */
	    if ((op->error_code = -send_post_cmd(op, 1, NULL)))
	    {
		gen_mutex_lock(&error_ops_mutex);
		op_list_add(error_ops, op);
		gen_mutex_unlock(&error_ops_mutex);
	    }
	}

	desc = desc_next;
    }

    gen_mutex_unlock(&no_mem_queue_mutex);
}

/* Invoked on BMI_unexpected_free.  */
int
BMI_zoid_server_unexpected_free(void* buffer)
{
    struct ZBMIControlEagerFreeCmd cmd;

    if ((char*)buffer < zbmi_shm_eager ||
	(char*)buffer >= zbmi_shm_eager + zbmi_shm_size_eager)
    {
	return -BMI_EINVAL;
    }

    cmd.command_id = ZBMI_CONTROL_EAGER_FREE;
    cmd.unexpected = 1;
    cmd.buffer = (char*)buffer - zbmi_shm_eager;

    if (mq_send(zbmi_control_queue, (void*)&cmd, sizeof(cmd), 0) < 0)
    {
	perror("mq_send");
	return -BMI_EINVAL;
    }

    return 0;
}

/* Invoked on BMI_testunexpected.  */
int
BMI_zoid_server_testunexpected(int incount, int* outcount,
			       struct bmi_method_unexpected_info* info,
			       uint8_t class, int max_idle_time_ms)
{
    mqd_t reply_queue;
    int queue_id;
    int i;
    struct ZBMIControlUnexpTestCmd cmd;
    char resp_buffer[ZBMI_MAX_MSG_SIZE];
    struct ZBMIControlUnexpTestResp* resp;
    struct SharedMessageDescriptor* msg_desc;

    /* Check for ready messages in the shared memory region first.  */
    sem_wait(&eager_desc->unexp_queue_sem);

    *outcount = 0;
    for (msg_desc = (struct SharedMessageDescriptor*)
	     (eager_desc->unexp_queue_first + zbmi_shm_eager);
	 msg_desc != (struct SharedMessageDescriptor*)zbmi_shm_eager;
	 msg_desc = (struct SharedMessageDescriptor*)
	     (msg_desc->next + zbmi_shm_eager))
    {
	if (*outcount >= incount)
	    break;

	if (msg_desc->message_type == MSG_TYPE_SEND &&
	    msg_desc->class == class)
	{
	    info[*outcount].error_code = 0;
	    if (!(info[*outcount].addr = get_client_addr(msg_desc->addr)))
		return -BMI_ENOMEM;
	    info[*outcount].buffer = msg_desc->buffer;
	    info[*outcount].size = msg_desc->size;
	    info[*outcount].tag = msg_desc->tag;

	    /* Prevent any subsequent tests from matching it.  */
	    msg_desc->message_type = MSG_TYPE_DONE;

	    (*outcount)++;
	}
    }

    sem_post(&eager_desc->unexp_queue_sem);

    if (*outcount > 0 || max_idle_time_ms == 0)
	return 0;

    if ((reply_queue = get_zoid_reply_queue(&queue_id)) < 0)
	return reply_queue;

    cmd.command_id = ZBMI_CONTROL_UNEXP_TEST;
    cmd.queue_id = queue_id;
    cmd.incount = incount;
    cmd.max_idle_time_ms = max_idle_time_ms;
    cmd.class = class;

    if (mq_send(zbmi_control_queue, (void*)&cmd, sizeof(cmd), 0) < 0)
    {
	perror("mq_send");
	release_zoid_reply_queue(queue_id);
	return -BMI_EINVAL;
    }

    if (mq_receive(reply_queue, resp_buffer, sizeof(resp_buffer), NULL) < 0)
    {
	perror("mq_receive");
	release_zoid_reply_queue(queue_id);
	return -BMI_EINVAL;
    }

    release_zoid_reply_queue(queue_id);

    resp = (struct ZBMIControlUnexpTestResp*)resp_buffer;
    *outcount = resp->outcount;
    for (i = 0; i < resp->outcount; i++)
    {
	msg_desc = (struct SharedMessageDescriptor*)
	    (resp->descs[i] + zbmi_shm_eager);

	info[i].error_code = 0;
	if (!(info[i].addr = get_client_addr(msg_desc->addr)))
	    return -BMI_ENOMEM;
	info[i].buffer = msg_desc->buffer;
	info[i].size = msg_desc->size;
	info[i].tag = msg_desc->tag;

	/* msg_desc->message_type has already been set to MSG_TYPE_DONE
	   by the zbmi plugin.  */
    }

    return 0;
}

/* A common send routine used for all expected messages.  */
int
zoid_server_send_common(bmi_op_id_t* id, bmi_method_addr_p dest,
			const void*const* buffer_list,
			const bmi_size_t* size_list, int list_count,
			bmi_size_t total_size, enum bmi_buffer_type buffer_type,
			bmi_msg_tag_t tag, void* user_ptr,
			bmi_context_id context_id, PVFS_hint hints)
{
    method_op_p new_op;
    int ret;

    if (!(new_op = bmi_alloc_method_op(sizeof(struct zoid_server_method_data))))
	return -BMI_ENOMEM;
    *id = new_op->op_id;
    new_op->addr = dest;
    new_op->send_recv = BMI_SEND;
    new_op->user_ptr = user_ptr;
    new_op->msg_tag = tag;
    new_op->list_count = list_count;
    new_op->actual_size = total_size;
    if (list_count == 1)
    {
	new_op->buffer = (void*)buffer_list[0];
	new_op->buffer_list = &new_op->buffer;
	new_op->size_list = &new_op->actual_size;
    }
    else
    {
	new_op->buffer = NULL;
	new_op->buffer_list = (void*const*)buffer_list;
	new_op->size_list = size_list;
    }
    new_op->error_code = 0;
    METHOD_DATA(new_op)->tmp_buffer = NULL;
    METHOD_DATA(new_op)->zoid_buf_id = 0;

    if (buffer_type == BMI_EXT_ALLOC)
    {
	/* Copy to shared memory area.  */

	void *buf, *buf_cur;
	int i;

	if (!(buf = buf_cur = BMI_zoid_server_memalloc(total_size)))
	{
	    /* No memory for the temporary buffer.  This is not considered
	       a fatal error; we will retry when some memory is returned.  */
	    return enqueue_no_mem(new_op, total_size);
	}

	METHOD_DATA(new_op)->tmp_buffer = buf;

	for (i = 0; i < list_count; i++)
	{
	    memcpy(buf_cur, buffer_list[i], size_list[i]);
	    buf_cur += size_list[i];
	}
    }
    else
    {
	/* Verify that the buffer is actually allocated by us.  */
	int i;
	for (i = 0; i < list_count; i++)
	    if ((char*)buffer_list[i] < zbmi_shm_rzv ||
		(char*)buffer_list[i] + size_list[i] > zbmi_shm_rzv +
		zbmi_shm_size_total - zbmi_shm_size_eager)
	    {
		return -BMI_EINVAL;
	    }
    }

    if ((ret = send_post_cmd(new_op, 0, NULL)) == 1)
    {
	/* Immediate completion.  */
	if (buffer_type == BMI_EXT_ALLOC)
	    BMI_zoid_server_memfree(METHOD_DATA(new_op)->tmp_buffer);

	bmi_dealloc_method_op(new_op);
    }

    return ret;
}

/* A common receive routine used for all expected messages.  */
int
zoid_server_recv_common(bmi_op_id_t* id, bmi_method_addr_p src,
			void *const* buffer_list, const bmi_size_t* size_list,
			int list_count, bmi_size_t total_expected_size,
			bmi_size_t* total_actual_size,
			enum bmi_buffer_type buffer_type, bmi_msg_tag_t tag,
			void* user_ptr, bmi_context_id context_id,
			PVFS_hint hints)
{
    method_op_p new_op;
    int ret, length;
    struct SharedMessageDescriptor* msg_desc;

    /* Check for matching eager messages first.  */

    sem_wait(&eager_desc->eager_queue_sem);

    for (msg_desc = (struct SharedMessageDescriptor*)
	     (eager_desc->eager_queue_first + zbmi_shm_eager);
	 msg_desc != (struct SharedMessageDescriptor*)zbmi_shm_eager;
	 msg_desc = (struct SharedMessageDescriptor*)
	     (msg_desc->next + zbmi_shm_eager))
    {
	if (msg_desc->message_type == MSG_TYPE_SEND &&
	    msg_desc->addr == ((struct zoid_addr*)src->method_data)->pid &&
	    msg_desc->tag == tag)
	{
	    break;
	}
    }

    if (msg_desc != (struct SharedMessageDescriptor*)zbmi_shm_eager)
    {
	int i;
	size_t copied;
	struct ZBMIControlEagerFreeCmd cmd;

	/* We found a matching eager message.  All we need to do is copy
	   the memory over.  */

	if (total_expected_size < msg_desc->size)
	{
	    fprintf(stderr, "Message size mismatch, ION "
		    "expected size %lld < CN total size %d\n",
		    total_expected_size, msg_desc->size);

	    sem_post(&eager_desc->eager_queue_sem);
	    return -BMI_EINVAL;
	}

	copied = 0;
	i = 0;
	while (copied < msg_desc->size)
	{
	    size_t tocopy = (size_list[i] < msg_desc->size - copied ?
			     size_list[i] : msg_desc->size - copied);
	    memcpy(buffer_list[i], msg_desc->buffer + copied, tocopy);
	    copied += tocopy;
	    i++;
	}
	*total_actual_size = msg_desc->size;

	/* Prevent any subsequent matches.  */
	msg_desc->message_type = MSG_TYPE_DONE;

	sem_post(&eager_desc->eager_queue_sem);

	/* Request its release (only the zbmi plugin can do it).  */
	cmd.command_id = ZBMI_CONTROL_EAGER_FREE;
	cmd.unexpected = 0;
	cmd.buffer = (char*)msg_desc - zbmi_shm_eager;

	if (mq_send(zbmi_control_queue, (void*)&cmd, sizeof(cmd), 0) < 0)
	{
	    perror("mq_send");
	    return -BMI_EINVAL;
	}

	/* Immediate completion.  */
	return 1;
    }

    sem_post(&eager_desc->eager_queue_sem);

    if (!(new_op = bmi_alloc_method_op(sizeof(struct zoid_server_method_data))))
	return -BMI_ENOMEM;
    *id = new_op->op_id;
    new_op->addr = src;
    new_op->send_recv = BMI_RECV;
    new_op->user_ptr = user_ptr;
    new_op->msg_tag = tag;
    new_op->list_count = list_count;
    new_op->expected_size = total_expected_size;
    if (list_count == 1)
    {
	new_op->buffer = (void*)buffer_list[0];
	new_op->buffer_list = &new_op->buffer;
	new_op->size_list = &new_op->expected_size;
    }
    else
    {
	new_op->buffer = NULL;
	new_op->buffer_list = buffer_list;
	new_op->size_list = size_list;
    }
    new_op->error_code = 0;
    METHOD_DATA(new_op)->tmp_buffer = NULL;
    METHOD_DATA(new_op)->zoid_buf_id = 0;

    if (buffer_type == BMI_EXT_ALLOC)
    {
	/* Allocate a shared memory area.  */
	void* buf;

	if (!(buf = BMI_zoid_server_memalloc(total_expected_size)))
	{
	    /* No memory for the temporary buffer.  This is not considered
	       a fatal error; we will retry when some memory is returned.  */
	    return enqueue_no_mem(new_op, total_expected_size);
	}

	METHOD_DATA(new_op)->tmp_buffer = buf;
    }
    else
    {
	/* Verify that the buffer is actually allocated by us.  */
	int i;
	for (i = 0; i < list_count; i++)
	    if ((char*)buffer_list[i] < zbmi_shm_rzv ||
		(char*)buffer_list[i] + size_list[i] > zbmi_shm_rzv +
		zbmi_shm_size_total - zbmi_shm_size_eager)
	    {
		return -BMI_EINVAL;
	    }
    }

    if ((ret = send_post_cmd(new_op, 0, &length)) == 1)
    {
	/* Immediate completion.  */
	*total_actual_size = length;

	if (buffer_type == BMI_EXT_ALLOC)
	{
	    /* Copy the memory back to the user buffer(s).  */
	    int j, size_remaining = length;
	    void *buf_cur = METHOD_DATA(new_op)->tmp_buffer;
	    j = 0;
	    while (size_remaining > 0)
	    {
		int tocopy = (new_op->size_list[j] < size_remaining ?
			      new_op->size_list[j] : size_remaining);

		memcpy(new_op->buffer_list[j], buf_cur, tocopy);
		buf_cur += tocopy;
		size_remaining -= tocopy;
		j++;
	    }

	    BMI_zoid_server_memfree(METHOD_DATA(new_op)->tmp_buffer);
	}

	bmi_dealloc_method_op(new_op);
    }

    return ret;
}

/* A common test routine used for all expected messages.  "incount" is 0
   for testcontext; in that case "id_array" is an output argument.  */
int
zoid_server_test_common(int incount, bmi_op_id_t* id_array,
			int outcount_max, int* outcount, int* index_array,
			bmi_error_code_t* error_code_array,
			bmi_size_t* actual_size_array,
			void** user_ptr_array, int max_idle_time_ms,
			bmi_context_id context_id)
{
    mqd_t reply_queue;
    int queue_id;
    char cmd_buffer[ZBMI_MAX_MSG_SIZE], resp_buffer[ZBMI_MAX_MSG_SIZE];
    struct ZBMIControlTestCmd* cmd;
    int cmd_len;
    struct ZBMIControlTestResp* resp;
    int i, index;
    int outcount_used = 0; /* Counter of already used output entries.  */
    int incount_fwd = incount; /* Counter of how many input entries to
				  forward to the ZBMI plugin.  */

    /* We start by checking if there are any local failed/canceled operations
       and taking care of those first.  */
    gen_mutex_lock(&error_ops_mutex);
    if (incount)
    {
	for (i = 0; i < incount; i++)
	{
	    method_op_p op = (method_op_p)id_gen_fast_lookup(id_array[i]);

	    if (op->error_code)
	    {
		if (outcount_used >= outcount_max)
		    break;

		if (index_array)
		    index_array[outcount_used] = i;
		else
		    assert(outcount_used == i);
		error_code_array[i] = op->error_code;
		actual_size_array[i] = 0;
		if (user_ptr_array)
		    user_ptr_array[i] = op->user_ptr;
		outcount_used++;

		op_list_remove(op);
		bmi_dealloc_method_op(op);
	    }
	}
    }
    else
    {
	/* Testcontext.  */
	method_op_p op, tmp;

	qlist_for_each_entry_safe(op, tmp, error_ops, op_list_entry)
	{
	    if (outcount_used >= outcount_max)
		break;

	    id_array[outcount_used] = op->op_id;
	    error_code_array[outcount_used] = op->error_code;
	    actual_size_array[outcount_used] = 0;
	    if (user_ptr_array)
		user_ptr_array[outcount_used] = op->user_ptr;
	    outcount_used++;

	    op_list_remove(op);
	    bmi_dealloc_method_op(op);
	}
    }
    gen_mutex_unlock(&error_ops_mutex);

    if (outcount_used > 0)
    {
	/* Return immediately if there is something to return.  */
	*outcount = outcount_used;
	return 0;
    }
#if 0 /* Commented out so that testcontext won't return 0 if there are ready
	 expected rendezvous messages.  */
    else if (!incount)
    {
	/* Testcontext.  Check in the shared memory region for ready
	   unexpected messages and return immediately if any.  */
	struct SharedMessageDescriptor* msg_desc;

	sem_wait(&eager_desc->unexp_queue_sem);

	for (msg_desc = (struct SharedMessageDescriptor*)
		 (eager_desc->unexp_queue_first + zbmi_shm_eager);
	     msg_desc != (struct SharedMessageDescriptor*)zbmi_shm_eager;
	     msg_desc = (struct SharedMessageDescriptor*)
		 (msg_desc->next + zbmi_shm_eager))
	{
	    if (msg_desc->message_type == MSG_TYPE_SEND)
		break;
	}

	sem_post(&eager_desc->unexp_queue_sem);

	if (msg_desc != (struct SharedMessageDescriptor*)zbmi_shm_eager)
	{
	    *outcount = 0;
	    return 0;
	}
    }
#endif

    if ((reply_queue = get_zoid_reply_queue(&queue_id)) < 0)
	return reply_queue;

    cmd_len = offsetof(typeof(*cmd), zoid_ids) +
	incount_fwd * sizeof(cmd->zoid_ids[0]);
    if (cmd_len > ZBMI_MAX_MSG_SIZE)
	return -BMI_EINVAL;

    cmd = (struct ZBMIControlTestCmd*)cmd_buffer;
    cmd->command_id = ZBMI_CONTROL_TEST;
    cmd->queue_id = queue_id;
    cmd->timeout_ms = max_idle_time_ms;
    /* incount_fwd == 0 indicates "testcontext".  We still need to communicate
       the max. count of outputs we are prepared to handle.  */
    cmd->count = (incount_fwd ? incount_fwd : -outcount_max);
    for (i = 0; i < incount; i++)
    {
	method_op_p op = (method_op_p)id_gen_fast_lookup(id_array[i]);

	cmd->zoid_ids[i] = METHOD_DATA(op)->zoid_buf_id;
    }

    if (mq_send(zbmi_control_queue, (void*)cmd, cmd_len, 0) < 0)
    {
	perror("mq_send");
	release_zoid_reply_queue(queue_id);
	return -BMI_EINVAL;
    }

    if (mq_receive(reply_queue, resp_buffer, sizeof(resp_buffer), NULL) < 0)
    {
	perror("mq_receive");
	release_zoid_reply_queue(queue_id);
	return -BMI_EINVAL;
    }
    resp = (struct ZBMIControlTestResp*)resp_buffer;

    assert(resp->count <= outcount_max);
    *outcount = resp->count;

    for (i = 0, index = 0; i < resp->count; i++, index++)
    {
	method_op_p op = (method_op_p)id_gen_fast_lookup(resp->list[i].
							 bmi_id);
	if (incount_fwd)
	{
	    for (; index < incount_fwd; index++)
	    {
		if (cmd->zoid_ids[index] == METHOD_DATA(op)->zoid_buf_id)
		    break;
	    }
	    assert(index < incount_fwd);

	    if (index_array)
		index_array[i] = index;
	    else
		assert(i == index);
	}
	else /* testcontext */
	{
	    /* Make sure the returned method_op is fully initialized.
	       Only an issue for testcontext, since other test calls
	       require bmi_op_id_t, which implies full initialization.  */
	    gen_mutex_lock(&METHOD_DATA(op)->post_mutex);
	    gen_mutex_unlock(&METHOD_DATA(op)->post_mutex);
	    id_array[i] = resp->list[i].bmi_id;
	}

	if (resp->list[i].length < 0) /* Most likely BMI_ECANCEL */
	{
	    error_code_array[index] = -resp->list[i].length;
	    actual_size_array[index] = 0;
	}
	else
	{
	    actual_size_array[index] = resp->list[i].length;
	    error_code_array[index] = 0;
	}

	if (user_ptr_array)
	    user_ptr_array[index] = op->user_ptr;

	/* We are done with this message.  Clean up.  */
	if (METHOD_DATA(op)->tmp_buffer)
	{
	    if (op->send_recv == BMI_RECV)
	    {
		/* Copy the memory back to the user buffer(s).  */
		int j, size_remaining = resp->list[i].length;
		void *buf_cur = METHOD_DATA(op)->tmp_buffer;
		j = 0;
		while (size_remaining > 0)
		{
		    int tocopy = (op->size_list[j] < size_remaining ?
				  op->size_list[j] : size_remaining);

		    memcpy(op->buffer_list[j], buf_cur, tocopy);
		    buf_cur += tocopy;
		    size_remaining -= tocopy;
		    j++;
		}
	    }

	    BMI_zoid_server_memfree(METHOD_DATA(op)->tmp_buffer);
	}

	bmi_dealloc_method_op(op);
    } /* for (i) */

    release_zoid_reply_queue(queue_id);

    return 0;
}

/* Invoked on BMI_cancel.  */
int
BMI_zoid_server_cancel(bmi_op_id_t id, bmi_context_id context_id)
{
    struct ZBMIControlCancelCmd cmd;
    method_op_p op;

    op = (method_op_p)id_gen_fast_lookup(id);

    /* We have to distinguish here between requests that have been registered
       with the ZBMI plugin (we need to unregister those) and those that
       have not, most likely because of a lack of memory (those can be handled
       locally).  */
    if (!METHOD_DATA(op)->zoid_buf_id)
    {
	gen_mutex_lock(&no_mem_queue_mutex);

	/* Test again, now with mutex properly locked.  */
	if (!METHOD_DATA(op)->zoid_buf_id)
	{
	    if (!op->error_code)
	    {
		/* It must be an out-of-memory case on no_mem_queue.  */
		struct no_mem_descriptor* desc;

		op->error_code = BMI_ECANCEL;

		for (desc = no_mem_queue_first; desc;)
		    if (desc->op == op)
		    {
			if (desc->prev)
			    desc->prev->next = desc->next;
			if (desc->next)
			    desc->next->prev = desc->prev;
			if (no_mem_queue_first == desc)
			    no_mem_queue_first = no_mem_queue_first->next;
			if (no_mem_queue_last == desc)
			    no_mem_queue_last = no_mem_queue_last->prev;
			free(desc);
			break;
		    }
		assert(desc);

		gen_mutex_lock(&error_ops_mutex);
		op_list_add(error_ops, op);
		gen_mutex_unlock(&error_ops_mutex);
	    }

	    gen_mutex_unlock(&no_mem_queue_mutex);

	    return 0;
	}

	gen_mutex_unlock(&no_mem_queue_mutex);
    }

    cmd.command_id = ZBMI_CONTROL_CANCEL;
    cmd.zoid_id = METHOD_DATA(op)->zoid_buf_id;

    if (mq_send(zbmi_control_queue, (void*)&cmd, sizeof(cmd), 0) < 0)
    {
	perror("mq_send");
	return -BMI_EINVAL;
    }

    return 0;
}

/* An internal routine used to obtain a (reply) queue to the ZBMI plugin.  */
static mqd_t
get_zoid_reply_queue(int* queue_id)
{
    int i;

    gen_mutex_lock(&zbmi_queues_mutex);

    for (i = 0; i < zbmi_queues_used; i++)
	if (!zbmi_queues_inuse[i])
	    break;

    if (i == zbmi_queues_used)
    {
	char queue_name[256];
	struct mq_attr attr;

	/* All open queues are currently in use.  Open a new one.  */
	if (zbmi_queues_used == zbmi_queues_len)
	{
	    /* Enlarge the arrays first.  */
	    int j;
	    int* zbmi_queues_new;
	    char* zbmi_queues_inuse_new;

	    if (zbmi_queues_len == 0)
		zbmi_queues_len = ZBMI_QUEUES_LEN_INIT;
	    else
		zbmi_queues_len *= 2;
	    zbmi_queues_new = realloc(zbmi_queues, zbmi_queues_len *
				      sizeof(*zbmi_queues));
	    if (!zbmi_queues_new)
	    {
		gen_mutex_unlock(&zbmi_queues_mutex);
		return -BMI_ENOMEM;
	    }
	    zbmi_queues = zbmi_queues_new;
	    zbmi_queues_inuse_new = realloc(zbmi_queues_inuse,
					    zbmi_queues_len *
					    sizeof(*zbmi_queues_inuse));
	    if (!zbmi_queues_inuse_new)
	    {
		gen_mutex_unlock(&zbmi_queues_mutex);
		return -BMI_ENOMEM;
	    }
	    zbmi_queues_inuse = zbmi_queues_inuse_new;

	    for (j = zbmi_queues_used; j < zbmi_queues_len; j++)
		zbmi_queues_inuse[j] = 0;
	}

	sprintf(queue_name, ZBMI_REPLY_QUEUE_TEMPLATE, i);
	attr.mq_flags = 0;
	attr.mq_maxmsg = 2; /* We never put more than one in there.  */
	attr.mq_msgsize = ZBMI_MAX_MSG_SIZE;
	attr.mq_curmsgs = 0;

	zbmi_queues[i] = mq_open(queue_name, O_RDONLY | O_CREAT | O_EXCL,
				 0666, &attr);
	if (zbmi_queues[i] < 0)
	{
	    if (errno == EEXIST)
	    {
		/* There's no such thing as O_TRUNC for message queues, so
		   we need to do it manually.  */
		mq_unlink(queue_name);
		zbmi_queues[i] = mq_open(queue_name,
					 O_RDONLY | O_CREAT | O_EXCL,
					 0666, &attr);
	    }
	}

	if (zbmi_queues[i] < 0)
	{
	    perror("ZBMI reply queue");
	    gen_mutex_unlock(&zbmi_queues_mutex);
	    return -BMI_EINVAL;
	}

	zbmi_queues_used++;
    }

    zbmi_queues_inuse[i] = 1;

    gen_mutex_unlock(&zbmi_queues_mutex);

    *queue_id = i;

    return zbmi_queues[i];
}

/* Releases the queue obtained with get_zoid_reply_queue.  */
static void
release_zoid_reply_queue(int queue_id)
{
    assert(queue_id >= 0 && queue_id < zbmi_queues_used);

    gen_mutex_lock(&zbmi_queues_mutex);

    assert(zbmi_queues_inuse[queue_id]);
    zbmi_queues_inuse[queue_id] = 0;

    gen_mutex_unlock(&zbmi_queues_mutex);
}

/* Translates a ZOID address to a BMI address, allocating a new one if
   necessary.  */
static bmi_method_addr_p
get_client_addr(int zoid_addr)
{
    bmi_method_addr_p ret;

    assert(zoid_addr >= 0);

    gen_mutex_lock(&clients_mutex);

    if (zoid_addr >= clients_len)
    {
	/* Enlarge the array first.  */

	bmi_method_addr_p* clients_addr_new;
	int i;

	if (!(clients_addr_new = realloc(clients_addr,
					 (zoid_addr + CLIENTS_LEN_INC) *
					 sizeof(*clients_addr))))
	{
	    gen_mutex_unlock(&clients_mutex);
	    return NULL;
	}
	clients_addr = clients_addr_new;

	for (i = clients_len; i < zoid_addr + CLIENTS_LEN_INC; i++)
	    clients_addr[i] = NULL;

	clients_len = zoid_addr + CLIENTS_LEN_INC;
    }

    if (!clients_addr[zoid_addr])
    {
	if ((clients_addr[zoid_addr] =
	     bmi_alloc_method_addr(zoid_method_id, sizeof(struct zoid_addr))))
	{
	    ((struct zoid_addr*)clients_addr[zoid_addr]->method_data)->pid =
		zoid_addr;
	    bmi_method_addr_reg_callback(clients_addr[zoid_addr]);
	}
    }

    ret = clients_addr[zoid_addr];

    gen_mutex_unlock(&clients_mutex);

    return ret;
}

/* Releases a no longer needed client address.  */
void
zoid_server_free_client_addr(bmi_method_addr_p addr)
{
    gen_mutex_lock(&clients_mutex);

    assert(((struct zoid_addr*)addr->method_data)->pid < clients_len &&
	   clients_addr[((struct zoid_addr*)addr->method_data)->pid] == addr);
    clients_addr[((struct zoid_addr*)addr->method_data)->pid] = NULL;

    gen_mutex_unlock(&clients_mutex);
}

/* Puts an out-of-temporary-buffer-memory operation on the "no_mem" list.  */
static int
enqueue_no_mem(method_op_p op, bmi_size_t total_size)
{
    struct no_mem_descriptor *nomemdesc, *desc;

    if (!(nomemdesc = malloc(sizeof(*nomemdesc))))
	return -BMI_ENOMEM;

    nomemdesc->total_size = total_size;
    nomemdesc->op = op;

    /* no_mem_queue is sorted in descending size order.
       Look for an appropriate spot to insert a new entry.  */
    gen_mutex_lock(&no_mem_queue_mutex);

    for (desc = no_mem_queue_first; desc; desc = desc->next)
	if (total_size > desc->total_size)
	    break;

    if (!desc)
    {
	/* Insert at the end (or no_mem_queue is empty).  */
	if (no_mem_queue_first)
	{
	    no_mem_queue_last->next = nomemdesc;
	    nomemdesc->prev = no_mem_queue_last;
	}
	else
	{
	    no_mem_queue_first = nomemdesc;
	    nomemdesc->prev = NULL;
	}

	no_mem_queue_last = nomemdesc;
	nomemdesc->next = NULL;
    }
    else if (desc->prev)
    {
	/* Insert before desc.  */
	nomemdesc->next = desc;
	nomemdesc->prev = desc->prev;
	desc->prev->next = nomemdesc;
	desc->prev = nomemdesc;
    }
    else
    {
	/* Insert as the first element of the queue.  */
	nomemdesc->next = no_mem_queue_first;
	no_mem_queue_first->prev = nomemdesc;

	nomemdesc->prev = NULL;

	no_mem_queue_first = nomemdesc;
    }

    gen_mutex_unlock(&no_mem_queue_mutex);

    return 0;
}

/* A common internal posting routine for send and receive requests.
   "not_immediate" is used for messages triggered from memfree; it is
   inconvenient at that point for messages to succeed immediately (we
   would need a separate queue for them).
   The function returns 0 if posted successfully, 1 for immediate
   completion, and a negative value if failed.
   For immediate completions of receives, the length of the received
   message is stored in *length;
*/
static int
send_post_cmd(method_op_p op, int not_immediate, int* length)
{
    mqd_t reply_queue;
    int queue_id;
    int cmd_len, i;
    char cmd_buffer[ZBMI_MAX_MSG_SIZE];
    struct ZBMIControlPostCmd* cmd;
    struct ZBMIControlPostResp resp;
    int list_count_zoid;

    if ((reply_queue = get_zoid_reply_queue(&queue_id)) < 0)
	return reply_queue;

    list_count_zoid = (METHOD_DATA(op)->tmp_buffer ? 1 : op->list_count);
    cmd_len = offsetof(typeof(*cmd), buf.list) +
	list_count_zoid * sizeof(cmd->buf.list[0]);
    if (cmd_len > ZBMI_MAX_MSG_SIZE)
	return -BMI_EINVAL;

    cmd = (struct ZBMIControlPostCmd*)cmd_buffer;
    cmd->command_id = (op->send_recv == BMI_SEND ? ZBMI_CONTROL_POST_SEND :
	   ZBMI_CONTROL_POST_RECV);
    cmd->queue_id = queue_id;
    cmd->not_immediate = not_immediate;
    cmd->bmi_id = op->op_id;
    cmd->buf.addr = ((struct zoid_addr*)op->addr->method_data)->pid;
    cmd->buf.tag = op->msg_tag;
    cmd->buf.list_count = list_count_zoid;
    if (METHOD_DATA(op)->tmp_buffer)
    {
	cmd->buf.list[0].buffer = (char*)METHOD_DATA(op)->tmp_buffer -
	    zbmi_shm_rzv;
	cmd->buf.list[0].size = (op->send_recv == BMI_SEND ? op->actual_size :
				 op->expected_size);
    }
    else
	for (i = 0; i < op->list_count; i++)
	{
	    cmd->buf.list[i].buffer = (char*)op->buffer_list[i] - zbmi_shm_rzv;
	    cmd->buf.list[i].size = op->size_list[i];
	}

    /* The moment the command is written below, we can get a completion of
       this request on another thread that is waiting in testcontext.  That
       thread will release method_op_p, resulting in us accessing free memory.
       This mutex prevents that.  */
    gen_mutex_init(&METHOD_DATA(op)->post_mutex);
    gen_mutex_lock(&METHOD_DATA(op)->post_mutex);

    if (mq_send(zbmi_control_queue, (void*)cmd, cmd_len, 0) < 0)
    {
	perror("mq_send");
	gen_mutex_unlock(&METHOD_DATA(op)->post_mutex);
	release_zoid_reply_queue(queue_id);
	return -BMI_EINVAL;
    }

    if (mq_receive(reply_queue, (void*)&resp, ZBMI_MAX_MSG_SIZE, NULL) !=
	sizeof(resp))
    {
	perror("mq_receive");
	gen_mutex_unlock(&METHOD_DATA(op)->post_mutex);
	release_zoid_reply_queue(queue_id);
	return -BMI_EINVAL;
    }

    release_zoid_reply_queue(queue_id);

    if (!resp.zoid_id)
    {
	gen_mutex_unlock(&METHOD_DATA(op)->post_mutex);
	return -BMI_ENOMEM;
    }
    else if (resp.zoid_id == -1)
    {
	/* Immediate completion.  */
	assert(!not_immediate);
	gen_mutex_unlock(&METHOD_DATA(op)->post_mutex);
	if (length)
	    *length = resp.length;
	return 1;
    }

    METHOD_DATA(op)->zoid_buf_id = resp.zoid_id;

    gen_mutex_unlock(&METHOD_DATA(op)->post_mutex);

    return 0;
}
