#include "shmem_util.h"

/** Aquire a SysV shared memory segment. 
 * key_file and proj_id are identifiers used by ftok to uniquly identify the 
 * segment. size is the desired size in bytes. memory is an optional parameter
 * that refers to a void pointer which can be set to the address of the segment.
 */
int shmem_init(char *key_file, int proj_id, size_t size, void **memory)
{
    int key = 0;
    int id = 0;

    /* Generate key based on key_file and proj_id */
    key = ftok(key_file, proj_id);
    if(key < 0)
    {
        return -1;
    }

    /* Allocate Shared Memory Segment */
    id = shmget(key, size, FLAGS | IPC_CREAT | IPC_EXCL);
    if(id < 0)
    {
        return -1;
    }

    /* Reference to pointer not required. */
    if(memory)
    {
        *memory = shmat(id, NULL, AT_FLAGS);
        if(*memory == (void *) -1)
        {
            return -1;
        }
    }
    else
    {
        if(shmat(id, NULL, AT_FLAGS) == (void *) 0)
        {
            return -1;
        }
    }

    return 0;
}

/** Destroy SysV shared memory segment */
int shmem_destroy(char *key_file, int proj_id)
{
    int key = 0;
    int id = 0;
    int rc = 0;

    /* Generate key based on key_file and proj_id */
    key = ftok(key_file, proj_id);
    if(key < 0)
    {
        return -1;
    }

    /* Allocate Shared Memory Segment */
    id = shmget(key, 0, FLAGS);
    if(id < 0)
    {
        return -1;
    }

    rc = shmctl(id, IPC_RMID, NULL);
    if(rc < 0)
    {
        return -1;
    }
    return 0;
}

