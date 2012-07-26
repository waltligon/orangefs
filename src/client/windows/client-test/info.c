/* Copyright (C) 2011 Omnibond, LLC
   Client tests -- file info functions */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#ifdef WIN32
#include <direct.h>
#else
#include <sys/vfs.h>
#endif
#include <ctype.h>
#include <errno.h>

#include "test-support.h"
#include "info.h"

int file_time(global_options *options, int fatal)
{
    time_t t, diff1, diff2, diff3;
    char *file_name;
    int code;
    struct _stat buf;

    t = time(NULL);

    /* create file */
    file_name = randfile(options->root_dir);
    if ((code = quick_create(file_name)) != 0)
    {
        free(file_name);
        return code;
    }

    /* get times */
    if (_stat(file_name, &buf) != 0)
    {
        free(file_name);
        return errno;
    }

    diff1 = buf.st_atime - t;
    if (diff1 < 0) diff1 *= -1;
    diff2 = buf.st_ctime - t;
    if (diff2 < 0) diff2 *= -1;
    diff3 = buf.st_mtime - t;
    if (diff3 < 0) diff3 *= -1;

    code = !((diff1 < 15) && (diff2 < 15) && (diff3 < 15));
    
    report_result(options, 
                  "file-time",
                  "main",
                  RESULT_SUCCESS,
                  0,
                  OPER_EQUAL,
                  0);

    _unlink(file_name);

    free(file_name);

    if (code != 0 && fatal)
        return CODE_FATAL;

    return 0;
}

#ifdef WIN32
int volume_space(global_options *options, int fatal)
{
    struct _diskfree_t driveinfo;
    __int64 total_clusters, avail_clusters, 
            sectors_per_cluster, bytes_per_sector,
            bytes_free, bytes_total;
    int code;
    double gb_free, gb_total;

    code = _getdiskfree(toupper(options->root_dir[0]) - 'A' + 1, &driveinfo); 

    report_result(options, 
                  "volume-space",
                  "main",
                  RESULT_SUCCESS,
                  0,
                  OPER_EQUAL,
                  code);

    if (code == 0)
    {
        total_clusters = driveinfo.total_clusters;
        avail_clusters = driveinfo.avail_clusters;
        sectors_per_cluster = driveinfo.sectors_per_cluster;
        bytes_per_sector = driveinfo.bytes_per_sector;

        bytes_free = avail_clusters * sectors_per_cluster * bytes_per_sector;
        gb_free = (double) bytes_free / (double) (1024*1024*1024);

        report_perf(options,
                    "volume-space",
                    "free-space",
                    gb_free,
                    "%4.3f GB");

        bytes_total = total_clusters * sectors_per_cluster * bytes_per_sector;
        gb_total = (double) bytes_total / (double) (1024*1024*1024);

        report_perf(options,
                    "volume-space",
                    "avail-space",
                    gb_total,
                    "%4.3f GB");
    }

    if (code != 0 && fatal)
        return CODE_FATAL;

    return 0;
}
#else
int volume_space(global_options *options, int fatal)
{
    struct statfs buf;
    int code;
    double gb_free, gb_total;

    code = statfs(options->root_dir, &buf);
    if (code != 0)
    {
        return errno;
    }

    gb_free = (double) (buf.f_bsize * buf.f_bfree) / (double) 1024*1024*1024;
    gb_total = (double) (buf.f_bsize * buf.f_blocks) / (double) 1024*1024*1024;

    report_perf(options,
                "volume-space",
                "free-space",
                gb_free,
                "%4.3f GB");

    report_perf(options,
                "volume-space",
                "avail-space",
                gb_total,
                "%4.3f GB");

    return 0;
}
#endif