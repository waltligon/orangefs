/* Copyright (C) 2011 Omnibond, LLC
   Client testing - support routines */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "test-support.h"

const char *ops[] = {"=", "<>", "<", ">", "=<", ">="};

/* allocate and concatenate a string */
char *quickcat(const char *str1, const char *str2)
{
    char *str;

    str = (char *) malloc(strlen(str1) + strlen(str2) + 1);
    
    sprintf(str, "%s%s", str1, str2);

    return str;
}

/* generate a random dir name... caller must free */
char *randdir(const char *root)
{
    char *slashdir, name[16], *dir;

    if (!root)
        return NULL;

    /* append trailing slash */
    if (strlen(root) > 0 && root[strlen(root)-1] != '\\')
        slashdir = quickcat(root, "\\");
    else
        slashdir = _strdup(root);

    /* generate random dir name */
    sprintf(name, "dir%05d", rand());

    dir = quickcat(slashdir, name);

    free(slashdir);

    return dir;
}

/* generate a random file name... caller must free */
char *randfile(const char *root)
{
    char *slashdir, name[16], *file;

    if (!root)
        return NULL;

    /* append trailing slash */
    if (strlen(root) > 0 && root[strlen(root)-1] != '\\')
        slashdir = quickcat(root, "\\");
    else
        slashdir = _strdup(root);

    /* generate random dir name */
    sprintf(name, "f%05d.tst", rand());

    file = quickcat(slashdir, name);

    free(slashdir);

    return file;
}

void report_error(op_options *options,
                  const char *msg)
{
    /* report to console and/or file */
    if (options->report_flags & REPORT_CONSOLE)
        printf("%s\n", msg);

    if (options->report_flags & REPORT_FILE)
    {
        fprintf(options->freport, "%s\n", msg);
        fflush(options->freport);
    }

}

void report_result(op_options *options,
                   const char *test_name,
                   int expected_result,
                   int expected_code,
                   int code_operation,
                   int actual_code)
{
    int comp, ret;
    char *ok_str, *line;
    size_t line_size = 0;

    /* determine result of test */
    switch (code_operation)
    {
    case OPER_EQUAL:
        comp = actual_code == expected_code;
        break;
    case OPER_NOTEQUAL:
        comp = actual_code != expected_code;
        break;
    case OPER_LESS:
        comp = actual_code < expected_code;
        break;
    case OPER_GREATER:
        comp = actual_code > expected_code;
        break;
    case OPER_LESSOREQUAL:
        comp = actual_code <= expected_code;
        break;
    case OPER_GREATEROREQUAL:
        comp = actual_code >= expected_code;
    }

    ok_str = comp ? "OK" : "NOT_OK";

    /* determine size of line buffer */
    line_size += strlen(test_name);
    line_size += 14;  /* EXPECT_SUCCESS */
    line_size += 11;  /* max decimal size -- actual_code */
    line_size += 2;  /* operator */
    line_size += 11;  /* expected_code */
    line_size += 6;   /* NOT_OK */
    line_size += 6;  /* spaces & null */

    line = (char *) malloc(line_size);

    /* print to buffer */
    ret = _snprintf(line, line_size, "%s %s %d %2s %d %s",
                    test_name, 
                    (expected_result == RESULT_SUCCESS) ? "EXPECT_SUCCESS" : "EXPECT_FAILURE",
                    actual_code, ops[code_operation], expected_code,
                    ok_str);
    if (ret < 0)
        sprintf(line, "LINE BUFFER OVERFLOW");

    /* report to console and/or file */
    if (options->report_flags & REPORT_CONSOLE)
        printf("%s\n", line);

    if (options->report_flags & REPORT_FILE)
    {
        fprintf(options->freport, "%s\n", line);
        fflush(options->freport);
    }

    free(line);
}

