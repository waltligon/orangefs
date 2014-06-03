#include <sys/statvfs.h>
#include <sys/utsname.h>

#include <mntent.h>
#include <stdio.h>
#include <stdlib.h>

char *bgproc_fs;
char *bgproc_outdir;

int bgproc_setup(void)
{
    bgproc_fs = getenv("bgproc_fs");
    if (!bgproc_fs)
    {
        fprintf(stderr, "missing environment variable bgproc_fs\n");
        exit(EXIT_FAILURE);
    }
    bgproc_outdir = getenv("bgproc_outdir");
    if (!bgproc_outdir)
    {
        fprintf(stderr, "missing environment variable bgproc_outdir\n");
        exit(EXIT_FAILURE);
    }
    return 0;
}

int main(void)
{
    struct utsname buf;
    struct mntent *me;
    char path[512];
    FILE *f, *outf;

    if (bgproc_setup() != 0)
    {
        fprintf(stderr, "could not setup bgproc\n");
        return EXIT_FAILURE;
    }

    if (uname(&buf) == -1)
    {
        perror("could not get uname");
        return EXIT_FAILURE;
    }

    if (snprintf(path, sizeof path, "%s/%s", bgproc_outdir, buf.nodename) >
            sizeof path)
    {
        fprintf(stderr, "out of memory\n");
        return EXIT_FAILURE;
    }

    outf = fopen(path, "w");
    if (outf == NULL)
    {
        perror("could not open output file");
        return EXIT_FAILURE;
    }

    if (fprintf(outf, "%s %s %s %s\n", buf.sysname, buf.release,
            buf.version, buf.machine) == -1)
    {
        perror("could not write output file");
        return EXIT_FAILURE;
    }

    /* getmntent, statvfs */
    f = fopen("/etc/mtab", "r");
    if (f == NULL)
    {
        perror("could not list filesystems");
        return EXIT_FAILURE;
    }
    while ((me = getmntent(f)) != NULL)
    {
        struct statvfs sfs;
        if (statvfs(me->mnt_dir, &sfs) != 0)
        {
            if (fprintf(outf, "%s failure\n", me->mnt_dir) == -1)
            {
                perror("could not write output file");
                return EXIT_FAILURE;
            }
        }
        if (fprintf(outf, "%s %f\n", me->mnt_dir,
                (double)sfs.f_bfree/sfs.f_blocks) == -1)
        {
            perror("could not write output file");
            return EXIT_FAILURE;
        }
    }
    if (fclose(f) == -1)
    {
        perror("could not list filesystems");
        return EXIT_FAILURE;
    }

    if (fclose(outf) == -1)
    {
        perror("could not close output file");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
