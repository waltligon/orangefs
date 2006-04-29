/*
 * (C) 2006 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/resource.h>
#include <unistd.h>
#include <netdb.h>
#include "pvfs2.h"
#include "pvfs2-mgmt.h"
#include "server-config.h"
#include "synch-server-mapping.h"
#include "quicklist.h"
#include "quickhash.h"
#include "bmi-method-support.h"
#include "dlm_prot_client.h"
#include "vec_prot_client.h"
#include "vec_common.h"
#include "synch-cache.h"
/* Maximum number of entries in the FSID <-> sockaddr mapping tables */
#define   MAX_FSIDS  16

struct synch_fsid_options {
    PVFS_fs_id  fsid;
    int nservers;
    PVFS_BMI_addr_t *servers;
    struct sockaddr *addresses;
    struct qlist_head hash_link;
    int *dlm_port, *vec_port;
};

static int    synch_count   = 0;
static struct synch_fsid_options  options[MAX_FSIDS];
static struct qhash_table *s_synch_fsid_options_table = NULL;

static int normalize_to_errno(PVFS_error error_code)
{
    if(error_code > 0)
    {
        gossip_err("synchclient: error status received.\n");
        error_code = -error_code;
    }

    /* convert any error codes that are in pvfs2 format */
    if(IS_PVFS_NON_ERRNO_ERROR(-error_code))
    {
        /* assume a default error code */
        gossip_err("synchclient: warning: "
            "got error code without errno equivalent: %d.\n", error_code);
        error_code = -EINVAL;
    }
    else if(IS_PVFS_ERROR(-error_code))
    {
        error_code = -PVFS_ERROR_TO_ERRNO(-error_code);
    }
    return(error_code);
}

static int fsid_hash(void *key, int table_size)
{
    PVFS_fs_id tag = *((PVFS_fs_id *)key);
    return tag % table_size;
}

static int fsid_compare(void *key, struct qlist_head *link)
{
    struct synch_fsid_options *opt = NULL;
    PVFS_fs_id tag = *((PVFS_fs_id *)key);

    opt = qlist_entry(link, struct synch_fsid_options, hash_link);
    return opt->fsid == tag ? 1 : 0;
}

static int add_to_synch_fsid_options_table(struct synch_fsid_options *opt)
{
    if (opt)
    {
        qhash_add(s_synch_fsid_options_table,
               (void *) &opt->fsid, &opt->hash_link);
        return 0;
    }
    return -1;
}

static struct synch_fsid_options *
find_synch_fsid_options(PVFS_fs_id fsid)
{
    struct qlist_head *link = NULL;
    struct synch_fsid_options *opt = NULL;

    link = qhash_search(s_synch_fsid_options_table, (void *) &fsid);
    if (link)
    {
        opt = qhash_entry(link,
                struct synch_fsid_options, hash_link);
        if (opt->fsid != fsid)
        {
            gossip_err("Impossible happened: fsid's dont match!\n");
            assert(0);
        }
    }
    return opt;
}

static struct synch_fsid_options * 
alloc_synch_slot(PVFS_fs_id fsid, PVFS_error *err)
{
    struct synch_fsid_options *tmp = NULL;
   
    *err = 0;
    /* See if we have an entry for this fsid */
    if ((tmp = find_synch_fsid_options(fsid)) == NULL)
    {
        int nservers = 0, i;
        PVFS_credentials creds;

        /* Are we out of slots? */
        if (synch_count >= MAX_FSIDS)
        {
            gossip_err("synchclient: Impossible happened, exhausted "
						" synch server mapping slots!\n");
            *err = -PVFS_EINVAL;
            return NULL;
        }
        PVFS_util_gen_credentials(&creds);
        /* Get the number of servers for this file system */
        if ((*err = PVFS_mgmt_count_servers(
                fsid, &creds, PVFS_MGMT_META_SERVER | PVFS_MGMT_IO_SERVER,
                &nservers)) < 0)
        {
            gossip_err("synchclient: Could not ascertain number "
						" of servers?!\n");
            return NULL;
        }
        /* Find a new one from a preallocated pool of objects */
        tmp = &options[synch_count++];
        tmp->fsid = fsid;
        tmp->nservers = nservers;
        /* Allocate memory for all ports */
        tmp->dlm_port = (int *) calloc(nservers, sizeof(int));
        if (tmp->dlm_port == NULL)
        {
            gossip_err("synchclient: Could not allocate memory for "
						" synch server mapping table\n");
            synch_count--;
            *err = -PVFS_ENOMEM;
            return NULL;
        }
        tmp->vec_port = (int *) calloc(nservers, sizeof(int));
        if (tmp->vec_port == NULL)
        {
            gossip_err("synchclient: Could not allocate memory for "
						" synch server mapping table\n");
            synch_count--;
            free(tmp->dlm_port);
            tmp->dlm_port = NULL;
            *err = -PVFS_ENOMEM;
            return NULL;
        }
        /* Allocate memory to hold the addresses of all servers  */
        tmp->servers  = (PVFS_BMI_addr_t *) 
            calloc(nservers, sizeof (PVFS_BMI_addr_t));
        if (tmp->servers == NULL)
        {
            gossip_err("synchclient: Could not allocate memory for "
						" synch server mapping table\n");
            free(tmp->dlm_port);
            tmp->dlm_port = NULL;
            free(tmp->vec_port);
            tmp->vec_port = NULL;
            synch_count--;
            *err = -PVFS_ENOMEM;
            return NULL;
        }
        /* Allocate memory to hold the socket addresses of all servers as well */
        tmp->addresses = (struct sockaddr *)
            calloc(nservers, sizeof (struct sockaddr));
        if (tmp->addresses == NULL)
        {
            gossip_err("synchclient: Could not allocate memory for "
						" synch server mapping table\n");
            free(tmp->servers);
            tmp->servers = NULL;
            free(tmp->dlm_port);
            tmp->dlm_port = NULL;
            free(tmp->vec_port);
            tmp->vec_port = NULL;
            synch_count--;
            *err = -PVFS_ENOMEM;
            return NULL;
        }
        /* Get hold of all the BMI addresses of all the servers */
        if ((*err = PVFS_mgmt_get_server_array(
                fsid, &creds, PVFS_MGMT_META_SERVER | PVFS_MGMT_IO_SERVER,
                tmp->servers, &nservers)) < 0)
        {
            gossip_err("synchclient: Could not ascertain addresses of "
                    "all participating servers\n");
            free(tmp->servers);
            tmp->servers = NULL;
            free(tmp->addresses);
            tmp->addresses = NULL;
            free(tmp->dlm_port);
            tmp->dlm_port = NULL;
            free(tmp->vec_port);
            tmp->vec_port = NULL;
            synch_count--;
            return NULL;
        }
        /* Convert them to a string representation */
        for (i = 0; i < nservers; i++)
        {
            int temp;
            const char *string_server_address = NULL;
            char *tcp_string = NULL, *ptr;
            struct hostent *hent;

            string_server_address = 
                PVFS_mgmt_map_addr(fsid, &creds, tmp->servers[i], &temp);
            if (string_server_address == NULL)
            {
                gossip_err("synchclient: Could not map address of server\n");
                break;
            }
            PVFS_mgmt_map_synch_ports(
                    fsid, (char *) string_server_address,
                    &tmp->dlm_port[i],
                    &tmp->vec_port[i]);
            /* Currently, we only support tcp server addresses */
            tcp_string = string_key("tcp", string_server_address);
            if (tcp_string == NULL)
            {
                gossip_err("synchclient: Unsupported server addresses\n");
                break;
            }
            /* Remove port numbers */
            ptr = strchr(tcp_string, ':');
            if (ptr)
            {
                *ptr = '\0';
            }
            /* lookup the addresses */
            hent = gethostbyname(tcp_string);
            if (hent == NULL)
            {
                gossip_err("synchclient: Could not lookup server address\n");
                free(tcp_string);
                break;
            }
            ((struct sockaddr_in *) &tmp->addresses[i])->sin_family = AF_INET;
            memcpy(&((struct sockaddr_in *) &tmp->addresses[i])->sin_addr.s_addr, 
                    hent->h_addr_list[0], hent->h_length);

            free(tcp_string);
        }
        if (i != nservers)
        {
            free(tmp->dlm_port);
            tmp->dlm_port = NULL;
            free(tmp->vec_port);
            tmp->vec_port = NULL;
            free(tmp->servers);
            tmp->servers = NULL;
            free(tmp->addresses);
            tmp->addresses = NULL;
            synch_count--;
            *err = -PVFS_EINVAL;
            return NULL;
        }
        /* Finally, we add this to our hash table */
        add_to_synch_fsid_options_table(tmp);
    }
    return tmp;
}

static void dealloc_synch_slots(void)
{
    int i;
    for (i = 0; i < MAX_FSIDS; i++)
    {
		  if (options[i].servers)
			  free(options[i].servers);
		  if (options[i].addresses)
			  free(options[i].addresses);
          if (options[i].dlm_port)
              free(options[i].dlm_port);
          if (options[i].vec_port)
              free(options[i].vec_port);
    }
    return;
}

int PINT_synch_server_mapping(enum PVFS_synch_method method, 
            PVFS_handle handle, PVFS_fs_id fsid,
			struct sockaddr *synch_server)
{
    struct synch_fsid_options *fsid_opt = NULL;
	PVFS_error ret;
    int sync_server, port;

	/* Find out how many and which servers etc for this given file system */
    fsid_opt = alloc_synch_slot(fsid, &ret);
    if (fsid_opt == NULL)
    {
        return normalize_to_errno(ret);
    }
	/* Hash the fsid and handle to get the synch server's address */
    sync_server = abs(hash2(handle, fsid)) % (fsid_opt->nservers);
	/* make sure we have a sane index */
	assert(sync_server >= 0 && sync_server < fsid_opt->nservers);
	/* Copy the address */
	memcpy(synch_server, &fsid_opt->addresses[sync_server], sizeof(struct sockaddr));
    if (method == PVFS_SYNCH_DLM) {
        /* and the ports */
        port = fsid_opt->dlm_port[sync_server] < 0 ? DLM_REQ_PORT : fsid_opt->dlm_port[sync_server];
    }
    else {
        /* and the ports */
        port = fsid_opt->vec_port[sync_server] < 0 ? VEC_REQ_PORT : fsid_opt->vec_port[sync_server];
    }
    ((struct sockaddr_in *) synch_server)->sin_port = htons((short) port);
	return 0;
}

int PINT_initialize_synch_server_mapping_table(void)
{
    s_synch_fsid_options_table = qhash_init(fsid_compare, fsid_hash, 31);
    if (s_synch_fsid_options_table == NULL)
    {
        return -ENOMEM;
    }
    return 0;
}

void PINT_finalize_synch_server_mapping_table(void)
{
	dealloc_synch_slots();
    qhash_finalize(s_synch_fsid_options_table);
}

/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 * End:
 *
 * vim: ts=4 sts=4 sw=4 expandtab
 */
