#define _GNU_SOURCE

#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "flag_conv_common.h"

/* TODO: verify all functions specified in JNI layer have the appropriate 
 * flags declared (see posix-pvfs.c in src/client/usrint) 
 */
uint64_t posix_flag_to_num(char *str)
{
    //printf("posix_flag_to_num: %s\n", str);
    if(CMP("SEEK_SET"))
    {
        return SEEK_SET;
    }
    else if(CMP("SEEK_CUR"))
    {
        return SEEK_CUR;
    }
    else if(CMP("SEEK_END"))
    {
        return SEEK_END;
    }
    else if(CMP("O_ACCMODE"))
    {
        return O_ACCMODE;
    }
    else if(CMP("O_RDONLY"))
    {
        return O_RDONLY;
    }
    else if(CMP("O_WRONLY"))
    {
        return O_WRONLY;
    }
    else if(CMP("O_RDWR"))
    {
        return O_RDWR;
    }
    else if(CMP("O_CREAT"))
    {
        return O_CREAT;
    }
    else if(CMP("O_EXCL"))
    {
        return O_EXCL;
    }
    else if(CMP("O_NOCTTY"))
    {
        return O_NOCTTY;
    }
    else if(CMP("O_TRUNC"))
    {
        return O_TRUNC;
    }
    else if(CMP("O_APPEND"))
    {
        return O_APPEND;
    }
    else if(CMP("O_NONBLOCK"))
    {
        return O_NONBLOCK;
    }
    else if(CMP("O_NDELAY"))
    {
        return O_NDELAY;
    }
    else if(CMP("O_SYNC"))
    {
        return O_SYNC;
    }
    else if(CMP("O_FSYNC"))
    {
        return O_FSYNC;
    }
    else if(CMP("O_ASYNC"))
    {
        return O_ASYNC;
    }
    else if(CMP("O_DIRECT"))
    {
        return O_DIRECT;
    }
    else if(CMP("O_DIRECTORY"))
    {
        return O_DIRECTORY;
    }
    else if(CMP("O_NOFOLLOW"))
    {
        return O_NOFOLLOW;
    }
    else if(CMP("O_NOATIME"))
    {
        return O_NOATIME;
    }
    else if(CMP("O_CLOEXEC"))
    {
        return O_CLOEXEC;
    }
    else if(CMP("O_LARGEFILE"))
    {
        return O_LARGEFILE;
    }
    else if(CMP("S_IRWXU"))
    {
        return S_IRWXU;
    }
    else if(CMP("S_IRUSR"))
    {
        return S_IRUSR;
    }
    else if(CMP("S_IWUSR"))
    {
        return S_IWUSR;
    }
    else if(CMP("S_IXUSR"))
    {
        return S_IXUSR;
    }
    else if(CMP("S_IRWXG"))
    {
        return S_IRWXG;
    }
    else if(CMP("S_IRGRP"))
    {
        return S_IRGRP;
    }
    else if(CMP("S_IWGRP"))
    {
        return S_IWGRP;
    }
    else if(CMP("S_IXGRP"))
    {
        return S_IXGRP;
    }
    else if(CMP("S_IRWXO"))
    {
        return S_IRWXO;
    }
    else if(CMP("S_IROTH"))
    {
        return S_IROTH;
    }
    else if(CMP("S_IWOTH"))
    {
        return S_IWOTH;
    }
    else if(CMP("S_IXOTH"))
    {
        return S_IXOTH;
    }
    else
    {
        printf("ALERT: unrecognized posix flag: %s\n", str);
        uint64_t octal = 0;
        int rc = sscanf(str, "%llo", (long long unsigned int *) &octal);
        return (rc == 1 ? octal : 0);
    }
    return 0;
}

uint64_t posix_or_flags(int n, char **flags)
{
    uint64_t ret = 0;
    int i;
    for(i = 0; i < n; i++)
    {
        //printf("flags[%d] = %s\n", i, flags[i]);
        ret |= posix_flag_to_num(flags[i]);
    }
    return ret;
}

uint64_t posix_fsti(char *str)
{
    //printf("posix_fsti: %s\n", str);
    /* see flag_conv_common.h */
    SETUP_FLAG_STRING_TO_INT
    uint64_t ret = posix_or_flags(n, flag_ptrs);
    P(ret);    
    return ret;
}

