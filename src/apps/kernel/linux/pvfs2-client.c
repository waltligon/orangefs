/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include "pvfs2-types.h"

#ifndef PVFS2_VERSION
#define PVFS2_VERSION "Unknown"
#endif

#define PVFS2_CLIENT_CORE_NAME  "pvfs2-client-core"

#define MAX_DEV_INIT_FAILURES 10

#define DEFAULT_ACACHE_TIMEOUT_STR "5"

#define DEFAULT_LOGFILE "/tmp/pvfs2-client.log"

typedef struct
{
    int verbose;
    int foreground;
    char *acache_timeout;
    char *path;
    char *logfile;
} options_t;

static void client_sig_handler(int signum);
static void parse_args(int argc, char **argv, options_t *opts);
static int verify_pvfs2_client_path(options_t *opts);
static int monitor_pvfs2_client(options_t *opts);


int main(int argc, char **argv)
{
    pid_t new_pid = 0;
    options_t opts;

    memset(&opts, 0, sizeof(options_t));

    parse_args(argc, argv, &opts);

    /* pvfs2-client-core will call PINT_dev_initialize and hit 
     * this error, however since we close all file descriptors 
     * we don't see the message. */
    if ((getuid() != 0) && (geteuid() != 0))
    {
        fprintf(stderr, "Error: must be run as root\n");
	return(-1);
    }

    if (opts.verbose)
    {
        printf("pvfs2-client starting\n");
    }

    umask(027);

    signal(SIGHUP,  client_sig_handler);
    signal(SIGINT,  client_sig_handler);
    signal(SIGPIPE, client_sig_handler);
    signal(SIGILL,  client_sig_handler);
    signal(SIGTERM, client_sig_handler);
    signal(SIGSEGV, client_sig_handler);

    if (!opts.foreground)
    {
        if (opts.verbose)
        {
            printf("Backgrounding pvfs2-client daemon\n");
        }
        new_pid = fork();
        assert(new_pid != -1);

        if (new_pid > 0)
        {
            exit(0);
        }
        else if (setsid() < 0)
        {
            exit(1);
        }
        /* get rid of stdout/stderr/stdin */
        freopen("/dev/null", "r", stdin);
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);
    }
    return monitor_pvfs2_client(&opts);
}

static void client_sig_handler(int signum)
{
    kill(0, signum);
    switch (signum)
    {
        case SIGPIPE:
        case SIGILL:
        case SIGSEGV:
            exit(1);
        case SIGHUP:
        case SIGINT:
        case SIGTERM:
            exit(0);
    }
}

static int verify_pvfs2_client_path(options_t *opts)
{
    int ret = -1;
    struct stat statbuf;

    if (opts)
    {
        memset(&statbuf, 0 , sizeof(struct stat));

        if (stat(opts->path, &statbuf) == 0)
        {
            ret = ((S_ISREG(statbuf.st_mode) &&
                    (statbuf.st_mode & S_IXUSR)) ? 0 : 1);
            
        }
    }
    return ret;
}

static int monitor_pvfs2_client(options_t *opts)
{
    int ret = 1;
    pid_t new_pid = 0, wpid = 0;
    int dev_init_failures = 0;

    assert(opts);

    while(1)
    {
        if (opts->verbose)
        {
            printf("Spawning new child process\n");
        }
        new_pid = fork();
        assert(new_pid != -1);

        if (new_pid != 0)
        {
            if (opts->verbose)
            {
                printf("Waiting on child with pid %d\n", (int)new_pid);
            }

            wpid = waitpid(new_pid, &ret, 0);
            assert(wpid != -1);

            if (WIFEXITED(ret))
            {
                if (opts->verbose)
                {
                    printf("Child process with pid %d exited with "
                           "value %d\n", new_pid, (int)WEXITSTATUS(ret));
                }

                if (WEXITSTATUS(ret) == (unsigned char)-PVFS_EDEVINIT)
                {
                    /*
                      it's likely that the device was not released yet
                      by the client core process, even though it was
                      terminated.  in this case, sleep for a bit and
                      try again up to MAX_DEV_INIT_FAILURES
                      consecutive times.

                      this can happen after signaled termination,
                      particularly on 2.4.x kernels. it seems the
                      client-core sometimes doesn't release the device
                      quickly enough.  while inelegant, this sleep is
                      a temporary measure since we plan to remove the
                      signaled termination of the client-core all
                      together in the future.
                    */
                    if (++dev_init_failures == MAX_DEV_INIT_FAILURES)
                    {
                        break;
                    }
                    continue;
                }

                /* catch special case of exiting due to device error */
                if (WEXITSTATUS(ret) == (unsigned char)-PVFS_ENODEV)
                {
                    /* the pvfs2-client.log should log specifics from
                     * pvfs2-client-core
                     */
                    fprintf(stderr, "Device error caught, exiting now...\n");
                    exit(1);        
                }

                if ((opts->path[0] != '/') && (opts->path [0] != '.'))
                {
                    printf("*** The pvfs2-client-core has exited ***\n");
                    printf("If the pvfs2-client-core is not in your "
                           "configured PATH, please specify the\n full "
                           "path name (instead of \"%s\")\n",opts->path);
                }
                break;
            }

            if (WIFSIGNALED(ret))
            {
                dev_init_failures = 0;

                if (opts->verbose)
                {
                    printf("Child process with pid %d was killed by an "
                           "uncaught signal %d\n", new_pid,
                           WTERMSIG(ret));
                }
                continue;
            }
        }
        else
        {
            sleep(1);

            if (opts->verbose)
            {
                printf("About to exec %s\n",opts->path);
            }

            ret = execlp(opts->path, PVFS2_CLIENT_CORE_NAME, 
                    "-a", opts->acache_timeout, "-L", opts->logfile, NULL);

            fprintf(stderr, "Could not exec %s, errno is %d\n",
                    opts->path, errno);
            exit(1);
        }
    }
    return ret;
}

static void print_help(char *progname)
{
    assert(progname);

    printf("Usage: %s [OPTION]...[PATH]\n\n",progname);
    printf("-h, --help                    display this help and exit\n");
    printf("-v, --version                 display version and exit\n");
    printf("-V, --verbose                 run in verbose output mode\n");
    printf("-f, --foreground              run in foreground mode\n");
    printf("-L  --logfile                 specify log file to write to\n"
            "   (defaults to /tmp/pvfs2-client.log)\n");
    printf("-a MS, --acache-timeout=MS    acache timeout in ms "
           "(default is 0 ms)\n");
    printf("-p PATH, --path PATH          execute pvfs2-client at "
           "PATH\n");
}

static void parse_args(int argc, char **argv, options_t *opts)
{
    int ret = 0, option_index = 0;
    char *cur_option = NULL;

    static struct option long_opts[] =
    {
        {"help",0,0,0},
        {"version",0,0,0},
        {"verbose",0,0,0},
        {"foreground",0,0,0},
        {"logfile",1,0,0},
        {"acache-timeout",1,0,0},
        {"path",1,0,0},
        {0,0,0,0}
    };

    assert(opts);

    while((ret = getopt_long(argc, argv, "hvVfa:p:L:",
                             long_opts, &option_index)) != -1)
    {
        switch(ret)
        {
            case 0:
                cur_option = (char *)long_opts[option_index].name;

                if (strcmp("help", cur_option) == 0)
                {
                    goto do_help;
                }
                else if (strcmp("version", cur_option) == 0)
                {
                    goto do_version;
                }
                else if (strcmp("verbose", cur_option) == 0)
                {
                    goto do_verbose;
                }
                else if (strcmp("foreground", cur_option) == 0)
                {
                    goto do_foreground;
                }
                else if (strcmp("acache-timeout", cur_option) == 0)
                {
                    goto do_acache;
                }
                else if (strcmp("path", cur_option) == 0)
                {
                    goto do_path;
                }
                else if (strcmp("logfile", cur_option) == 0)
                {
                    goto do_logfile;
                }
                break;
            case 'h':
          do_help:
                print_help(argv[0]);
                exit(0);
            case 'v':
          do_version:
                printf("%s\n", PVFS2_VERSION);
                exit(0);
            case 'V':
          do_verbose:
                opts->verbose = 1;
                break;
            case 'f':
          do_foreground:
                opts->foreground = 1;
                /* for now, foreground implies verbose */
                goto do_verbose;
                break;
            case 'a':
          do_acache:
                opts->acache_timeout = optarg;
                break;
            case 'L':
          do_logfile:
                opts->logfile = optarg;
                break;
            case 'p':
          do_path:
                opts->path = optarg;
                if (verify_pvfs2_client_path(opts))
                {
                    fprintf(stderr, "Invalid pvfs2-client-core path: %s\n",
                            opts->path);
                    exit(1);
                }
                break;
            default:
                fprintf(stderr, "Unrecognized option.  "
                        "Try --help for information.\n");
                exit(1);
        }
    }

    if (!opts->logfile)
    {
        opts->logfile = DEFAULT_LOGFILE;
    }
    /* make sure that log file location is writable before proceeding */
    ret = open(opts->logfile, O_CREAT|O_WRONLY, S_IRUSR|S_IWUSR);
    if(ret < 0)
    {
        fprintf(stderr, "Error: logfile (%s) isn't writable.\n",
                opts->logfile);
        exit(1);
    } 

    if (!opts->path)
    {
        /*
          since they didn't specify a specific path, we're going to
          let execlp() sort things out later
        */
        opts->path = PVFS2_CLIENT_CORE_NAME;
    }

    if (!opts->acache_timeout)
    {
        opts->acache_timeout = DEFAULT_ACACHE_TIMEOUT_STR;
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
