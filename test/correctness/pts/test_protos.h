#ifndef INCLUDE_TESTPROTOS_H
#define INCLUDE_TESTPROTOS_H

#include <stdio.h>
#include <pts.h>
#include <generic.h>

/* include all test specific header files */
#include <test_create.h>
#include <test_pvfs_datatype_init.h>
#include <test_pvfs_datatype_contig.h>
#include <test_pvfs_datatype_vector.h>
#include <test_pvfs_datatype_hvector.h>

enum test_types { 
    TEST_CREATE,
        TEST_PVFSDATATYPE_INIT,
        TEST_PVFSDATATYPE_CONTIG,
        TEST_PVFSDATATYPE_VECTOR,
        TEST_PVFSDATATYPE_HVECTOR
};

void setup_ptstests(config *myconfig) {

   /* 
     example test setup, must define the three following values (pointers):
     1.) test_func, must be setwhich is a function that does all of the actual work
     2.) test_param_init, optionally set function that parses arguments on --long
     format
     3.) test_name, must be set to a distinct test name string
   */
   myconfig->testpool[TEST_CREATE].test_func = (void *)create_file;
   myconfig->testpool[TEST_CREATE].test_name = str_malloc("create_file");
   myconfig->testpool[TEST_PVFSDATATYPE_INIT].test_func = (void *)test_pvfs_datatype_init;
   myconfig->testpool[TEST_PVFSDATATYPE_INIT].test_name = str_malloc("test_pvfs_datatype_init");

   myconfig->testpool[TEST_PVFSDATATYPE_CONTIG].test_func = (void *)test_pvfs_datatype_contig;
   myconfig->testpool[TEST_PVFSDATATYPE_CONTIG].test_name = str_malloc("test_pvfs_datatype_contig");

   myconfig->testpool[TEST_PVFSDATATYPE_VECTOR].test_func = (void *)test_pvfs_datatype_vector;
   myconfig->testpool[TEST_PVFSDATATYPE_VECTOR].test_name = str_malloc("test_pvfs_datatype_vector");

   myconfig->testpool[TEST_PVFSDATATYPE_HVECTOR].test_func = (void *)test_pvfs_datatype_hvector;
   myconfig->testpool[TEST_PVFSDATATYPE_HVECTOR].test_name = str_malloc("test_pvfs_datatype_hvector");
}

#endif
