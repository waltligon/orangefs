/*
 * (C) 2006 The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef _DLM_CONFIG_H
#define _DLM_CONFIG_H

#define DLM_NUM_THREADS   5     /* Number of threads in thread pool */
#define DLM_REQ_PORT      5334  /* Default port for the dlm server  */
#define DLM_HANDLE_LENGTH 128   /* Opaque handle for a given file system object */
#define DLM_NUM_VECTORS   10    /* Maximum number of <disjoint offset-size pairs> in dlm_lockv rpc */
#define DLM_MAX_SERVERS   4096  /* Maximum number of I/O servers that we can support */
#define DLM_CLNT_TIMEOUT  25    /* Client-timeout */
#define DLM_MAX_RETRIES   10    /* Maximum number of retries */

#endif
/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 * End:
 *
 * vim: ts=4 sts=4 sw=4 expandtab
 */
