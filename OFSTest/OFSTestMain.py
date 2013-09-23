# This class drives the OrangeFS testing
#
#
# So, what do we need to do?
#
# 0. Determine OFSTestSpecifications
#   a. Specifications should be separate class.
#   b. Specification object can be created from config file or command line
#   input.
# 
# 1. Build the OFSTestNetwork
#   a. Create node for local machine.
#   b. Create nodes for existing remote machines, if applicable.
#   c. Create all EC2 machines, if applicable.
#
# 2. Download and build the OFS Software
#   a. Download and build OFS Software.
#   b. Copy OFSSoftware to other machines.
#   c. Download and build testing software.
#
# 3. Run OFSTests
#   a. Run tests on all client machines.
#   b. (Do we need to run them on all server machines?)
#
# 4. Analyze test results
#   a. Determine whether test passed or failed
#   b. Store results in dictionary [test/pass]
#   c. Report results to caller.
#
# 5. Cleanup
#   a. Take down all OrangeFS servers
#   b. Take down all OrangeFS clients
#   c. Delete all EC2 nodes
#


import OFSTestConfig
import OFSTestConfigFile
import OFSTestConfigMenu
import OFSTestConfigBuildbot
import OFSTestNetwork
import time
import sys
import traceback
from pprint import pprint

class OFSTestMain(object):
    
    def __init__(self,config):
        self.config = config
        self.ofs_network = OFSTestNetwork.OFSTestNetwork()

    def setConfig(self,**kwargs):
        self.config.setConfig(**kwargs)
    
    def printConfig(self):
        self.config.printDict()

    def writeOutput(self,filename,function,rc):
        output = open(filename,'a+')
        if rc != 0:
            output.write("%s........................................FAIL: RC = %r\n" % (function.__name__,rc))
        else:
            output.write("%s........................................PASS.\n" % function.__name__)
        output.close()
    
    def initNetwork(self):
    
        if len(self.config.node_ip_addresses) > 0:
            print "===========================================================" 
            print "Adding %d Existing Nodes to OFS cluster" % len(self.config.node_ip_addresses)

            for i in range(len(self.config.node_ip_addresses)):
                # do we have an external ip address
                if len(self.config.node_ext_ip_addresses) > 0:
                    ext_ip_address = self.config.node_ext_ip_addresses[i]
                else:
                    # if not, underlying functionality will use one IP for both.
                    ext_ip_address = None
                
                self.ofs_network.addRemoteNode(ip_address=self.config.node_ip_addresses[i],username=self.config.node_usernames[i],key=self.config.ssh_key_filepath,is_ec2=self.config.using_ec2,ext_ip_address=ext_ip_address)
        
        print "===========================================================" 
        print "Distributing SSH keys"
        
        self.ofs_network.uploadKeys()
        
        print "===========================================================" 
        print "Verifying hostname resolution"
        # make sure everyone can find each other
        self.ofs_network.updateEtcHosts()
    
    def checkOFS(self):
        
        # if we need to create new EC2 nodes, then OFS not setup
        if self.config.number_new_ec2_nodes > 0:        
            return 1;
        
        # initialize the network
        self.initNetwork();
       
    
        # TODO: Make this smart enough to detect if the installation is running.

        
        self.ofs_network.networkOFSSettings(
            ofs_installation_location = self.config.install_prefix,
            db4_prefix=self.config.db4_prefix,
            ofs_extra_tests_location=self.config.ofs_extra_tests_location,
            pvfs2tab_file=self.config.ofs_pvfs2tab_file,
            resource_location=self.config.ofs_resource_location,
            resource_type=self.config.ofs_resource_type,
            ofs_config_file=self.config.ofs_config_file,
            ofs_fs_name=self.config.ofs_fs_name,
            ofs_host_name_override=self.config.ofs_host_name_override,
            ofs_mount_point=self.config.ofs_mount_point
            )
        
        '''
        
        print ""
        print "==================================================================="
        print "Start OFS Server"
        
        rc = self.ofs_network.startOFSServers()

        print ""
        print "==================================================================="
        print "Start OFS Client"
        rc = self.ofs_network.startOFSClientAllNodes(security=self.config.ofs_security_mode)

        '''
        return 0
        

    def setupOFS(self):
        

        filename = "OFSTestsetup.log"

        
        self.ofs_network.addEC2Connection(self.config.ec2rc_sh,self.config.ec2_key_name,self.config.ssh_key_filepath)

        if self.config.number_new_ec2_nodes > 0:
            print "===========================================================" 
            print "Creating %d new EC2/OpenStack Nodes" % self.config.number_new_ec2_nodes

            #print " %d new EC2 nodes" % self.config.number_new_ec2_nodes
            self.ofs_network.createNewEC2Nodes(self.config.number_new_ec2_nodes,self.config.ec2_image,self.config.ec2_machine,self.config.ec2_associate_ip,self.config.ec2_domain)
        

        
        
        self.initNetwork()
        
    
        if self.config.using_ec2 == True:
            print ""
            print "==================================================================="
            print "Updating New Nodes (This may take awhile...)"
            self.ofs_network.updateEC2Nodes()
            

            print ""
            print "==================================================================="
            print "Installing Required Software"
            self.ofs_network.installRequiredSoftware()

            #Skip Torque install until mpi finished
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
        print "Downloading and building OrangeFS from %s resource %s" % (self.config.ofs_resource_type,self.config.ofs_resource_location)

        rc = self.ofs_network.buildOFSFromSource(
        resource_type=self.config.ofs_resource_type,
        resource_location=self.config.ofs_resource_location,
        build_kmod=self.config.ofs_build_kmod,
        enable_strict=self.config.enable_strict,
        enable_fuse=self.config.install_fuse,
        enable_shared=self.config.install_shared,
        ofs_prefix=self.config.install_prefix,
        db4_prefix=self.config.db4_prefix,
        ofs_patch_files=self.config.ofs_patch_files,
        configure_opts=self.config.configure_opts,
        security_mode=self.config.ofs_security_mode,
        debug=self.config.ofs_compile_debug
        )
        
        if rc != 0:
            print "Could not build OrangeFS. Aborting."
            return rc
        
        print ""
        print "==================================================================="
        print "Installing OrangeFS to "+self.config.install_prefix

        
        rc = self.ofs_network.installOFSBuild(install_opts=self.config.install_opts)
        if rc != 0:
            print "Could not install OrangeFS. Aborting."
            return rc
        
        if self.config.install_tests == True:
            print ""
            print "==================================================================="
            print "Installing OrangeFS Tests"


            rc = self.ofs_network.installOFSTests()
            if rc != 0:
                print "Could not install OrangeFS tests. Aborting."
                return rc

      
            print ""
            print "==================================================================="
            print "Installing Third-Party Benchmarks"


            rc = self.ofs_network.installBenchmarks()
            if rc != 0:
                print "Could not install third-party benchmarks. Aborting."
                return rc


        



        print ""
        print "==================================================================="
        print "Configure OrangeFS Server"

        rc = self.ofs_network.configureOFSServer(ofs_fs_name=self.config.ofs_fs_name,pvfs2genconfig_opts=self.config.pvfs2genconfig_opts,security=self.config.ofs_security_mode)
        if rc != 0:
            print "Could not configure OrangeFS servers. Aborting."
            return rc


        print ""
        print "==================================================================="
        print "Copy installation to all nodes"


        rc = self.ofs_network.copyOFSToNodeList()
        # should handle this with exceptions.

        if self.config.ofs_security_mode != None:
            if self.config.ofs_security_mode.lower() == "key":
                print ""
                print "==================================================================="
                print "Generating OrangeFS security keys"

                rc = self.ofs_network.generateOFSKeys()
            
        
        print ""
        print "==================================================================="
        print "Start OFS Server"
        
        rc = self.ofs_network.startOFSServers()
        # also need to handle error conditions
   

        
#        if self.config.ofs_mount_fuse == False:
        print ""
        print "==================================================================="
        print "Start OFS Client"
        rc = self.ofs_network.startOFSClientAllNodes(security=self.config.ofs_security_mode)

        


        
        #Mpich depends on pvfs2 and must be installed afterwards 
        '''
        print ""
        print "==================================================================="
        print "Install mpich2"
        self.ofs_network.installMpich2()
        print ""
        print "==================================================================="
        print "Setup MPI Environment"
        self.ofs_network.setupMPIEnvironment()
        print ""
        print "==================================================================="
        print "Setup PAV Conf"
        self.ofs_network.generatePAVConf()
        '''
        
        return 0

    def runTest(self):
        
        print "Printing dictionary of master node"
        pprint(self.ofs_network.created_nodes[0].__dict__)
        print ""
        print "==================================================================="
        print "Run Tests"
        

        # todo: Move this section to OFSTestNetwork
        
        filename = self.config.log_file
        
        import OFSSysintTest
        import OFSVFSTest
        import OFSUserintTest

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

            #print "Cleaning up "+head_node.ofs_mount_point
            #head_node.runSingleCommand("%s/bin/pvfs2-rm %s/*" % (head_node.ofs_installation_location,head_node.ofs_mount_point))
            #head_node.runSingleCommand("mkdir -p %s" % head_node.ofs_mount_point)

        if self.config.run_vfs_tests == True:
            output = open(filename,'a+')
            mount_type = "kmod"
            head_node.unmountOFSFilesystem()
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
                #print "Cleaning up "+head_node.ofs_mount_point
                #head_node.runSingleCommand("rm -rf %s/*" % head_node.ofs_mount_point)
                #head_node.runSingleCommand("mkdir -p %s" % head_node.ofs_mount_point)

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
                #head_node.unmountOFS()
                head_node.stopOFSClient()
                #for node in self.ofs_network.created_nodes:
                
                
                
                output = open(filename,'a+')
                output.write("Usrint Tests ==================================================\n")
                output.close()

                for callable in OFSUserintTest.tests:
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
                #print "Cleaning up "+head_node.ofs_mount_point
                #head_node.runSingleCommand("rm -rf %s/*" % head_node.ofs_mount_point)
                #head_node.runSingleCommand("mkdir -p %s" % head_node.ofs_mount_point)
                
   



        
        if self.config.run_fuse_tests == True:
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
                                                
                #print "Cleaning up "+head_node.ofs_mount_point
                #head_node.runSingleCommand("rm -rf %s/*" % head_node.ofs_mount_point)
                #head_node.runSingleCommand("mkdir -p %s" % head_node.ofs_mount_point)

            else:
                output.write("VFS Tests (%s) could not run. Mount failed.=======================\n" % mount_type)
                    
                output.close()
        
        
            

        if self.config.ec2_delete_after_test == True:
            print ""
            print "==================================================================="
            print "Terminating Nodes"
            self.ofs_network.terminateAllEC2Nodes()


