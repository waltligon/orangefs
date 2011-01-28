/* Copyright (C) 2011 Omnibond, LLC
   Client -- A test of a test */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "test-support.h"

int test_test(op_options *options)
{
    
    report_result("test-test", RESULT_SUCCESS, 0, OPER_EQUAL, 0);

    return 0;
}