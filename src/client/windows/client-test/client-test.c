/* Copyright (C) 2011 Omnibond, LLC
   Windows client - test program */

#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "test-support.h"

/* globals */
char *root_dir;

#define MALLOC_CHECK(ptr)    if (ptr == NULL) \
                                 return NULL;

/* test prototypes */
/* extern int test_test(op_options *options, int fatal); */
extern int create_dir(op_options *options, int fatal);
extern int create_subdir(op_options *options, int fatal);

typedef struct _list_node
{
    void *data;
    struct _list_node *next;
} list_node;

typedef struct 
{
    const char *name;
    int (*function) (op_options *, int);
    int fatal;
} test_operation;

test_operation op_table[] =
{
    /*{"test-test", test_test, FALSE}, */
    {"create-dir", create_dir, TRUE},
    {"create-subdir", create_subdir, TRUE},
    {NULL, NULL, 0}
};

list_node *add_list_node(list_node *head, void *pdata, size_t len)
{
    list_node *last_node, *new_node;

    /* find the end of the list */
    last_node = head;
    while (last_node->next)
        last_node = last_node->next;

    /* create new head */
    if (last_node == head)
    {
        head->data = malloc(len);
        MALLOC_CHECK(head->data);
        memcpy(head->data, pdata, len);

        new_node = head;
    }
    else
    {
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

BOOL init()
{
    DWORD attrs;

    srand(time(NULL));

    /* Check the specified root directory */
    attrs = GetFileAttributes(root_dir);

    return (attrs != INVALID_FILE_ATTRIBUTES) && 
           (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

int run_tests(int argc, char **argv)
{
    int i, ret = 0;
    BOOL all_tests = TRUE;
    op_options test_options;
    list_node *test_list, *node;
    test_operation *op;

    test_list = (list_node *) calloc(1, sizeof(list_node));

    /* read arguments for tests (all by default) */
    for (i = 2; i < argc; i++)
    {
        if (argv[i][0] == '-')
            /* TODO: OPTION */
            ;
        else 
        {
            add_list_node(test_list, argv[i], strlen(argv[i])+1);
            all_tests = FALSE;
        }
    }

    /* set options */
    test_options.root_dir = root_dir;
    test_options.report_flags = REPORT_CONSOLE;

    /* call the test options */
    if (all_tests)
    {
        for (i = 0; op_table[i].name; i++)
        {
            /* run the test function */
            ret = op_table[i].function(&test_options, op_table[i].fatal);
            /* this means the test had a technical failure, rather than
               an expected failure (for some tests) */
            if (ret == CODE_FATAL)
            {
                fprintf(stderr, "Test %s: fatal exit\n", op_table[i].name);
                break;
            }
            else if (ret != 0) {
                fprintf(stderr, "Test %s exited with technical error %d\n",
                    op_table[i].name, ret);
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
                ret = op->function(&test_options, op->fatal);
                if (ret == CODE_FATAL)
                {
                    fprintf(stderr, "Test %s: fatal exit\n", op->name);
                    break;
                }
                else if (ret != 0)
                {
                    fprintf(stderr, "Test %s exited with technical error %d\n",
                        op->name, ret);
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

    if (argc < 2)
    {
        printf("USAGE: client-test root-dir [options]\n");
        printf("    root-dir: path for test files, e.g. Z:\\client-test\n");
        /* TODO: list available tests */

        return -1;
    }

    root_dir = argv[1];

    if (init())
    {
        ret = run_tests(argc, argv);
        finalize();
    }
    else
    {
        fprintf(stderr, "init failed with error code %u\n", GetLastError());
        return -1;
    }

    if (ret != 0)
        fprintf(stderr, "Testing failed with error code %d\n", ret);

    return ret;
}