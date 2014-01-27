#!/usr/bin/python
#
#
# OFSHadoopTest
#
# This class implements tests to be run on the virtual file system.
#
#
# variables:
#
#   header = Name of header printed in output file
#   prefix = Name of prefix for test name
#   run_client = False
#   mount_fs = Does the file system need to be mounted?
#   mount_as_fuse = False
#   tests = list of test functions (at end of file)
#------------------------------------------------------------------------------


header = "OFS Hadoop Test"
prefix = "hadoop"
mount_fs = False
run_client = False
mount_as_fuse = False


#------------------------------------------------------------------------------
#
#	wordcount()
#
#   Tests hadoop using the wordcount program with a series of gutenberg project
#   e-books.
#
#------------------------------------------------------------------------------

def wordcount(testing_node,output=[]):

    # change directory

    testing_node.runSingleCommand("mkdir -p /home/%s/gutenberg" % testing_node.current_user)    
    testing_node.changeDirectory("/home/%s/gutenberg" % testing_node.current_user)    
    testing_node.runSingleCommand("%s/bin/hadoop dfs -mkdir /user/%s/gutenberg" % (testing_node.hadoop_location,testing_node.current_user))
    
    # get the gutenberg files
    testing_node.runSingleCommand("wget http://www.gutenberg.org/cache/epub/5000/pg5000.txt")
    testing_node.runSingleCommand("wget http://www.gutenberg.org/cache/epub/20417/pg20417.txt")
    testing_node.runSingleCommand("wget http://www.gutenberg.org/cache/epub/4300/pg4300.txt")
    
    testing_node.runSingleCommand("%s/bin/hadoop dfs -copyFromLocal /home/%s/gutenberg/* /user/%s/gutenberg" % (testing_node.hadoop_location,testing_node.current_user,testing_node.current_user))
    
    rc = testing_node.runSingleCommand("%s/bin/hadoop jar %s/hadoop*examples*.jar  wordcount /user/%s/gutenberg /user/%s/gutenberg-output" % (testing_node.hadoop_location,testing_node.hadoop_location,testing_node.current_user,testing_node.current_user),output)
    # Todo: Compare acutal results with expected.
    return rc

#------------------------------------------------------------------------------
#
#	TestDFSIO_clean()
#
#   runs TestDFSIO with clean option
#
#------------------------------------------------------------------------------

def TestDFSIO_clean(testing_node,output=[]):

    rc = testing_node.runSingleCommand("%s/bin/hadoop jar %s/hadoop*test*.jar  TestDFSIO -clean" % (testing_node.hadoop_location,testing_node.hadoop_location),output)
    return rc

#------------------------------------------------------------------------------
#
#	TestDFSIO_read()
#
#   runs TestDFSIO with read option. Must be run after write, but not after 
#   clean.
#
#------------------------------------------------------------------------------

def TestDFSIO_read(testing_node,output=[]):

    rc = testing_node.runSingleCommand("%s/bin/hadoop jar %s/hadoop*test*.jar  TestDFSIO -read -nrFiles 10 -fileSize 100" % (testing_node.hadoop_location,testing_node.hadoop_location),output)
    return rc


#------------------------------------------------------------------------------
#
#	TestDFSIO_write()
#
#   runs TestDFSIO with write option - Must be run before read.
#
#------------------------------------------------------------------------------

def TestDFSIO_write(testing_node,output=[]):

    rc = testing_node.runSingleCommand("%s/bin/hadoop jar %s/hadoop*test*.jar  TestDFSIO -write -nrFiles 10 -fileSize 100" % (testing_node.hadoop_location,testing_node.hadoop_location),output)
    return rc

#------------------------------------------------------------------------------
#
#	terasort_full_5g()
#
#   runs terasort with 5G worth of data per node
#
#------------------------------------------------------------------------------


def terasort_full_5g(testing_node,output=[]):
    
    # large-scale generate 5GB of data
    rc = testing_node.runSingleCommand("%s/bin/hadoop jar %s/hadoop*examples*.jar  teragen 50000000 /user/%s/terasort5-input" % (testing_node.hadoop_location,testing_node.hadoop_location,testing_node.current_user),output)
    if rc != 0:
        return rc    
    rc = testing_node.runSingleCommand("%s/bin/hadoop jar %s/hadoop*examples*.jar  terasort /user/%s/terasort5-input /user/%s/terasort5-output" % (testing_node.hadoop_location,testing_node.hadoop_location,testing_node.current_user,testing_node.current_user),output)
    if rc != 0:
        return rc    
    rc = testing_node.runSingleCommand("%s/bin/hadoop jar %s/hadoop*examples*.jar  teravalidate /user/%s/terasort5-output /user/%s/terasort5-validate" % (testing_node.hadoop_location,testing_node.hadoop_location,testing_node.current_user,testing_node.current_user),output)

    return rc      
#------------------------------------------------------------------------------
#
#	terasort_full_1g()
#
#   runs terasort with 1G worth of data per node
#
#------------------------------------------------------------------------------

    
def terasort_full_1g(testing_node,output=[]):
    
    # small-scale generate 1GB of data
    rc = testing_node.runSingleCommand("%s/bin/hadoop jar %s/hadoop*examples*.jar  teragen 10000000 /user/%s/terasort-input" % (testing_node.hadoop_location,testing_node.hadoop_location,testing_node.current_user),output)
    if rc != 0:
        return rc    
    rc = testing_node.runSingleCommand("%s/bin/hadoop jar %s/hadoop*examples*.jar  terasort /user/%s/terasort-input /user/%s/terasort-output" % (testing_node.hadoop_location,testing_node.hadoop_location,testing_node.current_user,testing_node.current_user),output)
    if rc != 0:
        return rc    
    rc = testing_node.runSingleCommand("%s/bin/hadoop jar %s/hadoop*examples*.jar  teravalidate /user/%s/terasort-output /user/%s/terasort-validate" % (testing_node.hadoop_location,testing_node.hadoop_location,testing_node.current_user,testing_node.current_user),output)

    return rc     


#------------------------------------------------------------------------------
#
#   mrbench()
#
#   Hadoop MapReduce benchmark
#
#------------------------------------------------------------------------------ 
    
def mrbench(testing_node,output=[]):

    rc = testing_node.runSingleCommand("%s/bin/hadoop jar %s/hadoop*test*.jar  mrbench -numRuns 50" % (testing_node.hadoop_location,testing_node.hadoop_location),output)
    return rc


tests = [ wordcount,
TestDFSIO_write,
TestDFSIO_read,
TestDFSIO_clean,
mrbench,
terasort_full_1g
 ]
