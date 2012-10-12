/* Copyright (C) 2011 Omnibond, LLC
   Client tests -- file operation declarations */

#ifndef __FILE_OPS_H
#define __FILE_OPS_H

#include "test-support.h"

int delete_file(global_options *options, int fatal);
int delete_file_notexist(global_options *options, int fatal);
int delete_dir_empty(global_options *options, int fatal);
int delete_dir_notempty(global_options *options, int fatal);

int rename_file(global_options *options, int fatal);
int rename_file_exist(global_options *options, int fatal);

int move_file(global_options *options, int fatal);
int move_file_baddir(global_options *options, int fatal);
int move_file_exist(global_options *options, int fatal);

#endif