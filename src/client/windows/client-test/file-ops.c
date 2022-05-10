/* Copyright (C) 2011 Omnibond, LLC
   Client tests -- file operations */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#ifdef WIN32
#include <direct.h>
#endif

#include "test-support.h"
#include "file-ops.h"


/* return 0 if file found */
int lookup_file_int(char *file_name)
{
    struct _stat buf;

    return _stat(file_name, &buf) == 0 ? 0 : ENOENT;
}

int delete_file(global_options *options, int fatal)
{
    char *file_name;
    int code;

    /* delete existing file */
    file_name = randfile(options->root_dir);

    code = quick_create(file_name);
    if (code != 0)
    {
        free(file_name);
        return -code;
    }

    /* delete file */
    if (_unlink(file_name) == 0)
    {
        code = lookup_file_int(file_name) == ENOENT ? 0 : 1;
    }
    else
    {
        code = errno;
    }
    
    report_result(options,
                  "delete-file",
                  "main",
                  RESULT_SUCCESS,
                  0,
                  OPER_EQUAL,
                  code);

    free(file_name);

    if (code != 0 && fatal)
        return CODE_FATAL;

    return 0;
}

int delete_file_notexist(global_options *options, int fatal)
{
    char *file_name;
    int code;

    /* delete nonexistent file */
    file_name = randfile(options->root_dir);

    _unlink(file_name);
    code = errno;
    
    report_result(options,
                  "delete-file-notexist",
                  "main",
                  RESULT_FAILURE,
                  ENOENT,
                  OPER_EQUAL,
                  code);

    free(file_name);

    if (code != ENOENT && fatal)
        return CODE_FATAL;

    return 0;
}

int delete_dir_empty(global_options *options, int fatal)
{
    char *dir_name;
    int code;

    /* create directory */
    dir_name = randdir(options->root_dir);

    if (_mkdir(dir_name) != 0)
    {
        free(dir_name);
        return errno;
    }

    /* remove directory */
    if (_rmdir(dir_name) == 0)
    {
        code = lookup_file_int(dir_name) == ENOENT ? 0 : 1;
    }
    else
    {
        code = errno;
    }

    report_result(options,
                  "delete-dir-empty",
                  "main",
                  RESULT_SUCCESS,
                  0,
                  OPER_EQUAL,
                  code);

    free(dir_name);

    if (code != 0 && fatal)
        return CODE_FATAL;

    return 0;
}

int delete_dir_notempty(global_options *options, int fatal)
{
    char *dir_name, *file_name;
    int code;

    /* create dir */
    dir_name = randdir(options->root_dir);
    if (_mkdir(dir_name) != 0)
    {
        free(dir_name);
        return errno;
    }

    /* create file */
    file_name = randfile(dir_name);
    if ((code = quick_create(file_name)) != 0)
    {
        _rmdir(dir_name);
        free(file_name);        
        free(dir_name);

        return code;
    }

    /* attempt to delete dir */
    code = _rmdir(dir_name) == 0 ? 0 : errno;

    report_result(options,
                  "delete-dir-notempty",
                  "main",
                  RESULT_FAILURE,
                  ENOTEMPTY,
                  OPER_EQUAL,
                  code);

    /* cleanup file and dir */
    _unlink(file_name);
    _rmdir(dir_name);

    free(file_name);
    free(dir_name);

    if (code != 0 && fatal)
        return CODE_FATAL;

    return 0;
}

int rename_file(global_options *options, int fatal)
{
    char *file_name, *new_name;
    int code;

    /* create file */
    file_name = randfile(options->root_dir);
    if ((code = quick_create(file_name)) != 0)
    {
        free(file_name);
        return code;
    }

    /* rename */
    new_name = randfile(options->root_dir);
    code = rename(file_name, new_name) == 0 ? 0 : errno;

    report_result(options,
                  "rename-file",
                  "main",
                  RESULT_SUCCESS,
                  0,
                  OPER_EQUAL,
                  code);

    /* cleanup file */
    _unlink(new_name);

    free(new_name);
    free(file_name);

    if (code != 0 && fatal)
        return CODE_FATAL;

    return 0;
}

/* On OrangeFS, the target file will be overwritten */
int rename_file_exist(global_options *options, int fatal)
{
    char *file_name, *new_name;
    int code;

    /* create file */
    file_name = randfile(options->root_dir);
    if ((code = quick_create(file_name)) != 0)
    {
        free(file_name);
        return code;
    }

    /* create second file */
    new_name = randfile(options->root_dir);
    if ((code = quick_create(new_name)) != 0)
    {
        free(new_name);
        free(file_name);
        return code;
    }

    /* rename */    
    code = rename(file_name, new_name) == 0 ? 0 : errno;

    report_result(options,
                  "rename-file-exist",
                  "main",
                  RESULT_SUCCESS,
                  0,
                  OPER_EQUAL,
                  code);

    /* cleanup file */
    _unlink(new_name);

    free(new_name);
    free(file_name);

    if (code != EACCES && fatal)
        return CODE_FATAL;

    return 0;
}

int move_file(global_options *options, int fatal)
{
    char *dir_name, *file_name, *new_name;
    int code;

    /* create file */
    file_name = randfile(options->root_dir);
    if ((code = quick_create(file_name)) != 0)
    {
        free(file_name);
        return code;
    }

    /* create dir */
    dir_name = randdir(options->root_dir);
    if (_mkdir(dir_name) != 0)
    {
        free(dir_name);
        free(file_name);
        return errno;
    }

    /* move file */    
    new_name = (char *) malloc(strlen(dir_name) + strlen(file_name) + 4);
    sprintf(new_name, "%s%c%s", dir_name, SLASH_CHAR, strrchr(file_name, SLASH_CHAR)+1);
    code = rename(file_name, new_name) == 0 ? 0 : errno;

    report_result(options,
                  "move-file",
                  "main",
                  RESULT_SUCCESS,
                  0,
                  OPER_EQUAL,
                  code);

    free(new_name);
    free(dir_name);
    free(file_name);

    if (code != 0 && fatal)
        return CODE_FATAL;

    return 0;
}

int move_file_baddir(global_options *options, int fatal)
{
    char *dir_name, *file_name, *new_name;
    int code;

    /* create file */
    file_name = randfile(options->root_dir);
    if ((code = quick_create(file_name)) != 0)
    {
        free(file_name);
        return code;
    }

    /* "fake" dir */
    dir_name = randdir(options->root_dir);

    /* move file */    
    new_name = (char *) malloc(strlen(dir_name) + strlen(file_name) + 4);
    sprintf(new_name, "%s%c%s", dir_name, SLASH_CHAR, strrchr(file_name, SLASH_CHAR)+1);

    errno = 0;
    code = rename(file_name, new_name);
    if (code != 0) {
        code = errno;
    }

    report_result(options,
                  "move-file-baddir",
                  "main",
                  RESULT_FAILURE,
                  ENOENT,
                  OPER_EQUAL,
                  code);

    /* cleanup */
    _rmdir(dir_name);

    free(new_name);
    free(dir_name);
    free(file_name);

    if (code != ENOENT && fatal)
        return CODE_FATAL;

    return 0;
}

int move_file_exist(global_options *options, int fatal)
{
    char *dir_name, *file_name, *new_name;
    int code;

    /* create file */
    file_name = randfile(options->root_dir);
    if ((code = quick_create(file_name)) != 0)
    {
        free(file_name);
        return code;
    }

    /* "fake" dir */
    dir_name = randdir(options->root_dir);
    if (_mkdir(dir_name) != 0)
    {
        free(dir_name);
        free(file_name);
        return errno;
    }

    /* create new file */    
    new_name = (char *) malloc(strlen(dir_name) + strlen(file_name) + 4);
    sprintf(new_name, "%s%c%s", dir_name, SLASH_CHAR, strrchr(file_name, SLASH_CHAR)+1);
    if ((code = quick_create(new_name)) != 0)
    {
        free(new_name);
        free(dir_name);
        free(file_name);
        return 0;
    }

    /* move file over existing file */
    code = rename(file_name, new_name) == 0 ? 0 : errno;

    report_result(options,
                  "move-file-exist",
                  "main",
                  RESULT_SUCCESS,
                  0,
                  OPER_EQUAL,
                  code);

    /* cleanup */
    _rmdir(dir_name);

    free(new_name);
    free(dir_name);
    free(file_name);

    if (code != 0 && fatal)
        return CODE_FATAL;

    return 0;
}

