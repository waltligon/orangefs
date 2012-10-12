/*
 * (C) 2010-2011 Clemson University and Omnibond LLC
 *
 * See COPYING in top-level directory.
 */
   
/* 
 * Configuration file function declarations
 */

#ifndef __CONFIG_H
#define __CONFIG_H

#include "client-service.h"

int get_config(PORANGEFS_OPTIONS options,
               char *error_msg,
               unsigned int error_msg_len);

#endif