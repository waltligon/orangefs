/* Copyright (C) 2011 Omnibond, LLC
   Client testing - support routines */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "test-support.h"
#include "timer.h"

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
    if (strlen(root) > 0 && root[strlen(root)-1] != SLASH_CHAR)
        slashdir = quickcat(root, SLASH_STR);
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
    if (strlen(root) > 0 && root[strlen(root)-1] != SLASH_CHAR)
        slashdir = quickcat(root, SLASH_STR);
    else
        slashdir = _strdup(root);

    /* generate random dir name */
    sprintf(name, "f%05d.tst", rand());

    file = quickcat(slashdir, name);

    free(slashdir);

    return file;
}

/* create a 0-byte file */
int quick_create(char *file_name)
{
    FILE *f;

    f = fopen(file_name, "w");
    if (f)
    {
        fclose(f);
        return 0;
    }

    return errno;
}

void report_error(global_options *options,
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

void report_result(global_options *options,
                   const char *test_name,
                   const char *sub_test,
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
    line_size += 8;   /* expected */
    line_size += 2;   /* operator */
    line_size += 11;  /* expected_code */
    line_size += 3;   /* got */
    line_size += 6;   /* NOT_OK */
    line_size += 9;   /* spaces & null */

    line = (char *) malloc(line_size);

    /* print to buffer */
    ret = _snprintf(line, line_size, "%s %s %s expected %s %d got %d %s",
                    test_name, 
                    sub_test,
                    (expected_result == RESULT_SUCCESS) ? "EXPECT_SUCCESS" : "EXPECT_FAILURE",
                    ops[code_operation], expected_code, actual_code,
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

void report_perf(global_options *options,
                 const char *test_name,
                 const char *sub_test,
                 double value,
                 char *format)
{
    char valstr[32];

    /* get the string for the value in the specified format */
    sprintf(valstr, format, value);

    /* report to console and/or file */
    if (options->report_flags & REPORT_CONSOLE)
        printf("%s %s PERF %s\n", test_name, sub_test, valstr);

    if (options->report_flags & REPORT_FILE)
    {
        fprintf(options->freport, "%s %s PERF %s\n", test_name, sub_test, valstr);
        fflush(options->freport);
    }

}
                   