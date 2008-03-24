/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */

/* Tests rate of concurrent independent metadata operations using multiple
 * interfaces
 */

#define _XOPEN_SOURCE 500

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>

#include <mpi.h>

void vfs_mktestdir(int rank, int* n_ops);
void vfs_rmtestdir(int rank, int* n_ops);
void vfs_create(int rank, int* n_ops);
void vfs_rm(int rank, int* n_ops);
void vfs_prep(int rank, int* n_ops);
void vfs_readdir(int rank, int* n_ops);
void vfs_readdir_and_stat(int rank, int* n_ops);
void vfs_write(int rank, int* n_ops);
void vfs_read(int rank, int* n_ops);
void vfs_print_error(int errcode, char *str); 

void mpi_print_error(int errcode, char *str); 

int* vfs_fds = NULL;
DIR* vfs_dir = NULL;
struct stat* vfs_stats = NULL;
char* vfs_buf = NULL;

int current_size = -1;

struct api_ops
{
    char *name;
    void (*prep) (int rank, int* n_ops);
    void (*mktestdir) (int rank, int* n_ops);
    void (*rmtestdir) (int rank, int* n_ops);
    void (*create) (int rank, int* n_ops);
    void (*rm) (int rank, int* n_ops);
    void (*readdir) (int rank, int* n_ops);
    void (*readdir_and_stat) (int rank, int* n_ops);
    void (*readdirplus) (int rank, int* n_ops);
    void (*write) (int rank, int* n_ops);
    void (*read) (int rank, int* n_ops);
    void (*unstuff) (int rank, int* n_ops);
    void (*print_error) (int errorcode, char* str);
};

struct api_ops api_table[] = {
    {
        .name = "VFS",
        .prep = vfs_prep,
        .mktestdir = vfs_mktestdir,
        .rmtestdir = vfs_rmtestdir,
        .create = vfs_create,
        .rm = vfs_rm,
        .readdir = vfs_readdir,
        .readdir_and_stat = vfs_readdir_and_stat,
        .readdirplus = NULL,
        .read = vfs_read,
        .write = vfs_write,
        .unstuff = NULL,
        .print_error = vfs_print_error,
    },
    {
        .name = "PVFS system interface",
        .prep = NULL,
        .mktestdir = NULL,
        .rmtestdir = NULL,
        .create = NULL,
        .rm = NULL,
        .readdir = NULL,
        .readdir_and_stat = NULL,
        .readdirplus = NULL,
        .read = NULL,
        .write = NULL,
        .unstuff = NULL,
        .print_error = NULL,
    },
    {
        .name = "MPI-IO",
        .prep = NULL,
        .mktestdir = vfs_mktestdir, /* borrow vfs mkdir */
        .rmtestdir = vfs_rmtestdir, /* borrow vfs rmdir */
        .create = NULL,
        .rm = NULL,
        .readdir = NULL,
        .readdir_and_stat = NULL,
        .readdirplus = NULL,
        .read = NULL,
        .write = NULL,
        .unstuff = NULL,
        .print_error = mpi_print_error,
    },
    {0}
};

struct test_results
{
    char* op;
    int n_ops;
    double time;
    int size;
};

struct test_results result_array[100];

#ifndef PATH_MAX
#define PATH_MAX FILENAME_MAX
#endif

extern char *optarg;
int opt_nfiles = -1;
char opt_basedir[PATH_MAX] = {0};
int opt_size = -1;
int opt_size2 = -1;
int opt_api = -1; 

void usage(char *name); 
int parse_args(int argc, char **argv);
void handle_error(int errcode, char *str); 
int run_test_phase(double* elapsed_time, int* size, int* n_ops, char* fn_name, 
    void (*fn)(int, int*), int rank);

void usage(char *name)
{
    int i = 0;

    fprintf(stderr,
        "usage: %s -d base_dir -n num_files_per_proc -s size1 -S size2 -a api \n", name);
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
    while ((c = getopt(argc, argv, "d:n:a:s:S:")) != -1)
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
        case 'S':
            opt_size2 = atoi(optarg);
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
    if(opt_basedir[0] == 0 || opt_nfiles < 1 || opt_size < 1 || opt_api < 0 || opt_size2 < 1)
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
    if(api_table[opt_api].print_error)
    {
        api_table[opt_api].print_error(errcode, str);
    }
    else
    {
        fprintf(stderr, "Error: %s: %d\n", str, errcode);
    }
    MPI_Abort(MPI_COMM_WORLD, 1);
}

void vfs_print_error(
    int errcode,
    char *str)
{
    perror(str);
}

void mpi_print_error(
    int errcode,
    char *str)
{
    char msg[MPI_MAX_ERROR_STRING];
    int resultlen;
    MPI_Error_string(errcode, msg, &resultlen);
    fprintf(stderr, "%s: %s\n", str, msg);
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

    current_size = 0;

    /* do any setup required by the api */
    result_array[test].op = "prep";
    run_test_phase(
        &result_array[test].time, 
        &result_array[test].size,
        &result_array[test].n_ops,
        result_array[test].op, 
        api_table[opt_api].prep, 
        rank);
    test++;

    /* make subdir for each proc */
    result_array[test].op = "mktestdir";
    run_test_phase(
        &result_array[test].time, 
        &result_array[test].size,
        &result_array[test].n_ops,
        result_array[test].op, 
        api_table[opt_api].mktestdir, 
        rank);
    test++;

    /* create files */
    result_array[test].op = "create";
    run_test_phase(
        &result_array[test].time, 
        &result_array[test].size,
        &result_array[test].n_ops,
        result_array[test].op, 
        api_table[opt_api].create, 
        rank);
    test++;

    /* readdir */
    result_array[test].op = "readdir";
    run_test_phase(
        &result_array[test].time, 
        &result_array[test].size,
        &result_array[test].n_ops,
        result_array[test].op, 
        api_table[opt_api].readdir, 
        rank);
    test++;

    /* readdir and stat */
    result_array[test].op = "readdir_and_stat";
    run_test_phase(
        &result_array[test].time, 
        &result_array[test].size,
        &result_array[test].n_ops,
        result_array[test].op, 
        api_table[opt_api].readdir_and_stat, 
        rank);
    test++;

    /* readdirplus */
    result_array[test].op = "readdirplus";
    run_test_phase(
        &result_array[test].time, 
        &result_array[test].size,
        &result_array[test].n_ops,
        result_array[test].op, 
        api_table[opt_api].readdirplus, 
        rank);
    test++;

    /* use first size */
    current_size = opt_size;

    /* write */
    result_array[test].op = "write";
    run_test_phase(
        &result_array[test].time, 
        &result_array[test].size,
        &result_array[test].n_ops,
        result_array[test].op, 
        api_table[opt_api].write, 
        rank);
    test++;

    /* read */
    result_array[test].op = "read";
    run_test_phase(
        &result_array[test].time, 
        &result_array[test].size,
        &result_array[test].n_ops,
        result_array[test].op, 
        api_table[opt_api].read, 
        rank);
    test++;

    /* readdir */
    result_array[test].op = "readdir";
    run_test_phase(
        &result_array[test].time, 
        &result_array[test].size,
        &result_array[test].n_ops,
        result_array[test].op, 
        api_table[opt_api].readdir, 
        rank);
    test++;

    /* readdir and stat */
    result_array[test].op = "readdir_and_stat";
    run_test_phase(
        &result_array[test].time, 
        &result_array[test].size,
        &result_array[test].n_ops,
        result_array[test].op, 
        api_table[opt_api].readdir_and_stat, 
        rank);
    test++;

    /* readdirplus */
    result_array[test].op = "readdirplus";
    run_test_phase(
        &result_array[test].time, 
        &result_array[test].size,
        &result_array[test].n_ops,
        result_array[test].op, 
        api_table[opt_api].readdirplus, 
        rank);
    test++;

    /* remove files */
    result_array[test].op = "rm";
    run_test_phase(
        &result_array[test].time, 
        &result_array[test].size,
        &result_array[test].n_ops,
        result_array[test].op, 
        api_table[opt_api].rm, 
        rank);
    test++;

    current_size = 0;

    /* create files (again) */
    result_array[test].op = "create";
    run_test_phase(
        &result_array[test].time, 
        &result_array[test].size,
        &result_array[test].n_ops,
        result_array[test].op, 
        api_table[opt_api].create, 
        rank);
    test++;

    /* use second size */
    current_size = opt_size2;

    /* write */
    result_array[test].op = "write";
    run_test_phase(
        &result_array[test].time, 
        &result_array[test].size,
        &result_array[test].n_ops,
        result_array[test].op, 
        api_table[opt_api].write, 
        rank);
    test++;

    /* read */
    result_array[test].op = "read";
    run_test_phase(
        &result_array[test].time, 
        &result_array[test].size,
        &result_array[test].n_ops,
        result_array[test].op, 
        api_table[opt_api].read, 
        rank);
    test++;

    /* readdir */
    result_array[test].op = "readdir";
    run_test_phase(
        &result_array[test].time, 
        &result_array[test].size,
        &result_array[test].n_ops,
        result_array[test].op, 
        api_table[opt_api].readdir, 
        rank);
    test++;

    /* readdir and stat */
    result_array[test].op = "readdir_and_stat";
    run_test_phase(
        &result_array[test].time, 
        &result_array[test].size,
        &result_array[test].n_ops,
        result_array[test].op, 
        api_table[opt_api].readdir_and_stat, 
        rank);
    test++;

    /* readdirplus */
    result_array[test].op = "readdirplus";
    run_test_phase(
        &result_array[test].time, 
        &result_array[test].size,
        &result_array[test].n_ops,
        result_array[test].op, 
        api_table[opt_api].readdirplus, 
        rank);
    test++;

    /* remove files */
    result_array[test].op = "rm";
    run_test_phase(
        &result_array[test].time, 
        &result_array[test].size,
        &result_array[test].n_ops,
        result_array[test].op, 
        api_table[opt_api].rm, 
        rank);
    test++;

    current_size = 0;

    /* remove subdir for each proc */
    result_array[test].op = "rmtestdir";
    run_test_phase(
        &result_array[test].time, 
        &result_array[test].size,
        &result_array[test].n_ops,
        result_array[test].op, 
        api_table[opt_api].rmtestdir, 
        rank);
    test++;


    /* print all results */
    if (rank == 0)
    {
        printf("<api>\t<op>\t<file size>\t<procs>\t<n_ops_per_proc>\t<n_ops_total>\t<time>\t<rate_per_proc>\t<rate_total>\n");
        /* this phase only makes one dir per proc */
        for(i=0; i<test; i++)
        {
            if(result_array[i].n_ops > 0)
            {
                printf("%s\t%s\t%d\t%d\t%d\t%d\t%f\t%f\t%f\n",
                    api_table[opt_api].name,
                    result_array[i].op,
                    result_array[i].size,
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

int run_test_phase(double* elapsed_time, int* size, int* n_ops, char* fn_name, 
    void (*fn)(int, int*), int rank)
{
    double test_start, test_end, local_elapsed;

    if (rank == 0)
    {
        printf("# Pause...\n");
    }

    /* pause a bit to let local caches timeout (if applicable) */
    sleep(5);

    if (rank == 0)
    {
        printf("# Now running [%s] test...\n", fn_name);
    }

    *size = current_size;

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
        handle_error(errno, "mkdir");
    }

    return;
}

void vfs_read(int rank, int* n_ops)
{
    int i;
    int ret;

    *n_ops = opt_nfiles;

    for(i=0; i<opt_nfiles; i++)
    {
        ret = pread(vfs_fds[i], vfs_buf, current_size, 0);
        if(ret < 0)
        {
            handle_error(errno, "pread");
        }
    }

    return;
}


void vfs_write(int rank, int* n_ops)
{
    int i;
    int ret;

    *n_ops = opt_nfiles;

    for(i=0; i<opt_nfiles; i++)
    {
        ret = pwrite(vfs_fds[i], vfs_buf, current_size, 0);
        if(ret < 0)
        {
            handle_error(errno, "pwrite");
        }
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

        vfs_fds[i] = open(test_file, (O_CREAT|O_RDWR), (S_IWUSR|S_IRUSR));
        if(vfs_fds[i] < 0)
        {
            handle_error(errno, "creat");
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
            handle_error(errno, "unlink");
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
        handle_error(errno, "rmdir");
    }

    return;
}

void vfs_prep(int rank, int* n_ops)
{
    int i;
    int biggest;

    *n_ops = 1;

    /* set up an array of file descriptors to keep track of open files */
    vfs_fds = malloc(opt_nfiles*sizeof(int));
    if(!vfs_fds)
    {
        handle_error(errno, "malloc");
    }

    /* ditto for stat results */
    vfs_stats = malloc((opt_nfiles+2)*sizeof(struct stat));
    if(!vfs_stats)
    {
        handle_error(errno, "malloc");
    }

    biggest = (opt_size > opt_size2 ? opt_size : opt_size2);

    /* a bufer to read and write from */
    vfs_buf = malloc(biggest);
    if(!vfs_buf)
    {
        handle_error(errno, "malloc");
    }
    /* fill a pattern in */
    memset(vfs_buf, 0, biggest);
    for(i=0; i<biggest; i++)
    {
        vfs_buf[i] += biggest;
    }

    return;
}

void vfs_readdir(int rank, int* n_ops)
{
    char test_dir[PATH_MAX];
    int i;
    struct dirent* dent;

    *n_ops = opt_nfiles + 2; /* . and .. entries */

    sprintf(test_dir, "%s/rank%d", opt_basedir, rank);
    vfs_dir = opendir(test_dir);
    if(!vfs_dir)
    {
        handle_error(errno, "opendir");
    }

    for(i=0; i<(*n_ops); i++)
    {
        dent = readdir(vfs_dir);
        if(!dent)
        {
            handle_error(errno, "creat");
        }
    }

    return;

}

void vfs_readdir_and_stat(int rank, int* n_ops)
{
    char test_dir[PATH_MAX];
    char test_file[PATH_MAX];
    int fname_off = 0;
    int i;
    struct dirent* dent;
    int ret;

    *n_ops = opt_nfiles + 2; /* . and .. entries */

    sprintf(test_dir, "%s/rank%d", opt_basedir, rank);
    vfs_dir = opendir(test_dir);
    if(!vfs_dir)
    {
        handle_error(errno, "opendir");
    }
    
    fname_off = sprintf(test_file, "%s/rank%d/", opt_basedir, rank);

    for(i=0; i<(*n_ops); i++)
    {
        dent = readdir(vfs_dir);
        if(!dent)
        {
            handle_error(errno, "creat");
        }
        sprintf(&test_file[fname_off], "%s", dent->d_name);
        ret = stat(test_file, &vfs_stats[i]);
        if(ret != 0)
        {
            handle_error(errno, "stat");
        }
    }

    return;
}


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
