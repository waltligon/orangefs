/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */




/*
 * This is an example of a server program that uses the BMI
 * library for communications
 */

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <time.h>

#include "bmi.h"
#include "gossip.h"
#include "test-bmi.h"
#include <src/common/misc/pvfs2-internal.h>  /* lld(), llu() */

/**************************************************************
 * Data structures
 */

#define SERVER          1
#define CLIENT          2
#define BYTES           (1<<10)
#define ITERATIONS      1000

#define RECV            0
#define SEND            1

#define KEY             0xdeadbeef

struct msg {
        int swap;
        int bytes;
        int iterations;
};

/* A little structure to hold program options, either defaults or
 * specified on the command line
 */
struct options
{
        char *hostid;           /* host identifier */
        char *method;
        int  which;
        int  bytes;
        int  iterations;
};


/**************************************************************
 * Internal utility functions
 */

static struct options *parse_args(int argc, char *argv[]);

static void print_usage(void)
{
        fprintf(stderr, "usage: pingpong -h HOST_URI -s|-c [-b BYTES] [-i ITERATIONS]\n");
        fprintf(stderr, "       where:\n");
        fprintf(stderr, "       HOST_URI is tcp://host:port, mx://host:board:endpoint, etc\n");
        fprintf(stderr, "       -s is server and -c is client\n");
        fprintf(stderr, "       BYTES per message [default 1024]\n");
        fprintf(stderr, "       ITERATIONS to send/receive [default 1000]\n");
        return;
}

/**************************************************************/

int main(int argc, char **argv)
{
        struct options                  *opts = NULL;
        int                             ret = -1;
        PVFS_BMI_addr_t                 peer_addr;
        void                            *recv_buffer = NULL;
        void                            *send_buffer = NULL;
        bmi_op_id_t                     op_id[2];
        bmi_error_code_t                error_code;
        int                             outcount = 0;
        struct BMI_unexpected_info      request_info;
        bmi_size_t                      actual_size;
        bmi_context_id                  context;
        struct msg                      *tx_msg  = NULL;
        struct msg                      *rx_msg  = NULL;
        int                             count   = 0;
        struct timeval                  start;
        struct timeval                  end;
        double                          duration = 0.0;

        /* grab any command line options */
        opts = parse_args(argc, argv);
        if (!opts) {
                print_usage();
                return (-1);
        }

        /* set debugging stuff */
        gossip_enable_stderr();
        gossip_set_debug_mask(0, GOSSIP_BMI_DEBUG_ALL);

        /* initialize local interface (default options) */
        if (opts->which == SERVER)
            ret = BMI_initialize(opts->method, opts->hostid, BMI_INIT_SERVER);
        else
            ret = BMI_initialize(opts->method, NULL, 0);

        if (ret < 0) {
                errno = -ret;
                perror("BMI_initialize");
                return (-1);
        }

        ret = BMI_open_context(&context);
        if (ret < 0) {
                errno = -ret;
                perror("BMI_open_context()");
                return (-1);
        }

        if (opts->which == SERVER) {
                int     iterations      = 0;
                int     msg_len         = 0;
                int     alloc_len       = 0;

                /* wait for an initial request to get size */
                do {
                        ret = BMI_testunexpected(1, &outcount, &request_info, 10);
                } while (ret == 0 && outcount == 0);

                if (ret < 0) {
                        fprintf(stderr, "Request recv failure (bad state).\n");
                        errno = -ret;
                        perror("BMI_testunexpected");
                        goto server_exit;
                }
                if (request_info.error_code != 0) {
                        fprintf(stderr, "Request recv failure (bad state).\n");
                        goto server_exit;
                }

                if (request_info.size != sizeof(struct msg)) {
                        fprintf(stderr, "Bad Request! Received %d bytes\n", 
                                        (int) request_info.size);
                        goto server_exit;
                }

                rx_msg = (struct msg *) request_info.buffer;
                opts->bytes = ntohl(rx_msg->bytes);
                iterations = ntohl(rx_msg->iterations);

                printf("Receiving %d messages of %d bytes\n", iterations, opts->bytes);

                peer_addr = request_info.addr;

                msg_len = sizeof(struct msg);
                alloc_len = opts->bytes;
                if (alloc_len < msg_len) alloc_len = msg_len;

                /* create an ack */
                send_buffer = BMI_memalloc(peer_addr, alloc_len, BMI_SEND);
                if (!send_buffer) {
                        fprintf(stderr, "BMI_memalloc failed.\n");
                        return (-1);
                }
                memset(send_buffer, 0, opts->bytes);

                tx_msg = (struct msg *) send_buffer;
                tx_msg->swap = htonl(KEY);
                tx_msg->bytes = htonl(opts->bytes);
                tx_msg->iterations = htonl(opts->iterations);

                /* create a buffer to recv into */
                recv_buffer = BMI_memalloc(peer_addr, alloc_len, BMI_RECV);
                if (!recv_buffer) {
                        fprintf(stderr, "BMI_memalloc failed.\n");
                        return (-1);
                }

                /* post the ack */
                ret = BMI_post_send(&(op_id[SEND]), peer_addr, tx_msg,
                                msg_len, BMI_PRE_ALLOC, 0, NULL,
                                context);
                if (ret < 0) {
                        fprintf(stderr, "BMI_post_send failure.\n");
                        return (-1);
                } else if (ret == 0) {
                        do {
                                ret = BMI_test(op_id[SEND], &outcount, &error_code,
                                           &actual_size, NULL, 10, context);
                        } while (ret == 0 && outcount == 0);

                        if (ret < 0 || error_code != 0) {
                                fprintf(stderr, "ack send failed.\n");
                                return (-1);
                        }

                        if (actual_size != (bmi_size_t) msg_len) {
                                fprintf(stderr, "Expected %d but received %llu\n",
                                                msg_len, llu(actual_size));
                        }
                }

                /* start iterations */
                while (count < iterations) {
                        count++;
                        /* receive the ping */
                        do {
                                ret = BMI_testunexpected(1, &outcount, &request_info, 10);
                        } while (ret == 0 && outcount == 0);

                        if (ret < 0) {
                                fprintf(stderr, "Request recv failure (bad state).\n");
                                errno = -ret;
                                perror("BMI_testunexpected");
                                goto server_exit;
                        }
                        if (request_info.error_code != 0) {
                                fprintf(stderr, "Request recv failure (bad state).\n");
                                goto server_exit;
                        }

                        if (request_info.size != opts->bytes) {
                                fprintf(stderr, "Expected %d but received %llu\n",
                                                opts->bytes, llu(actual_size));
                                return (-1);
                        }
                        /* send the pong */
                        ret = BMI_post_send(&(op_id[SEND]), peer_addr, send_buffer,
                                        opts->bytes, BMI_PRE_ALLOC, count, NULL, context);
                        if (ret < 0) {
                                fprintf(stderr, "BMI_post_send failure.\n");
                                return (-1);
                        } else if (ret == 0) {
                                do {
                                        ret = BMI_test(op_id[SEND], &outcount, &error_code,
                                                &actual_size, NULL, 10, context);
                                } while (ret == 0 && outcount == 0);

                                if (ret < 0 || error_code != 0) {
                                        fprintf(stderr, "ack send failed.\n");
                                        return (-1);
                                }
                                if (actual_size != opts->bytes) {
                                        fprintf(stderr, "Expected %d but received %llu\n",
                                                        opts->bytes, llu(actual_size));
                                        return (-1);
                                }
                        }
                }
        } else { /* CLIENT */
                int     msg_len         = 0;
                int     alloc_len       = 0;

                /* get a bmi_addr for the server */
                ret = BMI_addr_lookup(&peer_addr, opts->hostid);
                if (ret < 0) {
                        errno = -ret;
                        perror("BMI_addr_lookup");
                        return (-1);
                }

                msg_len = sizeof(struct msg);
                alloc_len = opts->bytes;
                if (alloc_len < msg_len) alloc_len = msg_len;

                /* create send buffer */
                send_buffer = BMI_memalloc(peer_addr, alloc_len, BMI_SEND);
                if (!send_buffer) {
                        fprintf(stderr, "BMI_memalloc failed.\n");
                        return (-1);
                }
                memset(send_buffer, 0, opts->bytes);

                tx_msg = (struct msg *) send_buffer;
                tx_msg->swap = htonl(KEY);
                tx_msg->bytes = htonl(opts->bytes);
                tx_msg->iterations = htonl(opts->iterations);

                /* create a buffer to recv into */
                recv_buffer = BMI_memalloc(peer_addr, alloc_len, BMI_RECV);
                if (!recv_buffer) {
                        fprintf(stderr, "BMI_memalloc failed.\n");
                        return (-1);
                }


                /* post the test parameters */
                ret = BMI_post_sendunexpected(&(op_id[SEND]), peer_addr, tx_msg,
                                msg_len, BMI_PRE_ALLOC, 0, NULL, context);
                if (ret < 0) {
                        fprintf(stderr, "BMI_post_send failure.\n");
                        return (-1);
                } else if (ret == 0) {
                        do {
                                ret = BMI_test(op_id[SEND], &outcount, &error_code,
                                                &actual_size, NULL, 10, context);
                        } while (ret == 0 && outcount == 0);

                        if (ret < 0 || error_code != 0) {
                                fprintf(stderr, "send ping failed.\n");
                                return (-1);
                        }
                        if (actual_size != msg_len) {
                                fprintf(stderr, "Expected %d but received %llu\n",
                                                msg_len, llu(actual_size));
                                return (-1);
                        }
                }

                /* post a recv for the ack */
                ret = BMI_post_recv(&(op_id[RECV]), peer_addr, recv_buffer,
                                msg_len, &actual_size, BMI_PRE_ALLOC, 0, NULL, context);
                if (ret < 0) {
                        fprintf(stderr, "BMI_post_recv_failure.\n");
                        return (-1);
                } else if (ret == 0) {
                        do {
                                ret = BMI_test(op_id[RECV], &outcount, &error_code,
                                        &actual_size, NULL, 10, context);
                        } while (ret == 0 && outcount == 0);

                        if (ret < 0 || error_code != 0) {
                                fprintf(stderr, "data recv failed.\n");
                                return (-1);
                        }
                        if (actual_size != msg_len) {
                                fprintf(stderr, "Expected %d but received %llu\n",
                                                msg_len, llu(actual_size));
                                return (-1);
                        }
                }

                /* make sure server has posted first recv */
                sleep(1);

                gettimeofday(&start, NULL);

                /* start iterations */
                while (count < opts->iterations) {
                        count++;
                        /* post the recv for the pong */
                        ret = BMI_post_recv(&(op_id[RECV]), peer_addr, recv_buffer,
                                        opts->bytes, &actual_size, BMI_PRE_ALLOC, count,
                                        NULL, context);

                        if (ret < 0) {
                                fprintf(stderr, "BMI_post_recv_failure.\n");
                                return (-1);
                        }

                        /* send the ping */
                        ret = BMI_post_sendunexpected(&(op_id[SEND]), peer_addr, send_buffer,
                                        opts->bytes, BMI_PRE_ALLOC, count, NULL, context);
                        if (ret < 0) {
                                fprintf(stderr, "BMI_post_send failure.\n");
                                return (-1);
                        } else if (ret == 0) {
                                do {
                                        ret = BMI_test(op_id[SEND], &outcount, &error_code,
                                                        &actual_size, NULL, 10, context);
                                } while (ret == 0 && outcount == 0);

                                if (ret < 0 || error_code != 0) {
                                        fprintf(stderr, "send ping failed.\n");
                                        return (-1);
                                }
                                if (actual_size != opts->bytes) {
                                        fprintf(stderr, "Expected %d but received %llu\n",
                                                        opts->bytes, llu(actual_size));
                                        return (-1);
                                }
                        }
                        /* complete the receive for the pong */
                        do {
                                ret = BMI_test(op_id[RECV], &outcount, &error_code,
                                                &actual_size, NULL, 10, context);
                        } while (ret == 0 && outcount == 0);

                        if (ret < 0 || error_code != 0) {
                                fprintf(stderr, "data recv failed.\n");
                                return (-1);
                        }
                        if (actual_size != opts->bytes) {
                                fprintf(stderr, "Expected %d but received %llu\n",
                                                opts->bytes, llu(actual_size));
                                return (-1);
                        }
                }
                gettimeofday(&end, NULL);

                duration = (double) end.tv_sec + ((double) end.tv_usec * 0.000001);
                duration -= (double) start.tv_sec + ((double) start.tv_usec * 0.0000001);

                fprintf(stdout, "Completed %d iterations of %d byte messages in "
                                "%.2f secs\n", count, opts->bytes, duration);
                fprintf(stdout, "one-way latency %.2f usecs  bi-dir throughput %.2f MB/s  "
                                "%.2f messages/sec\n", 
                                duration / (double) opts->iterations * 1000000.0 / 2.0,
                                (double) opts->bytes * (double) opts->iterations / 
                                duration / 1000000 * 2, (double) opts->iterations / duration);
        }

        /* free up the message buffers */
        BMI_memfree(peer_addr, recv_buffer, opts->bytes, BMI_RECV);
        BMI_memfree(peer_addr, send_buffer, opts->bytes, BMI_SEND);

server_exit:

        /* shutdown the local interface */
        BMI_close_context(context);
        ret = BMI_finalize();
        if (ret < 0) {
                errno = -ret;
                perror("BMI_finalize");
                return (-1);
        }

        /* turn off debugging stuff */
        gossip_disable();

        return (0);
}


static int check_uri(char *uri)
{
        int ret = 0; /* failure */
        if (uri[0] == ':' && uri[1] == '/' && uri[2] == '/') ret = 1;
        return ret;
}


static void get_method(struct options *opts)
{
        char *id = opts->hostid;

        if (id[0] == 't' && id[1] == 'c' && id[2] == 'p' && check_uri(&id[3])) {
                opts->method = strdup("bmi_tcp");
        } else if (id[0] == 'g' && id[1] == 'm' && check_uri(&id[2])) {
                opts->method = strdup("bmi_gm");
        } else if (id[0] == 'm' && id[1] == 'x' && check_uri(&id[2])) {
                opts->method = strdup("bmi_mx");
        } else if (id[0] == 'i' && id[1] == 'b' && check_uri(&id[2])) {
                opts->method = strdup("bmi_ib");
        }
        return;
}

static struct options *parse_args(int argc, char *argv[])
{

        /* getopt stuff */
        extern char *optarg;
        char flags[] = "h:scb:i:";
        int one_opt = 0;

        struct options *opts = NULL;

        /* create storage for the command line options */
        opts = (struct options *) calloc(1, sizeof(struct options));
        if (!opts) {
            goto parse_args_error;
        }

        /* look at command line arguments */
        while ((one_opt = getopt(argc, argv, flags)) != EOF) {
                switch (one_opt) {
                case ('h'):
                        opts->hostid = (char *) strdup(optarg);
                        if (opts->hostid == NULL) {
                	        goto parse_args_error;
                        }
                        get_method(opts);
                        break;
                case ('s'):
                        if (opts->which == CLIENT) {
                                fprintf(stderr, "use -s OR -c, not both\n");
                	        goto parse_args_error;
                        }
                        opts->which = SERVER;
                        break;
                case ('c'):
                        if (opts->which == SERVER) {
                                fprintf(stderr, "use -s OR -c, not both\n");
                	        goto parse_args_error;
                        }
                        opts->which = CLIENT;
                        break;
                case ('b'):
                        opts->bytes = (int) strtol(optarg, NULL, 0);
                        break;
                case ('i'):
                        opts->iterations = (int) strtol(optarg, NULL, 0);
                        break;
                default:
                        break;
                }
        }

        /* if we didn't get a host argument, bail: */
        if (opts->hostid == NULL) {
                fprintf(stderr, "you must specify -h\n");
                goto parse_args_error;
        }
        if (opts->method == NULL) {
                fprintf(stderr, "you must use a valid HOST_URI\n");
                goto parse_args_error;
        }
        if (opts->which == 0) {
                fprintf(stderr, "you must specify -s OR -c\n");
                goto parse_args_error;
        }
        if (opts->bytes == 0) {
                opts->bytes = BYTES;
        }
        if (opts->iterations == 0) {
                opts->iterations = ITERATIONS;
        }

        return (opts);

parse_args_error:

        /* if an error occurs, just free everything and return NULL */
        if (opts) {
                if (opts->hostid) {
                        free(opts->hostid);
                }
                free(opts);
        }
        return (NULL);
}

/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */
