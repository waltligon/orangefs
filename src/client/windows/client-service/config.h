/*
 * (C) 2010-2022 Omnibond Systems, LLC
 *
 * See COPYING in top-level directory.
 */
   
/* 
 * Configuration file function declarations
 */

#ifndef __CONFIG_H
#define __CONFIG_H

#include "pvfs2-types.h"
#include "quicklist.h"

#include "client-service.h"

/* struct for user entries */
typedef struct {
    char user_name[STR_BUF_LEN];
    PVFS_uid uid;
    PVFS_gid gid;
    struct qlist_head link;
} CONFIG_USER_ENTRY, *PCONFIG_USER_ENTRY;

/* callback definition for keyword processing */
typedef struct {
    const char *keyword;
    int min_args, max_args;
    int (*keyword_cb)(PORANGEFS_OPTIONS options, const char *keyword, 
                      char **args, char *error_msg);
} CONFIG_KEYWORD_DEF, *PCONFIG_KEYWORD_DEF;

int get_config(PORANGEFS_OPTIONS options,
               char *error_msg,
               unsigned int error_msg_len);

int add_users(PORANGEFS_OPTIONS options,
              char *error_msg,
              unsigned int error_msg_len);

#endif