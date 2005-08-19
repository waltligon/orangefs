/*
 * Copyright © Acxiom Corporation, 2005
 *
 * See COPYING in top-level directory.
 */

/* portions taken from mount.pvfs.c (PVFS 1.6.3) (c) 1997 Clemson University */

/* This program does the following things:
 * 1) rudimentary argument checking (true checking done by kernel module)
 * 2) copy device description to options field 
 * 3) call mount
 * 4) update mtab
 *
 * The critical step (and the reason we don't use the standard mount command
 * for 2.4 Linux kernels) is step 2.  This puts the device description into
 * a field that will be seen by the pvfs2 kernel module.  The normal device
 * field is discarded by the kernel too early.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <mntent.h>
#include <sys/time.h>
#include <sys/mount.h>

#include "pvfs2-types.h"
#include "realpath.h"

#define PVFS2_MTAB "/etc/mtab"
#define PVFS2_TMP_MTAB "/etc/mtab.pvfs2"

static int usage(
    int argc,
    char **argv);
static int do_mtab(
    struct mntent *);
static int parse_args(
    int argc,
    char *argv[],
    char **dev,
    char **mntpnt,
    char **kern_options,
    char **orig_options);

int main(
    int argc,
    char **argv)
{
    int ret, flags = 0;
    char type[] = "pvfs2";
    struct mntent myment;
    struct stat sb, mntstat;
    char *dev, *kern_options, *orig_options, *tmp_mntpnt;
    char mntpnt[PVFS_NAME_MAX+2];

    if (argc < 3)
    {
        fprintf(stderr, "Error: too few arguments\n");
        usage(argc, argv);
        return (-1);
    }

    if ((ret = parse_args(argc, argv, &dev, &tmp_mntpnt, &kern_options,
        &orig_options)))
    {
        fprintf(stderr, "Error: could not parse command line arguments.\n");
        usage(argc, argv);
        return (-1);
    }

    ret = PINT_realpath(tmp_mntpnt, mntpnt, (PVFS_NAME_MAX+1));
    if(ret < 0)
    {
        PVFS_perror("PINT_realpath", ret);
        return(-1);
    }
    free(tmp_mntpnt);

    /* verify that local mountpoint exists and is a directory */
    if (stat(mntpnt, &mntstat) < 0 || !S_ISDIR(mntstat.st_mode))
    {
        fprintf(stderr,
                "Error: invalid mount point %s (should be a local directory)\n",
                mntpnt);
        return (-1);
    }

    if ((ret =
         mount(dev, mntpnt, type, flags,
               (void *)kern_options)) < 0)
    {
        perror("mount");
        return (-1);
    }

    memset(&myment, 0, sizeof(myment));
    myment.mnt_fsname = dev;
    myment.mnt_dir = mntpnt;
    myment.mnt_type = type;
    myment.mnt_opts = orig_options;
    /* if we weren't given any options, then just use "rw" as a place holder */
    if(!myment.mnt_opts)
    {
        myment.mnt_opts = "rw";
    }

    /* Leave mtab alone if it is a link */
    if (lstat(PVFS2_MTAB, &sb) == 0 && S_ISLNK(sb.st_mode))
    {
        return (0);
    }

    ret = do_mtab(&myment);

    if(dev)
        free(dev);
    if(orig_options)
        free(orig_options);
    if(kern_options)
        free(kern_options);

    return (ret);
}

/* parse_args()
 *
 * parses command line arguments and builds appropriate string
 * representations.
 *
 * returns 0 on success, -1 on failure.
 */
static int parse_args(
    int argc,
    char *argv[],
    char **dev,
    char **mntpnt,
    char **kern_options,
    char **orig_options)
{
    int opt;
    int opts_set_flag = 0;

    /* safety */
    *dev = NULL;
    *mntpnt = NULL;
    *kern_options = NULL;
    *orig_options = NULL;

    /* Start parsage */
    while ((opt = getopt(argc, argv, "o:")) != EOF)
    {
        switch (opt)
        {
        case 'o':
            if(opts_set_flag)
            {
                fprintf(stderr, "Error: please use only one -o argument.\n");
                return(-1);
            }
            *orig_options = (char*)malloc(strlen(optarg)+1);
            if(!(*orig_options))
            {
                perror("malloc");
                return(-1);
            }
            opts_set_flag = 1;
            strcpy(*orig_options, optarg);
            break;
        default:
            fprintf(stderr, "Error: argument format is incorrect.\n");
            return -1;
        }
    }

    if (optind != (argc - 2))
    {
        fprintf(stderr, "Error: argument format is incorrect.\n");
        return (-1);
    }

    if(opts_set_flag)
    {
        *kern_options = (char*)malloc(strlen(*orig_options) +
            strlen(argv[optind]) + 2);
        if(!(*kern_options))
        {
            perror("malloc");
            return(-1);
        }
        sprintf(*kern_options, "%s,%s", argv[optind], *orig_options);
    }
    else
    {
        *kern_options = (char*)malloc(strlen(argv[optind]) + 1);
        if(!(*kern_options))
        {
            perror("malloc");
            return(-1);
        }
        strcpy(*kern_options, argv[optind]);
    }

    *mntpnt = (char*)malloc(strlen(argv[optind + 1])+1);
    if(!(*mntpnt))
    {
        perror("malloc");
        return(-1);
    }
    strcpy(*mntpnt, argv[optind + 1]);

    *dev = (char*)malloc(strlen(argv[optind])+1);
    if(!(*dev))
    {
        perror("malloc");
        return(-1);
    }
    strcpy(*dev, argv[optind]);

    return 0;
}

static int usage(
    int argc,
    char **argv)
{
    fprintf(stderr,
            "Usage: mount.pvfs2 [-o <options>] <proto>://<svr>:<port>/<fs name> directory\n"
            "   Ex: mount.pvfs2 tcp://localhost:3334/pvfs2-fs /mnt/pvfs2.\n"
            "   Note: multiple options may be seperated with commas.\n");
    return 0;
}

/* do_mtab()
 *
 * Given a pointer to a filled struct mntent,
 * add an entry to /etc/mtab.
 *
 * returns 0 on success, -1 on failure
 */
static int do_mtab(
    struct mntent *myment)
{
    struct mntent *ment;
    FILE *mtab;
    FILE *tmp_mtab;

    mtab = setmntent(PVFS2_MTAB, "r");
    tmp_mtab = setmntent(PVFS2_TMP_MTAB, "w");

    if (mtab == NULL)
    {
        fprintf(stderr, "Error: couldn't open " PVFS2_MTAB " for read\n");
        endmntent(mtab);
        endmntent(tmp_mtab);
        return -1;
    }
    else if (tmp_mtab == NULL)
    {
        fprintf(stderr, "Error: couldn't open " PVFS2_TMP_MTAB " for write\n");
        endmntent(mtab);
        endmntent(tmp_mtab);
        return -1;
    }

    while ((ment = getmntent(mtab)) != NULL)
    {
        if (strcmp(myment->mnt_dir, ment->mnt_dir) != 0)
        {
            if (addmntent(tmp_mtab, ment) == 1)
            {
                fprintf(stderr, "Error: couldn't add entry to" PVFS2_TMP_MTAB "\n");
                endmntent(mtab);
                endmntent(tmp_mtab);
                return -1;
            }
        }
    }

    endmntent(mtab);

    if (addmntent(tmp_mtab, myment) == 1)
    {
        fprintf(stderr, "Error: couldn't add entry to " PVFS2_TMP_MTAB "\n");
        return -1;
    }

    endmntent(tmp_mtab);

    if (rename(PVFS2_TMP_MTAB, PVFS2_MTAB))
    {
        fprintf(stderr, "Error: couldn't rename " PVFS2_TMP_MTAB " to " PVFS2_MTAB "\n");
        return -1;
    }

    if (chmod(PVFS2_MTAB, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH) < 0)
    {
        if (errno != EROFS)
        {
            int errsv = errno;
            fprintf(stderr, "mount: error changing mode of %s: %s",
                    PVFS2_MTAB, strerror(errsv));
        }
        return (-1);
    }
    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
