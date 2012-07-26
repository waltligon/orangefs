#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdarg.h>
#include <unistd.h>
#include <getopt.h>
#include <mpi.h>
#include "pts.h"
#include <generic.h>
#include "gossip.h"

/*pvfs2 functions we're calling (mostly gossip args)*/
#include "pvfs2-debug.h"
#include "pint-util.h"

/* this is where all of the individual test prototypes are */
#include "test-protos.h"

/* yeah yeah, only one global...stores all config data (debug flag, mpi_id, etc */
static config pts_config;

int main(int argc, char **argv) {
  
  int numprocs, myid, rc, status;
  PINT_time_marker marker1, marker2;
  double wtime, stime, utime;

  MPI_Init(&argc,&argv);
  MPI_Comm_size(MPI_COMM_WORLD, &numprocs);
  MPI_Comm_rank(MPI_COMM_WORLD, &myid);
  
  /* initialize the config structure, this initializes most global config info, and sets up the array of test function pointers that we can draw from later when setting up the testqueue[] */
  rc = init_config(&pts_config);
  if (rc) {
    MPI_Finalize();
    exit(1);
  }
  
  /* status variable, used to communicate exit or not, 0 means all is well*/
  status = 0;
  
  /* process 0 stuff */
  if (myid == 0) {
    /* parse cmdline, should prolly use getopt ... */
    rc = parse_cmdline(argc, argv, &pts_config);
    if (rc) {
      status = 1;
    }
    
    /* parse config file, set up pts_config.testqueue[] pretty much */
    rc = parse_configfile(&pts_config);
    if (rc) {
      status = 1;
    }
  }


  /* check to make sure all is still OK to continue */
  MPI_Bcast(&status, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if (status > 0) {
    printf("%d: error status recieved, exiting\n", myid);
    MPI_Finalize();
    exit(1);
  }
  
  /*  printf("%d: here\n", myid); */
  pts_config.myid = myid;
  pts_config.numprocs = numprocs;
  rc = sync_config(&pts_config);
  
  /* get the config out to everybody ... don't know how to bcast a whole
     struct */
  MPI_Bcast(&pts_config.debug, 1, MPI_INT, 0, MPI_COMM_WORLD);
  pts_config.myid = myid;
  
  /* need to learn how to dup communicators ... should do that here*/
  
  PINT_time_mark(&marker1);
  rc = run_tests(&pts_config);
  PINT_time_mark(&marker2);
  
  fprintf(stderr, "%d: DONE RUNNING TESTS!\n", myid);
  /*   MPI_Barrier(MPI_COMM_WORLD); */
  if(numprocs == 1)
  {
    PINT_time_diff(marker1, marker2, &wtime, &utime, &stime);
    fprintf(stderr, "Elapsed time:\n");
    fprintf(stderr, "----------------------\n");
    fprintf(stderr, "   wall: %f\n", wtime);
    fprintf(stderr, "   user: %f\n", utime);
    fprintf(stderr, "   system: %f\n", stime);
  }
  MPI_Finalize();

  exit(0);
}


/* gets the data from proc 0's testqueue array out to all nodes, kinda messy */
int sync_config(config *myconfig) {
  int i, pindex, myid;
  char *rawparams = NULL;
  
  myid = myconfig->myid;
  
  for (i=0; i<MAX_RUN_TESTS; i++) {
    MPI_Barrier(MPI_COMM_WORLD);
    
    /* proc 0 gets info from queue about which test from test pool is queued*/
    if (myid == 0) {
      if ( pts_config.testqueue[i].test_func != NULL) {	
	pindex = pts_config.testqueue[i].test_index;
	rawparams = pts_config.testqueue[i].test_params;
      } else {
	/* if we've reached the last test, set pindex = -1 indicating stop */
	pindex = -1;
      }
    } else {
      /* all non proc 0 nodes gobble up some space */
      rawparams = malloc(4096);
      memset(rawparams, 0, 4096);
    }

    /* bcast send out the new pindex */
    MPI_Bcast(&pindex, 1, MPI_INT, 0, MPI_COMM_WORLD);

    if (pindex == -1) {
      /* no more test, set i to boundry */
      i = MAX_RUN_TESTS;
    } else {
      /* broadcast the rawparams assiciated with this specific test */
      MPI_Bcast(rawparams, 4096, MPI_CHAR, 0, MPI_COMM_WORLD);
      
      if (myid != 0) {
	/* all non proc 0 nodes now have enough info to add a test to their
	   queues */
	pts_config.testqueue[i] = pts_config.testpool[pindex];
	pts_config.testqueue[i].test_params = rawparams;
      }
    }
  }

  return(0);
}

int run_tests(config *myconfig) {
  int index, rc, errcode, *allerrcodes, i;
  char *buf = malloc(4096);
  void *params = NULL;
  MPI_Comm newcomm;

  MPI_Comm_dup(MPI_COMM_WORLD, &newcomm);

  index = 0;
  while(myconfig->testqueue[index].test_func != NULL) {
    MPI_Barrier(MPI_COMM_WORLD);
    
    if (myconfig->myid == 0) {
      fprintf(stderr, "PTS: Running test %s\n",
	      myconfig->testqueue[index].test_name);
      fprintf(stderr, "PTS: -------------------------------------------\n");
    }
    if (myconfig->testqueue[index].test_param_init != NULL) {
      params = run_param(myconfig->testqueue[index].test_param_init, myconfig->testqueue[index].test_params);
      if (params == NULL) {
	printf("ERROR: cannot setup params\n");
      }
    }

    if (myconfig->testqueue[index].test_func != NULL) {

      rc = run_test(myconfig->testqueue[index].test_func, &newcomm,
		    myconfig->myid, buf, params);
      free(params);
    } else {
      fprintf(stderr, "PTS: test %s not set up properly, no function to run\n",
	      myconfig->testqueue[index].test_name);
      rc = 1;
    }
    
    errcode = rc;
    fflush(stdout);
    MPI_Barrier(MPI_COMM_WORLD);

    allerrcodes = malloc(sizeof(int[myconfig->numprocs]));
    memset(allerrcodes, 0, sizeof(int[myconfig->numprocs]));
    MPI_Gather(&errcode, 1, MPI_INT, allerrcodes, 1, MPI_INT, 0, MPI_COMM_WORLD);

    if (myconfig->myid == 0) {
      for (i=0; i<myconfig->numprocs; i++) {
	if (allerrcodes[i]) {
	  fprintf(stderr, "PTS: node %d, test %s -> FAILED\n", i, myconfig->testqueue[index].test_name);
	} else {
	  fprintf(stderr, "PTS: node %d, test %s -> PASSED\n", i, myconfig->testqueue[index].test_name);
	}
      }
    }
    free(allerrcodes);

    if (myconfig->myid == 0) {
      fprintf(stderr, "\n\n");
    }
    index++;
    memset(buf, 0, 4096);
  }
  free(buf);
  
  MPI_Comm_free(&newcomm);
  return(0);
}

int run_test(int(*test)(MPI_Comm *comm, int rank, char *buf, void *params),
  MPI_Comm *mycomm, int myid, char *buf, void *params) {
  return(test(mycomm, myid, buf, params));
}

void *run_param(void *(*param)(char *), char *buf) {
  return(param(buf));
}

char *str_malloc(const char *instr) {
  char *tmpstr = malloc(strlen(instr)+1);
  strncpy(tmpstr, instr, strlen(instr)+1);
  return(tmpstr);
}

int init_config(config *myconfig) {
  int i;

  memset(myconfig, 0, sizeof(*myconfig));

  myconfig->debug = 0;
  myconfig->configfile = NULL;

  /* the test_param_init should point to generic_param_init */
  for (i=0; i<MAX_DISTINCT_TESTS; i++) {
    myconfig->testpool[i].test_func = NULL;
    myconfig->testpool[i].test_param_init = generic_param_parser;
    myconfig->testpool[i].test_name = NULL;
    myconfig->testpool[i].test_params = NULL;
    myconfig->testpool[i].test_index = i;
  }

  for (i=0; i<MAX_RUN_TESTS; i++) {
    myconfig->testqueue[i].test_func = NULL;
    myconfig->testqueue[i].test_param_init = NULL;
    myconfig->testqueue[i].test_name = NULL;
    myconfig->testqueue[i].test_params = NULL;
    myconfig->testqueue[i].test_index = 0;
  }

  /* set up function vector to point at all tests in testpool*/
  setup_ptstests(myconfig);

  return(0);
}

int parse_cmdline(int argc, char **argv, config *myconfig) {
  int c, index;

  static struct option long_opts[] = {
	  { "debug", required_argument, NULL, 'd' },
	  { "conf", required_argument, NULL, 'c' },
	  { 0, 0, 0, 0}
  };
  while ( ( c = getopt_long(argc, argv, "d:c:",
				  long_opts, &index)) != -1 ) {
	  switch(c) {
		  case 'd':
			  myconfig->debug = (int)strtoul(optarg, NULL, 0);
			  break;
		  case 'c':
			  myconfig->configfile = str_malloc(optarg);
			  break;
		  case '?':
		  case ':':
		  default:
			  usage();
			  return 1;
	  }

  }
#if 0
  if ( optind == argc ) {
	  usage();
	  return 1;
  }
#endif

  return 0;
}

int parse_configfile(config *myconfig) {
  int qindex, pindex;
  char *linebuf = malloc(4096);
  char *strindex, *rawparams;
  FILE *fh;

  fh = fopen(myconfig->configfile, "r");
  if (fh == NULL) {
    return(1);
  }

  qindex = 0;
  while(fgets(linebuf, 4096, fh) != NULL) {
    if (linebuf[0] != '#') {
      strindex = strchr(linebuf, ':');
      if (strindex != NULL) {
	*strindex = '\0';
	pindex = lookup_test(myconfig, linebuf);
	/*	printf("pindex %d\n", pindex); */
	strindex++;
	rawparams = (char *)strdup(strindex);
	
	if (pindex < 0) {
	  fprintf(stderr, "parse error, test '%s' not found internally\n", linebuf);
	} else {
	  myconfig->testqueue[qindex] = myconfig->testpool[pindex];
	  myconfig->testqueue[qindex].test_params = rawparams;
	  qindex++;
	}

      }
    }
  }
  fclose(fh);
  free(linebuf);

  return(0);
}

int lookup_test(config *myconfig, char *test_name) {
  int i;
  for (i=0; i<MAX_DISTINCT_TESTS; i++) {
    if (myconfig->testpool[i].test_name != NULL) {
      if (!strcmp(test_name, myconfig->testpool[i].test_name)) {
	return(i);
      }
    }
  }
  return(-1);
}

void usage(void) {
  printf("USAGE: pts <options>\nOPTIONS:\n\t--debug <debug_level>\n\t--conf </path/to/configfile>\n");
}

void pts_debug(char *format_str, ...) {
  if (pts_config.debug) {
    va_list arguments;
    va_start(arguments, format_str);

    vprintf(format_str, arguments);
    
    va_end(arguments);
  }
}

