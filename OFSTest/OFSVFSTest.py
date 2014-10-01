#!/usr/bin/python
#
# @namespace OFSVFSTest
#
# @brief This class implements tests to be run on the virtual file system when mounted via kernel module or fuse. 
#
#
# @var  header 
# Name of header printed in output file
# @var  prefix  
# Name of prefix for test name
# @var  run_client  
# Do we need to run the PVFS2 client?
# @var  mount_fs  
# Does the file system need to be mounted?
# @var  mount_as_fuse
# Do we mount it via fuse?
# @var  tests  
# List of test functions (at end of file)
#
#


import inspect

header = "OFS VFS Test(kmod)"
prefix = "vfs-kmod"
mount_fs = True
run_client = True
mount_as_fuse = False
debug = True


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
# @fn setFuseConfig()
#
# The same tests are run for kernel mod (kmod) and fuse testing. This function 
# sets the variable to fuse mode.


def setFuseConfig():

    global header 
    global prefix
    global mount_fs
    global run_client
    global mount_as_fuse
    
    header = "OFS VFS Test(fuse)"
    prefix = "vfs-fuse"
    mount_fs = True
    run_client = False
    mount_as_fuse = True
    

##
# @fn setKmodConfig()
#
# The same tests are run for kernel mod (kmod) and fuse testing. This function 
# sets the variable to Kmod mode.


def setKmodConfig():

    global header 
    global prefix
    global mount_fs
    global run_client
    global mount_as_fuse

    header = "OFS VFS Test(kmod)"
    prefix = "vfs-kmod"
    mount_fs = True
    run_client = True
    mount_as_fuse = False

##
#
# @fn append(testing_node,output=[]):
#
#   This test checks the append ( > and >>) functionality by appending to a 
#   file on the mounted OrangeFS filesystem, then comparing it to an expected
#   results reference file.
#
#   append2() tests in a directory on the OrangeFS filesystem while append()
#   tests directly on the root of the filesystem.
#
#
# @param testing_node OFSTestNode on which tests are run.
# @param output Array that holds output from commands. Passed by reference. 
#   
# @return 0 Test ran successfully
# @return Not 0 Test failed
#

    
def append(testing_node,output=[]):
    
    append_test = testing_node.ofs_mount_point +"/append_test"
    local_reference = testing_node.ofs_installation_location + "/append_ref"
    
    test_string = ""
    for i in range(25):
        test_string = "%s line%d\n" % (test_string,i)
    
    # use a batchfile to create the files. This avoids redirect confusion
    testing_node.addBatchCommand('echo "%s" > %s' % (test_string,local_reference))
    testing_node.addBatchCommand('echo "%s" > %s' % (test_string,append_test))
    testing_node.addBatchCommand('echo "%s" >> %s' % (test_string,local_reference))
    testing_node.addBatchCommand('echo "%s" >> %s' % (test_string,append_test))
    testing_node.runAllBatchCommands()
   
    # now diff it
    rc = testing_node.runSingleCommand("diff -u %s %s" % (append_test, local_reference),output)
    return rc

##
#
# @fn append2(testing_node,output=[]):
#
#   This test checks the append ( > and >>) functionality by appending to a 
#   file on the mounted OrangeFS filesystem, then comparing it to an expected
#   results reference file.
#
#   append2() tests in a directory on the OrangeFS filesystem while append()
#   tests directly on the root of the filesystem.
#
# @param testing_node OFSTestNode on which tests are run.
# @param output Array that holds output from commands. Passed by reference. 
#   
# @return 0 Test ran successfully
# @return Not 0 Test failed
#

def append2(testing_node,output=[]):
    pvfs2_testdir = testing_node.ofs_mount_point +"/append_dir"
    append_test =  pvfs2_testdir+"/append_test2"
    local_reference = testing_node.ofs_installation_location + "/append_ref2"
    
    test_string = ""
    for i in range(25):
        test_string = "%s line%d\n" % (test_string,i)
    
    # use a batchfile to create the files. This avoids redirect confusion
    testing_node.addBatchCommand("mkdir -p %s" % pvfs2_testdir)
    testing_node.addBatchCommand('echo "%s" > %s' % (test_string,local_reference))
    testing_node.addBatchCommand('echo "%s" > %s' % (test_string,append_test))
    testing_node.addBatchCommand('echo "%s" >> %s' % (test_string,local_reference))
    testing_node.addBatchCommand('echo "%s" >> %s' % (test_string,append_test))
    testing_node.runAllBatchCommands()
   
    rc = testing_node.runSingleCommand("diff -u %s %s" % (append_test, local_reference),output)

    return rc

##
#
# @fn bonnie(testing_node,output=[]):
#
# Bonnie++ tests large file IO and creation/deletion of small files.
#
# See http://sourceforge.net/projects/bonnie/
#
# @param testing_node OFSTestNode on which tests are run.
# @param output Array that holds output from commands. Passed by reference. 
#   
# @return 0 Test ran successfully
# @return Not 0 Test failed
#


def bonnie(testing_node,output=[]):

    
    rc = 0
    #make sure that the benchmarks have been installed
    if testing_node.ofs_extra_tests_location == "":
        testing_node.installBenchmarks()
    
    testing_node.changeDirectory("%s/bonnie++-1.03e" % testing_node.ofs_extra_tests_location)
    
    # check to see if we have already compiled bonnie++
    if testing_node.runSingleCommand( "[ -f %s/bonnie++-1.03e/bonnie++ ]" % testing_node.ofs_extra_tests_location):

        rc = testing_node.runSingleCommand("./configure",output)
        if rc != 0:
            return rc
        rc = testing_node.runSingleCommand("make",output)
        if rc != 0:
            return rc
        
    testing_node.changeDirectory(testing_node.ofs_mount_point)
    rc = testing_node.runSingleCommand(testing_node.ofs_extra_tests_location+"/bonnie++-1.03e/bonnie++  -n 1:0:0:1  -r 8 -s 16 ",output)
    

    return rc

##
#
# @fn dbench(testing_node,output=[]):
#
#   DBENCH is a tool to generate I/O workloads to either a filesystem or to a 
#   networked CIFS or NFS server. It can even talk to an OrangeFS target.
#   DBENCH can be used to stress a filesystem or a server to see which workload
#   it becomes saturated and can also be used for preditcion analysis to 
#   determine "How many concurrent clients/applications performing this 
#   workload can my server handle before response starts to lag?"
#
#   http://dbench.samba.org/
#
#
# @param testing_node OFSTestNode on which tests are run.
# @param output Array that holds output from commands. Passed by reference. 
#   
# @return 0 Test ran successfully
# @return Not 0 Test failed
#
    
def dbench(testing_node,output=[]):
    
    rc = 0
    #make sure that the benchmarks have been installed
    if testing_node.ofs_extra_tests_location == "":
        testing_node.installBenchmarks()
    
    #testing_node.runSingleCommand("mkdir -p %s/dbench-3.03" % (testing_node.ofs_extra_tests_location))
    testing_node.changeDirectory("%s/dbench-3.03" % testing_node.ofs_extra_tests_location)
    
    # check to see if we have already compiled dbench
    if testing_node.runSingleCommand( "[ -f %s/dbench-3.03/dbench ]" % testing_node.ofs_extra_tests_location):

        rc = testing_node.runSingleCommand("make clean",output)
        rc = testing_node.runSingleCommand("./configure",output)

        # Patch dbench to add support for OrangeFS
        rc = testing_node.runSingleCommand("patch -p3 < %s/test/automated/vfs-tests.d/dbench.patch" % testing_node.ofs_source_location,output)
        if rc != 0:
            return rc
        
        rc = testing_node.runSingleCommand("make",output)
        if rc != 0:
            return rc

    # Copy the loadfile to the OrangeFS 
    rc = testing_node.runSingleCommand("cp client.txt %s" % testing_node.ofs_mount_point,output)
    if rc != 0:
        return rc
    
    # Run dbench from the mount_point.
    testing_node.changeDirectory(testing_node.ofs_mount_point)
    
    
    rc = testing_node.runSingleCommand(testing_node.ofs_extra_tests_location+"/dbench-3.03/dbench -c client.txt 10 -t 300 ",output)
    

    return rc

##
#
# @fn fdtree(testing_node,output=[]):
#
#   The fdtree software is used for testing the metadata performance of a file 
#   system.
#
#   https://computing.llnl.gov/?set=code&page=sio_downloads
#
#
# @param testing_node OFSTestNode on which tests are run.
# @param output Array that holds output from commands. Passed by reference. 
#   
# @return 0 Test ran successfully
# @return Not 0 Test failed
#    

def fdtree(testing_node,output=[]):

    
    rc = 0
    #make sure that the benchmarks have been installed
    if testing_node.ofs_extra_tests_location == "":
        testing_node.installBenchmarks()
    
    # Run fdtree from the mount_point
    testing_node.changeDirectory(testing_node.ofs_mount_point)
    rc = testing_node.runSingleCommand(testing_node.ofs_extra_tests_location+"/fdtree-1.0.1/fdtree.bash -l 4 -d 5",output)
    
    return rc

##
#
# @fn fstest(testing_node,output=[]):
#
#   Test filesystem (for example, fusecompress) for errors in implementing 
#   random file access. It writes and reads random blocks of random lengths at 
#   random locations, and verifies XOR sums at the end of operation.
#
#   https://code.google.com/p/fstest/
#
#
# @param testing_node OFSTestNode on which tests are run.
# @param output Array that holds output from commands. Passed by reference. 
#   
# @return 0 Test ran successfully
# @return Not 0 Test failed
#    

    
def fstest(testing_node,output=[]):
    testing_node.runSingleCommand("mkdir -p %s/fstest" % testing_node.ofs_mount_point,output)
    
    if testing_node.runSingleCommand( "[ -f %s/fstest ]" % testing_node.ofs_source_location):
        rc = testing_node.runSingleCommand("gcc %s/test/automated/vfs-tests.d/fstest.c -o %s/fstest" % (testing_node.ofs_source_location,testing_node.ofs_source_location),output)
        if rc != 0:
            return rc
    
    rc = testing_node.runSingleCommand("%s/fstest -p %s/fstest" % (testing_node.ofs_source_location,testing_node.ofs_mount_point),output)
        
    return rc

##
#
# @fn fsx(testing_node,output=[]):
#
#   File system exerciser
#
#   http://codemonkey.org.uk/projects/fsx/
#
# @param testing_node OFSTestNode on which tests are run.
# @param output Array that holds output from commands. Passed by reference. 
#   
# @return 0 Test ran successfully
# @return Not 0 Test failed
#
def fsx(testing_node,output=[]):

    
    testing_node.runSingleCommand("mkdir -p %s/fsx" % testing_node.ofs_mount_point)
    
    # check to see if we have already compiled fsx
    if testing_node.runSingleCommand( "[ -f %s/fsx ]" % testing_node.ofs_source_location):

        rc = testing_node.runSingleCommand("gcc %s/test/automated/vfs-tests.d/fsx.c -o %s/fsx" % (testing_node.ofs_source_location,testing_node.ofs_source_location),output)
        if rc != 0:
            return rc
    
    rc = testing_node.runSingleCommand("%s/fsx -N 1000 -W -R %s/fsx_testfile" % (testing_node.ofs_source_location,testing_node.ofs_mount_point),output)
    return rc

##
#
# @fn iozone(testing_node,output=[]):
#
#   IOzone is a filesystem benchmark tool. The benchmark generates and measures
#   a variety of file operations. Iozone has been ported to many machines and 
#   runs under many operating systems.
#
#   Iozone is useful for performing a broad filesystem analysis of a vendor's 
#   computer platform. The benchmark tests file I/O performance for the 
#   following operations:
#
#   Read, write, re-read, re-write, read backwards, read strided, fread, 
#   fwrite, random read, pread ,mmap, aio_read, aio_write
#
#   http://www.iozone.org/
#
#
# @param testing_node OFSTestNode on which tests are run.
# @param output Array that holds output from commands. Passed by reference. 
#   
# @return 0 Test ran successfully
# @return Not 0 Test failed
#

def iozone(testing_node,output=[]):
    
    rc = 0
    #make sure that the benchmarks have been installed
    if testing_node.ofs_extra_tests_location == "":
        testing_node.installBenchmarks()
    testing_node.changeDirectory("%s/iozone3_239/src/current" % testing_node.ofs_extra_tests_location)
   
    # check to see if we have already compiled iozone
    if testing_node.runSingleCommand( "[ -f %s/iozone3_239/src/current/iozone ]" % testing_node.ofs_extra_tests_location):
        rc = testing_node.runSingleCommand("patch -p5 < %s/test/automated/vfs-tests.d/iozone.patch" % testing_node.ofs_source_location,output)
        if rc != 0:
            return rc
        
        rc = testing_node.runSingleCommand("make linux",output)
        if rc != 0:
            return rc
    
    rc = testing_node.runSingleCommand("./iozone -a -y 4096 -q $((1024*16)) -n 4096 -g $((1024*16*2)) -f %s/test_iozone_file" % testing_node.ofs_mount_point,output)
        
    return rc

##
#
# @fn ltp(testing_node,output=[]):
#
#   The Linux Test Project is a joint project started by SGI and maintained 
#   by IBM, that has a goal to deliver test suites to the open source 
#   community that validate the reliability, robustness, and stability of 
#   Linux. The LTP testsuite contains a collection of tools for testing the 
#   Linux kernel and related features.
#
#   This runs the pvfs testcases for LTP
#
#   http://ltp.sourceforge.net/
#
# @param testing_node OFSTestNode on which tests are run.
# @param output Array that holds output from commands. Passed by reference. 
#   
# @return 0 Test ran successfully
# @return Not 0 Test failed
#  
def ltp(testing_node,output=[]):
    
    LTP_ARCHIVE_VERSION = "ltp-full-20120903"
    LTP_ARCHIVE_VERSION = "20140422"
    #LTP_ARCHIVE_TYPE = ".bz2"
    LTP_ARCHIVE_TYPE = ".tar.gz"
    LTP_ARCHIVE = "%s%s" % (LTP_ARCHIVE_VERSION,LTP_ARCHIVE_TYPE)
    #LTP_URL = "http://devorange.clemson.edu/pvfs"
    LTP_PREFIX = "/opt/ltp"
    LTP_URL = "https://github.com/linux-test-project/ltp/archive"
    
    
    rc = 0
    vfs_type = "kmod"
    # check for fuse
    tmp = []
    testing_node.checkMount(mount_point=testing_node.ofs_mount_point,output=tmp)
    
    if "pvfs2fuse" in tmp[1]:
        vfs_type = "fuse"
        print "LTP test cannot be run for filesystem mounted via fuse"
        return -999
    
    #make sure that the benchmarks have been installed
    if testing_node.ofs_extra_tests_location == "":
        testing_node.installBenchmarks()
        
    testing_node.changeDirectory(testing_node.ofs_extra_tests_location)
    
    
    
    if testing_node.runSingleCommand("[ -f %s/runltp ]" % LTP_PREFIX):
        testing_node.runSingleCommand("rm -rf ltp-" + LTP_ARCHIVE_VERSION + "*",output)
        rc = testing_node.runSingleCommand("wget --no-check-certificate --output-document=%s %s/%s" % (LTP_ARCHIVE,LTP_URL,LTP_ARCHIVE),output)
        if rc != 0:
            
            return rc

        rc = testing_node.runSingleCommand("tar -xzf %s" % LTP_ARCHIVE,output)
        if rc != 0:
            
            return rc
        
        testing_node.changeDirectory(testing_node.ofs_extra_tests_location+"/ltp-"+LTP_ARCHIVE_VERSION)
        
        # Patch ltp for OrangeFS support
        rc = testing_node.runSingleCommand("patch -p1 < %s/test/automated/vfs-tests.d/ltp-20140422-zoo-path.patch" % testing_node.ofs_source_location,output)
        if rc != 0:
            
            return rc
        
        rc = testing_node.runSingleCommand("make autotools")
        rc = testing_node.runSingleCommand("DEBUG_CFLAGS='-g' OPT_CFLAGS='-O0' ./configure --prefix=%s" % LTP_PREFIX,output)
        #if rc != 0:
        #    return rc

        rc = testing_node.runSingleCommand('make all',output)
        if rc != 0:
            
            return rc

        testing_node.runSingleCommand('make install',output)
        if rc != 0:
            return rc
        
    testing_node.runSingleCommand("cp %s/test/automated/vfs-tests.d/ltp-pvfs-testcases runtest/" % testing_node.ofs_source_location)
    testing_node.runSingleCommand("cp %s/test/automated/vfs-tests.d/ltp-pvfs-testcases %s/runtest/" % (testing_node.ofs_source_location,LTP_PREFIX))
    testing_node.runSingleCommand("mkdir -p %s/ltp-tmp" % testing_node.ofs_mount_point)
    testing_node.runSingleCommand("chmod 777 %s/ltp-tmp" % testing_node.ofs_mount_point)
    testing_node.runSingleCommand("umask 0")
    
    testing_node.changeDirectory(LTP_PREFIX)
    
    
    print 'sudo PVFS2TAB_FILE=%s/etc/orangefstab LD_LIBRARY_PATH=/opt/db4/lib:%s/lib ./runltp -p -l %s/ltp-pvfs-testcases-%s.log -d %s/ltp-tmp -f ltp-pvfs-testcases -A %s/zoo.tmp 2>&1 | tee %s/ltp-pvfs-testcases-%s.output' % (testing_node.ofs_installation_location,testing_node.ofs_installation_location,testing_node.ofs_installation_location, vfs_type, testing_node.ofs_mount_point,testing_node.ofs_extra_tests_location,testing_node.ofs_installation_location,vfs_type)
    rc = testing_node.runSingleCommandAsRoot('PVFS2TAB_FILE=%s/etc/orangefstab LD_LIBRARY_PATH=/opt/db4/lib:%s/lib ./runltp -p -l %s/ltp-pvfs-testcases-%s.log -d %s/ltp-tmp -f ltp-pvfs-testcases -A %s/zoo.tmp 2>&1 | tee %s/ltp-pvfs-testcases-%s.output' % (testing_node.ofs_installation_location,testing_node.ofs_installation_location,testing_node.ofs_installation_location, vfs_type, testing_node.ofs_mount_point,testing_node.ofs_extra_tests_location,testing_node.ofs_installation_location,vfs_type),output)

    # check to see if log file is there
    if testing_node.runSingleCommand("[ -f %s/ltp-pvfs-testcases-%s.log ]"% (testing_node.ofs_installation_location,vfs_type)):
        print "Could not find ltp-pvfs-testcases.log file."
        return 1

    failrc = testing_node.runSingleCommand("grep FAIL %s/ltp-pvfs-testcases-%s.log" % (testing_node.ofs_installation_location,vfs_type))
    testing_node.changeDirectory('~')

  
    if failrc == 0:
        # if grep returns O, then there were failures.
        print "LTP completed with failures"
        return 1
    else:
        print "LTP completed successfully"
        return 0
        
    
    return rc
    

##
#
# @fn mkdir_vfs(testing_node,output=[]):
#
#   This runs the test-mkdir utility
#
#
# @param testing_node OFSTestNode on which tests are run.
# @param output Array that holds output from commands. Passed by reference. 
#   
# @return 0 Test ran successfully
# @return Not 0 Test failed
#
    

def mkdir_vfs(testing_node,output=[]):

    options = "--hostname=%s --fs-name=%s --network-proto=tcp --port=%s --exe-path=%s/bin --print-results --verbose" % (testing_node.hostname,testing_node.ofs_fs_name,testing_node.ofs_tcp_port,testing_node.ofs_installation_location)
    rc = testing_node.runSingleCommand("PATH=%s/bin:$PATH %s/test/test-mkdir --directory %s %s" % (testing_node.ofs_installation_location,testing_node.ofs_installation_location,testing_node.ofs_mount_point,options),output)
    return rc

##
#
# @fn shelltest(testing_node,output=[]):
#
#   This runs the pvfs2-shell-test.sh script.
#
# @param testing_node OFSTestNode on which tests are run.
# @param output Array that holds output from commands. Passed by reference. 
#   
# @return 0 Test ran successfully
# @return Not 0 Test failed
# 
    
def shelltest(testing_node,output=[]):

    testing_node.changeDirectory("~")
    rc = testing_node.runSingleCommand("bash %s/test/kernel/linux-2.6/pvfs2-shell-test.sh %s 2>&1" % (testing_node.ofs_source_location,testing_node.ofs_mount_point),output)
    return rc

##
#
# @fn symlink_vfs(testing_node,output=[]):
#
#   This runs the test-symlink-perms utility
#
# @param testing_node OFSTestNode on which tests are run.
# @param output Array that holds output from commands. Passed by reference. 
#   
# @return 0 Test ran successfully
# @return Not 0 Test failed
# 
def symlink_vfs(testing_node,output=[]):

    options = "--hostname=%s --fs-name=%s --network-proto=tcp --port=%s --exe-path=%s/bin --print-results --verbose" % (testing_node.hostname,testing_node.ofs_fs_name,testing_node.ofs_tcp_port,testing_node.ofs_installation_location)
    rc = testing_node.runSingleCommand("PATH=%s/bin:$PATH %s/test/test-symlink-perms --directory %s %s" % (testing_node.ofs_installation_location,testing_node.ofs_installation_location,testing_node.ofs_mount_point,options),output)
    return rc

##
# 
# @fn tail(testing_node,output=[]):
#
# This test checks to see if tail command works.
#
# @param testing_node OFSTestNode on which tests are run.
# @param output Array that holds output from commands. Passed by reference. 
#   
# @return 0 Test ran successfully
# @return Not 0 Test failed
#    
    
def tail(testing_node,output=[]):
    
    tail_test = testing_node.ofs_mount_point +"/tail_test"
    local_reference = testing_node.ofs_installation_location + "/tail_ref"
    
    test_string = ""
    for i in range(25):
        test_string = "%s line%d\n" % (test_string,i)
    
    # Create the file
    testing_node.runSingleCommand("%s > %s" % (test_string,tail_test))
   
    rc = testing_node.runSingleCommand("tail "+tail_test,output)
    # OK should do more here
    return rc

##
#
# @fn vfs_cp(testing_node,output=[]):
#
#	This copies a file to OrangeFS mount_point and back. Copied file should be
#	the same as the original.
#
# @param testing_node OFSTestNode on which tests are run.
# @param output Array that holds output from commands. Passed by reference. 
#   
# @return 0 Test ran successfully
# @return Not 0 Test failed
#  
    
    
def vfs_cp(testing_node,output=[]):

    # open the vfs_cp.log file
    filename = open(inspect.stack()[0][3]+".log","w")
    
    # Copy file from installation location/bin to mount point
    testing_node.runSingleCommand("cp %s/bin/pvfs2-cp %s" % (testing_node.ofs_installation_location,testing_node.ofs_mount_point))
    
    # Now copy it back to installation location.
    testing_node.runSingleCommand("cp %s/pvfs2-cp %s" % (testing_node.ofs_mount_point,testing_node.ofs_installation_location))
    
    # Compare original in the bin directory to what was copied back
    rc = testing_node.runSingleCommand("cmp %s/bin/pvfs2-cp %s/pvfs2-cp" % (testing_node.ofs_installation_location,testing_node.ofs_installation_location),output)
    return rc

tests = [ 
#ltp,
append,
append2,

fdtree,
fstest,
fsx,
iozone,
mkdir_vfs,
shelltest,
symlink_vfs,
tail,
vfs_cp,
bonnie,
dbench,
ltp
 ]
