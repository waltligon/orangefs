/* Copyright (C) 2011 Omnibond, LLC
   Client testing - support routines */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "test-support.h"

void report_result(const char *test_name,
                   int expected_result,
                   int expected_code,
                   int code_operation,
                   int actual_code)
{

    /* TODO: report to console and/or file */

    /* TODO: vary success based on code operation */

    printf("%s %s %d %d %s\n", test_name, 
        (expected_result == RESULT_SUCCESS) ? "EXPECT_SUCCESS" : "EXPECT_FAILURE",
        expected_code, actual_code,
        (expected_code == actual_code) ? "OK" : "NOT_OK");

}

