#ifndef __PVFS_HELPER_H
#define __PVFS_HELPER_H

/* pvfs specific includes (from test/client/sysint) */
#include <client.h>
#include <helper.h>

/* don't change this w/o changing the test_files array */
#define NUM_TEST_FILES                         10
#define TEST_FILE_PREFIX                "/tpvfs"
#define MAX_TEST_PATH_LEN              0x00000040
#define TEST_PVFS_DATA_SIZE             1024*1024

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
    int num_test_files;
    pvfs_mntlist mnt;
    PVFS_sysresp_init resp_init;
} pvfs_helper_t;

/* these are some helper functions that are implemented in pvfs-helper.c */

PVFS_handle create_dir(PVFS_handle parent,
                       PVFS_fs_id fs_id,
                       char *name);

PVFS_handle get_root(PVFS_fs_id fs_id);

int initialize_sysint(void);

#endif /* __PVFS_HELPER_H */
