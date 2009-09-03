/*
 * (C) 2004 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __PVFS2_FSCK_H
#define __PVFS2_FSCK_H

/*
 * Defines
 */
#define NUM_REPAIRS_PER_REQUEST  100

/*
 * fsck options
 *   start: start the fsck process
 *  cancel: stop the fsck process
 *  report: report status of each servers progress
 *  repair: start the repair process
 */
enum {
    OPT_START = 1,
    OPT_CANCEL,
    OPT_REPORT,
    OPT_REPAIR,
    OPT_FSID
};

/*
 * Data Structures
 */
struct program_options
{
    PVFS_fs_id fsid;   /* collection id */
    int        cmd;    /* fsck command  */
};

/*
 * Prototypes
 */
static int parse_options (int argc, char **argv);
static void usage (char *prog_name);
static const char * state2str (int state);
static const char * op2str (int op);

static const char * phase2str (int phase);

#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
