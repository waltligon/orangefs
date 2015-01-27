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

#include <gossip.h>
#include <pvfs2-debug.h>
#include "fcached.h"

static void cleanup(void)
{
    gossip_debug(GOSSIP_FCACHE_DEBUG, "fcached cleanup starting...\n");
    /* Try flushing any dirty cached pages to OrangeFS */


    /* TODO */


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
    int ret = 0;
    unsigned char daemonize = 1;
    char * fcached_logfile = NULL;
    char * fcached_pidfile = NULL;

    atexit(cleanup);

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
                /* TODO */
                break;
            case 'd':
                printf("option -d\n");
                break;
            case 'h':
                printf("option -h\n");
                break;
            case 'l':
                printf("option -l with value '%s'\n", optarg);
                fcached_logfile = optarg;
                break;
            case 'p':
                printf("option -p with value '%s'\n", optarg);
                fcached_pidfile = optarg;
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

    /* TODO: Run in foreground? */

    /* user provided fcached_logfile path */
    if(fcached_logfile == NULL)
    {
        fcached_logfile = getenv(FCACHE_LOGFILE_ENV_VAR);
    }

    /* user provided fcached_pidfile path */
    if(fcached_pidfile == NULL)
    {
        fcached_pidfile = getenv(FCACHE_PIDFILE_ENV_VAR);
    }


    if(fcached_logfile != NULL)
    {
        gossip_enable_file(fcached_logfile, "a");
    }
    else
    {
        gossip_enable_file(FCACHE_LOGFILE_DEFAULT, "a");
    }

    gossip_set_debug_mask(1, GOSSIP_FCACHE_DEBUG);
    gossip_debug(GOSSIP_FCACHE_DEBUG, "fcached starting...\n");


    /* TODO: cache initialization */


    gossip_debug(GOSSIP_FCACHE_DEBUG, "fcached started successfully.\n");

    /* TODO: remove this and instead live forever!!! */
    exit(EXIT_SUCCESS);
    return 0;
}
