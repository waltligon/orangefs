/* Copyright (C) 2011 Omnibond, LLC
   Client service declarations */

#ifndef __CLIENT_SERVICE_H
#define __CLIENT_SERVICE_H

#include "quicklist.h"

typedef struct
{
    char mount_point[MAX_PATH];
    char cert_dir_prefix[MAX_PATH];
    char ca_path[MAX_PATH];
    int threads;
    int debug;
} ORANGEFS_OPTIONS, *PORANGEFS_OPTIONS;

typedef struct
{
    struct qlist_head list_link;
    char user_name[256];
    int uid;
    int gid;
} ORANGEFS_USER, *PORANGEFS_USER;

void DbgPrint(char *format, ...);

#endif