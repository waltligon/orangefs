#ifndef INCLUDE_TESTPROTOS_H
#define INCLUDE_TESTPROTOS_H

#include <stdio.h>
#include <pts.h>
#include <generic.h>

/* include all test specific header files */
#include <test_create.h>

enum test_types { 
	TEST_CREATE
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
}

#endif
