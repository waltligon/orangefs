/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */

/* Tests rate of concurrent independent metadata operations using multiple
 * interfaces
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>

#include <mpi.h>

void vfs_mktestdir(int rank, int* n_ops);
void vfs_rmtestdir(int rank, int* n_ops);
void vfs_create(int rank, int* n_ops);
void vfs_rm(int rank, int* n_ops);
void vfs_prep(int rank, int* n_ops);

int* vfs_fds = NULL;

struct api_ops
{
    char *name;
    void (*prep) (int rank, int* n_ops);
    void (*mktestdir) (int rank, int* n_ops);
    void (*rmtestdir) (int rank, int* n_ops);
    void (*create) (int rank, int* n_ops);
    void (*rm) (int rank, int* n_ops);
};

struct api_ops api_table[] = {
    {
        .name = "VFS",
        .prep = vfs_prep,
        .mktestdir = vfs_mktestdir,
        .rmtestdir = vfs_rmtestdir,
        .create = vfs_create,
        .rm = vfs_rm,
    },
    {
        .name = "PVFS system interface",
        .prep = NULL,
        .mktestdir = NULL,
        .rmtestdir = NULL,
        .create = NULL,
        .rm = NULL,
    },
    {
        .name = "MPI-IO",
        .prep = NULL,
        .mktestdir = vfs_mktestdir, /* borrow vfs mkdir */
        .rmtestdir = vfs_rmtestdir, /* borrow vfs rmdir */
        .create = NULL,
        .rm = NULL,
    },
    {0}
};

struct test_results
{
    char* op;
    int n_ops;
    double time;
};

struct test_results result_array[100];

#ifndef PATH_MAX
#define PATH_MAX FILENAME_MAX
#endif

extern char *optarg;
int opt_nfiles = -1;
char opt_basedir[PATH_MAX] = {0};
int opt_size = -1;
int opt_api = -1; 

void usage(char *name); 
int parse_args(int argc, char **argv);
void handle_error(int errcode, char *str); 
int run_test_phase(double* elapsed_time, int* n_ops, char* fn_name, 
    void (*fn)(int, int*), int rank);

void usage(char *name)
{
    int i = 0;

    fprintf(stderr,
        "usage: %s -d base_dir -n num_files_per_proc -s size_of_files -a api \n", name);
    fprintf(stderr, "    where api is one of:\n");
    while(api_table[i].name != NULL)
    {
        fprintf(stderr, "        %d: %s\n", i, api_table[i].name);
        i++;
    }

    exit(-1);
}

int parse_args(
    int argc,
    char **argv)
{
    int c;
    while ((c = getopt(argc, argv, "d:n:a:s:")) != -1)
    {
        switch (c)
        {
        case 'd':
            strncpy(opt_basedir, optarg, PATH_MAX);
            break;
        case 'n':
            opt_nfiles = atoi(optarg);
            break;
        case 's':
            opt_size = atoi(optarg);
            break;
        case 'a':
            opt_api = atoi(optarg);
            break;
        case '?':
        case ':':
        default:
            usage(argv[0]);
            exit(-1);
        }
    }
    if(opt_basedir[0] == 0 || opt_nfiles < 1 || opt_size < 1 || opt_api < 0)
    {
        usage(argv[0]);
        exit(-1);
    }

    return 0;
}

void handle_error(
    int errcode,
    char *str)
{
    char msg[MPI_MAX_ERROR_STRING];
    int resultlen;
    MPI_Error_string(errcode, msg, &resultlen);
    fprintf(stderr, "%s: %s\n", str, msg);
    MPI_Abort(MPI_COMM_WORLD, 1);
}


int main(
    int argc,
    char **argv)
{
    int rank, nprocs;
    int test = 0;
    int i;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

    parse_args(argc, argv);

    /* do any setup required by the api */
    result_array[test].op = "prep";
    run_test_phase(
        &result_array[test].time, 
        &result_array[test].n_ops,
        result_array[test].op, 
        api_table[opt_api].prep, 
        rank);
    test++;

    /* make subdir for each proc */
    result_array[test].op = "mktestdir";
    run_test_phase(
        &result_array[test].time, 
        &result_array[test].n_ops,
        result_array[test].op, 
        api_table[opt_api].mktestdir, 
        rank);
    test++;

    /* create files */
    result_array[test].op = "create";
    run_test_phase(
        &result_array[test].time, 
        &result_array[test].n_ops,
        result_array[test].op, 
        api_table[opt_api].create, 
        rank);
    test++;

    /* remove files */
    result_array[test].op = "rm";
    run_test_phase(
        &result_array[test].time, 
        &result_array[test].n_ops,
        result_array[test].op, 
        api_table[opt_api].rm, 
        rank);
    test++;

    /* remove subdir for each proc */
    result_array[test].op = "rmtestdir";
    run_test_phase(
        &result_array[test].time, 
        &result_array[test].n_ops,
        result_array[test].op, 
        api_table[opt_api].rmtestdir, 
        rank);
    test++;


    /* print all results */
    if (rank == 0)
    {
        printf("<api>\t<op>\t<procs>\t<n_ops_per_proc>\t<n_ops_total>\t<time>\t<rate_per_proc>\t<rate_total>\n");
        /* this phase only makes one dir per proc */
        for(i=0; i<test; i++)
        {
            if(result_array[i].n_ops > 0)
            {
                printf("%s\t%s\t%d\t%d\t%d\t%f\t%f\t%f\n",
                    api_table[opt_api].name,
                    result_array[i].op,
                    nprocs,
                    result_array[i].n_ops,
                    result_array[i].n_ops*nprocs,
                    result_array[i].time,
                    ((double)result_array[i].n_ops)/result_array[i].time,
                    ((double)result_array[i].n_ops*nprocs)/result_array[i].time);
            }
        }
    }

    MPI_Finalize();

    return 0;
}

int run_test_phase(double* elapsed_time, int* n_ops, char* fn_name, 
    void (*fn)(int, int*), int rank)
{
    double test_start, test_end, local_elapsed;

    if (rank == 0)
    {
        printf("# Now running [%s] test...\n", fn_name);
    }

    MPI_Barrier(MPI_COMM_WORLD);

    test_start = MPI_Wtime();

    if(fn)
    {
        /* we assume this aborts if it fails */
        fn(rank, n_ops);
    }
    else
    {
        if(rank == 0)
        {
            printf("#   Skipping test.\n");
        }
    }

    test_end = MPI_Wtime();

    local_elapsed = test_end - test_start;

    MPI_Allreduce(&local_elapsed, elapsed_time, 1,
                  MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);

    return(0);
}

void vfs_mktestdir(int rank, int* n_ops)
{
    char test_dir[PATH_MAX];
    int ret;

    *n_ops = 1;

    sprintf(test_dir, "%s/rank%d", opt_basedir, rank);

    ret = mkdir(test_dir, S_IRWXU);
    if(ret != 0)
    {
        handle_error(-errno, "mkdir");
    }

    return;
}

void vfs_create(int rank, int* n_ops)
{
    char test_file[PATH_MAX];
    int fname_off = 0;
    int i;

    *n_ops = opt_nfiles;
    fname_off = sprintf(test_file, "%s/rank%d/", opt_basedir, rank);

    for(i=0; i<opt_nfiles; i++)
    {
        sprintf(&test_file[fname_off], "%d", i);

        vfs_fds[i] = creat(test_file, S_IRWXU);
        if(vfs_fds[i] < 0)
        {
            handle_error(-errno, "creat");
        }
    }

    return;
}

void vfs_rm(int rank, int* n_ops)
{
    char test_file[PATH_MAX];
    int ret;
    int fname_off = 0;
    int i;

    *n_ops = opt_nfiles;
    fname_off = sprintf(test_file, "%s/rank%d/", opt_basedir, rank);

    for(i=0; i<opt_nfiles; i++)
    {
        sprintf(&test_file[fname_off], "%d", i);

        ret = unlink(test_file);
        if(ret < 0)
        {
            handle_error(-errno, "unlink");
        }
    }

    return;
}

void vfs_rmtestdir(int rank, int* n_ops)
{
    char test_dir[PATH_MAX];
    int ret;

    *n_ops = 1;

    sprintf(test_dir, "%s/rank%d", opt_basedir, rank);

    ret = rmdir(test_dir);
    if(ret != 0)
    {
        handle_error(-errno, "rmdir");
    }

    return;
}

void vfs_prep(int rank, int* n_ops)
{
    *n_ops = 1;

    /* set up an array of file descriptors to keep track of open files */
    vfs_fds = malloc(opt_nfiles*sizeof(int));
    if(!vfs_fds)
    {
        handle_error(-errno, "malloc");
    }

}


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
