#ifndef __PVFS_HELPER_H
#define __PVFS_HELPER_H

#include <unistd.h>
#include <sys/types.h>

/* pvfs specific includes (from test/client/sysint) */
#include "client.h"
#include "pvfs2-util.h"

/* don't change this w/o changing the test_files array */
#define NUM_TEST_FILES                         10
#define TEST_FILE_PREFIX                "/tpvfs"
#define TEST_PVFS_DATA_SIZE             1024*1024

#define debug_printf(format, f...)                              \
  do {                                                          \
     fprintf(stderr, "file %s, line %d\n", __FILE__, __LINE__); \
     fprintf(stderr, format, ##f);                              \
   } while (0)

typedef struct
{
    int initialized;
    int num_test_files;
    PVFS_fs_id fs_id;
} pvfs_helper_t;
extern pvfs_helper_t pvfs_helper;

/*
  these are some helper functions that are implemented in pvfs-helper.c
  they return 0 on success; non-zero otherwise unless specified
*/
int create_dir(PVFS_pinode_reference parent_refn, char *name,
               PVFS_pinode_reference *out_refn);

int remove_file(PVFS_pinode_reference parent_refn, char *name);

int remove_dir(PVFS_pinode_reference parent_refn, char *name);

int lookup_name(PVFS_pinode_reference pinode_refn, char *name,
                PVFS_pinode_reference *out_refn);

int get_root(PVFS_fs_id fs_id, PVFS_pinode_reference *pinode_refn);

int initialize_sysint(void);
int finalize_sysint(void);

#endif /* __PVFS_HELPER_H */
