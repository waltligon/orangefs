#!/usr/bin/python
import OFSTestNode

def cp(testing_node,output=[]):

    def copy_test_pvt(testing_node,source,destination,local,args,output=[]):
        #print "%s/bin/pvfs2-cp %s %s %s" % (testing_node.ofs_installation_location,source,destination,args)
        testing_node.runSingleCommand("%s/bin/pvfs2-cp %s %s %s" % (testing_node.ofs_installation_location,source,destination,args))
        #print "%s/bin/pvfs2-cp %s %s %s" % (testing_node.ofs_installation_location,destination,local,args)
        testing_node.runSingleCommand("%s/bin/pvfs2-cp %s %s %s" % (testing_node.ofs_installation_location,destination,local,args))
        result = testing_node.runSingleCommandBacktick("diff %s %s" % (source,local))
        #print "diff %s %s" % (source,local)
        #print "copy_test_pvt: Diff %s " % (result)
        return rc
        
    test_source = testing_node.ofs_source_location + "/configure.in"
    test_destination = testing_node.ofs_mountpoint + "/configure.in"
    test_local = testing_node.ofs_source_location + "/config"
    
    test_args = [ "",'-s 128','-s $((1024*1024))', '-n 1', '-n 4', '-b 128', '-b $((1024*1024))' ]
    
    rc = 0
    
    for i in range(len(test_args)):
        rc += copy_test_pvt(testing_node,test_source,"%s%d"%(test_destination,i),"%s%d"%(test_local,i),test_args[i],output)
    
    return rc
    


def misc(testing_node,output=[]):

    

    def touch(testing_node,output=[]):
        #${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/bin/pvfs2-touch $PVFS2_MOUNTPOINT/miscfile    
        rc = testing_node.runSingleCommand("%s/bin/pvfs2-touch %s/miscfile" % (testing_node.ofs_installation_location,testing_node.ofs_mountpoint),output)
        return rc

    def chown(testing_node,output=[]):
        #${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/bin/pvfs2-touch $PVFS2_MOUNTPOINT/miscfile
        groups_v = testing_node.runSingleCommandBacktick("groups").split(' ')
        rc = testing_node.runSingleCommand("%s/bin/pvfs2-chown %s %s %s/miscfile" % (testing_node.ofs_installation_location,testing_node.current_user,groups_v[0],testing_node.ofs_mountpoint),output)
        return rc

    def chmod(testing_node,output=[]):
        #${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/bin/pvfs2-touch $PVFS2_MOUNTPOINT/miscfile
        rc = testing_node.runSingleCommand("%s/bin/pvfs2-chmod 777 %s/miscfile" % (testing_node.ofs_installation_location,testing_node.ofs_mountpoint),output)
        return rc

    def drop_caches(testing_node,output=[]):
        dc_out = testing_node.runSingleCommandBacktick("%s/bin/pvfs2-drop-caches -m %s 2>&1" % (testing_node.ofs_installation_location,testing_node.ofs_mountpoint),output)
        print "dc out = "+dc_out
        #if  != 0:
        #    print "pvfs2-chmod failed with code %d" % rc
        #    return 1
        #else:
        return 0

    def fsck(testing_node,output=[]):
        rc = testing_node.runSingleCommand("%s/bin/pvfs2-fsck -m %s" % (testing_node.ofs_installation_location,testing_node.ofs_mountpoint),output)
        return rc

    def validate(testing_node,output=[]):
        rc = testing_node.runSingleCommand("%s/bin/pvfs2-validate -d %s -c -f -s" % (testing_node.ofs_installation_location,testing_node.ofs_mountpoint),output)
        return rc   
        
    def fs_dump(testing_node,output=[]):
        rc = testing_node.runSingleCommand("%s/bin/pvfs2-fs-dump -m %s" % (testing_node.ofs_installation_location,testing_node.ofs_mountpoint),output)
        return rc

    def ls(testing_node,output=[]):
        rc = testing_node.runSingleCommand("%s/bin/pvfs2-ls %s" % (testing_node.ofs_installation_location,testing_node.ofs_mountpoint),output)
        return rc

    def perror(testing_node,output=[]):
        rc = testing_node.runSingleCommand("%s/bin/pvfs2-perror 0 2>&1" % testing_node.ofs_installation_location,output)
        return rc

    def stat(testing_node,output=[]):
        rc = testing_node.runSingleCommand("%s/bin/pvfs2-stat %s/miscfile" % (testing_node.ofs_installation_location,testing_node.ofs_mountpoint),output)
        return rc

    def viewdist(testing_node,output=[]):
        rc = testing_node.runSingleCommand("%s/bin/pvfs2-viewdist -f %s/miscfile" % (testing_node.ofs_installation_location,testing_node.ofs_mountpoint),output)
        return rc

    def xattr(testing_node,output=[]):
        rc = testing_node.runSingleCommand("%s/bin/pvfs2-xattr -s -k user.foo -v bar %s/miscfile" % (testing_node.ofs_installation_location,testing_node.ofs_mountpoint),output)
        return rc

    def rm(testing_node,output=[]):
        rc = testing_node.runSingleCommand("%s/bin/pvfs2-rm %s/miscfile" % (testing_node.ofs_installation_location,testing_node.ofs_mountpoint),output)
        return rc

    def set_debugmask(testing_node,output=[]):
        rc = testing_node.runSingleCommand('%s/bin/pvfs2-set-debugmask -m %s "none"' % (testing_node.ofs_installation_location,testing_node.ofs_mountpoint),output)
        return rc

    def set_mode_admin(testing_node,output=[]):
        rc = testing_node.runSingleCommand('%s/bin/pvfs2-set-mode -m %s "admin"' % (testing_node.ofs_installation_location,testing_node.ofs_mountpoint),output)
        return rc

    def set_mode_normal(testing_node,output=[]):
        rc = testing_node.runSingleCommand('%s/bin/pvfs2-set-mode -m %s "normal"' % (testing_node.ofs_installation_location,testing_node.ofs_mountpoint),output)
        return rc

    def set_sync(testing_node,output=[]):
        rc = testing_node.runSingleCommand('%s/bin/pvfs2-set-sync -m %s -D 0 -M 1' % (testing_node.ofs_installation_location,testing_node.ofs_mountpoint),output)
        return rc

    def statfs(testing_node,output=[]):
        rc = testing_node.runSingleCommand('%s/bin/pvfs2-statfs -m %s' % (testing_node.ofs_installation_location,testing_node.ofs_mountpoint),output)
        return rc

    
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

def mkdir_sysint(testing_node,output=[]):
    #note update test programs to remove old directories first!
    options = "--hostname=%s --fs-name=%s --network-proto=tcp --port=%s --exe-path=%s/bin --print-results --verbose" % (testing_node.host_name,testing_node.ofs_fs_name,testing_node.tcp_port,testing_node.ofs_installation_location)
    rc = testing_node.runSingleCommand("PATH=%s/bin:$PATH %s/test/test-mkdir --directory %s --use-lib %s" % (testing_node.ofs_installation_location,testing_node.ofs_installation_location,testing_node.ofs_mountpoint,options),output)
    return rc

def ping(testing_node,output=[]):
    
    rc = testing_node.runSingleCommand("%s/bin/pvfs2-ping -m %s" % (testing_node.ofs_installation_location,testing_node.ofs_mountpoint),output)
    #print "RC = %d" % rc
    return rc

def symlink_sysint(testing_node,output=[]):
    options = "--hostname=%s --fs-name=%s --network-proto=tcp --port=%s --exe-path=%s/bin --print-results --verbose" % (testing_node.host_name,testing_node.ofs_fs_name,testing_node.tcp_port,testing_node.ofs_installation_location)
    rc = testing_node.runSingleCommand("PATH=%s/bin:$PATH %s/test/test-symlink-perms --directory %s --use-lib %s" % (testing_node.ofs_installation_location,testing_node.ofs_installation_location,testing_node.ofs_mountpoint,options),output)
    return rc

def zerofill(testing_node,output=[]):
    rc = testing_node.runSingleCommand("PATH=%s/bin:$PATH %s/test/test-zero-fill -v" % (testing_node.ofs_installation_location,testing_node.ofs_installation_location),output)
    return rc
