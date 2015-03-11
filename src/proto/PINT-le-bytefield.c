/*
 * (C) 2001 Clemson University and The University of Chicago
 * (C) 2003 Pete Wyckoff, Ohio Supercomputer Center <pw@osc.edu>
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

#define __PINT_REQPROTO_ENCODE_FUNCS_C  /* trigger actual definitions */
#include "endecode-funcs.h"
#include "bmi.h"
#include "bmi-byteswap.h"
#include "gossip.h"
#include "pvfs2-debug.h"
#include "pvfs2-dist-basic.h"
#include "pvfs2-types.h"
#include "pvfs2-req-proto.h"
#include "PINT-reqproto-encode.h"
#include "PINT-reqproto-module.h"
#include "src/io/description/pint-request.h"  /* for PINT_Request */
#include "src/io/description/pint-distribution.h"  /* for PINT_dist_lookup */
#include "pvfs2-internal.h"
#include "pint-hint.h"
#include "pint-util.h"
#include "security-util.h"

const char PVFS2_BLANK_ISSUER[] = "";

/* defined later */
static int check_req_size(struct PVFS_server_req *req);
static int check_resp_size(struct PVFS_server_resp *resp);
static void zero_capability(PVFS_capability*);
static void zero_credential(PVFS_credential*);

static int initializing_sizes = 0;

/* an array of structs for storing precalculated maximum encoding sizes
 * for each type of server operation 
 */
static struct {
    int req;
    int resp;
} *max_size_array = NULL;

/* lebf_initialize()
 *
 * initializes the encoder module, calculates max sizes of each request type 
 * in advance
 *
 * no return value
 */
static void lebf_initialize(void)
{
    struct PVFS_server_req req = {0};
    struct PVFS_server_resp resp = {0};
    enum PVFS_server_op op_type;
    int reqsize, respsize;
    int noreq;
    PINT_dist tmp_dist;
    PINT_Request tmp_req;
    char *tmp_name = strdup("foo");
    const int init_big_size = 4 * 1024 * 1024;
    int i;

    gossip_debug(GOSSIP_ENDECODE_DEBUG,"lebf_initialize\n");

    max_size_array = malloc(PVFS_SERV_NUM_OPS * sizeof(*max_size_array));
    if (max_size_array == NULL)
        return;

    /*
     * Some messages have extra structures, and even indeterminate sizes
     * which are hand-calculated here.  Also some fields must be initialized
     * for encoding to work properly.
     */
    memset(&tmp_dist, 0, sizeof(tmp_dist));
    tmp_dist.dist_name = strdup(PVFS_DIST_BASIC_NAME);
    if (PINT_dist_lookup(&tmp_dist)) {
        gossip_err("%s: dist %s does not exist?!?\n",
          __func__, tmp_dist.dist_name);
        exit(1);
    }
    memset(&tmp_req, 0, sizeof(tmp_req));

    initializing_sizes = 1;

    /* set number of hints in request to the max */
    for(i = 0; i < PVFS_HINT_MAX; ++i)
    {
        char name[PVFS_HINT_MAX_NAME_LENGTH] = {0};
        char val[PVFS_HINT_MAX_LENGTH] = {0};
        PVFS_hint_add(&req.hints, name, PVFS_HINT_MAX_LENGTH, val);
    }

    for (op_type=0; op_type<PVFS_SERV_NUM_OPS; op_type++) {
        memset(&req.u, 0, sizeof(req.u));
        memset(&resp.u, 0, sizeof(resp.u));
        req.op = resp.op = op_type;
        reqsize = 0;
        respsize = 0;
        noreq = 0;
        switch (op_type) {
            case PVFS_SERV_INVALID:
            case PVFS_SERV_PERF_UPDATE:
            case PVFS_SERV_PRECREATE_POOL_REFILLER:
            case PVFS_SERV_JOB_TIMER:
                /* never used, skip initialization */
                continue;
            case PVFS_SERV_GETCONFIG:
                resp.u.getconfig.fs_config_buf = tmp_name;
                respsize = extra_size_PVFS_servresp_getconfig;
                break;
            case PVFS_SERV_LOOKUP_PATH:
                req.u.lookup_path.path = "";
                resp.u.lookup_path.handle_count = 0;
                resp.u.lookup_path.attr_count = 0;
                reqsize = extra_size_PVFS_servreq_lookup_path;
                respsize = extra_size_PVFS_servresp_lookup_path;
                break;
            case PVFS_SERV_BATCH_CREATE:
                /* can request a range of handles */
                req.u.batch_create.handle_extent_array.extent_count = 0;
                req.u.batch_create.object_count = 0;
                resp.u.batch_create.handle_count = 0;
                reqsize = extra_size_PVFS_servreq_batch_create;
                respsize = extra_size_PVFS_servresp_batch_create;
                break;
            case PVFS_SERV_CREATE:
                zero_credential(&req.u.create.credential);
                zero_capability(&resp.u.create.metafile_attrs.capability);
                /* can request a range of handles */
                reqsize = extra_size_PVFS_servreq_create;
                respsize = extra_size_PVFS_servresp_create;
                break;
            case PVFS_SERV_MIRROR:
                 req.u.mirror.dist = &tmp_dist;
                 req.u.mirror.dst_count = 0;
                 reqsize = extra_size_PVFS_servreq_mirror;
                 respsize = extra_size_PVFS_servresp_mirror;
                 break;
            case PVFS_SERV_IMM_COPIES:
                 break;
            case PVFS_SERV_REMOVE:
                /* nothing special, let normal encoding work */
                break;
            case PVFS_SERV_BATCH_REMOVE:
                req.u.batch_remove.handles = NULL;
                req.u.batch_remove.handle_count = 0;
                reqsize = extra_size_PVFS_servreq_batch_remove;
                break;
            case PVFS_SERV_MGMT_REMOVE_OBJECT:
                /* nothing special, let normal encoding work */
                break;
            case PVFS_SERV_MGMT_REMOVE_DIRENT:
                req.u.mgmt_remove_dirent.entry = tmp_name;
                reqsize = extra_size_PVFS_servreq_mgmt_remove_dirent;
                break;
            case PVFS_SERV_TREE_REMOVE:
                zero_credential(&req.u.tree_remove.credential);
                req.u.tree_remove.handle_array = NULL;
                req.u.tree_remove.handle_count = 0;
                resp.u.tree_remove.status = NULL;
                resp.u.tree_remove.handle_count = 0;
                resp.u.tree_remove.caller_handle_index = 0;
                reqsize = extra_size_PVFS_servreq_tree_remove;
                respsize = extra_size_PVFS_servresp_tree_remove;
                break;
            case PVFS_SERV_IO:
                req.u.io.io_dist = &tmp_dist;
                req.u.io.file_req = &tmp_req;
                reqsize = extra_size_PVFS_servreq_io;
                break;
            case PVFS_SERV_SMALL_IO:
                req.u.small_io.dist = &tmp_dist;
                req.u.small_io.file_req = &tmp_req;
                reqsize = extra_size_PVFS_servreq_small_io;
                respsize = extra_size_PVFS_servresp_small_io;
                break;
            case PVFS_SERV_GETATTR:
                zero_credential(&req.u.getattr.credential);
                resp.u.getattr.attr.mask = 0;
                reqsize = extra_size_PVFS_servreq_getattr;
                respsize = extra_size_PVFS_servresp_getattr;
                break;
            case PVFS_SERV_UNSTUFF:
                zero_credential(&req.u.unstuff.credential);
                resp.u.unstuff.attr.mask = 0;
                reqsize = extra_size_PVFS_servreq_unstuff;
                respsize = extra_size_PVFS_servresp_unstuff;
                break;
            case PVFS_SERV_SETATTR:
                req.u.setattr.attr.mask = 0;
                zero_credential(&req.u.setattr.credential);
                reqsize = extra_size_PVFS_servreq_setattr;
                break;
            case PVFS_SERV_CRDIRENT:
                req.u.crdirent.name = tmp_name;
                reqsize = extra_size_PVFS_servreq_crdirent;
                break;
            case PVFS_SERV_RMDIRENT:
                req.u.rmdirent.entry = tmp_name;
                reqsize = extra_size_PVFS_servreq_rmdirent;
                break;
            case PVFS_SERV_CHDIRENT:
                req.u.chdirent.entry = tmp_name;
                reqsize = extra_size_PVFS_servreq_chdirent;
                break;
            case PVFS_SERV_TRUNCATE:
                /* nothing special */
                break;
            case PVFS_SERV_MKDIR:
                zero_credential(&req.u.mkdir.credential);
                req.u.mkdir.handle_extent_array.extent_count = 0;
                req.u.mkdir.attr.mask = 0;
                zero_capability(&resp.u.mkdir.capability);
                reqsize = extra_size_PVFS_servreq_mkdir;
                respsize = extra_size_PVFS_servresp_mkdir;
                break;
            case PVFS_SERV_READDIR:
                resp.u.readdir.directory_version = 0;
                resp.u.readdir.dirent_count = 0;
                respsize = extra_size_PVFS_servresp_readdir;
                break;
            case PVFS_SERV_FLUSH:
                /* nothing special */
                break;
            case PVFS_SERV_MGMT_SETPARAM:
                /* nothing special */
                break;
            case PVFS_SERV_MGMT_NOOP:
                /* nothing special */
                break;
            case PVFS_SERV_STATFS:
                /* nothing special */
                break;
            case PVFS_SERV_MGMT_GET_DIRDATA_HANDLE:
                /* nothing special */
                break;
            case PVFS_SERV_WRITE_COMPLETION:
                /* only a response, but nothing special there */
                noreq = 1;
                break;
            case PVFS_SERV_MGMT_PERF_MON:
                resp.u.mgmt_perf_mon.perf_array_count = 0;
                respsize = extra_size_PVFS_servresp_mgmt_perf_mon;
                break;
            case PVFS_SERV_MGMT_ITERATE_HANDLES:
                resp.u.mgmt_iterate_handles.handle_count = 0;
                respsize = extra_size_PVFS_servresp_mgmt_iterate_handles;
                break;
            case PVFS_SERV_MGMT_DSPACE_INFO_LIST:
                req.u.mgmt_dspace_info_list.handle_count = 0;
                resp.u.mgmt_dspace_info_list.dspace_info_count = 0;
                reqsize = extra_size_PVFS_servreq_mgmt_dspace_info_list;
                respsize = extra_size_PVFS_servresp_mgmt_dspace_info_list;
                break;
            case PVFS_SERV_MGMT_EVENT_MON:
                resp.u.mgmt_event_mon.event_count = 0;
                respsize = extra_size_PVFS_servresp_mgmt_event_mon;
                break;
            case PVFS_SERV_PROTO_ERROR:
                /* nothing special */
                break;
            case PVFS_SERV_GETEATTR:
                req.u.geteattr.nkey = 0;
                resp.u.geteattr.nkey = 0;
                reqsize = extra_size_PVFS_servreq_geteattr;
                respsize = extra_size_PVFS_servresp_geteattr;
                break;
            case PVFS_SERV_SETEATTR:
                req.u.seteattr.nkey = 0;
                reqsize = extra_size_PVFS_servreq_seteattr;
                break;
            case PVFS_SERV_ATOMICEATTR:
                req.u.atomiceattr.nkey = 0;
                resp.u.atomiceattr.nkey = 0;
                reqsize = extra_size_PVFS_servreq_atomiceattr;
                respsize = extra_size_PVFS_servresp_atomiceattr;
                break;
            case PVFS_SERV_DELEATTR:
                req.u.deleattr.key.buffer_sz = 0;
                reqsize = extra_size_PVFS_servreq_deleattr;
                break;
            case PVFS_SERV_LISTEATTR:
                resp.u.listeattr.nkey = 0;
                req.u.listeattr.nkey = 0;
                reqsize = extra_size_PVFS_servreq_listeattr;
                respsize = extra_size_PVFS_servresp_listeattr;
                break;
            case PVFS_SERV_LISTATTR:
                resp.u.listattr.nhandles = 0;
                req.u.listattr.nhandles = 0;
                reqsize = extra_size_PVFS_servreq_listattr;
                respsize = extra_size_PVFS_servresp_listattr;
                break;
            case PVFS_SERV_TREE_GET_FILE_SIZE:
                req.u.tree_get_file_size.handle_array = NULL;
                req.u.tree_get_file_size.num_data_files = 0;
                zero_credential(&req.u.tree_get_file_size.credential);
                resp.u.tree_get_file_size.size = NULL;
                resp.u.tree_get_file_size.error = NULL;
                resp.u.tree_get_file_size.handle_count = 0;
                resp.u.tree_get_file_size.caller_handle_index = 0;
                reqsize = extra_size_PVFS_servreq_tree_get_file_size;
                respsize = extra_size_PVFS_servresp_tree_get_file_size;
                break;
            case PVFS_SERV_TREE_GETATTR:
                req.u.tree_getattr.handle_array = NULL;
                req.u.tree_getattr.handle_count = 0;
                zero_credential(&req.u.tree_getattr.credential);
                resp.u.tree_getattr.attr = NULL;
                resp.u.tree_getattr.error = NULL;
                resp.u.tree_getattr.handle_count = 0;
                resp.u.tree_getattr.caller_handle_index = 0;
                reqsize = extra_size_PVFS_servreq_tree_getattr;
                respsize = extra_size_PVFS_servresp_tree_getattr;
                break;
            case PVFS_SERV_MGMT_GET_UID:
                resp.u.mgmt_get_uid.uid_info_array_count = 0;
                respsize = extra_size_PVFS_servresp_mgmt_get_uid;
                break;
            case PVFS_SERV_TREE_SETATTR:
                zero_credential(&req.u.tree_setattr.credential);
                req.u.tree_setattr.handle_count = 0;
                req.u.tree_setattr.handle_array = NULL;
                resp.u.tree_setattr.handle_count = 0;
                resp.u.tree_setattr.status = NULL;
                reqsize = extra_size_PVFS_servreq_tree_setattr;
                respsize = extra_size_PVFS_servresp_tree_setattr;
                break;
            case PVFS_SERV_MGMT_GET_DIRENT:
                req.u.mgmt_get_dirent.entry = tmp_name;
                reqsize = extra_size_PVFS_servreq_mgmt_get_dirent;
                break;
            case PVFS_SERV_MGMT_CREATE_ROOT_DIR:
                /* nothing special */
                break;
            case PVFS_SERV_MGMT_SPLIT_DIRENT:
                req.u.mgmt_split_dirent.dist = &tmp_dist;
                req.u.mgmt_split_dirent.nentries = 0;
                req.u.mgmt_split_dirent.entry_handles = NULL;
                req.u.mgmt_split_dirent.entry_names = NULL;
                reqsize = extra_size_PVFS_servreq_mgmt_split_dirent;
                break;
            case PVFS_SERV_MGMT_GET_USER_CERT:
                req.u.mgmt_get_user_cert.fs_id = 0;
                req.u.mgmt_get_user_cert.userid = tmp_name;
                req.u.mgmt_get_user_cert.enc_pwd = tmp_name;
                req.u.mgmt_get_user_cert.enc_pwd_size = 0;
                req.u.mgmt_get_user_cert.exp = 0;
                reqsize = extra_size_PVFS_servreq_mgmt_get_user_cert;
                respsize = extra_size_PVFS_servresp_mgmt_get_user_cert;
                break;
            case PVFS_SERV_MGMT_GET_USER_CERT_KEYREQ:
                req.u.mgmt_get_user_cert_keyreq.fs_id = 0;
                respsize = extra_size_PVFS_servresp_mgmt_get_user_cert_keyreq;
                break;
            case PVFS_SERV_NUM_OPS:  /* sentinel, should not hit */
                assert(0);
                break;
        }
        /* since these take the max size when mallocing in the encode,
         * give them a huge number, then later fix it. */
        max_size_array[op_type].req = 
                                 max_size_array[op_type].resp = init_big_size;

        /* account for extra structures in every request */
        zero_capability(&req.capability);
        reqsize += extra_size_PVFS_servreq;

        if (noreq)
            reqsize = 0;
        else
            reqsize += check_req_size(&req);

        respsize += check_resp_size(&resp);

        if (reqsize > init_big_size)
            gossip_err("%s: op %d reqsize %d exceeded prealloced %d\n",
              __func__, op_type, reqsize, init_big_size);
        if (respsize > init_big_size)
            gossip_err("%s: op %d respsize %d exceeded prealloced %d\n",
              __func__, op_type, respsize, init_big_size);
        max_size_array[op_type].req = reqsize;
        max_size_array[op_type].resp = respsize;
    }

    /* clean up stuff just used for initialization */
    PVFS_hint_free(req.hints);
    free(tmp_dist.dist_name);
    free(tmp_name);
    initializing_sizes = 0;
}

static void lebf_finalize(void)
{
    free(max_size_array);
}

/* lebf_encode_calc_max_size()
 *
 * reports the maximum allowed encoded size for the given request type
 *
 * returns size on success, -errno on failure
 */
static int lebf_encode_calc_max_size(
    enum PINT_encode_msg_type input_type,
    enum PVFS_server_op op_type)
{
    if(input_type == PINT_ENCODE_REQ)
        return(max_size_array[op_type].req);
    else if(input_type == PINT_ENCODE_RESP)
        return(max_size_array[op_type].resp);

    return -PVFS_EINVAL;
}
#define BF_ENCODE_TARGET_MSG_INIT(_msg) \
    (_msg)->buffer_list = &target_msg->buffer_stub; \
    (_msg)->size_list = &target_msg->size_stub; \
    (_msg)->alloc_size_list = &target_msg->alloc_size_stub; \
    (_msg)->list_count = 1; \
    (_msg)->buffer_type = BMI_PRE_ALLOC;

/*
 * Used by both encode functions, request and response, to set
 * up the one buffer which will hold the encoded message.
 */
static int
encode_common(struct PINT_encoded_msg *target_msg, int maxsize)
{
    int ret = 0;
    void *buf = NULL;

    gossip_debug(GOSSIP_ENDECODE_DEBUG,"encode_common\n");
    /* this encoder always uses just one buffer */
    BF_ENCODE_TARGET_MSG_INIT(target_msg);

    gossip_debug(GOSSIP_ENDECODE_DEBUG,"\tmaxsize:%d\tinitializing_sizes:%d\n"
                                      ,maxsize,initializing_sizes);

    /* allocate the max size buffer to avoid the work of calculating it */
    buf = (initializing_sizes ? malloc(maxsize) :
           BMI_memalloc(target_msg->dest, maxsize, BMI_SEND));
    if (!buf)
    {
        gossip_err("Error: failed to BMI_malloc memory for response.\n");
        gossip_err("Error: is BMI address %llu still valid?\n", llu(target_msg->dest));
        ret = -PVFS_ENOMEM;
        goto out;
    }

    target_msg->buffer_list[0] = buf;
    target_msg->alloc_size_list[0] = maxsize;
    target_msg->ptr_current = buf;

    /* generic header */
    memcpy(target_msg->ptr_current, le_bytefield_table.generic_header,
           PINT_ENC_GENERIC_HEADER_SIZE);
    target_msg->ptr_current += PINT_ENC_GENERIC_HEADER_SIZE;

 out:
    return ret;
}

/* lebf_encode_req()
 *
 * encodes a request structure
 *
 * returns 0 on success, -errno on failure
 */
static int lebf_encode_req(
    struct PVFS_server_req *req,
    struct PINT_encoded_msg *target_msg)
{
    int ret = 0;
    char **p;

    gossip_debug(GOSSIP_ENDECODE_DEBUG,"Executing lebf_encode_req...\n");
    gossip_debug(GOSSIP_ENDECODE_DEBUG,"\treq->op:%d\n",req->op);

    ret = encode_common(target_msg, max_size_array[req->op].req);

    if (ret)
        goto out;
    gossip_debug(GOSSIP_ENDECODE_DEBUG,"lebf_encode_req\n");

    /* every request has these fields */
    p = &target_msg->ptr_current;
    encode_PVFS_server_req(p, req);

#define CASE(tag,var) \
    case tag: encode_PVFS_servreq_##var(p,&req->u.var); break

    switch (req->op) {

        /* call standard function defined in headers */
        CASE(PVFS_SERV_LOOKUP_PATH, lookup_path);
        CASE(PVFS_SERV_CREATE, create);
        CASE(PVFS_SERV_MIRROR, mirror);
        CASE(PVFS_SERV_UNSTUFF, unstuff);
        CASE(PVFS_SERV_BATCH_CREATE, batch_create);
        CASE(PVFS_SERV_BATCH_REMOVE, batch_remove);
        CASE(PVFS_SERV_REMOVE, remove);
        CASE(PVFS_SERV_MGMT_REMOVE_OBJECT, mgmt_remove_object);
        CASE(PVFS_SERV_MGMT_REMOVE_DIRENT, mgmt_remove_dirent);
        CASE(PVFS_SERV_TREE_REMOVE, tree_remove);
        CASE(PVFS_SERV_TREE_GET_FILE_SIZE, tree_get_file_size);
        CASE(PVFS_SERV_TREE_GETATTR, tree_getattr);
        CASE(PVFS_SERV_TREE_SETATTR, tree_setattr);
        CASE(PVFS_SERV_MGMT_GET_DIRDATA_HANDLE, mgmt_get_dirdata_handle);
        CASE(PVFS_SERV_IO, io);
        CASE(PVFS_SERV_SMALL_IO, small_io);
        CASE(PVFS_SERV_GETATTR, getattr);
        CASE(PVFS_SERV_SETATTR, setattr);
        CASE(PVFS_SERV_CRDIRENT, crdirent);
        CASE(PVFS_SERV_RMDIRENT, rmdirent);
        CASE(PVFS_SERV_CHDIRENT, chdirent);
        CASE(PVFS_SERV_TRUNCATE, truncate);
        CASE(PVFS_SERV_MKDIR, mkdir);
        CASE(PVFS_SERV_READDIR, readdir);
        CASE(PVFS_SERV_FLUSH, flush);
        CASE(PVFS_SERV_STATFS, statfs);
        CASE(PVFS_SERV_MGMT_SETPARAM, mgmt_setparam);
        CASE(PVFS_SERV_MGMT_PERF_MON, mgmt_perf_mon);
        CASE(PVFS_SERV_MGMT_ITERATE_HANDLES, mgmt_iterate_handles);
        CASE(PVFS_SERV_MGMT_DSPACE_INFO_LIST, mgmt_dspace_info_list);
        CASE(PVFS_SERV_MGMT_EVENT_MON, mgmt_event_mon);
        CASE(PVFS_SERV_GETEATTR, geteattr);
        CASE(PVFS_SERV_SETEATTR, seteattr);
        CASE(PVFS_SERV_ATOMICEATTR, atomiceattr);
        CASE(PVFS_SERV_DELEATTR, deleattr);
        CASE(PVFS_SERV_LISTEATTR, listeattr);
        CASE(PVFS_SERV_LISTATTR,  listattr);
        CASE(PVFS_SERV_MGMT_GET_UID, mgmt_get_uid);
        CASE(PVFS_SERV_MGMT_GET_DIRENT, mgmt_get_dirent);
        CASE(PVFS_SERV_MGMT_CREATE_ROOT_DIR, mgmt_create_root_dir);
        CASE(PVFS_SERV_MGMT_SPLIT_DIRENT, mgmt_split_dirent);
        CASE(PVFS_SERV_MGMT_GET_USER_CERT, mgmt_get_user_cert);
        CASE(PVFS_SERV_MGMT_GET_USER_CERT_KEYREQ, mgmt_get_user_cert_keyreq);
        case PVFS_SERV_GETCONFIG:
        case PVFS_SERV_MGMT_NOOP:
        case PVFS_SERV_PROTO_ERROR:
        case PVFS_SERV_IMM_COPIES:
            /* nothing else */
            break;

        case PVFS_SERV_INVALID:
        case PVFS_SERV_WRITE_COMPLETION:
        case PVFS_SERV_PERF_UPDATE:
        case PVFS_SERV_PRECREATE_POOL_REFILLER:
        case PVFS_SERV_JOB_TIMER:
        case PVFS_SERV_NUM_OPS:  /* sentinel */
            gossip_err("%s: invalid operation %d\n", __func__, req->op);
            ret = -PVFS_ENOSYS;
            break;
    }

#undef CASE

    /* although much more may have been allocated */
    target_msg->total_size = target_msg->ptr_current
      - (char *) target_msg->buffer_list[0];
    target_msg->size_list[0] = target_msg->total_size;

    if (target_msg->total_size > max_size_array[req->op].req)
    {
        ret = -PVFS_ENOMEM;
        gossip_err("%s: op %d needed %lld bytes but alloced only %d\n",
          __func__, req->op, lld(target_msg->total_size),
          max_size_array[req->op].req);
    }

  out:
    return ret;
}


/* lebf_encode_resp()
 *
 * encodes a response structure
 *
 * returns 0 on success, -errno on failure
 */
static int lebf_encode_resp(
    struct PVFS_server_resp *resp,
    struct PINT_encoded_msg *target_msg)
{
    int ret;
    char **p;

    ret = encode_common(target_msg, max_size_array[resp->op].resp);
    if (ret)
        goto out;
    gossip_debug(GOSSIP_ENDECODE_DEBUG,"lebf_encode_resp\n");

    /* every response has these fields */
    p = &target_msg->ptr_current;
    encode_PVFS_server_resp(p, resp);

#define CASE(tag,var) \
    case tag: encode_PVFS_servresp_##var(p,&resp->u.var); break


    /* we stand a good chance of segfaulting if we try to encode the response
     * after something bad happened reading data from disk. */
    if (resp->status == 0) 
    {

        /* extra encoding rules for particular responses */
        switch (resp->op) {

        /* call standard function defined in headers */
        CASE(PVFS_SERV_GETCONFIG, getconfig);
        CASE(PVFS_SERV_LOOKUP_PATH, lookup_path);
        CASE(PVFS_SERV_CREATE, create);
        CASE(PVFS_SERV_MIRROR, mirror);
        CASE(PVFS_SERV_UNSTUFF, unstuff);
        CASE(PVFS_SERV_BATCH_CREATE, batch_create);
        CASE(PVFS_SERV_IO, io);
        CASE(PVFS_SERV_SMALL_IO, small_io);
        CASE(PVFS_SERV_GETATTR, getattr);
        CASE(PVFS_SERV_RMDIRENT, rmdirent);
        CASE(PVFS_SERV_CHDIRENT, chdirent);
        CASE(PVFS_SERV_MKDIR, mkdir);
        CASE(PVFS_SERV_READDIR, readdir);
        CASE(PVFS_SERV_STATFS, statfs);
        CASE(PVFS_SERV_MGMT_PERF_MON, mgmt_perf_mon);
        CASE(PVFS_SERV_MGMT_ITERATE_HANDLES, mgmt_iterate_handles);
        CASE(PVFS_SERV_MGMT_DSPACE_INFO_LIST, mgmt_dspace_info_list);
        CASE(PVFS_SERV_MGMT_EVENT_MON, mgmt_event_mon);
        CASE(PVFS_SERV_WRITE_COMPLETION, write_completion);
        CASE(PVFS_SERV_MGMT_GET_DIRDATA_HANDLE, mgmt_get_dirdata_handle);
        CASE(PVFS_SERV_GETEATTR, geteattr);
        CASE(PVFS_SERV_ATOMICEATTR, atomiceattr);
        CASE(PVFS_SERV_LISTEATTR, listeattr);
        CASE(PVFS_SERV_LISTATTR, listattr);
        CASE(PVFS_SERV_TREE_GET_FILE_SIZE, tree_get_file_size);
        CASE(PVFS_SERV_TREE_REMOVE, tree_remove);
        CASE(PVFS_SERV_TREE_GETATTR, tree_getattr);
        CASE(PVFS_SERV_TREE_SETATTR, tree_setattr);
        CASE(PVFS_SERV_MGMT_GET_UID, mgmt_get_uid);
        CASE(PVFS_SERV_MGMT_GET_DIRENT, mgmt_get_dirent);
        CASE(PVFS_SERV_MGMT_GET_USER_CERT, mgmt_get_user_cert);
        CASE(PVFS_SERV_MGMT_GET_USER_CERT_KEYREQ, mgmt_get_user_cert_keyreq);
        case PVFS_SERV_REMOVE:
        case PVFS_SERV_MGMT_REMOVE_OBJECT:
        case PVFS_SERV_MGMT_REMOVE_DIRENT:
        case PVFS_SERV_SETATTR:
        case PVFS_SERV_SETEATTR:
        case PVFS_SERV_DELEATTR:
        case PVFS_SERV_CRDIRENT:
        case PVFS_SERV_TRUNCATE:
        case PVFS_SERV_FLUSH:
        case PVFS_SERV_MGMT_NOOP:
        case PVFS_SERV_BATCH_REMOVE:
        case PVFS_SERV_PROTO_ERROR:
        case PVFS_SERV_IMM_COPIES:
        case PVFS_SERV_MGMT_SETPARAM:
        case PVFS_SERV_MGMT_CREATE_ROOT_DIR:
        case PVFS_SERV_MGMT_SPLIT_DIRENT:
            /* nothing else */
            break;

        case PVFS_SERV_INVALID:
        case PVFS_SERV_PERF_UPDATE:
        case PVFS_SERV_PRECREATE_POOL_REFILLER:
        case PVFS_SERV_JOB_TIMER:
        case PVFS_SERV_NUM_OPS:  /* sentinel */
            gossip_err("%s: invalid operation %d\n", __func__, resp->op);
            ret = -PVFS_ENOSYS;
            break;
        }
    } 

#undef CASE

    /* although much more may have been allocated */
    target_msg->total_size = target_msg->ptr_current
      - (char *) target_msg->buffer_list[0];
    target_msg->size_list[0] = target_msg->total_size;

    if (target_msg->total_size > max_size_array[resp->op].resp) {
        ret = -PVFS_ENOMEM;
        gossip_err("%s: op %d needed %lld bytes but alloced only %d\n",
          __func__, resp->op, lld(target_msg->total_size),
          max_size_array[resp->op].resp);
    }

  out:
    return ret;
}

/* lebf_decode_req()
 *
 * decodes a request message
 *
 * input_buffer is a pointer past the generic header; it starts with
 * PVFS_server_req->op.
 *
 * returns 0 on success, -errno on failure
 */
static int lebf_decode_req(
    void *input_buffer,
    int input_size,
    struct PINT_decoded_msg *target_msg,
    PVFS_BMI_addr_t target_addr)
{
    int ret = 0;
    char *ptr = input_buffer;
    char **p = &ptr;
    struct PVFS_server_req *req = &target_msg->stub_dec.req;

    target_msg->buffer = req;

    /* decode generic part of request (enough to get op number) */
    decode_PVFS_server_req(p, req);
    gossip_debug(GOSSIP_ENDECODE_DEBUG,"lebf_decode_req\n");

#define CASE(tag,var) \
    case tag: decode_PVFS_servreq_##var(p, &req->u.var); break

    switch (req->op) {

        /* call standard function defined in headers */
        CASE(PVFS_SERV_LOOKUP_PATH, lookup_path);
        CASE(PVFS_SERV_CREATE, create);
        CASE(PVFS_SERV_MIRROR, mirror);
        CASE(PVFS_SERV_UNSTUFF, unstuff);
        CASE(PVFS_SERV_BATCH_CREATE, batch_create);
        CASE(PVFS_SERV_BATCH_REMOVE, batch_remove);
        CASE(PVFS_SERV_REMOVE, remove);
        CASE(PVFS_SERV_MGMT_REMOVE_OBJECT, mgmt_remove_object);
        CASE(PVFS_SERV_MGMT_REMOVE_DIRENT, mgmt_remove_dirent);
        CASE(PVFS_SERV_TREE_REMOVE, tree_remove);
        CASE(PVFS_SERV_TREE_GET_FILE_SIZE, tree_get_file_size);
        CASE(PVFS_SERV_TREE_GETATTR, tree_getattr);
        CASE(PVFS_SERV_TREE_SETATTR, tree_setattr);
        CASE(PVFS_SERV_MGMT_GET_DIRDATA_HANDLE, mgmt_get_dirdata_handle);
        CASE(PVFS_SERV_IO, io);
        CASE(PVFS_SERV_SMALL_IO, small_io);
        CASE(PVFS_SERV_GETATTR, getattr);
        CASE(PVFS_SERV_SETATTR, setattr);
        CASE(PVFS_SERV_CRDIRENT, crdirent);
        CASE(PVFS_SERV_RMDIRENT, rmdirent);
        CASE(PVFS_SERV_CHDIRENT, chdirent);
        CASE(PVFS_SERV_TRUNCATE, truncate);
        CASE(PVFS_SERV_MKDIR, mkdir);
        CASE(PVFS_SERV_READDIR, readdir);
        CASE(PVFS_SERV_FLUSH, flush);
        CASE(PVFS_SERV_STATFS, statfs);
        CASE(PVFS_SERV_MGMT_SETPARAM, mgmt_setparam);
        CASE(PVFS_SERV_MGMT_PERF_MON, mgmt_perf_mon);
        CASE(PVFS_SERV_MGMT_ITERATE_HANDLES, mgmt_iterate_handles);
        CASE(PVFS_SERV_MGMT_DSPACE_INFO_LIST, mgmt_dspace_info_list);
        CASE(PVFS_SERV_MGMT_EVENT_MON, mgmt_event_mon);
        CASE(PVFS_SERV_GETEATTR, geteattr);
        CASE(PVFS_SERV_SETEATTR, seteattr);
        CASE(PVFS_SERV_ATOMICEATTR, atomiceattr);
        CASE(PVFS_SERV_DELEATTR, deleattr);
        CASE(PVFS_SERV_LISTEATTR, listeattr);
        CASE(PVFS_SERV_LISTATTR, listattr);
        CASE(PVFS_SERV_MGMT_GET_UID, mgmt_get_uid);
        CASE(PVFS_SERV_MGMT_GET_DIRENT, mgmt_get_dirent);
        CASE(PVFS_SERV_MGMT_CREATE_ROOT_DIR, mgmt_create_root_dir);
        CASE(PVFS_SERV_MGMT_SPLIT_DIRENT, mgmt_split_dirent);
        CASE(PVFS_SERV_MGMT_GET_USER_CERT, mgmt_get_user_cert);
        CASE(PVFS_SERV_MGMT_GET_USER_CERT_KEYREQ, mgmt_get_user_cert_keyreq);
        case PVFS_SERV_GETCONFIG:
        case PVFS_SERV_MGMT_NOOP:
        case PVFS_SERV_IMM_COPIES:
            /* nothing else */
            break;

        case PVFS_SERV_INVALID:
        case PVFS_SERV_WRITE_COMPLETION:
        case PVFS_SERV_PERF_UPDATE:
        case PVFS_SERV_PRECREATE_POOL_REFILLER:
        case PVFS_SERV_JOB_TIMER:
        case PVFS_SERV_PROTO_ERROR:
        case PVFS_SERV_NUM_OPS:  /* sentinel */
            gossip_lerr("%s: invalid operation %d.\n", __func__, req->op);
            ret = -PVFS_EPROTO;
            goto out;
    }

#undef CASE

    if (ptr != (char *) input_buffer + input_size)
    {
        gossip_lerr("%s: op %d consumed %ld bytes, but message was %d bytes.\n",
                    __func__, req->op, (long)(ptr - (char *) input_buffer), input_size);
        ret = -PVFS_EPROTO;
    }

  out:
    return(ret);
}

/* lebf_decode_resp()
 *
 * decodes a response structure
 *
 * returns 0 on success, -errno on failure
 */
static int lebf_decode_resp(
    void *input_buffer,
    int input_size,
    struct PINT_decoded_msg *target_msg,
    PVFS_BMI_addr_t target_addr)
{
    int ret = 0;
    char *ptr = input_buffer;
    char **p = &ptr;
    struct PVFS_server_resp *resp = &target_msg->stub_dec.resp;

    target_msg->buffer = resp;

    /* decode generic part of response (including op number) */
    decode_PVFS_server_resp(p, resp);
    gossip_debug(GOSSIP_ENDECODE_DEBUG,"lebf_decode_resp\n");

    if (resp->status != 0) 
        goto out;

#define CASE(tag,var) \
    case tag: decode_PVFS_servresp_##var(p,&resp->u.var); break

    switch (resp->op) {

        /* call standard function defined in headers */
        CASE(PVFS_SERV_GETCONFIG, getconfig);
        CASE(PVFS_SERV_LOOKUP_PATH, lookup_path);
        CASE(PVFS_SERV_CREATE, create);
        CASE(PVFS_SERV_MIRROR, mirror);
        CASE(PVFS_SERV_UNSTUFF, unstuff);
        CASE(PVFS_SERV_BATCH_CREATE, batch_create);
        CASE(PVFS_SERV_IO, io);
        CASE(PVFS_SERV_SMALL_IO, small_io);
        CASE(PVFS_SERV_GETATTR, getattr);
        CASE(PVFS_SERV_RMDIRENT, rmdirent);
        CASE(PVFS_SERV_CHDIRENT, chdirent);
        CASE(PVFS_SERV_MKDIR, mkdir);
        CASE(PVFS_SERV_READDIR, readdir);
        CASE(PVFS_SERV_STATFS, statfs);
        CASE(PVFS_SERV_MGMT_PERF_MON, mgmt_perf_mon);
        CASE(PVFS_SERV_MGMT_ITERATE_HANDLES, mgmt_iterate_handles);
        CASE(PVFS_SERV_MGMT_DSPACE_INFO_LIST, mgmt_dspace_info_list);
        CASE(PVFS_SERV_MGMT_EVENT_MON, mgmt_event_mon);
        CASE(PVFS_SERV_MGMT_GET_DIRDATA_HANDLE, mgmt_get_dirdata_handle);
        CASE(PVFS_SERV_WRITE_COMPLETION, write_completion);
        CASE(PVFS_SERV_GETEATTR, geteattr);
        CASE(PVFS_SERV_ATOMICEATTR, atomiceattr);
        CASE(PVFS_SERV_LISTEATTR, listeattr);
        CASE(PVFS_SERV_LISTATTR, listattr);
        CASE(PVFS_SERV_TREE_GET_FILE_SIZE, tree_get_file_size);
        CASE(PVFS_SERV_TREE_REMOVE, tree_remove);
        CASE(PVFS_SERV_TREE_GETATTR, tree_getattr);
        CASE(PVFS_SERV_TREE_SETATTR, tree_setattr);
        CASE(PVFS_SERV_MGMT_GET_UID, mgmt_get_uid);
        CASE(PVFS_SERV_MGMT_GET_DIRENT, mgmt_get_dirent);
        CASE(PVFS_SERV_MGMT_GET_USER_CERT, mgmt_get_user_cert);
        CASE(PVFS_SERV_MGMT_GET_USER_CERT_KEYREQ, mgmt_get_user_cert_keyreq);
        case PVFS_SERV_REMOVE:
        case PVFS_SERV_BATCH_REMOVE:
        case PVFS_SERV_MGMT_REMOVE_OBJECT:
        case PVFS_SERV_MGMT_REMOVE_DIRENT:
        case PVFS_SERV_SETATTR:
        case PVFS_SERV_SETEATTR:
        case PVFS_SERV_DELEATTR:
        case PVFS_SERV_CRDIRENT:
        case PVFS_SERV_TRUNCATE:
        case PVFS_SERV_FLUSH:
        case PVFS_SERV_MGMT_NOOP:
        case PVFS_SERV_PROTO_ERROR:
        case PVFS_SERV_IMM_COPIES:
        case PVFS_SERV_MGMT_SETPARAM:
        case PVFS_SERV_MGMT_CREATE_ROOT_DIR:
        case PVFS_SERV_MGMT_SPLIT_DIRENT:
            /* nothing else */
            break;

        case PVFS_SERV_INVALID:
        case PVFS_SERV_PERF_UPDATE:
        case PVFS_SERV_PRECREATE_POOL_REFILLER:
        case PVFS_SERV_JOB_TIMER:
        case PVFS_SERV_NUM_OPS:  /* sentinel */
            gossip_lerr("%s: invalid operation %d.\n", __func__, resp->op);
            ret = -PVFS_EPROTO;
            goto out;
    }

#undef CASE

    if (ptr != (char *) input_buffer + input_size) {
        gossip_lerr("%s: op %d consumed %ld bytes, but message was %d bytes.\n",
                    __func__, resp->op, (long)(ptr - (char *) input_buffer),
                    input_size);
        ret = -PVFS_EPROTO;
    }

  out:
    return(ret);
}

/* lebf_encode_rel()
 *
 * releases resources consumed while encoding
 *
 * no return value 
 */
static void lebf_encode_rel(
    struct PINT_encoded_msg *msg,
    enum PINT_encode_msg_type input_type)
{
    gossip_debug(GOSSIP_ENDECODE_DEBUG,"lebf_encode_rel\n");
    /* just a single buffer to free */
    if (initializing_sizes)
    {
        free(msg->buffer_list[0]);
    }
    else
    {
        BMI_memfree(msg->dest, msg->buffer_list[0],
                    msg->alloc_size_list[0], BMI_SEND);
    }
}

/* lebf_decode_rel()
 *
 * releases resources consumed while decoding
 *
 * no return value
 */
static void lebf_decode_rel(struct PINT_decoded_msg *msg,
                            enum PINT_encode_msg_type input_type)
{
    gossip_debug(GOSSIP_ENDECODE_DEBUG,"lebf_decode_rel\n");
    if (input_type == PINT_DECODE_REQ) {
        struct PVFS_server_req *req = &msg->stub_dec.req;
        decode_free(req->capability.handle_array);
        decode_free(req->capability.signature);
        /* decode_free(req->capability.issuer); */
        switch (req->op) {
            case PVFS_SERV_CREATE:
                decode_free(req->u.create.credential.group_array);
                decode_free(req->u.create.credential.signature);
#ifdef ENABLE_SECURITY_CERT
                decode_free(req->u.create.credential.certificate.buf);
#endif
                if (req->u.create.attr.mask & PVFS_ATTR_META_DIST)
                    decode_free(req->u.create.attr.u.meta.dist);
                if (req->u.create.layout.server_list.servers)
                    decode_free(req->u.create.layout.server_list.servers);
                break;
            case PVFS_SERV_BATCH_CREATE:
                decode_free(
                    req->u.batch_create.handle_extent_array.extent_array);
                break;

            case PVFS_SERV_IO:
                decode_free(req->u.io.io_dist);
                decode_free(req->u.io.file_req);
                break;

            case PVFS_SERV_SMALL_IO:
                decode_free(req->u.small_io.dist);
                decode_free(req->u.small_io.file_req);
                break;

            case PVFS_SERV_MIRROR:
                decode_free(req->u.mirror.dist);
                decode_free(req->u.mirror.dst_handle);
                decode_free(req->u.mirror.wcIndex);
                break;

            case PVFS_SERV_MKDIR:
                decode_free(req->u.mkdir.handle_extent_array.extent_array);
                decode_free(req->u.mkdir.credential.group_array);
                decode_free(req->u.mkdir.credential.signature);
#ifdef ENABLE_SECURITY_CERT
                decode_free(req->u.mkdir.credential.certificate.buf);
#endif
                if (req->u.mkdir.attr.mask & PVFS_ATTR_META_DIST)
                    decode_free(req->u.mkdir.attr.u.meta.dist);
                if (req->u.mkdir.attr.mask & PVFS_ATTR_META_DFILES)
                    decode_free(req->u.mkdir.attr.u.meta.dfile_array);
                if (req->u.mkdir.attr.mask & PVFS_ATTR_CAPABILITY) {
                    decode_free(req->u.mkdir.attr.capability.handle_array);
                    decode_free(req->u.mkdir.attr.capability.signature);
                }
                break;

            case PVFS_SERV_MGMT_DSPACE_INFO_LIST:
                decode_free(req->u.mgmt_dspace_info_list.handle_array);
                break;

            case PVFS_SERV_SETATTR:
                decode_free(req->u.setattr.credential.group_array);
                decode_free(req->u.setattr.credential.signature);
#ifdef ENABLE_SECURITY_CERT
                decode_free(req->u.setattr.credential.certificate.buf);
#endif
                if (req->u.setattr.attr.mask & PVFS_ATTR_META_DIST)
                    decode_free(req->u.setattr.attr.u.meta.dist);
                if (req->u.setattr.attr.mask & PVFS_ATTR_META_DFILES)
                    decode_free(req->u.setattr.attr.u.meta.dfile_array);
                if (req->u.setattr.attr.mask
                        & PVFS_ATTR_DISTDIR_ATTR)
                {
                    if(req->u.setattr.attr.dist_dir_bitmap)
                    {
                        decode_free
                            (req->u.setattr.attr.dist_dir_bitmap);
                        req->u.setattr.attr.dist_dir_bitmap = NULL;
                    }
                    if(req->u.setattr.attr.dirdata_handles)
                    {
                        decode_free
                            (req->u.setattr.attr.dirdata_handles);
                        req->u.setattr.attr.dirdata_handles = NULL;
                    }
                }
                break;

            case PVFS_SERV_TREE_REMOVE:
                decode_free(req->u.tree_remove.handle_array);
                decode_free(req->u.tree_remove.credential.group_array);
                decode_free(req->u.tree_remove.credential.signature);
#ifdef ENABLE_SECURITY_CERT
                decode_free(req->u.tree_remove.credential.certificate.buf);
#endif
                break;

            case PVFS_SERV_TREE_GET_FILE_SIZE:
                decode_free(req->u.tree_get_file_size.handle_array);
                decode_free(req->u.tree_get_file_size.credential.group_array);
                decode_free(req->u.tree_get_file_size.credential.signature);
#ifdef ENABLE_SECURITY_CERT
                decode_free(req->u.tree_get_file_size.credential.certificate.buf);
#endif
                break;

            case PVFS_SERV_TREE_GETATTR:
                decode_free(req->u.tree_getattr.handle_array);
                decode_free(req->u.tree_getattr.credential.group_array);
                decode_free(req->u.tree_getattr.credential.signature);
#ifdef ENABLE_SECURITY_CERT
                decode_free(req->u.tree_getattr.credential.certificate.buf);
#endif
                break;

            case PVFS_SERV_TREE_SETATTR:
                decode_free(req->u.tree_setattr.handle_array);
                PINT_free_object_attr(&req->u.tree_setattr.attr);
                decode_free(req->u.tree_setattr.credential.group_array);
                decode_free(req->u.tree_setattr.credential.signature);
#ifdef ENABLE_SECURITY_CERT
                decode_free(req->u.tree_setattr.credential.certificate.buf);
#endif
                break;

            case PVFS_SERV_LISTATTR:
                if (req->u.listattr.handles)
                    decode_free(req->u.listattr.handles);
                break;

            case PVFS_SERV_GETATTR:
                decode_free(req->u.getattr.credential.group_array);
                decode_free(req->u.getattr.credential.signature);
#ifdef ENABLE_SECURITY_CERT
                decode_free(req->u.getattr.credential.certificate.buf);
#endif
                break;

            case PVFS_SERV_SETEATTR:
                decode_free(req->u.seteattr.key);
                decode_free(req->u.seteattr.val);
                break;

            case PVFS_SERV_GETEATTR:
                decode_free(req->u.geteattr.key);
                decode_free(req->u.geteattr.valsz);
                break;
                
            case PVFS_SERV_ATOMICEATTR:
                decode_free(req->u.atomiceattr.key);
                decode_free(req->u.atomiceattr.val);
                decode_free(req->u.atomiceattr.valsz);
                break;

            case PVFS_SERV_UNSTUFF:
                decode_free(req->u.unstuff.credential.group_array);
                decode_free(req->u.unstuff.credential.signature);
#ifdef ENABLE_SECURITY_CERT
                decode_free(req->u.unstuff.credential.certificate.buf);
#endif
                break;

            case PVFS_SERV_LOOKUP_PATH:
                decode_free(req->u.lookup_path.credential.group_array);
                decode_free(req->u.lookup_path.credential.signature);
#ifdef ENABLE_SECURITY_CERT
                decode_free(req->u.lookup_path.credential.certificate.buf);
#endif
                break;

            case PVFS_SERV_CRDIRENT:
                decode_free(req->u.crdirent.credential.group_array);
                decode_free(req->u.crdirent.credential.signature);
#ifdef ENABLE_SECURITY_CERT
                decode_free(req->u.crdirent.credential.certificate.buf);
#endif
                break;

            case PVFS_SERV_REMOVE:
                decode_free(req->u.remove.credential.group_array);
                decode_free(req->u.remove.credential.signature);
#ifdef ENABLE_SECURITY_CERT
                decode_free(req->u.remove.credential.certificate.buf);
#endif
                break;

            case PVFS_SERV_MGMT_SPLIT_DIRENT:
                decode_free(req->u.mgmt_split_dirent.dist);
                decode_free(req->u.mgmt_split_dirent.entry_handles);
                decode_free(req->u.mgmt_split_dirent.entry_names);
                break;

            case PVFS_SERV_GETCONFIG:
            case PVFS_SERV_MGMT_REMOVE_OBJECT:
            case PVFS_SERV_MGMT_REMOVE_DIRENT:
            case PVFS_SERV_MGMT_GET_DIRDATA_HANDLE:
            case PVFS_SERV_RMDIRENT:
            case PVFS_SERV_CHDIRENT:
            case PVFS_SERV_TRUNCATE:
            case PVFS_SERV_READDIR:
            case PVFS_SERV_FLUSH:
            case PVFS_SERV_MGMT_SETPARAM:
            case PVFS_SERV_MGMT_NOOP:
            case PVFS_SERV_STATFS:
            case PVFS_SERV_MGMT_ITERATE_HANDLES:
            case PVFS_SERV_MGMT_PERF_MON:
            case PVFS_SERV_MGMT_EVENT_MON:
            case PVFS_SERV_MGMT_GET_UID:
            case PVFS_SERV_MGMT_GET_DIRENT:
            case PVFS_SERV_DELEATTR:
            case PVFS_SERV_LISTEATTR:
            case PVFS_SERV_BATCH_REMOVE:
            case PVFS_SERV_IMM_COPIES:
            case PVFS_SERV_MGMT_CREATE_ROOT_DIR:
            case PVFS_SERV_MGMT_GET_USER_CERT_KEYREQ:
            case PVFS_SERV_MGMT_GET_USER_CERT:
              /*nothing to free*/
                  break;
            case PVFS_SERV_INVALID:
            case PVFS_SERV_WRITE_COMPLETION:
            case PVFS_SERV_PERF_UPDATE:
            case PVFS_SERV_PRECREATE_POOL_REFILLER:
            case PVFS_SERV_JOB_TIMER:
            case PVFS_SERV_PROTO_ERROR:            
            case PVFS_SERV_NUM_OPS:  /* sentinel */
                gossip_lerr("%s: invalid request operation %d.\n",
                  __func__, req->op);
                break;
        }
    } else if (input_type == PINT_DECODE_RESP) {
        struct PVFS_server_resp *resp = &msg->stub_dec.resp;

        if(resp->status == 0)
        {
            switch (resp->op)
            {                
                case PVFS_SERV_LOOKUP_PATH: 
                    {
                        struct PVFS_servresp_lookup_path *lookup =
                            &resp->u.lookup_path;
                        decode_free(lookup->handle_array);
                        decode_free(lookup->attr_array);
                        break;
                    }
                
                case PVFS_SERV_READDIR:
                    decode_free(resp->u.readdir.dirent_array);
                    break;

                case PVFS_SERV_MGMT_PERF_MON:
                    decode_free(resp->u.mgmt_perf_mon.perf_array);
                    break;

                case PVFS_SERV_MGMT_ITERATE_HANDLES:
                    decode_free(resp->u.mgmt_iterate_handles.handle_array);
                    break;
                
                case PVFS_SERV_BATCH_CREATE:
                    decode_free(resp->u.batch_create.handle_array);
                    break;
                
                case PVFS_SERV_CREATE:
                       if ( resp->u.create.metafile_attrs.mask & PVFS_ATTR_CAPABILITY )
                       {
                          decode_free(resp->u.create.metafile_attrs.capability.signature);
                          decode_free(resp->u.create.metafile_attrs.capability.handle_array);
                       }
                       if ( resp->u.create.metafile_attrs.mask & PVFS_ATTR_META_DFILES )
                       {
                          decode_free(resp->u.create.metafile_attrs.u.meta.dfile_array);
                       }
                    break;

                case PVFS_SERV_MGMT_DSPACE_INFO_LIST:
                    decode_free(resp->u.mgmt_dspace_info_list.dspace_info_array);
                    break;

                case PVFS_SERV_GETATTR:
                    if (resp->u.getattr.attr.mask & PVFS_ATTR_META_DIST)
                        decode_free(resp->u.getattr.attr.u.meta.dist);
                    if (resp->u.getattr.attr.mask & PVFS_ATTR_META_DFILES)
                       decode_free(resp->u.getattr.attr.u.meta.dfile_array);
                    if (resp->u.getattr.attr.mask 
                         & PVFS_ATTR_META_MIRROR_DFILES)
                       decode_free
                        (resp->u.getattr.attr.u.meta.mirror_dfile_array);
                    if (resp->u.getattr.attr.mask & PVFS_ATTR_CAPABILITY) {
                        decode_free(resp->u.getattr.attr.capability.handle_array);
                        decode_free(resp->u.getattr.attr.capability.signature);
                    }
                    if (resp->u.getattr.attr.mask
                         & PVFS_ATTR_DISTDIR_ATTR)
                    {
                       decode_free
                        (resp->u.getattr.attr.dist_dir_bitmap);
                       decode_free
                        (resp->u.getattr.attr.dirdata_handles);
                    }
                    break;

                case PVFS_SERV_UNSTUFF:
                    if (resp->u.unstuff.attr.mask & PVFS_ATTR_META_DIST)
                        decode_free(resp->u.unstuff.attr.u.meta.dist);
                    if (resp->u.unstuff.attr.mask & PVFS_ATTR_META_DFILES)
                    {
                        decode_free(resp->u.unstuff.attr.u.meta.dfile_array);
                    }
                    if (resp->u.unstuff.attr.mask 
                         & PVFS_ATTR_META_MIRROR_DFILES)
                       decode_free
                        (resp->u.unstuff.attr.u.meta.mirror_dfile_array);
                    break;

                case PVFS_SERV_MGMT_EVENT_MON:
                    decode_free(resp->u.mgmt_event_mon.event_array);
                    break;

                case PVFS_SERV_GETEATTR:
                    /* need a loop here?  WBL */
                    if (resp->u.geteattr.val)
                        decode_free(resp->u.geteattr.val);
                    break;
                case PVFS_SERV_ATOMICEATTR:
                    /* need a loop here? */
                    if (resp->u.geteattr.val)
                        decode_free(resp->u.geteattr.val);
                    break;
                case PVFS_SERV_LISTEATTR:
                    if (resp->u.listeattr.key)
                        decode_free(resp->u.listeattr.key);
                    break;
                case PVFS_SERV_LISTATTR:
                    {
                     int i;
                     if (resp->u.listattr.error)
                         decode_free(resp->u.listattr.error);
                     if (resp->u.listattr.attr) {
                         for (i = 0; i < resp->u.listattr.nhandles; i++) {
                          if (resp->u.listattr.attr[i].mask &
                                   PVFS_ATTR_META_DIST)
                           decode_free(resp->u.listattr.attr[i].u.meta.dist);
                          if (resp->u.listattr.attr[i].mask &
                                   PVFS_ATTR_META_DFILES)
                          {
                           decode_free(
                              resp->u.listattr.attr[i].u.meta.dfile_array);
                          }
                          if(resp->u.listattr.attr[i].mask &
                             PVFS_ATTR_META_MIRROR_DFILES)
                            decode_free(
                          resp->u.listattr.attr[i].u.meta.mirror_dfile_array);
                          if (resp->u.listattr.attr[i].mask & 
                              PVFS_ATTR_CAPABILITY) {
                           decode_free(
                             resp->u.listattr.attr[i].capability.handle_array);
                           decode_free(
                             resp->u.listattr.attr[i].capability.signature);
                          }
                         }/*end for*/
                         decode_free(resp->u.listattr.attr);
                     }/*end if attr*/
                        break;
                    }/*end case*/

                case PVFS_SERV_MIRROR:
                   {
                      decode_free(resp->u.mirror.bytes_written);
                      decode_free(resp->u.mirror.write_status_code);
                      break;
                   }

                case PVFS_SERV_TREE_REMOVE:
                   {
                      decode_free(resp->u.tree_remove.status);
                      break;
                   }

                case PVFS_SERV_TREE_GET_FILE_SIZE:
                   {
                      decode_free(resp->u.tree_get_file_size.size);
                      decode_free(resp->u.tree_get_file_size.error);
                      break;
                   }

                case PVFS_SERV_TREE_GETATTR:
                   {
                      decode_free(resp->u.tree_getattr.attr);
                      decode_free(resp->u.tree_getattr.error);
                      break;
                   }

                case PVFS_SERV_TREE_SETATTR:
                   {
                      decode_free(resp->u.tree_setattr.status);
                      break;
                   }

                case PVFS_SERV_MGMT_GET_UID:
                   {
                      decode_free(resp->u.mgmt_get_uid.uid_info_array);
                      break;
                   }
                case PVFS_SERV_MGMT_GET_USER_CERT:
                   { 
                      decode_free(resp->u.mgmt_get_user_cert.cert.buf);                      
                      break;
                   }

                case PVFS_SERV_MGMT_GET_USER_CERT_KEYREQ:
                   {
                      decode_free(resp->u.mgmt_get_user_cert_keyreq.public_key.buf);
                      break;
                   }
                case PVFS_SERV_GETCONFIG:
                case PVFS_SERV_REMOVE:
                case PVFS_SERV_MGMT_REMOVE_OBJECT:
                case PVFS_SERV_MGMT_REMOVE_DIRENT:
                case PVFS_SERV_MGMT_GET_DIRDATA_HANDLE:
                case PVFS_SERV_IO:
                case PVFS_SERV_SMALL_IO:
                case PVFS_SERV_SETATTR:
                case PVFS_SERV_SETEATTR:
                case PVFS_SERV_DELEATTR:
                case PVFS_SERV_CRDIRENT:
                case PVFS_SERV_RMDIRENT:
                case PVFS_SERV_CHDIRENT:
                case PVFS_SERV_TRUNCATE:
                case PVFS_SERV_MKDIR:
                case PVFS_SERV_FLUSH:
                case PVFS_SERV_MGMT_SETPARAM:
                case PVFS_SERV_MGMT_NOOP:
                case PVFS_SERV_STATFS:
                case PVFS_SERV_WRITE_COMPLETION:
                case PVFS_SERV_PROTO_ERROR:
                case PVFS_SERV_BATCH_REMOVE:
                case PVFS_SERV_IMM_COPIES:
                case PVFS_SERV_MGMT_GET_DIRENT:
                case PVFS_SERV_MGMT_CREATE_ROOT_DIR:
                case PVFS_SERV_MGMT_SPLIT_DIRENT:
                  /*nothing to free */
                   break;
                case PVFS_SERV_INVALID:
                case PVFS_SERV_PERF_UPDATE:
                case PVFS_SERV_PRECREATE_POOL_REFILLER:
                case PVFS_SERV_JOB_TIMER:
                case PVFS_SERV_NUM_OPS:  /* sentinel */
                    gossip_lerr("%s: invalid response operation %d.\n",
                                __func__, resp->op);
                    break;
            }
        }
    }
}

static int check_req_size(struct PVFS_server_req *req)
{
    struct PINT_encoded_msg msg;
    int size;

    gossip_debug(GOSSIP_ENDECODE_DEBUG,"check_req_size\n");
    lebf_encode_req(req, &msg);
    size = msg.total_size;
    lebf_encode_rel(&msg, 0);
    return size;
}

static int check_resp_size(struct PVFS_server_resp *resp)
{
    struct PINT_encoded_msg msg;
    int size;

    gossip_debug(GOSSIP_ENDECODE_DEBUG,"check_resp_size\n");
    lebf_encode_resp(resp, &msg);
    size = msg.total_size;
    lebf_encode_rel(&msg, 0);
    return size;
}

static void zero_capability(PVFS_capability *cap)
{
    cap->issuer = (char *) PVFS2_BLANK_ISSUER;
    cap->sig_size = 0;
    cap->num_handles = 0;
}

static void zero_credential(PVFS_credential *cred)
{
    cred->issuer = (char *) PVFS2_BLANK_ISSUER;
    cred->num_groups = 0;
    cred->sig_size = 0;
}

static PINT_encoding_functions lebf_functions = {
    lebf_encode_req,
    lebf_encode_resp,
    lebf_decode_req,
    lebf_decode_resp,
    lebf_encode_rel,
    lebf_decode_rel,
    lebf_encode_calc_max_size
};

PINT_encoding_table_values le_bytefield_table = {
    &lebf_functions,
    "little endian bytefield",
    lebf_initialize,
    lebf_finalize
};

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
