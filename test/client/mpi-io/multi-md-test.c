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
#include "pvfs2.h"

void vfs_mktestdir(int rank, int* n_ops);
void vfs_rmtestdir(int rank, int* n_ops);
void vfs_create(int rank, int* n_ops);
void vfs_rm(int rank, int* n_ops);
void vfs_prep(int rank, int* n_ops);
void vfs_readdir(int rank, int* n_ops);
void vfs_readdir_and_stat(int rank, int* n_ops);
void vfs_write(int rank, int* n_ops);
void vfs_read(int rank, int* n_ops);
void vfs_close(int rank, int* n_ops);
void vfs_print_error(int errcode, char *str); 

void mpi_print_error(int errcode, char *str); 

void pvfs_prep(int rank, int* n_ops);
void pvfs_mktestdir(int rank, int* n_ops);
void pvfs_rmtestdir(int rank, int* n_ops);
void pvfs_create(int rank, int* n_ops);
void pvfs_rm(int rank, int* n_ops);
void pvfs_readdir(int rank, int* n_ops);
void pvfs_readdir_and_stat(int rank, int* n_ops);
void pvfs_readdirplus(int rank, int* n_ops);
void pvfs_read(int rank, int* n_ops);
void pvfs_write(int rank, int* n_ops);

void pvfs_print_error(int errcode, char *str);

int* vfs_fds = NULL;
struct stat* vfs_stats = NULL;
char* vfs_buf = NULL;

#define PVFS_DIRENT_COUNT 32
PVFS_object_ref* pvfs_refs = NULL;
char* pvfs_buf = NULL;
PVFS_object_ref pvfs_basedir;
PVFS_object_ref pvfs_testdir;
PVFS_credentials pvfs_creds;

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
    void (*close) (int rank, int* n_ops);
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
        .close = vfs_close,
        .print_error = vfs_print_error,
    },
    {
        .name = "PVFS_sys",
        .prep = pvfs_prep,
        .mktestdir = pvfs_mktestdir,
        .rmtestdir = pvfs_rmtestdir,
        .create = pvfs_create,
        .rm = pvfs_rm,
        .readdir = pvfs_readdir,
        .readdir_and_stat = pvfs_readdir_and_stat,
        .readdirplus = pvfs_readdirplus,
        .read = pvfs_read,
        .write = pvfs_write,
        .close = NULL,
        .print_error = pvfs_print_error,
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
        .close = NULL,
        .print_error = mpi_print_error,
    },
    {0}
};

struct test_results
{
    char* op;
    int n_ops;
    double time;
    double min_time;
    int size;
    int nprocs;
};

#define MAX_TEST_COUNT 1000

#define CHECK_MAX_TEST(__x) \
do { \
    if(__x > MAX_TEST_COUNT) \
    { \
        fprintf(stderr, "Error: exceeded MAX_TEST_COUNT.\n"); \
        MPI_Abort(MPI_COMM_WORLD, 1); \
    } \
} while(0)

struct test_results* result_array;

#ifndef PATH_MAX
#define PATH_MAX FILENAME_MAX
#endif

extern char *optarg;
int opt_nfiles = -1;
char opt_basedir[PATH_MAX] = {0};
int opt_size = -1;
int opt_api = -1; 
int opt_pause = -1; 
int opt_start_clients = -1;
int opt_end_clients = -1;
int opt_interval_clients = -1;
unsigned int opt_timeout = 100;

void usage(char *name); 
int parse_args(int argc, char **argv);
void handle_error(int errcode, char *str); 
int run_test_phase(double* elapsed_time, double* min_elapsed_time, int* size, int* n_ops, char* fn_name, 
    void (*fn)(int, int*), int rank, int procs);
void print_result(int rank, struct test_results* result);

void usage(char *name)
{
    int i = 0;

    fprintf(stderr,
        "usage: %s -d base_dir -n num_files_per_proc -s size -a api -p seconds_to_pause -c client_spec <-t tcache_timeout_ms>\n", name);
    fprintf(stderr, "    where api is one of:\n");
    while(api_table[i].name != NULL)
    {
        fprintf(stderr, "        %d: %s\n", i, api_table[i].name);
        i++;
    }
    fprintf(stderr, "    and client_spec is of the form:\n");
    fprintf(stderr, "        <start client count>,<interval>,<end client count>\n");
    fprintf(stderr, "        and <interval> must be non-zero.\n");

    exit(-1);
}

int parse_args(
    int argc,
    char **argv)
{
    int c;
    int ret;
    while ((c = getopt(argc, argv, "d:n:a:s:p:c:t:")) != -1)
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
        case 'p':
            opt_pause = atoi(optarg);
            break;
        case 't':
            ret = sscanf(optarg, "%u", &opt_timeout);
            if(ret != 1)
            {
                usage(argv[0]);
                exit(-1);
            }
            break;
        case 'c':
            ret = sscanf(optarg, "%d,%d,%d", 
                &opt_start_clients, &opt_interval_clients, &opt_end_clients);
            if(ret != 3)
            {
                usage(argv[0]);
                exit(-1);
            }
            break;
        case '?':
        case ':':
        default:
            usage(argv[0]);
            exit(-1);
        }
    }
    if(opt_basedir[0] == 0 || opt_nfiles < 1 || opt_size < 1 || opt_api < 0 || opt_pause < 0 || opt_start_clients < 1 || opt_end_clients < 1 || opt_interval_clients < 1 || opt_start_clients > opt_end_clients)
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

void pvfs_print_error(
    int errcode,
    char *str)
{
    PVFS_perror(str, errcode);
}


void vfs_print_error(
    int errcode,
    char *str)
{
    fprintf(stderr, "%s: %s\n", str, strerror(errcode));
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

    /* NOTE: could probably skip tracking each phase and just print and
     * discard results after each phase.  Hanging onto this for now in case
     * it is useful later
     */
    result_array = malloc(MAX_TEST_COUNT*sizeof(*result_array));
    if(!result_array)
    {
        perror("malloc");
        return(-1);
    }

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

    parse_args(argc, argv);

    if(opt_end_clients > nprocs || opt_start_clients > nprocs)
    {
        if(rank == 0)
        {
            fprintf(stderr, "Error: not executed with enough processes for client specification.\n");
        }
        MPI_Finalize();
        return(-1);
    }

    /* key for data tables */
    if(rank == 0)
    {
        printf("# sysint tests using acache and ncache timeout of: %u ms.\n", 
            opt_timeout);
        printf("# <api>\t<op>\t<file_size>\t<procs>\t<n_ops_per_proc>\t<n_ops_total>\t<time>\t<rate_per_proc>\t<rate_total>\t<min time>\n");
    }

    /* do any setup required by the api */
    result_array[test].op = "prep";
    result_array[test].nprocs = nprocs;
    run_test_phase(
        &result_array[test].time, 
        &result_array[test].min_time, 
        &result_array[test].size,
        &result_array[test].n_ops,
        result_array[test].op, 
        api_table[opt_api].prep, 
        rank,
        nprocs);
    print_result(rank, &result_array[test]);
    test++;
    CHECK_MAX_TEST(test);

    /* make subdir for each proc */
    result_array[test].op = "mktestdir";
    result_array[test].nprocs = nprocs;
    run_test_phase(
        &result_array[test].time, 
        &result_array[test].min_time, 
        &result_array[test].size,
        &result_array[test].n_ops,
        result_array[test].op, 
        api_table[opt_api].mktestdir, 
        rank,
        nprocs);
    print_result(rank, &result_array[test]);
    test++;
    CHECK_MAX_TEST(test);

    for(i=opt_start_clients; i<=opt_end_clients; i+=opt_interval_clients)
    {
        /* create files */
        result_array[test].op = "create";
        result_array[test].nprocs = i;
        run_test_phase(
            &result_array[test].time, 
            &result_array[test].min_time, 
            &result_array[test].size,
            &result_array[test].n_ops,
            result_array[test].op, 
            api_table[opt_api].create, 
            rank,
            i);
        print_result(rank, &result_array[test]);
        test++;
        CHECK_MAX_TEST(test);

        /* readdir */
        result_array[test].op = "readdir";
        result_array[test].nprocs = i;
        run_test_phase(
            &result_array[test].time, 
            &result_array[test].min_time, 
            &result_array[test].size,
            &result_array[test].n_ops,
            result_array[test].op, 
            api_table[opt_api].readdir, 
            rank,
            i);
        print_result(rank, &result_array[test]);
        test++;
        CHECK_MAX_TEST(test);

        /* readdir and stat */
        result_array[test].op = "readdir_and_stat";
        result_array[test].nprocs = i;
        run_test_phase(
            &result_array[test].time, 
            &result_array[test].min_time, 
            &result_array[test].size,
            &result_array[test].n_ops,
            result_array[test].op, 
            api_table[opt_api].readdir_and_stat, 
            rank,
            i);
        print_result(rank, &result_array[test]);
        test++;
        CHECK_MAX_TEST(test);

        /* readdirplus */
        result_array[test].op = "readdirplus";
        result_array[test].nprocs = i;
        run_test_phase(
            &result_array[test].time, 
            &result_array[test].min_time, 
            &result_array[test].size,
            &result_array[test].n_ops,
            result_array[test].op, 
            api_table[opt_api].readdirplus, 
            rank,
            i);
        print_result(rank, &result_array[test]);
        test++;
        CHECK_MAX_TEST(test);

        /* write */
        result_array[test].op = "write";
        result_array[test].nprocs = i;
        run_test_phase(
            &result_array[test].time, 
            &result_array[test].min_time, 
            &result_array[test].size,
            &result_array[test].n_ops,
            result_array[test].op, 
            api_table[opt_api].write, 
            rank,
            i);
        print_result(rank, &result_array[test]);
        test++;
        CHECK_MAX_TEST(test);

        /* read */
        result_array[test].op = "read";
        result_array[test].nprocs = i;
        run_test_phase(
            &result_array[test].time, 
            &result_array[test].min_time, 
            &result_array[test].size,
            &result_array[test].n_ops,
            result_array[test].op, 
            api_table[opt_api].read, 
            rank,
            i);
        print_result(rank, &result_array[test]);
        test++;
        CHECK_MAX_TEST(test);

        /* readdir */
        result_array[test].op = "readdir";
        result_array[test].nprocs = i;
        run_test_phase(
            &result_array[test].time, 
            &result_array[test].min_time, 
            &result_array[test].size,
            &result_array[test].n_ops,
            result_array[test].op, 
            api_table[opt_api].readdir, 
            rank,
            i);
        print_result(rank, &result_array[test]);
        test++;
        CHECK_MAX_TEST(test);

        /* readdir and stat */
        result_array[test].op = "readdir_and_stat";
        result_array[test].nprocs = i;
        run_test_phase(
            &result_array[test].time, 
            &result_array[test].min_time, 
            &result_array[test].size,
            &result_array[test].n_ops,
            result_array[test].op, 
            api_table[opt_api].readdir_and_stat, 
            rank,
            i);
        print_result(rank, &result_array[test]);
        test++;
        CHECK_MAX_TEST(test);

        /* readdirplus */
        result_array[test].op = "readdirplus";
        result_array[test].nprocs = i;
        run_test_phase(
            &result_array[test].time, 
            &result_array[test].min_time, 
            &result_array[test].size,
            &result_array[test].n_ops,
            result_array[test].op, 
            api_table[opt_api].readdirplus, 
            rank,
            i);
        print_result(rank, &result_array[test]);
        test++;
        CHECK_MAX_TEST(test);

        /* close files */
        result_array[test].op = "close";
        result_array[test].nprocs = i;
        run_test_phase(
            &result_array[test].time, 
            &result_array[test].min_time, 
            &result_array[test].size,
            &result_array[test].n_ops,
            result_array[test].op, 
            api_table[opt_api].close, 
            rank,
            i);
        print_result(rank, &result_array[test]);
        test++;
        CHECK_MAX_TEST(test);

        /* remove files */
        result_array[test].op = "rm";
        result_array[test].nprocs = i;
        run_test_phase(
            &result_array[test].time, 
            &result_array[test].min_time, 
            &result_array[test].size,
            &result_array[test].n_ops,
            result_array[test].op, 
            api_table[opt_api].rm, 
            rank,
            i);
        print_result(rank, &result_array[test]);
        test++;
        CHECK_MAX_TEST(test);
    }

    /* remove subdir for each proc */
    result_array[test].op = "rmtestdir";
    result_array[test].nprocs = nprocs;
    run_test_phase(
        &result_array[test].time, 
        &result_array[test].min_time, 
        &result_array[test].size,
        &result_array[test].n_ops,
        result_array[test].op, 
        api_table[opt_api].rmtestdir, 
        rank,
        nprocs);
    print_result(rank, &result_array[test]);
    test++;
    CHECK_MAX_TEST(test);

    MPI_Finalize();

    return 0;
}

void print_result(int rank, struct test_results* result)
{
    if (rank == 0)
    {
        if(result->n_ops > 0)
        {
            printf("%s\t%s\t%d\t%d\t%d\t%d\t%f\t%f\t%f\t%f\n",
                api_table[opt_api].name,
                result->op,
                result->size,
                result->nprocs,
                result->n_ops,
                result->n_ops * result->nprocs,
                result->time,
                ((double)result->n_ops) / result->time,
                ((double)result->n_ops * result->nprocs) / result->time,
                result->min_time);
        }
    }

    return;
}

int run_test_phase(double* elapsed_time, double* min_elapsed_time, int* size, int* n_ops, char* fn_name, 
    void (*fn)(int, int*), int rank, int procs)
{
    double test_start, test_end, local_elapsed;

    if(fn)
    {
        /* pause a bit to let local caches timeout (if applicable) */
        if (rank == 0)
        {
            printf("# Pause...\n");
        }
        sleep(opt_pause);
    }

    if (rank == 0)
    {
        printf("# Now running [%s] test...\n", fn_name);
    }

    *size = opt_size;

    MPI_Barrier(MPI_COMM_WORLD);

    test_start = MPI_Wtime();

    /* if function is defined and we are within the current client count */
    if(fn && rank < procs)
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
    MPI_Allreduce(&local_elapsed, min_elapsed_time, 1,
                  MPI_DOUBLE, MPI_MIN, MPI_COMM_WORLD);

    return(0);
}

void pvfs_rmtestdir(int rank, int* n_ops)
{
    int ret;
    char test_dir[PATH_MAX];

    sprintf(test_dir, "rank%d", rank);

    *n_ops = 1;

    ret = PVFS_sys_remove(test_dir, pvfs_basedir, &pvfs_creds, NULL);
    if(ret < 0)
    {
        handle_error(ret, "PVFS_sys_remove");
    }
    
    return;
}

void pvfs_mktestdir(int rank, int* n_ops)
{
    int ret;
    PVFS_sysresp_mkdir resp_mkdir;
    char test_dir[PATH_MAX];
    PVFS_sys_attr attr;

    *n_ops = 1;

    sprintf(test_dir, "rank%d", rank);

    attr.owner = pvfs_creds.uid;
    attr.group = pvfs_creds.gid;
    attr.perms = PVFS_U_EXECUTE|PVFS_U_WRITE|PVFS_U_READ;
    attr.mask = (PVFS_ATTR_SYS_ALL_SETABLE);

    ret = PVFS_sys_mkdir(test_dir, pvfs_basedir, attr, &pvfs_creds, &resp_mkdir, NULL);
    if(ret < 0)
    {
        handle_error(ret, "PVFS_sys_mkdir");
    }

    pvfs_testdir = resp_mkdir.ref;

    return;
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

void pvfs_write(int rank, int* n_ops)
{
    int i;
    int ret;
    PVFS_sysresp_io resp_io;
    PVFS_Request mem_req, file_req;

    *n_ops = opt_nfiles;

    file_req = PVFS_BYTE;
    PVFS_Request_contiguous(opt_size, PVFS_BYTE, &mem_req);

    for(i=0; i<opt_nfiles; i++)
    {
        ret = PVFS_sys_write(pvfs_refs[i], file_req, 0, pvfs_buf, mem_req,
            &pvfs_creds, &resp_io, NULL);
        if(ret < 0)
        {
            handle_error(ret, "PVFS_sys_read");
        }
    }

    PVFS_Request_free(&mem_req);

    return;
}


void pvfs_read(int rank, int* n_ops)
{
    int i;
    int ret;
    PVFS_sysresp_io resp_io;
    PVFS_Request mem_req, file_req;

    *n_ops = opt_nfiles;

    file_req = PVFS_BYTE;
    PVFS_Request_contiguous(opt_size, PVFS_BYTE, &mem_req);

    for(i=0; i<opt_nfiles; i++)
    {
        ret = PVFS_sys_read(pvfs_refs[i], file_req, 0, pvfs_buf, mem_req,
            &pvfs_creds, &resp_io, NULL);
        if(ret < 0)
        {
            handle_error(ret, "PVFS_sys_read");
        }
    }

    PVFS_Request_free(&mem_req);

    return;
}


void vfs_read(int rank, int* n_ops)
{
    int i;
    int ret;

    *n_ops = opt_nfiles;

    for(i=0; i<opt_nfiles; i++)
    {
        ret = pread(vfs_fds[i], vfs_buf, opt_size, 0);
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
        ret = pwrite(vfs_fds[i], vfs_buf, opt_size, 0);
        if(ret < 0)
        {
            handle_error(errno, "pwrite");
        }
    }

    return;
}


void pvfs_create(int rank, int* n_ops)
{
    char test_file[PATH_MAX];
    PVFS_sys_attr attr;
    PVFS_sysresp_create resp_create;
    int ret;

    int i;

    *n_ops = opt_nfiles;

    attr.owner = pvfs_creds.uid;
    attr.group = pvfs_creds.gid;
    attr.perms = PVFS_U_WRITE|PVFS_U_READ;
    attr.mask = (PVFS_ATTR_SYS_ALL_SETABLE);

    for(i=0; i<opt_nfiles; i++)
    {
        sprintf(test_file, "%d", i);

        ret = PVFS_sys_create(test_file, pvfs_testdir, attr, &pvfs_creds,
            NULL, &resp_create, PVFS_SYS_LAYOUT_DEFAULT, NULL);
        if(ret < 0)
        {
            handle_error(ret, "PVFS_sys_create");
        }

        pvfs_refs[i] = resp_create.ref;
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
            handle_error(errno, "open");
        }
    }

    return;
}

void vfs_close(int rank, int* n_ops)
{
    int i;
    int ret;

    *n_ops = opt_nfiles;

    for(i=0; i<opt_nfiles; i++)
    {
        ret = close(vfs_fds[i]);
        if(ret < 0)
        {
            handle_error(errno, "close");
        }
    }

    return;
}

void pvfs_rm(int rank, int* n_ops)
{
    char test_file[PATH_MAX];
    int ret;
    int i;

    *n_ops = opt_nfiles;

    for(i=0; i<opt_nfiles; i++)
    {
        sprintf(test_file, "%d", i);

        ret = PVFS_sys_remove(test_file, pvfs_testdir, &pvfs_creds, NULL);
        if(ret < 0)
        {
            handle_error(ret, "PVFS_sys_remove");
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

void pvfs_prep(int rank, int* n_ops)
{
    int i;
    int ret;
    char pvfs_path[PATH_MAX];
    PVFS_fs_id fs_id;
    PVFS_sysresp_lookup resp_lookup;

    *n_ops = 1;

    /* initialize pvfs library */
    ret = PVFS_util_init_defaults();
    if(ret < 0)
    {
        handle_error(ret, "init");
    }

    /* set ncache and acache timeout values */
    ret = PVFS_sys_set_info(PVFS_SYS_NCACHE_TIMEOUT_MSECS, opt_timeout);
    if(ret < 0)
    {
        handle_error(ret, "PVFS_sys_set_info");
    }
    ret = PVFS_sys_set_info(PVFS_SYS_ACACHE_TIMEOUT_MSECS, opt_timeout);
    if(ret < 0)
    {
        handle_error(ret, "PVFS_sys_set_info");
    }

    /* set up an array of handles to keep track of files */
    pvfs_refs = malloc(opt_nfiles*sizeof(*pvfs_refs));
    if(!pvfs_refs)
    {
        handle_error(errno, "malloc");
    }

    /* a bufer to read and write from */
    pvfs_buf = malloc(opt_size);
    if(!pvfs_buf)
    {
        handle_error(errno, "malloc");
    }
    /* fill a pattern in */
    memset(pvfs_buf, 0, opt_size);
    for(i=0; i<opt_size; i++)
    {
        pvfs_buf[i] += i;
    }

    /* find the base directory */
    ret = PVFS_util_resolve(opt_basedir, &fs_id, pvfs_path, PATH_MAX);
    if(ret < 0)
    {
        handle_error(ret, "PVFS_util_resolve");
    }

    PVFS_util_gen_credentials(&pvfs_creds);

    ret = PVFS_sys_lookup(fs_id, pvfs_path, &pvfs_creds, &resp_lookup, 
        PVFS2_LOOKUP_LINK_FOLLOW, NULL);
    if(ret < 0)
    {
        handle_error(ret, "PVFS_sys_lookup");
    }
    pvfs_basedir = resp_lookup.ref;

    return;
}


void vfs_prep(int rank, int* n_ops)
{
    int i;

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

    /* a bufer to read and write from */
    vfs_buf = malloc(opt_size);
    if(!vfs_buf)
    {
        handle_error(errno, "malloc");
    }
    /* fill a pattern in */
    memset(vfs_buf, 0, opt_size);
    for(i=0; i<opt_size; i++)
    {
        vfs_buf[i] += i;
    }

    return;
}


void pvfs_readdirplus(int rank, int* n_ops)
{
    PVFS_sysresp_readdirplus rdplus_response;
    int ret;
    int total_entries = 0;
    PVFS_ds_position token = PVFS_READDIR_START;
    int i;

    *n_ops = opt_nfiles;

    do
    {
        ret = PVFS_sys_readdirplus(pvfs_testdir, token, PVFS_DIRENT_COUNT,
            &pvfs_creds, 
            PVFS_ATTR_SYS_ALL_NOHINT,
            &rdplus_response,
            NULL);
        if(ret < 0)
        {
            handle_error(ret, "PVFS_sys_readdirplus");
        }
        total_entries += rdplus_response.pvfs_dirent_outcount;
        if(rdplus_response.pvfs_dirent_outcount)
        {
            for(i=0; i< rdplus_response.pvfs_dirent_outcount; i++)
            {
                PVFS_util_release_sys_attr(&rdplus_response.attr_array[i]);
                if(rdplus_response.stat_err_array[i] != 0)
                {
                    handle_error(ret, "PVFS_sys_readdirplus attr");
                }
            }
            free(rdplus_response.dirent_array);
            free(rdplus_response.stat_err_array);
            free(rdplus_response.attr_array);
        }
        token = rdplus_response.token;
    } while(total_entries < opt_nfiles);

    return;
}


void pvfs_readdir(int rank, int* n_ops)
{
    PVFS_sysresp_readdir rd_response;
    int ret;
    int total_entries = 0;
    PVFS_ds_position token = PVFS_READDIR_START;

    *n_ops = opt_nfiles;

    do
    {
        ret = PVFS_sys_readdir(pvfs_testdir, token, PVFS_DIRENT_COUNT,
            &pvfs_creds, &rd_response, NULL);
        if(ret < 0)
        {
            handle_error(ret, "PVFS_sys_readdir");
        }
        total_entries += rd_response.pvfs_dirent_outcount;
        if(rd_response.pvfs_dirent_outcount)
        {
            free(rd_response.dirent_array);
        }
        token = rd_response.token;
    } while(total_entries < opt_nfiles);

    return;
}

void pvfs_readdir_and_stat(int rank, int* n_ops)
{
    PVFS_sysresp_readdir rd_response;
    PVFS_sysresp_getattr getattr_response;
    int ret;
    int total_entries = 0;
    PVFS_ds_position token = PVFS_READDIR_START;
    PVFS_object_ref tmp_ref;
    int i;

    *n_ops = opt_nfiles;

    do
    {
        ret = PVFS_sys_readdir(pvfs_testdir, token, PVFS_DIRENT_COUNT,
            &pvfs_creds, &rd_response, NULL);
        if(ret < 0)
        {
            handle_error(ret, "PVFS_sys_readdir");
        }

        for(i=0; i< rd_response.pvfs_dirent_outcount; i++)
        {
            tmp_ref.handle = rd_response.dirent_array[i].handle;
            tmp_ref.fs_id = pvfs_testdir.fs_id;
            ret = PVFS_sys_getattr(tmp_ref, PVFS_ATTR_SYS_ALL_NOHINT,
                &pvfs_creds, &getattr_response, NULL);
            if(ret < 0)
            {
                handle_error(ret, "PVFS_sys_getattr");
            }
            PVFS_util_release_sys_attr(&getattr_response.attr);
        }

        total_entries += rd_response.pvfs_dirent_outcount;
        if(rd_response.pvfs_dirent_outcount)
        {
            free(rd_response.dirent_array);
        }
        token = rd_response.token;
    } while(total_entries < opt_nfiles);

    return;
}


void vfs_readdir(int rank, int* n_ops)
{
    char test_dir[PATH_MAX];
    int i;
    struct dirent* dent;
    DIR* vfs_dir = NULL;

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
            handle_error(errno, "readdir");
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
    DIR* vfs_dir = NULL;

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
            handle_error(errno, "readdir");
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
