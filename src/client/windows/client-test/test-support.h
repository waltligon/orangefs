/* Copyright (C) 2011 Omnibond, LLC 
   Client testing -- declarations */

#ifndef __TESTS_H
#define __TESTS_H

typedef struct
{
    const char *root_dir;
} op_options;

/* constants */
#define RESULT_SUCCESS   0
#define RESULT_FAILURE   1

#define OPER_EQUAL          0
#define OPER_NOTEQUAL       1
#define OPER_LESS           2
#define OPER_GREATER        3
#define OPER_LESSOREQUAL    4
#define OPER_GREATEROREQUAL 5

/* functions */
void report_result(const char *test_name,
                   int expected_result,
                   int expected_code,
                   int code_operation,
                   int actual_code);

#endif