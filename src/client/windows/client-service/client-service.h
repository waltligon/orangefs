/*
 * (C) 2010-2011 Clemson University and Omnibond LLC
 *
 * See COPYING in top-level directory.
 */
   
/*
 * Client service declarations 
 */

#ifndef __CLIENT_SERVICE_H
#define __CLIENT_SERVICE_H

#include "quicklist.h"

#define USER_MODE_NONE 0
#define USER_MODE_LIST 1
#define USER_MODE_CERT 2
#define USER_MODE_LDAP 3

typedef struct
{
    char host[256];
    int port;
    int secure;
    char bind_dn[256];
    char bind_password[32];
    char search_root[256];
    int search_scope;
    char search_class[32];
    char naming_attr[32];
    char uid_attr[32];
    char gid_attr[32];
} LDAP_OPTIONS, *PLDAP_OPTIONS;

typedef struct
{
    char mount_point[MAX_PATH];
    char cert_dir_prefix[MAX_PATH];
    char ca_path[MAX_PATH];
    int threads;
    unsigned int new_file_perms,
                 new_dir_perms;
    int debug;
    int debug_stderr;
    char debug_mask[256];
    char debug_file[MAX_PATH];
    int user_mode;
    LDAP_OPTIONS ldap;
} ORANGEFS_OPTIONS, *PORANGEFS_OPTIONS;

void DbgPrint(char *format, ...);

BOOL report_error_event(char *message, 
                        BOOL startup);

#endif