/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */
#include "pvfs2-config.h"
#ifdef __KERNEL__
#include "pvfs2-kernel.h"
#else
#include <stdio.h>
#include <time.h>
#endif
#include "pvfs2-util.h"

int32_t PVFS_translate_mode(int mode, int suid)
{
    int ret = 0, i = 0;
#define NUM_MODES 11
    static int modes[NUM_MODES] =
    {
        S_IXOTH, S_IWOTH, S_IROTH,
        S_IXGRP, S_IWGRP, S_IRGRP,
        S_IXUSR, S_IWUSR, S_IRUSR,
        S_ISGID, S_ISUID
    };
    static int pvfs2_modes[NUM_MODES] =
    {
        PVFS_O_EXECUTE, PVFS_O_WRITE, PVFS_O_READ,
        PVFS_G_EXECUTE, PVFS_G_WRITE, PVFS_G_READ,
        PVFS_U_EXECUTE, PVFS_U_WRITE, PVFS_U_READ,
        PVFS_G_SGID,    PVFS_U_SUID
    };

    for(i = 0; i < NUM_MODES; i++)
    {
        if (mode & modes[i])
        {
            ret |= pvfs2_modes[i];
        }
    }
    if (suid == 0 && (ret & PVFS_U_SUID))
    {
         ret &= ~PVFS_U_SUID;
    }
    return ret;
#undef NUM_MODES
}

#ifndef __KERNEL__

PVFS_time PVFS_util_get_current_time(void)
{
    struct timeval t = {0,0};
    PVFS_time current_time = 0;

    gettimeofday(&t, NULL);
    current_time = (PVFS_time)t.tv_sec;
    return current_time;
}

PVFS_time PVFS_util_mktime_version(PVFS_time time)
{
    struct timeval t = {0,0};
    PVFS_time version = (time << 32);

    gettimeofday(&t, NULL);
    version |= (PVFS_time)t.tv_usec;
    return version;
}

PVFS_time PVFS_util_mkversion_time(PVFS_time version)
{
    return (PVFS_time)(version >> 32);
}

#endif

char *get_object_type(int objtype)
{
    static char *obj_types[] =
    {
         "NONE", "METAFILE", "DATAFILE",
         "DIRECTORY", "SYMLINK", "DIRDATA", "UNKNOWN"
    };
    switch(objtype)
    {
    case PVFS_TYPE_NONE:
         return obj_types[0];
    case PVFS_TYPE_METAFILE:
         return obj_types[1];
    case PVFS_TYPE_DATAFILE:
         return obj_types[2];
    case PVFS_TYPE_DIRECTORY:
         return obj_types[3];
    case PVFS_TYPE_SYMLINK:
         return obj_types[4];
    case PVFS_TYPE_DIRDATA:
         return obj_types[5];
    }
    return obj_types[6];
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
