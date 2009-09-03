#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <getopt.h>
#include <libgen.h>
#include <string.h>
#include <time.h>

#include "pvfs2.h"
#include "pvfs2-mgmt.h"
#include "pvfs2-internal.h"
#include "pvfs2-fsck.h"
#include "pint-cached-config.h"

static const char * state2str (int state)
{
    const char * ptr;
    switch (state)
    {
    case FSCK_IDLE:   ptr = "idle       "; break;
    case FSCK_IP:     ptr = "in-progress"; break;
    case FSCK_CANCEL: ptr = "cancelled  "; break;
    default:          ptr = "unknown    "; break;
    }
    return ptr;
}

static const char * phase2str (int phase)
{
    const char * ptr;
    switch (phase)
    {
    case FSCK_PHASE_CHECK:     ptr = "check    "; break;
    case FSCK_PHASE_PRECREATE: ptr = "precreate"; break;
    case FSCK_PHASE_BSTREAM:   ptr = "bstream  "; break;
    case FSCK_PHASE_ORPHAN:    ptr = "orphan   "; break;
    case FSCK_PHASE_REPAIR:    ptr = "repair   "; break;
    case FSCK_COMPLETE:        ptr = "complete "; break;
    default:                   ptr = "unknown  "; break;
    }
    return ptr;
}

static const char * op2str (int op)
{
    const char * ptr;
    switch (op)
    {
    case FSCK_OP_MOVE:    ptr = "move   "; break;
    case FSCK_OP_DELETE:  ptr = "delete "; break;
    case FSCK_OP_CREATE:  ptr = "create "; break;
    case FSCK_OP_BCREATE: ptr = "bcreate"; break;
    default: ptr = "unknown"; break;
    }
    return ptr;
}

static char posbuf[32];
static const char * pos2str (PVFS_ds_position pos)
{
    const char * ptr;
    switch (pos)
    {
    case PVFS_ITERATE_START: ptr = "start"; break;
    case PVFS_ITERATE_END:   ptr = "end  "; break;
    default:
        sprintf(posbuf, "%llu", llu(pos));
        ptr = posbuf;
        break;
    }
    return ptr;
}

/* stores program arguments */
struct program_options fsck_options = { 0, EOF };

/* Program Arguments */
struct option long_options[] =
{
    {"start",  0, &fsck_options.cmd, OPT_START},
    {"cancel", 0, &fsck_options.cmd, OPT_CANCEL},
    {"report", 0, &fsck_options.cmd, OPT_REPORT},
    {"repair", 0, &fsck_options.cmd, OPT_REPAIR},
    {"fsid",   1, NULL,              OPT_FSID}
};

static int parse_options (int argc, char **argv)
{
    int long_index = 0;
    int o;

    while((o = getopt_long(argc, argv, "", long_options, &long_index)) != EOF)
    {
        switch(o)
        {
        default:
            return 1;
        case 0:
            break;
        case OPT_FSID:
            fsck_options.fsid = strtoull(optarg, NULL, 0);
            break;
        }
    }

    if ((fsck_options.fsid == 0) || (fsck_options.cmd == EOF)) return 1;
    return 0;
}

static int fsck_start (
    PVFS_credentials *cred_p,
    PVFS_fs_id        fsid,
    int               server_count,
    PVFS_BMI_addr_t  *server_addr)
{
    struct PVFS_mgmt_setparam_value param_value;
    PVFS_mgmt_fsck_resp *resp_array;
    PVFS_ds_position position;
    PVFS_error ret;
    uint64_t log_count;
    int64_t uid;
    int i;
    int fault;

    uid = time(NULL);

    param_value.type    = PVFS_MGMT_PARAM_TYPE_UINT64;
    param_value.u.value = PVFS_SERVER_ADMIN_MODE;

    (void) PVFS_mgmt_setparam_all(fsid, cred_p, PVFS_SERV_PARAM_MODE,
               &param_value, NULL, NULL);

    resp_array = malloc(server_count*sizeof(*resp_array)); 
    assert(resp_array);

    fault = 0;

    for (i=0; i<server_count; i++)
    {
        memset(&resp_array[i], 0, sizeof(*resp_array));

        position  = PVFS_ITERATE_START;
        log_count = 0;
        resp_array[i].log  = NULL;

        ret = PVFS_mgmt_fsck(fsid, cred_p, &(server_addr[i]),
                  FSCK_START, uid, &position, &log_count, &(resp_array[i]),
                  PVFS_HINT_NULL);
        if (ret != 0)
        {
            fault = 1;
            PVFS_perror("PVFS_mgmt_fsck", ret);
        }
    }

    free(resp_array);
    return fault;
}

static int fsck_cancel (
    PVFS_credentials *cred_p,
    PVFS_fs_id        fsid,
    int               server_count,
    PVFS_BMI_addr_t  *server_addr)
{
    PVFS_mgmt_fsck_resp *resp_array;
    PVFS_ds_position position;
    PVFS_error ret;
    uint64_t log_count;
    int64_t uid = 0;
    int fault;
    int i;

    resp_array = malloc(server_count*sizeof(*resp_array)); 
    assert(resp_array);

    fault = 0;

    for (i=0; i<server_count; i++)
    {
        memset(&resp_array[i], 0, sizeof(*resp_array));

        log_count = 0;
        position  = PVFS_ITERATE_START;

        ret = PVFS_mgmt_fsck(fsid, cred_p, &(server_addr[i]),
                  FSCK_STOP, uid, &position, &log_count, &resp_array[i],
                  PVFS_HINT_NULL);
        if (ret < 0)
        {
            fault = 1;
            PVFS_perror("PVFS_mgmt_fsck", ret);
        }
    }

    for (i=0; i<server_count; i++)
    {
        if (resp_array[i].state != FSCK_CANCEL)
        {
            fprintf(stderr, "server: %d not cancelled : %s\n",
                i, state2str(resp_array[i].state));
            fault = 1;
        }
    }

    free(resp_array);
    return fault;
}

static int fsck_report (
    PVFS_credentials *cred_p,
    PVFS_fs_id        fsid,
    int               server_count,
    PVFS_BMI_addr_t  *server_addr)
{
    PVFS_mgmt_fsck_resp resp;
    PVFS_ds_position position;
    PVFS_error ret;
    uint64_t log_count;
    int64_t uid = 0;
    int i,j;
    int header;
    char  server_name[128];

    for (i=0; i<server_count; i++)
    {
        memset(&resp, 0, sizeof(resp));
        position  = PVFS_ITERATE_START;
        log_count = NUM_REPAIRS_PER_REQUEST;
        resp.log  = malloc(log_count*sizeof(PVFS_mgmt_fsck_repair));
        header = 1;

        while (position != PVFS_ITERATE_END)
        {
            ret = PVFS_mgmt_fsck(
                      fsid,
                      cred_p,
                      &(server_addr[i]),
                      FSCK_REPORT,
                      uid,
                      &position,
                      &log_count,
                      &resp,
                      PVFS_HINT_NULL);
            if (ret != 0)
            {
                PVFS_perror("PVFS_mgmt_fsck", ret);
            }
            else
            {
                if (header)
                {
                    (void) PINT_cached_config_get_server_name_from_addr(
                        fsck_options.fsid, server_addr[i],
                        sizeof(server_name), server_name);
                    printf ("server:%s\n", server_name);
                    printf ("  state     : %s\n", state2str(resp.state));
                    printf ("  phase     : %s\n", phase2str(resp.phase));
                    printf ("  position  : %s\n", pos2str(position));
                    printf ("  repairs   : %llu\n", llu(log_count));
                    header = 0;
                }
                for (j=0; j<log_count; j++)
                {
                    printf ("      parent: %llu\n",
                        llu(resp.log[j].parent));
                    printf ("        operation: %s\n",
                        op2str(resp.log[j].operation));
                    printf ("        child :    %llu\n",
                        llu(resp.log[j].child));
                }
            }
        }

        free(resp.log);
    }

    return ret;
}

static void usage (char *prog_name)
{
    fprintf(stderr,
        "usage: %s --[start|cancel|report|repair] --fsid=FSID\n",
        prog_name);
    return;
}

int main (int argc, char **argv)
{
    PVFS_credentials creds;
    PVFS_BMI_addr_t *server_addrs;
    int              server_count;
    int              ret;

    ret = parse_options(argc, argv);
    if (ret != 0)
    {
        usage(basename(argv[0]));
        return 1;
    }
    
    ret = PVFS_util_init_defaults();
    if (ret != 0)
    {
        PVFS_perror("PVFS_util_init_defaults", ret);
        return ret;
    }

    PVFS_util_gen_credentials(&creds);

    server_count = 0;
    ret = PVFS_mgmt_count_servers(
              fsck_options.fsid,
              &creds,
              PVFS_MGMT_IO_SERVER|PVFS_MGMT_META_SERVER,
              &server_count);
    if (ret != 0)
    {
        PVFS_perror("PVFS_mgmt_count_servers", ret);
        return ret;
    }

    server_addrs = malloc(server_count*sizeof(PVFS_BMI_addr_t));
    if (!server_addrs)
    {
        perror("malloc");
        return 1;
    }

    ret = PVFS_mgmt_get_server_array(
              fsck_options.fsid,
              &creds,
              PVFS_MGMT_IO_SERVER|PVFS_MGMT_META_SERVER,
              server_addrs,
              &server_count);
    if (ret != 0)
    {
        PVFS_perror("PVFS_mgmt_get_server_array", ret);
        return ret;
    }


    switch (fsck_options.cmd)
    {
    case OPT_START:
        ret = fsck_start(&creds,fsck_options.fsid,server_count,server_addrs);
        break;
    case OPT_CANCEL:
        ret = fsck_cancel(&creds,fsck_options.fsid,server_count,server_addrs);
        break; 
    case OPT_REPORT:
        ret = fsck_report(&creds,fsck_options.fsid,server_count,server_addrs);
        break;
    case OPT_REPAIR:
        ret = 1;
        break;
    }

    PVFS_sys_finalize();

    if (server_addrs) free(server_addrs);

    return ret;
}
