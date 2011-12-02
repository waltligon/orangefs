/* Copyright (C) 2011 Omnibond, LLC
   Client test - function declarations */

#ifndef __TEST_LIST_H
#define __TEST_LIST_H

#include "test-support.h"

#include "create.h"
#include "open.h"
#include "test-io.h"
#include "file-ops.h"
#include "info.h"
#include "find.h"

typedef struct 
{
    const char *name;
    int (*function) (global_options *, int);
    int fatal;
} test_operation;

test_operation op_table[] =
{
    /*{"test-test", test_test, FALSE}, */
    {"create-dir", create_dir, TRUE},
    {"create-subdir", create_subdir, FALSE},
    {"create-dir-toolong", create_dir_toolong, FALSE},
    {"create-files", create_files, TRUE},
    {"create-file-long", create_file_long, FALSE},
    {"create-file-toolong", create_file_toolong, FALSE},
    {"create-files-many", create_files_many, FALSE},
    {"open-file", open_file, TRUE},
    {"io-file", io_file, FALSE},
    {"flush-file", flush_file, FALSE},
    {"delete-file", delete_file, FALSE},
    {"delete-file-notexist", delete_file_notexist, FALSE},
    {"delete-dir-empty", delete_dir_empty, FALSE},
    {"delete-dir-notempty", delete_dir_notempty, FALSE},
    {"rename-file", rename_file, FALSE},
    {"rename-file-exist", rename_file_exist, FALSE},
    {"move-file", move_file, FALSE},
    {"move-file-baddir", move_file_baddir, FALSE},
    {"move-file-exist", move_file_exist, FALSE},
    {"file-time", file_time, FALSE},
#ifdef WIN32
    {"volume-space", volume_space, FALSE},
    {"find-files", find_files, FALSE},
    {"find-files-pattern", find_files_pattern, FALSE},
#endif
    {"io-file-mt", io_file_mt, FALSE},
    {NULL, NULL, 0}
};


#endif
