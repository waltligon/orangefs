/* Copyright (C) 2011 Omnibond, LLC
   Client service declarations */

#ifndef __CLIENT_SERVICE_H
#define __CLIENT_SERVICE_H

#include "quicklist.h"

#define USER_MODE_NONE 0
#define USER_MODE_LIST 1
#define USER_MODE_CERT 2
#define USER_MODE_LDAP 3

typedef struct
{
    char mount_point[MAX_PATH];
    char cert_dir_prefix[MAX_PATH];
    char ca_path[MAX_PATH];
    int threads;
    int debug;
    int user_mode;
} ORANGEFS_OPTIONS, *PORANGEFS_OPTIONS;

void DbgPrint(char *format, ...);

#endif