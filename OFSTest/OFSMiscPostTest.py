#!/usr/bin/python
##
#
# @namespace OFSMiscPostTest
#
# @brief This class implements tests to be run after all other tests have run.
#
#
# @var  header 
# Name of header printed in output file
# @var  prefix  
# Name of prefix for test name
# @var  run_client  
# False
# @var  mount_fs  
# Does the file system need to be mounted?
# @var  mount_as_fuse
# False
# @var  tests  
# List of test functions (at end of file)
#
#


header = "OFS Post-run Tests"
prefix = "posttest"
mount_fs = False
run_client = False
mount_as_fuse = False

    
#------------------------------------------------------------------------------
#  
# Test functions
#
# All functions MUST have the following parameters and return code convention.
#
#   params:
#
#   testing_node = OFSTestNode on which the tests will be run
#   output = Array that stores output information
#
#   return:
#   
#        0: Test ran successfully
#        !0: Test failed
#------------------------------------------------------------------------------



## 
# @fn checkSHM(testing_node,output=[]):
#
# checks to see if there are any pvfs files in the /dev/shm directory. No files
# should remain in directory after client is shut down.
# @param testing_node OFSTestNode on which tests are run.
# @param output Array that holds output from commands. Passed by reference. 
#   
# @return 0 Test ran successfully
# @return Not 0 Test failed
#
#


def checkSHM(testing_node,output=[]):
 
    rc = testing_node.runSingleCommand("ls -A /dev/shm/pvfs*",output)
    
    if rc != 0:
        # We shouldn't find any, so if ls fails, return success.
        return 0
    else:
        # And if it succeeds, return failure.
        return 1

tests = [ checkSHM ]
