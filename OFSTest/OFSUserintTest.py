#!/usr/bin/python
#
#
# OFSVFSTest
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
#   tests = list of test functions
#------------------------------------------------------------------------------

import inspect

header = "OFS Usrint Library Test"
prefix = "usrint"
mount_fs = False
run_client = False
mount_as_fuse = False
tests = [ 
append,
append2,
bonnie,
fdtree,
fstest,
fsx,
iozone,
mkdir_usrint,
shelltest,
symlink_usrint,
tail,
usrint_cp,
ltp,
dbench ]

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


#------------------------------------------------------------------------------
#
# setFuseConfig()
#
# The same tests are run for kernel mod (kmod) and fuse testing. This function 
# sets the variable to fuse mode.
#------------------------------------------------------------------------------

def setFuseConfig():

    OFSVFSTest.header = "OFS VFS Test(fuse)"
    OFSVFSTest.prefix = "vfs-fuse"
    OFSVFSTest.mount_fs = True
    OFSVFSTest.run_client = False
    OFSVFSTest.mount_as_fuse = True
    
#------------------------------------------------------------------------------
#
# setKmodConfig()
#
# The same tests are run for kernel mod (kmod) and fuse testing. This function 
# sets the variable to Kmod mode.
#------------------------------------------------------------------------------

def setKmodConfig():
    OFSVFSTest.header = "OFS VFS Test(kmod)"
    OFSVFSTest.prefix = "vfs-kmod"
    OFSVFSTest.mount_fs = True
    OFSVFSTest.run_client = True
    OFSVFSTest.mount_as_fuse = False

#------------------------------------------------------------------------------
#
#   append()
#
#   This test checks the append ( > and >>) functionality by appending to a 
#   file on the mounted OrangeFS filesystem, then comparing it to an expected
#   results reference file.
#
#   append2() tests in a directory on the OrangeFS filesystem while append()
#   tests directly on the root of the filesystem.
#
#------------------------------------------------------------------------------
    
def append(testing_node,output=[]):
    
    append_test = testing_node.ofs_mount_point +"/append_test"
    local_reference = testing_node.ofs_installation_location + "/append_ref"
    
    test_string = ""
    for i in range(25):
        test_string = "%s line%d \\\n" % (test_string,i)
    
    preload = "LD_PRELOAD=%s/lib/libofs.so:%s/lib/libpvfs2.so " % (testing_node.ofs_installation_location,testing_node.ofs_installation_location)
    # use bash -c to create the files. This avoids redirect confusion
    #print 'bash -c \'echo "%s" > %s\''% (test_string,local_reference)
    testing_node.runSingleCommand('bash -c \'echo "%s" > %s\'' % (test_string,local_reference),output)
    #print output
    #print 'bash -c \'%s echo "%s" > %s\'' % (preload,test_string,append_test)
    testing_node.runSingleCommand('%s bash -c \'echo "%s" > %s\'' % (preload,test_string,append_test),output)
    #print output
    #print 'bash -c \'echo "%s" >> %s\'' % (test_string,local_reference)
    testing_node.runSingleCommand('bash -c \'echo "%s" >> %s\'' % (test_string,local_reference),output)
    #print output
    #print 'bash -c \'%s echo "%s" >> %s\'' % (preload,test_string,append_test)
    testing_node.runSingleCommand('%s bash -c \'echo "%s" >> %s\'' % (preload,test_string,append_test),output)
    #print output
    
    
    # now diff it
    rc = testing_node.runSingleCommand("%s diff -u %s %s" % (preload, append_test, local_reference),output)
    return rc

#------------------------------------------------------------------------------
#   append2()
#
#   This test checks the append ( > and >>) functionality by appending to a 
#   file on the mounted OrangeFS filesystem, then comparing it to an expected
#   results reference file.
#
#   append2() tests in a directory on the OrangeFS filesystem while append()
#   tests directly on the root of the filesystem.
#------------------------------------------------------------------------------

def append2(testing_node,output=[]):
    pvfs2_testdir = testing_node.ofs_mount_point +"/append_dir"
    append_test =  pvfs2_testdir+"/append_test2"
    local_reference = testing_node.ofs_installation_location + "/append_ref2"
    
    test_string = ""
    for i in range(25):
        test_string = "%s line%d \\\n" % (test_string,i)
    
    # use bash -c to create the files. This avoids redirect confusion
    preload = "LD_PRELOAD=%s/lib/libofs.so:%s/lib/libpvfs2.so " % (testing_node.ofs_installation_location,testing_node.ofs_installation_location)
    
    testing_node.runSingleCommand("%s mkdir -p %s" % (preload,pvfs2_testdir))

    #print 'bash -c \'echo "%s" > %s\''% (test_string,local_reference)
    testing_node.runSingleCommand('bash -c \'echo "%s" > %s\'' % (test_string,local_reference),output)
    #print output
    #print 'bash -c \'%s echo "%s" > %s\'' % (preload,test_string,append_test)
    testing_node.runSingleCommand('%s bash -c \'echo "%s" > %s\'' % (preload,test_string,append_test),output)
    #print output
    #print 'bash -c \'echo "%s" >> %s\'' % (test_string,local_reference)
    testing_node.runSingleCommand('bash -c \'echo "%s" >> %s\'' % (test_string,local_reference),output)
    #print output
    #print 'bash -c \'%s echo "%s" >> %s\'' % (preload,test_string,append_test)
    testing_node.runSingleCommand('%s bash -c \'echo "%s" >> %s\'' % (preload,test_string,append_test),output)
    #print output
    
   
    rc = testing_node.runSingleCommand("%s diff -u %s %s" % (preload, append_test, local_reference),output)

    return rc

#------------------------------------------------------------------------------
#
# bonnie()
#
# Bonnie++ tests large file IO and creation/deletion of small files.
#
# See http://sourceforge.net/projects/bonnie/
#------------------------------------------------------------------------------

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

    rc = testing_node.runSingleCommand("echo export LD_PRELOAD=%s/lib/libofs.so:%s/lib/libpvfs2.so; %s/bonnie++-1.03e/bonnie++  -n 1:0:0:1  -r 8 -s 16 2>&1" % (testing_node.ofs_installation_location,testing_node.ofs_installation_location,testing_node.ofs_extra_tests_location),output)
    

    return rc

#------------------------------------------------------------------------------
#
#   dbench()
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
#------------------------------------------------------------------------------
    
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
        rc = testing_node.runSingleCommand("patch -p3 < %s/test/automated/usrint-tests.d/dbench.patch" % testing_node.ofs_source_location,output)
        if rc != 0:
            return rc
        
        rc = testing_node.runSingleCommand("make",output)
        if rc != 0:
            return rc

    preload = "LD_PRELOAD=%s/lib/libofs.so:%s/lib/libpvfs2.so " % (testing_node.ofs_installation_location,testing_node.ofs_installation_location)
    rc = testing_node.runSingleCommand("%s cp %s/dbench-3.03/client.txt %s" % (preload,testing_node.ofs_extra_tests_location,testing_node.ofs_mount_point),output)
    if rc != 0:
        return rc
    
    #testing_node.changeDirectory(testing_node.ofs_mount_point)
    rc = testing_node.runSingleCommand("%s bash -c 'cd %s; %s/dbench-3.03/dbench -c client.txt 10 -t 300'" %(preload,testing_node.ofs_mount_point,testing_node.ofs_extra_tests_location),output)
       
    return rc

#------------------------------------------------------------------------------
#
# fdtree()
#
#   The fdtree software is used for testing the metadata performance of a file 
#   system.
#
#   https://computing.llnl.gov/?set=code&page=sio_downloads
#
#------------------------------------------------------------------------------    

def fdtree(testing_node,output=[]):

    
    rc = 0
    #make sure that the benchmarks have been installed
    if testing_node.ofs_extra_tests_location == "":
        testing_node.installBenchmarks()
    
    preload = "LD_PRELOAD=%s/lib/libofs.so:%s/lib/libpvfs2.so " % (testing_node.ofs_installation_location,testing_node.ofs_installation_location)

    # fdtree must be run from the mountpoint, but need to cd to that directory w/usrint libraries.
	testing_node.changeDirectory("~")
    rc = testing_node.runSingleCommand("%s cd %s; bash -c \"%s/fdtree-1.0.1/fdtree.bash -l 4 -d 5\"" % (preload,testing_node.ofs_mount_point,testing_node.ofs_extra_tests_location),output)
    
    return rc

#------------------------------------------------------------------------------
#
# fstest()
#
#   Test filesystem (for example, fusecompress) for errors in implementing 
#   random file access. It writes and reads random blocks of random lengths at 
#   random locations, and verifies XOR sums at the end of operation.
#
#   https://code.google.com/p/fstest/
#
#------------------------------------------------------------------------------    
def fstest(testing_node,output=[]):

    
def fstest(testing_node,output=[]):
    preload = "LD_PRELOAD=%s/lib/libofs.so:%s/lib/libpvfs2.so " % (testing_node.ofs_installation_location,testing_node.ofs_installation_location)
    testing_node.runSingleCommand("%s mkdir -p %s/fstest" % (preload,testing_node.ofs_mount_point))
    if testing_node.runSingleCommand( "[ -f %s/fstest ]" % (testing_node.ofs_source_location)):
        rc = testing_node.runSingleCommand("gcc %s/test/automated/usrint-tests.d/fstest.c -o %s/fstest" % (testing_node.ofs_source_location,testing_node.ofs_source_location),output)
        if rc != 0:
            return rc
        
    rc = testing_node.runSingleCommand("%s %s/fstest -p %s/fstest" %(preload,testing_node.ofs_source_location,testing_node.ofs_mount_point),output)
        
    return rc

#------------------------------------------------------------------------------
#
# fsx()
#
#   File system exerciser
#
#   http://codemonkey.org.uk/projects/fsx/
#------------------------------------------------------------------------------
def fsx(testing_node,output=[]):
    preload = "LD_PRELOAD=%s/lib/libofs.so:%s/lib/libpvfs2.so " % (testing_node.ofs_installation_location,testing_node.ofs_installation_location)
    testing_node.runSingleCommand("%s mkdir -p %s/fsx" % (preload,testing_node.ofs_mount_point))
    if testing_node.runSingleCommand( "[ -f %s/fsx ]" % testing_node.ofs_source_location):
        rc = testing_node.runSingleCommand("gcc %s/test/automated/usrint-tests.d/fsx.c -o %s/fsx" % (testing_node.ofs_source_location,testing_node.ofs_source_location),output)
        if rc != 0:
            return rc
    
    rc = testing_node.runSingleCommand("%s %s/fsx -N 1000 -W -R %s/fsx_testfile" %(preload,testing_node.ofs_source_location,testing_node.ofs_mount_point),output)
    
    return rc

#------------------------------------------------------------------------------
#
# iozone()
#
#   IOzone is a filesystem benchmark tool. The benchmark generates and measures
#   a variety of file operations. Iozone has been ported to many machines and 
#   runs under many operating systems.
#
#   Iozone is useful for performing a broad filesystem analysis of a vendor’s 
#   computer platform. The benchmark tests file I/O performance for the 
#   following operations:
#
#   Read, write, re-read, re-write, read backwards, read strided, fread, 
#   fwrite, random read, pread ,mmap, aio_read, aio_write
#
#   http://www.iozone.org/
#
#------------------------------------------------------------------------------

def iozone(testing_node,output=[]):
    
    rc = 0
    #make sure that the benchmarks have been installed
    if testing_node.ofs_extra_tests_location == "":
        testing_node.installBenchmarks()
    testing_node.changeDirectory("%s/iozone3_239/src/current" % testing_node.ofs_extra_tests_location)
    if testing_node.runSingleCommand( "[ -f %s/iozone3_239/src/current/iozone ]" % testing_node.ofs_extra_tests_location):       
        
        rc = testing_node.runSingleCommand("patch -p5 < %s/test/automated/usrint-tests.d/iozone.patch" % testing_node.ofs_source_location,output)
        if rc != 0:
            return rc
    
        rc = testing_node.runSingleCommand("make linux",output)
        if rc != 0:
            return rc
            
    rc = testing_node.runSingleCommand("LD_PRELOAD=%s/lib/libofs.so:%s/lib/libpvfs2.so ./iozone -a -y 4096 -q $((1024*16)) -n 4096 -g $((1024*16*2)) -f %s/test_iozone_file" %(testing_node.ofs_installation_location,testing_node.ofs_installation_location,testing_node.ofs_mount_point),output)
        
    return rc

#------------------------------------------------------------------------------
#
#   ltp()
#
#   The Linux™ Test Project is a joint project started by SGI™ and maintained 
#   by IBM®, that has a goal to deliver test suites to the open source 
#   community that validate the reliability, robustness, and stability of 
#   Linux. The LTP testsuite contains a collection of tools for testing the 
#   Linux kernel and related features.
#
#   This runs the pvfs testcases for LTP
#
#   http://ltp.sourceforge.net/
#
#------------------------------------------------------------------------------    
def ltp(testing_node,output=[]):
    
    LTP_ARCHIVE_VERSION = "ltp-full-20120903"
    LTP_ARCHIVE_TYPE = ".bz2"
    LTP_ARCHIVE = "%s%s" % (LTP_ARCHIVE_VERSION,LTP_ARCHIVE_TYPE)
    LTP_URL = "http://devorange.clemson.edu/pvfs"
    vfs_type = "usrint"
    
    rc = 0
    #make sure that the benchmarks have been installed
    if testing_node.ofs_extra_tests_location == "":
        testing_node.installBenchmarks()
        
    testing_node.changeDirectory(testing_node.ofs_extra_tests_location)
    
    if testing_node.runSingleCommand("[ -f /tmp/ltp/runltp ]"):
        testing_node.runSingleCommand("rm -rf " + LTP_ARCHIVE_VERSION + "*",output)
        rc = testing_node.runSingleCommand("wget -q %s/%s" % (LTP_URL,LTP_ARCHIVE),output)
        if rc != 0:
            
            return rc

        rc = testing_node.runSingleCommand("tar -xjf %s" % LTP_ARCHIVE,output)
        if rc != 0:
            
            return rc
        
        testing_node.changeDirectory(testing_node.ofs_extra_tests_location+"/"+LTP_ARCHIVE_VERSION)
        
        rc = testing_node.runSingleCommand("patch -p1 < %s/test/automated/usrint-tests.d/ltp-20120903-zoo-path.patch" % testing_node.ofs_source_location,output)
        if rc != 0:
            
            return rc
        
        rc = testing_node.runSingleCommand('./configure --prefix=/tmp/ltp ADD_CFLAGS="-D_GNU_SOURCE"',output)
        #if rc != 0:
        #    return rc

        rc = testing_node.runSingleCommand('export CFLAGS="-g"; make all',output)
        if rc != 0:
            
            return rc

        testing_node.runSingleCommand('make install',output)
        if rc != 0:
            return rc
        
        
    testing_node.runSingleCommand("cp %s/test/automated/usrint-tests.d/ltp-pvfs-testcases runtest/" % testing_node.ofs_source_location)
    testing_node.runSingleCommand("cp %s/test/automated/usrint-tests.d/ltp-pvfs-testcases /tmp/ltp/runtest/" % testing_node.ofs_source_location)
    testing_node.runSingleCommand("mkdir -p %s/ltp-tmp" % testing_node.ofs_source_location)
    testing_node.runSingleCommand("chmod 777 %s/ltp-tmp" % testing_node.ofs_mount_point)
    testing_node.runSingleCommand("umask 0")
    
    testing_node.changeDirectory('/tmp/ltp')
    
    print 'sudo PVFS2TAB_FILE=%s/etc/orangefstab LD_LIBRARY_PATH=/opt/db4/lib:%s/lib ./runltp -p -l %s/ltp-pvfs-testcases-%s.log -d %s/ltp-tmp -f ltp-pvfs-testcases -z %s/zoo.tmp >& %s/ltp-pvfs-testcases-%s.output' % (testing_node.ofs_installation_location,testing_node.ofs_installation_location,testing_node.ofs_installation_location, vfs_type, testing_node.ofs_mount_point,testing_node.ofs_extra_tests_location,testing_node.ofs_installation_location,vfs_type)
    rc = testing_node.runSingleCommandAsBatch('sudo PVFS2TAB_FILE=%s/etc/orangefstab LD_LIBRARY_PATH=/opt/db4/lib:%s/lib ./runltp -p -l %s/ltp-pvfs-testcases-%s.log -d %s/ltp-tmp -f ltp-pvfs-testcases -z %s/zoo.tmp >& %s/ltp-pvfs-testcases-%s.output' % (testing_node.ofs_installation_location,testing_node.ofs_installation_location,testing_node.ofs_installation_location, vfs_type, testing_node.ofs_mount_point,testing_node.ofs_extra_tests_location,testing_node.ofs_installation_location,vfs_type),output)

    # check to see if log file is there
    if testing_node.runSingleCommand("[ -f %s/ltp-pvfs-testcases-%s.log ]"% (testing_node.ofs_installation_location,vfs_type)):
        print "Could not find ltp-pvfs-testcases.log file."
        return 1

    failrc = testing_node.runSingleCommand("grep TFAIL %s/ltp-pvfs-testcases-%s.log" % (testing_node.ofs_installation_location,vfs_type),output)
    testing_node.changeDirectory('~')

  
    if failrc == 0:
        # if grep returns O, then there were failures.
        print "LTP completed with failures"
        return 1
    else:
        print "LTP completed successfully"
        return 0
        
    
    return rc
    

#------------------------------------------------------------------------------
#
#   mkdir_usrint()
#
#   This runs the test-mkdir utility
#
#------------------------------------------------------------------------------    
    

def mkdir_usrint(testing_node,output=[]):
    
    options = "--hostname=%s --fs-name=%s --network-proto=tcp --port=%s --exe-path=%s/bin --print-results --verbose" % (testing_node.host_name,testing_node.ofs_fs_name,testing_node.ofs_tcp_port,testing_node.ofs_installation_location)
    rc = testing_node.runSingleCommand("export LD_PRELOAD=%s/lib/libofs.so:%s/lib/libpvfs2.so; PATH=%s/bin:$PATH %s/test/test-mkdir --directory %s %s" % (testing_node.ofs_installation_location,testing_node.ofs_installation_location,testing_node.ofs_installation_location,testing_node.ofs_installation_location,testing_node.ofs_mount_point,options),output)
    return rc

#------------------------------------------------------------------------------
#
#   shelltest()
#
#   This runs the pvfs2-shell-test.sh script.
#------------------------------------------------------------------------------    
    
def shelltest(testing_node,output=[]):
    
    
    #print testing_node.runSingleCommandBacktick("find /tmp -name pvfs2-shell-test.sh")
    #hack to workaround bug in pvfs2-shell-test.sh
    testing_node.changeDirectory("~")
    rc = testing_node.runSingleCommand("LD_PRELOAD=%s/lib/libofs.so:%s/lib/libpvfs2.so cd %s; bash %s/test/kernel/linux-2.6/pvfs2-shell-test.sh %s " % (testing_node.ofs_installation_location,testing_node.ofs_installation_location,testing_node.ofs_mount_point,testing_node.ofs_source_location,testing_node.ofs_mount_point),output)
    return rc

#------------------------------------------------------------------------------
#
#   symlink_usrint()
#
#   This runs the test-symlink-perms utility
#------------------------------------------------------------------------------    
def symlink_usrint(testing_node,output=[]):

    preload = "LD_PRELOAD=%s/lib/libofs.so:%s/lib/libpvfs2.so " % (testing_node.ofs_installation_location,testing_node.ofs_installation_location)
    options = "--hostname=%s --fs-name=%s --network-proto=tcp --port=%s --exe-path=%s/bin --print-results --verbose" % (testing_node.host_name,testing_node.ofs_fs_name,testing_node.ofs_tcp_port,testing_node.ofs_installation_location)
    rc = testing_node.runSingleCommand("%s PATH=%s/bin:$PATH %s/test/test-symlink-perms --directory %s %s" % (preload,testing_node.ofs_installation_location,testing_node.ofs_installation_location,testing_node.ofs_mount_point,options),output)
    return rc

#------------------------------------------------------------------------------
# 
# tail()
#
# This test checks to see if tail command works.
#------------------------------------------------------------------------------    
    
def tail(testing_node,output=[]):
    
    tail_test = testing_node.ofs_mount_point +"/tail_test"
    local_reference = testing_node.ofs_installation_location + "/tail_ref"
    
    test_string = ""
    for i in range(25):
        test_string = "%s line%d\n" % (test_string,i)
    
    preload = "LD_PRELOAD=%s/lib/libofs.so:%s/lib/libpvfs2.so " % (testing_node.ofs_installation_location,testing_node.ofs_installation_location)
    testing_node.runSingleCommand("bash -c '%s %s > %s'" % (preload,test_string,tail_test))
   
    # now diff it
    rc = testing_node.runSingleCommand("bash -c '%s tail %s'" % (preload,tail_test),output)
    return rc

#------------------------------------------------------------------------------
#
#	usrint_cp()
#
#	This copies a file to OrangeFS mountpoint and back. Copied file should be
#	the same as the original.
#------------------------------------------------------------------------------    
    
    
def usrint_cp(testing_node,output=[]):
    filename = open(inspect.stack()[0][3]+".log","w")
    preload = "LD_PRELOAD=%s/lib/libofs.so:%s/lib/libpvfs2.so " % (testing_node.ofs_installation_location,testing_node.ofs_installation_location)
    testing_node.runSingleCommand("%s cp %s/bin/pvfs2-cp %s" % (preload,testing_node.ofs_installation_location,testing_node.ofs_mount_point))
    testing_node.runSingleCommand("%s cp %s/pvfs2-cp %s" % (preload,testing_node.ofs_mount_point,testing_node.ofs_installation_location))
    rc = testing_node.runSingleCommand("%s cmp %s/bin/pvfs2-cp %s/pvfs2-cp" % (preload,testing_node.ofs_installation_location,testing_node.ofs_installation_location),output)
    return rc


