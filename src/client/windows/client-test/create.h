/* Copyright (C) 2011 Omnibond, LLC
   Client test - create function prototypes */

#ifndef __CREATE_H
#define __CREATE_H

#include "test-support.h"

int create_dir(global_options *options, int fatal);
int create_subdir(global_options *options, int fatal);
int create_dir_toolong(global_options *options, int fatal);
int create_files(global_options *options, int fatal);
int create_file_long(global_options *options, int fatal);
int create_file_toolong(global_options *options, int fatal);
int create_files_many(global_options *options, int fatal);

#endif