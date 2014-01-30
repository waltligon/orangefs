#!/usr/bin/python
#
# OFSTestNetwork.py
#
# OFSTestNework is the class that forms the abstraction for the cluster. All operations on the cluster
# should be performed via OFSTestNetwork, not the OFSTestNodes.
#
# Every network must have the following:
#
# 1. At least one OFSTestNode in the created_nodes array.
# 2. A local_master, which represents the local machine from which the tests are run.
#
# EC2/OpenStack based virtual networks will also have an ec2_connection_manager.
#
#------------------------------------------------------------------------------

import os
import OFSTestNode
import OFSTestLocalNode 
import OFSTestRemoteNode 
import OFSEC2ConnectionManager
import Queue
import threading
import time
from pprint import pprint

class OFSTestNetwork(object):

#------------------------------------------------------------------------------
#
# __init__()
#
#    Initializes variables and creates local master.
#
#------------------------------------------------------------------------------

    def __init__(self):
    
        # Configuration for ec2 
        self.ec2_connection_manager = None
        # dictionary of instances
           
        self.created_nodes = []
        
        print "==================================================================="
        print "Checking Local machine"
        self.local_master = OFSTestLocalNode.OFSTestLocalNode()
        self.mpi_nfs_directory = ""
        self.openmpi_version = ""

        #self.created_nodes['127.0.0.1']=self.local_master
   
#------------------------------------------------------------------------------
#
#    findNode(ip_address,host_name)
#
#    Finds an OFSTestNode by ip address or hostname
#
#    Returns OFSTestNode if found, None, if not found.
#
#------------------------------------------------------------------------------
    def findNode(self,ip_address="",host_name=""):
    
        
        if ip_address != "":
            if ip_address == self.local_master.ip_address:
                return self.local_master
            else:
                return next((i for i in self.created_nodes if i.ip_address == ip_address), None) 
        elif host_name != "":
            print "Available host names"
            print [i.host_name for i in self.created_nodes]
            if host_name == self.local_master.host_name:
                return self.local_master
            else:
                return next((i for i in self.created_nodes if i.host_name == host_name), None) 
        else:
            return None
#------------------------------------------------------------------------------
#
#    addRemoteNode(username,ip_address,key,is_ec2=False,ext_ip_address=None)
#
#    Creates a new OFSTestNode and adds it to the created_nodes list.
#
#    Returns the OFSTestNode
#
#------------------------------------------------------------------------------
        
    def addRemoteNode(self,username,ip_address,key,is_ec2=False,ext_ip_address=None):
        #This function adds a remote node
        
        # Is this a remote machine or an existing ec2 node?
        remote_node = OFSTestRemoteNode.OFSTestRemoteNode(username=username,ip_address=ip_address,key=key,local_node=self.local_master,is_ec2=is_ec2,ext_ip_address=ext_ip_address)
                
        # Add to the node dictionary
        self.created_nodes.append(remote_node)
        
        # Return the new node
        return remote_node

#------------------------------------------------------------------------------
#
#    runSimultaneousCommands(self,node_list,node_function=OFSTestNode.OFSTestNode.runSingleCommand,args=[],kwargs={})
#
#    Runs a command on multiple nodes.
#
#    Parameters:
#                node_list = List of nodes to run command on 
#                node_function = Python function to run on all OFSTestNodes.
#                                Default is OFSTestNode.runSingleCommand
#                args[]        =    Arguments to Python node_function
#                kwargs{}    = Keyword args to Python node_function
#
#------------------------------------------------------------------------------        
        
    def runSimultaneousCommands(self,node_list=None,node_function=OFSTestNode.OFSTestNode.runSingleCommand,args=[],kwargs={}):
        
        #passes in a thread class that does the work on a node list with arguments args
        
        if node_list == None:
            node_list = self.created_nodes
         
        queue = Queue.Queue()
        class NodeThread(threading.Thread):

            def __init__(self, queue):
                threading.Thread.__init__(self)
                self.queue = queue
          
            def run(self):
                while True:
                    #grabs host from queue
                    #print "Queue length is %d" % self.queue.qsize()
                    node = self.queue.get()
                    
            
                    #runs the selected node function
                    if len(args) > 0:
                        rc = node_function(node,*args)
                    elif len(kwargs) > 0:
                        rc = node_function(node,**kwargs)
                    else:
                        rc = node_function(node)
                        
                    #signals to queue job is done
                    self.queue.task_done()
          
          
        start = time.time()
          
          
        #spawn a pool of threads, and pass them queue instance 
        #pool of threads will be the same number as the node list
        for n in node_list:
            t = NodeThread(queue)
            t.setDaemon(True)
            t.start()
              
        #populate queue with data   
        for node in node_list:
            queue.put(node)
           
        #wait on the queue until everything has been processed     
        queue.join()
          
#------------------------------------------------------------------------------
#
#    addEC2Connection(self,ec2_config_file,key_name,key_location)
#
#    add the EC2Connection
#
#    Parameters:
#                ec2_config_file = location of ec2rc.sh file 
#                key_name = Name of EC2 key to access node
#                key_location = Location of .pem file that contains the EC2 key
#
#------------------------------------------------------------------------------   
    
    
    def addEC2Connection(self,ec2_config_file,key_name,key_location):
        #This function initializes the ec2 connection
        self.ec2_connection_manager = OFSEC2ConnectionManager.OFSEC2ConnectionManager(ec2_config_file)
        self.ec2_connection_manager.setEC2Key(key_name,key_location)
        
#------------------------------------------------------------------------------
#
# createNewEC2Nodes(number_nodes,image_name,machine_type,associateip=False,domain=None):
#
# Creates new ec2 nodes and adds them to created_nodes list.
#
# Parameters
#    
#    number_nodes = number of nodes to be created
#    image_name = Name of EC2 image to launch
#    machine_type = EC2 "flavor" of virtual node
#    associateip = Associate to external ip?
#    domain = Domain to associate with external ip
#
# Return
#
#    Returns list of new nodes.
#------------------------------------------------------------------------------

    
    def createNewEC2Nodes(self,number_nodes,image_name,machine_type,associateip=False,domain=None):
        
        # This function creates number nodes on the ec2 system. 
        # It returns a list of nodes
        
        new_instances = self.ec2_connection_manager.createNewEC2Instances(number_nodes,image_name,machine_type)
        # new instances should have a 60 second delay to make sure everything is running.

        ip_addresses = []
        new_ofs_test_nodes = []
        
        for idx,instance in enumerate(new_instances):
            instance.update()
            #print "Instance %s at %s ext %s has state %s with code %r" % (instance.id,instance.ip_address,ip_addresses[idx],instance.state,instance.state_code)
            
            while instance.state_code == 0:
                
                time.sleep(10)
                instance.update()
                #print "Instance %s at %s ext %s has state %s with code %r" % (instance.id,instance.ip_address,ip_addresses[idx],instance.state,instance.state_code)
            
            
        
        # now that the instances are up, check the external ip
        if associateip == True:
            # if we need to associate an external ip address, do so
            ip_addresses = self.ec2_connection_manager.associateIPAddresses(new_instances,domain)
        else:
            #otherwise use the default internal address
            
            for i in new_instances:
                i.update()
                print "Instance %s using current IP %s" % (i.id,i.ip_address)
                #(i.__dict__)
                ip_addresses.append(i.ip_address)

        print "===========================================================" 
        print "Adding new nodes to OFS cluster"
 
        for idx,instance in enumerate(new_instances):
            # Create the node and get the instance name
            if "ubuntu" in image_name:
                name = 'ubuntu'
            elif "fedora" in image_name:
                # fedora 18 = ec2-user, fedora 19 = fedora
                
                # fedora 18 = ec2-user, fedora 19 = fedora
                name = 'fedora'
            else:
                name = 'ec2-user'
            
            new_node = OFSTestRemoteNode.OFSTestRemoteNode(username=name,ip_address=instance.ip_address,key=self.ec2_connection_manager.instance_key_location,local_node=self.local_master,is_ec2=True,ext_ip_address=ip_addresses[idx])

            new_ofs_test_nodes.append(new_node)
        
            # Add the node to the created nodes list.
            self.created_nodes.append(new_node)
        
        # return the list of newly created nodes.
        
        return new_ofs_test_nodes
    
#------------------------------------------------------------------------------
#
# uploadKeys(node_list=None)
#
# Upload ssh keys to the list of remote nodes
#
# Parameters:
#    node_list = list of nodes to upload the keys.
#
#------------------------------------------------------------------------------
 

    def uploadKeys(self,node_list=None):
        # if a list is not provided upload all keys
        if node_list == None:
            node_list = self.created_nodes
            
        for node in node_list:
            self.runSimultaneousCommands(node_list=node_list,node_function=OFSTestRemoteNode.OFSTestRemoteNode.uploadRemoteKeyFromLocal, args=[self.local_master,node.ext_ip_address])
        
#------------------------------------------------------------------------------
#      
# enablePasswordlessSSH(node_list=None):
#
# Enable passwordless SSH for the node for the current user.
#
#------------------------------------------------------------------------------
        

    def enablePasswordlessSSH(self,node_list=None):
        
        if node_list == None:
            node_list = self.created_nodes
        
        for src_node in node_list:
            # passwordless access to localhost
            src_node.runSingleCommand("/usr/bin/ssh-keyscan localhost >> /home/%s/.ssh/known_hosts" % src_node.current_user)
            src_node.runSingleCommand("/usr/bin/ssh-keyscan 127.0.0.1 >> /home/%s/.ssh/known_hosts" % src_node.current_user)
            
            for dest_node in node_list:
                # passwordless access to all other nodes
                print "Enabling passwordless SSH from %s to %s/%s/%s" % (src_node.host_name,dest_node.host_name,dest_node.ip_address,dest_node.ext_ip_address)
                src_node.runSingleCommand("/usr/bin/ssh-keyscan %s >> /home/%s/.ssh/known_hosts" % (dest_node.host_name,src_node.current_user))
                src_node.runSingleCommand("/usr/bin/ssh-keyscan %s >> /home/%s/.ssh/known_hosts" % (dest_node.ext_ip_address,src_node.current_user))
                src_node.runSingleCommand("/usr/bin/ssh-keyscan %s >> /home/%s/.ssh/known_hosts" % (dest_node.ip_address,src_node.current_user))
                
#------------------------------------------------------------------------------
#      
# terminateEC2Node(remote_node)
#
# Terminate the remote node and remove it from the created node list.
#
#------------------------------------------------------------------------------


    def terminateEC2Node(self,remote_node):
                
        if remote_node.is_ec2 == False: 
            print "Node at %s is not controlled by the ec2 manager." % remote_node.ip_address
            return
        
        rc = self.ec2_connection_manager.terminateEC2Instance(remote_node.ip_address)
        
        # if the node was terminated, remove it from the list.
        if rc == 0:
            self.created_nodes = [ x for x in self.created_nodes if x.ip_address != remote_node.ip_address]
        else:
            print "Could not delete node at %s, error code %d" % (remote_node.ip_address,rc)
            
        return rc

#------------------------------------------------------------------------------
#      
# updateEC2Nodes():
#
#    Update only the EC2 Nodes
#
#
#------------------------------------------------------------------------------
        
    def updateEC2Nodes(self,node_list=None):
        # This only updates the EC2 controlled nodes
         
        if node_list == None:
            node_list = self.created_nodes
        
        ec2_nodes = [node for node in self.created_nodes if node.is_ec2 == True]
        self.updateNodes(ec2_nodes)   

#------------------------------------------------------------------------------
# updateEtcHosts(node_list)
#
# This function updates the etc hosts file on each node with the hostname and ip
# address. Also creates the necessary mpihosts config files.
#
# This function updates the etc hosts file on each node with the
#------------------------------------------------------------------------------

    def updateEtcHosts(self,node_list=None):
        
        #This function updates the etc hosts file on each node with the 
        if node_list == None:
            node_list = self.created_nodes
        
 
        for node in node_list:
            node.created_openmpihosts = "~/openmpihosts"
            node.created_mpichhosts = "~/mpichhosts"
            for n2 in node_list:
                # can we ping the node?
                #print "Pinging %s from local node" % n2.host_name
                rc = node.runSingleCommand("ping -c 1 %s" % n2.host_name)
                # if not, add to the /etc/hosts file
                if rc != 0:
                    print "Could not ping %s at %s from %s. Manually adding to /etc/hosts" % (n2.host_name,n2.ip_address,n2.host_name)
                    node.addBatchCommand('sudo bash -c \'echo -e "%s     %s     %s" >> /etc/hosts\'' % (n2.ip_address,n2.host_name,n2.host_name))
                    # also create mpihosts files
                    node.runSingleCommand('echo "%s   slots=2" >> %s' % (n2.host_name,node.created_openmpihosts))
                    node.runSingleCommand('echo "%s:2" >> %s' % (n2.host_name,node.created_mpichhosts))

                    
            node.runAllBatchCommands()

        node.host_name = node.runSingleCommandBacktick("hostname")

#------------------------------------------------------------------------------
# updateNodes(node_list)
#
# This updates the system software on the list of nodes.
#
#------------------------------------------------------------------------------
     
    def updateNodes(self,node_list=None):
        if node_list == None:
            node_list = self.created_nodes
            
        # Run updateNode on the nodes simultaneously. 
        self.runSimultaneousCommands(node_list=node_list,node_function=OFSTestNode.OFSTestNode.updateNode)
        # Wait for reboot
        print "Waiting 90 seconds for nodes to reboot"
        time.sleep(90)
        # workaround for strange cuer1 issue where hostname changes on reboot.
        for node in node_list:
            tmp_host_name = node.runSingleCommandBacktick("hostname")
            if tmp_host_name != node.host_name:
                print "Hostname changed from %s to %s! Resetting to %s" % (node.host_name,tmp_host_name,node.host_name)
                node.runSingleCommandAsBatch("sudo hostname %s" % node.host_name)

#------------------------------------------------------------------------------
# installRequiredSoftware(self,node_list=None):
#
# This installs the required software on all the nodes
#
#------------------------------------------------------------------------------                
        
    
    def installRequiredSoftware(self,node_list=None):
        if node_list == None:
            node_list = self.created_nodes
        self.runSimultaneousCommands(node_list=node_list,node_function=OFSTestNode.OFSTestNode.installRequiredSoftware)
    
#------------------------------------------------------------------------------
# buildOFSFromSource(
#         resource_type,
#         resource_location,
#         download_location=None,
#         build_node=None,
#         build_kmod=True,
#         enable_strict=False,
#         enable_fuse=False,
#         enable_shared=False,
#         enable_hadoop=False,
#         ofs_prefix="/opt/orangefs",
#         db4_prefix="/opt/db4",
#         security_mode=None,
#         ofs_patch_files=[],
#         configure_opts="",
#         make_opts="",
#         debug=False):
#
#
# This builds OrangeFS on the build node
#
# Parameters
#
#    resource_type: What form is the source tree? (SVN,TAR,LOCAL) etc.
#    resource_location: Where is the source located?
#    download_location: Where should the source be downloaded to?
#    build_node:    On which node should OFS be build (default first)
#    build_kmod:    Build kernel module
#    enable_strict:    Enable strict option
#    enable_fuse:    Build fuse module
#    enable_shared:     Build shared libraries
#    enable_hadoop:    Build hadoop support
#    ofs_prefix:    Where should OFS be installed?
#    db4_prefix:    Where is db4 located?
#    security_mode:    None, Cert, Key
#    ofs_patch_files: Location of OrangeFS patches.
#    configure_opts:    Additional configure options
#    make_opts:        Make options
#
#------------------------------------------------------------------------------      

    def buildOFSFromSource(self,
        resource_type,
        resource_location,
        download_location=None,
        build_node=None,
        build_kmod=True,
        enable_strict=False,
        enable_fuse=False,
        enable_shared=False,
        enable_hadoop=False,
        ofs_prefix="/opt/orangefs",
        db4_prefix="/opt/db4",
        security_mode=None,
        ofs_patch_files=[],
        configure_opts="",
        make_opts="",
        debug=False):
        
        output = []
        dir_list = ""
        
        print "Resource type is "+resource_type+" location is "+resource_location
        
        if build_node == None:
            build_node = self.created_nodes[0]
            
        if download_location == None:
            download_location = "/home/%s/" % build_node.current_user
        
        if resource_type == "LOCAL":
            # copy from the local to the buildnode, then pretend it is a "buildnode" resource.
            # shameless hack.
            #print "Local resource"
            # get the basename from the resource location
            dir_list = os.path.basename(resource_location)
            #print dir_list
            # add the directory to the download location
            #download_location = "%s%s" % (download_location,dir_list)
            # create the destination directory
            #rc = build_node.runSingleCommand("mkdir -p "+download_location)
            #print "Copying source from %s to %s" % (resource_location,build_node.host_name)
            rc = self.local_master.copyToRemoteNode(resource_location,build_node,download_location,True)
            if rc != 0:
                print "Could not copy source from %s to %s" % (resource_location,build_node.host_name)
                return rc
            #rc = build_node.runSingleCommand("ls -l "+download_location+dir_list,output)
            #print output
            
            #print "Calling build_node.copyOFSSource(%s,%s,%s)" %(resource_type,resource_location,download_location+dir_list)
        
            resource_type = "BUILDNODE"
            resource_location = download_location+dir_list
        rc = build_node.copyOFSSource(resource_type,resource_location,download_location+dir_list)
        
        if rc != 0:
            return rc

        for patch in ofs_patch_files:
            
            
            rc = self.local_master.copyToRemoteNode(patch,build_node,patch,False)
            if rc != 0:
                print "Could not upload patch at %s to buildnode %s" % (patch,build_node.host_name)


        rc = build_node.configureOFSSource(build_kmod=build_kmod,enable_strict=enable_strict,enable_shared=enable_shared,ofs_prefix=ofs_prefix,db4_prefix=db4_prefix,ofs_patch_files=ofs_patch_files,configure_opts=configure_opts,security_mode=security_mode,enable_hadoop=enable_hadoop)
        if rc != 0:
            return rc
        
        rc = build_node.makeOFSSource(make_opts)
        if rc != 0:
            return rc
        
        return rc

#------------------------------------------------------------------------------   
#    
#    installOFSBuild(build_node=None,install_opts=""):
#
#    Install the OrangeFS build on a given node
#------------------------------------------------------------------------------       
    
    def installOFSBuild(self,build_node=None,install_opts=""):
        if build_node == None:
            build_node = self.created_nodes[0]
        return build_node.installOFSSource(install_opts)
    
#------------------------------------------------------------------------------   
#    
#    installOFSTests(build_node=None,install_opts=""):
#
#    Install the OrangeFS tests in the source code on a given node
#------------------------------------------------------------------------------ 

    def installOFSTests(self,client_node=None,configure_options=""):
        if client_node == None:
            client_node = self.created_nodes[0]
        return client_node.installOFSTests(configure_options)

#------------------------------------------------------------------------------   
#    
#    installBenchmarks(build_node=None):
#
#    Install the 3rd party benchmarks on the given node.
#------------------------------------------------------------------------------ 

    def installBenchmarks(self,build_node=None):
        if build_node == None:
            build_node = self.created_nodes[0]
        return build_node.installBenchmarks("http://devorange.clemson.edu/pvfs/benchmarks-20121017.tar.gz","/home/%s/benchmarks" % build_node.current_user)

#------------------------------------------------------------------------------   
#    
#    configureOFSServer(self,ofs_fs_name,master_node=None,node_list=None,pvfs2genconfig_opts="",security=None):
#
#    Parameters:
#
#    ofs_fs_name    Default name of OrangeFS service: version < 2.9 = "pvfs2-fs"; version >= 2.9 = "orangefs"
#    master_node    Master node in the cluster.
#    node_list      List of nodes in OrangeFS cluster
#    pvfs2genconfig_opts = Additional options for pvfs2genconfig utility
#    security        None, "Key", "Cert"
#------------------------------------------------------------------------------ 
 
       
    def configureOFSServer(self,ofs_fs_name,master_node=None,node_list=None,pvfs2genconfig_opts="",security=None):
        if node_list == None:
            node_list = self.created_nodes
        if master_node == None:
            master_node = node_list[0]
        return master_node.configureOFSServer(ofs_hosts_v=node_list,ofs_fs_name=ofs_fs_name,configuration_options=pvfs2genconfig_opts,security=security)

#------------------------------------------------------------------------------   
#    
#    copyOFSToNodeList(destination_list=None)
#
#    Copy OFS from build node to rest of cluster.
#------------------------------------------------------------------------------         
        
    def copyOFSToNodeList(self,destination_list=None):
        if destination_list == None:
            destination_list = self.created_nodes;
        self.copyResourceToNodeList(node_function=OFSTestNode.OFSTestNode.copyOFSInstallationToNode,destination_list=destination_list)

#------------------------------------------------------------------------------   
#    
#    copyOFSResourceToNodeList(node_function,destination_list=None)
#
# This is a multi-threaded recursive copy routine.
# Function copies OrangeFS from list[0] to list[len/2]
# Then it partitions the list into two parts and copies again. This copies at
# O(log n) time.
#
# For an eight node setup [0:7], copy is as follows:
# 1: 0->4
# 2: 0->2 4->6
# 3: 0->1 2->3 4->5 6->7
#
# Parameters:
#    node_function    Python function/method that does copying
#    destination_list    list of nodes. Assumption is source is at node[0].
#------------------------------------------------------------------------------    

    def copyResourceToNodeList(self,node_function,destination_list=None):

        
        if destination_list == None:
            destination_list = self.created_nodes
            
        # This assumes that the OFS installation is at the destination_list[0]
        list_length = len(destination_list)
        
        # If our list is of length 1 or less, return.
        if list_length <= 1:
            return 
        
        #copy from list[0] to list[list_length/2]
        print "Copying from %s to %s" % (destination_list[0].ip_address,destination_list[list_length/2].ip_address)
        #rc = destination_list[0].copyOFSInstallationToNode(destination_list[list_length/2])
        rc = node_function(destination_list[0],destination_list[list_length/2])
        
        
        # TODO: Throw an exception if the copy fails.
       
        queue = Queue.Queue()
        class CopyThread(threading.Thread):

            def __init__(self, queue,manager):
                threading.Thread.__init__(self)
                self.queue = queue
                self.manager = manager
          
            def run(self):
                while True:
                    #grabs host from queue
                    #print "Queue length is %d" % self.queue.qsize()
                    list = self.queue.get()
                    
                    #print "Copying %r" % list
                    self.manager.copyResourceToNodeList(node_function=node_function,destination_list=list)
                    
                        
                    #signals to queue job is done
                    self.queue.task_done()
          
          
        start = time.time()
          
          
        #spawn two threads to partition the list.
        for n in range(2):
            t = CopyThread(queue,self)
            t.setDaemon(True)
            t.start()
              
        #populate queue with data   
        queue.put(destination_list[:list_length/2])
        queue.put(destination_list[list_length/2:])
           
        #wait on the queue until everything has been processed     
        queue.join()
    
#------------------------------------------------------------------------------   
#    
#    stopOFSServers(node_list):
#
#    Stops the OrangeFS servers on the nodes
#------------------------------------------------------------------------------     
          
    def stopOFSServers(self,node_list=None):
        if node_list == None:
            node_list = self.created_nodes
        self.runSimultaneousCommands(node_list=node_list,node_function=OFSTestNode.OFSTestNode.stopOFSServer)
        time.sleep(20)

#------------------------------------------------------------------------------   
#    
#    startOFSServers(node_list):
#
#    Starts the OrangeFS servers on the nodes
#------------------------------------------------------------------------------     

    def startOFSServers(self,node_list=None):
        if node_list == None:
            node_list = self.created_nodes
        self.runSimultaneousCommands(node_list=node_list,node_function=OFSTestNode.OFSTestNode.startOFSServer)
        time.sleep(20)

#------------------------------------------------------------------------------   
#    
#    startOFSClientAllNodes(node_list, security):
#
#    Starts the OrangeFS servers on all created nodes
#------------------------------------------------------------------------------     

    def startOFSClientAllNodes(self,security=None,node_list=None):
        if node_list == None:
            node_list = self.created_nodes
        for node in node_list:
            self.startOFSClient(node,security)
#------------------------------------------------------------------------------   
#    
#    startOFSClient(client_node, security):
#
#    Starts the OrangeFS servers on one node
#------------------------------------------------------------------------------
            
    def startOFSClient(self,client_node=None,security=None):
        if client_node == None:
            client_node = self.created_nodes[0]
        client_node.installKernelModule()
        #client_node.runSingleCommand('/sbin/lsmod | grep pvfs')
        client_node.startOFSClient(security=security)


        time.sleep(10)
        #client_node.runSingleCommand('ps aux | grep pvfs')

#------------------------------------------------------------------------------   
#    
#    mountOFSFilesystem(mount_fuse,client_node):
#
#    Mount OrangeFS on a given client node.
#------------------------------------------------------------------------------

    def mountOFSFilesystem(self,mount_fuse=False,client_node=None,):
        if client_node == None:
            client_node = self.created_nodes[0]
        client_node.mountOFSFilesystem(mount_fuse=mount_fuse)
        time.sleep(10)
        print "Checking mount"
        mount_res=client_node.runSingleCommandBacktick("mount | grep -i pvfs")
        print mount_res
        #client_node.runSingleCommand("touch %s/myfile" % client_node.ofs_mount_point)
        #client_node.runSingleCommand("ls %s/myfile" % client_node.ofs_mount_point)

#------------------------------------------------------------------------------   
#    
#    mountOFSFilesystemAllNodes(mount_fuse):
#
#    Mount OrangeFS on a given client node.
#------------------------------------------------------------------------------
    def mountOFSFilesystemAllNodes(self,mount_fuse=False,node_list=None):
        
        # TODO: make this multithreaded
        for node in self.created_nodes:
            self.mountOFSFilesystem(mount_fuse=mount_fuse,client_node=node)
#------------------------------------------------------------------------------   
#    
#    stopOFSClient(client_node=None):
#
#    Stop the OrangeFS client on a given node
#------------------------------------------------------------------------------
        
    def stopOFSClient(self,client_node=None):
        if client_node == None:
            client_node = self.created_nodes[0]
        client_node.stopOFSClient()
        
#------------------------------------------------------------------------------   
#    
#    stopOFSClientAllNodes(node_list=None):
#
#    Stop the OrangeFS client on all nodes in list
#------------------------------------------------------------------------------

    def stopOFSClientAllNodes(self,node_list=None):
        if node_list == None:
            node_list = self.created_nodes
        # TODO: make this multithreaded
        for node in node_list:
            self.stopOFSClient(node)

#------------------------------------------------------------------------------   
#    
#    unmountOFSFilesystemAllNodes(node_list=None):
#
#    Unmount the OrangeFS directory on all nodes in list
#------------------------------------------------------------------------------
    
    def unmountOFSFilesystemAllNodes(self,node_list=None):
        if node_list == None:
            node_list = self.created_nodes
        # TODO: make this multithreaded
        for node in node_list:
            self.unmountOFSFilesystem(node)

#------------------------------------------------------------------------------   
#    
#    terminateAllEC2Nodes(node_list=None):
#
#    Terminate all the ec2 nodes in a list
#------------------------------------------------------------------------------

    def terminateAllEC2Nodes(self, node_list=None):
        if node_list == None:
            node_list = self.created_nodes
        for node in node_list:
            if node.is_ec2 == True:
                self.terminateEC2Node(node)

#------------------------------------------------------------------------------   
#    
#    installTorque(node_list=None, head_node=None):
#
#    Install Torque on the virtual cluster
#------------------------------------------------------------------------------                
    
 
    def installTorque(self, node_list=None, head_node=None):
        
        if node_list == None:
            node_list = self.created_nodes
        
        if head_node == None:
            head_node = node_list[0]
        # first install torque on the head node.
        head_node.installTorqueServer()
        
        
        #now add all the client nodes
        
        for i in range(len(node_list)):
                node_list[i].installTorqueClient(head_node)
                
                torque_node_cpu = node_list[i].runSingleCommandBacktick('grep proc /proc/cpuinfo | wc -l')
                if "ubuntu" in head_node.distro.lower() or "mint" in head_node.distro.lower() or "debian" in head_node.distro.lower():
                    head_node.runSingleCommand("sudo bash -c 'echo \"%s np=%s\" >> /var/spool/torque/server_priv/nodes'" % (node_list[i].host_name,torque_node_cpu))
                elif "centos" in head_node.distro.lower() or "scientific linux" in head_node.distro.lower() or "redhat" in head_node.distro.lower() or "fedora" in head_node.distro.lower():
                    head_node.runSingleCommandAsBatch("sudo bash -c 'echo \"%s np=%s\" >> /var/lib/torque/server_priv/nodes'" % (node_list[i].host_name,torque_node_cpu))
        head_node.restartTorqueServer()    

        # wait for the server to start
        time.sleep(15)
        
        down_host_names = self.checkTorque()
        print "Down torque nodes: "
        print down_host_names
        retries = 0

        try:
            while len(down_host_names) > 0 and retries < 5:
                for host_name in down_host_names:
                    # if a node is down, try restarting Torque Mom. Not sure why this happens.
                    node = self.findNode(host_name=host_name.rstrip())
                    node.restartTorqueMom()
                
                #wait for all clients to start
                time.sleep(15)
                down_host_names = self.checkTorque()
                print "Down torque nodes: "
                print down_host_names
                retries = retries + 1
        # break on an AttributeErrors if node isn't found
        except AttributeError:
            pass
    
#------------------------------------------------------------------------------   
#    
#    checkTorque(node_list=None, head_node=None):
#
#    Check to see if Torque is running properly.
#------------------------------------------------------------------------------      
            
    def checkTorque(self, node_list=None, head_node=None):
        
        if node_list == None:
            node_list = self.created_nodes
        
        if head_node == None:
            head_node = node_list[0]
        
        #pbsnodeout = head_node.runSingleCommandBacktick("pbsnodes")
        #print pbsnodeout
        pbsnodeout = head_node.runSingleCommandBacktick("pbsnodes -l | grep down | awk '{print \$1}'")
        #print pbsnodeout
        return pbsnodeout.split("\n")
    
#     MPI support - Not used.
#             
#     # makePBSScript
#     # This turns a command into a pbs script
#     def makePBSScript(self,command):
#     
#         script = '''#!/bin/bash
#         #PBS -l walltime=0:10:0
#         #PBS -l nodes=%d
#         #PBS -j oe
#         #PBS -q shared
#         nprocs=%d
# 
#         PATH=%s:/bin:$PATH
# 
#         mpdboot --file=/home/%s/mpd.hosts --totalnum=%d
# 
#         mpiexec -np %d %s
#         mpdallexit
#         ''' % (len(self.created_nodes),len(self.created_nodes),self.created_nodes[0].mpich2_installation_location,self.created_nodes[0].current_user,len(self.created_nodes),len(self.created_nodes),command)
#         
#         print script
#         return script
# 
#     
#         
#     def blockPBSUntilDone(self,jobid):
#         
#         rc = self.created_nodes[0].runSingleCommand("qstat -i %s" % jobid)
# 
#         while rc == 0:
#             time.sleep(60)
#             rc = self.created_nodes[0].runSingleCommand("qstat -i %s" % jobid)
#             
#     
#     def setupMPIEnvironment(self,headnode=None):
#         
#         if headnode==None:
#             headnode = self.created_nodes[0]
#     
#         headnode.setupMPIEnvironment()

#------------------------------------------------------------------------------   
#    
#    installMpich2(node_list=None, head_node=None):
#
#    Install Mpich on the virtual cluster
#------------------------------------------------------------------------------   
     
     
    def installMpich2(self,node_list=None,head_node=None):
        # TODO: Make this match installOpenMPI()         
        
        if node_list == None:
            node_list = self.created_nodes
        if head_node==None:
            head_node = node_list[0]
    
        head_node.installMpich2()

#------------------------------------------------------------------------------   
#    
#    generateOFSKeys()
#
#    Generate SSH keys for OrangeFS security
#------------------------------------------------------------------------------   
    
    def generateOFSKeys(self,node_list=None,head_node=None):
        if node_list == None:
            node_list = self.created_nodes
        if head_node==None:
            head_node = node_list[0]
            
        rc = self.generateOFSServerKeys(node_list,head_node)
        if rc != 0:
            return rc
        rc = self.generateOFSClientKeys(node_list,head_node)
        if rc != 0:
            return rc
        rc = self.generateOFSKeystoreFile(node_list,head_node)
        if rc != 0:
            return rc
        return 0

#------------------------------------------------------------------------------   
#    
#    generateOFSServerKeys()
#
#    Generate server SSH keys for OrangeFS security
#------------------------------------------------------------------------------    
    
    def generateOFSServerKeys(self,node_list=None,security_node=None):
        if node_list == None:
            node_list = self.created_nodes
        if security_node==None:
            security_node = node_list[0]
        
        #for each server, create a private server key at <orangefs>/etc/orangefs-serverkey.pem
        #cd /tmp/$USER/keys
        #openssl genrsa -out orangefs-serverkey-(remote host_name).pem 2048
        #copy  orangefs-serverkey-(remote_hostname).pem <orangefs>/etc/
        output = []
       
        
        security_node.runSingleCommand("mkdir -p /tmp/%s/security" % security_node.current_user)
        security_node.changeDirectory("/tmp/%s/security" % security_node.current_user)
        
        for node in self.created_nodes:
            keyname = "orangefs-serverkey-%s.pem" % node.host_name
            rc = security_node.runSingleCommand("openssl genrsa -out %s 2048" % keyname,output)
            if rc != 0:
                print "Could not create server security key for "+node.host_name
                print output 
                return rc
            rc = security_node.copyToRemoteNode(source="/tmp/%s/security/%s" % (security_node.current_user,keyname), destinationNode=node, destination="%s/etc/orangefs-serverkey.pem" % node.ofs_installation_location, recursive=False)
            if rc != 0:
                print "Could not copy server security key %s for %s " % (keyname,node.host_name)
                print output 
                return rc
            
            
            #rc = node.runSingleCommand("chown 
        


        return 0

#------------------------------------------------------------------------------   
#    
#    generateOFSClientKeys()
#
#    Generate client SSH keys for OrangeFS security
#------------------------------------------------------------------------------   
    
    def generateOFSClientKeys(self,node_list=None,security_node=None):
        if node_list == None:
            node_list = self.created_nodes
        if security_node==None:
            security_node = node_list[0]

        # cd /tmp/$USER/keys
        # for each client
        #   openSSl genrsa -out pvfs2-clientkey-{hostname}.pem 1024
        #   copy pvfs2-clientkey-{hostname).pem hostname:{orangefs}/etc
                
        
        output = []
       
        
        security_node.runSingleCommand("mkdir -p /tmp/%s/security" % security_node.current_user)
        security_node.changeDirectory("/tmp/%s/security" % security_node.current_user)

        for node in self.created_nodes:
            keyname = "pvfs2-clientkey-%s.pem" % node.host_name
            rc = security_node.runSingleCommand("openssl genrsa -out %s 1024" % keyname,output)
            if rc != 0:
                print "Could not create client security key for "+node.host_name
                print output 
                return rc
            rc = security_node.copyToRemoteNode(source="/tmp/%s/security/%s" % (security_node.current_user,keyname), destinationNode=node, destination="%s/etc/pvfs2-clientkey.pem" % node.ofs_installation_location, recursive=False)
            if rc != 0:
                print "Could not copy client security key %s for %s " % (keyname,node.host_name)
                print output 
                return rc


        return 0
    
#------------------------------------------------------------------------------   
#    
#    generateOFSKeystoreFile()
#
#    Generate OrangeFS keystore file for security
#
#
#------------------------------------------------------------------------------       

    
    def generateOFSKeystoreFile(self,node_list=None,security_node=None):
        if node_list == None:
            node_list = self.created_nodes
        if security_node==None:
            security_node = node_list[0]
        
        # cd /tmp/$USER/keys
        # for each server
        #   echo "S:{hostname} >> orangefs-keystore
        #   openssl rsa -in orangefs-serverkey.pem -pubout  >> orangefs_keystore
        # 
        # for each client 
        #   echo "C:{hostname} >> orangefs-keystore
        #   openssl rsa -in pvfs2clientkey.pem -pubout  >> orangefs_keystore
        
        # copy orangefs_keystore to all remote servers
        # 
        # sudo chown root:root /opt/orangefs/etc/pvfs2-clientkey.pem
        # sudo chmod 600 /opt/orangefs/etc/pvfs2-clientkey.pem
        
        output = []

        
        security_node.runSingleCommand("mkdir -p /tmp/%s/security" % security_node.current_user)
        security_node.changeDirectory("/tmp/%s/security" % security_node.current_user)
        
        # generate the keystore for entire network
        for node in self.created_nodes:
            
            server_keyname = "orangefs-serverkey-%s.pem" % node.host_name
            client_keyname = "pvfs2-clientkey-%s.pem" % node.host_name
            if node.alias_list == None:
                node.alias_list = node.getAliasesFromConfigFile(node.ofs_installation_location + "/etc/orangefs.conf")
            if len(node.alias_list) == 0:
                print "Could not get aliases"
                return -1
            for alias in node.alias_list:
                if node.host_name in alias:
                    security_node.runSingleCommand('echo "S:%s" >> orangefs-keystore' % alias)
                    security_node.runSingleCommand('openssl rsa -in %s -pubout >> orangefs-keystore' % server_keyname)
                
            security_node.runSingleCommand('echo "C:%s" >> orangefs-keystore' % node.host_name)
            security_node.runSingleCommand('openssl rsa -in %s -pubout >> orangefs-keystore' % client_keyname)
            
        for node in self.created_nodes:

            rc = security_node.copyToRemoteNode(source="/tmp/%s/security/orangefs-keystore" % security_node.current_user, destinationNode=node, destination="%s/etc/" % node.ofs_installation_location, recursive=False)
            if rc != 0:
                print "Could not copy keystore for %s " % (node.host_name)
                print output 
                return rc
            #now protect the files
            rc = node.runSingleCommand("chmod 400 %s/etc/*.pem" % node.ofs_installation_location)
            if rc != 0:
                print "Could not protect keys on %s " % (node.host_name)
                print output 
                return rc

        
        return 0


#------------------------------------------------------------------------------  
#
#    findExistingOFSInstallation()
#
#    find an existing OrangeFS installation for each node in the list.
#
#------------------------------------------------------------------------------     
    def findExistingOFSInstallation(self,node_list=None):
        if node_list == None:
            node_list = self.created_nodes
        for node in node_list:
            rc = node.findExistingOFSInstallation()
            if rc != 0:
                return rc
        
        return 0
            
#------------------------------------------------------------------------------  
#
#    networkOFSSettings()
#
#    Manually set OrangeFS settings for each node in the list. Used for retesting.
#
#------------------------------------------------------------------------------          
    
    def networkOFSSettings(self,
            ofs_installation_location,
            db4_prefix,
            ofs_extra_tests_location,
            pvfs2tab_file,
            resource_location,
            resource_type,
            ofs_config_file,
            ofs_fs_name,
            ofs_host_name_override,
            ofs_mount_point,
            node_list = None
            ):

        if node_list == None:
            node_list = self.created_nodes
            
        for i,node in enumerate():

            # source - Must provide location
            node.resource_location = resource_location
            node.resource_type = resource_type
            if resource_type == "BUILDNODE":
                node.ofs_source_location = resource_location

            
            
            if ofs_installation_location != None:
                node.ofs_installation_location = ofs_installation_location
            
            if db4_prefix != None:
                #rc = node.findDB4location
                node.db4_dir = db4_prefix
                node.db4_lib = db4_prefix+"/lib"

            # just reinstall extra tests
            if ofs_extra_tests_location != None:
                node.ofs_extra_tests_location = ofs_extra_tests_location
            
            if pvfs2tab_file != None:
                # find PVFS2TAB_FILE--or should we?
                node.setEnvironmentVariable("PVFS2TAB_FILE",pvfs2tab_file)
            
                
            # does OFS config file need to be provided?
            if ofs_config_file != None:
                node.ofs_conf_file = ofs_config_file
            
            if ofs_fs_name != None:
                node.ofs_fs_name = ofs_fs_name
            
            # Mount point. can be read from mount
            if ofs_mount_point != None:
                node.ofs_mount_point = ofs_mount_point
            
            # Hostname override. Needed to workaround an openstack problem.
            if len(ofs_host_name_override) > 0:
                try:
                    node.host_name = ofs_host_name_override[i]
                    node.runSingleCommandAsBatch("sudo hostname "+node.host_name)
                except:
                    # if not, ignore the error
                    pass
            #node.setEnvironmentVariable("LD_LIBRARY_PATH",node.db4_lib+":"+node.ofs_installation_location+":$LD_LIBRARY_PATH")

#------------------------------------------------------------------------------  
#
#    exportNFSDirectory()
#
#    export an NFS directory for each server in the list.
#
#------------------------------------------------------------------------------        

    def exportNFSDirectory(self,directory,nfs_server_list=None,options=None,network=None,netmask=None):
        if nfs_server_list == None:
            nfs_server_list = self.created_nodes
        
        rc = 0
        for nfs_server in nfs_server_list:
            rc += nfs_server.exportNFSDirectory(directory,options,network,netmask)

        return rc
 
#------------------------------------------------------------------------------  
#
#    mountNFSDirectory()
#
#    Mount an NFS share at mountpoint for each client in the client list.
#
#------------------------------------------------------------------------------     
    
    def mountNFSDirectory(self,nfs_share,mountpoint,options="",nfs_client_list=None):
        
        if nfs_client_list == None:
            nfs_client_list = self.created_nodes
    
        fail = 0
        
        for nfs_client in nfs_client_list:
            rc = nfs_client.mountNFSDirectory(nfs_share,mountpoint,options)
            if rc != 0:
                print "Mounting %s at %s with options %s on %s failed with RC=%d" %(nfs_share,mountpoint,options,nfs_client.ip_address,rc)
                fail = fail+1
            
        
        return fail

#------------------------------------------------------------------------------  
#
#    installOpenMPI()
#
#    installOpenMPI at the mpi_nfs_directory
#
#------------------------------------------------------------------------------    

    def installOpenMPI(self,mpi_nfs_directory=None,build_node=None,mpi_local_directory=None):
        if build_node == None:
            build_node = self.created_nodes[0]
        
        
        # the nfs directory is where all nodes will access MPI
        if mpi_nfs_directory == None:
            self.mpi_nfs_directory = "/opt/mpi"
      
        # the mpi local directory is where mpi will be built
        if mpi_local_directory == None:
            mpi_local_directory = "/home/%s/mpi" % build_node.current_user
        
        # export the nfs directory to all nodes.
        nfs_share = build_node.exportNFSDirectory(directory_name=mpi_local_directory)
        
        # wait for NFS servers to come up
        time.sleep(30)
        
        # mount the local directory as an NFS directory on all nodes.
        rc = self.mountNFSDirectory(nfs_share=nfs_share,mountpoint=self.mpi_nfs_directory,options="bg,intr,noac,nfsvers=3")
        
        # build mpi in the build location, but install it to the nfs directory
        rc = build_node.installOpenMPI(install_location=self.mpi_nfs_directory,build_location=self.mpi_nfs_directory)
    
        
        
        
        # how many slots per node do we need?

        # Testing requires np=4
        MAX_SLOTS = 4
        number_nodes = len(self.created_nodes)
    
        # number of slots needed is total/nodes.
        slots = (MAX_SLOTS / number_nodes) 
        
        # if this doesn't divide evenly, add an extra slot per node. Works well enough.
        if (MAX_SLOTS % number_nodes) != 0:
            slots = slots+1
        
        self.openmpi_version = build_node.openmpi_version

        build_node.changeDirectory(self.mpi_nfs_directory)

        # we created an openmpihost file earlier. Now copy it to the appropriate directory.
        if build_node.created_openmpihosts != None:
            rc = build_node.runSingleCommand("/bin/cp %s %s" % (build_node.created_openmpihosts,self.mpi_nfs_directory))
            if rc == 0:
                build_node.created_openmpihosts = self.mpi_nfs_directory + "/" + os.path.basename(build_node.created_openmpihosts)
            
        else:
            #print 'grep -v localhost /etc/hosts | awk \'{print \\\$2 "\tslots=%r"}\' > %s/openmpihosts' % (slots,self.mpi_nfs_directory)
            output = []
            build_node.runSingleCommand("grep -v localhost /etc/hosts | awk '{print \$2 \"\\tslots=%r\"}' > %s/openmpihosts" % (slots,self.mpi_nfs_directory),output)
            print output
        # update runtest to use openmpihosts file - should be done in patched openmpi
        #print 'sed -i s,"mpirun -np","mpirun --hostfile %s/openmpihosts -np",g %s/%s/ompi/mca/io/romio/test/runtests' % (self.mpi_nfs_directory,self.mpi_nfs_directory,build_node.openmpi_version)
        #build_node.runSingleCommand('sed -i s,"mpirun -np","mpirun --hostfile %s/openmpihosts -np",g %s/%s/ompi/mca/io/romio/test/runtests' % (self.mpi_nfs_directory,self.mpi_nfs_directory,build_node.openmpi_version))
        #build_node.runSingleCommand('chmod a+x %s/%s/ompi/mca/io/romio/test/runtests' % (self.mpi_nfs_directory,build_node.openmpi_version))
        # update bashrc     
        for node in self.created_nodes:
            #node.openmpi_installation_location = self.mpi_nfs_directory
            # have we already done this?
            #done = node.runSingleCommand("grep -v %s /home/%s/.bashrc" % (node.openmpi_installation_location,node.current_user))
            #done = 0
            #if done == 0:
            print "echo 'export PATH=%s/openmpi/bin:%s/bin:\$PATH' >> /home/%s/.bashrc" % (self.mpi_nfs_directory,node.ofs_installation_location,node.current_user)
            node.runSingleCommand("echo 'export PATH=%s/openmpi/bin:%s/bin:\$PATH' >> /home/%s/.bashrc" % (self.mpi_nfs_directory,node.ofs_installation_location,node.current_user))
            node.runSingleCommand("echo 'export LD_LIBRARY_PATH=%s/openmpi/lib:%s/lib:%s/lib:\$LD_LIBRARY_PATH' >> /home/%s/.bashrc" % (self.mpi_nfs_directory,node.ofs_installation_location,node.db4_dir,node.current_user))
            node.runSingleCommand("echo 'export PVFS2TAB_FILE=%s/etc/orangefstab' >> /home/%s/.bashrc" % (node.ofs_installation_location,node.current_user))
        
        return rc


#------------------------------------------------------------------------------  
#
#    setupHadoop()
#
#    Hadoop should be installed with required software. This configures hadoop 
#    for OrangeFS testing on the virtual cluster.
#
#------------------------------------------------------------------------------ 
    def setupHadoop(self,hadoop_nodes=None,master_node=None):
        if hadoop_nodes == None:
            hadoop_nodes = self.created_nodes
        
        if master_node == None:
            master_node = hadoop_nodes[0]
        
        # remove list of slaves. We will be rebuilding it.
        master_node.runSingleCommand("rm %s/conf/slaves" % master_node.hadoop_location)

        for node in hadoop_nodes:
            
            # setup hadoop-env.sh
            node.runSingleCommand("echo 'export JAVA_HOME=%s' >> %s/conf/hadoop-env.sh" % (node.jdk6_location,node.hadoop_location))
            node.runSingleCommand("echo 'export LD_LIBRARY_PATH=%s/lib' >> %s/conf/hadoop-env.sh" % (node.ofs_installation_location,node.hadoop_location))
            node.runSingleCommand("echo 'export JNI_LIBRARY_PATH=%s/lib' >> %s/conf/hadoop-env.sh" % (node.ofs_installation_location,node.hadoop_location))
            node.runSingleCommand("echo 'export HADOOP_CLASSPATH=\$JNI_LIBRARY_PATH/ofs_hadoop.jar:\$JNI_LIBRARY_PATH/ofs_jni.jar' >> %s/conf/hadoop-env.sh" % node.hadoop_location)
            # copy templates to node
            master_node.copyToRemoteNode(source="%s/test/automated/hadoop-tests.d/conf/" % master_node.ofs_source_location,destinationNode=node,destination="%s/conf/" % node.hadoop_location,recursive=True)
            
            # update mapred-site.xml
            node.runSingleCommand("sed -i s/__NODE001__/%s/ %s/conf/mapred-site.xml" % (master_node.host_name,node.hadoop_location))
            
            # update core-site.xml
            node.runSingleCommand("sed -i s,__MNT_LOCATION__,%s, %s/conf/core-site.xml" % (node.ofs_mount_point,node.hadoop_location))

            # point slave node to master
            node.runSingleCommand("echo '%s' > %s/conf/masters" % (master_node.host_name,node.hadoop_location))
            
            # notify master of new slave
            master_node.runSingleCommand("echo '%s' >> %s/conf/slaves" % (node.host_name,master_node.hadoop_location))
            
        output = []
        master_node.runSingleCommand("%s/bin/start-mapred.sh" % master_node.hadoop_location,output)
        print output
        time.sleep(20)
        # hadoop dfs -ls is our "ping" for hadoop. 
        rc = master_node.runSingleCommand("%s/bin/hadoop dfs -ls /" % master_node.hadoop_location,output)
        if rc != 0:
            print "========Hadoop dfs -ls output==========="
            print output
        else:
            print "Hadoop setup successfully"
            master_node.runSingleCommand("%s/bin/hadoop dfs -mkdir /user/%s" % (master_node.hadoop_location,master_node.current_user))
            
        return rc
        
    
# def test_driver():
#     my_node_manager = OFSTestNetwork()
#     my_node_manager.addEC2Connection("ec2-cred/ec2rc.sh","Buildbot","/home/jburton/buildbot.pem")
# 
#     nodes  = my_node_manager.createNewEC2Nodes(4,"cloud-ubuntu-12.04","c1.small")
#     #nodes  = my_node_manager.createNewEC2Nodes(6,"cloud-sl6","c1.small")
# 
#     '''
#     ec2_ip_addresses = ['10.20.102.98','10.20.102.97','10.20.102.99','10.20.102.100']
# 
#     nodes = []
# 
#     for my_address in ec2_ip_addresses:
#         nodes.append(my_node_manager.addRemoteNode(ip_address=my_address,username="ubuntu",key="/home/jburton/buildbot.pem",is_ec2=True))
#     for node in nodes:
#         node.setEnvironmentVariable("VMSYSTEM","cloud-ubuntu-12.04")
#     '''
# 
#     print ""
#     print "==================================================================="
#     print "Updating New Nodes"
#     
#     my_node_manager.updateEC2Nodes()
# 
#     print ""
#     print "==================================================================="
#     print "Installing Required Software"
#     my_node_manager.installRequiredSoftware()
# 
#     print ""
#     print "==================================================================="
#     print "Installing Torque"
#     my_node_manager.installTorque()
# 
#     print ""
#     print "==================================================================="
#     
#     print "Check Torque"
#     my_node_manager.checkTorque()
#      
#     print ""
#     print "==================================================================="
#     print "Downloading and Installing OrangeFS"
# 
#     my_node_manager.buildAndInstallOFSFromSource(resource_type="SVN",resource_location="http://orangefs.org/svn/orangefs/branches/stable")
#     #my_node_manager.buildAndInstallOFSFromSource(resource_type="SVN",resource_location="http://orangefs.org/svn/orangefs/trunk")
#     #my_node_manager.buildAndInstallOFSFromSource(resource_type="TAR",resource_location="http://www.orangefs.org/downloads/LATEST/source/orangefs-2.8.7.tar.gz")
#     #for node in my_node_manager.created_nodes:
#     #    node.setEnvironmentVariable("LD_LIBRARY_PATH","%s/lib:/opt/db4/lib:$LD_LIBRARY_PATH" % (node.ofs_installation_location))
#     #    node.setEnvironmentVariable("LD_PRELOAD","%s/lib/libofs.so:%s/lib/libpvfs2.so" % (node.ofs_installation_location,node.ofs_installation_location))
#     print ""
#     print "==================================================================="
#     print "Installing Benchmarks"
# 
# 
#     my_node_manager.installBenchmarks()
# 
# 
# 
#     print ""
#     print "==================================================================="
#     print "Copy installation to all nodes"
# 
#     my_node_manager.configureOFSServer()
#     my_node_manager.copyOFSToNodeList()
# 
# 
#         
#     print ""
#     print "==================================================================="
#     print "Start OFS Server"
#     my_node_manager.startOFSServers()
# 
#     print ""
#     print "==================================================================="
#     print "Start Client"
#     my_node_manager.startOFSClient()
# 
#     print ""
#     print "==================================================================="
#     print "Mount Filesystem"
#     my_node_manager.mountOFSFilesystem()
# 
# 
#     print ""
#     print "==================================================================="
#     print "Run Tests"
#     communicate = []
#     rc = nodes[0].runSingleCommand(command="ps aux | grep pvfs",output=communicate)
#     print "RC = %d" % rc
#     print "STDOUT = "+communicate[1]
#     print "STDERR = "+communicate[2]
#     nodes[0].runSingleCommand("mount")
#     nodes[0].runSingleCommand("dmesg")
# 
# 
#     import OFSSysintTest
#     import OFSVFSTest
#     import OFSUsrintTest
# 
#     rc = 0
# 
# 
#     def write_output(filename,function,rc):
#         output = open(filename,'a+')
#         if rc != 0:
#             output.write("%s........................................FAIL: RC = %d\n" % (function.__name__,rc))
#         else:
#             output.write("%s........................................PASS.\n" % function.__name__)
#         output.close()
# 
#     #filename = "OFSTestNetwork.log"    
#     output = open(filename,'w+')
#     output.write("Sysint Tests ==================================================\n")
#     output.close()
# 
#     for callable in OFSSysintTest.__dict__.values():
#         try:
#             #print "Running %s" % callable.__name__
#             rc = nodes[0].runOFSTest(callable)
#             write_output(filename,callable,rc)
#         except AttributeError:
#             pass
#         except TypeError:
#             pass
# 
#     output = open(filename,'a+')
#     nodes[0].runSingleCommandBacktick("dmesg")
#     output.write("VFS Tests ==================================================\n")
#     output.close()
# 
#     for callable in OFSVFSTest.__dict__.values():
#         try:
#             rc = nodes[0].runOFSTest(callable)
#             write_output(filename,callable,rc)
#         except AttributeError:
#             pass
#         except TypeError:
#             pass
#    
#     #my_node_manager.stopOFSServers()
#     #my_node_manager.stopOFSClient()
# 
#     #for node in my_node_manager.created_nodes:
# 
#      #   node.setEnvironmentVariable("LD_PRELOAD","%s/lib/libofs.so:%s/lib/libpvfs2.so" % (node.ofs_installation_location,node.ofs_installation_location))
#         #node.setEnvironmentVariable("LD_LIBRARY_PATH","%s/lib:/opt/db4/lib:$LD_LIBRARY_PATH" % (node.ofs_installation_location))
#     
#     #my_node_manager.startOFSServers()
#     ''' 
#     output = open(filename,'a+')
#     nodes[0].runSingleCommandBacktick("dmesg")
#     output.write("Userint Tests ==================================================\n")
#     output.close()
# 
#     for callable in OFSUsrintTest.__dict__.values():
#         try:
#             rc = nodes[0].runOFSTest(callable)
#             write_output("OFSTestNetwork.log",callable,rc)
#         except AttributeError:
#             pass
#         except TypeError:
#             pass
# 
#     '''
# 
# 
# 
#       
#     print ""
#     print "==================================================================="
#     print "Stop Client"
#     my_node_manager.stopOFSServers()
# 
# 
# 
#     print ""
#     print "==================================================================="
#     print "Terminating Nodes"
# 
#   #  my_node_manager.terminateAllEC2Nodes()
#    
#     #my_node_manager.runSimultaneousCommands(node_list=nodes,args=["sudo apt-get update && sudo apt-get -y dist-upgrade < /dev/zero && sudo reboot"])
#     #my_node_manager.runSimultaneousCommands(node_list=nodes,args=["sudo reboot"])
# 
# 
# 
#     #print "Getting source"
#     #my_node_manager.runSimultaneousCommands(node_list=nodes,args=["sudo apt-get -y install subversion"])
#     #my_node_manager.runSimultaneousCommands(node_list=nodes,node_function=OFSTestNode.copyOFSSource,kwargs={"resource_type": "SVN","resource": "http://orangefs.org/svn/orangefs/trunk","dest_dir": "/tmp/ubuntu"})
# 
#     #for n in nodes:
#     #    my_node_manager.terminateEC2Node(n)
# 
#     #print my_node.runSingleCommand("whoami")
# 
# #Call script with -t to test
# if len(sys.argv) > 1 and sys.argv[1] == "-t":
#     test_driver()
