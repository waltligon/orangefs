/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __STATE_MACHINE_H
#define __STATE_MACHINE_H

#include "pvfs2-internal.h"
#include "job.h"
#include "quicklist.h"
#include "server-config-mgr.h"

/* STATE-MACHINE.H
 *
 * This file sets up all the definitions necessary for our state machine
 * implementation.  This is set up in this (somewhat obscure) way because
 * we want:
 * - a very fast state machine implementation (few dereferences)
 * - easy access to the state data for the implementation
 * - reuse across client and server (different state data)
 *
 * The important thing to note about this file is that it requires that
 * PINT_OP_STATE be defined.  This must be a typedef to the structure
 * that holds the necessary state information for a given state machine.
 * There are four fields that must exist and are used by the state machine
 * implementation:
 * - int op;
 * - int stackptr;
 * - PINT_state_array_values *current_state;
 * - PINT_state_array_values *state_stack[PINT_STATE_STACK_SIZE];
 *
 * Also, PINT_STATE_STACK_SIZE must be defined or enum'd before that
 * declaration.
 *
 * The file state-machine-fns.h defines a set of functions for use in
 * interacting with the state machine.  There are also a couple of other
 * functions that must be defined, in particular some sort of initialization
 * function.  See src/server/server-state-machine.c for examples.
 */

/* these are state runtime control that directs each state in
 * executing.  Most common are RUN, JUMP, PJMP and SWITCH
 */
enum PINT_state_code {
    SM_NONE   = 0,
    SM_NEXT   = 1,
    SM_RETURN = 2,
    SM_EXTERN = 3,
    SM_NESTED = 5,
    SM_JUMP   = 6,
    SM_TERM   = 7,
    SM_PJMP   = 8,
    SM_RUN    = 9,
    SM_SWITCH = 10
};

/*define msgpairarray parameters for server-to-server requests*/
#define PINT_serv_init_msgarray_params(sm_p, __fsid)             \
do {                                                             \
   PINT_sm_msgpair_params *mpp = &sm_p->msgarray_op.params;      \
   struct server_configuration_s *server_config =                \
        PINT_server_config_mgr_get_config();                     \
   mpp->job_context = server_job_context;                        \
   if (server_config)                                            \
   {                                                             \
      mpp->job_timeout = server_config->client_job_bmi_timeout;  \
      mpp->retry_limit = server_config->client_retry_limit;      \
      mpp->retry_delay = server_config->client_retry_delay_ms;   \
   }                                                             \
   else                                                          \
   {                                                             \
      mpp->job_timeout = PVFS2_CLIENT_JOB_BMI_TIMEOUT_DEFAULT;   \
      mpp->retry_limit = PVFS2_CLIENT_RETRY_LIMIT_DEFAULT;       \
      mpp->retry_delay = PVFS2_CLIENT_RETRY_DELAY_MS_DEFAULT;    \
   }                                                             \
} while (0)                                                      

/* these define things like stack size and so forth for the common
 * state machine code.
 * The state stack size limits the number of nested state machines that
 * can exist.  8 seems reasonable.
 */
#define PINT_STATE_STACK_SIZE 8

struct PINT_state_stack_s
{
    struct PINT_state_s *state;
    int prev_base_frame;
};

/* these structures are for the frame stack used by the SMCB.
 * The frame stack defines memory areas used by different SMs.
 * and can be manually pushed and pop'd when one SM calls a
 * different SM.  Frames can be of different types and sizes.
 * currently it is up to the code to keep it straight, but 
 * hopefully a new revision will use existing fields to keep up
 * with the type and size of each frame.  This was recently
 * moved from state-machine-fns.c to state-machine.h.
 */

/* a mod to facilitate sharing of frames for housekeeping
 * not for normal use.  Addind a struct which points to
 * each frame with fields.  Frame functions have both usual
 * and frame_info versions.  The frame_info version let the
 * code have access to attributes such as frame type and
 * size.  Right now we don't use these, but we will.
 */

/* Every frame has a frame_info that hold generic data for
 * the frame independent of the scmb's it is linked to.
 * The frame_info points to the frame.
 */

struct PINT_frame_info_s
{
    int ftype;   /* 0 unknown, 1 s_op, 2 mop, 3 sm_p */
    int fsize;   /* in bytes */
    int frefcnt; /* manages sharing */
    void *frame;
};

/* Each smcb has a stack of frames inplemented into a
 * linked list.  These are the items in the list. These
 * have a pointer to the frame_info for each frame, which
 * in turn point to the actual frames.
 */
struct PINT_frame_s
{
    int task_id;
    struct PINT_frame_info_s *frame_info;
    int error;
    struct qlist_head link;
};

#define PVFS_debug_frame_stack(mask, smcb) \
do { \
    gossip_if(mask) \
    { \
        struct PINT_frame_s *pos = NULL; \
        int cnt = 0; \
        gossip_lsadebug("Debug Frame Stack " #smcb " (%p):\n", smcb); \
        qlist_for_each_prev_entry(pos, &smcb->frames, link) \
        { \
            char str[2] = ""; \
            if(cnt - smcb->base_frame == 0) \
            { \
                sprintf(str, "*"); \
            } \
            gossip_lsadebug("Frame %d%s:\n", cnt, str); \
            gossip_lsadebug("  task id %d\n", pos->task_id); \
            gossip_lsadebug("  ftype %d\n", pos->frame_info->ftype); \
            gossip_lsadebug("  fsize %d\n", pos->frame_info->fsize); \
            gossip_lsadebug("  frefcnt %d\n", pos->frame_info->frefcnt); \
            gossip_lsadebug("  frame (%p)\n", pos->frame_info->frame); \
            gossip_lsadebug("  error %d\n", pos->error); \
            cnt++; \
        } \
        gossip_lsadebug("End Debug Frame Stack\n"); \
    } \
    gossip_end; \
} while (0)

/* State machine control block - one per running instance of a state
 * machine
 */
typedef struct PINT_smcb
{
    /* state machine execution variables */
    int stackptr;
    struct PINT_state_s *current_state;
    struct PINT_state_stack_s state_stack[PINT_STATE_STACK_SIZE];

    struct qlist_head frames;  /* circular list of frames */
    int base_frame;            /* index of current base frame */
    int frame_count;           /* number of frames in list */
    int pjmp_frame_count;      /* number of PJMP frames on the stack */
    int children_running;      /* the number of child SMs running */
                               /* different from pjmp_frame_count because */
                               /* this decrements as tasks finish */

    /* Official copies of credentials and capability */
    PVFS_credential *credential;
    PVFS_capability *capability;
    /* usage specific routine to look up SM from OP */
    struct PINT_state_machine_s *(*op_get_state_machine)(int, int);
    /* state machine context and control variables */
    int op; /* this field externally indicates type of state machine/request */
    PVFS_id_gen_t op_id; /* unique ID for this operation */
    struct PINT_smcb *parent_smcb; /* points to parent smcb or NULL */
    int op_terminate; /* indicates SM is ready to terminate */
    int op_cancelled; /* indicates SM operation was cancelled */
    int op_completed;  /* indicates SM operation was added to completion Q */
    /* add a lock here */
    job_context_id context; /* job context when waiting for children */
    int (*terminate_fn)(struct PINT_smcb *, job_status_s *);
    void *user_ptr; /* external user pointer */
    int immediate; /* specifies immediate completion of the state machine */
} PINT_smcb;

#define PINT_SET_OP_COMPLETE do{PINT_smcb_set_complete(smcb);} while (0)

struct PINT_state_machine_s
{
    const char *name;
    struct PINT_state_s *first_state;
};

struct PINT_state_s
{
    const char *state_name;
    struct PINT_state_machine_s *parent_machine;
    enum PINT_state_code flag;
    union
    {
        int (*func)(struct PINT_smcb *, job_status_s *);
        struct PINT_state_machine_s *nested;
    } action;
    struct PINT_pjmp_tbl_s *pjtbl;
    struct PINT_tran_tbl_s *trtbl;
};

struct PINT_pjmp_tbl_s
{
    int return_value;
    enum PINT_state_code flag;
    struct PINT_state_machine_s *state_machine;
};

struct PINT_tran_tbl_s
{
    int return_value;
    enum PINT_state_code flag;
    struct PINT_state_s *next_state;
};

/* All state action functions return this type which controls state
 * machine action
 */
typedef enum {
    SM_ACTION_DEFERRED = 0,
    SM_ACTION_COMPLETE = 1,
    SM_ACTION_TERMINATE = 2,
    SM_ERROR = -1             /* this is a catastrophic error */
} PINT_sm_action;

extern char * PINT_sm_action_string[];
#define SM_ACTION_STRING(action) \
    (action == SM_ERROR ? "ERROR" : PINT_sm_action_string[action])

#define SM_ACTION_ISERR(ret) ((ret)<0)
#define SM_ACTION_ISVALID(ret)     \
    (ret == SM_ACTION_DEFERRED  || \
     ret == SM_ACTION_COMPLETE  || \
     ret == SM_ACTION_TERMINATE || \
     ret == SM_ERROR)

/* what is this type? */
enum {
    JMP_NOT_READY = 99,
    DEFAULT_ERROR = -1,
};

/*Added info for ftype and fsize updates*/
typedef enum Ftype {
    UNKNOWN,
    S_OP,
    M_OP,
    SM_P
}Ftype;

typedef struct{
    int id;
    const char* type_name;
    int size;
}Frame_type;

extern Frame_type fsizes[];
extern int fsizes_len;
int lookup_fsize(int id);

#define ENCODE_TYPE 0
#define SM_STATE_RETURN -1
#define SM_NESTED_STATE 1

/* Prototypes for functions provided by user */
int PINT_state_machine_complete(void *);

/* This macro returns the state machine string of the current machine.
 * We assume the first 6 characters of every state machine name are "pvfs2_".
 */
#define PINT_state_machine_current_machine_name(smcb) \
    ((smcb)->current_state ? (((smcb)->current_state->parent_machine->name) + 0) : "UNKNOWN")

/*    
 *  ((smcb)->current_state ? (((smcb)->current_state->parent_machine->name) + 6) : "UNKNOWN")
 */   


/* This macro returns the current state invoked */
#define PINT_state_machine_current_state_name(smcb) \
    ((smcb)->current_state ? ((smcb)->current_state->state_name) : "UNKNOWN")

/* Prototypes for functions defined in by state machine code */
int PINT_state_machine_halt(void);
int PINT_state_machine_terminate(struct PINT_smcb *, job_status_s *);
PINT_sm_action PINT_state_machine_next(struct PINT_smcb *,job_status_s *);
PINT_sm_action PINT_state_machine_invoke(struct PINT_smcb *, job_status_s *);
PINT_sm_action PINT_state_machine_start(struct PINT_smcb *, job_status_s *);
PINT_sm_action PINT_state_machine_continue(struct PINT_smcb *smcb,
                                           job_status_s *r);
#ifdef WIN32
int PINT_state_machine_locate(struct PINT_smcb *, int);
#else
int PINT_state_machine_locate(struct PINT_smcb *, int) __attribute__((used));
#endif
int PINT_smcb_set_op(struct PINT_smcb *smcb, int op);

int PINT_smcb_op(struct PINT_smcb *smcb);

int PINT_smcb_immediate_completion(struct PINT_smcb *smcb);

void PINT_smcb_set_complete(struct PINT_smcb *smcb);

int PINT_smcb_invalid_op(struct PINT_smcb *smcb);

int PINT_smcb_complete(struct PINT_smcb *smcb);

void PINT_smcb_set_cancelled(struct PINT_smcb *smcb);

int PINT_smcb_cancelled(struct PINT_smcb *smcb);

int PINT_smcb_alloc(struct PINT_smcb **,
                    int,
                    Ftype type,
                    struct PINT_state_machine_s *(*getmach)(int, int),
                    int (*term_fn)(struct PINT_smcb *,
                    job_status_s *),
                    job_context_id context_id);

void PINT_smcb_free(struct PINT_smcb *);

struct PINT_frame_info_s *PINT_sm_frame_info(struct PINT_smcb *, int);

void *PINT_sm_frame(struct PINT_smcb *, int);

int PINT_sm_push_frame_info(struct PINT_smcb *smcb,
                            int task_id,
                            struct PINT_frame_info_s *frame_p);

int PINT_sm_push_frame(struct PINT_smcb *smcb, int task_id, void *frame_p, Ftype type);

int PINT_sm_push_frame_ref(struct PINT_smcb *smcb,
                           int task_id,
                           void *frame_p,
                           int refcnt, 
                           Ftype type);

int PINT_sm_push_dup_frame(struct PINT_smcb *smcb, int frame_size);

struct PINT_frame_info_s *PINT_sm_pop_frame_info(struct PINT_smcb *smcb,
                                                 int *task_id,
                                                 int *error_code,
                                                 int *remaining, 
                                                 Ftype* type);
void *PINT_sm_pop_frame(struct PINT_smcb *smcb,
                        int *task_id,
                        int *error_code,
                        int *remaining, 
                        Ftype* type);

PINT_sm_action PINT_sm_pop_old_pjmp_frames(struct PINT_smcb *smcb, int child_count);

int PINT_sm_pop_top_frames(struct PINT_smcb *smcb);

/* This macro is used in calls to PINT_sm_frame() */
#define PINT_FRAME_CURRENT 0
#define PINT_FRAME_PARENT -1
#define PINT_FRAME_TOP 1

extern struct PINT_state_machine_s pvfs2_void_sm;

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

#endif
