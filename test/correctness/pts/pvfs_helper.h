#ifndef __PVFS_HELPER_H
#define __PVFS_HELPER_H

/* pvfs specific includes */
#include "client.h"

/* don't change this w/o changing the test_files array */
#define NUM_TEST_FILES               5

#define TEST_PVFS_DATA_SIZE  1024*1024

#ifdef __GNUC__
#define debug_printf(format, f...)                              \
  do {                                                          \
     fprintf(stderr, "file %s, line %d\n", __FILE__, __LINE__); \
     fprintf(stderr, format, ##f);                              \
   } while (0);

#endif /* __GNUC__ */

typedef struct
{
    int initialized;
    pvfs_mntlist mnt;
    PVFS_sysresp_init resp_init;
} pvfs_helper_t;

extern int initialize_sysint();

#endif /* __PVFS_HELPER_H */
