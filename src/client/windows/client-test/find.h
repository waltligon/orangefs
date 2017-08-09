/* Copyright (C) 2011 Omnibond, LLC
   Client tests - find file declarations */

#ifndef __FIND_H
#define __FIND_H

#ifdef WIN32

#include "test-support.h"

int find_files(global_options *options, int fatal);
int find_files_pattern(global_options *options, int fatal);

#endif

#endif