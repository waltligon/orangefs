import inspect



def append(testing_node,output=[]):
    

    
    append_test = testing_node.ofs_mountpoint +"/append_test"
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

def append2(testing_node,output=[]):


    pvfs2_testdir = testing_node.ofs_mountpoint +"/append_dir"
    append_test = testing_node.ofs_mountpoint +"/append_test2"
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




def bonnie(testing_node,output=[]):

    
    rc = 0
    #make sure that the benchmarks have been installed
    if testing_node.ofs_extra_tests_location == "":
        testing_node.installBenchmarks()
    
    testing_node.changeDirectory("%s/bonnie++-1.03e" % testing_node.ofs_extra_tests_location)
    
    # check to see if we have already compiled dbench
    if testing_node.runSingleCommand( "[ -f %s/bonnie++-1.03e/bonnie++ ]" % testing_node.ofs_extra_tests_location):

        rc = testing_node.runSingleCommand("./configure",output)
        if rc != 0:
            return rc
        rc = testing_node.runSingleCommand("make",output)
        if rc != 0:
            return rc
        
    testing_node.changeDirectory(testing_node.ofs_mountpoint)
    if "ubuntu" in testing_node.distro.lower():
        rc = testing_node.runSingleCommand("echo \""+testing_node.ofs_extra_tests_location+"/bonnie++-1.03e/bonnie++  -n 1:0:0:1  -r 8 -s 16 \"",output)
    else:
        rc = testing_node.runSingleCommand(testing_node.ofs_extra_tests_location+"/bonnie++-1.03e/bonnie++  -n 1:0:0:1  -r 8 -s 16 ",output)
    

    return rc
    
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
        #if rc != 0:
        #    return rc
        
        rc = testing_node.runSingleCommand("patch -p3 < %s/test/automated/vfs-tests.d/dbench.patch" % testing_node.ofs_source_location,output)
        if rc != 0:
            return rc
        
        rc = testing_node.runSingleCommand("make",output)
        if rc != 0:
            return rc

    rc = testing_node.runSingleCommand("cp client.txt %s" % testing_node.ofs_mountpoint,output)
    if rc != 0:
        return rc
    
    
    testing_node.changeDirectory(testing_node.ofs_mountpoint)
    
    
    if "ubuntu" in testing_node.distro.lower():
        rc = testing_node.runSingleCommand("echo \""+testing_node.ofs_extra_tests_location+"/dbench-3.03/dbench -c client.txt 10 -t 300\"",output)
    else:
        rc = testing_node.runSingleCommand(testing_node.ofs_extra_tests_location+"/dbench-3.03/dbench -c client.txt 10 -t 300 ",output)
       
    return rc
    
def fdtree(testing_node,output=[]):

    
    rc = 0
    #make sure that the benchmarks have been installed
    if testing_node.ofs_extra_tests_location == "":
        testing_node.installBenchmarks()
    
    testing_node.changeDirectory(testing_node.ofs_mountpoint)
    rc = testing_node.runSingleCommand(testing_node.ofs_extra_tests_location+"/fdtree-1.0.1/fdtree.bash -l 4 -d 5",output)
    
    return rc
    
def fstest(testing_node,output=[]):

    
    testing_node.runSingleCommand("mkdir -p %s/fstest" % testing_node.ofs_mountpoint,output)
    
    if testing_node.runSingleCommand( "[ -f %s/fstest ]" % testing_node.ofs_source_location):
        rc = testing_node.runSingleCommand("gcc %s/test/automated/vfs-tests.d/fstest.c -o %s/fstest" % (testing_node.ofs_source_location,testing_node.ofs_source_location),output)
        if rc != 0:
            return rc
    
    rc = testing_node.runSingleCommand("%s/fstest -p %s/fstest" % (testing_node.ofs_source_location,testing_node.ofs_mountpoint),output)
        
    return rc

def fsx(testing_node,output=[]):

    
    testing_node.runSingleCommand("mkdir -p %s/fsx" % testing_node.ofs_mountpoint)
    
    # check to see if we have already compiled fsx
    if testing_node.runSingleCommand( "[ -f %s/fsx ]" % testing_node.ofs_source_location):

        rc = testing_node.runSingleCommand("gcc %s/test/automated/vfs-tests.d/fsx.c -o %s/fsx" % (testing_node.ofs_source_location,testing_node.ofs_source_location),output)
        if rc != 0:
            
            return rc
    
    rc = testing_node.runSingleCommand("%s/fsx -N 1000 -W -R %s/fsx_testfile" % (testing_node.ofs_source_location,testing_node.ofs_mountpoint),output)
    
    return rc

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
    
    rc = testing_node.runSingleCommand("./iozone -a -y 4096 -q $((1024*16)) -n 4096 -g $((1024*16*2)) -f %s/test_iozone_file" % testing_node.ofs_mountpoint,output)
        
    return rc
    
def ltp(testing_node,output=[]):

    
    LTP_ARCHIVE_VERSION = "ltp-full-20120903"
    LTP_ARCHIVE_TYPE = ".bz2"
    LTP_ARCHIVE = "%s%s" % (LTP_ARCHIVE_VERSION,LTP_ARCHIVE_TYPE)
    LTP_URL = "http://devorange.clemson.edu/pvfs"
    
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
        
        rc = testing_node.runSingleCommand("patch -p1 < %s/test/automated/vfs-tests.d/ltp-20120903-zoo-path.patch" % testing_node.ofs_source_location,output)
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
    
        
    testing_node.runSingleCommand("cp %s/test/automated/vfs-tests.d/ltp-pvfs-testcases runtest/" % testing_node.ofs_source_location)
    testing_node.runSingleCommand("cp %s/test/automated/vfs-tests.d/ltp-pvfs-testcases /tmp/ltp/runtest/" % testing_node.ofs_source_location)
    testing_node.runSingleCommand("mkdir -p %s/ltp-tmp" % testing_node.ofs_source_location)
    testing_node.runSingleCommand("chmod 777 %s/ltp-tmp" % testing_node.ofs_mountpoint)
    testing_node.runSingleCommand("umask 0")
    
    testing_node.changeDirectory('/tmp/ltp')
    
    print 'sudo ./runltp -p -l %s/ltp-pvfs-testcases.log -d %s/ltp-tmp -f ltp-pvfs-testcases -z %s/zoo.tmp >& %s/ltp-pvfs-testcases.output' % (testing_node.ofs_installation_location, testing_node.ofs_mountpoint,testing_node.ofs_extra_tests_location,testing_node.ofs_installation_location)    
    rc = testing_node.runSingleCommandAsBatch('sudo ./runltp -p -l %s/ltp-pvfs-testcases.log -d %s/ltp-tmp -f ltp-pvfs-testcases -z %s/zoo.tmp >& %s/ltp-pvfs-testcases.output' % (testing_node.ofs_installation_location, testing_node.ofs_mountpoint,testing_node.ofs_extra_tests_location,testing_node.ofs_installation_location),output)
    if rc != 0:
        return rc
    
    failrc = testing_node.runSingleCommand("grep FAIL %s/ltp-pvfs-testcases.log",output)
    testing_node.changeDirectory('~')

    if failrc == 0:
        # if grep returns O, then there were failures.
        return 1
    else:
        return 0
        
    
    return rc
    
    
    
#    FAILCOUNT=`grep FAIL ltp-pvfs-testcases-$THISDATE.log | wc -l`

    

def mkdir_vfs(testing_node,output=[]):

    options = "--hostname=%s --fs-name=%s --network-proto=tcp --port=%s --exe-path=%s/bin --print-results --verbose" % (testing_node.host_name,testing_node.ofs_fs_name,testing_node.ofs_tcp_port,testing_node.ofs_installation_location)
    rc = testing_node.runSingleCommand("PATH=%s/bin:$PATH %s/test/test-mkdir --directory %s %s" % (testing_node.ofs_installation_location,testing_node.ofs_installation_location,testing_node.ofs_mountpoint,options),output)
    return rc
    
def shelltest(testing_node,output=[]):

    
    
    #print testing_node.runSingleCommandBacktick("find /tmp -name pvfs2-shell-test.sh")
    #hack to workaround bug in pvfs2-shell-test.sh
    testing_node.changeDirectory("~")
    rc = testing_node.runSingleCommand("bash %s/test/kernel/linux-2.6/pvfs2-shell-test.sh %s 2>&1" % (testing_node.ofs_source_location,testing_node.ofs_mountpoint),output)
    return rc

def symlink_vfs(testing_node,output=[]):

    options = "--hostname=%s --fs-name=%s --network-proto=tcp --port=%s --exe-path=%s/bin --print-results --verbose" % (testing_node.host_name,testing_node.ofs_fs_name,testing_node.ofs_tcp_port,testing_node.ofs_installation_location)
    rc = testing_node.runSingleCommand("PATH=%s/bin:$PATH %s/test/test-symlink-perms --directory %s %s" % (testing_node.ofs_installation_location,testing_node.ofs_installation_location,testing_node.ofs_mountpoint,options),output)
    return rc
    
def tail(testing_node,output=[]):

    
    tail_test = testing_node.ofs_mountpoint +"/tail_test"
    local_reference = testing_node.ofs_installation_location + "/tail_ref"
    
    test_string = ""
    for i in range(25):
        test_string = "%s line%d\n" % (test_string,i)
    
    # use a batchfile to create the files. This avoids redirect confusion
    #testing_node.addBatchCommand("%s > %s" % (test_string,local_reference))
    testing_node.runSingleCommand("%s > %s" % (test_string,tail_test))
   
    # now diff it
    rc = testing_node.runSingleCommand("tail "+tail_test,output)
    return rc
    
    
def vfs_cp(testing_node,output=[]):


    filename = open(inspect.stack()[0][3]+".log","w")
    testing_node.runSingleCommand("cp %s/bin/pvfs2-cp %s" % (testing_node.ofs_installation_location,testing_node.ofs_mountpoint))
    testing_node.runSingleCommand("cp %s/pvfs2-cp %s" % (testing_node.ofs_mountpoint,testing_node.ofs_installation_location))
    rc = testing_node.runSingleCommand("cmp %s/bin/pvfs2-cp %s/pvfs2-cp" % (testing_node.ofs_installation_location,testing_node.ofs_installation_location),output)
    return rc

tests = [ append,
append2,
bonnie,
dbench,
fdtree,
fstest,
fsx,
iozone,
ltp,
mkdir_vfs,
shelltest,
symlink_vfs,
tail,
vfs_cp ]
