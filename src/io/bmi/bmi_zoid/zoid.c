/* ZOID implementation of a BMI method */

/*
  Limitations:

  - unexpected messages limited to 8K (easy to increase)
  - expected messages limited to 128M (easy to increase, but even the current
  value is not safe given the memory consideration on IONs)
  - compute nodes can only communicate with the ION, not with each other
  - unexpected messages can only be sent by a CN and can only be received
  by an ION
  - only one string address is supported, "zoid://", which denotes the server
  - only one, global context is supported
  - multithreading is not supported on the compute node client side
*/

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <bmi-method-support.h>
#include <id-generator.h>
#include <op-list.h>

#include "zbmi.h"
#include "zoid_api.h"
#include "zoid.h"

#define CLIENT 0
#define SERVER 1

/* [Compute node] CLIENT or [I/O node] SERVER */
static int zoid_node_type;

/* As passed to the initialize routine.  */
int zoid_method_id;

/* A queue of all non-immediate posted message sends/receives.  Only used
   on the client side.  We don't allocate a custom method data for the
   entries; rather, we reuse the method_data pointer itself to indicate if
   we are dealing with an unexpected message.  */
static op_list_p zoid_ops;

/* As a special feature, client-side ZOID method allows post timeouts.
   If non-zero this enables combined post & test in order to reduce the
   number of round-trip messages to the server.  */
static int post_timeout = 0;


static int zoid_err_to_bmi(int err);

/* A common send routine used for both expected and unexpected messages.  */
static int
zoid_post_send_common(bmi_op_id_t* id, bmi_method_addr_p dest,
		      const void*const* buffer_list,
		      const bmi_size_t* size_list, int list_count,
		      bmi_size_t total_size, enum bmi_buffer_type buffer_type,
		      bmi_msg_tag_t tag, uint8_t class, void* user_ptr,
		      bmi_context_id context_id, PVFS_hint hints,
		      int unexpected)
{
    int ret, err;
    method_op_p new_op;

    if (unexpected)
    {
	/* We only support unexpected messages from clients.  */
	if (zoid_node_type == SERVER)
	    abort();
	
	if (total_size > ZOID_MAX_UNEXPECTED_MSG)
	    return -BMI_EMSGSIZE;
    }
    else /* expected */
    {
	if (total_size > ZOID_MAX_EXPECTED_MSG)
	    return -BMI_EMSGSIZE;
    }

    if (zoid_node_type == CLIENT)
    {
	size_t* size_list_cp;

	/* We only support communication for the compute nodes to the I/O
	   node server.  */
	assert(((struct zoid_addr*)dest->method_data)->pid ==
	       ZOID_ADDR_SERVER_PID);

	/* In principle, for expected messages we should start with a
	   handshake (post_test).  However, because of how input userbuf is
	   implemented in ZOID, there is an implicit ZOID handshake that we
	   can take advantage of, so we attempt to send immediately.  */

	/* bmi_size_t is 64-bit, which is not supported by ZOID.  */
	size_list_cp = alloca(list_count * sizeof(*size_list_cp));
	{
	    int i;
	    for (i = 0; i < list_count; i++)
		size_list_cp[i] = size_list[i];
	}

	if (total_size <= ZOID_MAX_EAGER_MSG)
	    ret = zbmi_send_eager((const void**)buffer_list, size_list_cp,
				  list_count, tag, class, unexpected,
				  post_timeout);
	else
	    ret = zbmi_send(buffer_list, size_list_cp, list_count, tag, class,
			    unexpected, post_timeout);
	err = __zoid_error();
	if (total_size <= ZOID_MAX_EAGER_MSG ?
	    (!err && ret == 0) : (err == ENOMEM))
	{
	    /* Indicates that there was no memory on the server side
	       for the message.  Could happen if this is an expected
	       message and no matching receive has been posted, or if
	       we sent too many unexpected messages without the server-side
	       receiving anything.  */

	    if (!(new_op = bmi_alloc_method_op(0)))
		return -BMI_ENOMEM;
	    *id = new_op->op_id;
	    new_op->addr = dest;
	    new_op->send_recv = BMI_SEND;
	    new_op->user_ptr = user_ptr;
	    new_op->msg_tag = tag;
	    new_op->class = class;
	    new_op->list_count = list_count;
	    new_op->actual_size = total_size;
	    if (list_count == 1)
	    {
		/* Our buffer_list and size_list pointers might be
		   temporary (see, e.g., BMI_zoid_post_send), so we
		   prefer to copy the data over to someplace more
		   permanent.  */
		new_op->buffer = (void*)buffer_list[0];
		new_op->buffer_list = &new_op->buffer;
		new_op->size_list = &new_op->actual_size;
	    }
	    else
	    {
		new_op->buffer_list = (void*const*)buffer_list;
		new_op->size_list = size_list;
	    }
	    new_op->error_code = 0;
	    new_op->method_data = (void*)unexpected;

	    op_list_add(zoid_ops, new_op);

	    return 0; /* Non-immediate completion.  */
	}
	else if (err)
	    return zoid_err_to_bmi(err);

	return 1; /* Immediate completion.  */
    }

    /* Server code.  */

    return zoid_server_send_common(id, dest, buffer_list, size_list,
				   list_count, total_size, buffer_type,
				   tag, user_ptr, context_id, hints);
}

/* A common send routine used for all expected messages.  */
static int
zoid_post_recv_common(bmi_op_id_t* id, bmi_method_addr_p src,
		      void *const* buffer_list, const bmi_size_t* size_list,
		      int list_count, bmi_size_t total_expected_size,
		      bmi_size_t* total_actual_size,
		      enum bmi_buffer_type buffer_type, bmi_msg_tag_t tag,
		      void* user_ptr, bmi_context_id context_id,
		      PVFS_hint hints)
{
    int ret, err;
    method_op_p new_op;

    if (total_expected_size > ZOID_MAX_EXPECTED_MSG)
	return -BMI_EMSGSIZE;

    if (zoid_node_type == CLIENT)
    {
	size_t* size_list_cp;

	/* We only support communication for the compute nodes to the I/O
	   node server.  */
	assert(((struct zoid_addr*)src->method_data)->pid ==
	       ZOID_ADDR_SERVER_PID);

	/* In principle, for expected messages we should start with a
	   handshake (post_test).  However, we can take a shortcut and
	   immediately try a receive instead, which will first do a test
	   on the server side.  */

	/* Try immediate completion first.  */

	size_list_cp = alloca(list_count * sizeof(*size_list_cp));
	{
	    int i;
	    for (i = 0; i < list_count; i++)
		size_list_cp[i] = size_list[i];
	}

	ret = zbmi_recv(buffer_list, size_list_cp, list_count, tag,
			post_timeout);
	if ((err = __zoid_error()))
	    return zoid_err_to_bmi(err);

	if (ret == 1) /* Immediate completion succeeded!  */
	{
	    int i;

	    *total_actual_size = 0;
	    for (i = 0; i < list_count; i++)
		*total_actual_size += size_list_cp[i];

	    return 1;
	}
	else if (ret < 0)
	{
	    /* Error.  */
	    return -BMI_EPROTO;
	}

	/* No matching send.  Queue the message.  */

	if (!(new_op = bmi_alloc_method_op(0)))
	    return -BMI_ENOMEM;
	*id = new_op->op_id;
	new_op->addr = src;
	new_op->send_recv = BMI_RECV;
	new_op->user_ptr = user_ptr;
	new_op->msg_tag = tag;
	new_op->class = 0;
	new_op->list_count = list_count;
	new_op->expected_size = total_expected_size;
	if (list_count == 1)
	{
	    /* See zoid_post_send_common for an explanation.  */
	    new_op->buffer = buffer_list[0];
	    new_op->buffer_list = &new_op->buffer;
	    new_op->size_list = &new_op->expected_size;
	}
	else
	{
	    new_op->buffer_list = buffer_list;
	    new_op->size_list = size_list;
	}
	new_op->error_code = 0;
	new_op->method_data = (void*)0;

	op_list_add(zoid_ops, new_op);

	return 0; /* Non-immediate completion.  */
    }

    /* Server code.  */

    return zoid_server_recv_common(id, src, buffer_list, size_list, list_count,
				   total_expected_size, total_actual_size,
				   buffer_type, tag, user_ptr, context_id,
				   hints);
}

/* A common test routine used for all pending messages.  */
static int
zoid_test_common(int incount, bmi_op_id_t* id_array, int outcount_max,
		 int* outcount, int* index_array,
		 bmi_error_code_t* error_code_array,
		 bmi_size_t* actual_size_array, void** user_ptr_array,
		 int max_idle_time_ms, bmi_context_id context_id)
{
    int i, ret, err, out;
#if 0
    fprintf(stderr, "zoid_test_common incount %d, outcount_max %d, timeout %d\n", incount, outcount_max, max_idle_time_ms);
    for (i = 0; i < incount; i++)
	fprintf(stderr, "id[%d]: %lld\n", i, id_array[i]);
#endif
    if (zoid_node_type == CLIENT)
    {
	bmi_msg_tag_t* tags = alloca(incount * sizeof(*tags));
	enum bmi_op_type* ops = alloca(incount * sizeof(*ops));
	ssize_t* unexp_sizes = alloca(incount * sizeof(*unexp_sizes));
	int* ready = alloca(incount * sizeof(*ready));
	int incount_fwd, canceled, out_total;

	/* First do a test to see what is ready on the server side.  */

	incount_fwd = 0;
	canceled = 0;
	for (i = 0; i < incount; i++)
	{
	    method_op_p op = (method_op_p)id_gen_fast_lookup(id_array[i]);

	    /* Canceled messages are not queried.  */
	    if (op->error_code == BMI_ECANCEL)
	    {
		canceled++;
		continue;
	    }

	    tags[incount_fwd] = op->msg_tag;
	    ops[incount_fwd] = op->send_recv;
	    if ((int)op->method_data)
		unexp_sizes[incount_fwd] = op->actual_size;
	    else
		unexp_sizes[incount_fwd] = -1;
	    incount_fwd++;
	}

	/* If we have canceled messages, then we don't need to wait.  We
	   still check with the server to find about ready messages, but
	   that's it.  */
	if (canceled > 0)
	    max_idle_time_ms = 0;

	ret = zbmi_test(tags, ops, unexp_sizes, incount_fwd, ready,
			max_idle_time_ms);
	if ((err = __zoid_error()))
	    return zoid_err_to_bmi(err);

	/* Now that we know where we stand, we can perform the actual
	   sends/receives.  */
#if 0
	fprintf(stderr, "zbmi_test returned %d\n", ret);
#endif
	out_total = ret + canceled;

	for (i = 0, out = 0; i < incount && out < out_total; i++)
	{
	    method_op_p op;

	    if (out == outcount_max)
		break;

	    op = (method_op_p)id_gen_fast_lookup(id_array[i]);

	    if (op->error_code == BMI_ECANCEL)
	    {
		actual_size_array[out] = 0;
		error_code_array[out] = BMI_ECANCEL;
	    }
	    else if (ready[i])
	    {
		size_t* size_list_cp;
#if 0
		fprintf(stderr, "op %d is ready!\n", i);
#endif
		size_list_cp = alloca(op->list_count * sizeof(*size_list_cp));
		{
		    int j;
		    for (j = 0; j < op->list_count; j++)
			size_list_cp[j] = op->size_list[j];
		}

		if (op->send_recv == BMI_SEND)
		{
		    ret = zbmi_send((const void*const*)op->buffer_list,
				    size_list_cp, op->list_count, op->msg_tag,
				    op->class, (int)op->method_data, 0);
		    if ((err = __zoid_error()))
		    {
#if 0
			fprintf(stderr, "zbmi_send returned err %d\n", err);
#endif
			if (err == ENOMEM)
			{
			    /* This is unexpected, but not impossible.  The
			       server side might have issued a cancel.  Or, for
			       unexpected messages, another client might have
			       sent a message and used up the space that we
			       wanted to use.  */
			    continue;
			}
			return zoid_err_to_bmi(err);
		    }

		    assert (ret == 1);

		    actual_size_array[out] = op->actual_size;
		}
		else /* BMI_RECV */
		{
		    int j;

		    ret = zbmi_recv(op->buffer_list, size_list_cp,
				    op->list_count, op->msg_tag, 0);
		    if ((err = __zoid_error()))
			return zoid_err_to_bmi(err);

		    if (ret != 1)
		    {
			/* This is unexpected, but not impossible.  The
			   server side might have issued a cancel.  */
			continue;
		    }

		    actual_size_array[out] = 0;
		    for (j = 0; j < op->list_count; j++)
			actual_size_array[out] += size_list_cp[j];
		}
		error_code_array[out] = 0;
	    }
	    else /* not ready */
		continue;

	    if (index_array)
		index_array[out] = i;
	    if (user_ptr_array)
		user_ptr_array[out] = op->user_ptr;

	    op_list_remove(op);
	    bmi_dealloc_method_op(op);

	    out++;
	}
	*outcount = out;

	return 0;
    }

    /* Server code.  */

    return zoid_server_test_common(incount, id_array, outcount_max, outcount,
				   index_array, error_code_array,
				   actual_size_array, user_ptr_array,
				   max_idle_time_ms, context_id);
}

/* Internal routine to translate the few POSIX errors used by ZOID to
   their BMI equivalents.  */
static int
zoid_err_to_bmi(int err)
{
    if (err == ENOMEM)
	return -BMI_ENOMEM;
    else if (err == E2BIG)
	return -BMI_EMSGSIZE;
    else if (err == ENOSYS)
	return -BMI_ENOSYS;
    else /* Some undefined error.  */
	return -1;
}

/* Invoked on BMI_initialize.  */
static int
BMI_zoid_initialize(bmi_method_addr_p listen_addr, int method_id,
		    int init_flags)
{
#if 0
    fprintf(stderr, "Invoked zoid_initialize\n");
#endif
    zoid_node_type = (init_flags & BMI_INIT_SERVER) ? SERVER : CLIENT;

    zoid_method_id = method_id;

    if (!(zoid_ops = op_list_new()))
	return -BMI_ENOMEM;

    if (zoid_node_type == CLIENT)
    {
	if (__zoid_init())
	    return -BMI_EINVAL;
    }
    else /* SERVER */
	return BMI_zoid_server_initialize();

    return 0;
}

/* Invoked on BMI_finalize.  */
static int
BMI_zoid_finalize(void)
{
    if (zoid_ops)
    {
	op_list_cleanup(zoid_ops);
	zoid_ops = NULL;
    }

    if (zoid_node_type == CLIENT)
    {
	/* Nothing to do, maybe free some internal memory buffers... */
    }
    else /* SERVER */
	return BMI_zoid_server_finalize();
    
    return 0;
}

/* Invoked on BMI_set_info.  The only important case seems to be an internal
   invocation to release a no longer needed address.  */
static int
BMI_zoid_set_info(int option, void* inout_parameter)
{
    switch (option)
    {
	case BMI_DROP_ADDR:
	    if (zoid_node_type == SERVER)
		zoid_server_free_client_addr(inout_parameter);
	    bmi_dealloc_method_addr(inout_parameter);
	    break;

	case BMI_ZOID_POST_TIMEOUT:
	    if (zoid_node_type == CLIENT)
		post_timeout = *(int*)inout_parameter;
	    break;
    }

    return 0;
}

/* Invoked on BMI_get_info.  */
static int
BMI_zoid_get_info(int option, void* inout_parameter)
{
    switch (option)
    {
	case BMI_CHECK_MAXSIZE:
	    *(int*)inout_parameter = ZOID_MAX_EXPECTED_MSG;
	    break;

	case BMI_GET_UNEXP_SIZE:
	    *(int*)inout_parameter = ZOID_MAX_UNEXPECTED_MSG;
	    break;

	default:
	    return -BMI_ENOSYS;
    }

    return 0;
}

/* Invoked on BMI_memalloc.  Important on the server, not so much on the
   client.  */
static void*
BMI_zoid_memalloc(bmi_size_t size, enum bmi_op_type send_recv)
{
    if (zoid_node_type == CLIENT)
    {
	void* ptr;

	/* Ordinary malloc() is also aligned to 16 bytes on BG, but let's be
	   explicit here...  */
	if (posix_memalign(&ptr, 16, size))
	    return NULL;

	return ptr;
    }
    else /* SERVER */
	return BMI_zoid_server_memalloc(size);
}

/* Invoked on BMI_memfree.  */
static int
BMI_zoid_memfree(void* buffer, bmi_size_t size, enum bmi_op_type send_recv)
{
    if (zoid_node_type == CLIENT)
	free(buffer);
    else /* SERVER */
	BMI_zoid_server_memfree(buffer);
    return 0;
}

/* Invoked on BMI_unexpected_free.  We only support in on the server.  */
static int
BMI_zoid_unexpected_free(void* buffer)
{
    if (zoid_node_type == CLIENT)
    {
	/* We only support unexpected messages from clients to the server.  */
	abort();
    }
    else
	return BMI_zoid_server_unexpected_free(buffer);
}

/* Invoked on BMI_post_test.  */
static int
BMI_zoid_test(bmi_op_id_t id, int* outcount, bmi_error_code_t* error_code,
	      bmi_size_t* actual_size, void** user_ptr, int max_idle_time_ms,
	      bmi_context_id context_id)
{
#if 0
    fprintf(stderr, "BMI_zoid_test invoked\n");
#endif
    return zoid_test_common(1, &id, 1, outcount, NULL, error_code,
			    actual_size, user_ptr, max_idle_time_ms,
			    context_id);
}

/* Invoked on BMI_post_testsome.  */
static int
BMI_zoid_testsome(int incount, bmi_op_id_t* id_array, int* outcount,
		  int* index_array, bmi_error_code_t* error_code_array,
		  bmi_size_t* actual_size_array, void** user_ptr_array,
		  int max_idle_time_ms, bmi_context_id context_id)
{
    return zoid_test_common(incount, id_array, incount, outcount, index_array,
			    error_code_array, actual_size_array,
			    user_ptr_array, max_idle_time_ms, context_id);
}

/* Invoked on BMI_testcontext.  */
static int
BMI_zoid_testcontext(int incount, bmi_op_id_t* out_id_array, int* outcount,
		     bmi_error_code_t* error_code_array,
		     bmi_size_t* actual_size_array, void** user_ptr_array,
		     int max_idle_time_ms, bmi_context_id context_id)
{
    if (zoid_node_type == CLIENT)
    {
	/* We scan zoid_ops for pending request and invoke zoid_test_common
	   on them.  This is OK because this code is not expected to be
	   multi-thread safe (otherwise, a testcontext in one thread followed
	   by a post_send/recv in another, would not take that post into
	   account).  */
	int ret, i;
	int pending_count = op_list_count(zoid_ops);
	bmi_op_id_t* tmp_id_array = alloca(pending_count *
					   sizeof(*tmp_id_array));
	int* tmp_index_array = alloca(incount * sizeof(*tmp_index_array));
	method_op_p met;

	i = 0;
	qlist_for_each_entry(met, zoid_ops, op_list_entry)
	    tmp_id_array[i++] = met->op_id;

	ret = zoid_test_common(pending_count, tmp_id_array, incount, outcount,
			       tmp_index_array, error_code_array,
			       actual_size_array, user_ptr_array,
			       max_idle_time_ms, context_id);

	for (i = 0; i < *outcount; i++)
	    out_id_array[i] = tmp_id_array[tmp_index_array[i]];

	return ret;
    }

    /* Server code.  */

    return zoid_server_test_common(0, out_id_array, incount, outcount, NULL,
				   error_code_array, actual_size_array,
				   user_ptr_array, max_idle_time_ms,
				   context_id);
}

/* Invoked on BMI_testunexpected_class.  We only support it on the server.  */
static int
BMI_zoid_testunexpected(int incount, int* outcount,
			struct bmi_method_unexpected_info* info,
			uint8_t class, int max_idle_time_ms)
{
    if (zoid_node_type == CLIENT)
	abort();

    /* Server code.  */

    return BMI_zoid_server_testunexpected(incount, outcount, info, class,
					  max_idle_time_ms);
}

/* Invoked on BMI_addr_lookup, also part of BMI_intialize.  The only address
   we support in the string form is zoid://, which denotes the server.  */
static struct bmi_method_addr*
BMI_zoid_method_addr_lookup(const char *id)
{
    static bmi_method_addr_p new_addr = NULL;
#if 0
    fprintf(stderr, "Invoked method_addr_lookup with id %s\n", id);
#endif
    if (strcmp(id, "zoid://"))
	return NULL;

    if (!new_addr)
    {
	/* Note: zoid_method_id will not be initialized here if we are a server.
	   Any solution?  */
	if (!(new_addr = bmi_alloc_method_addr(zoid_method_id,
					       sizeof(struct zoid_addr))))
	    return NULL;

	((struct zoid_addr*)new_addr->method_data)->pid = ZOID_ADDR_SERVER_PID;
    }

    return new_addr;
}

/* Invoked on BMI_post_send_list.  */
static int
BMI_zoid_post_send_list(bmi_op_id_t* id, bmi_method_addr_p dest,
			const void*const* buffer_list,
			const bmi_size_t* size_list, int list_count,
			bmi_size_t total_size, enum bmi_buffer_type buffer_type,
			bmi_msg_tag_t tag, void* user_ptr,
			bmi_context_id context_id, PVFS_hint hints)
{
    return zoid_post_send_common(id, dest, buffer_list, size_list, list_count,
				 total_size, buffer_type, tag, 0, user_ptr,
				 context_id, hints, 0);
}

/* Invoked on BMI_post_recv_list.  */
static int
BMI_zoid_post_recv_list(bmi_op_id_t* id, bmi_method_addr_p src,
			void *const* buffer_list, const bmi_size_t* size_list,
			int list_count, bmi_size_t total_expected_size,
			bmi_size_t* total_actual_size,
			enum bmi_buffer_type buffer_type, bmi_msg_tag_t tag,
			void* user_ptr, bmi_context_id context_id,
			PVFS_hint hints)
{
    return zoid_post_recv_common(id, src, buffer_list, size_list, list_count,
				 total_expected_size, total_actual_size,
				 buffer_type, tag, user_ptr, context_id, hints);
}

/* Invoked on BMI_post_sendunexpected_list_class.  We only support it on
   clients.  */
static int
BMI_zoid_post_sendunexpected_list(bmi_op_id_t* id, bmi_method_addr_p dest,
				  const void*const* buffer_list,
				  const bmi_size_t* size_list, int list_count,
				  bmi_size_t total_size,
				  enum bmi_buffer_type buffer_type,
				  bmi_msg_tag_t tag, uint8_t class,
				  void* user_ptr, bmi_context_id context_id,
				  PVFS_hint hints)
{
    return zoid_post_send_common(id, dest, buffer_list, size_list, list_count,
				 total_size, buffer_type, tag, class, user_ptr,
				 context_id, hints, 1);
}

/* Invoked on BMI_open_context.  We only support one, global context.  */
static int
BMI_zoid_open_context(bmi_context_id context_id)
{
    return 0;
}

/* Invoked on BMI_close_context.  We only support one, global context.  */
static void
BMI_zoid_close_context(bmi_context_id context_id)
{
}

/* Invoked on BMI_cancel.  */
static int
BMI_zoid_cancel(bmi_op_id_t id, bmi_context_id context_id)
{
    if (zoid_node_type == CLIENT)
    {
	/* Because of a lack of multi-threading considerations and the fact
	   that the server holds no state about pending requests, this is very
	   easy on the client side.  */
	method_op_p op = (method_op_p)id_gen_fast_lookup(id);

	op->error_code = BMI_ECANCEL;

	return 0;
    }

    /* Server code.  */

    return BMI_zoid_server_cancel(id, context_id);
}

/* Invoked on BMI_rev_lookup_unexpected.  */
static const char*
BMI_zoid_rev_lookup_unexpected(bmi_method_addr_p map)
{
    /* No idea what the purpose of this one is, so we don't implement it.  */
    fprintf(stderr, "zoid_rev_lookup_unexpected invoked!\n");
    /* FIXME!  */

    return NULL;
}

const struct bmi_method_ops bmi_zoid_ops =
{
    .method_name = "bmi_zoid",

    .flags = BMI_METHOD_FLAG_NO_POLLING,

    .initialize = BMI_zoid_initialize,
    .finalize = BMI_zoid_finalize,

    .set_info = BMI_zoid_set_info,
    .get_info = BMI_zoid_get_info,

    .memalloc = BMI_zoid_memalloc,
    .memfree = BMI_zoid_memfree,
    .unexpected_free = BMI_zoid_unexpected_free,

    .test = BMI_zoid_test,
    .testsome = BMI_zoid_testsome,
    .testcontext = BMI_zoid_testcontext,
    .testunexpected = BMI_zoid_testunexpected,

    .method_addr_lookup = BMI_zoid_method_addr_lookup,

    .post_send_list = BMI_zoid_post_send_list,
    .post_recv_list = BMI_zoid_post_recv_list,
    .post_sendunexpected_list = BMI_zoid_post_sendunexpected_list,

    .open_context = BMI_zoid_open_context,
    .close_context = BMI_zoid_close_context,

    .cancel = BMI_zoid_cancel,

    .rev_lookup_unexpected = BMI_zoid_rev_lookup_unexpected,
};
