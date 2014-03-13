# !/usr/bin/python
# 
# OFSSetup.py
#
# This class drives the OrangeFS testing. This sets up OrangeFS on the 
# OFSTestNetwork based on the OFSTestConfig.
#
#
# Members:
#
# OFSTestConfig self.config = Testing configuration. 
# OFSTestNework self.network = Virtual cluster.
#
# Methods:
#
#   setConfig()     Sets self.config based on info from the command line, 
#                   config file, etc.
#
#   printConfig()   Prints the dictionary of self.config. For debugging.
#
#   setupOFS()      Builds the OFSTestNetwork as specified by the config.
#
#   runTest()       Runs the tests for OrangeFS as specified by the config.
#


import OFSTestConfig
import OFSTestConfigFile
import OFSTestConfigMenu
import OFSTestConfigBuildbot
import OFSTestNetwork
import time
import sys
import traceback

class OFSSetup(object):

#------------------------------------------------------------------------------
#
# __init__()
#
# Members:
#
# OFSTestConfig self.config = Testing configuration. 
# OFSTestNework self.network = Virtual cluster.
#
#------------------------------------------------------------------------------
    
    def __init__(self,config):
        self.config = config
        self.ofs_network = OFSTestNetwork.OFSTestNetwork()

#------------------------------------------------------------------------------
#   setConfig()     Sets self.config based on info from the command line, 
#                   config file, etc.
#------------------------------------------------------------------------------

    def setConfig(self,**kwargs):
        self.config.setConfig(**kwargs)
    
#------------------------------------------------------------------------------
#   printConfig()   Prints the dictionary of self.config. For debugging.
#------------------------------------------------------------------------------
    def printConfig(self):
        self.config.printDict()
        
#------------------------------------------------------------------------------
#   setupOFS()      Builds the OFSTestNetwork as specified by the config.
#------------------------------------------------------------------------------
        
    def setupOFS(self):
        
        
        if self.config.using_cloud == True:
            # First, if we're using Cloud/Openstack, open the connection
            print "Connecting to Cloud/OpenStack"
            self.ofs_network.addCloudConnection(self.config.cloud_config,self.config.cloud_key_name,self.config.ssh_key_filepath)

            # if the configuration says that we need to create new Cloud nodes, 
            # do it.
            if self.config.number_new_cloud_nodes > 0:
                print "Testing with %d new Cloud nodes" % self.config.number_new_cloud_nodes
                self.ofs_network.createNewCloudNodes(self.config.number_new_cloud_nodes,self.config.cloud_image,self.config.cloud_machine,self.config.cloud_associate_ip,self.config.cloud_domain)
        
        # Add the information about the nodes (real and virtual/Cloud) to our 
        # OFSTestNetwork
        if len(self.config.node_ip_addresses) > 0:
            for i in range(len(self.config.node_ip_addresses)):
                self.ofs_network.addRemoteNode(ip_address=self.config.node_ip_addresses[i],username=self.config.node_usernames[i],key=self.config.ssh_key_filepath,is_cloud=self.config.using_cloud)

    
        if self.config.using_cloud == True:
            
            # Sometimes virtual networking doesn't do a good job of letting the
            # hosts find each other. This method sets standard hostnames and 
            # updates the /etc/hosts file ofeach virtual node so that everyone 
            # can find everyone else.
            
            self.ofs_network.updateEtcHosts()
        
        
        
    
        if self.config.update_nodes == True:
            # Install the latest software on the nodes and reboot.
            print ""
            print "==================================================================="
            print "Updating New Nodes"
            self.ofs_network.updateCloudNodes()
            
        
        if self.config.install_prereqs == True: 
            # Install software required to compile and run OFS and all tests.
            print ""
            print "==================================================================="
            print "Installing Required Software"
            self.ofs_network.installRequiredSoftware()

        ''' 
        if self.config.install_MPI == True:
            print ""
            print "==================================================================="
            print "Installing Torque" 
            self.ofs_network.installTorque()

            print ""
            print "==================================================================="

            print "Check Torque"
            self.ofs_network.checkTorque()

            print ""
            print "==================================================================="
            print "Install mpich2"
            self.ofs_network.installMpich2()
            '''

        if self.config.install_OrangeFS_server == True:

            print ""
            print "==================================================================="
            print "Downloading and Installing OrangeFS from %s resource %s" % (self.config.ofs_resource_type,self.config.ofs_resource_location)
 
            self.ofs_network.buildAndInstallOFSFromSource(resource_type=self.config.ofs_resource_type,resource_location=self.config.ofs_resource_location,configure_opts=self.config.configure_opts,build_kmod=self.config.ofs_build_kmod)

            print ""
            print "==================================================================="
            print "Configuring OrangeFS"

            self.ofs_network.configureOFSServer(ofs_fs_name=self.config.ofs_fs_name,pvfs2genconfig_opts=self.config.pvfs2genconfig_opts)            

            print ""
            print "==================================================================="
            print "Copy installation to all nodes"

            self.ofs_network.copyOFSToNodeList()

            print ""
            print "==================================================================="
            print "Start OFS Server"
            self.ofs_network.startOFSServers()
            
        if self.config.install_OrangeFS_client == True:
            print ""
            print "==================================================================="
            print "Start Client"
            self.ofs_network.startOFSClient()

        if self.config.install_testing_Benchmarks == True:
            # Install misc. testing benchmarks from devorange.clemson.edu
            print ""
            print "==================================================================="
            print "Installing Benchmarks"

            self.ofs_network.installBenchmarks()

        
        if self.config.mount_OFS_after_setup == True:
            # Testing normally mounts and unmounts as necessary, but if we are 
            # setting up an installation w/o testing it, we may need to mount 
            # the OFS share.
            print ""
            print "==================================================================="
            print "Mounting OrangeFS"
            self.ofs_network.mountOFSFilesystem(mount_fuse=self.config.ofs_mount_as_fuse)
            
        

        
        #Mpich depends on pvfs2 and must be installed afterwards 


    def runTest(self):
        print ""
        print "==================================================================="
        print "Run Tests"
        

        
        filename = self.config.log_file
        
        import OFSSysintTest
        import OFSVFSTest
        import OFSUsrintTest

        rc = 0

        head_node = self.ofs_network.created_nodes[0]
        head_node.setEnvironmentVariable("LD_LIBRARY_PATH","/opt/db4/lib:%s/lib" % head_node.ofs_installation_location)
        head_node.setEnvironmentVariable("LIBRARY_PATH","/opt/db4/lib:%s/lib" % head_node.ofs_installation_location)
    
        head_node.changeDirectory("~")

        output = open(filename,'w+')
        output.write("Running OrangeFS Tests ==================================================\n")
        output.close()


        if self.config.run_sysint_tests == True:
            output = open(filename,'a+')
            output.write("Sysint Tests ==================================================\n")
            output.close()

            for callable in OFSSysintTest.tests:
                #print "Running %s" % callable.__name__
                try:
                    rc = head_node.runOFSTest("sysint",callable)
                    self.writeOutput(filename,callable,rc)
                except:
                    print "Unexpected error:", sys.exc_info()[0]
                    traceback.print_exc()
                    pass

            print "Cleaning up "+head_node.ofs_mount_point
            head_node.runSingleCommand("%s/bin/pvfs2-rm %s/*" % (head_node.ofs_installation_location,head_node.ofs_mount_point))
            head_node.runSingleCommand("mkdir -p %s" % head_node.ofs_mount_point)

        if self.config.run_vfs_tests == True:
            output = open(filename,'a+')
            mount_type = "kmod"
            head_node.unmountOFS()
            head_node.mountOFSFilesystem(mount_fuse=False)
            rc = head_node.checkMount()
            
            if rc == 0:
                output.write("VFS Tests (%s) ==================================================\n" % mount_type)

                output.close()

                for callable in OFSVFSTest.tests:
                    try:
                        rc = head_node.runOFSTest("vfs-%s" % mount_type,callable)
                        self.writeOutput(filename,callable,rc)
                    except:
                        print "Unexpected error:", sys.exc_info()[0]
                        traceback.print_exc()
                        pass
                    #except AttributeError:
                    #    print "AttributeError running %s" % callable.__name__
                    #    pass
                    #    
                    #except TypeError:
                    #    print "AttributeError running %s" % callable.__name__
                    #    pass
                print "Cleaning up "+head_node.ofs_mount_point
                head_node.runSingleCommand("rm -rf %s/*" % head_node.ofs_mount_point)
                head_node.runSingleCommand("mkdir -p %s" % head_node.ofs_mount_point)

            else:
                output.write("VFS Tests (%s) could not run. Mount failed.=======================\n" % mount_type)
                    
                output.close()


        if self.config.run_usrint_tests == True:
            # stop the client and remove the kernel module before running usrint tests
            #if self.config.ofs_mount_fuse == True:
            if False == True:
                output = open(filename,'a+')
                output.write("Usrint Tests not compatible with fuse=====================================\n")
                output.close()
            else:
                head_node.unmountOFS()
                #head_node.stopOFSClient()
                #for node in self.ofs_network.created_nodes:
                
                
                
                output = open(filename,'a+')
                output.write("Usrint Tests ==================================================\n")
                output.close()

                for callable in OFSUsrintTest.tests:
                    try:
                        rc = head_node.runOFSTest("usrint",callable)
                        self.writeOutput(filename,callable,rc)
                    except:
                        print "Unexpected error:", sys.exc_info()[0]
                        traceback.print_exc()
                        pass
                    '''
                    except AttributeError:
                        pass
                    except TypeError:
                        pass
                    except:
                        pass
                    '''    
                print "Cleaning up "+head_node.ofs_mount_point
                head_node.runSingleCommand("rm -rf %s/*" % head_node.ofs_mount_point)
                head_node.runSingleCommand("mkdir -p %s" % head_node.ofs_mount_point)
                
   



        
        if self.config.ofs_mount_fuse == True:
            output = open(filename,'a+')
            head_node.stopOFSClient()
            head_node.mountOFSFilesystem(mount_fuse=True)
            rc = head_node.checkMount()
            
            mount_type = "fuse"

            if rc == 0:
                output.write("VFS Tests (%s) ==================================================\n" % mount_type)
                    
                output.close()
                
                

                for callable in OFSVFSTest.tests:
                    try:
                        rc = head_node.runOFSTest("vfs-%s" % mount_type,callable)
                        self.writeOutput(filename,callable,rc)
                    except:
                        print "Unexpected error:", sys.exc_info()[0]
                        traceback.print_exc()
                        pass
                                                
                print "Cleaning up "+head_node.ofs_mount_point
                head_node.runSingleCommand("rm -rf %s/*" % head_node.ofs_mount_point)
                head_node.runSingleCommand("mkdir -p %s" % head_node.ofs_mount_point)

            else:
                output.write("VFS Tests (%s) could not run. Mount failed.=======================\n" % mount_type)
                    
                output.close()
        
        
            

        if self.config.cloud_delete_after_test == True:
            print ""
            print "==================================================================="
            print "Terminating Nodes"
            self.ofs_network.terminateAllCloudNodes()


