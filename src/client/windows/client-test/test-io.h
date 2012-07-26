/* Copyright (C) 2011 Omnibond, LLC
   Client test - IO declarations */

#ifndef __TEST_IO_H
#define __TEST_IO_H

#include "test-support.h"

int io_file(global_options *options, int fatal);
int flush_file(global_options *options, int fatal);
int io_file_mt(global_options *options, int fatal);

#endif