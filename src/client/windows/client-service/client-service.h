/* Copyright (C) 2011 Omnibond, LLC
   Client service declarations */

#ifndef __CLIENT_SERVICE_H
#define __CLIENT_SERVICE_H

typedef struct
{
    char mount_point[MAX_PATH];
    int threads;
    int debug;
} ORANGEFS_OPTIONS, *PORANGEFS_OPTIONS;


#endif