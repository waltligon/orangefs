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
#include <test-invalid-files.h>
#include <test-uninitialized.h>
#include <test-finalized.h>
#include <test-misc.h>
#include <test-concurrent-meta.h>
#include <test-request-indexed.h>
#include <test-request-contiguous.h>
#include <test-encode-basic.h>
#include <test-noncontig-pattern.h>
#include <test-write-eof.h>
#include <test-vector-offset.h>
#include <test-vector-start-final-offset.h>
#include <test-contiguous-datatype.h>
#include <test-explicit-offset.h>
#include <test-request-tiled.h>
#include <test-mix.h>
#include <test-romio-noncontig-pattern2.h>

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
	PVFS_STOP_SERVER,
	TEST_INVALID_FILES,
	TEST_UNINITIALIZED,
	TEST_FINALIZED,
	TEST_MISC,
	TEST_CONCURRENT_META,
	TEST_REQUEST_INDEXED,
	TEST_REQUEST_CONTIGUOUS,
	TEST_ENCODE_BASIC,
	TEST_NONCONTIG_PATTERN,
	TEST_WRITE_EOF,
	TEST_VECTOR_OFFSET,
	TEST_VECTOR_START_FINAL_OFFSET,
	TEST_CONTIGUOUS_DATATYPE,
	TEST_EXPLICIT_OFFSET,
	TEST_REQUEST_TILED,
	TEST_MIX,
	TEST_ROMIO_NONCONTIG_PATTERN2
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
   myconfig->testpool[TEST_INVALID_FILES].test_param_init = (void *)null_params_parser;
   myconfig->testpool[TEST_INVALID_FILES].test_name = str_malloc("test_invalid_files");
   myconfig->testpool[TEST_UNINITIALIZED].test_func = (void *)test_uninitialized;
   myconfig->testpool[TEST_UNINITIALIZED].test_param_init = (void *)null_params_parser;
   myconfig->testpool[TEST_UNINITIALIZED].test_name = str_malloc("test_uninitialized");
   myconfig->testpool[TEST_FINALIZED].test_func = (void *)test_finalized;
   myconfig->testpool[TEST_FINALIZED].test_param_init = (void *)null_params_parser;
   myconfig->testpool[TEST_FINALIZED].test_name = str_malloc("test_finalized");
   myconfig->testpool[TEST_MISC].test_func = (void *)test_misc;
   myconfig->testpool[TEST_MISC].test_param_init = (void *)null_params_parser;
   myconfig->testpool[TEST_MISC].test_name = str_malloc("test_misc");
   myconfig->testpool[TEST_CONCURRENT_META].test_func = (void *)test_concurrent_meta;
   myconfig->testpool[TEST_CONCURRENT_META].test_param_init = (void *)null_params_parser;
   myconfig->testpool[TEST_CONCURRENT_META].test_name = str_malloc("test_concurrent_meta");
   myconfig->testpool[TEST_REQUEST_INDEXED].test_func = (void *)test_request_indexed;
   myconfig->testpool[TEST_REQUEST_INDEXED].test_name = str_malloc("test_request_indexed");
   myconfig->testpool[TEST_REQUEST_CONTIGUOUS].test_func = (void *)test_request_contiguous;
   myconfig->testpool[TEST_REQUEST_CONTIGUOUS].test_name = str_malloc("test_request_contiguous");
   myconfig->testpool[TEST_ENCODE_BASIC].test_func = (void *)test_encode_basic;
   myconfig->testpool[TEST_ENCODE_BASIC].test_name = str_malloc("test_encode_basic");
   myconfig->testpool[TEST_NONCONTIG_PATTERN].test_func = (void *)test_noncontig_pattern;
   myconfig->testpool[TEST_NONCONTIG_PATTERN].test_name = str_malloc("test_noncontig_pattern");
   myconfig->testpool[TEST_WRITE_EOF].test_func = (void *)test_write_eof;
   myconfig->testpool[TEST_WRITE_EOF].test_name = str_malloc("test_write_eof");
   myconfig->testpool[TEST_VECTOR_OFFSET].test_func = (void *)test_vector_offset;
   myconfig->testpool[TEST_VECTOR_OFFSET].test_name = str_malloc("test_vector_offset");
   myconfig->testpool[TEST_VECTOR_START_FINAL_OFFSET].test_func = (void *)test_vector_start_final_offset;
   myconfig->testpool[TEST_VECTOR_START_FINAL_OFFSET].test_name = str_malloc("test_vector_start_final_offset");
   myconfig->testpool[TEST_CONTIGUOUS_DATATYPE].test_func = (void *)test_contiguous_datatype;
   myconfig->testpool[TEST_CONTIGUOUS_DATATYPE].test_name = str_malloc("test_contiguous_datatype");
   myconfig->testpool[TEST_EXPLICIT_OFFSET].test_func = (void *)test_explicit_offset;
   myconfig->testpool[TEST_EXPLICIT_OFFSET].test_name = str_malloc("test_explicit_offset");
   myconfig->testpool[TEST_REQUEST_TILED].test_func = (void *)test_request_tiled;
   myconfig->testpool[TEST_REQUEST_TILED].test_name = str_malloc("test_request_tiled");
   myconfig->testpool[TEST_MIX].test_func = (void *)test_mix;
   myconfig->testpool[TEST_MIX].test_name = str_malloc("test_mix");
   myconfig->testpool[TEST_ROMIO_NONCONTIG_PATTERN2].test_func = (void *)test_romio_noncontig_pattern2;
   myconfig->testpool[TEST_ROMIO_NONCONTIG_PATTERN2].test_name = str_malloc("test_romio_noncontig_pattern2");
}

#endif
