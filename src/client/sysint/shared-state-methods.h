/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __SHARED_STATE_METHODS_H
#define __SHARED_STATE_METHODS_H

/*
  this file is for storing common methods that are shared
  between the following client state machines implementations:

  sys-create.sm
  sys-mkdir.sm
  sys-symlink.sm
*/

/*
  shared/common state operation functions
*/
int sm_common_parent_getattr_setup_msgpair(PINT_client_sm *sm_p,
                                           job_status_s *js_p);
int sm_common_parent_getattr_failure(PINT_client_sm *sm_p,
                                     job_status_s *js_p);
int sm_common_object_getattr_setup_msgpair(PINT_client_sm *sm_p,
                                           job_status_s *js_p);
int sm_common_object_getattr_failure(PINT_client_sm *sm_p,
                                     job_status_s *js_p);

/*
  shared/common msgpair completion functions
*/
int sm_common_directory_getattr_comp_fn(void *v_p,
                                        struct PVFS_server_resp *resp_p,
                                        int index);
int sm_common_object_getattr_comp_fn(void *v_p,
                                     struct PVFS_server_resp *resp_p,
                                     int index);

#endif /* __SHARED_STATE_METHODS_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
