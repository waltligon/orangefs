#ifndef INCLUDE_TESTPROTOS_H
#define INCLUDE_TESTPROTOS_H

#include <stdio.h>
#include <pts.h>
#include <generic.h>

/* include all test specific header files */
#include <test-create.h>
#include <test-dir-torture.h>
#include <test-dir-operations.h>
#include <test-lookup-bench.h>
#include <test-pvfs-datatype-init.h>
#include <test-pvfs-datatype-contig.h>
#include <test-pvfs-datatype-vector.h>
#include <test-pvfs-datatype-hvector.h>
#include <test-null-params.h>
#include <pvfs-restart-server.h>
#include <pvfs-stop-server.h>
#include <null_params.h>
#include <test-invlaid-files.h>

enum test_types { 
   TEST_CREATE,
   TEST_PVFSDATATYPE_INIT,
   TEST_PVFSDATATYPE_CONTIG,
   TEST_PVFSDATATYPE_VECTOR,
   TEST_PVFSDATATYPE_HVECTOR,
	TEST_DIR_TORTURE,
	TEST_DIR_OPERATIONS,
	TEST_LOOKUP_BENCH,
   TEST_NULL_PARAMS,
	PVFS_RESTART_SERVER,
	PVFS_STOP_SERVER
};

void setup_ptstests(config *myconfig) {

   /* 
     example test setup, must define the three following values (pointers):
     1.) test_func, must be setwhich is a function that does all of the actual work
     2.) test_param_init, optionally set function that parses arguments on --long
     format
     3.) test_name, must be set to a distinct test name string
   */
   myconfig->testpool[TEST_CREATE].test_func = (void *)test_create;
   myconfig->testpool[TEST_CREATE].test_name = str_malloc("test_create");
   myconfig->testpool[TEST_PVFSDATATYPE_INIT].test_func = (void *)test_pvfs_datatype_init;
   myconfig->testpool[TEST_PVFSDATATYPE_INIT].test_name = str_malloc("test_pvfs_datatype_init");

   myconfig->testpool[TEST_PVFSDATATYPE_CONTIG].test_func = (void *)test_pvfs_datatype_contig;
   myconfig->testpool[TEST_PVFSDATATYPE_CONTIG].test_name = str_malloc("test_pvfs_datatype_contig");

   myconfig->testpool[TEST_PVFSDATATYPE_VECTOR].test_func = (void *)test_pvfs_datatype_vector;
   myconfig->testpool[TEST_PVFSDATATYPE_VECTOR].test_name = str_malloc("test_pvfs_datatype_vector");

   myconfig->testpool[TEST_PVFSDATATYPE_HVECTOR].test_func = (void *)test_pvfs_datatype_hvector;
   myconfig->testpool[TEST_PVFSDATATYPE_HVECTOR].test_name = str_malloc("test_pvfs_datatype_hvector");
   myconfig->testpool[TEST_DIR_TORTURE].test_func = (void *)test_dir_torture;
   myconfig->testpool[TEST_DIR_TORTURE].test_name = str_malloc("test_dir_torture");
   myconfig->testpool[TEST_DIR_OPERATIONS].test_func = (void *)test_dir_operations;
   myconfig->testpool[TEST_DIR_OPERATIONS].test_name = str_malloc("test_dir_operations");

   myconfig->testpool[TEST_LOOKUP_BENCH].test_func = (void *)test_lookup_bench;
   myconfig->testpool[TEST_LOOKUP_BENCH].test_name = str_malloc("test_lookup_bench");
   myconfig->testpool[TEST_NULL_PARAMS].test_func = (void *)test_null_params;
   myconfig->testpool[TEST_NULL_PARAMS].test_param_init = (void *)null_params_parser;
   myconfig->testpool[TEST_NULL_PARAMS].test_name = str_malloc("test_null_params");
   myconfig->testpool[PVFS_RESTART_SERVER].test_func = (void *)pvfs_restart_server;
   myconfig->testpool[PVFS_RESTART_SERVER].test_name = str_malloc("pvfs_restart_server");
   myconfig->testpool[PVFS_STOP_SERVER].test_func = (void *)pvfs_stop_server;
   myconfig->testpool[PVFS_STOP_SERVER].test_name = str_malloc("pvfs_stop_server");
   myconfig->testpool[TEST_INVALID_FILES].test_func = (void *)test_invalid_files;
   myconfig->testpool[TEST_INVALID_FILES].test_name = str_malloc("test_invalid_files");
}

#endif
