/*
 * Copyright © Acxiom Corporation, 2005
 *
 * See COPYING in top-level directory.
 */
 
#ifndef __FSCK_UTILS_H
#define __FSCK_UTILS_H

#include "pvfs2.h"
#include "pvfs2-mgmt.h"
#include "pvfs2-internal.h"
#include "pint-cached-config.h"
#include "pint-sysint-utils.h"

#include <stdlib.h>

/** \defgroup fsckutils FSCK Utilities 
 *
 * The fsck-utils implements a API for checking validity of different PVFS_objects.
 *
 * List of PVFS_Object types with API's allowing validity checks:
 * - PVFS_TYPE_METAFILE
 * - PVFS_TYPE_DIRECTORY
 * - PVFS_TYPE_DATAFILE
 * - PVFS_TYPE_DIRDATA
 * - PVFS_TYPE_SYMLINK
 * .
 * The API is broken up into two sections. The first section simply validates
 * whether an object is valid. The second section will perform some action 
 * in attempt to either fix or remove the problem.
 *
 * @see PINT_fsck_options for optional file system checks
 *
 * Any driver program calling this will need to initialize the PVFS system 
 * interface first.
 * @{
 */

/** \file
 * Declarations for the fsck utility component.
 */

/** FSCK options */
struct PINT_fsck_options
{
    unsigned int fix_errors;             /**< fix errors found */
    unsigned int check_stranded_objects; /**< check for stranded objects */
    unsigned int check_symlink_target;   /**< checks symlink target for bad practice */
    unsigned int check_dir_entry_names;  /**< checks dirent names for bad practice */
    unsigned int verbose;                /**< enable verbose output */
    unsigned int check_fs_configs;       /**< verify fs config files */
    char* start_path;                    /**< PVFS2 path to begin check */
};

int PVFS_fsck_initialize(
    const struct PINT_fsck_options* options,
    const PVFS_credentials* creds,
    const PVFS_fs_id* cur_fs);

int PVFS_fsck_validate_dfile(
    const struct PINT_fsck_options* fsck_options,
    const PVFS_handle* handle,
    const PVFS_fs_id* cur_fs,
    const PVFS_credentials* creds,
    PVFS_size* dfile_total_size);

int PVFS_fsck_validate_dfile_attr(
    const struct PINT_fsck_options* fsck_options,
    const PVFS_sysresp_getattr* attributes);

int PVFS_fsck_validate_metafile(
    const struct PINT_fsck_options* fsck_options,
    const PVFS_object_ref* obj_ref,
    const PVFS_sysresp_getattr* attributes,
    const PVFS_credentials* creds);

int PVFS_fsck_validate_metafile_attr(
    const struct PINT_fsck_options* fsck_options,
    const PVFS_sysresp_getattr* attributes);

int PVFS_fsck_validate_symlink(
    const struct PINT_fsck_options* fsck_options,
    const PVFS_object_ref* obj_ref, 
    const PVFS_sysresp_getattr* attributes);

int PVFS_fsck_validate_symlink_attr(
    const struct PINT_fsck_options* fsck_options,
    const PVFS_sysresp_getattr* attributes);

int PVFS_fsck_validate_symlink_target(
    const struct PINT_fsck_options* fsck_options,
    const PVFS_sysresp_getattr* attributes);

int PVFS_fsck_validate_dirdata(
    const struct PINT_fsck_options* fsck_options,
    const PVFS_handle* handle, 
    const PVFS_fs_id* cur_fs, 
    const PVFS_credentials* creds);

int PVFS_fsck_validate_dirdata_attr(
    const struct PINT_fsck_options* fsck_options,
    const PVFS_sysresp_getattr* attributes);

int PVFS_fsck_validate_dir(
    const struct PINT_fsck_options* fsck_options,
    const PVFS_object_ref* obj_ref, 
    const PVFS_sysresp_getattr* attributes, 
    const PVFS_credentials* creds,
    PVFS_dirent* directory_entries);

int PVFS_fsck_validate_dir_attr(
    const struct PINT_fsck_options* fsck_options,
    const PVFS_sysresp_getattr* attributes);

int PVFS_fsck_validate_dir_ent(
    const struct PINT_fsck_options* fsck_options,
    const char* filename);

int PVFS_fsck_finalize(
    const struct PINT_fsck_options* fsck_options,
    const PVFS_fs_id* cur_fs,
    const PVFS_credentials*);

int PVFS_fsck_get_attributes(
    const struct PINT_fsck_options*,
    const PVFS_object_ref*,
    const PVFS_credentials*,
    PVFS_sysresp_getattr* );

int PVFS_fsck_check_server_configs(
    const struct PINT_fsck_options*,
    const PVFS_credentials*,
    const PVFS_fs_id*);

/** TODO: 
 * Need to add functions to repair problems.  (PVFS2_fsck_fix_XXXX() ?)
 */ 

#endif

/* @} */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
