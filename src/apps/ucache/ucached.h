#ifndef UCACHED_H
#define UCACHED_H

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <string.h>
 
#define LOG "/tmp/ucached.log"

/* FIFO Defines */
#define FILE_MODE (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH)
#define FIFO1 "/tmp/ucached.fifo.1"
#define FIFO2 "/tmp/ucached.fifo.2"
#define BUFF_SIZE 4096

/* Message passing to and from daemon */
int myread(int readfd, char *buffer);
void mywrite(int writefd, const char *src, char *buffer);

/* For shared memory for ucache and ucache locks */
#include "shmem_util.h"
#define KEY_FILE "/etc/fstab"
#define PROJ_ID1 61
#define PROJ_ID2 'a'
#define CACHE_SIZE (256 * 512 * 1024)
#define LOCKS_SIZE (513 * 24)

/* Choose which reponse to send to caller */
#define CHECK_RC(rc)                                                        \
    if(rc >= 0)                                                             \
    {                                                                       \
       mywrite(writefd, "SUCCESS", buffer);                                 \
    }                                                                       \
    else                                                                    \
    {                                                                       \
        mywrite(writefd, "FAILURE: check log: /tmp/ucached.log", buffer);   \
    }                                                                       \

#endif
