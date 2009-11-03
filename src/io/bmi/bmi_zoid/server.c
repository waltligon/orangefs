/* ZOID implementation of a BMI method -- I/O node server side */

#include <assert.h>
#include <stdio.h>

#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <bmi-method-support.h>
#include <bmi-method-callback.h>
#include <id-generator.h>
#include <op-list.h>

#include "zoid.h"
#include "zbmi_pool.h"
#include "zbmi_protocol.h"

/* method_op_p's method_data points to this structure on the server side.  */
struct ZoidServerMethodData
{
    void* tmp_buffer; /* Used with BMI_EXT_ALLOC to store the address of the
			 temporary shared memory buffer, NULL otherwise.  */
    int zoid_buf_id; /* 0 if the operation has not yet been sent to ZOID.  */
};

/* Describes a request with BMI_EXT_ALLOC pending for a temporary memory
   buffer.  */
struct NoMemDescriptor
{
    struct NoMemDescriptor* next;
    struct NoMemDescriptor* prev;

    bmi_size_t total_size;
    method_op_p op;
};

/* Command streams to the zbmi plugin in the ZOID daemon.  */
#define ZBMI_SOCKETS_LEN_INIT 10
static pthread_mutex_t zbmi_sockets_mutex = PTHREAD_MUTEX_INITIALIZER;
static int* zbmi_sockets = NULL;
static char* zbmi_sockets_inuse = NULL; /* Whether a particular zbmi_sockets
					   entry is currently in use.  */
static int zbmi_sockets_len = 0; /* Length of zbmi_sockets and
				    zbmi_sockets_inuse.  */
static int zbmi_sockets_used = 0; /* Count of initialized zbmi_sockets
				     entries.  */

/* An array of client addresses.  */
#define CLIENTS_LEN_INC 10
static pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
static bmi_method_addr_p* clients_addr = NULL;
static int clients_len = 0;

/* Shared memory buffer between the ZBMI plugin an us.  */
static void* zbmi_shm = NULL;
static void *zbmi_shm_unexp, *zbmi_shm_exp;

static int zbmi_shm_size_total;
static int zbmi_shm_size_unexp;

/* Queue of operations with BMI_EXT_ALLOC buffers pending for a temporary
   memory buffer, sorted by descending total_size.  */
static struct NoMemDescriptor *no_mem_queue_first = NULL;
/* Only valid if no_mem_queue_first != NULL.  */
static struct NoMemDescriptor *no_mem_queue_last;
/* Protects access to the above queue.  */
static pthread_mutex_t no_mem_queue_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Queue of failed/canceled operations (that the ZOID server doesn't know
   about).  */
static op_list_p error_ops;
static pthread_mutex_t error_ops_mutex = PTHREAD_MUTEX_INITIALIZER;


static ssize_t socket_read(int fd, void* buf, size_t count);
static ssize_t socket_write(int fd, const void* buf, size_t count);
static int get_zoid_socket(int* release_token);
static void release_zoid_socket(int release_token);
static bmi_method_addr_p get_client_addr(int zoid_addr);
static int enqueue_no_mem(method_op_p op, bmi_size_t total_size);
static int send_post_cmd(method_op_p op);


/* These symbols come from external libraries that would need to be linked
   even on the client-side, even though the client never actually invokes
   them.  */
typeof(shm_open) shm_open __attribute__((weak));
typeof(shm_unlink) shm_unlink __attribute__((weak));


/* Invoked on BMI_initialize.  */
int
BMI_zoid_server_initialize(void)
{
    int hdr;
    struct ZBMIControlInitResp init_resp;
    int shm_fd;
    int zoid_fd, zoid_release;

    /* Connect to the ZBMI plugin in the ZOID daemon.  This will initialize
       all the socket structures first.  */
    
    if ((zoid_fd = get_zoid_socket(&zoid_release)) < 0)
	return zoid_fd;

    /* Initial handshake.  */

    hdr = ZBMI_CONTROL_INIT;

    if (socket_write(zoid_fd, &hdr, sizeof(hdr)) != sizeof(hdr))
    {
	perror("write");
	release_zoid_socket(zoid_release);
	return -BMI_EINVAL;
    }

    if (socket_read(zoid_fd, &init_resp, sizeof(init_resp)) !=
	sizeof(init_resp))
    {
	perror("read");
	release_zoid_socket(zoid_release);
	return -BMI_EINVAL;
    }

    release_zoid_socket(zoid_release);

    zbmi_shm_size_total = init_resp.shm_size_total;
    zbmi_shm_size_unexp = init_resp.shm_size_unexp;

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

    /* The shared memory buffer starts with an unexpected section, which is
       managed by the ZBMI ZOID plugin.  */
    zbmi_shm_unexp = zbmi_shm;
    /* The expected buffers part is initialized and managed by us.  */
    zbmi_shm_exp = zbmi_shm + zbmi_shm_size_unexp;
    zbmi_pool_init(zbmi_shm_exp, zbmi_shm_size_total - zbmi_shm_size_unexp);

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
    for (i = 0; i < zbmi_sockets_used; i++)
	close(zbmi_sockets[i]);
    free(zbmi_sockets_inuse);
    free(zbmi_sockets);
    zbmi_sockets_len = zbmi_sockets_used = 0;

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
    struct NoMemDescriptor* desc;

    zbmi_pool_free(buffer);

    /* Once some memory has been freed, go over the queue of requests waiting
       for memory and try to satisfy any of them.  */
    pthread_mutex_lock(&no_mem_queue_mutex);

    for (desc = no_mem_queue_first; desc;)
    {
	struct NoMemDescriptor* desc_next = desc->next;
	void* buf;

	if ((buf = BMI_zoid_server_memalloc(desc->total_size)))
	{
	    method_op_p op = desc->op;

	    ((struct ZoidServerMethodData*)op->method_data)->tmp_buffer = buf;

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

	    if ((op->error_code = -send_post_cmd(op)))
	    {
		pthread_mutex_lock(&error_ops_mutex);
		op_list_add(error_ops, op);
		pthread_mutex_unlock(&error_ops_mutex);
	    }
	}

	desc = desc_next;
    }

    pthread_mutex_unlock(&no_mem_queue_mutex);
}

/* Invoked on BMI_unexpected_free.  */
int
BMI_zoid_server_unexpected_free(void* buffer)
{
    int zoid_fd, zoid_release;
    int hdr;
    struct ZBMIControlUnexpFreeCmd cmd;

    if (buffer < zbmi_shm_unexp ||
	buffer >= zbmi_shm_unexp + zbmi_shm_size_unexp)
	return -BMI_EINVAL;

    if ((zoid_fd = get_zoid_socket(&zoid_release)) < 0)
	return zoid_fd;

    hdr = ZBMI_CONTROL_UNEXP_FREE;
    cmd.buffer = buffer - zbmi_shm_unexp;

    if (socket_write(zoid_fd, &hdr, sizeof(hdr)) != sizeof(hdr) ||
	socket_write(zoid_fd, &cmd, sizeof(cmd)) != sizeof(cmd))
    {
	perror("write");
	release_zoid_socket(zoid_release);
	return -BMI_EINVAL;
    }

    release_zoid_socket(zoid_release);
    return 0;
}

/* Invoked on BMI_testunexpected.  */
int
BMI_zoid_server_testunexpected(int incount, int* outcount,
			       struct bmi_method_unexpected_info* info,
			       int max_idle_time_ms)
{
    int zoid_fd, zoid_release;
    int hdr;
    int i;
    struct ZBMIControlUnexpTestCmd cmd;
    struct ZBMIControlUnexpTestResp resp;
    struct ZBMIControlBufDesc* buf_descs = NULL;

    if ((zoid_fd = get_zoid_socket(&zoid_release)) < 0)
	return zoid_fd;

    hdr = ZBMI_CONTROL_UNEXP_TEST;
    cmd.incount = incount;
    cmd.max_idle_time_ms = max_idle_time_ms;

    if (socket_write(zoid_fd, &hdr, sizeof(hdr)) != sizeof(hdr) ||
	socket_write(zoid_fd, &cmd, sizeof(cmd)) != sizeof(cmd))
    {
	perror("write");
	release_zoid_socket(zoid_release);
	return -BMI_EINVAL;
    }

    if (socket_read(zoid_fd, &resp, offsetof(typeof(resp), buffers)) !=
	offsetof(typeof(resp), buffers))
    {
	perror("read");
	release_zoid_socket(zoid_release);
	return -BMI_EINVAL;
    }
    if (resp.outcount_bytes > 0)
    {
	buf_descs = alloca(resp.outcount_bytes);

	if (socket_read(zoid_fd, buf_descs, resp.outcount_bytes) !=
	    resp.outcount_bytes)
	{
	    perror("read");
	    release_zoid_socket(zoid_release);
	    return -BMI_EINVAL;
	}
    }

    release_zoid_socket(zoid_release);

    *outcount = resp.outcount;
    for (i = 0; i < resp.outcount; i++)
    {
	info[i].error_code = 0;

	if (!(info[i].addr = get_client_addr(buf_descs->addr)))
	    return -BMI_ENOMEM;

	if (buf_descs->list_count != 1)
	    return -BMI_EINVAL;

	info[i].buffer = zbmi_shm_unexp + buf_descs->list[0].buffer;
	info[i].size = buf_descs->list[0].size;
	info[i].tag = buf_descs->tag;

	buf_descs = (struct ZBMIControlBufDesc*)
	    (((char*)buf_descs) + offsetof(typeof(*buf_descs), list) +
	     buf_descs->list_count * sizeof(buf_descs->list[0]));
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

    /* Server-side sends are never immediate, so we start by allocating a
       method op.  */

    if (!(new_op = bmi_alloc_method_op(sizeof(struct ZoidServerMethodData))))
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
    ((struct ZoidServerMethodData*)new_op->method_data)->tmp_buffer = NULL;
    ((struct ZoidServerMethodData*)new_op->method_data)->zoid_buf_id = 0;

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

	((struct ZoidServerMethodData*)new_op->method_data)->tmp_buffer = buf;

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
	    if (buffer_list[i] < zbmi_shm_exp ||
		buffer_list[i] + size_list[i] > zbmi_shm_exp +
		zbmi_shm_size_total - zbmi_shm_size_unexp)
	    {
		return -BMI_EINVAL;
	    }
    }

    return send_post_cmd(new_op);
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

    /* Server-side receives are never immediate, so we start by allocating a
       method op.  */

    if (!(new_op = bmi_alloc_method_op(sizeof(struct ZoidServerMethodData))))
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
    ((struct ZoidServerMethodData*)new_op->method_data)->tmp_buffer = NULL;
    ((struct ZoidServerMethodData*)new_op->method_data)->zoid_buf_id = 0;

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

	((struct ZoidServerMethodData*)new_op->method_data)->tmp_buffer = buf;
    }
    else
    {
	/* Verify that the buffer is actually allocated by us.  */
	int i;
	for (i = 0; i < list_count; i++)
	    if (buffer_list[i] < zbmi_shm_exp ||
		buffer_list[i] + size_list[i] > zbmi_shm_exp +
		zbmi_shm_size_total - zbmi_shm_size_unexp)
	    {
		return -BMI_EINVAL;
	    }
    }

    return send_post_cmd(new_op);
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
    int zoid_fd, zoid_release;
    int hdr;
    struct ZBMIControlTestCmd* cmd;
    int cmd_len;
    struct ZBMIControlTestResp resp;
    int i;
    int outcount_used = 0; /* Counter of already used output entries.  */
    int incount_fwd = incount; /* Counter of how many input entries to
				  forward to the ZBMI plugin.  */

    /* We start by checking if there are any local failed/canceled operations
       and taking care of those first.  */
    pthread_mutex_lock(&error_ops_mutex);
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
		/* Note: we will dealloc a little later.  */
	    }
	}

	if (outcount_used > 0)
	{
	    incount_fwd = incount - outcount_used;
	    if (index_array)
		index_array += outcount_used;
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

	if (outcount_used > 0)
	{
	    id_array += outcount_used;
	    error_code_array += outcount_used;
	    actual_size_array += outcount_used;
	    user_ptr_array += outcount_used;
	}
    }
    pthread_mutex_unlock(&error_ops_mutex);

    hdr = ZBMI_CONTROL_TEST;
    cmd_len = offsetof(typeof(*cmd), zoid_ids) +
	incount_fwd * sizeof(cmd->zoid_ids[0]);
    cmd = alloca(cmd_len);

    cmd->timeout_ms = max_idle_time_ms;

    /* incount_fwd == 0 indicates "testcontext".  We still need to communicate
       the max. count of outputs we are prepared to handle.  */
    cmd->count = (incount_fwd ? incount_fwd : -outcount_max);
    for (i = 0; i < incount; i++)
    {
	method_op_p op = (method_op_p)id_gen_fast_lookup(id_array[i]);
	if (op->error_code)
	{
	    bmi_dealloc_method_op(op);
	    continue;
	}
	cmd->zoid_ids[i] = ((struct ZoidServerMethodData*)op->method_data)->
	    zoid_buf_id;
    }

    if (outcount_used > 0)
    {
	outcount_max -= outcount_used;
	if (outcount_max == 0 || (incount > 0 && incount_fwd == 0))
	{
	    *outcount = outcount_used;
	    return 0;
	}
    }

    /* Note: this is shifted later than usual in the function body so that
       we can invoke bmi_dealloc_method_op above as appropriate.  */
    if ((zoid_fd = get_zoid_socket(&zoid_release)) < 0)
	return zoid_fd;

    if (socket_write(zoid_fd, &hdr, sizeof(hdr)) != sizeof(hdr) ||
	socket_write(zoid_fd, cmd, cmd_len) != cmd_len)
    {
	perror("write");
	release_zoid_socket(zoid_release);
	return -BMI_EINVAL;
    }

    if (socket_read(zoid_fd, &resp, offsetof(typeof(resp), list)) !=
	offsetof(typeof(resp), list))
    {
	perror("read");
	release_zoid_socket(zoid_release);
	return -BMI_EINVAL;
    }
    assert(resp.count <= outcount_max);
    *outcount = resp.count;
    if (resp.count > 0)
    {
	struct ZBMIControlTestRespList* resp_list;
	int index;

	resp_list = alloca(resp.count * sizeof(*resp_list));

	if (socket_read(zoid_fd, resp_list, resp.count * sizeof(*resp_list)) !=
	    resp.count * sizeof(*resp_list))
	{
	    perror("read");
	    release_zoid_socket(zoid_release);
	    return -BMI_EINVAL;
	}

	for (i = 0, index = 0; i < resp.count; i++, index++)
	{
	    method_op_p op = (method_op_p)id_gen_fast_lookup(resp_list[i].
							     bmi_id);
	    if (incount_fwd)
	    {
		for (; index < incount_fwd; index++)
		{
		    if (cmd->zoid_ids[index] == ((struct ZoidServerMethodData*)
						 op->method_data)->zoid_buf_id)
			break;
		}
		assert(index < incount_fwd);

		if (index_array)
		    index_array[i] = index;
		else
		    assert(i == index);
	    }
	    else /* testcontext */
		id_array[i] = resp_list[i].bmi_id;

	    if (resp_list[i].length < 0) /* Most likely BMI_ECANCEL */
	    {
		error_code_array[index] = -resp_list[i].length;
		actual_size_array[index] = 0;
	    }
	    else
	    {
		actual_size_array[index] = resp_list[i].length;
		error_code_array[index] = 0;
	    }

	    if (user_ptr_array)
		user_ptr_array[index] = op->user_ptr;

	    /* We are done with this message.  Clean up.  */
	    if (((struct ZoidServerMethodData*)op->method_data)->tmp_buffer)
	    {
		if (op->send_recv == BMI_RECV)
		{
		    /* Copy the memory back to the user buffer(s).  */
		    int j, size_remaining = resp_list[i].length;
		    void *buf_cur = ((struct ZoidServerMethodData*)op->
				     method_data)->tmp_buffer;
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

		BMI_zoid_server_memfree(((struct ZoidServerMethodData*)op->
					 method_data)->tmp_buffer);
	    }

	    bmi_dealloc_method_op(op);
	} /* for (i) */
    } /* if (resp.count > 0) */

    release_zoid_socket(zoid_release);

    return 0;
}

/* Invoked on BMI_cancel.  */
int
BMI_zoid_server_cancel(bmi_op_id_t id, bmi_context_id context_id)
{
    int zoid_fd, zoid_release;
    int hdr;
    struct ZBMIControlCancelCmd cmd;
    method_op_p op;

    op = (method_op_p)id_gen_fast_lookup(id);

    /* We have to distinguish here between requests that have been registered
       with the ZBMI plugin (we need to unregister those) and those that
       have not, most likely because of a lack of memory (those can be handled
       locally).  */
    if (!((struct ZoidServerMethodData*)op->method_data)->zoid_buf_id)
    {
	pthread_mutex_lock(&no_mem_queue_mutex);

	/* Test again, now with mutex properly locked.  */
	if (!((struct ZoidServerMethodData*)op->method_data)->zoid_buf_id)
	{
	    if (!op->error_code)
	    {
		/* It must be an out-of-memory case on no_mem_queue.  */
		struct NoMemDescriptor* desc;

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

		pthread_mutex_lock(&error_ops_mutex);
		op_list_add(error_ops, op);
		pthread_mutex_unlock(&error_ops_mutex);
	    }

	    pthread_mutex_unlock(&no_mem_queue_mutex);

	    return 0;
	}

	pthread_mutex_unlock(&no_mem_queue_mutex);
    }

    if ((zoid_fd = get_zoid_socket(&zoid_release)) < 0)
	return zoid_fd;

    hdr = ZBMI_CONTROL_CANCEL;
    cmd.zoid_id = ((struct ZoidServerMethodData*)op->method_data)->
	    zoid_buf_id;

    if (socket_write(zoid_fd, &hdr, sizeof(hdr)) != sizeof(hdr) ||
	socket_write(zoid_fd, &cmd, sizeof(cmd)) != sizeof(cmd))
    {
	perror("write");
	release_zoid_socket(zoid_release);
	return -BMI_EINVAL;
    }

    return 0;
}

/*
 * A more robust version of read(2).
 */
static ssize_t
socket_read(int fd, void* buf, size_t count)
{
    size_t already_read = 0;

    while (already_read < count)
    {
	ssize_t n;

	n = read(fd, buf + already_read, count - already_read);

	if (n == -1)
	{
	    if (errno == EINTR || errno == EAGAIN)
		continue;
	    return -1;
	}
	else if (n == 0)
	    return already_read;
	else
	    already_read += n;
    }

    return already_read;
}

/*
 * A more robust version of write(2).
 */
static ssize_t
socket_write(int fd, const void* buf, size_t count)
{
    size_t already_written = 0;

    while (already_written < count)
    {
	ssize_t n;

	n = write(fd, buf + already_written, count - already_written);

	if (n == -1)
	{
	    if (errno == EINTR || errno == EAGAIN)
		continue;
	    return -1;
	}
	else
	    already_written += n;
    }

    return already_written;
}

/* An internal routine used to obtain a socket to the ZBMI plugin.  */
static int
get_zoid_socket(int* release_token)
{
    int i;

    pthread_mutex_lock(&zbmi_sockets_mutex);

    for (i = 0; i < zbmi_sockets_used; i++)
	if (!zbmi_sockets_inuse[i])
	    break;

    if (i == zbmi_sockets_used)
    {
	/* All open sockets are currently in use.  Open a new one.  */
	struct sockaddr_un addr;

	if (zbmi_sockets_used == zbmi_sockets_len)
	{
	    /* Enlarge the arrays first.  */
	    int j;
	    int* zbmi_sockets_new;
	    char* zbmi_sockets_inuse_new;

	    if (zbmi_sockets_len == 0)
		zbmi_sockets_len = ZBMI_SOCKETS_LEN_INIT;
	    else
		zbmi_sockets_len *= 2;
	    zbmi_sockets_new = realloc(zbmi_sockets, zbmi_sockets_len *
				       sizeof(*zbmi_sockets));
	    if (!zbmi_sockets_new)
	    {
		pthread_mutex_unlock(&zbmi_sockets_mutex);
		return -BMI_ENOMEM;
	    }
	    zbmi_sockets = zbmi_sockets_new;
	    zbmi_sockets_inuse_new = realloc(zbmi_sockets_inuse,
					     zbmi_sockets_len *
					     sizeof(*zbmi_sockets_inuse));
	    if (!zbmi_sockets_inuse_new)
	    {
		pthread_mutex_unlock(&zbmi_sockets_mutex);
		return -BMI_ENOMEM;
	    }
	    zbmi_sockets_inuse = zbmi_sockets_inuse_new;

	    for (j = zbmi_sockets_used; j < zbmi_sockets_len; j++)
		zbmi_sockets_inuse[j] = 0;
	}

	if ((zbmi_sockets[i] = socket(PF_UNIX, SOCK_STREAM, 0)) < 0)
	{
	    perror("ZBMI control socket");
	    pthread_mutex_unlock(&zbmi_sockets_mutex);
	    return -BMI_EINVAL;
	}

	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, ZBMI_SOCKET_NAME);
#if 0
	if (bind(zbmi_sockets[i], (struct sockaddr*)&addr, sizeof(addr)) < 0)
	{
	    perror("bind " ZBMI_SOCKET_NAME);
	    close(zbmi_sockets[i]);
	    pthread_mutex_unlock(&zbmi_sockets_mutex);
	    return -BMI_EINVAL;
	}
#endif
	while (connect(zbmi_sockets[i], (struct sockaddr*)&addr, sizeof(addr))
	       < 0)
	{
	    if (errno == ENOENT || errno == ECONNREFUSED)
	    {
		/* ZOID server not running yet or too many requests?  Wait
		   a little...  */
		sleep(1);
	    }
	    else
	    {
		perror("connect to ZOID");
		close(zbmi_sockets[i]);
		pthread_mutex_unlock(&zbmi_sockets_mutex);
		return -BMI_EINVAL;
	    }
	}

	zbmi_sockets_used++;
    }

    zbmi_sockets_inuse[i] = 1;

    pthread_mutex_unlock(&zbmi_sockets_mutex);

    *release_token = i;

    return zbmi_sockets[i];
}

/* Releases the socket obtained with get_zoid_socket.  */
static void
release_zoid_socket(int release_token)
{
    assert(release_token >= 0 && release_token < zbmi_sockets_used);

    pthread_mutex_lock(&zbmi_sockets_mutex);

    assert(zbmi_sockets_inuse[release_token]);
    zbmi_sockets_inuse[release_token] = 0;

    pthread_mutex_unlock(&zbmi_sockets_mutex);
}

/* Translates a ZOID address to a BMI address, allocating a new one if
   necessary.  */
static bmi_method_addr_p
get_client_addr(int zoid_addr)
{
    bmi_method_addr_p ret;

    assert(zoid_addr >= 0);

    pthread_mutex_lock(&clients_mutex);

    if (zoid_addr >= clients_len)
    {
	/* Enlarge the array first.  */

	bmi_method_addr_p* clients_addr_new;
	int i;

	if (!(clients_addr_new = realloc(clients_addr,
					 (zoid_addr + CLIENTS_LEN_INC) *
					 sizeof(*clients_addr))))
	{
	    pthread_mutex_unlock(&clients_mutex);
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

    pthread_mutex_unlock(&clients_mutex);

    return ret;
}

/* Releases a no longer needed client address.  */
void
zoid_server_free_client_addr(bmi_method_addr_p addr)
{
    pthread_mutex_lock(&clients_mutex);

    assert(((struct zoid_addr*)addr->method_data)->pid < clients_len &&
	   clients_addr[((struct zoid_addr*)addr->method_data)->pid] == addr);
    clients_addr[((struct zoid_addr*)addr->method_data)->pid] = NULL;

    pthread_mutex_unlock(&clients_mutex);
}

/* Puts an out-of-temporary-buffer-memory operation on the "no_mem" list.  */
static int
enqueue_no_mem(method_op_p op, bmi_size_t total_size)
{
    struct NoMemDescriptor *nomemdesc, *desc;

    if (!(nomemdesc = malloc(sizeof(*nomemdesc))))
	return -BMI_ENOMEM;

    nomemdesc->total_size = total_size;
    nomemdesc->op = op;

    /* no_mem_queue is sorted in descending size order.
       Look for an appropriate spot to insert a new entry.  */
    pthread_mutex_lock(&no_mem_queue_mutex);

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

    pthread_mutex_unlock(&no_mem_queue_mutex);

    return 0;
}

/* A common internal posting routine for send and receive requests.  */
static int
send_post_cmd(method_op_p op)
{
    int zoid_fd, zoid_release;
    int cmd_len, i;
    int hdr;
    struct ZBMIControlPostCmd* cmd;
    struct ZBMIControlPostResp resp;
    int list_count_zoid;

    if ((zoid_fd = get_zoid_socket(&zoid_release)) < 0)
	return zoid_fd;

    hdr = (op->send_recv == BMI_SEND ? ZBMI_CONTROL_POST_SEND :
	   ZBMI_CONTROL_POST_RECV);
    list_count_zoid = (((struct ZoidServerMethodData*)op->method_data)->
		       tmp_buffer ? 1 : op->list_count);
    cmd_len = offsetof(typeof(*cmd), buf.list) +
	list_count_zoid * sizeof(cmd->buf.list[0]);
    cmd = alloca(cmd_len);
    cmd->bmi_id = op->op_id;
    cmd->buf.addr = ((struct zoid_addr*)op->addr->method_data)->pid;
    cmd->buf.tag = op->msg_tag;
    cmd->buf.list_count = list_count_zoid;
    if (((struct ZoidServerMethodData*)op->method_data)->tmp_buffer)
    {
	cmd->buf.list[0].buffer = ((struct ZoidServerMethodData*)op->
				   method_data)->tmp_buffer - zbmi_shm_exp;
	cmd->buf.list[0].size = (op->send_recv == BMI_SEND ? op->actual_size :
				 op->expected_size);
    }
    else
	for (i = 0; i < op->list_count; i++)
	{
	    cmd->buf.list[i].buffer = op->buffer_list[i] - zbmi_shm_exp;
	    cmd->buf.list[i].size = op->size_list[i];
	}

    if (socket_write(zoid_fd, &hdr, sizeof(hdr)) != sizeof(hdr) ||
	socket_write(zoid_fd, cmd, cmd_len) != cmd_len)
    {
	perror("write");
	release_zoid_socket(zoid_release);
	return -BMI_EINVAL;
    }

    if (socket_read(zoid_fd, &resp, sizeof(resp)) != sizeof(resp))
    {
	perror("read");
	release_zoid_socket(zoid_release);
	return -BMI_EINVAL;
    }

    release_zoid_socket(zoid_release);

    if (!resp.zoid_id)
	return -BMI_ENOMEM;

    ((struct ZoidServerMethodData*)op->method_data)->zoid_buf_id = resp.zoid_id;

    return 0;
}
