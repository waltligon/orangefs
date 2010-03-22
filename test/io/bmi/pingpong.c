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
#include <math.h>

#include "pvfs2.h"
#include "bmi.h"
#include "gossip.h"
#include "test-bmi.h"
#include <src/common/misc/pvfs2-internal.h>  /* lld(), llu() */

/**************************************************************
 * Data structures 
 */

#define SERVER          1
#define CLIENT          2
#define MIN_BYTES       1
#define MAX_BYTES       (4<<20)

#define RECV            0
#define SEND            1

#define EXPECTED        0
#define UNEXPECTED      1

#define ITERATIONS      10000

struct msg {
        int test;
};

/* A little structure to hold program options, either defaults or
 * specified on the command line 
 */
struct options
{
        char *hostid;           /* host identifier */
        char *method;
        int  which;
        int  test;
};


/**************************************************************
 * Internal utility functions
 */

static struct options *parse_args(int argc, char *argv[]);
static int do_server(struct options *opts, bmi_context_id *context);
static int do_client(struct options *opts, bmi_context_id *context);

static void print_usage(void)
{
        fprintf(stderr, "usage: pingpong -h HOST_URI -s|-c [-u]\n");
        fprintf(stderr, "       where:\n");
        fprintf(stderr, "       HOST_URI is tcp://host:port, mx://host:board:endpoint, etc\n");
        fprintf(stderr, "       -s is server and -c is client\n");
        fprintf(stderr, "       -u will use unexpected messages (pass to client only)\n");
        return;
}

/**************************************************************/

int main(int argc, char **argv)
{
        struct options                  *opts = NULL;
        int                             ret = -1;
        bmi_context_id                  context;

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
            ret = BMI_initialize(NULL, NULL, 0);

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
                ret = do_server(opts, &context);
        } else {
                ret = do_client(opts, &context);
        }

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

static int bytes_to_iterations(int bytes)
{
        int     ret     = ITERATIONS;
        if (bytes >= (128*1024)) ret = 5000;
        if (bytes >= (256*1024)) ret = 2500;
        if (bytes >= (1024*1024)) ret = 1000;
        if (bytes >= (2*1024*1024)) ret = 500;
        if (bytes >= (4*1024*1024)) ret = 250;
        return ret;
}

static int do_server(struct options *opts, bmi_context_id *context)
{
        int                             ret = 0;
        int                             i = 0;
        PVFS_BMI_addr_t                 peer_addr;
        PVFS_BMI_addr_t                 server_addr;
        void                            *recv_buffer = NULL;
        void                            *send_buffer = NULL;
        bmi_op_id_t                     op_id[2];
        bmi_error_code_t                error_code;
        int                             outcount = 0;
        struct BMI_unexpected_info      request_info;
        bmi_size_t                      actual_size;
        struct msg                      *tx_msg  = NULL;
        struct msg                      *rx_msg  = NULL;
        int                             bytes   = MIN_BYTES;
        int                             max_bytes       = MAX_BYTES;
        int                             warmup  = 1;
        int                             iterations      = 0;
        int                             msg_len         = 0;
        int                             run     = 0;

        /* wait for an initial request to get size */
        do {
                ret = BMI_testunexpected(1, &outcount, &request_info, 10);
        } while (ret == 0 && outcount == 0);

        if (ret < 0) {
                fprintf(stderr, "Request recv failure (bad state).\n");
                errno = -ret;
                perror("BMI_testunexpected");
                return ret;
        }
        if (request_info.error_code != 0) {
                fprintf(stderr, "Request recv failure (bad state).\n");
                return ret;
        }
        
        if (request_info.size != sizeof(struct msg)) {
                fprintf(stderr, "Bad Request! Received %d bytes\n", 
                                (int) request_info.size);
                return ret;
        }

        rx_msg = (struct msg *) request_info.buffer;
        opts->test = ntohl(rx_msg->test);

        printf("Starting %s test\n", opts->test == EXPECTED ? "expected" : "unexpected");

        peer_addr = request_info.addr;

        BMI_unexpected_free(peer_addr, request_info.buffer);

        ret = BMI_get_info(server_addr, BMI_CHECK_MAXSIZE,
                           (void *)&max_bytes);
        if (ret < 0) {
                fprintf(stderr, "BMI_get_info() returned %d\n", ret);
                return ret;
        }
        if (max_bytes > MAX_BYTES) max_bytes = MAX_BYTES;

        if (opts->test == UNEXPECTED) {
                ret = BMI_addr_lookup(&server_addr, opts->hostid);
                if (ret < 0) {
                        errno = -ret;
                        perror("BMI_addr_lookup");
                        return (-1);
                }
                ret = BMI_get_info(server_addr, BMI_GET_UNEXP_SIZE,
                                   (void *)&max_bytes);
                if (ret < 0) {
                        fprintf(stderr, "BMI_get_info() returned %d\n", ret);
                        return ret;
                }
        } else {
                int     maxsize = 0;
                ret = BMI_get_info(server_addr, BMI_CHECK_MAXSIZE,
                                (void *)&maxsize);
                if (ret < 0) {
                        fprintf(stderr, "BMI_get_info() returned %d\n", ret);
                        return ret;
                }
                if (maxsize < max_bytes) max_bytes = maxsize;
        }

        msg_len = sizeof(struct msg);
    
        /* create an ack */
        send_buffer = BMI_memalloc(peer_addr, max_bytes, BMI_SEND);
        if (!send_buffer) {
                fprintf(stderr, "BMI_memalloc failed.\n");
                return (-1);
        }
        memset(send_buffer, 0, max_bytes);

        tx_msg = (struct msg *) send_buffer;
        tx_msg->test = htonl(opts->test);
    
        /* create a buffer to recv into */
        recv_buffer = BMI_memalloc(peer_addr, max_bytes, BMI_RECV);
        if (!recv_buffer) {
                fprintf(stderr, "BMI_memalloc failed.\n");
                return (-1);
        }

        /* post the ack */
        ret = BMI_post_send(&(op_id[SEND]), peer_addr, tx_msg,
                        msg_len, BMI_PRE_ALLOC, 0, NULL,
                        *context, NULL);
        if (ret < 0) {
                fprintf(stderr, "BMI_post_send failure.\n");
                return (-1);
        } else if (ret == 0) {
                do {
                        ret = BMI_test(op_id[SEND], &outcount, &error_code,
                                   &actual_size, NULL, 10, *context);
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
        while (bytes <= max_bytes) {
                iterations = bytes_to_iterations(bytes);

                for (i=0; i < iterations; i++) {
                        /* receive the ping */
                        if (opts->test == EXPECTED) {
                                ret = BMI_post_recv(&(op_id[RECV]), peer_addr, recv_buffer,
                                                bytes, &actual_size, BMI_PRE_ALLOC, i, NULL,
                                                *context, NULL);
                    
                                if (ret < 0) {
                                        fprintf(stderr, "BMI_post_recv_failure.\n");
                                        return (-1);
                                } else if (ret == 0) {
                                        do {
                                                ret = BMI_test(op_id[RECV], &outcount, &error_code,
                                                        &actual_size, NULL, 10, *context);
                                        } while (ret == 0 && outcount == 0);
                
                                        if (ret < 0 || error_code != 0) {
                                                fprintf(stderr, "data recv failed.\n");
                                                return (-1);
                                        }
                                        if (actual_size != bytes) {
                                                fprintf(stderr, "Expected %d but received %llu\n",
                                                                bytes, llu(actual_size));
                                                return (-1);
                                        }
                                }
                        } else { /* UNEXPECTED */
                                do {
                                        ret = BMI_testunexpected(1, &outcount, &request_info, 10);
                                } while (ret == 0 && outcount == 0);
                        
                                if (ret < 0) {
                                        fprintf(stderr, "Request recv failure (bad state).\n");
                                        errno = -ret;
                                        perror("BMI_testunexpected");
                                        return ret;
                                }
                                if (request_info.error_code != 0) {
                                        fprintf(stderr, "Request recv failure (bad state).\n");
                                        return ret;
                                }
                                
                                if (request_info.size != bytes) {
                                        fprintf(stderr, "Bad Request! Received %d bytes\n", 
                                                        (int) request_info.size);
                                        return ret;
                                }
                        }
                        /* send the pong */
                        ret = BMI_post_send(&(op_id[SEND]), peer_addr, send_buffer,
                                        bytes, BMI_PRE_ALLOC, i, NULL, *context, NULL);
                        if (ret < 0) {
                                fprintf(stderr, "BMI_post_send failure.\n");
                                return (-1);
                        } else if (ret == 0) {
                                do {
                                        ret = BMI_test(op_id[SEND], &outcount, &error_code,
                                                &actual_size, NULL, 10, *context);
                                } while (ret == 0 && outcount == 0);
        
                                if (ret < 0 || error_code != 0) {
                                        fprintf(stderr, "ack send failed.\n");
                                        return (-1);
                                }
                                if (actual_size != bytes) {
                                        fprintf(stderr, "Expected %d but received %llu\n",
                                                        bytes, llu(actual_size));
                                        return (-1);
                                }
                        }
                }
                if (!warmup) {
                        bytes *= 2;
                        run++;
                }
                else warmup = 0;
        }

        /* free up the message buffers */
        BMI_memfree(peer_addr, recv_buffer, max_bytes, BMI_RECV);
        BMI_memfree(peer_addr, send_buffer, max_bytes, BMI_SEND);

        return ret;
}

static int do_client(struct options *opts, bmi_context_id *context)
{
        int                     ret             = 0;
        int                     i               = 0;
        PVFS_BMI_addr_t         peer_addr;
        void                    *recv_buffer    = NULL;
        void                    *send_buffer    = NULL;
        bmi_op_id_t             op_id[2];
        bmi_error_code_t        error_code;
        int                     outcount        = 0;
        bmi_size_t              actual_size;
        struct msg              *tx_msg         = NULL;
        int                     bytes           = MIN_BYTES;
        int                     max_bytes       = MAX_BYTES;
        int                     warmup          = 1;
        int                     iterations      = 0;
        int                     msg_len         = 0;
        int                     run             = 0;
        struct timeval          start;
        struct timeval          end;
        double                  *val            = NULL;
        double                  lat             = 0.0;
        double                  min             = 99999.9;
        double                  max             = 0.0;
        double                  avg             = 0.0;

        /* get a bmi_addr for the server */
        ret = BMI_addr_lookup(&peer_addr, opts->hostid);
        if (ret < 0) {
                errno = -ret;
                perror("BMI_addr_lookup");
                return (-1);
        }

        if (opts->test == UNEXPECTED) {
                ret = BMI_get_info(peer_addr, BMI_GET_UNEXP_SIZE,
                                   (void *)&max_bytes);
                if (ret < 0) {
                        fprintf(stderr, "BMI_get_info() returned %d\n", ret);
                        return ret;
                }
        } else {
                int     maxsize = 0;
                ret = BMI_get_info(peer_addr, BMI_CHECK_MAXSIZE,
                                (void *)&maxsize);
                if (ret < 0) {
                        fprintf(stderr, "BMI_get_info() returned %d\n", ret);
                        return ret;
                }
                if (maxsize < max_bytes) max_bytes = maxsize;
        }

        msg_len = sizeof(struct msg);

        /* create send buffer */
        send_buffer = BMI_memalloc(peer_addr, max_bytes, BMI_SEND);
        if (!send_buffer) {
                fprintf(stderr, "BMI_memalloc failed.\n");
                return (-1);
        }
        memset(send_buffer, 0, max_bytes);

        tx_msg = (struct msg *) send_buffer;
        tx_msg->test = htonl(opts->test);
    
        /* create a buffer to recv into */
        recv_buffer = BMI_memalloc(peer_addr, max_bytes, BMI_RECV);
        if (!recv_buffer) {
                fprintf(stderr, "BMI_memalloc failed.\n");
                return (-1);
        }

        /* post the test parameters */
        ret = BMI_post_sendunexpected(&(op_id[SEND]), peer_addr, tx_msg,
                        msg_len, BMI_PRE_ALLOC, 0, NULL, *context, NULL);
        if (ret < 0) {
                fprintf(stderr, "BMI_post_sendunexpected failure.\n");
                return (-1);
        } else if (ret == 0) {
                do {
                        ret = BMI_test(op_id[SEND], &outcount, &error_code,
                                &actual_size, NULL, 10, *context);
                } while (ret == 0 && outcount == 0);
                if (ret < 0 || error_code != 0) {
                        fprintf(stderr, "data send failed.\n");
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
                        msg_len, &actual_size, BMI_PRE_ALLOC, 0, NULL, *context, NULL);
        if (ret < 0) {
                fprintf(stderr, "BMI_post_recv_failure.\n");
                return (-1);
        } else if (ret == 0) {
                do {
                        ret = BMI_test(op_id[RECV], &outcount, &error_code,
                                &actual_size, NULL, 10, *context);
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

        val = calloc(ITERATIONS, sizeof(double));
        if (val == NULL) {
                fprintf(stderr, "calloc() for val failed\n");
                return -1;
        }

        /* make sure server has posted first recv */
        sleep(1);

        fprintf(stdout, "     Bytes        usecs         MB/s       StdDev          Min          Max\n");

        /* start iterations */
        while (bytes <= max_bytes) {

                iterations = bytes_to_iterations(bytes);

                for (i=0; i < iterations; i++) {

                        gettimeofday(&start, NULL);

                        /* post the recv for the pong */
                        ret = BMI_post_recv(&(op_id[RECV]), peer_addr, recv_buffer,
                                        bytes, &actual_size, BMI_PRE_ALLOC, i,
                                        NULL, *context, NULL);
            
                        if (ret < 0) {
                                fprintf(stderr, "BMI_post_recv_failure.\n");
                                return (-1);
                        }
        
                        /* send the ping */
                        if (opts->test == EXPECTED) {
                                ret = BMI_post_send(&(op_id[SEND]), peer_addr, send_buffer,
                                                bytes, BMI_PRE_ALLOC, i, NULL, *context, NULL);
                        } else {
                                ret = BMI_post_sendunexpected(&(op_id[SEND]), peer_addr, 
                                                send_buffer, bytes, BMI_PRE_ALLOC, i, 
                                                NULL, *context, NULL);
                        }
                        if (ret < 0) {
                                fprintf(stderr, "BMI_post_sendunexpected failure.\n");
                                return (-1);
                        } else if (ret == 0) {
                                do {
                                        ret = BMI_test(op_id[SEND], &outcount, &error_code,
                                                        &actual_size, NULL, 10, *context);
                                } while (ret == 0 && outcount == 0);
                        
                                if (ret < 0 || error_code != 0) {
                                        fprintf(stderr, "send ping failed.\n");
                                        return (-1);
                                }
                                if (actual_size != bytes) {
                                        fprintf(stderr, "Expected %d but received %llu\n",
                                                        bytes, llu(actual_size));
                                        return (-1);
                                }
                        }
                        /* complete the receive for the pong */
                        do {
                                ret = BMI_test(op_id[RECV], &outcount, &error_code,
                                                &actual_size, NULL, 10, *context);
                        } while (ret == 0 && outcount == 0);
        
                        if (ret < 0 || error_code != 0) {
                                fprintf(stderr, "data recv failed.\n");
                                return (-1);
                        }
                        if (actual_size != bytes) {
                                fprintf(stderr, "Expected %d but received %llu\n",
                                                bytes, llu(actual_size));
                                return (-1);
                        }

                        gettimeofday(&end, NULL);

                        if (!warmup) {
                                val[i] =  (double) end.tv_sec + 
                                          (double) end.tv_usec * 0.000001;
                                val[i] -= (double) start.tv_sec + 
                                          (double) start.tv_usec * 0.000001;
                                lat += val[i];
                        }
                }
                if (!warmup) {
                        double stdev    = 0.0;

                        lat = lat / (double) iterations * 1000000.0 / 2.0;
                        min = 999999.9;
                        max = 0.0;
                        avg = 0.0;

                        /* convert seconds to MB/s */
                        for (i=0; i < iterations; i++) {
                                val[i] = (double) bytes * 2 / val[i] / 1000000.0;
                                avg += val[i];
                                if (val[i] < min) min = val[i];
                                if (val[i] > max) max = val[i];
                        }
                        avg /= iterations;

                        if (iterations > 1) {
                                for (i=0; i < iterations; i++) {
                                        double diff = val[i] - avg;
                                        stdev += diff * diff;
                                }
                                stdev = sqrt(stdev / (iterations - 1));
                        }

                        fprintf(stdout, "%10d %12.3f %12.3f +- %9.3f %12.3f %12.3f\n", bytes, lat, avg, stdev, min, max);

                        lat = 0.0;
                        bytes *= 2;
                        run++;
                } else warmup = 0;
        }

        /* free up the message buffers */
        BMI_memfree(peer_addr, recv_buffer, max_bytes, BMI_RECV);
        BMI_memfree(peer_addr, send_buffer, max_bytes, BMI_SEND);

        return ret;
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
        char flags[] = "h:scu";
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
                case ('u'):
                        opts->test = UNEXPECTED;
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
