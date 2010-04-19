/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* TCP/IP implementation of a BMI method */

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/poll.h>
#include <netinet/tcp.h>
#include <assert.h>
#include <sys/uio.h>
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "pint-mem.h"

#include "pvfs2-config.h"
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif

#include "bmi-method-support.h"
#include "bmi-method-callback.h"
#include "bmi-tcp-addressing.h"
#ifdef __PVFS2_USE_EPOLL__
#include "socket-collection-epoll.h"
#else
#include "socket-collection.h"
#endif
#include "op-list.h"
#include "gossip.h"
#include "sockio.h"
#include "bmi-byteswap.h"
#include "id-generator.h"
#include "pint-event.h"
#include "pvfs2-debug.h"
#ifdef USE_TRUSTED
#include "server-config.h"
#include "bmi-tcp-addressing.h"
#endif
#include "gen-locks.h"
#include "pint-hint.h"
#include "pint-event.h"
#include "quickhash.h"

#define BMI_TCP_S2S_MAGIC_NR 51912

static gen_mutex_t interface_mutex = GEN_MUTEX_INITIALIZER;
static gen_cond_t interface_cond = GEN_COND_INITIALIZER;
static int sc_test_busy = 0;

/* function prototypes */
int BMI_tcp_initialize(bmi_method_addr_p listen_addr,
		       int method_id,
		       int init_flags);
int BMI_tcp_finalize(void);
int BMI_tcp_set_info(int option,
		     void *inout_parameter);
int BMI_tcp_get_info(int option,
		     void *inout_parameter);
void *BMI_tcp_memalloc(bmi_size_t size,
		       enum bmi_op_type send_recv);
int BMI_tcp_memfree(void *buffer,
		    bmi_size_t size,
		    enum bmi_op_type send_recv);
int BMI_tcp_unexpected_free(void *buffer);
int BMI_tcp_test(bmi_op_id_t id,
		 int *outcount,
		 bmi_error_code_t * error_code,
		 bmi_size_t * actual_size,
		 void **user_ptr,
		 int max_idle_time_ms,
		 bmi_context_id context_id);
int BMI_tcp_testsome(int incount,
		     bmi_op_id_t * id_array,
		     int *outcount,
		     int *index_array,
		     bmi_error_code_t * error_code_array,
		     bmi_size_t * actual_size_array,
		     void **user_ptr_array,
		     int max_idle_time_ms,
		     bmi_context_id context_id);
int BMI_tcp_testunexpected(int incount,
			   int *outcount,
			   struct bmi_method_unexpected_info *info,
                           uint8_t class,
			   int max_idle_time_ms);
int BMI_tcp_testcontext(int incount,
		     bmi_op_id_t * out_id_array,
		     int *outcount,
		     bmi_error_code_t * error_code_array,
		     bmi_size_t * actual_size_array,
		     void **user_ptr_array,
		     int max_idle_time_ms,
		     bmi_context_id context_id);
bmi_method_addr_p BMI_tcp_method_addr_lookup(const char *id_string);
const char* BMI_tcp_addr_rev_lookup_unexpected(bmi_method_addr_p map);
int BMI_tcp_query_addr_range(bmi_method_addr_p, const char *, int);
int BMI_tcp_post_send_list(bmi_op_id_t * id,
			   bmi_method_addr_p dest,
			   const void *const *buffer_list,
			   const bmi_size_t *size_list,
			   int list_count,
			   bmi_size_t total_size,
			   enum bmi_buffer_type buffer_type,
			   bmi_msg_tag_t tag,
			   void *user_ptr,
			   bmi_context_id context_id,
                           PVFS_hint hints);
int BMI_tcp_post_recv_list(bmi_op_id_t * id,
			   bmi_method_addr_p src,
			   void *const *buffer_list,
			   const bmi_size_t *size_list,
			   int list_count,
			   bmi_size_t total_expected_size,
			   bmi_size_t * total_actual_size,
			   enum bmi_buffer_type buffer_type,
			   bmi_msg_tag_t tag,
			   void *user_ptr,
			   bmi_context_id context_id,
                           PVFS_hint hints);
int BMI_tcp_post_sendunexpected_list(bmi_op_id_t * id,
				     bmi_method_addr_p dest,
				     const void *const *buffer_list,
				     const bmi_size_t *size_list,
				     int list_count,
				     bmi_size_t total_size,
				     enum bmi_buffer_type buffer_type,
				     bmi_msg_tag_t tag,
                                     uint8_t class,
				     void *user_ptr,
				     bmi_context_id context_id,
                                     PVFS_hint hints);
int BMI_tcp_open_context(bmi_context_id context_id);
void BMI_tcp_close_context(bmi_context_id context_id);
int BMI_tcp_cancel(bmi_op_id_t id, bmi_context_id context_id);

char BMI_tcp_method_name[] = "bmi_tcp";

/* size of encoded message header */
#define TCP_ENC_HDR_SIZE 25

/* structure internal to tcp for use as a message header */
struct tcp_msg_header
{
    uint32_t magic_nr;          /* magic number */
    uint32_t mode;		/* eager, rendezvous, etc. */
    bmi_msg_tag_t tag;		/* user specified message tag */
    bmi_size_t size;		/* length of trailing message */
    uint32_t src_addr_hash;     /* hash of local svr addr (if present) */
    uint8_t class;              /* class of msg (if unexpected) */
    char enc_hdr[TCP_ENC_HDR_SIZE];  /* encoded version of header info */
};

#define BMI_TCP_ENC_HDR(hdr)						\
    do {								\
	*((uint32_t*)&((hdr).enc_hdr[0])) = htobmi32((hdr).magic_nr);	\
	*((uint32_t*)&((hdr).enc_hdr[4])) = htobmi32((hdr).mode);	\
	*((uint32_t*)&((hdr).enc_hdr[8])) = htobmi64((hdr).tag);	\
	*((uint32_t*)&((hdr).enc_hdr[12])) = htobmi32((hdr).src_addr_hash);\
	*((uint64_t*)&((hdr).enc_hdr[16])) = htobmi64((hdr).size);	\
	*((uint8_t*)&((hdr).enc_hdr[24])) = (hdr).class;                \
    } while(0)						    

#define BMI_TCP_DEC_HDR(hdr)						\
    do {								\
	(hdr).magic_nr = bmitoh32(*((uint32_t*)&((hdr).enc_hdr[0])));	\
	(hdr).mode = bmitoh32(*((uint32_t*)&((hdr).enc_hdr[4])));	\
	(hdr).tag = bmitoh32(*((uint32_t*)&((hdr).enc_hdr[8])));	\
	(hdr).src_addr_hash = bmitoh32(*((uint32_t*)&((hdr).enc_hdr[12])));\
	(hdr).size = bmitoh64(*((uint64_t*)&((hdr).enc_hdr[16])));	\
	(hdr).class = *((uint8_t*)&((hdr).enc_hdr[24]));                \
    } while(0)						    

/* enumerate states that we care about */
enum bmi_tcp_state
{
    BMI_TCP_INPROGRESS,
    BMI_TCP_BUFFERING,
    BMI_TCP_COMPLETE
};

/* tcp private portion of operation structure */
struct tcp_op
{
    struct tcp_msg_header env;	/* envelope for this message */
    enum bmi_tcp_state tcp_op_state;
    /* these two fields are used as place holders for the buffer
     * list and size list when we really don't have lists (regular
     * BMI_send or BMI_recv operations); it allows us to use
     * generic code to handle both cases 
     */
    void *buffer_list_stub;
    bmi_size_t size_list_stub;
};

/* static io vector for use with readv and writev; we can only use
 * this because BMI serializes module calls
 */
#define BMI_TCP_IOV_COUNT 10
static struct iovec stat_io_vector[BMI_TCP_IOV_COUNT+1];

/* internal utility functions */
static int tcp_server_init(void);
static void dealloc_tcp_method_addr(bmi_method_addr_p map);
static int tcp_sock_init(bmi_method_addr_p my_method_addr);
static int enqueue_operation(op_list_p target_list,
			     enum bmi_op_type send_recv,
			     bmi_method_addr_p map,
			     void *const *buffer_list,
			     const bmi_size_t *size_list,
			     int list_count,
			     bmi_size_t amt_complete,
			     bmi_size_t env_amt_complete,
			     bmi_op_id_t * id,
			     int tcp_op_state,
			     struct tcp_msg_header header,
			     void *user_ptr,
			     bmi_size_t actual_size,
			     bmi_size_t expected_size,
			     bmi_context_id context_id,
                             int32_t event_id);
static int tcp_cleanse_addr(bmi_method_addr_p map, int error_code);
static int tcp_shutdown_addr(bmi_method_addr_p map);
static int tcp_do_work(int max_idle_time);
static int tcp_do_work_error(bmi_method_addr_p map);
static int tcp_do_work_recv(bmi_method_addr_p map, int* stall_flag);
static int tcp_do_work_send(bmi_method_addr_p map, int* stall_flag);
static int work_on_recv_op(method_op_p my_method_op,
			   int *stall_flag);
static int work_on_send_op(method_op_p my_method_op,
			   int *blocked_flag, int* stall_flag);
static int tcp_accept_init(int *socket, char** peer);
static method_op_p alloc_tcp_method_op(void);
static void dealloc_tcp_method_op(method_op_p old_op);
static int handle_new_connection(bmi_method_addr_p map);
static int tcp_post_send_generic(bmi_op_id_t * id,
                                 bmi_method_addr_p dest,
                                 const void *const *buffer_list,
                                 const bmi_size_t *size_list,
                                 int list_count,
                                 enum bmi_buffer_type buffer_type,
                                 struct tcp_msg_header my_header,
                                 void *user_ptr,
                                 bmi_context_id context_id,
                                 PVFS_hint hints);
static int tcp_post_recv_generic(bmi_op_id_t * id,
                                 bmi_method_addr_p src,
                                 void *const *buffer_list,
                                 const bmi_size_t *size_list,
                                 int list_count,
                                 bmi_size_t expected_size,
                                 bmi_size_t * actual_size,
                                 enum bmi_buffer_type buffer_type,
                                 bmi_msg_tag_t tag,
                                 void *user_ptr,
                                 bmi_context_id context_id,
                                 PVFS_hint hints);
static int payload_progress(int s, void *const *buffer_list, const bmi_size_t* 
    size_list, int list_count, bmi_size_t total_size, int* list_index, 
    bmi_size_t* current_index_complete, enum bmi_op_type send_recv, 
    char* enc_hdr, bmi_size_t* env_amt_complete);
static uint32_t hashlittle( const void *key, size_t length, uint32_t initval);
static int addr_hash_compare(void* key, struct qhash_head* link);

#if defined(USE_TRUSTED) && defined(__PVFS2_CLIENT__)
static int tcp_enable_trusted(struct tcp_addr *tcp_addr_data);
#endif
#if defined(USE_TRUSTED) && defined(__PVFS2_SERVER__)
static int tcp_allow_trusted(struct sockaddr_in *peer_sockaddr);
#endif

static void bmi_set_sock_buffers(int socket);

/* exported method interface */
const struct bmi_method_ops bmi_tcp_ops = {
    .method_name = BMI_tcp_method_name,
    .initialize = BMI_tcp_initialize,
    .finalize = BMI_tcp_finalize,
    .set_info = BMI_tcp_set_info,
    .get_info = BMI_tcp_get_info,
    .memalloc = BMI_tcp_memalloc,
    .memfree  = BMI_tcp_memfree,
    .unexpected_free = BMI_tcp_unexpected_free,
    .test = BMI_tcp_test,
    .testsome = BMI_tcp_testsome,
    .testcontext = BMI_tcp_testcontext,
    .testunexpected = BMI_tcp_testunexpected,
    .method_addr_lookup = BMI_tcp_method_addr_lookup,
    .post_send_list = BMI_tcp_post_send_list,
    .post_recv_list = BMI_tcp_post_recv_list,
    .post_sendunexpected_list = BMI_tcp_post_sendunexpected_list,
    .open_context = BMI_tcp_open_context,
    .close_context = BMI_tcp_close_context,
    .cancel = BMI_tcp_cancel,
    .rev_lookup_unexpected = BMI_tcp_addr_rev_lookup_unexpected,
    .query_addr_range = BMI_tcp_query_addr_range,
};

/* module parameters */
static struct
{
    int method_flags;
    int method_id;
    bmi_method_addr_p listen_addr;
} tcp_method_params;

#if defined(USE_TRUSTED) && defined(__PVFS2_SERVER__)
static struct tcp_allowed_connection_s *gtcp_allowed_connection = NULL;
#endif

static int check_unexpected = 1;

/* op_list_array indices */
enum
{
    NUM_INDICES = 5,
    IND_SEND = 0,
    IND_RECV = 1,
    IND_RECV_INFLIGHT = 2,
    IND_RECV_EAGER_DONE_BUFFERING = 3,
    IND_COMPLETE_RECV_UNEXP = 4,	/* MAKE THIS COMES LAST */
};

/* internal operation lists */
static op_list_p op_list_array[6] = { NULL, NULL, NULL, NULL,
    NULL, NULL
};

/* internal completion queues */
static op_list_p completion_array[BMI_MAX_CONTEXTS] = { NULL };

/* internal socket collection */
static socket_collection_p tcp_socket_collection_p = NULL;

/* tunable parameters */
enum
{
    /* amount of pending connections we'll allow */
    TCP_BACKLOG = 256,
    /* amount of work to be done during a test.  This roughly 
     * translates into the number of sockets that we will perform
     * nonblocking operations on during one function call.
     */
    TCP_WORK_METRIC = 128
};

/* TCP message modes */
enum
{
    TCP_MODE_IMMED = 1,		/* not used for TCP/IP */
    TCP_MODE_UNEXP = 2,
    TCP_MODE_EAGER = 4,
    TCP_MODE_REND = 8
};

/* Allowable sizes for each mode */
enum
{
    TCP_MODE_EAGER_LIMIT = 16384,	/* 16K */
    TCP_MODE_REND_LIMIT = 16777216	/* 16M */
};

/* toggles cancel mode; for bmi_tcp this will result in socket being closed
 * in all cancellation cases
 */
static int forceful_cancel_mode = 0;

/*
  Socket buffer sizes, currently these default values will be used 
  for the clients... (TODO)
 */
static int tcp_buffer_size_receive = 0;
static int tcp_buffer_size_send = 0;

static PINT_event_type bmi_tcp_send_event_id;
static PINT_event_type bmi_tcp_recv_event_id;

static PINT_event_group bmi_tcp_event_group;
static pid_t bmi_tcp_pid;

static struct qhash_table* addr_hash_table = NULL;
#define ADDR_HASH_TABLE_SIZE 137

/*************************************************************************
 * Visible Interface 
 */

/* BMI_tcp_initialize()
 *
 * Initializes the tcp method.  Must be called before any other tcp
 * method functions.
 *
 * returns 0 on success, -errno on failure
 */
int BMI_tcp_initialize(bmi_method_addr_p listen_addr,
                       int method_id,
                       int init_flags)
{

    int ret = -1;
    int tmp_errno = bmi_tcp_errno_to_pvfs(-ENOSYS);
    struct tcp_addr *tcp_addr_data = NULL;
    int i = 0;

    gossip_ldebug(GOSSIP_BMI_DEBUG_TCP, "Initializing TCP/IP module.\n");

    /* check args */
    if ((init_flags & BMI_INIT_SERVER) && !listen_addr)
    {
        gossip_lerr("Error: bad parameters given to TCP/IP module.\n");
        return (bmi_tcp_errno_to_pvfs(-EINVAL));
    }

    gen_mutex_lock(&interface_mutex);

    /* zero out our parameter structure and fill it in */
    memset(&tcp_method_params, 0, sizeof(tcp_method_params));
    tcp_method_params.method_id = method_id;
    tcp_method_params.method_flags = init_flags;

    if (init_flags & BMI_INIT_SERVER)
    {
        /* hang on to our local listening address if needed */
        tcp_method_params.listen_addr = listen_addr;
        /* and initialize server functions */
        ret = tcp_server_init();
        if (ret < 0)
        {
            tmp_errno = bmi_tcp_errno_to_pvfs(ret);
            gossip_err("Error: tcp_server_init() failure.\n");
            goto initialize_failure;
        }
    }

    /* set up the operation lists */
    for (i = 0; i < NUM_INDICES; i++)
    {
        op_list_array[i] = op_list_new();
        if (!op_list_array[i])
        {
            tmp_errno = bmi_tcp_errno_to_pvfs(-ENOMEM);
            goto initialize_failure;
        }
    }

    /* set up the socket collection */
    if (tcp_method_params.method_flags & BMI_INIT_SERVER)
    {
        tcp_addr_data = tcp_method_params.listen_addr->method_data;
        tcp_socket_collection_p = BMI_socket_collection_init(tcp_addr_data->socket);
    }
    else
    {
        tcp_socket_collection_p = BMI_socket_collection_init(-1);
    }

    if (!tcp_socket_collection_p)
    {
        tmp_errno = bmi_tcp_errno_to_pvfs(-ENOMEM);
        goto initialize_failure;
    }

    bmi_tcp_pid = getpid();
    PINT_event_define_group("bmi_tcp", &bmi_tcp_event_group);

    /* Define the send event:
     *   START: (client_id, request_id, rank, handle, op_id, send_size)
     *   STOP: (size_sent)
     */
    PINT_event_define_event(
        &bmi_tcp_event_group,
#ifdef __PVFS2_SERVER__
        "bmi_server_send",
#else
        "bmi_client_send",
#endif
        "%d%d%d%llu%d%d",
        "%d", &bmi_tcp_send_event_id);

    /* Define the recv event:
     *   START: (client_id, request_id, rank, handle, op_id, recv_size)
     *   STOP: (size_received)
     */
    PINT_event_define_event(
        &bmi_tcp_event_group,
#ifdef __PVFS2_SERVER__
        "bmi_server_recv",
#else
        "bmi_client_recv",
#endif
        "%d%d%d%llu%d%d",
        "%d", &bmi_tcp_recv_event_id);
    
    /* create a hash table to store method addresses based on addr hash */
    addr_hash_table = qhash_init(addr_hash_compare, quickhash_null32_hash,
        ADDR_HASH_TABLE_SIZE);
    if(!addr_hash_table)
    {
        tmp_errno = bmi_tcp_errno_to_pvfs(-ENOMEM);
        goto initialize_failure;
    }

    gen_mutex_unlock(&interface_mutex);
    gossip_ldebug(GOSSIP_BMI_DEBUG_TCP,
                  "TCP/IP module successfully initialized.\n");
    return (0);

  initialize_failure:

    /* cleanup data structures and bail out */
    for (i = 0; i < NUM_INDICES; i++)
    {
        if (op_list_array[i])
        {
            op_list_cleanup(op_list_array[i]);
        }
    }
    if (tcp_socket_collection_p)
    {
        BMI_socket_collection_finalize(tcp_socket_collection_p);
    }
    gen_mutex_unlock(&interface_mutex);
    return (tmp_errno);
}


/* BMI_tcp_finalize()
 * 
 * Shuts down the tcp method.
 *
 * returns 0 on success, -errno on failure
 */
int BMI_tcp_finalize(void)
{
    int i = 0;

    gen_mutex_lock(&interface_mutex);

    /* shut down our listen addr, if we have one */
    if ((tcp_method_params.method_flags & BMI_INIT_SERVER)
        && tcp_method_params.listen_addr)
    {
        dealloc_tcp_method_addr(tcp_method_params.listen_addr);
    }

    /* note that this forcefully shuts down operations */
    for (i = 0; i < NUM_INDICES; i++)
    {
        if (op_list_array[i])
        {
            op_list_cleanup(op_list_array[i]);
            op_list_array[i] = NULL;
        }
    }

    /* get rid of socket collection */
    if (tcp_socket_collection_p)
    {
        BMI_socket_collection_finalize(tcp_socket_collection_p);
        tcp_socket_collection_p = NULL;
    }

    /* NOTE: we are trusting the calling BMI layer to deallocate 
     * all of the method addresses (this will close any open sockets)
     */
    gossip_ldebug(GOSSIP_BMI_DEBUG_TCP, "TCP/IP module finalized.\n");
    gen_mutex_unlock(&interface_mutex);
    return (0);
}


/*
 * BMI_tcp_method_addr_lookup()
 *
 * resolves the string representation of an address into a method
 * address structure.  
 *
 * returns a pointer to method_addr on success, NULL on failure
 */
bmi_method_addr_p BMI_tcp_method_addr_lookup(const char *id_string)
{
    char *tcp_string = NULL;
    char *delim = NULL;
    char *hostname = NULL;
    bmi_method_addr_p new_addr = NULL;
    struct tcp_addr *tcp_addr_data = NULL;
    int ret = -1;
    uint32_t addr_hash;
    struct qhash_head* tmp_link;

    tcp_string = string_key("tcp", id_string);
    if (!tcp_string)
    {
        /* the string doesn't even have our info */
        return (NULL);
    }

    /* start breaking up the method information */
    /* for normal tcp, it is simply hostname:port */
    if ((delim = index(tcp_string, ':')) == NULL)
    {
        gossip_lerr("Error: malformed tcp address.\n");
        free(tcp_string);
        return (NULL);
    }

    addr_hash = hashlittle(id_string, strlen(id_string), 3334);
    /* do we already have a connection established from this host? */
    if(addr_hash_table && (tmp_link = qhash_search(addr_hash_table,
        &addr_hash)))
    {
        /* we have already received an inbound connection from the host that
         * we are looking up.  Re-use the existing method addr rather than
         * creating a new one 
         */
        tcp_addr_data = qlist_entry(tmp_link, struct
            tcp_addr, hash_link);
        new_addr = tcp_addr_data->parent;
        if(tcp_addr_data->hostname)
            free(tcp_addr_data->hostname);
        tcp_addr_data->dont_reconnect = 0;
        assert(new_addr->ref_count == 1);
        new_addr->ref_count++;
    }
    else
    {
        /* looks ok, so let's build the method addr structure */
        new_addr = alloc_tcp_method_addr();
        if (!new_addr)
        {
            free(tcp_string);
            return (NULL);
        }
        tcp_addr_data = new_addr->method_data;
    }

    ret = sscanf((delim + 1), "%d", &(tcp_addr_data->port));
    if (ret != 1)
    {
        gossip_lerr("Error: malformed tcp address.\n");
        dealloc_tcp_method_addr(new_addr);
        free(tcp_string);
        return (NULL);
    }

    hostname = (char *) malloc((delim - tcp_string + 1));
    if (!hostname)
    {
        dealloc_tcp_method_addr(new_addr);
        free(tcp_string);
        return (NULL);
    }
    strncpy(hostname, tcp_string, (delim - tcp_string));
    hostname[delim - tcp_string] = '\0';

    tcp_addr_data->hostname = hostname;
    tcp_addr_data->addr_hash = addr_hash; 
    /* add entry to hash table so we can find it later */
    if(addr_hash_table)
    {
        qhash_add(addr_hash_table, &tcp_addr_data->addr_hash,
            &tcp_addr_data->hash_link);
    }
    gossip_debug(GOSSIP_BMI_DEBUG_TCP,
        "Hashed BMI address %s to %u\n", id_string,
        tcp_addr_data->addr_hash);

    free(tcp_string);

    return (new_addr);
}


/* BMI_tcp_memalloc()
 * 
 * Allocates memory that can be used in native mode by tcp.
 *
 * returns 0 on success, -errno on failure
 */
void *BMI_tcp_memalloc(bmi_size_t size,
		       enum bmi_op_type send_recv)
{
    /* we really don't care what flags the caller uses, TCP/IP has no
     * preferences about how the memory should be configured.
     */

/*    return (calloc(1,(size_t) size)); */
    return PINT_mem_aligned_alloc(size, 4096);
}


/* BMI_tcp_memfree()
 * 
 * Frees memory that was allocated with BMI_tcp_memalloc()
 *
 * returns 0 on success, -errno on failure
 */
int BMI_tcp_memfree(void *buffer,
		    bmi_size_t size,
		    enum bmi_op_type send_recv)
{
    PINT_mem_aligned_free(buffer);
    return (0);
}

/* BMI_tcp_unexpected_free()
 * 
 * Frees memory that was returned from BMI_tcp_test_unexpected()
 *
 * returns 0 on success, -errno on failure
 */
int BMI_tcp_unexpected_free(void *buffer)
{
    if (buffer)
    {
	free(buffer);
    }
    return (0);
}

#ifdef USE_TRUSTED

static struct tcp_allowed_connection_s *
alloc_trusted_connection_info(int network_count)
{
    struct tcp_allowed_connection_s *tcp_allowed_connection_info = NULL;

    tcp_allowed_connection_info = (struct tcp_allowed_connection_s *)
            calloc(1, sizeof(struct tcp_allowed_connection_s));
    if (tcp_allowed_connection_info)
    {
        tcp_allowed_connection_info->network =
            (struct in_addr *) calloc(network_count, sizeof(struct in_addr));
        if (tcp_allowed_connection_info->network == NULL)
        {
            free(tcp_allowed_connection_info);
            tcp_allowed_connection_info = NULL;
        }
        else
        {
            tcp_allowed_connection_info->netmask =
                (struct in_addr *) calloc(network_count, sizeof(struct in_addr));
            if (tcp_allowed_connection_info->netmask == NULL)
            {
                free(tcp_allowed_connection_info->network);
                free(tcp_allowed_connection_info);
                tcp_allowed_connection_info = NULL;
            }
            else {
                tcp_allowed_connection_info->network_count = network_count;
            }
        }
    }
    return tcp_allowed_connection_info;
}

static void 
dealloc_trusted_connection_info(void* ptcp_allowed_connection_info)
{
    struct tcp_allowed_connection_s *tcp_allowed_connection_info =
        (struct tcp_allowed_connection_s *) ptcp_allowed_connection_info;
    if (tcp_allowed_connection_info)
    {
        free(tcp_allowed_connection_info->network);
        tcp_allowed_connection_info->network = NULL;
        free(tcp_allowed_connection_info->netmask);
        tcp_allowed_connection_info->netmask = NULL;
        free(tcp_allowed_connection_info);
    }
    return;
}

#endif

/*
 * This function will convert a mask_bits value to an in_addr
 * representation. i.e for example if
 * mask_bits was 24 then it would be 255.255.255.0
 * if mask_bits was 22 then it would be 255.255.252.0
 * etc
 */
static void convert_mask(int mask_bits, struct in_addr *mask)
{
   uint32_t addr = -1;
   addr = addr & ~~(-1 << (mask_bits ? (32 - mask_bits) : 32));
   mask->s_addr = htonl(addr);
   return;
}

/* BMI_tcp_set_info()
 * 
 * Pass in optional parameters.
 *
 * returns 0 on success, -errno on failure
 */
int BMI_tcp_set_info(int option,
		     void *inout_parameter)
{
    int ret = -1;
    bmi_method_addr_p tmp_addr = NULL;

    gen_mutex_lock(&interface_mutex);

    switch (option)
    {
    case BMI_TCP_BUFFER_SEND_SIZE:
       tcp_buffer_size_send = *((int *)inout_parameter);
       ret = 0;
#ifdef __PVFS2_SERVER__
       /* Set the default socket buffer sizes for the server socket */
       bmi_set_sock_buffers(
           ((struct tcp_addr *)
            tcp_method_params.listen_addr->method_data)->socket);
#endif
       break;
    case BMI_TCP_BUFFER_RECEIVE_SIZE:
       tcp_buffer_size_receive = *((int *)inout_parameter);
       ret = 0;
#ifdef __PVFS2_SERVER__
       /* Set the default socket buffer sizes for the server socket */
       bmi_set_sock_buffers(
           ((struct tcp_addr *)
            tcp_method_params.listen_addr->method_data)->socket);
#endif
       break;
    case BMI_TCP_CLOSE_SOCKET: 
        /* this should no longer make it to the bmi_tcp method; see bmi.c */
        ret = 0;
        break;
    case BMI_FORCEFUL_CANCEL_MODE:
	forceful_cancel_mode = 1;
	ret = 0;
	break;
    case BMI_DROP_ADDR:
	if (inout_parameter == NULL)
	{
	    ret = bmi_tcp_errno_to_pvfs(-EINVAL);
	}
	else
	{
	    tmp_addr = (bmi_method_addr_p) inout_parameter;
	    /* take it out of the socket collection */
	    tcp_forget_addr(tmp_addr, 1, 0);
	    ret = 0;
	}
	break;
#ifdef USE_TRUSTED
    case BMI_TRUSTED_CONNECTION:
    {
        struct tcp_allowed_connection_s *tcp_allowed_connection = NULL;
        if (inout_parameter == NULL)
        {
            ret = bmi_tcp_errno_to_pvfs(-EINVAL);
            break;
        }
        else 
        {
            int    bmi_networks_count = 0;
            char **bmi_networks = NULL;
            int   *bmi_netmasks = NULL;
            struct server_configuration_s *svc_config = NULL;

            svc_config = (struct server_configuration_s *) inout_parameter;
            tcp_allowed_connection = alloc_trusted_connection_info(svc_config->allowed_networks_count);
            if (tcp_allowed_connection == NULL)
            {
                ret = bmi_tcp_errno_to_pvfs(-ENOMEM);
                break;
            }
#ifdef      __PVFS2_SERVER__
            gtcp_allowed_connection = tcp_allowed_connection;
#endif
            /* Stash this in the server_configuration_s structure. freed later on */
            svc_config->security = tcp_allowed_connection;
            svc_config->security_dtor = &dealloc_trusted_connection_info;
            ret = 0;
            /* Fill up the list of allowed ports */
            PINT_config_get_allowed_ports(svc_config, 
                    &tcp_allowed_connection->port_enforce, 
                    tcp_allowed_connection->ports);

            /* if it was enabled, make sure that we know how to deal with it */
            if (tcp_allowed_connection->port_enforce == 1)
            {
                /* illegal ports */
                if (tcp_allowed_connection->ports[0] > 65535 
                        || tcp_allowed_connection->ports[1] > 65535
                        || tcp_allowed_connection->ports[1] < tcp_allowed_connection->ports[0])
                {
                    gossip_lerr("Error: illegal trusted port values\n");
                    ret = bmi_tcp_errno_to_pvfs(-EINVAL);
                    /* don't enforce anything! */
                    tcp_allowed_connection->port_enforce = 0;
                }
            }
            ret = 0;
            /* Retrieve the list of BMI network addresses and masks  */
            PINT_config_get_allowed_networks(svc_config,
                    &tcp_allowed_connection->network_enforce,
                    &bmi_networks_count,
                    &bmi_networks,
                    &bmi_netmasks);

            /* if it was enabled, make sure that we know how to deal with it */
            if (tcp_allowed_connection->network_enforce == 1)
            {
                int i;

                for (i = 0; i < bmi_networks_count; i++)
                {
                    char *tcp_string = NULL;
                    /* Convert the network string into an in_addr_t structure */
                    tcp_string = string_key("tcp", bmi_networks[i]);
                    if (!tcp_string)
                    {
                        /* the string doesn't even have our info */
                        gossip_lerr("Error: malformed tcp network address\n");
                        ret = bmi_tcp_errno_to_pvfs(-EINVAL);
                    }
                    else {
                        /* convert this into an in_addr_t */
                        inet_aton(tcp_string, &tcp_allowed_connection->network[i]);
                        free(tcp_string);
                    }
                    convert_mask(bmi_netmasks[i], &tcp_allowed_connection->netmask[i]);
                }
                /* don't enforce anything if there were any errors */
                if (ret != 0)
                {
                    tcp_allowed_connection->network_enforce = 0;
                }
            }
        }
        break;
    }
#endif
    case BMI_TCP_CHECK_UNEXPECTED:
    {
        check_unexpected = *(int *)inout_parameter;
        ret = 0;
        break;
    }

    default:
	gossip_ldebug(GOSSIP_BMI_DEBUG_TCP,
                      "TCP hint %d not implemented.\n", option);
	ret = 0;
	break;
    }

    gen_mutex_unlock(&interface_mutex);
    return (ret);
}

/* BMI_tcp_get_info()
 * 
 * Query for optional parameters.
 *
 * returns 0 on success, -errno on failure
 */
int BMI_tcp_get_info(int option,
		     void *inout_parameter)
{
    struct method_drop_addr_query* query;
    struct tcp_addr* tcp_addr_data;
    int ret = 0;

    gen_mutex_lock(&interface_mutex);

    switch (option)
    {
    case BMI_CHECK_MAXSIZE:
	*((int *) inout_parameter) = TCP_MODE_REND_LIMIT;
        ret = 0;
	break;
    case BMI_DROP_ADDR_QUERY:
	query = (struct method_drop_addr_query*)inout_parameter;
	tcp_addr_data=query->addr->method_data;
	/* only suggest that we discard the address if we have experienced
	 * an error and there is no way to reconnect
	 */
	if(tcp_addr_data->addr_error != 0 &&
           tcp_addr_data->dont_reconnect == 1)
	{
	    query->response = 1;
	}
	else
	{
	    query->response = 0;
	}
        ret = 0;
	break;
    case BMI_GET_UNEXP_SIZE:
        *((int *) inout_parameter) = TCP_MODE_EAGER_LIMIT;
        ret = 0;
        break;

    default:
	gossip_ldebug(GOSSIP_BMI_DEBUG_TCP,
                      "TCP hint %d not implemented.\n", option);
        ret = -ENOSYS;
	break;
    }

    gen_mutex_unlock(&interface_mutex);
    return (ret < 0) ? bmi_tcp_errno_to_pvfs(ret) : ret;
}

/* BMI_tcp_test()
 * 
 * Checks to see if a particular message has completed.
 *
 * returns 0 on success, -errno on failure
 */
int BMI_tcp_test(bmi_op_id_t id,
		 int *outcount,
		 bmi_error_code_t * error_code,
		 bmi_size_t * actual_size,
		 void **user_ptr,
		 int max_idle_time,
		 bmi_context_id context_id)
{
    int ret = -1;
    method_op_p query_op = (method_op_p)id_gen_fast_lookup(id);

    assert(query_op != NULL);

    gen_mutex_lock(&interface_mutex);

    /* do some ``real work'' here */
    ret = tcp_do_work(max_idle_time);
    if (ret < 0)
    {
	gen_mutex_unlock(&interface_mutex);
	return (ret);
    }

    if(((struct tcp_op*)(query_op->method_data))->tcp_op_state ==
	BMI_TCP_COMPLETE)
    {
	assert(query_op->context_id == context_id);
	op_list_remove(query_op);
	if (user_ptr != NULL)
	{
	    (*user_ptr) = query_op->user_ptr;
	}
	(*error_code) = query_op->error_code;
	(*actual_size) = query_op->actual_size;
        PINT_EVENT_END(
            (query_op->send_recv == BMI_SEND ?
             bmi_tcp_send_event_id : bmi_tcp_recv_event_id), bmi_tcp_pid, NULL,
             query_op->event_id, id, *actual_size);

	dealloc_tcp_method_op(query_op);
	(*outcount)++;
    }

    gen_mutex_unlock(&interface_mutex);
    return (0);
}

/* BMI_tcp_testsome()
 * 
 * Checks to see if any messages from the specified list have completed.
 *
 * returns 0 on success, -errno on failure
 */
int BMI_tcp_testsome(int incount,
                     bmi_op_id_t * id_array,
                     int *outcount,
                     int *index_array,
                     bmi_error_code_t * error_code_array,
                     bmi_size_t * actual_size_array,
                     void **user_ptr_array,
                     int max_idle_time,
                     bmi_context_id context_id)
{
    int ret = -1;
    method_op_p query_op = NULL;
    int i;

    gen_mutex_lock(&interface_mutex);

    /* do some ``real work'' here */
    ret = tcp_do_work(max_idle_time);
    if (ret < 0)
    {
        gen_mutex_unlock(&interface_mutex);
        return (ret);
    }

    for(i=0; i<incount; i++)
    {
        if(id_array[i])
        {
            /* NOTE: this depends on the user passing in valid id's;
             * otherwise we segfault.  
             */
            query_op = (method_op_p)id_gen_fast_lookup(id_array[i]);
            if(((struct tcp_op*)(query_op->method_data))->tcp_op_state ==
               BMI_TCP_COMPLETE)
            {
                assert(query_op->context_id == context_id);
                /* this one's done; pop it out */
                op_list_remove(query_op);
                error_code_array[*outcount] = query_op->error_code;
                actual_size_array[*outcount] = query_op->actual_size;
                index_array[*outcount] = i;
                if (user_ptr_array != NULL)
                {
                    user_ptr_array[*outcount] = query_op->user_ptr;
                }
                PINT_EVENT_END(
                    (query_op->send_recv == BMI_SEND ?
                     bmi_tcp_send_event_id : bmi_tcp_recv_event_id),
                    bmi_tcp_pid, NULL,
                    query_op->event_id, actual_size_array[*outcount]);
                dealloc_tcp_method_op(query_op);
                (*outcount)++;
            }
        }
    }

    gen_mutex_unlock(&interface_mutex);
    return(0);
}


/* BMI_tcp_testunexpected()
 * 
 * Checks to see if any unexpected messages have completed.
 *
 * returns 0 on success, -errno on failure
 */
int BMI_tcp_testunexpected(int incount,
			   int *outcount,
			   struct bmi_method_unexpected_info *info,
                           uint8_t class,
			   int max_idle_time)
{
    int ret = -1;
    method_op_p query_op = NULL;
    struct op_list_search_key key;

    memset(&key, 0, sizeof(struct op_list_search_key));
    key.class = class;
    key.class_yes = 1;

    gen_mutex_lock(&interface_mutex);

    if(op_list_empty(op_list_array[IND_COMPLETE_RECV_UNEXP]))
    {
        /* do some ``real work'' here */
        ret = tcp_do_work(max_idle_time);
        if (ret < 0)
        {
            gen_mutex_unlock(&interface_mutex);
            return (ret);
        }
    }

    *outcount = 0;

    /* go through the completed/unexpected list as long as we are finding 
     * stuff and we have room in the info array for it
     */
    while(*outcount < incount)
    {
        query_op = op_list_search(op_list_array[IND_COMPLETE_RECV_UNEXP],
            &key);
        if(!query_op)
        {
            break;
        }
        info[*outcount].error_code = query_op->error_code;
        /* always show unexpected messages on primary address */
        if(query_op->addr->primary)
	    info[*outcount].addr = query_op->addr->primary;
        else
	    info[*outcount].addr = query_op->addr;
	info[*outcount].buffer = query_op->buffer;
	info[*outcount].size = query_op->actual_size;
	info[*outcount].tag = query_op->msg_tag;
	op_list_remove(query_op);
	dealloc_tcp_method_op(query_op);
	(*outcount)++;
    }

    gen_mutex_unlock(&interface_mutex);
    return (0);
}


/* BMI_tcp_testcontext()
 * 
 * Checks to see if any messages from the specified context have completed.
 *
 * returns 0 on success, -errno on failure
 */
int BMI_tcp_testcontext(int incount,
		     bmi_op_id_t* out_id_array,
		     int *outcount,
		     bmi_error_code_t * error_code_array,
		     bmi_size_t * actual_size_array,
		     void **user_ptr_array,
		     int max_idle_time,
		     bmi_context_id context_id)
{
    int ret = -1;
    method_op_p query_op = NULL;

    *outcount = 0;

    gen_mutex_lock(&interface_mutex);

    if(op_list_empty(completion_array[context_id]))
    {
        /* if there are unexpected ops ready to go, then short out so
         * that the next testunexpected call can pick it up without
         * delay
         */
        if(check_unexpected &&
           !op_list_empty(op_list_array[IND_COMPLETE_RECV_UNEXP]))
        {
            gen_mutex_unlock(&interface_mutex);
            return(0);
        }

        /* do some ``real work'' here */
        ret = tcp_do_work(max_idle_time);
        if (ret < 0)
        {
            gen_mutex_unlock(&interface_mutex);
            return (ret);
        }
    }

    /* pop as many items off of the completion queue as we can */
    while((*outcount < incount) && 
          (query_op =
           op_list_shownext(completion_array[context_id])))
    {
        assert(query_op);
        assert(query_op->context_id == context_id);

        /* this one's done; pop it out */
        op_list_remove(query_op);
        error_code_array[*outcount] = query_op->error_code;
        actual_size_array[*outcount] = query_op->actual_size;
        out_id_array[*outcount] = query_op->op_id;
        if (user_ptr_array != NULL)
        {
            user_ptr_array[*outcount] = query_op->user_ptr;
        }

        PINT_EVENT_END((query_op->send_recv == BMI_SEND ?
                        bmi_tcp_send_event_id : bmi_tcp_recv_event_id),
                       bmi_tcp_pid, NULL, query_op->event_id,
                       query_op->actual_size);

        dealloc_tcp_method_op(query_op);
        query_op = NULL;
        (*outcount)++;
    }

    gen_mutex_unlock(&interface_mutex);
    return(0);
}



/* BMI_tcp_post_send_list()
 *
 * same as the BMI_tcp_post_send() function, except that it sends
 * from an array of possibly non contiguous buffers
 *
 * returns 0 on success, 1 on immediate successful completion,
 * -errno on failure
 */
int BMI_tcp_post_send_list(bmi_op_id_t * id,
			   bmi_method_addr_p dest,
			   const void *const *buffer_list,
			   const bmi_size_t *size_list,
			   int list_count,
			   bmi_size_t total_size,
			   enum bmi_buffer_type buffer_type,
			   bmi_msg_tag_t tag,
			   void *user_ptr,
			   bmi_context_id context_id,
                           PVFS_hint hints)
{
    struct tcp_msg_header my_header;
    int ret = -1;
    struct tcp_addr *tcp_addr_data = NULL;

    /* clear the id field for safety */
    *id = 0;

    /* fill in the TCP-specific message header */
    if (total_size > TCP_MODE_REND_LIMIT)
    {
	gossip_lerr("Error: BMI message too large!\n");
	return (bmi_tcp_errno_to_pvfs(-EMSGSIZE));
    }

    if (total_size <= TCP_MODE_EAGER_LIMIT)
    {
	my_header.mode = TCP_MODE_EAGER;
    }
    else
    {
	my_header.mode = TCP_MODE_REND;
    }
    my_header.tag = tag;
    my_header.size = total_size;
    my_header.magic_nr = BMI_TCP_S2S_MAGIC_NR;
    if(tcp_method_params.method_flags & BMI_INIT_SERVER)
    {
        /* servers identify themselves to peers with an address hash */
        tcp_addr_data = tcp_method_params.listen_addr->method_data;
        my_header.src_addr_hash = tcp_addr_data->addr_hash;
    }
    else
    {
        my_header.src_addr_hash = 0;
    }

    gen_mutex_lock(&interface_mutex);

    ret = tcp_post_send_generic(id, dest, buffer_list,
                                size_list, list_count, buffer_type,
                                my_header, user_ptr, context_id, hints);
    gen_mutex_unlock(&interface_mutex);
    return(ret);
}

/* BMI_tcp_post_recv_list()
 *
 * same as the BMI_tcp_post_recv() function, except that it recvs
 * into an array of possibly non contiguous buffers
 *
 * returns 0 on success, 1 on immediate successful completion,
 * -errno on failure
 */
int BMI_tcp_post_recv_list(bmi_op_id_t * id,
			   bmi_method_addr_p src,
			   void *const *buffer_list,
			   const bmi_size_t *size_list,
			   int list_count,
			   bmi_size_t total_expected_size,
			   bmi_size_t * total_actual_size,
			   enum bmi_buffer_type buffer_type,
			   bmi_msg_tag_t tag,
			   void *user_ptr,
			   bmi_context_id context_id,
                           PVFS_hint hints)
{
    int ret = -1;

    if (total_expected_size > TCP_MODE_REND_LIMIT)
    {
	return (bmi_tcp_errno_to_pvfs(-EINVAL));
    }

    gen_mutex_lock(&interface_mutex);

    ret = tcp_post_recv_generic(id, src, buffer_list, size_list,
                                list_count, total_expected_size,
                                total_actual_size, buffer_type, tag, user_ptr,
                                context_id, hints);

    gen_mutex_unlock(&interface_mutex);
    return (ret);
}


/* BMI_tcp_post_sendunexpected_list()
 *
 * same as the BMI_tcp_post_sendunexpected() function, except that 
 * it sends from an array of possibly non contiguous buffers
 *
 * returns 0 on success, 1 on immediate successful completion,
 * -errno on failure
 */
int BMI_tcp_post_sendunexpected_list(bmi_op_id_t * id,
				     bmi_method_addr_p dest,
				     const void *const *buffer_list,
				     const bmi_size_t *size_list,
				     int list_count,
				     bmi_size_t total_size,
				     enum bmi_buffer_type buffer_type,
				     bmi_msg_tag_t tag,
                                     uint8_t class,
				     void *user_ptr,
				     bmi_context_id context_id,
                                     PVFS_hint hints)
{
    struct tcp_msg_header my_header;
    int ret = -1;
    struct tcp_addr *tcp_addr_data = NULL;

    /* clear the id field for safety */
    *id = 0;

    if (total_size > TCP_MODE_EAGER_LIMIT)
    {
	return (bmi_tcp_errno_to_pvfs(-EMSGSIZE));
    }

    my_header.mode = TCP_MODE_UNEXP;
    my_header.tag = tag;
    my_header.size = total_size;
    my_header.magic_nr = BMI_TCP_S2S_MAGIC_NR;
    my_header.class = class;
    if(tcp_method_params.method_flags & BMI_INIT_SERVER)
    {
        /* servers identify themselves to peers with an address hash */
        tcp_addr_data = tcp_method_params.listen_addr->method_data;
        my_header.src_addr_hash = tcp_addr_data->addr_hash;
    }
    else
    {
        my_header.src_addr_hash = 0;
    }

    gen_mutex_lock(&interface_mutex);

    ret = tcp_post_send_generic(id, dest, buffer_list,
                                size_list, list_count, buffer_type,
                                my_header, user_ptr, context_id, hints);

    gen_mutex_unlock(&interface_mutex);
    return(ret);
}


/* BMI_tcp_open_context()
 *
 * opens a new context with the specified context id
 *
 * returns 0 on success, -errno on failure
 */
int BMI_tcp_open_context(bmi_context_id context_id)
{

    gen_mutex_lock(&interface_mutex);

    /* start a new queue for tracking completions in this context */
    completion_array[context_id] = op_list_new();
    if (!completion_array[context_id])
    {
	gen_mutex_unlock(&interface_mutex);
	return(bmi_tcp_errno_to_pvfs(-ENOMEM));
    }

    gen_mutex_unlock(&interface_mutex);
    return(0);
}


/* BMI_tcp_close_context()
 *
 * shuts down a context, previously opened with BMI_tcp_open_context()
 *
 * no return value
 */
void BMI_tcp_close_context(bmi_context_id context_id)
{
    
    gen_mutex_lock(&interface_mutex);

    /* tear down completion queue for this context */
    op_list_cleanup(completion_array[context_id]);

    gen_mutex_unlock(&interface_mutex);
    return;
}


/* BMI_tcp_cancel()
 *
 * attempt to cancel a pending bmi tcp operation
 *
 * returns 0 on success, -errno on failure
 */
int BMI_tcp_cancel(bmi_op_id_t id, bmi_context_id context_id)
{
    method_op_p query_op = NULL;
    
    gen_mutex_lock(&interface_mutex);

    query_op = (method_op_p)id_gen_fast_lookup(id);
    if(!query_op)
    {
        /* if we can't find the operattion, then assume that it has already
         * completed naturally
         */
        gen_mutex_unlock(&interface_mutex);
        return(0);
    }

    /* easy case: is the operation already completed? */
    if(((struct tcp_op*)(query_op->method_data))->tcp_op_state ==
	BMI_TCP_COMPLETE)
    {
	/* only close socket in forceful cancel mode */
	if(forceful_cancel_mode)
	    tcp_forget_addr(query_op->addr, 0, -BMI_ECANCEL);
	/* we are done! status will be collected during test */
	gen_mutex_unlock(&interface_mutex);
	return(0);
    }

    /* has the operation started moving data yet? */
    if(query_op->env_amt_complete)
    {
	/* be pessimistic and kill the socket, even if not in forceful
	 * cancel mode */
	/* NOTE: this may place other operations beside this one into
	 * EINTR error state 
	 */
	tcp_forget_addr(query_op->addr, 0, -BMI_ECANCEL);
	gen_mutex_unlock(&interface_mutex);
	return(0);
    }

    /* if we fall to this point, op has been posted, but no data has moved
     * for it yet as far as we know
     */

    /* mark op as canceled, move to completion queue */
    query_op->error_code = -BMI_ECANCEL;
    if(query_op->send_recv == BMI_SEND)
    {
	BMI_socket_collection_remove_write_bit(tcp_socket_collection_p,
					   query_op->addr);
    }
    op_list_remove(query_op);
    ((struct tcp_op*)(query_op->method_data))->tcp_op_state = 
	BMI_TCP_COMPLETE;
    /* only close socket in forceful cancel mode */
    if(forceful_cancel_mode)
	tcp_forget_addr(query_op->addr, 0, -BMI_ECANCEL);
    op_list_add(completion_array[query_op->context_id], query_op);
    gen_mutex_unlock(&interface_mutex);
    return(0);
}

/*
 * For now, we only support wildcard strings that are IP addresses
 * and not *hostnames*!
 */
static int check_valid_wildcard(const char *wildcard_string, unsigned long *octets)
{
    int i, len = strlen(wildcard_string), last_dot = -1, octet_count = 0;
    char str[16];
    for (i = 0; i < len; i++)
    {
        char c = wildcard_string[i];
        memset(str, 0, 16);
        if ((c < '0' || c > '9') && c != '*' && c != '.')
            return -EINVAL;
        if (c == '*') {
            if (octet_count >= 4)
                return -EINVAL;
            octets[octet_count++] = 256;
        }
        else if (c == '.')
        {
            char *endptr = NULL;
            if (octet_count >= 4)
                return -EINVAL;
            strncpy(str, &wildcard_string[last_dot + 1], (i - last_dot - 1));
            octets[octet_count++] = strtol(str, &endptr, 10);
            if (*endptr != '\0' || octets[octet_count-1] >= 256)
                return -EINVAL;
            last_dot = i;
        }
    }
    for (i = octet_count; i < 4; i++)
    {
         octets[i] = 256;
    }
    return 0;
}

/*
 * return 1 if the addr specified is part of the wildcard specification of octet
 * return 0 otherwise.
 */
static int check_octets(struct in_addr addr, unsigned long *octets)
{
#define B1_MASK  0xff000000
#define B1_SHIFT 24
#define B2_MASK  0x00ff0000
#define B2_SHIFT 16
#define B3_MASK  0x0000ff00
#define B3_SHIFT 8
#define B4_MASK  0x000000ff
    uint32_t host_addr = ntohl(addr.s_addr);
    /* * stands for all clients */
    if (octets[0] == 256)
    {
        return 1;
    }
    if (((host_addr & B1_MASK) >> B1_SHIFT) != octets[0])
    {
        return 0;
    }
    if (octets[1] == 256)
    {
        return 1;
    }
    if (((host_addr & B2_MASK) >> B2_SHIFT) != octets[1])
    {
        return 0;
    }
    if (octets[2] == 256)
    {
        return 1;
    }
    if (((host_addr & B3_MASK) >> B3_SHIFT) != octets[2])
    {
        return 0;
    }
    if (octets[3] == 256)
    {
        return 1;
    }
    if ((host_addr & B4_MASK) != octets[3])
    {
        return 0;
    }
    return 1;
#undef B1_MASK
#undef B1_SHIFT 
#undef B2_MASK 
#undef B2_SHIFT
#undef B3_MASK
#undef B3_SHIFT
#undef B4_MASK
}
/* BMI_tcp_query_addr_range()
 * Check if a given address is within the network specified by the wildcard string!
 * or if it is part of the subnet mask specified
 */
int BMI_tcp_query_addr_range(bmi_method_addr_p map, const char *wildcard_string, int netmask)
{
    struct tcp_addr *tcp_addr_data = map->method_data;
    struct sockaddr_in map_addr;
    socklen_t map_addr_len = sizeof(map_addr);
    const char *tcp_wildcard = wildcard_string + 6 /* strlen("tcp://") */;
    int ret = -1;

    memset(&map_addr, 0, sizeof(map_addr));
    if(getpeername(tcp_addr_data->socket, (struct sockaddr *) &map_addr, &map_addr_len) < 0)
    {
        ret =  bmi_tcp_errno_to_pvfs(-EINVAL);
        gossip_err("Error: failed to retrieve peer name for client.\n");
        return(ret);
    }
    /* Wildcard specification */
    if (netmask == -1)
    {
        unsigned long octets[4];
        if (check_valid_wildcard(tcp_wildcard, octets) < 0)
        {
            gossip_lerr("Invalid wildcard specification: %s\n", tcp_wildcard);
            return -EINVAL;
        }
        gossip_debug(GOSSIP_BMI_DEBUG_TCP, "Map Address is : %s, Wildcard Octets: %lu.%lu.%lu.%lu\n", inet_ntoa(map_addr.sin_addr),
                octets[0], octets[1], octets[2], octets[3]);
        if (check_octets(map_addr.sin_addr, octets) == 1)
        {
            return 1;
        }
    }
    /* Netmask specification */
    else {
        struct sockaddr_in mask_addr, network_addr;
        memset(&mask_addr, 0, sizeof(mask_addr));
        memset(&network_addr, 0, sizeof(network_addr));
        /* Convert the netmask address */
        convert_mask(netmask, &mask_addr.sin_addr);
        /* Invalid network address */
        if (inet_aton(tcp_wildcard, &network_addr.sin_addr) == 0)
        {
            gossip_err("Invalid network specification: %s\n", tcp_wildcard);
            return -EINVAL;
        }
        /* Matches the subnet mask! */
        if ((map_addr.sin_addr.s_addr & mask_addr.sin_addr.s_addr)
                == (network_addr.sin_addr.s_addr & mask_addr.sin_addr.s_addr))
        {
            return 1;
        }
    }
    return 0;
}

/* BMI_tcp_addr_rev_lookup_unexpected()
 *
 * looks up an address that was initialized unexpectedly and returns a string
 * hostname
 *
 * returns string on success, "UNKNOWN" on failure
 */
const char* BMI_tcp_addr_rev_lookup_unexpected(bmi_method_addr_p map)
{
    struct tcp_addr *tcp_addr_data = map->method_data;
    int debug_on;
    uint64_t mask;
    socklen_t peerlen;
    struct sockaddr_in peer;
    int ret;
    struct hostent *peerent;
    char* tmp_peer;

    /* return default response if we don't have support for the right socket
     * calls 
     */
#if !defined(HAVE_GETHOSTBYADDR)
    return(tcp_addr_data->peer);
#else 

    /* Only resolve hostnames if a gossip mask is set to request it.
     * Otherwise we leave it at ip address 
     */
    gossip_get_debug_mask(&debug_on, &mask);

    if(!debug_on || (!(mask & GOSSIP_ACCESS_HOSTNAMES)))
    {
        return(tcp_addr_data->peer);
    }

    peerlen = sizeof(struct sockaddr_in);

    if(tcp_addr_data->peer_type == BMI_TCP_PEER_HOSTNAME)
    {
        /* full hostname already cached; return now */
        return(tcp_addr_data->peer);
    }

    /* if we hit this point, we need to resolve hostname */
    ret = getpeername(tcp_addr_data->socket, (struct sockaddr*)&(peer), &peerlen);
    if(ret < 0)
    {
        /* default to use IP address */
        return(tcp_addr_data->peer);
    }

    peerent = gethostbyaddr((void*)&peer.sin_addr.s_addr, 
        sizeof(struct in_addr), AF_INET);
    if(peerent == NULL)
    {
        /* default to use IP address */
        return(tcp_addr_data->peer);
    }
 
    tmp_peer = (char*)malloc(strlen(peerent->h_name) + 1);
    if(!tmp_peer)
    {
        /* default to use IP address */
        return(tcp_addr_data->peer);
    }
    strcpy(tmp_peer, peerent->h_name);
    if(tcp_addr_data->peer)
    {
        free(tcp_addr_data->peer);
    }
    tcp_addr_data->peer = tmp_peer;
    tcp_addr_data->peer_type = BMI_TCP_PEER_HOSTNAME;
    return(tcp_addr_data->peer);

#endif

}

/* tcp_forget_addr()
 *
 * completely removes a tcp method address from use, and aborts any
 * operations that use the address.  If the
 * dealloc_flag is set, the memory used by the address will be
 * deallocated as well.
 *
 * no return value
 */
void tcp_forget_addr(bmi_method_addr_p map,
		     int dealloc_flag,
		     int error_code)
{
    struct tcp_addr* tcp_addr_data = map->method_data;
    BMI_addr_t bmi_addr = tcp_addr_data->bmi_addr;
    int tmp_outcount;
    bmi_method_addr_p tmp_addr;
    int tmp_status;

    if (tcp_socket_collection_p)
    {
	BMI_socket_collection_remove(tcp_socket_collection_p, map);
	/* perform a test to force the socket collection to act on the remove
	 * request before continuing
	 */
        if(!sc_test_busy)
        {
            BMI_socket_collection_testglobal(tcp_socket_collection_p,
                0, &tmp_outcount, &tmp_addr, &tmp_status, 0);
        }
    }

    tcp_shutdown_addr(map);
    tcp_cleanse_addr(map, error_code);
    tcp_addr_data->addr_error = error_code;
    if (dealloc_flag)
    {
        map->ref_count--;
        if(map->ref_count == 0)
        {
            if(map->secondary)
            {
                dealloc_tcp_method_addr(map->secondary);
            }
            if(map->primary)
            {
                map->primary->secondary = NULL;
            }
	    dealloc_tcp_method_addr(map);
        }
    }
    else
    {
        if(!map->primary)
        {
            /* this will cause the bmi control layer to check to see if 
             * this address can be completely forgotten
             */
            bmi_method_addr_forget_callback(bmi_addr);
        }
    }
    return;
};

/******************************************************************
 * Internal support functions
 */


/*
 * dealloc_tcp_method_addr()
 *
 * destroys method address structures generated by the TCP/IP module.
 *
 * no return value
 */
static void dealloc_tcp_method_addr(bmi_method_addr_p map)
{

    struct tcp_addr *tcp_addr_data = NULL;

    tcp_addr_data = map->method_data;
    /* close the socket, as long as it is not the one we are listening on
     * as a server.
     */
    if (!tcp_addr_data->server_port)
    {
	if (tcp_addr_data->socket > -1)
	{
	    close(tcp_addr_data->socket);
	}
    }

    if (tcp_addr_data->hostname)
	free(tcp_addr_data->hostname);
    if (tcp_addr_data->peer)
        free(tcp_addr_data->peer);

    if (tcp_addr_data->hash_link.next || tcp_addr_data->hash_link.prev)
    {
        qhash_del(&tcp_addr_data->hash_link);
    }

    bmi_dealloc_method_addr(map);

    return;
}


/*
 * alloc_tcp_method_addr()
 *
 * creates a new method address with defaults filled in for TCP/IP.
 *
 * returns pointer to struct on success, NULL on failure
 */
bmi_method_addr_p alloc_tcp_method_addr(void)
{

    struct bmi_method_addr *my_method_addr = NULL;
    struct tcp_addr *tcp_addr_data = NULL;

    my_method_addr =
	bmi_alloc_method_addr(tcp_method_params.method_id, sizeof(struct tcp_addr));
    if (!my_method_addr)
    {
	return (NULL);
    }

    /* note that we trust the alloc_method_addr() function to have zeroed
     * out the structures for us already 
     */

    tcp_addr_data = my_method_addr->method_data;
    tcp_addr_data->socket = -1;
    tcp_addr_data->port = -1;
    tcp_addr_data->map = my_method_addr;
    tcp_addr_data->sc_index = -1;
    tcp_addr_data->parent = my_method_addr;

    return (my_method_addr);
}


/*
 * tcp_server_init()
 *
 * this function is used to prepare a node to recieve incoming
 * connections if it is initialized in a server configuration.   
 *
 * returns 0 on succes, -errno on failure
 */
static int tcp_server_init(void)
{

    int oldfl = 0;		/* old socket flags */
    struct tcp_addr *tcp_addr_data = NULL;
    int tmp_errno = bmi_tcp_errno_to_pvfs(-EINVAL);
    int ret = 0;

    /* create a socket */
    tcp_addr_data = tcp_method_params.listen_addr->method_data;
    if ((tcp_addr_data->socket = BMI_sockio_new_sock()) < 0)
    {
	tmp_errno = errno;
	gossip_err("Error: BMI_sockio_new_sock: %s\n", strerror(tmp_errno));
	return (bmi_tcp_errno_to_pvfs(-tmp_errno));
    }

    /* set it to non-blocking operation */
    oldfl = fcntl(tcp_addr_data->socket, F_GETFL, 0);
    if (!(oldfl & O_NONBLOCK))
    {
	fcntl(tcp_addr_data->socket, F_SETFL, oldfl | O_NONBLOCK);
    }

    /* setup for a fast restart to avoid bind addr in use errors */
    BMI_sockio_set_sockopt(tcp_addr_data->socket, SO_REUSEADDR, 1);

    /* bind it to the appropriate port */
    if(tcp_method_params.method_flags & BMI_TCP_BIND_SPECIFIC)
    {
        ret = BMI_sockio_bind_sock_specific(tcp_addr_data->socket,
            tcp_addr_data->hostname,
            tcp_addr_data->port);
        /* NOTE: this particular function converts errno in advance */
        if(ret < 0)
        {
            PVFS_perror_gossip("BMI_sockio_bind_sock_specific", ret);
            return(ret);
        }
    }
    else
    {
        ret = BMI_sockio_bind_sock(tcp_addr_data->socket,
            tcp_addr_data->port);
    }
    
    if (ret < 0)
    {
	tmp_errno = errno;
	gossip_err("Error: BMI_sockio_bind_sock: %s\n", strerror(tmp_errno));
	return (bmi_tcp_errno_to_pvfs(-tmp_errno));
    }

    /* go ahead and listen to the socket */
    if (listen(tcp_addr_data->socket, TCP_BACKLOG) != 0)
    {
	tmp_errno = errno;
	gossip_err("Error: listen: %s\n", strerror(tmp_errno));
	return (bmi_tcp_errno_to_pvfs(-tmp_errno));
    }

    return (0);
}


/* find_recv_inflight()
 *
 * checks to see if there is a recv operation in flight (when in flight
 * means that some of the data or envelope has been read) for a 
 * particular address. 
 *
 * returns pointer to operation on success, NULL if nothing found.
 */
static method_op_p find_recv_inflight(bmi_method_addr_p map)
{
    struct op_list_search_key key;
    method_op_p query_op = NULL;

    memset(&key, 0, sizeof(struct op_list_search_key));
    key.method_addr = map;
    key.method_addr_yes = 1;

    query_op = op_list_search(op_list_array[IND_RECV_INFLIGHT], &key);

    return (query_op);
}


/* tcp_sock_init()
 *
 * this is an internal function which is used to build up a TCP/IP
 * connection in the situation of a client side operation.
 * addressing information to determine which fields need to be set.
 * If the connection is already established then it does no work.
 *
 * NOTE: this is safe to call repeatedly.  However, always check the
 * value of the not_connected field in the tcp address before using the
 * address.
 *
 * returns 0 on success, -errno on failure
 */
static int tcp_sock_init(bmi_method_addr_p my_method_addr)
{

    int oldfl = 0;		/* socket flags */
    int ret = -1;
    struct pollfd poll_conn;
    struct tcp_addr *tcp_addr_data = my_method_addr->method_data;
    int tmp_errno = 0;

    /* check for obvious problems */
    assert(my_method_addr);
    assert(my_method_addr->method_type == tcp_method_params.method_id);
    assert(tcp_addr_data->server_port == 0);

    /* fail immediately if the address is in failure mode and we have no way
     * to reconnect
     */
    if(tcp_addr_data->addr_error && tcp_addr_data->dont_reconnect)
    {
	gossip_debug(GOSSIP_BMI_DEBUG_TCP, 
	"Warning: BMI communication attempted on an address in failure mode.\n");
	return(tcp_addr_data->addr_error);
    }

    if(tcp_addr_data->addr_error)
    {
        gossip_debug(GOSSIP_BMI_DEBUG_TCP, "%s: attempting reconnect.\n",
          __func__);
	tcp_addr_data->addr_error = 0;
	assert(tcp_addr_data->socket < 0);
	tcp_addr_data->not_connected = 1;
    }

    /* is there already a socket? */
    if (tcp_addr_data->socket > -1)
    {
	/* check to see if we still need to work on the connect.. */
	if (tcp_addr_data->not_connected)
	{
	    /* this is a little weird, but we complete the nonblocking
	     * connection by polling */
	    poll_conn.fd = tcp_addr_data->socket;
	    poll_conn.events = POLLOUT;
	    ret = poll(&poll_conn, 1, 2);
	    if ((ret < 0) || (poll_conn.revents & POLLERR))
	    {
		tmp_errno = errno;
		gossip_lerr("Error: poll: %s\n", strerror(tmp_errno));
		return (bmi_tcp_errno_to_pvfs(-tmp_errno));
	    }
	    if (poll_conn.revents & POLLOUT)
	    {
		tcp_addr_data->not_connected = 0;
	    }
	}
	/* return.  the caller should check the "not_connected" flag to
	 * see if the socket is usable yet. */
	return (0);
    }
    
    bmi_set_sock_buffers(tcp_addr_data->socket);

    /* at this point there is no socket.  try to build it */
    if (tcp_addr_data->port < 1)
    {
	return (bmi_tcp_errno_to_pvfs(-EINVAL));
    }

    /* make a socket */
    if ((tcp_addr_data->socket = BMI_sockio_new_sock()) < 0)
    {
	tmp_errno = errno;
	return (bmi_tcp_errno_to_pvfs(-tmp_errno));
    }

    /* set it to non-blocking operation */
    oldfl = fcntl(tcp_addr_data->socket, F_GETFL, 0);
    if (!(oldfl & O_NONBLOCK))
    {
	fcntl(tcp_addr_data->socket, F_SETFL, oldfl | O_NONBLOCK);
    }

#if defined(USE_TRUSTED) && defined(__PVFS2_CLIENT__)
    /* make sure if we need to bind or not to some local port ranges */
    tcp_enable_trusted(tcp_addr_data);
#endif

    /* turn off Nagle's algorithm */
    if (BMI_sockio_set_tcpopt(tcp_addr_data->socket, TCP_NODELAY, 1) < 0)
    {
	tmp_errno = errno;
	gossip_lerr("Error: failed to set TCP_NODELAY option.\n");
	close(tcp_addr_data->socket);
	return (bmi_tcp_errno_to_pvfs(-tmp_errno));
    }

       bmi_set_sock_buffers(tcp_addr_data->socket);

    if (tcp_addr_data->hostname)
    {
	gossip_ldebug(GOSSIP_BMI_DEBUG_TCP,
		      "Connect: socket=%d, hostname=%s, port=%d\n",
		      tcp_addr_data->socket, tcp_addr_data->hostname,
		      tcp_addr_data->port);
	ret = BMI_sockio_connect_sock(tcp_addr_data->socket,
                      tcp_addr_data->hostname,
		      tcp_addr_data->port);
    }
    else
    {
	return (bmi_tcp_errno_to_pvfs(-EINVAL));
    }

    if (ret < 0)
    {
	if (ret == -EINPROGRESS)
	{
	    tcp_addr_data->not_connected = 1;
	    /* this will have to be connected later with a poll */
	}
	else
	{
            /* NOTE: BMI_sockio_connect_sock returns a PVFS error */
            char buff[300];

            snprintf(buff, 300, "Error: BMI_sockio_connect_sock: (%s):", 
                     tcp_addr_data->hostname);

            PVFS_perror_gossip(buff, ret);
	    return (ret);
	}
    }

    return (0);
}


/* enqueue_operation()
 *
 * creates a new operation based on the arguments to the function.  It
 * then makes sure that the address is added to the socket collection,
 * and the operation is added to the appropriate operation queue.
 *
 * Damn, what a big prototype!
 *
 * returns 0 on success, -errno on failure
 */
static int enqueue_operation(op_list_p target_list,
			     enum bmi_op_type send_recv,
			     bmi_method_addr_p map,
			     void *const *buffer_list,
			     const bmi_size_t *size_list,
			     int list_count,
			     bmi_size_t amt_complete,
			     bmi_size_t env_amt_complete,
			     bmi_op_id_t * id,
			     int tcp_op_state,
			     struct tcp_msg_header header,
			     void *user_ptr,
			     bmi_size_t actual_size,
			     bmi_size_t expected_size,
			     bmi_context_id context_id,
                             int32_t eid)
{
    method_op_p new_method_op = NULL;
    struct tcp_op *tcp_op_data = NULL;
    struct tcp_addr* tcp_addr_data = NULL;
    int i;

    /* allocate the operation structure */
    new_method_op = alloc_tcp_method_op();
    if (!new_method_op)
    {
	return (bmi_tcp_errno_to_pvfs(-ENOMEM));
    }

    *id = new_method_op->op_id;
    new_method_op->event_id = eid;

    /* set the fields */
    new_method_op->send_recv = send_recv;
    new_method_op->addr = map;
    new_method_op->user_ptr = user_ptr;
    /* this is on purpose; we want to use the buffer_list all of
     * the time, no special case for one contig buffer
     */
    new_method_op->buffer = NULL;
    new_method_op->actual_size = actual_size;
    new_method_op->expected_size = expected_size;
    new_method_op->send_recv = send_recv;
    new_method_op->amt_complete = amt_complete;
    new_method_op->env_amt_complete = env_amt_complete;
    new_method_op->msg_tag = header.tag;
    new_method_op->mode = header.mode;
    new_method_op->list_count = list_count;
    new_method_op->context_id = context_id;

    /* set our current position in list processing */
    i=0;
    new_method_op->list_index = 0;
    new_method_op->cur_index_complete = 0;
    while(amt_complete > 0)
    {
	if(amt_complete >= size_list[i])
	{
	    amt_complete -= size_list[i];
	    new_method_op->list_index++;
	    i++;
	}
	else
	{
	    new_method_op->cur_index_complete = amt_complete;
	    amt_complete = 0;
	}
    }

    tcp_op_data = new_method_op->method_data;
    tcp_op_data->tcp_op_state = tcp_op_state;
    tcp_op_data->env = header;

    /* if there is only one item in the list, then keep the list stored
     * in the op structure.  This allows us to use the same code for send
     * and recv as we use for send_list and recv_list, without having to 
     * malloc lists for those special cases
     */
    if (list_count == 1)
    {
	new_method_op->buffer_list = &tcp_op_data->buffer_list_stub;
	new_method_op->size_list = &tcp_op_data->size_list_stub;
	((void**)new_method_op->buffer_list)[0] = buffer_list[0];
	((bmi_size_t*)new_method_op->size_list)[0] = size_list[0];
    }
    else
    {
	new_method_op->size_list = size_list;
	new_method_op->buffer_list = buffer_list;
    }

    tcp_addr_data = map->method_data;

    if(tcp_addr_data->addr_error)
    {
	/* server should always fail here, client should let receives queue
	 * as if nothing were wrong
	 */
	if(tcp_addr_data->dont_reconnect || send_recv == BMI_SEND)
	{
	    gossip_debug(GOSSIP_BMI_DEBUG_TCP, 
		       "Warning: BMI communication attempted on an "
		       "address in failure mode.\n");
	    new_method_op->error_code = tcp_addr_data->addr_error;
	    op_list_add(op_list_array[new_method_op->context_id],
			new_method_op);
	    return(tcp_addr_data->addr_error);
	}
    }

#if 0
    if(tcp_addr_data->addr_error)
    {
        /* this address is bad, don't try to do anything with it */
        gossip_err("Warning: BMI communication attempted on an "
                   "address in failure mode.\n");

        new_method_op->error_code = tcp_addr_data->addr_error;
        op_list_add(op_list_array[new_method_op->context_id],
                    new_method_op);
        return(tcp_addr_data->addr_error);
    }
#endif

    /* add the socket to poll on */
    BMI_socket_collection_add(tcp_socket_collection_p, map);
    if(send_recv == BMI_SEND)
    {
        BMI_socket_collection_add_write_bit(tcp_socket_collection_p, map);
    }

    /* keep up with the operation */
    op_list_add(target_list, new_method_op);

    return (0);
}


/* tcp_post_recv_generic()
 *
 * does the real work of posting an operation - works for both
 * eager and rendezvous messages
 *
 * returns 0 on success that requires later poll, returns 1 on instant
 * completion, -errno on failure
 */
static int tcp_post_recv_generic(bmi_op_id_t * id,
                                 bmi_method_addr_p src,
                                 void *const *buffer_list,
                                 const bmi_size_t *size_list,
                                 int list_count,
                                 bmi_size_t expected_size,
                                 bmi_size_t * actual_size,
                                 enum bmi_buffer_type buffer_type,
                                 bmi_msg_tag_t tag,
                                 void *user_ptr,
                                 bmi_context_id context_id,
                                 PVFS_hint hints)
{
    method_op_p query_op = NULL;
    int ret = -1;
    struct tcp_addr *tcp_addr_data = NULL;
    struct tcp_op *tcp_op_data = NULL;
    struct tcp_msg_header bogus_header;
    struct op_list_search_key key;
    bmi_size_t copy_size = 0;
    bmi_size_t total_copied = 0;
    int i;
    PINT_event_id eid = 0;

    PINT_EVENT_START(
        bmi_tcp_recv_event_id, bmi_tcp_pid, NULL, &eid,
        PINT_HINT_GET_CLIENT_ID(hints),
        PINT_HINT_GET_REQUEST_ID(hints),
        PINT_HINT_GET_RANK(hints),
        PINT_HINT_GET_HANDLE(hints),
        PINT_HINT_GET_OP_ID(hints),
        expected_size);

    tcp_addr_data = src->method_data;

    /* short out immediately if the address is bad and we have no way to
     * reconnect
     */
    if(tcp_addr_data->addr_error && tcp_addr_data->dont_reconnect)
    {
        gossip_debug(
            GOSSIP_BMI_DEBUG_TCP,
            "Warning: BMI communication attempted "
            "on an address in failure mode.\n");
        return(tcp_addr_data->addr_error);
    }

    /* lets make sure that the message hasn't already been fully
     * buffered in eager mode before doing anything else
     */
    memset(&key, 0, sizeof(struct op_list_search_key));
    key.method_addr = src;
    key.method_addr_yes = 1;
    key.msg_tag = tag;
    key.msg_tag_yes = 1;

    query_op =
        op_list_search(op_list_array[IND_RECV_EAGER_DONE_BUFFERING], &key);
    if (query_op)
    {
        /* make sure it isn't too big */
        if (query_op->actual_size > expected_size)
        {
            gossip_err("Error: message ordering violation;\n");
            gossip_err("Error: message too large for next buffer.\n");
            return (bmi_tcp_errno_to_pvfs(-EPROTO));
        }

        /* whoohoo- it is already done! */
        /* copy buffer out to list segments; handle short case */
        for (i = 0; i < list_count; i++)
        {
            copy_size = size_list[i];
            if (copy_size + total_copied > query_op->actual_size)
            {
                copy_size = query_op->actual_size - total_copied;
            }
            memcpy(buffer_list[i], (void *) ((char *) query_op->buffer +
                                             total_copied), copy_size);
            total_copied += copy_size;
            if (total_copied == query_op->actual_size)
            {
                break;
            }
        }
        /* copy out to correct memory regions */
        (*actual_size) = query_op->actual_size;
        free(query_op->buffer);
        *id = 0;
        op_list_remove(query_op);
        dealloc_tcp_method_op(query_op);
        PINT_EVENT_END(bmi_tcp_recv_event_id, bmi_tcp_pid, NULL, eid, 0,
                       *actual_size);

        return (1);
    }

    /* look for a message that is already being received */
    query_op = op_list_search(op_list_array[IND_RECV_INFLIGHT], &key);
    if (query_op)
    {
        tcp_op_data = query_op->method_data;
    }

    /* see if it is being buffered into a temporary memory region */
    if (query_op && tcp_op_data->tcp_op_state == BMI_TCP_BUFFERING)
    {
        /* make sure it isn't too big */
        if (query_op->actual_size > expected_size)
        {
            gossip_err("Error: message ordering violation;\n");
            gossip_err("Error: message too large for next buffer.\n");
            return (bmi_tcp_errno_to_pvfs(-EPROTO));
        }

        /* copy what we have so far into the correct buffers */
        total_copied = 0;
        for (i = 0; i < list_count; i++)
        {
            copy_size = size_list[i];
            if (copy_size + total_copied > query_op->amt_complete)
            {
                copy_size = query_op->amt_complete - total_copied;
            }
            if (copy_size > 0)
            {
                memcpy(buffer_list[i], (void *) ((char *) query_op->buffer +
                                                 total_copied), copy_size);
            }
            total_copied += copy_size;
            if (total_copied == query_op->amt_complete)
            {
                query_op->list_index = i;
                query_op->cur_index_complete = copy_size;
                break;
            }
        }

        /* see if we ended on a buffer boundary */
        if (query_op->cur_index_complete ==
            query_op->size_list[query_op->list_index])
        {
            query_op->list_index++;
            query_op->cur_index_complete = 0;
        }

        /* release the old buffer */
        if (query_op->buffer)
        {
            free(query_op->buffer);
        }

        *id = query_op->op_id;
        tcp_op_data = query_op->method_data;
        tcp_op_data->tcp_op_state = BMI_TCP_INPROGRESS;

        query_op->list_count = list_count;
        query_op->user_ptr = user_ptr;
        query_op->context_id = context_id;
        /* if there is only one item in the list, then keep the list stored
         * in the op structure.  This allows us to use the same code for send
         * and recv as we use for send_list and recv_list, without having to 
         * malloc lists for those special cases
         */
        if (list_count == 1)
        {
            query_op->buffer_list = &tcp_op_data->buffer_list_stub;
            query_op->size_list = &tcp_op_data->size_list_stub;
            ((void **)query_op->buffer_list)[0] = buffer_list[0];
            ((bmi_size_t *)query_op->size_list)[0] = size_list[0];
        }
        else
        {
            query_op->buffer_list = buffer_list;
            query_op->size_list = size_list;
        }

        if (query_op->amt_complete < query_op->actual_size)
        {
            /* try to recv some more data */
            tcp_addr_data = query_op->addr->method_data;
            ret = payload_progress(tcp_addr_data->socket,
                                   query_op->buffer_list,
                                   query_op->size_list,
                                   query_op->list_count,
                                   query_op->actual_size,
                                   &(query_op->list_index),
                                   &(query_op->cur_index_complete),
                                   BMI_RECV,
                                   NULL,
                                   0);
            if (ret < 0)
            {
                PVFS_perror_gossip("Error: payload_progress", ret);
                /* payload_progress() returns BMI error codes */
                tcp_forget_addr(query_op->addr, 0, ret);
                return (ret);
            }

            query_op->amt_complete += ret;
        }
        assert(query_op->amt_complete <= query_op->actual_size);
        if (query_op->amt_complete == query_op->actual_size)
        {
            /* we are done */
            op_list_remove(query_op);
            *id = 0;
            (*actual_size) = query_op->actual_size;
            dealloc_tcp_method_op(query_op);
            PINT_EVENT_END(
                bmi_tcp_recv_event_id, bmi_tcp_pid, NULL, eid,
                0, *actual_size);

            return (1);
        }
        else
        {
            /* there is still more work to do */
            tcp_op_data->tcp_op_state = BMI_TCP_INPROGRESS;
            return (0);
        }
    }

    /* NOTE: if the message was in flight, but not buffering, then
     * that means that it has already matched an earlier receive
     * post or else is an unexpected message that doesn't require a
     * matching receive post - at any rate it shouldn't be handled
     * here
     */

    /* if we hit this point we must enqueue */
    if (expected_size <= TCP_MODE_EAGER_LIMIT)
    {
        bogus_header.mode = TCP_MODE_EAGER;
    }
    else
    {
        bogus_header.mode = TCP_MODE_REND;
    }
    bogus_header.tag = tag;
    ret = enqueue_operation(op_list_array[IND_RECV],
                            BMI_RECV, src, buffer_list, size_list,
                            list_count, 0, 0, id, BMI_TCP_INPROGRESS,
                            bogus_header, user_ptr, 0,
                            expected_size, context_id, eid);
    /* just for safety; this field isn't valid to the caller anymore */
    (*actual_size) = 0;
    /* TODO: figure out why this causes deadlocks; observable in 2
     * scenarios:
     * - pvfs2-client-core with threaded library and nptl
     * - pvfs2-server threaded with nptl sending messages to itself
     */
#if 0
    if (ret >= 0)
    {
        /* go ahead and try to do some work while we are in this
         * function since we appear to be backlogged.  Make sure that
         * we do not wait in the poll, however.
         */
        ret = tcp_do_work(0);
    }
#endif
    return (ret);
}


/* tcp_cleanse_addr()
 *
 * finds all active operations matching the given address, places them
 * in an error state, and moves them to the completed queue.
 *
 * NOTE: this function does not shut down the address.  That should be
 * handled separately
 *
 * returns 0 on success, -errno on failure
 */
static int tcp_cleanse_addr(bmi_method_addr_p map, int error_code)
{
    int i = 0;
    struct op_list_search_key key;
    method_op_p query_op = NULL;

    memset(&key, 0, sizeof(struct op_list_search_key));
    key.method_addr = map;
    key.method_addr_yes = 1;

    /* NOTE: we know the unexpected completed queue is the last index! */
    for (i = 0; i < (NUM_INDICES - 1); i++)
    {
	if (op_list_array[i])
	{
	    while ((query_op = op_list_search(op_list_array[i], &key)))
	    {
		op_list_remove(query_op);
		query_op->error_code = error_code;
		if (query_op->mode == TCP_MODE_UNEXP && query_op->send_recv
		    == BMI_RECV)
		{
		    op_list_add(op_list_array[IND_COMPLETE_RECV_UNEXP],
				query_op);
		}
		else
		{
		    ((struct tcp_op*)(query_op->method_data))->tcp_op_state = 
			BMI_TCP_COMPLETE;
		    op_list_add(completion_array[query_op->context_id], query_op);
		}
	    }
	}
    }

    return (0);
}


/* tcp_shutdown_addr()
 *
 * closes connections associated with a tcp method address
 *
 * returns 0 on success, -errno on failure
 */
static int tcp_shutdown_addr(bmi_method_addr_p map)
{

    struct tcp_addr *tcp_addr_data = map->method_data;
    if (tcp_addr_data->socket > -1)
    {
	close(tcp_addr_data->socket);
    }
    tcp_addr_data->socket = -1;
    tcp_addr_data->not_connected = 1;

    return (0);
}


/* tcp_do_work()
 *
 * this is the function that actually does communication work during
 * BMI_tcp_testXXX and BMI_tcp_waitXXX functions.  The amount of work 
 * that it does is tunable.
 *
 * returns 0 on success, -errno on failure.
 */
static int tcp_do_work(int max_idle_time)
{
    int ret = -1;
    bmi_method_addr_p addr_array[TCP_WORK_METRIC];
    int status_array[TCP_WORK_METRIC];
    int socket_count = 0;
    int i = 0;
    int stall_flag = 0;
    int busy_flag = 1;
    struct timespec req;
    struct tcp_addr* tcp_addr_data = NULL;
    struct timespec wait_time;
    struct timeval start;

    if(sc_test_busy)
    {
        /* another thread is already polling or working on sockets */
        if(max_idle_time == 0)
        {
            /* we don't want to spend time waiting on it; return
             * immediately.
             */
            return(0);
        }

        /* Sleep until working thread thread signals that it has finished
         * its work and then return.  No need for this thread to poll;
         * the other thread may have already finished what we wanted.
         * This condition wait is used strictly as a best effort to
         * prevent busy spin.  We'll sort out the results later.
         */
        gettimeofday(&start, NULL);
        wait_time.tv_sec = start.tv_sec + max_idle_time / 1000;
        wait_time.tv_nsec = (start.tv_usec + ((max_idle_time % 1000)*1000))*1000;
        if (wait_time.tv_nsec > 1000000000)
        {
            wait_time.tv_nsec = wait_time.tv_nsec - 1000000000;
            wait_time.tv_sec++;
        }
        gen_cond_timedwait(&interface_cond, &interface_mutex, &wait_time);
        return(0);
    }

    /* this thread has gained control of the polling.  */
    sc_test_busy = 1;
    gen_mutex_unlock(&interface_mutex);

    /* our turn to look at the socket collection */
    ret = BMI_socket_collection_testglobal(tcp_socket_collection_p,
				       TCP_WORK_METRIC, &socket_count,
				       addr_array, status_array,
				       max_idle_time);

    gen_mutex_lock(&interface_mutex);
    sc_test_busy = 0;

    if (ret < 0)
    {
        /* wake up anyone else who might have been waiting */
        gen_cond_broadcast(&interface_cond);
        PVFS_perror_gossip("Error: socket collection:", ret);
        /* BMI_socket_collection_testglobal() returns BMI error code */
	return (ret);
    }

    if(socket_count == 0)
	busy_flag = 0;

    /* do different kinds of work depending on results */
    for (i = 0; i < socket_count; i++)
    {
	tcp_addr_data = addr_array[i]->method_data;
	/* skip working on addresses in failure mode */
	if(tcp_addr_data->addr_error)
	{
            /* addr_error field is in BMI error code format */
	    tcp_forget_addr(addr_array[i], 0, tcp_addr_data->addr_error);
	    continue;
	}

	if (status_array[i] & SC_ERROR_BIT)
	{
	    ret = tcp_do_work_error(addr_array[i]);
	    if (ret < 0)
	    {
                PVFS_perror_gossip("Warning: BMI error handling failure, continuing", ret);
	    }
	}
	else
	{
	    if (status_array[i] & SC_WRITE_BIT)
	    {
		ret = tcp_do_work_send(addr_array[i], &stall_flag);
		if (ret < 0)
		{
                    PVFS_perror_gossip("Warning: BMI send error, continuing", ret);
                }
		if(!stall_flag)
		    busy_flag = 0;
	    }
	    if (status_array[i] & SC_READ_BIT)
	    {
		ret = tcp_do_work_recv(addr_array[i], &stall_flag);
		if (ret < 0)
		{
                    PVFS_perror_gossip("Warning: BMI recv error, continuing", ret);
		}
		if(!stall_flag)
		    busy_flag = 0;
	    }
	}
    }

    /* IMPORTANT NOTE: if we have set the following flag, then it indicates that
     * poll() is finding data on our sockets, yet we are not able to move
     * any of it right now.  This means that the sockets are backlogged, and
     * BMI is in danger of busy spinning during test functions.  Let's sleep
     * for a millisecond here in hopes of letting the rest of the system
     * catch up somehow (either by clearing a backlog in another I/O
     * component, or by posting more matching BMI recieve operations)
     */
    if(busy_flag)
    {
	req.tv_sec = 0;
	req.tv_nsec = 1000;
        gen_mutex_unlock(&interface_mutex);
	nanosleep(&req, NULL);
        gen_mutex_lock(&interface_mutex);
    }

    /* wake up anyone else who might have been waiting */
    gen_cond_broadcast(&interface_cond);
    return (0);
}


/* tcp_do_work_send()
 *
 * does work on a TCP address that is ready to send data.
 *
 * returns 0 on success, -errno on failure
 */
static int tcp_do_work_send(bmi_method_addr_p map, int* stall_flag)
{
    method_op_p active_method_op = NULL;
    struct op_list_search_key key;
    int blocked_flag = 0;
    int ret = 0;
    int tmp_stall_flag;

    *stall_flag = 1;

    while (blocked_flag == 0 && ret == 0)
    {
	/* what we want to do here is find the first operation in the send
	 * queue for this address.
	 */
	memset(&key, 0, sizeof(struct op_list_search_key));
	key.method_addr = map;
	key.method_addr_yes = 1;
	active_method_op = op_list_search(op_list_array[IND_SEND], &key);
	if (!active_method_op)
	{
	    /* ran out of queued sends to work on */
	    return (0);
	}

	ret = work_on_send_op(active_method_op, &blocked_flag, &tmp_stall_flag);
	if(!tmp_stall_flag)
	    *stall_flag = 0;
    }

    return (ret);
}


/* handle_new_connection()
 *
 * this function should be called only on special tcp method addresses
 * that represent local server ports.  It will attempt to accept a new
 * connection and create a new method address for the remote host.
 *
 * side effect: destroys the temporary method_address that is passed in
 * to it.
 *
 * returns 0 on success, -errno on failure
 */
static int handle_new_connection(bmi_method_addr_p map)
{
    struct tcp_addr *tcp_addr_data = NULL;
    int accepted_socket = -1;
    bmi_method_addr_p new_addr = NULL;
    int ret = -1;
    char* tmp_peer = NULL;

    ret = tcp_accept_init(&accepted_socket, &tmp_peer);
    if (ret < 0)
    {
	return (ret);
    }
    if (accepted_socket < 0)
    {
	/* guess it wasn't ready after all */
	return (0);
    }

    /* ok, we have a new socket.  what now?  Probably simplest
     * thing to do is to create a new method_addr, add it to the
     * socket collection, and return.  It will get caught the next
     * time around */
    new_addr = alloc_tcp_method_addr();
    if (!new_addr)
    {
	return (bmi_tcp_errno_to_pvfs(-ENOMEM));
    }
    gossip_ldebug(GOSSIP_BMI_DEBUG_TCP,
                  "Assigning socket %d to new method addr.\n",
		  accepted_socket);
    tcp_addr_data = new_addr->method_data;
    tcp_addr_data->socket = accepted_socket;
    tcp_addr_data->peer = tmp_peer;
    tcp_addr_data->peer_type = BMI_TCP_PEER_IP;

    /* set a flag to make sure that we never try to reconnect this address
     * in the future
     */
    tcp_addr_data->dont_reconnect = 1;

    BMI_socket_collection_add(tcp_socket_collection_p, new_addr);

    dealloc_tcp_method_addr(map);
    return (0);

}


/* tcp_do_work_recv()
 * 
 * does work on a TCP address that is ready to recv data.
 *
 * returns 0 on success, -errno on failure
 */
static int tcp_do_work_recv(bmi_method_addr_p map, int* stall_flag)
{

    method_op_p active_method_op = NULL;
    int ret = -1;
    void *new_buffer = NULL;
    struct op_list_search_key key;
    struct tcp_msg_header new_header;
    struct tcp_addr *tcp_addr_data = map->method_data;
    struct tcp_op *tcp_op_data = NULL;
    int tmp_errno;
    int tmp;
    bmi_size_t old_amt_complete = 0;
    time_t current_time;

    *stall_flag = 1;

    /* figure out if this is a new connection */
    if (tcp_addr_data->server_port)
    {
	/* just try to accept connection- no work yet */
	*stall_flag = 0;
	return (handle_new_connection(map));
    }

    /* look for a recv for this address that is already in flight */
    active_method_op = find_recv_inflight(map);
    /* see if we found one in progress... */
    if (active_method_op)
    {
	tcp_op_data = active_method_op->method_data;
	if (active_method_op->mode == TCP_MODE_REND &&
	    tcp_op_data->tcp_op_state == BMI_TCP_BUFFERING)
	{
	    /* we must wait for recv post */
	    return (0);
	}
	else
	{
	    old_amt_complete = active_method_op->amt_complete;
	    ret = work_on_recv_op(active_method_op, stall_flag);
            gossip_debug(GOSSIP_BMI_DEBUG_TCP, "actual_size=%d, "
                         "amt_complete=%d, old_amt_complete=%d\n",
                         (int)active_method_op->actual_size,
                         (int)active_method_op->amt_complete,
                         (int)old_amt_complete);

	    if ((ret == 0) &&
                (old_amt_complete == active_method_op->amt_complete) &&
                active_method_op->actual_size &&
                (active_method_op->amt_complete <
                 active_method_op->actual_size))
	    {
                gossip_debug(
                    GOSSIP_BMI_DEBUG_TCP, "Warning: bmi_tcp unable "
                    "to recv any data reported by poll(). [1]\n");

                if (tcp_addr_data->zero_read_limit++ ==
                    BMI_TCP_ZERO_READ_LIMIT)
                {
                    gossip_debug(GOSSIP_BMI_DEBUG_TCP,
                                 "...dropping connection.\n");
                    tcp_forget_addr(map, 0, bmi_tcp_errno_to_pvfs(-EPIPE));
                }
	    }
            else
            {
                tcp_addr_data->zero_read_limit = 0;
            }
	    return(ret);
	}
    }

    /* let's see if a the entire header is ready to be received.  If so
     * we will go ahead and pull it.  Otherwise, we will try again later.
     * It isn't worth the complication of reading only a partial message
     * header - we really want it atomically
     */
    ret = BMI_sockio_nbpeek(tcp_addr_data->socket,
                            new_header.enc_hdr, TCP_ENC_HDR_SIZE);
    if (ret < 0)
    {
	tcp_forget_addr(map, 0, bmi_tcp_errno_to_pvfs(-errno));
	return (0);
    }

    if (ret == 0)
    {
        gossip_debug(
            GOSSIP_BMI_DEBUG_TCP, "Warning: bmi_tcp unable "
            "to recv any data reported by poll(). [2]\n");

        if (tcp_addr_data->zero_read_limit++ ==
            BMI_TCP_ZERO_READ_LIMIT)
        {
            gossip_debug(GOSSIP_BMI_DEBUG_TCP,
                         "...dropping connection.\n");
            tcp_forget_addr(map, 0, bmi_tcp_errno_to_pvfs(-EPIPE));
        }
	return(0);
    }
    else
    {
        tcp_addr_data->zero_read_limit = 0;
    }

    if (ret < TCP_ENC_HDR_SIZE)
    {
        current_time = time(NULL);
        if(!tcp_addr_data->short_header_timer)
        {
            tcp_addr_data->short_header_timer = current_time;
        }
        else if((current_time - tcp_addr_data->short_header_timer) > 
            BMI_TCP_HEADER_WAIT_SECONDS)
        {
	    gossip_err("Error: incomplete BMI TCP header after %d seconds, closing connection.\n",
                BMI_TCP_HEADER_WAIT_SECONDS);
            tcp_forget_addr(map, 0, bmi_tcp_errno_to_pvfs(-EPIPE));
            return (0);
        }

	/* header not ready yet, but we will keep hoping */
	return (0);
    }

    tcp_addr_data->short_header_timer = 0;
    *stall_flag = 0;
    gossip_ldebug(GOSSIP_BMI_DEBUG_TCP, "Reading header for new op.\n");
    ret = BMI_sockio_nbrecv(tcp_addr_data->socket,
                           new_header.enc_hdr, TCP_ENC_HDR_SIZE);
    if (ret < TCP_ENC_HDR_SIZE)
    {
	tmp_errno = errno;
	gossip_err("Error: BMI_sockio_nbrecv: %s\n", strerror(tmp_errno));
	tcp_forget_addr(map, 0, bmi_tcp_errno_to_pvfs(-tmp_errno));
	return (0);
    }

    /* decode the header */
    BMI_TCP_DEC_HDR(new_header);

    /* so we have the header. now what?  These are the possible
     * scenarios:
     * a) unexpected message
     * b) eager message for which a recv has been posted
     * c) eager message for which a recv has not been posted
     * d) rendezvous messsage for which a recv has been posted
     * e) rendezvous messsage for which a recv has not been posted
     * f) eager message for which a rend. recv has been posted
     */

    /* check magic number of message */
    if(new_header.magic_nr != BMI_TCP_S2S_MAGIC_NR)
    {
	gossip_err("Error: Bad magic in BMI TCP message.\n");
	gossip_err("Error: This may be due to port scanning or communication between incompatible versions of BMI.\n");
	tcp_forget_addr(map, 0, bmi_tcp_errno_to_pvfs(-EBADMSG));
	return(0);
    }

    gossip_debug(GOSSIP_BMI_DEBUG_TCP,
        "Received a message from hashed address %u\n",
        new_header.src_addr_hash);

    if(tcp_addr_data->addr_hash == 0)
    {
        struct qhash_head* tmp_link;
        struct tcp_addr* found_tcp_addr_data = NULL;
        bmi_method_addr_p found_map = NULL;

        /* This is the first incoming message on a new socket */
        if(new_header.src_addr_hash == 0)
        {
            /* client connection; there is no identifier in the header */
            /* register this address with the method control layer */
            tcp_addr_data->bmi_addr = bmi_method_addr_reg_callback(map);
            if (ret < 0)
            {
                tcp_shutdown_addr(map);
                dealloc_tcp_method_addr(map);
                return (ret);
            }
        }
        else
        {
            /* server connection; search to see if we can find an address 
             * that we already explicitly resolved to this host 
             */
            tmp_link = qhash_search(addr_hash_table, &new_header.src_addr_hash);
            if(tmp_link)
            {
                /* we found a match; this host has already been looked up
                 * locally
                 */
                found_tcp_addr_data = qlist_entry(tmp_link, struct
                    tcp_addr, hash_link);
                found_map = found_tcp_addr_data->parent;

                /* link the two addresses together */
                found_map->secondary = map;
                map->primary = found_map;
            }
            else
            {
                /* No lookups on this host yet. */
                tcp_addr_data->addr_hash = new_header.src_addr_hash;
                /* add entry to hash table so we can find it later */
                qhash_add(addr_hash_table, &tcp_addr_data->addr_hash,
                    &tcp_addr_data->hash_link);
                /* register this address with the method control layer */
                tcp_addr_data->bmi_addr = bmi_method_addr_reg_callback(map);
                if (ret < 0)
                {
                    tcp_shutdown_addr(map);
                    dealloc_tcp_method_addr(map);
                    return (ret);
                }
            }
        }
    }

    gossip_ldebug(GOSSIP_BMI_DEBUG_TCP, "Received new message; mode: %d.\n",
		  (int) new_header.mode);
    gossip_ldebug(GOSSIP_BMI_DEBUG_TCP, "tag: %d\n", (int) new_header.tag);

    if (new_header.mode == TCP_MODE_UNEXP)
    {
	/* allocate the operation structure */
	active_method_op = alloc_tcp_method_op();
	if (!active_method_op)
	{
	    tcp_forget_addr(map, 0, bmi_tcp_errno_to_pvfs(-ENOMEM));
	    return (bmi_tcp_errno_to_pvfs(-ENOMEM));
	}
	/* create data buffer */
	new_buffer = malloc(new_header.size);
	if (!new_buffer)
	{
	    dealloc_tcp_method_op(active_method_op);
	    tcp_forget_addr(map, 0, bmi_tcp_errno_to_pvfs(-ENOMEM));
	    return (bmi_tcp_errno_to_pvfs(-ENOMEM));
	}

	/* set the fields */
	active_method_op->send_recv = BMI_RECV;
	active_method_op->addr = map;
	active_method_op->actual_size = new_header.size;
	active_method_op->expected_size = 0;
	active_method_op->amt_complete = 0;
	active_method_op->env_amt_complete = TCP_ENC_HDR_SIZE;
	active_method_op->msg_tag = new_header.tag;
	active_method_op->buffer = new_buffer;
	active_method_op->mode = TCP_MODE_UNEXP;
	active_method_op->buffer_list = &(active_method_op->buffer);
	active_method_op->size_list = &(active_method_op->actual_size);
	active_method_op->list_count = 1;
        active_method_op->class = new_header.class;
	tcp_op_data = active_method_op->method_data;
	tcp_op_data->tcp_op_state = BMI_TCP_INPROGRESS;
	tcp_op_data->env = new_header;

	op_list_add(op_list_array[IND_RECV_INFLIGHT], active_method_op);
	/* grab some data if we can */
	return (work_on_recv_op(active_method_op, &tmp));
    }

    memset(&key, 0, sizeof(struct op_list_search_key));
    key.method_addr = map;
    key.method_addr_yes = 1;
    key.msg_tag = new_header.tag;
    key.msg_tag_yes = 1;

    /* look for a match within the posted operations */
    active_method_op = op_list_search(op_list_array[IND_RECV], &key);

    if (active_method_op)
    {
	/* make sure it isn't too big */
	if (new_header.size > active_method_op->expected_size)
	{
	    gossip_err("Error: message ordering violation;\n");
	    gossip_err("Error: message too large for next buffer.\n");
	    gossip_err("Error: incoming size: %ld, expected size: %ld\n",
			(long) new_header.size,
			(long) active_method_op->expected_size);
	    /* TODO: return error here or do something else? */
	    return (bmi_tcp_errno_to_pvfs(-EPROTO));
	}

	/* we found a match.  go work on it and return */
	op_list_remove(active_method_op);
	active_method_op->env_amt_complete = TCP_ENC_HDR_SIZE;
	active_method_op->actual_size = new_header.size;
	op_list_add(op_list_array[IND_RECV_INFLIGHT], active_method_op);
	return (work_on_recv_op(active_method_op, &tmp));
    }

    /* no match anywhere.  Start a new operation */
    /* allocate the operation structure */
    active_method_op = alloc_tcp_method_op();
    if (!active_method_op)
    {
	tcp_forget_addr(map, 0, bmi_tcp_errno_to_pvfs(-ENOMEM));
	return (bmi_tcp_errno_to_pvfs(-ENOMEM));
    }

    if (new_header.mode == TCP_MODE_EAGER)
    {
	/* create data buffer for eager messages */
	new_buffer = malloc(new_header.size);
	if (!new_buffer)
	{
	    dealloc_tcp_method_op(active_method_op);
	    tcp_forget_addr(map, 0, bmi_tcp_errno_to_pvfs(-ENOMEM));
	    return (bmi_tcp_errno_to_pvfs(-ENOMEM));
	}
    }
    else
    {
	new_buffer = NULL;
    }

    /* set the fields */
    active_method_op->send_recv = BMI_RECV;
    active_method_op->addr = map;
    active_method_op->actual_size = new_header.size;
    active_method_op->expected_size = 0;
    active_method_op->amt_complete = 0;
    active_method_op->env_amt_complete = TCP_ENC_HDR_SIZE;
    active_method_op->msg_tag = new_header.tag;
    active_method_op->buffer = new_buffer;
    active_method_op->mode = new_header.mode;
    active_method_op->buffer_list = &(active_method_op->buffer);
    active_method_op->size_list = &(active_method_op->actual_size);
    active_method_op->list_count = 1;
    tcp_op_data = active_method_op->method_data;
    tcp_op_data->tcp_op_state = BMI_TCP_BUFFERING;
    tcp_op_data->env = new_header;

    op_list_add(op_list_array[IND_RECV_INFLIGHT], active_method_op);

    /* grab some data if we can */
    if (new_header.mode == TCP_MODE_EAGER)
    {
	return (work_on_recv_op(active_method_op, &tmp));
    }

    return (0);
}


/*
 * work_on_send_op()
 *
 * used to perform work on a send operation.  this is called by the poll
 * function.
 * 
 * sets blocked_flag if no more work can be done on socket without
 * blocking
 * returns 0 on success, -errno on failure.
 */
static int work_on_send_op(method_op_p my_method_op,
			   int *blocked_flag, int* stall_flag)
{
    int ret = -1;
    struct tcp_addr *tcp_addr_data = my_method_op->addr->method_data;
    struct tcp_op *tcp_op_data = my_method_op->method_data;

    *blocked_flag = 1;
    *stall_flag = 0;

    /* make sure that the connection is done before we continue */
    if (tcp_addr_data->not_connected)
    {
	ret = tcp_sock_init(my_method_op->addr);
	if (ret < 0)
	{
            PVFS_perror_gossip("Error: socket failed to init", ret);
            /* tcp_sock_init() returns BMI error code */
	    tcp_forget_addr(my_method_op->addr, 0, ret);
	    return (0);
	}
	if (tcp_addr_data->not_connected)
	{
	    /* try again later- still could not connect */
	    tcp_op_data->tcp_op_state = BMI_TCP_INPROGRESS;
	    return (0);
	}
    }

    ret = payload_progress(tcp_addr_data->socket,
	my_method_op->buffer_list,
	my_method_op->size_list,
	my_method_op->list_count,
	my_method_op->actual_size,
	&(my_method_op->list_index),
	&(my_method_op->cur_index_complete),
	BMI_SEND,
	tcp_op_data->env.enc_hdr,
	&my_method_op->env_amt_complete);
    if (ret < 0)
    {
        PVFS_perror_gossip("Error: payload_progress", ret);
        /* payload_progress() returns BMI error codes */
	tcp_forget_addr(my_method_op->addr, 0, ret);
	return (0);
    }

    if(ret == 0)
	*stall_flag = 1;

    gossip_ldebug(GOSSIP_BMI_DEBUG_TCP, "Sent: %d bytes of data.\n", ret);
    my_method_op->amt_complete += ret;
    assert(my_method_op->amt_complete <= my_method_op->actual_size);

    if (my_method_op->amt_complete == my_method_op->actual_size && my_method_op->env_amt_complete == TCP_ENC_HDR_SIZE)
    {
	/* we are done */
	my_method_op->error_code = 0;
	BMI_socket_collection_remove_write_bit(tcp_socket_collection_p,
					   my_method_op->addr);
	op_list_remove(my_method_op);
	((struct tcp_op*)(my_method_op->method_data))->tcp_op_state = 
	    BMI_TCP_COMPLETE;
	op_list_add(completion_array[my_method_op->context_id], my_method_op);
	*blocked_flag = 0;
    }
    else
    {
	/* there is still more work to do */
	tcp_op_data->tcp_op_state = BMI_TCP_INPROGRESS;
    }

    return (0);
}


/*
 * work_on_recv_op()
 *
 * used to perform work on a recv operation.  this is called by the poll
 * function.
 * NOTE: this function assumes the method header has already been read.
 *
 * returns 0 on success, -errno on failure.
 */
static int work_on_recv_op(method_op_p my_method_op, int* stall_flag)
{

    int ret = -1;
    struct tcp_addr *tcp_addr_data = my_method_op->addr->method_data;
    struct tcp_op *tcp_op_data = my_method_op->method_data;

    *stall_flag = 1;

    if (my_method_op->actual_size != 0)
    {
	/* now let's try to recv some actual data */
	ret = payload_progress(tcp_addr_data->socket,
	    my_method_op->buffer_list,
	    my_method_op->size_list,
	    my_method_op->list_count,
	    my_method_op->actual_size,
	    &(my_method_op->list_index),
	    &(my_method_op->cur_index_complete),
	    BMI_RECV,
	    NULL,
	    0);
	if (ret < 0)
	{
            PVFS_perror_gossip("Error: payload_progress", ret);
            /* payload_progress() returns BMI error codes */
	    tcp_forget_addr(my_method_op->addr, 0, ret);
	    return (0);
	}
    }
    else
    {
	ret = 0;
    }

    if(ret > 0)
	*stall_flag = 0;

    my_method_op->amt_complete += ret;
    assert(my_method_op->amt_complete <= my_method_op->actual_size);

    if (my_method_op->amt_complete == my_method_op->actual_size)
    {
	/* we are done */
	op_list_remove(my_method_op);
	if (tcp_op_data->tcp_op_state == BMI_TCP_BUFFERING)
	{
	    /* queue up to wait on matching post recv */
	    op_list_add(op_list_array[IND_RECV_EAGER_DONE_BUFFERING],
			my_method_op);
	}
	else
	{
	    my_method_op->error_code = 0;
	    if (my_method_op->mode == TCP_MODE_UNEXP)
	    {
		op_list_add(op_list_array[IND_COMPLETE_RECV_UNEXP],
			    my_method_op);
	    }
	    else
	    {
		((struct tcp_op*)(my_method_op->method_data))->tcp_op_state = 
		    BMI_TCP_COMPLETE;
		op_list_add(completion_array[my_method_op->context_id], my_method_op);
	    }
	}
    }

    return (0);
}


/* tcp_do_work_error()
 * 
 * handles a tcp address that has indicated an error during polling.
 *
 * returns 0 on success, -errno on failure
 */
static int tcp_do_work_error(bmi_method_addr_p map)
{
    struct tcp_addr *tcp_addr_data = NULL;
    int buf;
    int ret;
    int tmp_errno;

    tcp_addr_data = map->method_data;

    /* perform a read on the socket so that we can get a real errno */
    ret = read(tcp_addr_data->socket, &buf, sizeof(int));
    if (ret == 0)
        tmp_errno = EPIPE;  /* report other side closed socket with this */
    else
        tmp_errno = errno;

    gossip_debug(GOSSIP_BMI_DEBUG_TCP, "Error: bmi_tcp: %s\n",
      strerror(tmp_errno));

    if (tcp_addr_data->server_port)
    {
	/* Ignore this and hope it goes away... we don't want to lose
	 * our local socket */
	dealloc_tcp_method_addr(map);
	gossip_lerr("Warning: error polling on server socket, continuing.\n");
	return (0);
    }

    if(tmp_errno == 0)
	tmp_errno = EPROTO;

    tcp_forget_addr(map, 0, bmi_tcp_errno_to_pvfs(-tmp_errno));

    return (0);
}

#if defined(USE_TRUSTED) && defined(__PVFS2_CLIENT__)
/*
 * tcp_enable_trusted()
 * Ideally, this function should look up the security configuration of
 * the server and determines
 * if it needs to bind to any specific port locally or not..
 * For now look at the FIXME below.
 */
static int tcp_enable_trusted(struct tcp_addr *tcp_addr_data)
{
    /*
     * FIXME:
     * For now, there is no way for us to check if a given
     * server is actually using port protection or not.
     * For now we unconditionally use a trusted port range
     * as long as USE_TRUSTED is #defined.
     *
     * Although most of the time we expect users
     * to be using a range of 0-1024, it is hard to keep probing
     * until one gets a port in the range specified.
     * Hence this is a temporary fix. we will see if this
     * requirement even needs to be met at all.
     */
    static unsigned short my_requested_port = 1023;
    unsigned short my_local_port = 0;
    struct sockaddr_in my_local_sockaddr;
    socklen_t len = sizeof(struct sockaddr_in);
    memset(&my_local_sockaddr, 0, sizeof(struct sockaddr_in));

    /* setup for a fast restart to avoid bind addr in use errors */
    if (BMI_sockio_set_sockopt(tcp_addr_data->socket, SO_REUSEADDR, 1) < 0)
    {
        gossip_lerr("Could not set SO_REUSEADDR on local socket (port %hd)\n", my_local_port);
    }
    if (BMI_sockio_bind_sock(tcp_addr_data->socket, my_requested_port) < 0)
    {
        gossip_lerr("Could not bind to local port %hd: %s\n", 
                my_requested_port, strerror(errno));
    }
    else {
        my_requested_port--;
    }
    my_local_sockaddr.sin_family = AF_INET;
    if (getsockname(tcp_addr_data->socket, 
                (struct sockaddr *)&my_local_sockaddr, &len) == 0)
    {
        my_local_port = ntohs(my_local_sockaddr.sin_port);
    }
    gossip_debug(GOSSIP_BMI_DEBUG_TCP, "Bound locally to port: %hd\n", my_local_port);
    return 0;
}

#endif

#if defined(USE_TRUSTED) && defined(__PVFS2_SERVER__)

static char *bad_errors[] = {
    "invalid network address",
    "invalid port",
    "invalid network address and port"
};

/*
 * tcp_allow_trusted()
 * if trusted ports was enabled make sure
 * that we can accept a particular connection from a given
 * client
 */
static int tcp_allow_trusted(struct sockaddr_in *peer_sockaddr)
{
    char *peer_hostname = inet_ntoa(peer_sockaddr->sin_addr);
    unsigned short peer_port = ntohs(peer_sockaddr->sin_port);
    int   i, what_failed   = -1;

    /* Don't refuse connects if there were any
     * parse errors or if it is not enabled in the config file
     */
    if (gtcp_allowed_connection->port_enforce == 0
            && gtcp_allowed_connection->network_enforce == 0)
    {
        return 0;
    }
    /* make sure that the client is within the allowed network */
    if (gtcp_allowed_connection->network_enforce == 1)
    {
        /* Always allow localhost to connect */
        if (ntohl(peer_sockaddr->sin_addr.s_addr) == INADDR_LOOPBACK)
        {
            goto port_check;
        }
        for (i = 0; i < gtcp_allowed_connection->network_count; i++)
        {
            /* check with all the masks */
            if ((peer_sockaddr->sin_addr.s_addr & gtcp_allowed_connection->netmask[i].s_addr) 
                    != (gtcp_allowed_connection->network[i].s_addr & gtcp_allowed_connection->netmask[i].s_addr ))
            {
                continue;
            }
            else {
                goto port_check;
            }
        }
        /* not from a trusted network */
        what_failed = 0;
    }
port_check:
    /* make sure that the client port numbers are within specified limits */
    if (gtcp_allowed_connection->port_enforce == 1)
    {
        if (peer_port < gtcp_allowed_connection->ports[0]
                || peer_port > gtcp_allowed_connection->ports[1])
        {
            what_failed = (what_failed < 0) ? 1 : 2;
        }
    }
    /* okay, we are good to go */
    if (what_failed < 0)
    {
        return 0;
    }
    /* no good */
    gossip_err("Rejecting client %s on port %d: %s\n",
           peer_hostname, peer_port, bad_errors[what_failed]);
    return -1;
}

#endif

/* 
 * tcp_accept_init()
 * 
 * used to establish a connection from the server side.  Attempts an
 * accept call and provides the socket if it succeeds.
 *
 * returns 0 on success, -errno on failure.
 */
static int tcp_accept_init(int *socket, char** peer)
{

    int ret = -1;
    int tmp_errno = 0;
    struct tcp_addr *tcp_addr_data = tcp_method_params.listen_addr->method_data;
    int oldfl = 0;
    struct sockaddr_in peer_sockaddr;
    int peer_sockaddr_size = sizeof(struct sockaddr_in);
    char* tmp_peer;

    /* do we have a socket on this end yet? */
    if (tcp_addr_data->socket < 0)
    {
	ret = tcp_server_init();
	if (ret < 0)
	{
	    return (ret);
	}
    }

    *socket = accept(tcp_addr_data->socket, (struct sockaddr*)&peer_sockaddr,
              (socklen_t *)&peer_sockaddr_size);

    if (*socket < 0)
    {
	if ((errno == EAGAIN) ||
	    (errno == EWOULDBLOCK) ||
	    (errno == ENETDOWN) ||
	    (errno == EPROTO) ||
	    (errno == ENOPROTOOPT) ||
	    (errno == EHOSTDOWN) ||
	    (errno == ENONET) ||
            (errno == EHOSTUNREACH) ||
	    (errno == EOPNOTSUPP) ||
            (errno == ENETUNREACH) ||
            (errno == ENFILE) ||
            (errno == EMFILE))
	{
	    /* try again later */
            if ((errno == ENFILE) || (errno == EMFILE))
            {
	        gossip_err("Error: accept: %s (continuing)\n",strerror(errno));
                bmi_method_addr_drop_callback(BMI_tcp_method_name);
            }
	    return (0);
	}
	else
	{
	    gossip_err("Error: accept: %s\n", strerror(errno));
	    return (bmi_tcp_errno_to_pvfs(-errno));
	}
    }

#if defined(USE_TRUSTED) && defined(__PVFS2_SERVER__)

    /* make sure that we are allowed to accept this connection */
    if (tcp_allow_trusted(&peer_sockaddr) < 0)
    {
        /* Force closure of the connection */
        close(*socket);
        return (bmi_tcp_errno_to_pvfs(-EACCES));
    }

#endif

    /* we accepted a new connection.  turn off Nagle's algorithm. */
    if (BMI_sockio_set_tcpopt(*socket, TCP_NODELAY, 1) < 0)
    {
	tmp_errno = errno;
	gossip_lerr("Error: failed to set TCP_NODELAY option.\n");
	close(*socket);
	return (bmi_tcp_errno_to_pvfs(-tmp_errno));
    }

    /* set it to non-blocking operation */
    oldfl = fcntl(*socket, F_GETFL, 0);
    if (!(oldfl & O_NONBLOCK))
    {
	fcntl(*socket, F_SETFL, oldfl | O_NONBLOCK);
    }

    /* allocate ip address string */
    tmp_peer = inet_ntoa(peer_sockaddr.sin_addr);
    *peer = (char*)malloc(strlen(tmp_peer)+1);
    if(!(*peer))
    {
        close(*socket);
        return(bmi_tcp_errno_to_pvfs(-BMI_ENOMEM));
    }
    strcpy(*peer, tmp_peer);

    return (0);
}


/* alloc_tcp_method_op()
 *
 * creates a new method op with defaults filled in for tcp.
 *
 * returns pointer to structure on success, NULL on failure
 */
static method_op_p alloc_tcp_method_op(void)
{
    method_op_p my_method_op = NULL;

    my_method_op = bmi_alloc_method_op(sizeof(struct tcp_op));

    /* we trust alloc_method_op to zero it out */

    return (my_method_op);
}


/* dealloc_tcp_method_op()
 *
 * destroys an existing tcp method op, freeing segment lists if
 * needed
 *
 * no return value
 */
static void dealloc_tcp_method_op(method_op_p old_op)
{
    bmi_dealloc_method_op(old_op);
    return;
}

/* tcp_post_send_generic()
 * 
 * Submits send operations (low level).
 *
 * returns 0 on success that requires later poll, returns 1 on instant
 * completion, -errno on failure
 */
static int tcp_post_send_generic(bmi_op_id_t * id,
                                 bmi_method_addr_p dest,
                                 const void *const *buffer_list,
                                 const bmi_size_t *size_list,
                                 int list_count,
                                 enum bmi_buffer_type buffer_type,
                                 struct tcp_msg_header my_header,
                                 void *user_ptr,
                                 bmi_context_id context_id,
                                 PVFS_hint hints)
{
    struct tcp_addr *tcp_addr_data = dest->method_data;
    method_op_p query_op = NULL;
    int ret = -1;
    bmi_size_t total_size = 0;
    bmi_size_t amt_complete = 0;
    bmi_size_t env_amt_complete = 0;
    struct op_list_search_key key;
    int list_index = 0;
    bmi_size_t cur_index_complete = 0;
    PINT_event_id eid = 0;

    if(PINT_EVENT_ENABLED)
    {
        int i = 0;
        for(; i < list_count; ++i)
        {
            total_size += size_list[i];
        }
    }

    PINT_EVENT_START(
        bmi_tcp_send_event_id, bmi_tcp_pid, NULL, &eid,
        PINT_HINT_GET_CLIENT_ID(hints),
        PINT_HINT_GET_REQUEST_ID(hints),
        PINT_HINT_GET_RANK(hints),
        PINT_HINT_GET_HANDLE(hints),
        PINT_HINT_GET_OP_ID(hints),
        total_size);

    /* Three things can happen here:
     * a) another op is already in queue for the address, so we just
     * queue up
     * b) we can send the whole message and return
     * c) we send part of the message and queue the rest
     */

    /* NOTE: on the post_send side of an operation, it doesn't really
     * matter whether the op is going to be eager or rendezvous.  It is
     * handled the same way (except for how the header is filled in).
     * The difference is in the recv processing for TCP.
     */

    /* NOTE: we also don't care what the buffer_type says, TCP could care
     * less what buffers it is using.
     */

    /* encode the message header */
    BMI_TCP_ENC_HDR(my_header);

    /* the first thing we must do is find out if another send is queued
     * up for this address so that we don't mess up our ordering.    */
    memset(&key, 0, sizeof(struct op_list_search_key));
    key.method_addr = dest;
    key.method_addr_yes = 1;
    query_op = op_list_search(op_list_array[IND_SEND], &key);
    if (query_op)
    {
        /* queue up operation */
        ret = enqueue_operation(op_list_array[IND_SEND], BMI_SEND,
                                dest, (void **) buffer_list,
                                size_list, list_count, 0, 0,
                                id, BMI_TCP_INPROGRESS, my_header, user_ptr,
                                my_header.size, 0,
                                context_id,
                                eid);

        /* TODO: is this causing deadlocks?  See similar call in recv
         * path for another example.  This particular one seems to be an
         * issue under a heavy bonnie++ load that Neill has been
         * debugging.  Comment out for now to see if the problem goes
         * away.
         */
#if 0
	if (ret >= 0)
	{
	    /* go ahead and try to do some work while we are in this
	     * function since we appear to be backlogged.  Make sure that
	     * we do not wait in the poll, however.
	     */
	    ret = tcp_do_work(0);
	}
#endif
	if (ret < 0)
	{
	    gossip_err("Error: enqueue_operation() or tcp_do_work() returned: %d\n", ret);
	}
	return (ret);
    }

    /* make sure the connection is established */
    ret = tcp_sock_init(dest);
    if (ret < 0)
    {
	gossip_debug(GOSSIP_BMI_DEBUG_TCP, "tcp_sock_init() failure.\n");
        /* tcp_sock_init() returns BMI error code */
	tcp_forget_addr(dest, 0, ret);
        PINT_EVENT_END(bmi_tcp_send_event_id, bmi_tcp_pid, NULL, 0, ret);
	return (ret);
    }

    tcp_addr_data = dest->method_data;

#if 0
    /* TODO: this is a hack for testing! */
    /* disables immediate send completion... */
    ret = enqueue_operation(op_list_array[IND_SEND], BMI_SEND,
			    dest, buffer_list, size_list, list_count, 0, 0,
			    id, BMI_TCP_INPROGRESS, my_header, user_ptr,
			    my_header.size, 0,
			    context_id);
    return(ret);
#endif

    if (tcp_addr_data->not_connected)
    {
	/* if the connection is not completed, queue up for later work */
	ret = enqueue_operation(op_list_array[IND_SEND], BMI_SEND,
				dest, (void **) buffer_list, size_list,
				list_count, 0, 0,
				id, BMI_TCP_INPROGRESS, my_header, user_ptr,
				my_header.size, 0,
				context_id,
                                eid);
	if(ret < 0)
	{
	    gossip_err("Error: enqueue_operation() returned: %d\n", ret);
	}
	return (ret);
    }

    /* try to send some data */
    env_amt_complete = 0;
    ret = payload_progress(tcp_addr_data->socket,
	(void **) buffer_list,
	size_list, list_count, my_header.size, &list_index,
	&cur_index_complete, BMI_SEND, my_header.enc_hdr, &env_amt_complete);
    if (ret < 0)
    {
        PVFS_perror_gossip("Error: payload_progress", ret);
        /* payload_progress() returns BMI error codes */
	tcp_forget_addr(dest, 0, ret);
        PINT_EVENT_END(bmi_tcp_send_event_id, bmi_tcp_pid, NULL, eid, 0, ret);
	return (ret);
    }

    gossip_ldebug(GOSSIP_BMI_DEBUG_TCP, "Sent: %d bytes of data.\n", ret);
    amt_complete = ret;
    assert(amt_complete <= my_header.size);
    if (amt_complete == my_header.size && env_amt_complete == TCP_ENC_HDR_SIZE)
    {
        /* we are already done */
        PINT_EVENT_END(bmi_tcp_send_event_id, bmi_tcp_pid,
                       NULL, eid, 0, amt_complete);
        return (1);
    }

    /* queue up the remainder */
    ret = enqueue_operation(op_list_array[IND_SEND], BMI_SEND,
                            dest, (void **) buffer_list,
                            size_list, list_count,
                            amt_complete, env_amt_complete, id,
                            BMI_TCP_INPROGRESS, my_header, user_ptr,
                            my_header.size, 0, context_id, eid);

    if(ret < 0)
    {
        gossip_err("Error: enqueue_operation() returned: %d\n", ret);
    }
    return (ret);
}


/* payload_progress()
 *
 * makes progress on sending/recving data payload portion of a message
 *
 * returns amount completed on success, -errno on failure
 */
static int payload_progress(int s, void *const *buffer_list, const bmi_size_t* 
    size_list, int list_count, bmi_size_t total_size, int* list_index, 
    bmi_size_t* current_index_complete, enum bmi_op_type send_recv, 
    char* enc_hdr, bmi_size_t* env_amt_complete)
{
    int i;
    int count = 0;
    int ret;
    int completed;
    /* used for finding the stopping point on short receives */
    int final_index = list_count-1;
    bmi_size_t final_size = size_list[list_count-1];
    bmi_size_t sum = 0;
    int vector_index = 0;
    int header_flag = 0;
    int tmp_env_done = 0;

    if(send_recv == BMI_RECV)
    {
	/* find out if we should stop short in list processing */
	for(i=0; i<list_count; i++)
	{
	    sum += size_list[i];
	    if(sum >= total_size)
	    {
		final_index = i;
		final_size = size_list[i] - (sum-total_size);
		break;
	    }
	}
    }

    assert(list_count > *list_index);

    /* make sure we don't overrun our preallocated iovec array */
    if((list_count - (*list_index)) > BMI_TCP_IOV_COUNT)
    {
	list_count = (*list_index) + BMI_TCP_IOV_COUNT;
    }

    /* do we need to send any of the header? */
    if(send_recv == BMI_SEND && *env_amt_complete < TCP_ENC_HDR_SIZE)
    {
	stat_io_vector[vector_index].iov_base = &enc_hdr[*env_amt_complete];
	stat_io_vector[vector_index].iov_len = TCP_ENC_HDR_SIZE - *env_amt_complete;
	count++;
	vector_index++;
	header_flag = 1;
    }

    /* setup vector */
    stat_io_vector[vector_index].iov_base = 
	(char*)buffer_list[*list_index] + *current_index_complete;
    count++;
    if(final_index == 0)
    {
	stat_io_vector[vector_index].iov_len = final_size - *current_index_complete;
    }
    else
    {
	stat_io_vector[vector_index].iov_len = 
	    size_list[*list_index] - *current_index_complete;
	for(i = (*list_index + 1); i < list_count; i++)
	{
	    vector_index++;
	    count++;
	    stat_io_vector[vector_index].iov_base = buffer_list[i];
	    if(i == final_index)
	    {
		stat_io_vector[vector_index].iov_len = final_size;
		break;
	    }
	    else
	    {
		stat_io_vector[vector_index].iov_len = size_list[i];
	    }
	}
    }

    assert(count > 0);

    if(send_recv == BMI_RECV)
    {
	ret = BMI_sockio_nbvector(s, stat_io_vector, count, 1);
    }
    else
    {
	ret = BMI_sockio_nbvector(s, stat_io_vector, count, 0);
    }

    /* if error or nothing done, return now */
    if(ret == 0)
	return(0);
    if(ret <= 0)
	return(bmi_tcp_errno_to_pvfs(-errno));

    completed = ret;
    if(header_flag && (completed >= 0))
    {
	/* take care of completed header status */
	tmp_env_done = TCP_ENC_HDR_SIZE - *env_amt_complete;
	if(tmp_env_done > completed)
	    tmp_env_done = completed;
	completed -= tmp_env_done;
	ret -= tmp_env_done;
	(*env_amt_complete) += tmp_env_done;
    }

    i=header_flag;
    while(completed > 0)
    {
	/* take care of completed data payload */
	if(completed >= stat_io_vector[i].iov_len)
	{
	    completed -= stat_io_vector[i].iov_len;
	    *current_index_complete = 0;
	    (*list_index)++;
	    i++;
	}
	else
	{
	    *current_index_complete += completed;
	    completed = 0;
	}
    }

    return(ret);
}

static void bmi_set_sock_buffers(int socket){
	//Set socket buffer sizes:
	gossip_debug(GOSSIP_BMI_DEBUG_TCP, "Default socket buffers send:%d receive:%d\n",
		GET_SENDBUFSIZE(socket), GET_RECVBUFSIZE(socket));
	gossip_debug(GOSSIP_BMI_DEBUG_TCP, "Setting socket buffer size for send:%d receive:%d \n",
		tcp_buffer_size_send, tcp_buffer_size_receive);
    if( tcp_buffer_size_receive != 0)
         SET_RECVBUFSIZE(socket,tcp_buffer_size_receive);
    if( tcp_buffer_size_send != 0)
         SET_SENDBUFSIZE(socket,tcp_buffer_size_send);
	gossip_debug(GOSSIP_BMI_DEBUG_TCP, "Reread socket buffers send:%d receive:%d\n",
		GET_SENDBUFSIZE(socket), GET_RECVBUFSIZE(socket));
}

/*
 * My best guess at if you are big-endian or little-endian.  This may
 * need adjustment.
 */
#if (defined(__BYTE_ORDER) && defined(__LITTLE_ENDIAN) && \
     __BYTE_ORDER == __LITTLE_ENDIAN) || \
    (defined(i386) || defined(__i386__) || defined(__i486__) || \
     defined(__i586__) || defined(__i686__) || defined(vax) || defined(MIPSEL))
# define HASH_LITTLE_ENDIAN 1
# define HASH_BIG_ENDIAN 0
#elif (defined(__BYTE_ORDER) && defined(__BIG_ENDIAN) && \
       __BYTE_ORDER == __BIG_ENDIAN) || \
      (defined(sparc) || defined(POWERPC) || defined(mc68000) || defined(sel))
# define HASH_LITTLE_ENDIAN 0
# define HASH_BIG_ENDIAN 1
#else
# define HASH_LITTLE_ENDIAN 0
# define HASH_BIG_ENDIAN 0
#endif

#define rot(x,k) (((x)<<(k)) | ((x)>>(32-(k))))

/*
-------------------------------------------------------------------------------
mix -- mix 3 32-bit values reversibly.

This is reversible, so any information in (a,b,c) before mix() is
still in (a,b,c) after mix().

If four pairs of (a,b,c) inputs are run through mix(), or through
mix() in reverse, there are at least 32 bits of the output that
are sometimes the same for one pair and different for another pair.
This was tested for:
* pairs that differed by one bit, by two bits, in any combination
  of top bits of (a,b,c), or in any combination of bottom bits of
  (a,b,c).
* "differ" is defined as +, -, ^, or ~^.  For + and -, I transformed
  the output delta to a Gray code (a^(a>>1)) so a string of 1's (as
  is commonly produced by subtraction) look like a single 1-bit
  difference.
* the base values were pseudorandom, all zero but one bit set, or 
  all zero plus a counter that starts at zero.

Some k values for my "a-=c; a^=rot(c,k); c+=b;" arrangement that
satisfy this are
    4  6  8 16 19  4
    9 15  3 18 27 15
   14  9  3  7 17  3
Well, "9 15 3 18 27 15" didn't quite get 32 bits diffing
for "differ" defined as + with a one-bit base and a two-bit delta.  I
used http://burtleburtle.net/bob/hash/avalanche.html to choose 
the operations, constants, and arrangements of the variables.

This does not achieve avalanche.  There are input bits of (a,b,c)
that fail to affect some output bits of (a,b,c), especially of a.  The
most thoroughly mixed value is c, but it doesn't really even achieve
avalanche in c.

This allows some parallelism.  Read-after-writes are good at doubling
the number of bits affected, so the goal of mixing pulls in the opposite
direction as the goal of parallelism.  I did what I could.  Rotates
seem to cost as much as shifts on every machine I could lay my hands
on, and rotates are much kinder to the top and bottom bits, so I used
rotates.
-------------------------------------------------------------------------------
*/
#define mix(a,b,c) \
{ \
  a -= c;  a ^= rot(c, 4);  c += b; \
  b -= a;  b ^= rot(a, 6);  a += c; \
  c -= b;  c ^= rot(b, 8);  b += a; \
  a -= c;  a ^= rot(c,16);  c += b; \
  b -= a;  b ^= rot(a,19);  a += c; \
  c -= b;  c ^= rot(b, 4);  b += a; \
}

/*
-------------------------------------------------------------------------------
final -- final mixing of 3 32-bit values (a,b,c) into c

Pairs of (a,b,c) values differing in only a few bits will usually
produce values of c that look totally different.  This was tested for
* pairs that differed by one bit, by two bits, in any combination
  of top bits of (a,b,c), or in any combination of bottom bits of
  (a,b,c).
* "differ" is defined as +, -, ^, or ~^.  For + and -, I transformed
  the output delta to a Gray code (a^(a>>1)) so a string of 1's (as
  is commonly produced by subtraction) look like a single 1-bit
  difference.
* the base values were pseudorandom, all zero but one bit set, or 
  all zero plus a counter that starts at zero.

These constants passed:
 14 11 25 16 4 14 24
 12 14 25 16 4 14 24
and these came close:
  4  8 15 26 3 22 24
 10  8 15 26 3 22 24
 11  8 15 26 3 22 24
-------------------------------------------------------------------------------
*/
#define final(a,b,c) \
{ \
  c ^= b; c -= rot(b,14); \
  a ^= c; a -= rot(c,11); \
  b ^= a; b -= rot(a,25); \
  c ^= b; c -= rot(b,16); \
  a ^= c; a -= rot(c,4);  \
  b ^= a; b -= rot(a,14); \
  c ^= b; c -= rot(b,24); \
}


/*
-------------------------------------------------------------------------------
hashlittle() -- hash a variable-length key into a 32-bit value
  k       : the key (the unaligned variable-length array of bytes)
  length  : the length of the key, counting by bytes
  initval : can be any 4-byte value
Returns a 32-bit value.  Every bit of the key affects every bit of
the return value.  Two keys differing by one or two bits will have
totally different hash values.

The best hash table sizes are powers of 2.  There is no need to do
mod a prime (mod is sooo slow!).  If you need less than 32 bits,
use a bitmask.  For example, if you need only 10 bits, do
  h = (h & hashmask(10));
In which case, the hash table should have hashsize(10) elements.

If you are hashing n strings (uint8_t **)k, do it like this:
  for (i=0, h=0; i<n; ++i) h = hashlittle( k[i], len[i], h);

By Bob Jenkins, 2006.  bob_jenkins@burtleburtle.net.  You may use this
code any way you wish, private, educational, or commercial.  It's free.

Use for hash table lookup, or anything where one collision in 2^^32 is
acceptable.  Do NOT use for cryptographic purposes.
-------------------------------------------------------------------------------
*/
static uint32_t hashlittle( const void *key, size_t length, uint32_t initval)
{
  uint32_t a,b,c;                                          /* internal state */
  union { const void *ptr; size_t i; } u;     /* needed for Mac Powerbook G4 */

  /* Set up the internal state */
  a = b = c = 0xdeadbeef + ((uint32_t)length) + initval;

  u.ptr = key;
  if (HASH_LITTLE_ENDIAN && ((u.i & 0x3) == 0)) {
    const uint32_t *k = (const uint32_t *)key;         /* read 32-bit chunks */
#ifdef VALGRIND
    const uint8_t  *k8;
#endif

    /*------ all but last block: aligned reads and affect 32 bits of (a,b,c) */
    while (length > 12)
    {
      a += k[0];
      b += k[1];
      c += k[2];
      mix(a,b,c);
      length -= 12;
      k += 3;
    }

    /*----------------------------- handle the last (probably partial) block */
    /* 
     * "k[2]&0xffffff" actually reads beyond the end of the string, but
     * then masks off the part it's not allowed to read.  Because the
     * string is aligned, the masked-off tail is in the same word as the
     * rest of the string.  Every machine with memory protection I've seen
     * does it on word boundaries, so is OK with this.  But VALGRIND will
     * still catch it and complain.  The masking trick does make the hash
     * noticably faster for short strings (like English words).
     */
#ifndef VALGRIND

    switch(length)
    {
    case 12: c+=k[2]; b+=k[1]; a+=k[0]; break;
    case 11: c+=k[2]&0xffffff; b+=k[1]; a+=k[0]; break;
    case 10: c+=k[2]&0xffff; b+=k[1]; a+=k[0]; break;
    case 9 : c+=k[2]&0xff; b+=k[1]; a+=k[0]; break;
    case 8 : b+=k[1]; a+=k[0]; break;
    case 7 : b+=k[1]&0xffffff; a+=k[0]; break;
    case 6 : b+=k[1]&0xffff; a+=k[0]; break;
    case 5 : b+=k[1]&0xff; a+=k[0]; break;
    case 4 : a+=k[0]; break;
    case 3 : a+=k[0]&0xffffff; break;
    case 2 : a+=k[0]&0xffff; break;
    case 1 : a+=k[0]&0xff; break;
    case 0 : return c;              /* zero length strings require no mixing */
    }

#else /* make valgrind happy */

    k8 = (const uint8_t *)k;
    switch(length)
    {
    case 12: c+=k[2]; b+=k[1]; a+=k[0]; break;
    case 11: c+=((uint32_t)k8[10])<<16;  /* fall through */
    case 10: c+=((uint32_t)k8[9])<<8;    /* fall through */
    case 9 : c+=k8[8];                   /* fall through */
    case 8 : b+=k[1]; a+=k[0]; break;
    case 7 : b+=((uint32_t)k8[6])<<16;   /* fall through */
    case 6 : b+=((uint32_t)k8[5])<<8;    /* fall through */
    case 5 : b+=k8[4];                   /* fall through */
    case 4 : a+=k[0]; break;
    case 3 : a+=((uint32_t)k8[2])<<16;   /* fall through */
    case 2 : a+=((uint32_t)k8[1])<<8;    /* fall through */
    case 1 : a+=k8[0]; break;
    case 0 : return c;
    }

#endif /* !valgrind */

  } else if (HASH_LITTLE_ENDIAN && ((u.i & 0x1) == 0)) {
    const uint16_t *k = (const uint16_t *)key;         /* read 16-bit chunks */
    const uint8_t  *k8;

    /*--------------- all but last block: aligned reads and different mixing */
    while (length > 12)
    {
      a += k[0] + (((uint32_t)k[1])<<16);
      b += k[2] + (((uint32_t)k[3])<<16);
      c += k[4] + (((uint32_t)k[5])<<16);
      mix(a,b,c);
      length -= 12;
      k += 6;
    }

    /*----------------------------- handle the last (probably partial) block */
    k8 = (const uint8_t *)k;
    switch(length)
    {
    case 12: c+=k[4]+(((uint32_t)k[5])<<16);
             b+=k[2]+(((uint32_t)k[3])<<16);
             a+=k[0]+(((uint32_t)k[1])<<16);
             break;
    case 11: c+=((uint32_t)k8[10])<<16;     /* fall through */
    case 10: c+=k[4];
             b+=k[2]+(((uint32_t)k[3])<<16);
             a+=k[0]+(((uint32_t)k[1])<<16);
             break;
    case 9 : c+=k8[8];                      /* fall through */
    case 8 : b+=k[2]+(((uint32_t)k[3])<<16);
             a+=k[0]+(((uint32_t)k[1])<<16);
             break;
    case 7 : b+=((uint32_t)k8[6])<<16;      /* fall through */
    case 6 : b+=k[2];
             a+=k[0]+(((uint32_t)k[1])<<16);
             break;
    case 5 : b+=k8[4];                      /* fall through */
    case 4 : a+=k[0]+(((uint32_t)k[1])<<16);
             break;
    case 3 : a+=((uint32_t)k8[2])<<16;      /* fall through */
    case 2 : a+=k[0];
             break;
    case 1 : a+=k8[0];
             break;
    case 0 : return c;                     /* zero length requires no mixing */
    }

  } else {                        /* need to read the key one byte at a time */
    const uint8_t *k = (const uint8_t *)key;

    /*--------------- all but the last block: affect some 32 bits of (a,b,c) */
    while (length > 12)
    {
      a += k[0];
      a += ((uint32_t)k[1])<<8;
      a += ((uint32_t)k[2])<<16;
      a += ((uint32_t)k[3])<<24;
      b += k[4];
      b += ((uint32_t)k[5])<<8;
      b += ((uint32_t)k[6])<<16;
      b += ((uint32_t)k[7])<<24;
      c += k[8];
      c += ((uint32_t)k[9])<<8;
      c += ((uint32_t)k[10])<<16;
      c += ((uint32_t)k[11])<<24;
      mix(a,b,c);
      length -= 12;
      k += 12;
    }

    /*-------------------------------- last block: affect all 32 bits of (c) */
    switch(length)                   /* all the case statements fall through */
    {
    case 12: c+=((uint32_t)k[11])<<24;
    case 11: c+=((uint32_t)k[10])<<16;
    case 10: c+=((uint32_t)k[9])<<8;
    case 9 : c+=k[8];
    case 8 : b+=((uint32_t)k[7])<<24;
    case 7 : b+=((uint32_t)k[6])<<16;
    case 6 : b+=((uint32_t)k[5])<<8;
    case 5 : b+=k[4];
    case 4 : a+=((uint32_t)k[3])<<24;
    case 3 : a+=((uint32_t)k[2])<<16;
    case 2 : a+=((uint32_t)k[1])<<8;
    case 1 : a+=k[0];
             break;
    case 0 : return c;
    }
  }

  final(a,b,c);
  return c;
}

static int addr_hash_compare(void* key, struct qhash_head* link)
{
    uint32_t *addr_hash = key;
    struct tcp_addr *tcp_addr_data = NULL;

    tcp_addr_data = qhash_entry(link, struct tcp_addr, hash_link);
    assert(tcp_addr_data);

    if(tcp_addr_data->addr_hash == *addr_hash)
    {
        return(1);
    }
    return(0);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
