#ifndef SHMEM_UTIL_H
#define SHMEM_UTIL_H

#define _XOPEN_SOURCE 500

#include <stdlib.h>
#include <unistd.h>
#include <sys/shm.h>
#include <stdio.h>
#include <errno.h>
#include <syslog.h>

#define SVSHM_MODE (SHM_R | SHM_W | SHM_R>>3 | SHM_R>>6)
#define FLAGS (SVSHM_MODE)
#define AT_FLAGS 0

int shmem_init(char *key_file, int proj_id, size_t size, void **memory);
int shmem_destroy(char *key_file, int proj_id);

#endif
