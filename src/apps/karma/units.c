/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "karma.h"

/* tables for calculating units */
static float time_table[7] = { 31557600.0, 2629800.0, 604800.0, 86400.0,
    3600.0, 60.0, 1.0
};
static char *time_abbrev[7] = { "years", "months", "weeks", "days", "hours",
    "min", "sec"
};

static float size_table[5] = { 1048576.0 * 1048576.0, 1024.0 * 1048576.0,
    1048576.0, 1024.0, 1.0
};
static char *size_abbrev[5] = { "TB", "GB", "MB", "KB", "bytes" };

static float count_table[5] = { 1000000000000.0, 1000000000.0, 1000000.0,
    1000.0, 1.0
};
static char *count_abbrev[5] = { "trillion", "billion", "million", "thousand",
    ""
};

static float ops_table[5] = { 1048576.0 * 1048576.0, 1024.0 * 1048576.0,
    1048576.0, 1024.0, 1.0
};
static char *ops_abbrev[5] = { "Tops", "Gops", "Mops", "Kops", "ops" };


char *gui_units_time(
    uint64_t time_sec,
    float *divisor)
{
    int i;

    for (i = 0; i < (sizeof(time_table) / sizeof(*time_table)) - 1; i++)
    {
        if (((float) time_sec) / time_table[i] > 1.0)
            break;
    }

    *divisor = time_table[i];
    return time_abbrev[i];
}

char *gui_units_size(
    PVFS_size size_bytes,
    float *divisor)
{
    int i;

    for (i = 0; i < (sizeof(size_table) / sizeof(*size_table)) - 1; i++)
    {
        if (((float) size_bytes) / size_table[i] > 1.0)
            break;
    }

    *divisor = size_table[i];
    return size_abbrev[i];
}

char *gui_units_count(
    uint64_t count,
    float *divisor)
{
    int i;

    for (i = 0; i < (sizeof(count_table) / sizeof(*count_table)) - 1; i++)
    {
        if (((float) count) / count_table[i] > 1.0)
            break;
    }

    *divisor = count_table[i];
    return count_abbrev[i];
}

char *gui_units_ops(
    PVFS_size ops,
    float *divisor)
{
    int i;

    for (i = 0; i < (sizeof(ops_table) / sizeof(*ops_table)) - 1; i++)
    {
        if (((float) ops) / ops_table[i] > 1.0)
            break;
    }

    *divisor = ops_table[i];
    return ops_abbrev[i];
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
