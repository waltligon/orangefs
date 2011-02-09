/* Copyright (C) 2011 Omnibond, LLC
   Client test - function declarations */

#ifndef __TEST_LIST_H
#define __TEST_LIST_H

#include "test-support.h"

#include "create.h"
#include "open.h"
#include "test-io.h"

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
    {"create-subdir", create_subdir, TRUE},
    {"create-dir-toolong", create_dir_toolong, FALSE},
    {"create-files", create_files, TRUE},
    {"create-file-long", create_file_long, FALSE},
    {"create-file-toolong", create_file_toolong, FALSE},
    {"create-files-many", create_files_many, FALSE},
    {"open-file", open_file, TRUE},
    {"io-file", io_file, FALSE},
    {"flush-file", flush_file, FALSE},
    {NULL, NULL, 0}
};


#endif
