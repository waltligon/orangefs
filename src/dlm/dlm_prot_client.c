/*
 * (C) 2006 The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "dlm_config.h"
#include "dlm_prot.h"

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
#include "dlm_prot_client.h"

static pthread_mutex_t dlm_mutex = PTHREAD_MUTEX_INITIALIZER;
typedef struct dlm_entry dlm_entry;

struct dlm_entry {
	int used;
	struct sockaddr_in addr;
	int tcp_prog_number, udp_prog_number;
	int tcp_version, udp_version;
	CLIENT *clnt;
	struct sockaddr_in our_addr;
};

enum {USED = 1, UNUSED = 0};
static dlm_entry dlm_table[DLM_MAX_SERVERS];
static int dlm_count = 0;

static int lockv_ctor(dlm_lockv_req *req, int nregions,
		dlm_offset_t *offsets, dlm_size_t *sizes)
{
	int i;
	req->dr_offsets.dlm_offsets_t_len = nregions;
	req->dr_offsets.dlm_offsets_t_val = 
		(dlm_offset_t *) calloc(nregions, sizeof(dlm_offset_t));
	if (req->dr_offsets.dlm_offsets_t_val == NULL)
	{
		return -ENOMEM;
	}
	req->dr_sizes.dlm_sizes_t_len     = nregions;
	req->dr_sizes.dlm_sizes_t_val     =
		(dlm_size_t *)   calloc(nregions, sizeof(dlm_size_t));
	if (req->dr_sizes.dlm_sizes_t_val == NULL)
	{
		free(req->dr_offsets.dlm_offsets_t_val);
		return -ENOMEM;
	}
	for (i = 0; i < nregions; i++)
	{
		req->dr_offsets.dlm_offsets_t_val[i] = offsets[i];
		req->dr_sizes.dlm_sizes_t_val[i] = sizes[i];
	}
	return 0;
}

static void lockv_dtor(dlm_lockv_req *req)
{
    if (req) {
        free(req->dr_offsets.dlm_offsets_t_val);
        free(req->dr_sizes.dlm_sizes_t_val);
    }
}

/* 
 * This variable determines whether or not CLIENT handles to dlm servers
 * need to be cached.
 * i.e we cache handles if it is set to 1
 * and re-connect everytime if it is set to 0.
 * it is a good thing if the client-side daemon caches CLIENT handles to
 * dlm servers because we dont expect them to die...
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
	while (i < dlm_count && j < DLM_MAX_SERVERS) {
		if (dlm_table[j].used == USED) {
			/* Matching the host addresses and port #s */
			if (dlm_table[j].addr.sin_addr.s_addr == raddr->sin_addr.s_addr
					&& dlm_table[j].addr.sin_port == raddr->sin_port) {
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
	while (i < DLM_MAX_SERVERS) {
		if (dlm_table[i].used != USED) {
			return i;
		}
		i++;
	}
	return -1;
}

static CLIENT** get_clnt_handle(int tcp, struct sockaddr_in *dlmaddr)
{
	int id = -1;
	CLIENT *clnt = NULL, **pclnt = NULL;
	static struct timeval new_dlm_timeout = {DLM_CLNT_TIMEOUT, 0};

	pthread_mutex_lock(&dlm_mutex);

	if ((id = find_id_of_host(dlmaddr)) < 0) {
		char *dlm_host = NULL;

		id = find_unused_id();
		dlm_host = inet_ntoa(dlmaddr->sin_addr);
		dlm_table[id].used = USED;
		memcpy(&dlm_table[id].addr, dlmaddr, sizeof(struct sockaddr_in));
		dlm_count++;

        /* fill in the program number for the given host, port number combinations */
		if ((dlm_table[id].tcp_prog_number = get_program_info(dlm_host, 
						IPPROTO_TCP, ntohs(dlmaddr->sin_port), &dlm_table[id].tcp_version)) < 0) {
			gossip_err("Could not lookup TCP program number and version number for %s:%d\n", 
					dlm_host, ntohs(dlmaddr->sin_port));
			pthread_mutex_unlock(&dlm_mutex);
			return NULL;
		}
		if ((dlm_table[id].udp_prog_number = get_program_info(dlm_host,
						IPPROTO_UDP, ntohs(dlmaddr->sin_port), &dlm_table[id].udp_version)) < 0) {
			gossip_err("Could not lookup UDP program number and version number for %s:%d\n", 
					dlm_host, ntohs(dlmaddr->sin_port));
			pthread_mutex_unlock(&dlm_mutex);
			return NULL;
		}

        /*
         * dlm_table[id].clnt = clnt = clnt_create(dlm_host,
         * (tcp == 1) ? dlm_table[id].tcp_prog_number : dlm_table[id].udp_prog_number,
         * (tcp == 1) ? dlm_table[id].tcp_version : dlm_table[id].udp_version,
         * (tcp == 1) ? "tcp" : "udp");
         */
    	dlm_table[id].clnt = clnt = get_svc_handle(dlm_host,
                (tcp == 1) ? dlm_table[id].tcp_prog_number : dlm_table[id].udp_prog_number,
                (tcp == 1) ? dlm_table[id].tcp_version     : dlm_table[id].udp_version,
                (tcp == 1) ? IPPROTO_TCP : IPPROTO_UDP, 
                DLM_CLNT_TIMEOUT, 
                (struct sockaddr *)&dlm_table[id].our_addr);

		if (clnt == NULL) {
			clnt_pcreateerror (dlm_host);
		}
		/* set the timeout to a fairly large value... */
		else {
			if (clnt_control(clnt, CLSET_TIMEOUT, (char *)&new_dlm_timeout) == 0)
			{
				gossip_debug(GOSSIP_DLM_DEBUG,
						"Cannot reset timeout.. continuing with 25 second timeout\n");
			}
		}
	}
	else {
		if ((clnt = dlm_table[id].clnt) == NULL) {
			char *dlm_host = NULL;

			dlm_host = inet_ntoa(dlmaddr->sin_addr);
            /*
             * dlm_table[id].clnt = clnt = clnt_create(dlm_host,
             * (tcp == 1) ? dlm_table[id].tcp_prog_number : dlm_table[id].udp_prog_number,
             * (tcp == 1) ? dlm_table[id].tcp_version : dlm_table[id].udp_version,
             * (tcp == 1) ? "tcp" : "udp");
             */
            dlm_table[id].clnt = clnt = get_svc_handle(dlm_host,
                    (tcp == 1) ? dlm_table[id].tcp_prog_number : dlm_table[id].udp_prog_number,
                    (tcp == 1) ? dlm_table[id].tcp_version     : dlm_table[id].udp_version,
                    (tcp == 1) ? IPPROTO_TCP : IPPROTO_UDP, 
                    DLM_CLNT_TIMEOUT, 
                    (struct sockaddr *)&dlm_table[id].our_addr);

			if (clnt == NULL) {
				clnt_pcreateerror (dlm_host);
			}
			else
			{
				if (clnt_control(clnt, CLSET_TIMEOUT, (char *)&new_dlm_timeout) == 0)
				{
					gossip_debug(GOSSIP_DLM_DEBUG, 
							"Cannot reset timeout.. continuing with 25 second timeout\n");
				}
			}
		}
	}
	pclnt = &dlm_table[id].clnt;
	gossip_debug(GOSSIP_DLM_DEBUG, "Contacting DLM server %s\n",
            inet_ntoa(dlm_table[id].our_addr.sin_addr));
	pthread_mutex_unlock(&dlm_mutex);
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
		gossip_debug(GOSSIP_DLM_DEBUG, "put_clnt_handle forcing re-registering callback\n");
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
	if (!opt || !opt->dlm_addr)\
	{\
		return -EINVAL;\
	}

int dlm_ping(struct dlm_options *opt)
{
	CLIENT **clnt = NULL;
	struct timeval tv = {25, 0};
	enum clnt_stat ans;
	int tcp, err;

	check_for_sanity(opt);
	tcp = opt->tcp;
	clnt = get_clnt_handle(tcp, (struct sockaddr_in *) opt->dlm_addr);
	if (clnt == NULL || *clnt == NULL) {
		return -ECONNREFUSED;
	}
	/* Null call */
	if ((ans = clnt_call(*clnt, NULLPROC, (xdrproc_t) xdr_void, (caddr_t) NULL, 
				(xdrproc_t) xdr_void, (caddr_t) NULL, tv)) != RPC_SUCCESS) {
		clnt_perror(*clnt, "\ndlm_ping: \n");
		err = convert_to_errno(ans);
		/* make it reconnect */
		put_clnt_handle(clnt, 1);
		return err;
	}
	/* drop it if the cache handle policy says so! */
	put_clnt_handle(clnt, 0);
	return 0;
}

int dlm_lock(struct dlm_options *opt,
		dlm_handle_t handle, dlm_mode_t mode,
		dlm_offset_t offset, dlm_size_t size, dlm_token_t *token)
{
	dlm_lock_req  req;
	dlm_lock_resp resp;
	CLIENT **clnt = NULL;
	enum clnt_stat ans;
	int tcp, retries = DLM_MAX_RETRIES, err;

	check_for_sanity(opt);
	tcp = opt->tcp;
	clnt = get_clnt_handle(tcp, (struct sockaddr_in *) opt->dlm_addr);
	if (clnt == NULL || *clnt == NULL) {
		return -ECONNREFUSED;
	}
	memset(&req, 0, sizeof(req));
	memset(&resp, 0, sizeof(resp));
	memcpy(&req.dr_handle, handle, sizeof(handle));
	req.dr_mode   = mode;
	req.dr_offset = offset;
	req.dr_size   = size;
again:
	ans = dlm_lock_1(req, &resp, *clnt);
	if (ans != RPC_SUCCESS) {
		clnt_perror(*clnt, "dlm_lock_1:");
		err = convert_to_errno(ans);
		/* make it reconnect */
		put_clnt_handle(clnt, 1);
		return err;
	}

	/* Check return values */
	if (resp.dr_status.ds_status == DLM_NO_ERROR) { 
		*token = resp.dr_token;
		/* drop it if the cache handle policy says so! */
		put_clnt_handle(clnt, 0);
		return 0;
	}
	else {
		if (resp.dr_status.ds_error == DLM_ETIMEDOUT && --retries > 0)
		{
			goto again;
		}
		err = resp.dr_status.ds_error;
		/* drop it if the cache handle policy says so! */
		put_clnt_handle(clnt, 0);
		return err;
	}
}

int dlm_lockv(struct dlm_options *opt,
		dlm_handle_t handle, dlm_mode_t mode,
		int nregions, 
		dlm_offset_t *offsets, dlm_size_t *sizes,
		dlm_token_t *token)
{
	dlm_lockv_req  req;
	dlm_lockv_resp resp;
	CLIENT **clnt = NULL;
	enum clnt_stat ans;
	int err, tcp, retries = DLM_MAX_RETRIES;

	check_for_sanity(opt);
	tcp = opt->tcp;
	clnt = get_clnt_handle(tcp, (struct sockaddr_in *) opt->dlm_addr);
	if (clnt == NULL || *clnt == NULL) {
		return -ECONNREFUSED;
	}
	memset(&req, 0, sizeof(req));
	memset(&resp, 0, sizeof(resp));
	/* Allocate memory for the request */
	if ((err = lockv_ctor(&req, nregions, offsets, sizes)) < 0)
	{
		/* drop it if the cache handle policy says so! */
		put_clnt_handle(clnt, 0);
		return err;
	}
again:
	ans = dlm_lockv_1(req, &resp, *clnt);
	if (ans != RPC_SUCCESS) {
		clnt_perror(*clnt, "dlm_lockv_1:");
		err = convert_to_errno(ans);
		/* make it reconnect */
		put_clnt_handle(clnt, 1);
        lockv_dtor(&req);
		return err;
	}

	/* Check return values */
	if (resp.dr_status.ds_status == DLM_NO_ERROR) { 
		*token = resp.dr_token;
		/* drop it if the cache handle policy says so! */
		put_clnt_handle(clnt, 0);
        lockv_dtor(&req);
		return 0;
	}
	else {
		if (resp.dr_status.ds_error == DLM_ETIMEDOUT && --retries > 0)
		{
			goto again;
		}
		err = resp.dr_status.ds_error;
		/* drop it if the cache handle policy says so! */
		put_clnt_handle(clnt, 0);
        lockv_dtor(&req);
		return err;
	}
}

int dlm_unlock(struct dlm_options *opt,
		dlm_token_t token)
{
	dlm_unlock_req  req;
	dlm_unlock_resp resp;
	CLIENT **clnt = NULL;
	enum clnt_stat ans;
	int tcp, err;

	check_for_sanity(opt);
	tcp = opt->tcp;
	clnt = get_clnt_handle(tcp, (struct sockaddr_in *) opt->dlm_addr);
	if (clnt == NULL || *clnt == NULL) {
		return -ECONNREFUSED;
	}
	memset(&req, 0, sizeof(req));
	memset(&resp, 0, sizeof(resp));
	req.ur_token = token;
	ans = dlm_unlock_1(req, &resp, *clnt);
	if (ans != RPC_SUCCESS) {
		clnt_perror(*clnt, "dlm_unlock_1:");
		err = convert_to_errno(ans);
		/* make it reconnect */
		put_clnt_handle(clnt, 1);
		return err;
	}
	/* drop it if the cache handle policy says so! */
	put_clnt_handle(clnt, 0);
	/* Check for errors */
	if (resp.ur_status.ds_status == DLM_NO_ERROR)
		return 0;
	else {
		err = resp.ur_status.ds_error;
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
