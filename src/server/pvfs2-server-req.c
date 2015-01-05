/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "pvfs2-server.h"
#include <assert.h>
#include "pvfs2-internal.h"
#include "assert.h"

/* server operation state machines */
extern struct PINT_server_req_params pvfs2_get_config_params;
extern struct PINT_server_req_params pvfs2_get_attr_params;
extern struct PINT_server_req_params pvfs2_list_attr_params;
extern struct PINT_server_req_params pvfs2_set_attr_params;
extern struct PINT_server_req_params pvfs2_create_params;
extern struct PINT_server_req_params pvfs2_crdirent_params;
extern struct PINT_server_req_params pvfs2_mkdir_params;
extern struct PINT_server_req_params pvfs2_readdir_params;
extern struct PINT_server_req_params pvfs2_lookup_params;
extern struct PINT_server_req_params pvfs2_io_params;
extern struct PINT_server_req_params pvfs2_small_io_params;
extern struct PINT_server_req_params pvfs2_remove_params;
extern struct PINT_server_req_params pvfs2_mgmt_remove_object_params;
extern struct PINT_server_req_params pvfs2_mgmt_remove_dirent_params;
extern struct PINT_server_req_params pvfs2_mgmt_get_dirdata_handle_params;
extern struct PINT_server_req_params pvfs2_rmdirent_params;
extern struct PINT_server_req_params pvfs2_chdirent_params;
extern struct PINT_server_req_params pvfs2_flush_params;
extern struct PINT_server_req_params pvfs2_truncate_params;
extern struct PINT_server_req_params pvfs2_setparam_params;
extern struct PINT_server_req_params pvfs2_noop_params;
extern struct PINT_server_req_params pvfs2_unexpected_params;
extern struct PINT_server_req_params pvfs2_statfs_params;
extern struct PINT_server_req_params pvfs2_perf_update_params;
extern struct PINT_server_req_params pvfs2_job_timer_params;
extern struct PINT_server_req_params pvfs2_proto_error_params;
extern struct PINT_server_req_params pvfs2_perf_mon_params;
extern struct PINT_server_req_params pvfs2_iterate_handles_params;
extern struct PINT_server_req_params pvfs2_get_eattr_params;
extern struct PINT_server_req_params pvfs2_get_eattr_list_params;
extern struct PINT_server_req_params pvfs2_set_eattr_params;
extern struct PINT_server_req_params pvfs2_set_eattr_list_params;
extern struct PINT_server_req_params pvfs2_atomic_eattr_params;
extern struct PINT_server_req_params pvfs2_atomic_eattr_lists_params;
extern struct PINT_server_req_params pvfs2_del_eattr_params;
extern struct PINT_server_req_params pvfs2_list_eattr_params;
extern struct PINT_server_req_params pvfs2_batch_create_params;
extern struct PINT_server_req_params pvfs2_batch_remove_params;
extern struct PINT_server_req_params pvfs2_unstuff_params;
extern struct PINT_server_req_params pvfs2_stuffed_create_params;
extern struct PINT_server_req_params pvfs2_precreate_pool_refiller_params;
extern struct PINT_server_req_params pvfs2_mirror_params;
extern struct PINT_server_req_params pvfs2_create_immutable_copies_params;
extern struct PINT_server_req_params pvfs2_tree_remove_params;
extern struct PINT_server_req_params pvfs2_tree_get_file_size_params;
extern struct PINT_server_req_params pvfs2_uid_mgmt_params;
extern struct PINT_server_req_params pvfs2_tree_setattr_params;
extern struct PINT_server_req_params pvfs2_mgmt_get_dirent_params;
extern struct PINT_server_req_params pvfs2_mgmt_create_root_dir_params;
extern struct PINT_server_req_params pvfs2_mgmt_split_dirent_params;
extern struct PINT_server_req_params pvfs2_tree_getattr_params;
#ifdef ENABLE_SECURITY_CERT
extern struct PINT_server_req_params pvfs2_get_user_cert_params;
extern struct PINT_server_req_params pvfs2_get_user_cert_keyreq_params;
#endif

/* table of incoming request types and associated parameters */
struct PINT_server_req_entry PINT_server_req_table[] =
{
    /* 0 */ {PVFS_SERV_INVALID, NULL},
    /* 1 */ {PVFS_SERV_CREATE, &pvfs2_create_params},
    /* 2 */ {PVFS_SERV_REMOVE, &pvfs2_remove_params},
    /* 3 */ {PVFS_SERV_IO, &pvfs2_io_params},
    /* 4 */ {PVFS_SERV_GETATTR, &pvfs2_get_attr_params},
    /* 5 */ {PVFS_SERV_SETATTR, &pvfs2_set_attr_params},
    /* 6 */ {PVFS_SERV_LOOKUP_PATH, &pvfs2_lookup_params},
    /* 7 */ {PVFS_SERV_CRDIRENT, &pvfs2_crdirent_params},
    /* 8 */ {PVFS_SERV_RMDIRENT, &pvfs2_rmdirent_params},
    /* 9 */ {PVFS_SERV_CHDIRENT, &pvfs2_chdirent_params},
    /* 10 */ {PVFS_SERV_TRUNCATE, &pvfs2_truncate_params},
    /* 11 */ {PVFS_SERV_MKDIR, &pvfs2_mkdir_params},
    /* 12 */ {PVFS_SERV_READDIR, &pvfs2_readdir_params},
    /* 13 */ {PVFS_SERV_GETCONFIG, &pvfs2_get_config_params},
    /* 14 */ {PVFS_SERV_WRITE_COMPLETION, NULL},
    /* 15 */ {PVFS_SERV_FLUSH, &pvfs2_flush_params},
    /* 16 */ {PVFS_SERV_MGMT_SETPARAM, &pvfs2_setparam_params},
    /* 17 */ {PVFS_SERV_MGMT_NOOP, &pvfs2_noop_params},
    /* 18 */ {PVFS_SERV_STATFS, &pvfs2_statfs_params},
    /* 19 */ {PVFS_SERV_PERF_UPDATE, &pvfs2_perf_update_params},
    /* 20 */ {PVFS_SERV_MGMT_PERF_MON, &pvfs2_perf_mon_params},
    /* 21 */ {PVFS_SERV_MGMT_ITERATE_HANDLES, &pvfs2_iterate_handles_params},
    /* 22 */ {PVFS_SERV_MGMT_DSPACE_INFO_LIST, NULL},
    /* 23 */ {PVFS_SERV_MGMT_EVENT_MON, NULL},
    /* 24 */ {PVFS_SERV_MGMT_REMOVE_OBJECT, &pvfs2_mgmt_remove_object_params},
    /* 25 */ {PVFS_SERV_MGMT_REMOVE_DIRENT, &pvfs2_mgmt_remove_dirent_params},
    /* 26 */ {PVFS_SERV_MGMT_GET_DIRDATA_HANDLE, &pvfs2_mgmt_get_dirdata_handle_params},
    /* 27 */ {PVFS_SERV_JOB_TIMER, &pvfs2_job_timer_params},
    /* 28 */ {PVFS_SERV_PROTO_ERROR, &pvfs2_proto_error_params},
    /* 29 */ {PVFS_SERV_GETEATTR, &pvfs2_get_eattr_params},
    /* 30 */ {PVFS_SERV_SETEATTR, &pvfs2_set_eattr_params},
    /* 31 */ {PVFS_SERV_DELEATTR, &pvfs2_del_eattr_params},
    /* 32 */ {PVFS_SERV_LISTEATTR, &pvfs2_list_eattr_params},
    /* 33 */ {PVFS_SERV_SMALL_IO, &pvfs2_small_io_params},
    /* 34 */ {PVFS_SERV_LISTATTR, &pvfs2_list_attr_params},
    /* 35 */ {PVFS_SERV_BATCH_CREATE, &pvfs2_batch_create_params},
    /* 36 */ {PVFS_SERV_BATCH_REMOVE, &pvfs2_batch_remove_params},
    /* 37 */ {PVFS_SERV_PRECREATE_POOL_REFILLER, &pvfs2_precreate_pool_refiller_params},
    /* 38 */ {PVFS_SERV_UNSTUFF, &pvfs2_unstuff_params},
    /* 39 */ {PVFS_SERV_MIRROR, &pvfs2_mirror_params},
    /* 40 */ {PVFS_SERV_IMM_COPIES, &pvfs2_create_immutable_copies_params},
    /* 41 */ {PVFS_SERV_TREE_REMOVE, &pvfs2_tree_remove_params},
    /* 42 */ {PVFS_SERV_TREE_GET_FILE_SIZE, &pvfs2_tree_get_file_size_params},
    /* 43 */ {PVFS_SERV_MGMT_GET_UID, &pvfs2_uid_mgmt_params},
    /* 44 */ {PVFS_SERV_TREE_SETATTR, &pvfs2_tree_setattr_params},
    /* 45 */ {PVFS_SERV_MGMT_GET_DIRENT, &pvfs2_mgmt_get_dirent_params},
    /* 46 */ {PVFS_SERV_MGMT_CREATE_ROOT_DIR, &pvfs2_mgmt_create_root_dir_params},
    /* 47 */ {PVFS_SERV_MGMT_SPLIT_DIRENT, &pvfs2_mgmt_split_dirent_params},
    /* 48 */ {PVFS_SERV_ATOMICEATTR, &pvfs2_atomic_eattr_params},
    /* 49 */ {PVFS_SERV_TREE_GETATTR, &pvfs2_tree_getattr_params},
#ifdef ENABLE_SECURITY_CERT    
    /* 50 */ {PVFS_SERV_MGMT_GET_USER_CERT, &pvfs2_get_user_cert_params},
    /* 51 */ {PVFS_SERV_MGMT_GET_USER_CERT_KEYREQ, &pvfs2_get_user_cert_keyreq_params}
#else
    /* 50 */ {PVFS_SERV_MGMT_GET_USER_CERT, NULL},
    /* 51 */ {PVFS_SERV_MGMT_GET_USER_CERT_KEYREQ, NULL},
#endif
};

#define CHECK_OP(_op_) assert(_op_ == PINT_server_req_table[_op_].op_type)

enum PINT_server_req_access_type PINT_server_req_readonly(
                                    struct PVFS_server_req *req)
{
    return PINT_SERVER_REQ_READONLY;
}

enum PINT_server_req_access_type PINT_server_req_modify(
                                    struct PVFS_server_req *req)
{
    return PINT_SERVER_REQ_MODIFY;
}

PINT_server_req_perm_fun
PINT_server_req_get_perm_fun(struct PVFS_server_req *req)
{
    CHECK_OP(req->op);
    return PINT_server_req_table[req->op].params->perm;
}

enum PINT_server_req_access_type
PINT_server_req_get_access_type(struct PVFS_server_req *req)
{
    CHECK_OP(req->op);

    if(!PINT_server_req_table[req->op].params->access_type)
    {
        return PINT_SERVER_REQ_READONLY;
    }
    return PINT_server_req_table[req->op].params->access_type(req);
}

enum PINT_server_sched_policy
PINT_server_req_get_sched_policy(struct PVFS_server_req *req)
{
    CHECK_OP(req->op);
    return PINT_server_req_table[req->op].params->sched_policy;
}

int PINT_server_req_get_object_ref(
    struct PVFS_server_req *req, PVFS_fs_id *fs_id, PVFS_handle *handle)
{
    CHECK_OP(req->op);

    if(!PINT_server_req_table[req->op].params->get_object_ref)
    {
        *fs_id = 0;
        *handle = 0;
        return 0;
    }
    else
    {
        return PINT_server_req_table[req->op].params->get_object_ref(
            req, fs_id, handle);
    }
}

int PINT_server_req_get_credential(
    struct PVFS_server_req *req, PVFS_credential **cred)
{
    int ret;
    CHECK_OP(req->op);

    if (!PINT_server_req_table[req->op].params->get_credential)
    {
        *cred = NULL;
        ret = 0;
    }
    else
    {
        ret = PINT_server_req_table[req->op].params->get_credential(
            req, cred);
    }

    return ret;
}

/*
 * PINT_map_server_op_to_string()
 *
 * provides a string representation of the server operation number
 *
 * returns a pointer to a static string (DONT FREE IT) on success,
 * null on failure
 */
const char* PINT_map_server_op_to_string(enum PVFS_server_op op)
{
    CHECK_OP(op);
    return PINT_server_req_table[op].params->string_name;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
