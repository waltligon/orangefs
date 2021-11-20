/* Copyright (C) 2011 Omnibond, LLC
   Windows client - test program */

#ifdef WIN32
#include <Windows.h>
#else
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include "test-support.h"
#include "test-list.h"

/* globals */

#define MALLOC_CHECK(ptr)    if (ptr == NULL) \
                                 return NULL;

typedef struct _list_node
{
    void *data;
    struct _list_node *next;
} list_node;

list_node *add_list_node(list_node *head, void *pdata, size_t len)
{
    list_node *last_node, *new_node;

    /* create new head */
    if (head->data == NULL)
    {
        head->data = malloc(len);
        MALLOC_CHECK(head->data);
        memcpy(head->data, pdata, len);

        new_node = head;
    }
    else
    {
        last_node = head;
        while (last_node->next)
            last_node = last_node->next;

        new_node = (list_node *) calloc(1, sizeof(list_node));
        MALLOC_CHECK(new_node);
        new_node->data = malloc(len);
        MALLOC_CHECK(new_node->data);
        memcpy(new_node->data, pdata, len);

        /* link in new node */
        last_node->next = new_node;
    }
    
    return new_node;
}

void free_list(list_node *head)
{
    list_node *pnode, *next_node;

    /* free list memory */
    pnode = head;
    do
    {
        next_node = pnode->next;
        free(pnode->data);
        free(pnode);
        pnode = next_node;
    } while (pnode);

}

test_operation *find_operation(char *name)
{
    int i;

    /* search op_table for operations */
    for (i = 0; op_table[i].name; i++)
    {
        if (!strcmp(name, op_table[i].name))
            return &op_table[i];
    }

    return NULL;
}

int setoption(int argc, char **argv, global_options *options, int *index)
{
    int ret = 0;

    /* set option */
    if (!_stricmp(argv[*index], "-tabfile"))
    {
        (*index)++;
        if (*index >= argc || argv[*index][0] == '-')
        {
            fprintf(stderr, "illegal option -tabfile: missing filename\n");
            ret = -1;
        }
        else
            options->tab_file = argv[*index];
    }
    else if (!_stricmp(argv[*index], "-console"))
    {
        options->report_flags |= REPORT_CONSOLE;
    }
    else if (!_stricmp(argv[*index], "-file"))
    {
        (*index)++;
        if (*index >= argc || argv[*index][0] == '-')
        {
            fprintf(stderr, "illegal option -file: missing filename\n");
            ret = -1;
        }
        else
        {
            options->report_flags |= REPORT_FILE;
            options->freport = fopen(argv[*index], "w");
            if (options->freport == NULL)
            {
                fprintf(stderr, "error: could not open report file\n");
                ret = -1;
            }
        }
    }
    
    return ret;
}

int init(int argc, char **argv, 
          global_options *options, list_node *test_list)
{    
    int i, ret = 0;
#ifdef WIN32
    DWORD attrs;    
#else
    struct stat buf;
#endif

    srand((unsigned int) time(NULL));

    /* Check the specified root directory */
#ifdef WIN32
    attrs = GetFileAttributes(options->root_dir);

    /* root directory must be found */
    if ((attrs == INVALID_FILE_ATTRIBUTES) ||
        !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        fprintf(stderr, "GetLastError: %d\n", GetLastError());
        return FALSE;
    }

#else
    ret = stat(options->root_dir, &buf);
    if (ret != 0 || !(S_ISDIR(buf.st_mode)))
        return FALSE;
#endif

    /* option default */
    options->report_flags = REPORT_CONSOLE;

    /* get options */
    i = 2;
    while (i < argc)
    {
        if (argv[i][0] == '-')
            ret = setoption(argc, argv, options, &i);            
        else 
            add_list_node(test_list, argv[i], strlen(argv[i])+1);

        if (ret != 0)
            return FALSE;

        i++;
    }

    return TRUE;
}

int run_tests(global_options *options, list_node *test_list)
{
    int i, ret = 0;
    int all_tests = TRUE;
    list_node *node;
    test_operation *op;

    all_tests = test_list->data == NULL;

    /* call the test options */
    if (all_tests)
    {
        for (i = 0; op_table[i].name; i++)
        {
            /* run the test function */
            ret = op_table[i].function(options, op_table[i].fatal);
            /* this means the test had a technical failure, rather than
               an expected failure (for some tests) */
            if (ret == CODE_FATAL)
            {
                fprintf(stderr, "Test %s: fatal exit\n", op_table[i].name);
                break;
            }
            else if (ret != 0) {
                char errbuf[512];

                strerror_s(errbuf, sizeof(errbuf), ret);
                fprintf(stderr, "Test %s exited with technical error: %s (%d)\n",
                    op_table[i].name, errbuf, ret);
                break;
            }
        }
    }
    else 
    {
        /* run in order specified */
        node = test_list;
        while (node)
        {
            op = find_operation((char *) node->data);
            if (op)
            {
                /* run the test function */
                ret = op->function(options, op->fatal);
                if (ret == CODE_FATAL)
                {
                    fprintf(stderr, "Test %s: fatal exit\n", op->name);
                    break;
                }
                else if (ret != 0)
                {
                    char errbuf[512];

                    strerror_s(errbuf, sizeof(errbuf), ret);
                    fprintf(stderr, "Test %s exited with technical error: %s (%d)\n",
                        op->name, errbuf, ret);
                    break;
                }
            }
            else
            {
                fprintf(stderr, "Invalid test: %s\n", (char *) node->data);
            }
            node = node->next;
        }
    }

    free_list(test_list);

    return ret;
}

void finalize()
{

}

int main(int argc, char **argv)
{
    int ret = 0;
    global_options *options;
    list_node *test_list;
    test_operation *cur_test;

    if (argc < 2)
    {
        printf("USAGE: client-test root-dir [options]\n");
        printf("    root-dir: path for test files, e.g. Z:\\client-test or /pvfs2\n");
        /* TODO: list available tests */
        printf("Tests:\n");
        
        cur_test = &op_table[0];
        while (cur_test->name)
        {
            printf("    %s\n", cur_test->name);
            cur_test++;
        }

        return -1;
    }

    /* init options */
    options = (global_options *) calloc(1, sizeof(global_options));

    /* append trailing slash to root_dir if necessary */
    options->root_dir = (char *) malloc(strlen(argv[1]) + 2);
    strcpy(options->root_dir, argv[1]);
    if (argv[1][strlen(argv[1])-1] != SLASH_CHAR)
        strcat(options->root_dir, SLASH_STR);

    test_list = (list_node *) calloc(1, sizeof(list_node));

    /* initialize and run tests */
    if (init(argc, argv, options, test_list))
    {
        ret = run_tests(options, test_list);
        finalize();
    }
    else
    {
#ifdef WIN32
        ret = GetLastError();
        if (ret == ERROR_FILE_NOT_FOUND)
#else
        ret = errno;
        if (ret == ENOENT)
#endif
            fprintf(stderr, "init failed: %s not found\n", argv[1]);
        else
            fprintf(stderr, "init failed with error code %u\n", ret);

        free(options->root_dir);

        return ret;
    }

    if (ret != 0 && ret != CODE_FATAL)
        fprintf(stderr, "Testing failed with error code %d\n", ret);

    free(options->root_dir);

/* {
        char dummy[16];
        fgets(dummy, 15, stdin);
    }
*/
    return ret;
}