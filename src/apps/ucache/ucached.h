#ifndef UCACHED_H
#define UCACHED_H

#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/stat.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <poll.h>
#include "ucache.h"
 
/* Daemon Log */
#define LOG "/tmp/ucached.log"

/* FIFO Defines */
#define FILE_MODE (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH)
#define FIFO1 "/tmp/ucached.fifo.1"
#define FIFO2 "/tmp/ucached.fifo.2"
#define BUFF_SIZE 4096
#define LOG_LEN 256

#ifndef LOG_TIMESTAMP
#define LOG_TIMESTAMP 0
#endif

#ifndef CREATE_AT_START
#define CREATE_AT_START 1
#endif 

#ifndef DEST_AT_EXIT 
#define DEST_AT_EXIT 1
#endif

#ifndef FIFO_TIMEOUT 
#define FIFO_TIMEOUT 10 /* Second */
#endif

/* For shared memory for ucache and ucache locks */
#define KEY_FILE "/etc/fstab"
#define SHM_ID1 'l' /* for ucache locks */ 
#define SHM_ID2 'm' /* for ucache memory */

#ifndef SHM_R
#define SHM_R 0400
#endif

#ifndef SHM_W
#define SHM_W 0200
#endif

/* SVSHM Permissions */
#ifndef SVSHM_MODE
#define SVSHM_MODE (SHM_R | SHM_W | SHM_R >> 3 | SHM_W >> 3 | SHM_R >> 6 | SHM_W >> 6)
#endif

#define LOCKS_SIZE ((BLOCKS_IN_CACHE + 1) * 24)

#ifndef BLOCK_LOCK_TIMEOUT
#define BLOCK_LOCK_TIMEOUT 100
#endif

#endif
