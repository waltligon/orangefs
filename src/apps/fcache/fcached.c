/*
 * (C) 2015 Clemson University
 *
 * See COPYING in top-level directory.
 */

/**
 *  PVFS2 File Cache Daemon
 */

#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <limits.h>
#include <assert.h>

#include <gossip.h>
#include <pvfs2-debug.h>
#include <pvfs2-types.h>
#include "fcached.h"

static char * obtain_pathname(
    const char * arg,
    const char * env,
    const char * conf_file_val,
    const char * impl_default);
static int open_pidfile(char * pidfile_pathname_str);
static int write_pidfile(int pidfile_fd);
static void remove_pidfile(void);
static void cleanup(void);
static void usage(int argc, char **argv);


static char * logfile_pathname = NULL;
static char * pidfile_pathname = NULL;

/**
 * Obtains a path name from the various methods of configuration in the
 * following order:
 *   - command line argument checked for in main
 *   - environment variable string
 *   - configuration file variable (obtained via some mechanism), TODO!
 *   - implementation default declared in some header
 *
 * This function just iterates over the parameters and attempts to strndup 
 * the first found parameter that is not 
 */
static char * obtain_pathname(
    const char * arg,
    const char * env,
    const char * conf_file_val,
    const char * impl_default)
{
    if(arg != NULL)
    {
        return strndup(arg, PVFS_PATH_MAX);
    }
    else if(env != NULL)
    {
        /* environment variable */
        char * tmp = getenv(env);
        if(tmp != NULL)
        {
            return strndup(tmp, PVFS_PATH_MAX);
        }
    }

    if(conf_file_val)
    {
        return strndup(conf_file_val, PVFS_PATH_MAX);
    }

    else if(impl_default)
    {
        return strndup(impl_default, PVFS_PATH_MAX);
    }

    /* An error iff the the implementation default is !NULL */
    return NULL;
}

static int open_pidfile(char * pidfile_pathname_str)
{
    int ret = -1;
    int pidfile_fd = -1;

    pidfile_fd = open(
        pidfile_pathname,
        O_CREAT | O_EXCL | O_WRONLY,
        FCACHE_PIDFILE_MODE);

    if(pidfile_fd == -1)
    {
        return pidfile_fd;
    }

    ret = atexit(remove_pidfile);
    if(ret != 0)
    {
        fprintf(stderr,
                "fcached failed to register the remove_pidfile function with "
                "atexit, ret = %d\n",
                ret);
        close(pidfile_fd);
        return -1;
    }
    return pidfile_fd;
}

static int write_pidfile(int pidfile_fd)
{
    int ret = -1;
    char pid_str[FCACHE_PID_BUFF_LEN] = {0};

    ret = snprintf(pid_str, FCACHE_PID_BUFF_LEN, "%ld\n", (long int) getpid());
    int pid_len = strlen(pid_str);
    ret = write(pidfile_fd, pid_str, pid_len);
    if(ret < pid_len)
    {
        close(pidfile_fd);
        return -1;
    }

    ret = close(pidfile_fd);
    if(ret < 0)
    {
        return -1;
    }
    return 0;
}

static void remove_pidfile(void)
{
    assert(pidfile_pathname);
    unlink(pidfile_pathname);
}

static void cleanup(void)
{
    gossip_debug(GOSSIP_FCACHE_DEBUG, "fcached cleanup starting...\n");
    /* Try flushing any dirty cached pages to OrangeFS */


    /* TODO */

    if(logfile_pathname)
    {
        free(logfile_pathname);
    }
    if(pidfile_pathname)
    {
        free(pidfile_pathname);
    }

    /* Cleanup the gossip logging utility */
    gossip_debug(GOSSIP_FCACHE_DEBUG, "fcached cleanup complete.\n");
    gossip_disable();
}

static void usage(int argc, char **argv)
{
    /* TODO */
    fprintf(stderr, "usage: %s\n", argv[0]);
}

int main(int argc, char *argv[])
{
    char * logfile_pathname_local = NULL;
    char * pidfile_pathname_local = NULL;
    int pidfile_fd = 0;
    int ret = 0;
    /* TODO: unsigned char foreground = 0; */

    ret = atexit(cleanup);
    if(ret != 0)
    {
        fprintf(stderr,
                "fcached failed to register the cleanup function with atexit, "
                "ret = %d\n", ret);
        exit(EXIT_FAILURE);
    }

    while(1)
    {
        static struct option long_options[] =
        {
            {"foreground", no_argument, 0, 'd'},
            {"help", no_argument, 0, 'h'},
            {"logfile", required_argument, 0, 'l'},
            {"pidfile", required_argument, 0, 'p'},
            {0, 0, 0, 0}
        };
        int long_optind = 0;
        const char * optstring = "dhl:p:?";
        ret = getopt_long(argc, argv, optstring, long_options, &long_optind);
        if(ret == -1)
        {
            break;
        }
        switch(ret)
        {
            case 0:
                break;
            case 'd':
                printf("option -d\n");
                break;
            case 'h':
                printf("option -h\n");
                break;
            case 'l':
                printf("option -l with value '%s'\n", optarg);
                logfile_pathname_local = optarg;
                break;
            case 'p':
                printf("option -p with value '%s'\n", optarg);
                pidfile_pathname_local = optarg;
                break;
            case '?':
                usage(argc, argv);
                /** We get here if user passed '?' as an option or some
                 * unrecognized flag. 'optopt' is set to 0 if the '?' flag was
                 * specified; otherwise, 'optopt' is set to the unrecognized
                 * character. The former should EXIT_FAILURE, while the latter
                 * should EXIT_SUCCESS.
                 */
                if(optopt != 0)
                {
                    exit(EXIT_FAILURE);
                }
                else
                {
                    exit(EXIT_SUCCESS);
                }
            default:
                usage(argc, argv);
                exit(EXIT_FAILURE);
        }
    }

    if(optind < argc)
    {
        fprintf(stderr, "unrecognized ARGV-elements:\n");
        while(optind < argc)
        {
            fprintf(stderr, "\t%s\n", argv[optind++]);
        }
        usage(argc, argv);
        exit(EXIT_FAILURE);
    }

    /* Change program state to reflect provided options... */
    /* --------------------------------------------------- */
    /*  Run in foreground ?
     *   - then don't daemonize and have gossip write to stderr
     *   - otherwise, have stderr written to logfile and daemonize!
     */
    /* TODO */


    logfile_pathname = obtain_pathname(
        logfile_pathname_local,
        FCACHE_LOGFILE_ENV_VAR,
        NULL,
        FCACHE_LOGFILE_DEFAULT);

#if 0
    fprintf(stdout, "INFO: logfile_pathname = %s\n", logfile_pathname);
#endif

    if(logfile_pathname == NULL)
    {
        perror("Error: fcached failed to obtain and/or allocate the "
               "logfile_pathname");
        exit(EXIT_FAILURE);
    }

    ret = gossip_enable_file(logfile_pathname, "a");
    if(ret != 0)
    {
        fprintf(stderr, "fcached failed to open the logfile: %s\n",
                logfile_pathname);
        exit(EXIT_FAILURE);
    }

    pidfile_pathname = obtain_pathname(
        pidfile_pathname_local,
        FCACHE_PIDFILE_ENV_VAR,
        NULL,
        FCACHE_PIDFILE_DEFAULT);

#if 0
    fprintf(stdout, "INFO: pidfile_pathname = %s\n", pidfile_pathname);
#endif

    if(pidfile_pathname == NULL && (
       pidfile_pathname_local != NULL ||
       getenv(FCACHE_PIDFILE_ENV_VAR) != NULL ||
       FCACHE_PIDFILE_DEFAULT != NULL))
    {
        perror("Error: fcached failed to obtain and/or allocate the "
               "pidfile_pathname although it was specified");
        exit(EXIT_FAILURE);
    }

    if(pidfile_pathname != NULL)
    {
        pidfile_fd = open_pidfile(pidfile_pathname);
        if(pidfile_fd == -1)
        {
            fprintf(stderr,
                    "fcached failed to open the pidfile or register the "
                    "remove_pidfile function with the atexit function: "
                    "pidfile_pathname = %s\n",
                    pidfile_pathname);
            perror("...the reason is");
            exit(EXIT_FAILURE);
        }
    }

    /* ...TODO make this process a daemon ... */

    if(pidfile_pathname != NULL)
    {
#if 0
        printf("WRITING PIDFILE: pidfile_fd = %d\n", pidfile_fd);
#endif
        ret = write_pidfile(pidfile_fd);
        if(ret == -1)
        {
            fprintf(stderr,
                    "fcached failed to write the pidfile or close it: "
                    "pidfile_pathname = %s\n",
                    pidfile_pathname);
            perror("Error: fcached failed to write the pidfile or close it");
            exit(EXIT_FAILURE);
        }
    }

    gossip_set_debug_mask(1, GOSSIP_FCACHE_DEBUG);
    gossip_debug(GOSSIP_FCACHE_DEBUG, "fcached starting...\n");


    /* TODO: cache initialization */
    sleep(20);


    gossip_debug(GOSSIP_FCACHE_DEBUG, "fcached started successfully.\n");

    /* TODO: remove this! */
    exit(EXIT_SUCCESS);
    return 0;
}
