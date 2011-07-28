/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */
#include "pvfs2-handle-to-str.h"
#include "quicklist.h"
#include "gen-locks.h"
#include "pvfs2-types.h"
#include "gossip.h"

static gen_mutex_t handle_list_mutex = GEN_MUTEX_INITIALIZER;

static str_list_p handle_buf_list = NULL;

/* This function takes a PVFS handle and unparses it. 
   A linked list is used to store the string buffers
   which are passed back. LRU eviction is used to keep
   the string buffers valid for as long as possible. 
   However, there is no guarantee of how long the buffer
   will be valid. If the buffer must be kept alive, it
   would be best to allocate your own memory and call
   PVFS_handle_unparse();  */ 
/* Note: include gossip.h" to see this function */
char *PVFS_handle_to_str(PVFS_handle handle)
{

    char *retStr;
    str_list_p head;
    handle_buf_p popped;
    int ret = 0;

    gen_mutex_lock(&handle_list_mutex);

    /* if linked list is uninitialized by client or server, create a quick list */
    if (handle_buf_list == NULL)
    {
        ret = create_str_list(QUICK_HANDLE_LIST_SIZE);
        if (ret < 0)
        {
            gossip_err("Error: Could not initialize the handle strings list\n");
            return NULL;
        }
    }

    head = qlist_pop(handle_buf_list);
    popped = qlist_entry(head, handle_buf_t, links);
    qlist_add_tail(&(popped->links), handle_buf_list);

    gen_mutex_unlock(&handle_list_mutex);
    PVFS_handle_unparse(handle, popped->buf);
    retStr = popped->buf;
    return retStr;
}

/* create a linked list to store string buffers for handle printing */
int create_str_list(int n_items)
{
    str_list_p list; 

    int i;
    handle_buf_p tmp;
    int ret;

    if (handle_buf_list)
    {
        destroy_str_list();
    }

    list = (str_list_p)malloc(sizeof(str_list_t));
    if (!(list))
    {
        ret = -PVFS_ENOMEM;
        return ret;
    }
    INIT_QLIST_HEAD(list);

    for (i = 0; i < n_items; i++)
    {
        tmp = (handle_buf_p)malloc(sizeof(handle_buf_t));
        if (!tmp)
        {
            ret = -PVFS_ENOMEM;
	    return ret;
        }
        tmp->buf = (char *)malloc(PVFS_HANDLE_STRING_LEN);
        if (!(tmp->buf))
        {
            ret = -PVFS_ENOMEM;
	    return ret;
        }
        qlist_add_tail(&(tmp->links), list);
    }

    handle_buf_list = list;

    return 0;
}

/* free all memory associated with our linked list and string buffers */
void destroy_str_list()
{

    handle_buf_p rover, tmp;

    if (!handle_buf_list)
    {
        return;
    }

    qlist_for_each_entry_safe(rover, tmp, handle_buf_list, links)
    {
        free(rover->buf);
        free(rover);
    }
    free(handle_buf_list);
    handle_buf_list = NULL;

    return;
}
                            

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

