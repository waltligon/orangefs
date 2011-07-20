/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */
#ifndef __PVFS_HANDLE_TO_STR_H
#define __PVFS_HANDLE_TO_STR_H

#define SERVER_HANDLE_LIST_SIZE 128
#define CLIENT_HANDLE_LIST_SIZE 64
#define QUICK_HANDLE_LIST_SIZE 10

#include "quicklist.h"
#include "pvfs2-types.h"

typedef struct qlist_head str_list_t, *str_list_p;

typedef struct handle_buf_node
{
    str_list_t links;
    char *buf;
} handle_buf_t, *handle_buf_p;

char *PVFS_handle_to_str(PVFS_handle handle);
int create_str_list(int n_items);
void destroy_str_list(void);

#endif /* __PVFS_HANDLE_TO_STR_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

