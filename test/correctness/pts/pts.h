#ifndef INCLUDE_PTS_H
#define INCLUDE_PTS_H

#include <mpi.h>
#include <generic.h>

#define MAX_DISTINCT_TESTS 256
#define MAX_RUN_TESTS 8192

/* pts.c structs */
typedef struct pts_test_t {
  void *test_func;
  void *test_param_init;
  char *test_name;
  char *test_params;
  int test_index;
} pts_test;

typedef struct config_t {
  int debug;
  char *configfile;
  pts_test testpool[MAX_DISTINCT_TESTS];
  pts_test testqueue[MAX_RUN_TESTS];
  int myid;
  int numprocs;
} config;



/* pts.c prototypes */
int init_config(config *);
int parse_cmdline(int, char **, config *);
int parse_configfile(config *);
void usage(void);
void pts_debug(char *, ...);
int run_tests(config *);
int run_test(int(*test)(void *, int, void *, void *), MPI_Comm *, int,
	     char *, void *);
void *run_param(void *(*param)(void *), char *);
char *str_malloc(const char *);

int sync_config(config *myconfig);
int lookup_test(config *myconfig, char *test_name);

#endif
