/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <unistd.h>
#include <sys/types.h>

#include "pvfs2-sysint.h"
#include "pvfs2-req-proto.h"
#include "pint-sysint-utils.h"
#include "pint-servreq.h"
#include "pint-bucket.h"
#include "acache.h"
#include "PINT-reqproto-encode.h"
#include "dotconf.h"
#include "trove.h"
#include "server-config.h"
#include "str-utils.h"
#include "client-state-machine.h"

static int g_session_tag;
gen_mutex_t *g_session_tag_mt_lock = NULL;
static struct server_configuration_s g_server_config;

/* analogous to 'get_server_config_struct' in pvfs2-server.c */
struct server_configuration_s *PINT_get_server_config_struct(void)
{
    return &g_server_config;
}

static int server_parse_config(
    struct server_configuration_s *config,
    struct PVFS_servresp_getconfig *response);

int get_next_session_tag(void)
{
    int ret = -1;

    if (g_session_tag_mt_lock == NULL)
    {
        g_session_tag_mt_lock = gen_mutex_build();
        if (g_session_tag_mt_lock == NULL)
        {
            return ret;
        }
    }

    gen_mutex_lock(g_session_tag_mt_lock);
    ret = g_session_tag;

    /* increment the tag, don't use zero */
    if (g_session_tag + 1 == 0)
    {
	g_session_tag = 1;
    }
    else
    {
	g_session_tag++;
    }
    gen_mutex_unlock(g_session_tag_mt_lock);

    return ret;
}

/* check permissions of a PVFS object against the access mode
 *
 * returns 0 on success, -1 on error
 */
int PINT_check_perms(PVFS_object_attr attr,
		PVFS_permissions mode,
		int uid,
		int gid)
{
    return ((((attr.perms & mode) == mode) ||
             ((attr.group == gid) && (attr.perms & mode) == mode) ||
             (attr.owner == uid)) ? 0 : -1);
}

int PINT_server_get_config(struct server_configuration_s *config,
                           pvfs_mntlist mntent_list)
{
    int ret = -1, i = 0;
    bmi_addr_t serv_addr;
    struct PVFS_server_req serv_req;
    struct PVFS_server_resp *serv_resp = NULL;
    PVFS_credentials creds;
    struct PINT_decoded_msg decoded;
    void* encoded_resp;
    struct pvfs_mntent *mntent_p = NULL;
    PVFS_msg_tag_t op_tag = get_next_session_tag();
    int found_one_good = 0;
    struct filesystem_configuration_s* cur_fs = NULL;

    /*
      for each entry in the pvfstab, attempt to query the server for
      getconfig information.  discontinue loop when we have info.
    */
    for (i = 0; i < mntent_list.ptab_count; i++)
    {
	mntent_p = &mntent_list.ptab_array[i];

   	/* obtain the metaserver to send the request */
	ret = BMI_addr_lookup(&serv_addr, mntent_p->pvfs_config_server);
	if (ret < 0)
	{
            gossip_ldebug(GOSSIP_CLIENT_DEBUG,"Failed to resolve BMI "
                          "address %s\n",mntent_p->pvfs_config_server);
	    continue;
	}

        creds.uid = getuid();
        creds.gid = getgid();

        memset(&serv_req,0,sizeof(struct PVFS_server_req));
	serv_req.op = PVFS_SERV_GETCONFIG;
	serv_req.credentials = creds;

	gossip_ldebug(GOSSIP_CLIENT_DEBUG,"asked for fs name = %s\n",
                      mntent_p->pvfs_fs_name);

	/* send the request and receive an acknowledgment */
	ret = PINT_send_req(serv_addr, &serv_req, mntent_p->encoding,
                            &decoded, &encoded_resp, op_tag);
	if (ret < 0)
        {
            gossip_err("Error: failed to send request to initial"
	    " configuration server;\n");
	    gossip_err("       please verify that your client configuration"
	    " is correct \n       and that the server is running.\n");
	    gossip_err("       (%s)\n", mntent_p->pvfs_config_server);
	    continue;
	}

	serv_resp = (struct PVFS_server_resp *)decoded.buffer;
	if (serv_resp->status != 0)
	{
	    PVFS_perror_gossip("Error: getconfig request denied",
                               serv_resp->status);
	    continue;
	}

        if (server_parse_config(config,&(serv_resp->u.getconfig)))
        {
            gossip_ldebug(GOSSIP_CLIENT_DEBUG,"Failed to getconfig from host "
                          "%s\n",mntent_p->pvfs_config_server);

            /* let go of any resources consumed by PINT_send_req() */
            PINT_release_req(serv_addr, &serv_req, mntent_p->encoding,
                             &decoded, &encoded_resp, op_tag);
            continue;
        }

	/* let go of any resources consumed by PINT_send_req() */
	PINT_release_req(serv_addr, &serv_req, mntent_p->encoding,
                         &decoded, &encoded_resp, op_tag);
        break;
    }

    /* verify that each pvfstab entry is valid according to the server */
    for (i = 0; i < mntent_list.ptab_count; i++)
    {
	mntent_p = &mntent_list.ptab_array[i];

        /* make sure we have valid information about this fs */
        cur_fs = PINT_config_find_fs_name(config, mntent_p->pvfs_fs_name);
        if (!cur_fs)
        {
            gossip_ldebug(GOSSIP_CLIENT_DEBUG,"Warning:  Cannot retrieve "
                          "information about pvfstab entry %s\n",
                          mntent_p->pvfs_config_server);

            /*
              if the device has no space left on it, we can't save
              the config file for parsing and get a failure; make
              a note of that possibility here
            */
            gossip_ldebug(GOSSIP_CLIENT_DEBUG,
                          "If you're sure that your pvfstab "
                          "file contains valid information, please make "
                          "sure that you are not out of disk space\n");
            continue;
        }
        else
	{
	    found_one_good = 1;
	    cur_fs->flowproto = mntent_p->flowproto;
	    cur_fs->encoding = mntent_p->encoding;
	}
    }

    if (found_one_good)
    {
	return(0); 
    }
    else
    {
	gossip_err("Error: no valid pvfs2tab entries found.\n");
	return(-PVFS_ENODEV);
    }
}

static int server_parse_config(struct server_configuration_s *config,
                               struct PVFS_servresp_getconfig *response)
{
    int ret = 1;
    int fs_fd = 0, server_fd = 0;
    char fs_template[] = ".__pvfs_fs_configXXXXXX";
    char server_template[] = ".__pvfs_server_configXXXXXX";

    if (config && response)
    {
        assert(response->fs_config_buf);
        assert(response->server_config_buf);

        fs_fd = mkstemp(fs_template);
        if (fs_fd == -1)
        {
            return ret;
        }

        server_fd = mkstemp(server_template);
        if (server_fd == -1)
        {
            close(fs_fd);
            return ret;
        }

        assert(!response->fs_config_buf[response->fs_config_buf_size - 1]);
        assert(!response->server_config_buf[response->server_config_buf_size - 1]);

        if (write(fs_fd,response->fs_config_buf,
                  (response->fs_config_buf_size - 1)) ==
            (response->fs_config_buf_size - 1))
        {
            if (write(server_fd,response->server_config_buf,
                      (response->server_config_buf_size - 1)) ==
                (response->server_config_buf_size - 1))
            {
                ret = PINT_parse_config(config, fs_template, server_template);
            }
        }
        close(fs_fd);
        close(server_fd);

        remove(fs_template);
        remove(server_template);
    }
    return ret;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
