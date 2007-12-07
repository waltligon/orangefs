/*
 * (C) 2007 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

enum
{
    CREATE_RETRY = 170
};

/* state function prototypes */
PINT_sm_action create_init(
    PINT_smcb *smcb, job_status_s *js_p);
PINT_sm_action create_dspace_create_setup_msgpair(
    PINT_smcb *smcb, job_status_s *js_p);
PINT_sm_action create_datafiles_setup_msgpair_array(
    PINT_smcb *smcb, job_status_s *js_p);
PINT_sm_action create_datafiles_failure(
    PINT_smcb *smcb, job_status_s *js_p);
PINT_sm_action create_setattr_setup_msgpair(
    PINT_smcb *smcb, job_status_s *js_p);
PINT_sm_action create_setattr_failure(
    PINT_smcb *smcb, job_status_s *js_p);
PINT_sm_action create_crdirent_setup_msgpair(
    PINT_smcb *smcb, job_status_s *js_p);
PINT_sm_action create_crdirent_failure(
    PINT_smcb *smcb, job_status_s *js_p);
PINT_sm_action create_delete_handles_setup_msgpair_array(
    PINT_smcb *smcb, job_status_s *js_p);
PINT_sm_action create_cleanup(
    PINT_smcb *smcb, job_status_s *js_p);
PINT_sm_action create_parent_getattr_inspect(
    PINT_smcb *smcb, job_status_s *js_p);
/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

