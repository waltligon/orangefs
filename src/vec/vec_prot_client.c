/*
 * (C) 2006 The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "vec_config.h"
#include "vec_prot.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <rpc/rpc.h>
#include <rpc/clnt.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "gossip.h"
#include "rpcutils.h"
#include "pvfs2-debug.h"
#include "vec_prot_client.h"
#include "vec_common.h"

static pthread_mutex_t vec_mutex = PTHREAD_MUTEX_INITIALIZER;
typedef struct vec_entry vec_entry;

struct vec_entry {
	int used;
	struct sockaddr_in addr;
	int tcp_prog_number, udp_prog_number;
	int tcp_version, udp_version;
	CLIENT *clnt;
	struct sockaddr_in our_addr;
};

enum {USED = 1, UNUSED = 0};
static vec_entry vec_table[VEC_MAX_SERVERS];
static int vec_count = 0;

/* 
 * This variable determines whether or not CLIENT handles to vec servers
 * need to be cached.
 * i.e we cache handles if it is set to 1
 * and re-connect everytime if it is set to 0.
 * it is a good thing if the client-side daemon caches CLIENT handles to
 * vec servers because we dont expect them to die...
 */
static int cache_handle_policy = 1;

static int convert_to_errno(enum clnt_stat rpc_error)
{
	if (rpc_error == RPC_SUCCESS) {
		return 0;
	}
	if (rpc_error == RPC_CANTDECODEARGS
			|| rpc_error == RPC_CANTDECODEARGS
			|| rpc_error == RPC_CANTSEND
			|| rpc_error == RPC_CANTRECV
			|| rpc_error == RPC_CANTDECODERES
			|| rpc_error == RPC_AUTHERROR) {
		return -EREMOTEIO;
	}
	if (rpc_error == RPC_TIMEDOUT) {
		return -ETIMEDOUT;
	}
	if (rpc_error == RPC_PROGNOTREGISTERED
			|| rpc_error == RPC_PROCUNAVAIL
			|| rpc_error == RPC_VERSMISMATCH) {
		return -ECONNREFUSED;
	}
	/* I know this will cause incorrect error messages to pop up,but theres no choice */
	return -EINVAL;
}

static int find_id_of_host(struct sockaddr_in *raddr)
{
	int i, j;

	i = 0;
	j = 0;
	while (i < vec_count && j < VEC_MAX_SERVERS) {
		if (vec_table[j].used == USED) {
			/* Matching the host addresses and port #s */
			if (vec_table[j].addr.sin_addr.s_addr == raddr->sin_addr.s_addr
					&& vec_table[j].addr.sin_port == raddr->sin_port) {
				return j;
			}
			i++;
		}
		j++;
	}
	return -1;
}

static int find_unused_id(void)
{
	int i;

	i = 0;
	while (i < VEC_MAX_SERVERS) {
		if (vec_table[i].used != USED) {
			return i;
		}
		i++;
	}
	return -1;
}

static CLIENT** get_clnt_handle(int tcp, struct sockaddr_in *vecaddr)
{
	int id = -1;
	CLIENT *clnt = NULL, **pclnt = NULL;
	static struct timeval new_vec_timeout = {VEC_CLNT_TIMEOUT, 0};

	pthread_mutex_lock(&vec_mutex);

	if ((id = find_id_of_host(vecaddr)) < 0) {
		char *vec_host = NULL;

		id = find_unused_id();
		vec_host = inet_ntoa(vecaddr->sin_addr);
		vec_table[id].used = USED;
		memcpy(&vec_table[id].addr, vecaddr, sizeof(struct sockaddr_in));
		vec_count++;
		
		/* fill in the program number for the given host, port number combinations */
		if ((vec_table[id].tcp_prog_number = get_program_info(vec_host, 
						IPPROTO_TCP, ntohs(vecaddr->sin_port), &vec_table[id].tcp_version)) < 0) {
			gossip_err("Could not lookup TCP program number and version number for %s:%d\n", 
					vec_host, ntohs(vecaddr->sin_port));
			pthread_mutex_unlock(&vec_mutex);
			return NULL;
		}
		if ((vec_table[id].udp_prog_number = get_program_info(vec_host,
						IPPROTO_UDP, ntohs(vecaddr->sin_port), &vec_table[id].udp_version)) < 0) {
			gossip_err("Could not lookup UDP program number and version number for %s:%d\n", 
					vec_host, ntohs(vecaddr->sin_port));
			pthread_mutex_unlock(&vec_mutex);
			return NULL;
		}

        /*
         * vec_table[id].clnt = clnt = clnt_create(vec_host,
         * (tcp == 1) ? vec_table[id].tcp_prog_number : vec_table[id].udp_prog_number,
         * (tcp == 1) ? vec_table[id].tcp_version : vec_table[id].udp_version,
         * (tcp == 1) ? "tcp" : "udp");
         */
		vec_table[id].clnt = clnt = get_svc_handle(vec_host,
                (tcp == 1) ? vec_table[id].tcp_prog_number : vec_table[id].udp_prog_number,
                (tcp == 1) ? vec_table[id].tcp_version     : vec_table[id].udp_version,
                (tcp == 1) ? IPPROTO_TCP : IPPROTO_UDP, 
                VEC_CLNT_TIMEOUT, 
                (struct sockaddr *)&vec_table[id].our_addr);
		if (clnt == NULL) {
			clnt_pcreateerror (vec_host);
		}
		/* set the timeout to a fairly large value... */
		else {
			if (clnt_control(clnt, CLSET_TIMEOUT, (char *)&new_vec_timeout) == 0)
			{
				gossip_debug(GOSSIP_VEC_DEBUG,
						"Cannot reset timeout.. continuing with 25 second timeout\n");
			}
		}
	}
	else {
		if ((clnt = vec_table[id].clnt) == NULL) {
			char *vec_host = NULL;

			vec_host = inet_ntoa(vecaddr->sin_addr);
            /*
             * vec_table[id].clnt = clnt = clnt_create(vec_host,
             * (tcp == 1) ? vec_table[id].tcp_prog_number : vec_table[id].udp_prog_number,
             * (tcp == 1) ? vec_table[id].tcp_version : vec_table[id].udp_version,
             * (tcp == 1) ? "tcp" : "udp");
             */
            vec_table[id].clnt = clnt = get_svc_handle(vec_host,
                (tcp == 1) ? vec_table[id].tcp_prog_number : vec_table[id].udp_prog_number,
                (tcp == 1) ? vec_table[id].tcp_version     : vec_table[id].udp_version,
                (tcp == 1) ? IPPROTO_TCP : IPPROTO_UDP, 
                VEC_CLNT_TIMEOUT, 
                (struct sockaddr *)&vec_table[id].our_addr);

			if (clnt == NULL) {
				clnt_pcreateerror (vec_host);
			}
			else
			{
				if (clnt_control(clnt, CLSET_TIMEOUT, (char *)&new_vec_timeout) == 0)
				{
					gossip_debug(GOSSIP_VEC_DEBUG, 
							"Cannot reset timeout.. continuing with 25 second timeout\n");
				}
			}
		}
	}
	pclnt = &vec_table[id].clnt;
	gossip_debug(GOSSIP_VEC_DEBUG, "Contacting version vector server %s\n",
			inet_ntoa(vec_table[id].our_addr.sin_addr));
	pthread_mutex_unlock(&vec_mutex);
	return pclnt;
}

static void put_clnt_handle(CLIENT **pclnt, int force_put)
{
	if (pclnt == NULL || 
			*pclnt == NULL) {
		return;
	}
	/* This will be set on an error condition only */
	if (force_put) 
	{
		clnt_destroy(*pclnt);
		/* make it reconnect next time around */
		*pclnt = NULL;
		/* we need to re-register also */
		gossip_debug(GOSSIP_VEC_DEBUG, "put_clnt_handle forcing re-registering callback\n");
		return;
	}
	/* This is more of a performance thing than any error condition */
	if (cache_handle_policy == 0) {
		clnt_destroy(*pclnt);
		/* make it reconnect */
		*pclnt = NULL;
	}
	return;
}

#define check_for_sanity(opt) \
	if (!opt || !opt->vec_addr)\
	{\
		return -EINVAL;\
	}

int vec_ping(struct vec_options *opt)
{
	CLIENT **clnt = NULL;
	struct timeval tv = {25, 0};
	enum clnt_stat ans;
	int tcp, err;

	check_for_sanity(opt);
	tcp = opt->tcp;
	clnt = get_clnt_handle(tcp, (struct sockaddr_in *) opt->vec_addr);
	if (clnt == NULL || *clnt == NULL) {
		return -ECONNREFUSED;
	}
	/* Null call */
	if ((ans = clnt_call(*clnt, NULLPROC, (xdrproc_t) xdr_void, (caddr_t) NULL, 
				(xdrproc_t) xdr_void, (caddr_t) NULL, tv)) != RPC_SUCCESS) {
		clnt_perror(*clnt, "\nvec_ping: \n");
		err = convert_to_errno(ans);
		/* make it reconnect */
		put_clnt_handle(clnt, 1);
		return err;
	}
	/* drop it if the cache handle policy says so! */
	put_clnt_handle(clnt, 0);
	return 0;
}

static void vec_getreq_dtor(vec_get_req *req)
{
    if (req) {
        vec_dtor(&req->vr_vector);
    }
    return;
}

static int vec_getreq_ctor(vec_get_req *req, vec_handle_t handle,
        vec_mode_t mode, vec_offset_t offset, vec_size_t size,
        vec_stripe_size_t stripe_size, vec_server_count_t nservers,
        int max_nvectors, vec_vectors_t *vector)
{
    if (!req || max_nvectors > VEC_MAX_SVECTORS) {
        gossip_err("Invalid arguments to vec_getreq_ctor\n");
        return -EINVAL;
    }

	memcpy(req->vr_handle, handle, sizeof(handle));
    req->vr_mode = mode;
    req->vr_offset = offset;
    req->vr_size = size;
    req->vr_stripe_size = stripe_size;
    req->vr_nservers = nservers;
    req->vr_max_vectors = max_nvectors;
    if (vector) {
        int err;
        if ((err = vec_ctor(&req->vr_vector, vector->vec_vectors_t_len)) < 0) {
            return err;
        }
        if ((err = vec_copy(&req->vr_vector, vector)) < 0) {
            vec_dtor(&req->vr_vector);
            return err;
        }
    }
    else {
        req->vr_vector.vec_vectors_t_len = 0;
        req->vr_vector.vec_vectors_t_val = NULL;
    }
    return 0;
}

static void vec_getresp_dtor(vec_get_resp *resp)
{
    if (resp) {
        svec_dtor(&resp->vr_vectors, VEC_MAX_SVECTORS);
    }
    return;
}

static int vec_getresp_ctor(vec_get_resp *resp)
{
    int err;

    if (!resp)
        return -EINVAL;

    if ((err = svec_ctor(&resp->vr_vectors, VEC_MAX_SVECTORS, VEC_MAX_SERVERS)) < 0) {
        return err;
    }
    return 0;
}

/* Copy get response from server to caller */
static int copy_vec_get_resp(vec_get_resp *resp, vec_svectors_t *svector)
{
    int err;
    int scount, vcount;

    scount = resp->vr_vectors.vec_svectors_t_len;
    if (scount == 0) {
        return -EINVAL;
    }
    vcount = resp->vr_vectors.vec_svectors_t_val[0].vec_vectors_t_len;

    /* Construct memory for the response object */
    if ((err = svec_ctor(svector, scount, vcount)) < 0) {
        return err;
        
    }
    /* Deep copy the object */
    if ((err = svec_copy(svector, &resp->vr_vectors)) < 0) {
        svec_dtor(svector, scount);
        return err;
    }
    return 0;
}

int vec_get(struct vec_options *opt,
		vec_handle_t handle, vec_mode_t mode,
		vec_offset_t offset, vec_size_t size,
        vec_stripe_size_t stripe_size, vec_server_count_t nservers,
        int  max_nvectors, vec_vectors_t *vector,
        vec_svectors_t *svector)
{
	vec_get_req  req;
	vec_get_resp resp;
	CLIENT **clnt = NULL;
	enum clnt_stat ans;
	int tcp, retries = VEC_MAX_RETRIES, err;

	check_for_sanity(opt);
	tcp = opt->tcp;
	clnt = get_clnt_handle(tcp, (struct sockaddr_in *) opt->vec_addr);
	if (clnt == NULL || *clnt == NULL) {
		return -ECONNREFUSED;
	}
	memset(&req, 0, sizeof(req));
	memset(&resp, 0, sizeof(resp));
    if ((err = vec_getreq_ctor(&req, handle, mode, offset, size, stripe_size, nservers, max_nvectors, vector)) < 0)
    {
        /* drop handle if policy says so */
		put_clnt_handle(clnt, 0);
		return err;
    }
    if ((err = vec_getresp_ctor(&resp)) < 0)
    {
        vec_getreq_dtor(&req);
        /* drop handle if policy says so */
		put_clnt_handle(clnt, 0);
		return err;
    }
again:
	ans = vec_get_1(req, &resp, *clnt);
	if (ans != RPC_SUCCESS) {
        vec_getreq_dtor(&req);
        vec_getresp_dtor(&resp);
		clnt_perror(*clnt, "vec_get_1:");
		/* make it reconnect */
		put_clnt_handle(clnt, 1);
		err = convert_to_errno(ans);
		return err;
	}

	/* Check return values */
	if (resp.vr_status.vs_status == VEC_NO_ERROR) { 
        int err;
        /* deep copy the server response */
        err = copy_vec_get_resp(&resp, svector);
        vec_getreq_dtor(&req);
        vec_getresp_dtor(&resp);
		/* drop it if the cache handle policy says so! */
		put_clnt_handle(clnt, 0);
		return err;
	}
	else {
		if (resp.vr_status.vs_error == VEC_ETIMEDOUT && --retries > 0)
		{
			goto again;
		}
		/* drop it if the cache handle policy says so! */
		put_clnt_handle(clnt, 0);
        vec_getreq_dtor(&req);
        vec_getresp_dtor(&resp);
		err = resp.vr_status.vs_error;
		return err;
	}
}

static void vec_putreq_dtor(vec_put_req *req)
{
    return;
}

static int vec_putreq_ctor(vec_put_req *req, vec_handle_t handle,
        vec_mode_t mode, vec_offset_t offset, vec_size_t size,
        vec_stripe_size_t stripe_size, vec_server_count_t nservers)
{
    memcpy(&req->vr_handle, handle, sizeof(handle));
	req->vr_mode   = mode;
	req->vr_offset = offset;
	req->vr_size   = size;
    req->vr_stripe_size = stripe_size;
    req->vr_nservers = nservers;
    return 0;
}

static void vec_putresp_dtor(vec_put_resp *resp)
{
    if (resp) 
    {
        vec_dtor(&resp->vr_vector);
    }
}

static int vec_putresp_ctor(vec_put_resp *resp)
{
    int err;

    if (!resp)
        return -EINVAL;

    if ((err = vec_ctor(&resp->vr_vector, VEC_MAX_SERVERS)) < 0) {
        return err;
    }
    return 0;
}

/* Deep copy put response from server to caller */
static int copy_vec_put_resp(vec_put_resp *resp, vec_vectors_t *vector)
{
    int err;

    if ((err = vec_ctor(vector, resp->vr_vector.vec_vectors_t_len)) < 0) {
        return err;
    }
    if ((err = vec_copy(vector, &resp->vr_vector)) < 0) {
        vec_dtor(vector);
        return err;
    }
    return 0;
}

int vec_put(struct vec_options *opt,
		vec_handle_t handle, vec_mode_t mode,
		vec_offset_t offset, vec_size_t size,
        vec_stripe_size_t stripe_size, vec_server_count_t nservers,
        vec_vectors_t *vector /* Filled in */) 
{
	vec_put_req  req;
	vec_put_resp resp;
	CLIENT **clnt = NULL;
	enum clnt_stat ans;
	int tcp, retries = VEC_MAX_RETRIES, err;

	check_for_sanity(opt);
	tcp = opt->tcp;
	clnt = get_clnt_handle(tcp, (struct sockaddr_in *) opt->vec_addr);
	if (clnt == NULL || *clnt == NULL) {
		return -ECONNREFUSED;
	}
	memset(&req, 0, sizeof(req));
	memset(&resp, 0, sizeof(resp));

    if ((err = vec_putreq_ctor(&req, handle, mode, offset, size, stripe_size, nservers)) < 0)
    {
        /* drop handle if policy says so */
		put_clnt_handle(clnt, 0);
        return err;
    }
    if ((err = vec_putresp_ctor(&resp)) < 0)
    {
        vec_putreq_dtor(&req);
        /* drop handle if policy says so */
		put_clnt_handle(clnt, 0);
        return err;
    }
again:
	ans = vec_put_1(req, &resp, *clnt);
	if (ans != RPC_SUCCESS) {
        vec_putreq_dtor(&req);
        vec_putresp_dtor(&resp);
		clnt_perror(*clnt, "vec_put_1:");
		/* make it reconnect */
		put_clnt_handle(clnt, 1);
		err = convert_to_errno(ans);
		return err;
	}

	/* Check return values */
	if (resp.vr_status.vs_status == VEC_NO_ERROR) { 
        err = copy_vec_put_resp(&resp, vector);
        vec_putreq_dtor(&req);
        vec_putresp_dtor(&resp);
		/* drop it if the cache handle policy says so! */
		put_clnt_handle(clnt, 0);
		return err;
	}
	else {
		if (resp.vr_status.vs_error == VEC_ETIMEDOUT && --retries > 0)
		{
			goto again;
		}
		/* drop it if the cache handle policy says so! */
		put_clnt_handle(clnt, 0);
        vec_putreq_dtor(&req);
        vec_putresp_dtor(&resp);
		err = resp.vr_status.vs_error;
		return err;
	}
}

/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 * End:
 *
 * vim: ts=4 sts=4 sw=4 expandtab
 */
