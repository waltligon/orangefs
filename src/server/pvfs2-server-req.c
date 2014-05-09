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

/* Table of incoming request types and associated parameters.
 * THIS MUST BE IN THE SAME ORDER AS enum PVFS_server_op in
 * src/proto/pvfs2-req-proto.h. */
struct PINT_server_req_params *PINT_server_req_table[] =
{
    /* 0 */ NULL,
    /* 1 */ &pvfs2_create_params,
    /* 2 */ &pvfs2_remove_params,
    /* 3 */ &pvfs2_io_params,
    /* 4 */ &pvfs2_get_attr_params,
    /* 5 */ &pvfs2_set_attr_params,
    /* 6 */ &pvfs2_lookup_params,
    /* 7 */ &pvfs2_crdirent_params,
    /* 8 */ &pvfs2_rmdirent_params,
    /* 9 */ &pvfs2_chdirent_params,
    /* 10 */ &pvfs2_truncate_params,
    /* 11 */ &pvfs2_mkdir_params,
    /* 12 */ &pvfs2_readdir_params,
    /* 13 */ &pvfs2_get_config_params,
    /* 14 */ NULL,
    /* 15 */ &pvfs2_flush_params,
    /* 16 */ &pvfs2_setparam_params,
    /* 17 */ &pvfs2_noop_params,
    /* 18 */ &pvfs2_statfs_params,
    /* 19 */ &pvfs2_perf_update_params,
    /* 20 */ &pvfs2_perf_mon_params,
    /* 21 */ &pvfs2_iterate_handles_params,
    /* 22 */ NULL,
    /* 23 */ NULL,
    /* 24 */ &pvfs2_mgmt_remove_object_params,
    /* 25 */ &pvfs2_mgmt_remove_dirent_params,
    /* 26 */ &pvfs2_mgmt_get_dirdata_handle_params,
    /* 27 */ &pvfs2_job_timer_params,
    /* 28 */ &pvfs2_proto_error_params,
    /* 29 */ &pvfs2_get_eattr_params,
    /* 30 */ &pvfs2_set_eattr_params,
    /* 31 */ &pvfs2_del_eattr_params,
    /* 32 */ &pvfs2_list_eattr_params,
    /* 33 */ &pvfs2_small_io_params,
    /* 34 */ &pvfs2_list_attr_params,
    /* 35 */ &pvfs2_batch_create_params,
    /* 36 */ &pvfs2_batch_remove_params,
    /* 37 */ &pvfs2_precreate_pool_refiller_params,
    /* 38 */ &pvfs2_unstuff_params,
    /* 39 */ &pvfs2_mirror_params,
    /* 40 */ &pvfs2_create_immutable_copies_params,
    /* 41 */ &pvfs2_tree_remove_params,
    /* 42 */ &pvfs2_tree_get_file_size_params,
    /* 43 */ &pvfs2_uid_mgmt_params,
    /* 44 */ &pvfs2_tree_setattr_params,
    /* 45 */ &pvfs2_mgmt_get_dirent_params,
    /* 46 */ &pvfs2_mgmt_create_root_dir_params,
    /* 47 */ &pvfs2_mgmt_split_dirent_params,
    /* 48 */ &pvfs2_atomic_eattr_params,
    /* 49 */ &pvfs2_tree_getattr_params,
#ifdef ENABLE_SECURITY_CERT    
    /* 50 */ &pvfs2_get_user_cert_params,
    /* 51 */ &pvfs2_get_user_cert_keyreq_params,
#else
    /* 50 */ NULL,
    /* 51 */ NULL,
#endif
};

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
    return PINT_server_req_table[req->op]->perm;
}

enum PINT_server_req_access_type
PINT_server_req_get_access_type(struct PVFS_server_req *req)
{
    if (PINT_server_req_table[req->op]->access_type != NULL)
        return PINT_server_req_table[req->op]->access_type(req);
    else
        return PINT_SERVER_REQ_READONLY;
}

enum PINT_server_sched_policy
PINT_server_req_get_sched_policy(struct PVFS_server_req *req)
{
    return PINT_server_req_table[req->op]->sched_policy;
}

int PINT_server_req_get_object_ref(
    struct PVFS_server_req *req, PVFS_fs_id *fs_id, PVFS_handle *handle)
{
    if(!PINT_server_req_table[req->op]->get_object_ref)
    {
        *fs_id = 0;
        *handle = 0;
        return 0;
    }
    else
    {
        return PINT_server_req_table[req->op]->get_object_ref(
            req, fs_id, handle);
    }
}

int PINT_server_req_get_credential(
    struct PVFS_server_req *req, PVFS_credential **cred)
{
    if (!PINT_server_req_table[req->op]->get_credential)
    {
        *cred = NULL;
        return 0;
    }
    else
    {
        return PINT_server_req_table[req->op]->get_credential(
            req, cred);
    }
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
    return PINT_server_req_table[op]->string_name;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
