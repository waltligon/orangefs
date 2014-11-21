#!/usr/bin/python
##
# @package OFSTest
# @namespace OFSHadoopTest
#
# @brief This class implements hadoop tests to be run on the virtual file system.
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


header = "OFS Hadoop Test"
prefix = "hadoop"
mount_fs = False
run_client = False
mount_as_fuse = False
debug = True


##
#
# @fn	wordcount(testing_node,output[])
#
# @brief  Tests hadoop using the wordcount program with a series of gutenberg project
#   e-books.
#
# @param testing_node OFSTestNode on which tests are run.
# @param output Array that holds output from commands. Passed by reference. 
#   
# @return 0 Test ran successfully
# @return Not 0 Test failed
#
#

def wordcount(testing_node,output=[]):

    # change directory

    testing_node.runSingleCommand("mkdir -p /home/%s/gutenberg" % testing_node.current_user)    
    testing_node.changeDirectory("/home/%s/gutenberg" % testing_node.current_user)    
    testing_node.runSingleCommand("%s/bin/hadoop dfs -mkdir -p /user/%s/gutenberg" % (testing_node.hadoop_location,testing_node.current_user))
    
    # get the gutenberg files
    testing_node.runSingleCommand("wget http://www.gutenberg.org/cache/epub/5000/pg5000.txt")
    testing_node.runSingleCommand("wget http://www.gutenberg.org/cache/epub/20417/pg20417.txt")
    testing_node.runSingleCommand("wget http://www.gutenberg.org/cache/epub/4300/pg4300.txt")
    
    testing_node.runSingleCommand("%s/bin/hadoop dfs -copyFromLocal /home/%s/gutenberg/* /user/%s/gutenberg" % (testing_node.hadoop_location,testing_node.current_user,testing_node.current_user))
    
    rc = testing_node.runSingleCommand("%s/bin/hadoop jar %s  wordcount /user/%s/gutenberg /user/%s/gutenberg-output" % (testing_node.hadoop_location,testing_node.hadoop_examples_location,testing_node.current_user,testing_node.current_user),output)
    # TODO: Compare acutal results with expected.
    return rc

##
#
# @fn	TestDFSIO_clean(testing_node,output=[]):
#
# @brief Runs TestDFSIO with clean option
# @param testing_node OFSTestNode on which tests are run.
# @param output Array that holds output from commands. Passed by reference. 
#   
# @return 0 Test ran successfully
# @return Not 0 Test failed
#
#


def TestDFSIO_clean(testing_node,output=[]):

    rc = testing_node.runSingleCommand("%s/bin/hadoop jar %s  TestDFSIO -clean" % (testing_node.hadoop_location,testing_node.hadoop_test_location),output)
    return rc

##
#
# @fn TestDFSIO_read(testing_node,output=[]):
#
# @brief Runs TestDFSIO with read option. Must be run after write, but not after 
#   clean.
# @param testing_node OFSTestNode on which tests are run.
# @param output Array that holds output from commands. Passed by reference. 
#   
# @return 0 Test ran successfully
# @return Not 0 Test failed
#

def TestDFSIO_read(testing_node,output=[]):

    rc = testing_node.runSingleCommand("%s/bin/hadoop jar %s  TestDFSIO -read -nrFiles 10 -fileSize 100" % (testing_node.hadoop_location,testing_node.hadoop_test_location),output)
    return rc


##
#
# @fn TestDFSIO_write(testing_node,output=[]):
#
# @brief Runs TestDFSIO with write option - Must be run before read.
# @param testing_node OFSTestNode on which tests are run.
# @param output Array that holds output from commands. Passed by reference. 
#   
# @return 0 Test ran successfully
# @return Not 0 Test failed
#

def TestDFSIO_write(testing_node,output=[]):

    rc = testing_node.runSingleCommand("%s/bin/hadoop jar %s  TestDFSIO -write -nrFiles 10 -fileSize 100" % (testing_node.hadoop_location,testing_node.hadoop_test_location),output)
    return rc

##
#
# @fn terasort_full(testing_node,output=[]):
#
# @brief Runs terasort with data the size of the half the memory on one node.
#
# @param testing_node OFSTestNode on which tests are run.
# @param output Array that holds output from commands. Passed by reference. 
#   
# @return 0 Test ran successfully
# @return Not 0 Test failed
#
#


def terasort_full(testing_node,output=[]):
    

    # generate 500M of data. Not much, but good enough to kick the tires.
    gensize = 5000000
     
    rc = testing_node.runSingleCommand("%s/bin/hadoop jar %s  teragen %d /user/%s/terasort5-input" % (testing_node.hadoop_location,testing_node.hadoop_examples_location,gensize,testing_node.current_user),output)
    if rc != 0:
        return rc    
    rc = testing_node.runSingleCommand("%s/bin/hadoop jar %s  terasort /user/%s/terasort5-input /user/%s/terasort5-output" % (testing_node.hadoop_location,testing_node.hadoop_examples_location,testing_node.current_user,testing_node.current_user),output)
    if rc != 0:
        return rc    
    rc = testing_node.runSingleCommand("%s/bin/hadoop jar %s  teravalidate /user/%s/terasort5-output /user/%s/terasort5-validate" % (testing_node.hadoop_location,testing_node.hadoop_examples_location,testing_node.current_user,testing_node.current_user),output)

    return rc      
##
#
# @fn	terasort_full_1g(testing_node,output=[]):
#
# @brief  Runs terasort with 1G worth of data per node
#
# @param testing_node OFSTestNode on which tests are run.
# @param output Array that holds output from commands. Passed by reference. 
#   
# @return 0 Test ran successfully
# @return Not 0 Test failed
#
#
# 
#     
# def terasort_full_1g(testing_node,output=[]):
#     
#     # small-scale generate 1GB of data
#     rc = testing_node.runSingleCommand("%s/bin/hadoop jar %s/hadoop*examples*.jar  teragen 10000000 /user/%s/terasort-input" % (testing_node.hadoop_location,testing_node.hadoop_location,testing_node.current_user),output)
#     if rc != 0:
#         return rc    
#     rc = testing_node.runSingleCommand("%s/bin/hadoop jar %s/hadoop*examples*.jar  terasort /user/%s/terasort-input /user/%s/terasort-output" % (testing_node.hadoop_location,testing_node.hadoop_location,testing_node.current_user,testing_node.current_user),output)
#     if rc != 0:
#         return rc    
#     rc = testing_node.runSingleCommand("%s/bin/hadoop jar %s/hadoop*examples*.jar  teravalidate /user/%s/terasort-output /user/%s/terasort-validate" % (testing_node.hadoop_location,testing_node.hadoop_location,testing_node.current_user,testing_node.current_user),output)
# 
#     return rc     


##
#
# @fn mrbench(testing_node,output=[]):
#
# @brief  Hadoop MapReduce benchmark
#
# @param testing_node OFSTestNode on which tests are run.
# @param output Array that holds output from commands. Passed by reference. 
#   
# @return 0 Test ran successfully
# @return Not 0 Test failed
#
    
def mrbench(testing_node,output=[]):

    rc = testing_node.runSingleCommand("%s/bin/hadoop jar %s  mrbench -numRuns 50" % (testing_node.hadoop_location,testing_node.hadoop_test_location),output)
    return rc



tests = [ wordcount,
TestDFSIO_write,
TestDFSIO_read,
TestDFSIO_clean,
mrbench,
terasort_full
 ]
