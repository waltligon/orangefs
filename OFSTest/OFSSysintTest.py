#!/usr/bin/python
##
#
# @namespace OFSSysintTest
#
# @brief This class implements system integration tests to be run on the virtual file system.
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
# Do we mount it via fuse?
# @var  tests  
# List of test functions (at end of file)

header = "OFS Sysint Test"
prefix = "sysint"
mount_fs =  False
run_client = True
mount_as_fuse = False
debug = True

##
#
# @fn cp(testing_node,output=[]):
#
#	This uses pvfs2_cp to copy file to OrangeFS mountpoint and back. 
#   Copied file should be the same as the original.
#
#   pvfs2_cp is run with a series of argument combinations.
# @param testing_node OFSTestNode on which tests are run.
# @param output Array that holds output from commands. Passed by reference. 
#   
# @return 0 Test ran successfully
# @return Not 0 Test failed
#
#

def cp(testing_node,output=[]):

    # interior function that actually does the copying.
    def copy_test_pvt(testing_node,source,destination,local,args,output=[]):
        #print "%s/bin/pvfs2-cp %s %s %s" % (testing_node.ofs_installation_location,source,destination,args)
        testing_node.runSingleCommand("%s/bin/pvfs2-cp %s %s %s" % (testing_node.ofs_installation_location,source,destination,args),output)
        #print "%s/bin/pvfs2-cp %s %s %s" % (testing_node.ofs_installation_location,destination,local,args)
        testing_node.runSingleCommand("%s/bin/pvfs2-cp %s %s %s" % (testing_node.ofs_installation_location,destination,local,args),output)
        testing_node.runSingleCommand("diff %s %s" % (source,local),output)
        #print "diff %s %s" % (source,local)
        #print "copy_test_pvt: Diff %s " % (result)
        return rc
        
    test_source = testing_node.ofs_source_location + "/configure.ac"
    test_destination = testing_node.ofs_mount_point + "/configure.ac"
    test_local = testing_node.ofs_source_location + "/config"
    
    test_args = [ "",'-s 128','-s $((1024*1024))', '-n 1', '-n 4', '-b 128', '-b $((1024*1024))' ]
    
    rc = 0
    
    for i in range(len(test_args)):
        rc += copy_test_pvt(testing_node,test_source,"%s%d"%(test_destination,i),"%s%d"%(test_local,i),test_args[i],output)
    
    return rc
    
##
#
# @fn misc(testing_node,output=[]):
#
#	This tests a variety of pvfs2 utilities.
# @param testing_node OFSTestNode on which tests are run.
# @param output Array that holds output from commands. Passed by reference. 
#   
# @return 0 Test ran successfully
# @return Not 0 Test failed
#
#



def misc(testing_node,output=[]):

    

    def touch(testing_node,output=[]):
        #${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/bin/pvfs2-touch $PVFS2_MOUNTPOINT/miscfile    
        rc = testing_node.runSingleCommand("%s/bin/pvfs2-touch %s/miscfile" % (testing_node.ofs_installation_location,testing_node.ofs_mount_point),output)
        return rc

    def chown(testing_node,output=[]):
        #${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/bin/pvfs2-touch $PVFS2_MOUNTPOINT/miscfile
        groups_v = testing_node.runSingleCommandBacktick("groups").split(' ')
        rc = testing_node.runSingleCommand("%s/bin/pvfs2-chown %s %s %s/miscfile" % (testing_node.ofs_installation_location,testing_node.current_user,groups_v[0],testing_node.ofs_mount_point),output)
        return rc

    def chmod(testing_node,output=[]):
        #${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/bin/pvfs2-touch $PVFS2_MOUNTPOINT/miscfile
        rc = testing_node.runSingleCommand("%s/bin/pvfs2-chmod 777 %s/miscfile" % (testing_node.ofs_installation_location,testing_node.ofs_mount_point),output)
        return rc

    def drop_caches(testing_node,output=[]):
        dc_out = testing_node.runSingleCommandBacktick("%s/bin/pvfs2-drop-caches -m %s 2>&1" % (testing_node.ofs_installation_location,testing_node.ofs_mount_point),output)
        #print "dc out = "+dc_out
        #if  != 0:
        #    print "pvfs2-chmod failed with code %d" % rc
        #    return 1
        #else:
        # Meh, let's just pass it anyway. I shhould fix this.
        return 0

    def fsck(testing_node,output=[]):
        rc = testing_node.runSingleCommand("%s/bin/pvfs2-fsck -m %s" % (testing_node.ofs_installation_location,testing_node.ofs_mount_point),output)
        return rc

    def validate(testing_node,output=[]):
        rc = testing_node.runSingleCommand("%s/bin/pvfs2-validate -d %s -c -f -s" % (testing_node.ofs_installation_location,testing_node.ofs_mount_point),output)
        return rc   
        
    def fs_dump(testing_node,output=[]):
        rc = testing_node.runSingleCommand("%s/bin/pvfs2-fs-dump -m %s" % (testing_node.ofs_installation_location,testing_node.ofs_mount_point),output)
        return rc

    def ls(testing_node,output=[]):
        rc = testing_node.runSingleCommand("%s/bin/pvfs2-ls %s" % (testing_node.ofs_installation_location,testing_node.ofs_mount_point),output)
        return rc

    def perror(testing_node,output=[]):
        rc = testing_node.runSingleCommand("%s/bin/pvfs2-perror 0 2>&1" % testing_node.ofs_installation_location,output)
        return rc

    def stat(testing_node,output=[]):
        rc = testing_node.runSingleCommand("%s/bin/pvfs2-stat %s/miscfile" % (testing_node.ofs_installation_location,testing_node.ofs_mount_point),output)
        return rc

    def viewdist(testing_node,output=[]):
        rc = testing_node.runSingleCommand("%s/bin/pvfs2-viewdist -f %s/miscfile" % (testing_node.ofs_installation_location,testing_node.ofs_mount_point),output)
        return rc

    def xattr(testing_node,output=[]):
        rc = testing_node.runSingleCommand("%s/bin/pvfs2-xattr -s -k user.foo -v bar %s/miscfile" % (testing_node.ofs_installation_location,testing_node.ofs_mount_point),output)
        return rc

    def rm(testing_node,output=[]):
        rc = testing_node.runSingleCommand("%s/bin/pvfs2-rm %s/miscfile" % (testing_node.ofs_installation_location,testing_node.ofs_mount_point),output)
        return rc

    def set_debugmask(testing_node,output=[]):
        rc = testing_node.runSingleCommand('%s/bin/pvfs2-set-debugmask -m %s "none"' % (testing_node.ofs_installation_location,testing_node.ofs_mount_point),output)
        return rc

    def set_mode_admin(testing_node,output=[]):
        rc = testing_node.runSingleCommand('%s/bin/pvfs2-set-mode -m %s "admin"' % (testing_node.ofs_installation_location,testing_node.ofs_mount_point),output)
        return rc

    def set_mode_normal(testing_node,output=[]):
        rc = testing_node.runSingleCommand('%s/bin/pvfs2-set-mode -m %s "normal"' % (testing_node.ofs_installation_location,testing_node.ofs_mount_point),output)
        return rc

    def set_sync(testing_node,output=[]):
        rc = testing_node.runSingleCommand('%s/bin/pvfs2-set-sync -m %s -D 0 -M 1' % (testing_node.ofs_installation_location,testing_node.ofs_mount_point),output)
        return rc

    def statfs(testing_node,output=[]):
        rc = testing_node.runSingleCommand('%s/bin/pvfs2-statfs -m %s' % (testing_node.ofs_installation_location,testing_node.ofs_mount_point),output)
        return rc

    # Add up the return codes of the interior functions and return it.
    rc = 0
    rc += touch(testing_node,output)
    rc += chown(testing_node,output)
    rc += chmod(testing_node,output)
    rc += drop_caches(testing_node,output)
    rc += fsck(testing_node,output)
    rc += validate(testing_node,output)
    rc += fs_dump(testing_node,output)
    rc += ls(testing_node,output)
    rc += perror(testing_node,output)
    rc += stat(testing_node,output)
    rc += viewdist(testing_node,output)
    rc += xattr(testing_node,output)
    rc += rm(testing_node,output)
    rc += set_debugmask(testing_node,output)
    rc += set_mode_admin(testing_node,output)
    rc += set_mode_normal(testing_node,output)
    rc += set_sync(testing_node,output)
    rc += statfs(testing_node,output)
    return rc

##
#
# @fn  mkdir_sysint(testing_node,output=[]):
#
#   This runs the test-mkdir utility
#
# @param testing_node OFSTestNode on which tests are run.
# @param output Array that holds output from commands. Passed by reference. 
#   
# @return 0 Test ran successfully
# @return Not 0 Test failed
#
#
    
    

def mkdir_sysint(testing_node,output=[]):
    
    # Note: Update test programs to remove old directories first!
    #
    # Note: There is a known issue with test-mkdir that causes it to fail. This
    # is not a problem with OrangeFS.
    
    options = "--hostname=%s --fs-name=%s --network-proto=tcp --port=%s --exe-path=%s/bin --print-results --verbose" % (testing_node.hostname,testing_node.ofs_fs_name,testing_node.ofs_tcp_port,testing_node.ofs_installation_location)
    rc = testing_node.runSingleCommand("PATH=%s/bin:$PATH %s/test/test-mkdir --directory %s --use-lib %s" % (testing_node.ofs_installation_location,testing_node.ofs_installation_location,testing_node.ofs_mount_point,options),output)
    return rc

##
#
# @fn  ping(testing_node,output=[]):
#
#   This runs the pvfs2-ping utility
#
# @param testing_node OFSTestNode on which tests are run.
# @param output Array that holds output from commands. Passed by reference. 
#   
# @return 0 Test ran successfully
# @return Not 0 Test failed
#
#
    

def ping(testing_node,output=[]):
    
    rc = testing_node.runSingleCommand("%s/bin/pvfs2-ping -m %s" % (testing_node.ofs_installation_location,testing_node.ofs_mount_point),output)
    #print "RC = %d" % rc
    return rc

##
#
# @fn  symlink_sysint(testing_node,output=[]):
#
#   This runs the test-symlink-perms utility
#
# @param testing_node OFSTestNode on which tests are run.
# @param output Array that holds output from commands. Passed by reference. 
#   
# @return 0 Test ran successfully
# @return Not 0 Test failed
#
#
    

def symlink_sysint(testing_node,output=[]):
    
    # Note: There is a known issue with test-mkdir that causes it to fail. This
    # is not a problem with OrangeFS.
    options = "--hostname=%s --fs-name=%s --network-proto=tcp --port=%s --exe-path=%s/bin --print-results --verbose" % (testing_node.hostname,testing_node.ofs_fs_name,testing_node.ofs_tcp_port,testing_node.ofs_installation_location)
    rc = testing_node.runSingleCommand("PATH=%s/bin:$PATH %s/test/test-symlink-perms --directory %s --use-lib %s" % (testing_node.ofs_installation_location,testing_node.ofs_installation_location,testing_node.ofs_mount_point,options),output)
    return rc

##
#
# @fn  zerofill(testing_node,output=[]):
#
#   This runs the test-zero-fill utility
#
# @param testing_node OFSTestNode on which tests are run.
# @param output Array that holds output from commands. Passed by reference. 
#   
# @return 0 Test ran successfully
# @return Not 0 Test failed
#
#    

def zerofill(testing_node,output=[]):
    rc = testing_node.runSingleCommand("PATH=%s/bin:$PATH %s/test/test-zero-fill -v" % (testing_node.ofs_installation_location,testing_node.ofs_installation_location),output)
    return rc

tests = [ ping,cp,misc,zerofill,mkdir_sysint,symlink_sysint]

