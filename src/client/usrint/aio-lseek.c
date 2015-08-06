/* WARNING: THIS FILE IS AUTOMATICALLY GENERATED FROM A .SM FILE.
 * Changes made here will certainly be overwritten.
 */

/* 
 * (C) 2012 Clemson University
 *
 * See COPYING in top-level directory.
 */

#include "pvfs2-internal.h"
#include "pvfs2-aio.h"
#include "client-state-machine.h"
#include "str-utils.h"

extern job_context_id pint_client_sm_context;

enum
{
    LSEEK_GETATTR_SM = 333,
    LSEEK_READDIR_SM
};


static PINT_sm_action lseek_init(
	struct PINT_smcb *smcb, job_status_s *js_p);

static struct PINT_state_s ST_init;
static struct PINT_pjmp_tbl_s ST_init_pjtbl[];
static struct PINT_tran_tbl_s ST_init_trtbl[];
static struct PINT_state_s ST_getattr_sm;
static struct PINT_pjmp_tbl_s ST_getattr_sm_pjtbl[];
static struct PINT_tran_tbl_s ST_getattr_sm_trtbl[];

static PINT_sm_action lseek_dir_check(
	struct PINT_smcb *smcb, job_status_s *js_p);

static struct PINT_state_s ST_dir_check;
static struct PINT_pjmp_tbl_s ST_dir_check_pjtbl[];
static struct PINT_tran_tbl_s ST_dir_check_trtbl[];
static struct PINT_state_s ST_readdir_sm;
static struct PINT_pjmp_tbl_s ST_readdir_sm_pjtbl[];
static struct PINT_tran_tbl_s ST_readdir_sm_trtbl[];

static PINT_sm_action lseek_readdir_check(
	struct PINT_smcb *smcb, job_status_s *js_p);

static struct PINT_state_s ST_readdir_check;
static struct PINT_pjmp_tbl_s ST_readdir_check_pjtbl[];
static struct PINT_tran_tbl_s ST_readdir_check_trtbl[];

static PINT_sm_action lseek_cleanup(
	struct PINT_smcb *smcb, job_status_s *js_p);

static struct PINT_state_s ST_cleanup;
static struct PINT_pjmp_tbl_s ST_cleanup_pjtbl[];
static struct PINT_tran_tbl_s ST_cleanup_trtbl[];

struct PINT_state_machine_s pvfs2_client_aio_lseek_sm = {
	.name = "pvfs2_client_aio_lseek_sm",
	.first_state = &ST_init
};

static struct PINT_state_s ST_init = {
	 .state_name = "init" ,
	 .parent_machine = &pvfs2_client_aio_lseek_sm ,
	 .flag = SM_RUN ,
	 .action.func = lseek_init ,
	 .pjtbl = NULL ,
	 .trtbl = ST_init_trtbl 
};

static struct PINT_tran_tbl_s ST_init_trtbl[] = {
	{ .return_value = LSEEK_GETATTR_SM ,
	 .next_state = &ST_getattr_sm },
	{ .return_value = 0 ,
	 .next_state = &ST_dir_check },
	{ .return_value = -1 ,
	 .next_state = &ST_cleanup }
};

static struct PINT_state_s ST_getattr_sm = {
	 .state_name = "getattr_sm" ,
	 .parent_machine = &pvfs2_client_aio_lseek_sm ,
	 .flag = SM_JUMP ,
	 .action.nested = &pvfs2_client_getattr_sm ,
	 .pjtbl = NULL ,
	 .trtbl = ST_getattr_sm_trtbl 
};

static struct PINT_tran_tbl_s ST_getattr_sm_trtbl[] = {
	{ .return_value = -1 ,
	 .next_state = &ST_dir_check }
};

static struct PINT_state_s ST_dir_check = {
	 .state_name = "dir_check" ,
	 .parent_machine = &pvfs2_client_aio_lseek_sm ,
	 .flag = SM_RUN ,
	 .action.func = lseek_dir_check ,
	 .pjtbl = NULL ,
	 .trtbl = ST_dir_check_trtbl 
};

static struct PINT_tran_tbl_s ST_dir_check_trtbl[] = {
	{ .return_value = LSEEK_READDIR_SM ,
	 .next_state = &ST_readdir_sm },
	{ .return_value = -1 ,
	 .next_state = &ST_cleanup }
};

static struct PINT_state_s ST_readdir_sm = {
	 .state_name = "readdir_sm" ,
	 .parent_machine = &pvfs2_client_aio_lseek_sm ,
	 .flag = SM_JUMP ,
	 .action.nested = &pvfs2_client_readdir_sm ,
	 .pjtbl = NULL ,
	 .trtbl = ST_readdir_sm_trtbl 
};

static struct PINT_tran_tbl_s ST_readdir_sm_trtbl[] = {
	{ .return_value = -1 ,
	 .next_state = &ST_readdir_check }
};

static struct PINT_state_s ST_readdir_check = {
	 .state_name = "readdir_check" ,
	 .parent_machine = &pvfs2_client_aio_lseek_sm ,
	 .flag = SM_RUN ,
	 .action.func = lseek_readdir_check ,
	 .pjtbl = NULL ,
	 .trtbl = ST_readdir_check_trtbl 
};

static struct PINT_tran_tbl_s ST_readdir_check_trtbl[] = {
	{ .return_value = -1 ,
	 .next_state = &ST_cleanup }
};

static struct PINT_state_s ST_cleanup = {
	 .state_name = "cleanup" ,
	 .parent_machine = &pvfs2_client_aio_lseek_sm ,
	 .flag = SM_RUN ,
	 .action.func = lseek_cleanup ,
	 .pjtbl = NULL ,
	 .trtbl = ST_cleanup_trtbl 
};

static struct PINT_tran_tbl_s ST_cleanup_trtbl[] = {
	{ .return_value = -1 ,

	 .flag = SM_TERM }
};

#ifndef WIN32
# 64 "src/client/usrint/aio-lseek.sm"
#endif


/*********************************************************/

PVFS_error PVFS_iaio_lseek(
    pvfs_descriptor *pd,
    off64_t offset,
    int whence,
    const PVFS_credential *credential,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr)
{
    int ret = -PVFS_EINVAL;
    PINT_smcb *smcb = NULL;
    PINT_client_sm *sm_p = NULL;

    gossip_debug(GOSSIP_CLIENT_DEBUG, "PVFS_iaio_lseek entered\n");

    if (!pd)
    {
        gossip_err("invalid (NULL) required arguments\n");
        return ret;
    }

    PINT_smcb_alloc(&smcb, PVFS_AIO_LSEEK,
                    sizeof(struct PINT_client_sm),
                    client_op_state_get_machine,
                    client_state_machine_terminate,
                    pint_client_sm_context);
    if (smcb == NULL)
    {
        return -PVFS_ENOMEM;
    }
    sm_p = PINT_sm_frame(smcb, PINT_FRAME_CURRENT);

    PINT_init_sysint_credentials(sm_p->cred_p, credential);
    PVFS_hint_copy(hints, &(sm_p->hints));
    sm_p->u.aio_lseek.pd = pd;
    sm_p->u.aio_lseek.offset = offset;
    sm_p->u.aio_lseek.whence = whence;

    return PINT_client_state_machine_post(
        smcb, op_id, user_ptr);
}

PVFS_error PVFS_aio_lseek(
    pvfs_descriptor *pd,
    off64_t offset,
    int whence,
    const PVFS_credential *credential,
    PVFS_hint hints)
{
    PVFS_error ret = -PVFS_EINVAL, error = 0;
    PVFS_sys_op_id op_id;

    gossip_debug(GOSSIP_CLIENT_DEBUG, "PVFS_aio_lseek entered\n");

    ret = PVFS_iaio_lseek(pd, offset, whence, credential, &op_id, hints, NULL);
    
    if (ret)
    {
        PVFS_perror_gossip("PVFS_iaio_lseek call", ret);
        error = ret;
    }
    else
    {
        ret = PVFS_sys_wait(op_id, "aio_lseek", &error);
        if (ret)
        {
            PVFS_perror_gossip("PVFS_sys_wait call", ret);
            error = ret;
        }
    }

    PINT_sys_release(op_id);
    return error;
}

/*******************************************************************/

static PINT_sm_action lseek_init(
        struct PINT_smcb *smcb, job_status_s *js_p)
{
    struct PINT_client_sm *sm_p = PINT_sm_frame(smcb, PINT_FRAME_CURRENT);
    
    gossip_debug(GOSSIP_CLIENT_DEBUG, "aio_lseek state: init\n");

    js_p->error_code = 0;

    switch(sm_p->u.aio_lseek.whence)
    {
        case SEEK_SET:
        {
            sm_p->u.aio_lseek.pd->s->file_pointer = sm_p->u.aio_lseek.offset;
            break;
        }
        case SEEK_CUR:
        {
            sm_p->u.aio_lseek.pd->s->file_pointer += sm_p->u.aio_lseek.offset;
            break;
        }
        case SEEK_END:
        {
            /* need to run getattr to find file size */
            PINT_client_sm *getattr_frame = malloc(sizeof(PINT_client_sm));
            if (getattr_frame == NULL)
            {
                js_p->error_code = -PVFS_ENOMEM;
                return SM_ACTION_COMPLETE;
            }
            memset(getattr_frame, 0, sizeof(PINT_client_sm));

            PINT_init_msgarray_params(getattr_frame,
                                      sm_p->u.aio_lseek.pd->s->pvfs_ref.fs_id);
            PINT_init_sysint_credentials(getattr_frame->cred_p, sm_p->cred_p);
            getattr_frame->error_code = 0;
            getattr_frame->object_ref = sm_p->u.aio_lseek.pd->s->pvfs_ref;
            getattr_frame->u.getattr.getattr_resp_p = &(sm_p->u.aio_lseek.getattr_resp);
            PVFS_hint_copy(sm_p->hints, &(getattr_frame->hints));
            PVFS_hint_add(&(getattr_frame->hints), PVFS_HINT_HANDLE_NAME,
                          sizeof(PVFS_handle),
                          &(sm_p->u.aio_lseek.pd->s->pvfs_ref.handle));

            PINT_SM_GETATTR_STATE_FILL(
                getattr_frame->getattr,
                sm_p->u.aio_lseek.pd->s->pvfs_ref,
                PVFS_ATTR_DATA_SIZE,
                PVFS_TYPE_NONE,
                0);

            PINT_sm_push_frame(smcb, 0, (void *)getattr_frame);
            js_p->error_code = LSEEK_GETATTR_SM;
            break;
        }
        default:
        {
            js_p->error_code = -PVFS_EINVAL;
            break;;   
        }
    }

    return SM_ACTION_COMPLETE;
}

static PINT_sm_action lseek_dir_check(
        struct PINT_smcb *smcb, job_status_s *js_p)
{
    struct PINT_client_sm *sm_p = PINT_sm_frame(smcb, PINT_FRAME_CURRENT);
    
    if (sm_p->u.aio_lseek.whence == SEEK_END)
    {
        int frame_id, frames_remaining;
        struct PINT_client_sm *getattr_frame = (PINT_client_sm *)
                                        PINT_sm_pop_frame(smcb,
                                            &frame_id, &js_p->error_code,
                                            &frames_remaining);

        js_p->error_code = getattr_frame->error_code;
        if (js_p->error_code)
        {
            free(getattr_frame);
            return SM_ACTION_COMPLETE;
        }

        sm_p->u.aio_lseek.pd->s->file_pointer = 
            getattr_frame->getattr.size + sm_p->u.aio_lseek.offset;                    
        free(getattr_frame);
    }

    if (S_ISDIR(sm_p->u.aio_lseek.pd->s->mode))
    {
        int dirent_no = sm_p->u.aio_lseek.pd->s->file_pointer /
                        sizeof(PVFS_dirent);
        sm_p->u.aio_lseek.pd->s->file_pointer = dirent_no * sizeof(PVFS_dirent);
        sm_p->u.aio_lseek.pd->s->token = PVFS_READDIR_START;
        if (dirent_no)
        {
            /* run a readdir sm */
            struct PINT_client_sm *readdir_frame = malloc(sizeof(PINT_client_sm));
            if (!readdir_frame)
            {
                js_p->error_code = -PVFS_ENOMEM;
                return SM_ACTION_COMPLETE;  
            }
            memset(readdir_frame, 0, sizeof(PINT_client_sm));

            if ((sm_p->u.aio_lseek.pd->s->pvfs_ref.handle == PVFS_HANDLE_NULL) ||
                (sm_p->u.aio_lseek.pd->s->pvfs_ref.fs_id == PVFS_FS_ID_NULL))
            {
                js_p->error_code = -PVFS_EINVAL;
                return SM_ACTION_COMPLETE;
            }

            if (dirent_no > PVFS_REQ_LIMIT_DIRENT_COUNT)
            {
                js_p->error_code = -PVFS_EINVAL;
                return SM_ACTION_COMPLETE;
            }

            PINT_init_msgarray_params(readdir_frame,
                                      sm_p->u.aio_lseek.pd->s->pvfs_ref.fs_id);
            PINT_init_sysint_credentials(readdir_frame->cred_p, sm_p->cred_p);
            readdir_frame->readdir.readdir_resp = &(sm_p->u.aio_lseek.readdir_resp);
            readdir_frame->object_ref = sm_p->u.aio_lseek.pd->s->pvfs_ref;
            PVFS_hint_copy(sm_p->hints, &readdir_frame->hints);
            PVFS_hint_add(&readdir_frame->hints, PVFS_HINT_HANDLE_NAME,
                          sizeof(PVFS_handle),
                          &sm_p->u.aio_lseek.pd->s->pvfs_ref.handle);

            /* point the sm dirent array and outcount to readdir resp field */
            readdir_frame->readdir_state.dirent_array = &sm_p->u.aio_lseek.readdir_resp.dirent_array;
            readdir_frame->readdir_state.dirent_outcount = &sm_p->u.aio_lseek.readdir_resp.pvfs_dirent_outcount;
            readdir_frame->readdir_state.token = &sm_p->u.aio_lseek.readdir_resp.token;
            readdir_frame->readdir_state.directory_version = &sm_p->u.aio_lseek.readdir_resp.directory_version;

            readdir_frame->readdir_state.pos_token = readdir_frame->readdir.pos_token =
                sm_p->u.aio_lseek.pd->s->token;
            readdir_frame->readdir_state.dirent_limit = readdir_frame->readdir.dirent_limit = dirent_no;

            js_p->error_code = LSEEK_READDIR_SM;
            PINT_sm_push_frame(smcb, 0, (void *)readdir_frame);
        }
    }

    return SM_ACTION_COMPLETE;
}

static PINT_sm_action lseek_readdir_check(
        struct PINT_smcb *smcb, job_status_s *js_p)
{
    struct PINT_client_sm *sm_p = PINT_sm_frame(smcb, PINT_FRAME_CURRENT);

    gossip_debug(GOSSIP_CLIENT_DEBUG, "aio_lseek state: readdir_check\n");

    int frame_id, frames_remaining;
    struct PINT_client_sm *readdir_frame = (PINT_client_sm *)
                                        PINT_sm_pop_frame(smcb,
                                            &frame_id, &js_p->error_code,
                                            &frames_remaining);

    js_p->error_code = readdir_frame->error_code;
    if (js_p->error_code >= 0)
    {
        sm_p->u.aio_lseek.pd->s->token = sm_p->u.aio_lseek.readdir_resp.token;
        free(sm_p->u.aio_lseek.readdir_resp.dirent_array);
    }

    free(readdir_frame);
    return SM_ACTION_COMPLETE;
}

static PINT_sm_action lseek_cleanup(
        struct PINT_smcb *smcb, job_status_s *js_p)
{
    struct PINT_client_sm *sm_p = PINT_sm_frame(smcb, PINT_FRAME_CURRENT);

    gossip_debug(GOSSIP_CLIENT_DEBUG, "aio_lseek state: cleanup\n");

    sm_p->error_code = js_p->error_code;

    PINT_SET_OP_COMPLETE;
    return SM_ACTION_TERMINATE;
}

/*
 * Local variables:
 *  mode: c
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ft=c ts=8 sts=4 sw=4 expandtab
 */
