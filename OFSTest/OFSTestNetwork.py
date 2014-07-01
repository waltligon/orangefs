#!/usr/bin/python
##
# @class OFSTestNetwork.py
#
# OFSTestNework is the class that forms the abstraction for the cluster. All operations on the cluster
# should be performed via OFSTestNetwork, not the OFSTestNodes.
#
# Every network must have the following:
#
# 1. At least one OFSTestNode in the network_nodes array.
# 2. A local_master, which represents the local machine from which the tests are run.
#
# Cloud/OpenStack based virtual networks will also have an cloud_connection_manager.
#


import os
import OFSTestNode
import OFSTestLocalNode 
import OFSTestRemoteNode 
import OFSCloudConnectionManager
import OFSEC2ConnectionManager
import OFSNovaConnectionManager
import Queue
import threading
import time
from pprint import pprint
import logging

class OFSTestNetwork(object):

    ##
    #
    # @fn __init__(self)
    #
    # Initializes variables and creates local master.
    #
    # @param self The object pointer
    

    def __init__(self):
    
        # Configuration for cloud 
        self.cloud_connection_manager = None
        # dictionary of instances
           
        self.network_nodes = []
        
        print "==================================================================="
        print "Checking Local machine"
        self.local_master = OFSTestLocalNode.OFSTestLocalNode()
        self.mpi_nfs_directory = ""
        self.openmpi_version = ""

        #self.network_nodes['127.0.0.1']=self.local_master
   

    ##
    # @fn  findNode(self,ip_address="",hostname=""):
    #
    # Finds an OFSTestNode by ip address or hostname
    #
    # @param self The object pointer
    # @param ip_address IP address of the node
    # @param hostname Hostname of the node
    #
    # @returns OFSTestNode if found, None, if not found.
    #

    def findNode(self,ip_address="",hostname=""):
    
        
        if ip_address != "":
            if ip_address == self.local_master.ip_address:
                return self.local_master
            else:
                return next((i for i in self.network_nodes if i.ip_address == ip_address), None) 
        elif hostname != "":
            logging.info("Available host names")
            logging.info([i.hostname for i in self.network_nodes])
            if hostname == self.local_master.hostname:
                return self.local_master
            else:
                return next((i for i in self.network_nodes if i.hostname == hostname), None) 
        else:
            return None

    ##
    #  @fn addRemoteNode(self,username,ip_address,key,is_cloud=False,ext_ip_address=None):
    #
    #    Creates a new OFSTestNode and adds it to the network_nodes list.
    #
    # @param self The object pointer
    # @param username User login
    # @param ip_address IP address of node
    # @param key ssh key file location to access the node for username
    # @param is_cloud Is this an Cloud/OpenStack node?
    # @param ext_ip_address Externally accessible IP address
    #
    #  @returns the OFSTestNode
    #

        
    def addRemoteNode(self,username,ip_address,key,is_cloud=False,ext_ip_address=None):
        #This function adds a remote node
        
        # Is this a remote machine or an existing cloud node?
        remote_node = OFSTestRemoteNode.OFSTestRemoteNode(username=username,ip_address=ip_address,key=key,local_node=self.local_master,is_cloud=is_cloud,ext_ip_address=ext_ip_address)
                
        # Add to the node dictionary
        self.network_nodes.append(remote_node)
        
        # Return the new node
        return remote_node


    ##
    #    @fn runSimultaneousCommands(self,node_list,node_function=OFSTestNode.OFSTestNode.runSingleCommand,args=[],kwargs={})
    #
    #    Runs a command on multiple nodes.
    #
    #    @param self The object pointer
    #    @param node_list  List of nodes to run command on 
    #    @param node_function Python function to run on all OFSTestNodes. Default is OFSTestNode.runSingleCommand
    #    @param args Arguments to Python node_function
    #    @param kwargs Keyword args to Python node_function
    #
        
        
    def runSimultaneousCommands(self,node_list=None,node_function=OFSTestNode.OFSTestNode.runSingleCommand,args=[],kwargs={}):
        
        #passes in a thread class that does the work on a node list with arguments args
        
        if node_list == None:
            node_list = self.network_nodes
         
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
          

    ##
    # @fn   addCloudConnection(self,cloud_config_file,key_name,key_location)
    #
    #    Initialize the CloudConnection
    #
    #    @param self The object pointer
    #    @param cloud_config_file location of cloudrc.sh file 
    #    @param key_name Name of Cloud key to access node
    #    @param key_location Location of .pem file that contains the Cloud key
    #
   
    
    
    def addCloudConnection(self,cloud_config_file,key_name,key_location,cloud_type="EC2",nova_password_file=None):
        #This function initializes the cloud connection
        self.cloud_type = cloud_type
        if (cloud_type == 'EC2'):
            self.cloud_connection_manager = OFSEC2ConnectionManager.OFSEC2ConnectionManager(cloud_config_file)
        elif (cloud_type == 'nova'):
            self.cloud_connection_manager = OFSNovaConnectionManager.OFSNovaConnectionManager(cloud_config_file,password_file=nova_password_file)
        self.cloud_connection_manager.setCloudKey(key_name,key_location)
        

    ##
    # @fn createNewCloudNodes(number_nodes,image_name,machine_type,associateip=False,domain=None):
    #
    # Creates new cloud nodes and adds them to network_nodes list.
    #
    #
    #    @param self The object pointer  
    #    @param number_nodes  number of nodes to be created
    #    @param image_name  Name of Cloud image to launch
    #    @param machine_type  Cloud "flavor" of virtual node
    #    @param associateip  Associate to external ip?
    #    @param domain Domain to associate with external ip
    #	 @param cloud_subnet cloud subnet id for primary network interface.
    #
    #    @returns list of new nodes.


    
    def createNewCloudNodes(self,number_nodes,image_name,machine_type,associateip=False,domain=None,cloud_subnet=None,instance_suffix=""):
        
        # This function creates number nodes on the cloud system. 
        # It returns a list of nodes
        new_ofs_test_nodes = self.cloud_connection_manager.createNewCloudNodes(number_nodes,image_name,machine_type,self.local_master,associateip,domain,cloud_subnet,instance_suffix)
        
                
        # Add the node to the created nodes list.
        for new_node in new_ofs_test_nodes:
            self.network_nodes.append(new_node)
        
        # return the list of newly created nodes.
        
        return new_ofs_test_nodes
    

##
# @fn uploadKeys(node_list=None)
#
# Upload ssh keys to the list of remote nodes
#
#    @param self The object pointer
#    @param node_list list of nodes to upload the keys.
#

 

    def uploadKeys(self,node_list=None):
        # if a list is not provided upload all keys
        if node_list == None:
            node_list = self.network_nodes
            
        for node in node_list:
            self.runSimultaneousCommands(node_list=node_list,node_function=OFSTestRemoteNode.OFSTestRemoteNode.uploadRemoteKeyFromLocal, args=[self.local_master,node.ext_ip_address])
        

    ##      
    # @fn enablePasswordlessSSH(self,node_list=None):
    #
    # Enable passwordless SSH for the node for the current user.
    #
    #    @param self The object pointer
    #    @param node_list List of nodes to enable passwordless ssh
            

    def enablePasswordlessSSH(self,node_list=None,user=None):
        
        if node_list == None:
            node_list = self.network_nodes
        

        
        for src_node in node_list:
            
            if user == None:
                user = src_node.current_user
        
            if user == "root":
                home_dir = "/root"
            else:
                home_dir = "/home/"+user
            
            # passwordless access to localhost
            src_node.runSingleCommand("/usr/bin/ssh-keyscan localhost >> %s/.ssh/known_hosts" % home_dir)
            src_node.runSingleCommand("/usr/bin/ssh-keyscan 127.0.0.1 >> %s/.ssh/known_hosts" % home_dir)
            
            for dest_node in node_list:
                # passwordless access to all other nodes
                logging.info("Enabling passwordless SSH from %s to %s/%s/%s" % (src_node.hostname,dest_node.hostname,dest_node.ip_address,dest_node.ext_ip_address))
                src_node.runSingleCommand("/usr/bin/ssh-keyscan %s >> %s/.ssh/known_hosts" % (dest_node.hostname,home_dir))
                src_node.runSingleCommand("/usr/bin/ssh-keyscan %s >> %s/.ssh/known_hosts" % (dest_node.ext_ip_address,home_dir))
                src_node.runSingleCommand("/usr/bin/ssh-keyscan %s >> %s/.ssh/known_hosts" % (dest_node.ip_address,home_dir))
                

    ##      
    # @fn terminateCloudNode(self, remote_node)
    #
    # Terminate the remote node and remove it from the created node list.
    #
    #    @param self The object pointer
    #    @param remote_node Node to be terminated.


    def terminateCloudNode(self,remote_node):
                
        if remote_node.is_cloud == False: 
            logging.exception("Node at %s is not controlled by the cloud manager." % remote_node.ip_address)
            return
        
        rc = self.cloud_connection_manager.terminateCloudInstance(remote_node.ip_address)
        
        # if the node was terminated, remove it from the list.
        if rc == 0:
            self.network_nodes = [ x for x in self.network_nodes if x.ip_address != remote_node.ip_address]
        else:
            logging.exception( "Could not delete node at %s, error code %d" % (remote_node.ip_address,rc))
            
        return rc


    ##      
    # @fn updateCloudNodes(self,node_list=None):
    #
    #    Update only the Cloud Nodes
    # 
    # @param self The object pointer
    # @param node_list List of nodes to update.
    #

        
    def updateCloudNodes(self,node_list=None):
        # This only updates the Cloud controlled nodes
         
        if node_list == None:
            node_list = self.network_nodes
        
        cloud_nodes = [node for node in self.network_nodes if node.is_cloud == True]
        self.updateNodes(cloud_nodes)   


    ##
    # @fn updateEtcHosts(self,node_list=None):
    #
    # This function updates the etc hosts file on each node with the hostname and ip
    # address. Also creates the necessary mpihosts config files.
    #
    #    @param self The object pointer
    #    @param node_list List of nodes in network


    def updateEtcHosts(self,node_list=None):
        
        #This function updates the etc hosts file on each node with the 
        if node_list == None:
            node_list = self.network_nodes
        
 
        for node in node_list:
            node.created_openmpihosts = "~/openmpihosts"
            node.created_mpichhosts = "~/mpichhosts"
            for n2 in node_list:
                # can we ping the node?
                #print "Pinging %s from local node" % n2.hostname
                rc = node.runSingleCommand("ping -c 1 %s" % n2.hostname)
                # if not, add to the /etc/hosts file
                if rc != 0:
                    logging.info("Could not ping %s at %s from %s. Manually adding to /etc/hosts" % (n2.hostname,n2.ip_address,node.hostname))
                    node.runSingleCommandAsRoot('bash -c \'echo -e "%s     %s     %s" >> /etc/hosts\'' % (n2.ip_address,n2.hostname,n2.hostname))
                    # also create mpihosts files
                    node.runSingleCommand('echo "%s   slots=2" >> %s' % (n2.hostname,node.created_openmpihosts))
                    node.runSingleCommand('echo "%s:2" >> %s' % (n2.hostname,node.created_mpichhosts))

                    
            node.hostname = node.runSingleCommandBacktick("hostname")

    ##
    # @fn updateNodes(self,node_list)
    #
    # This updates the system software on the list of nodes.
    #
    #    @param self The object pointer
    #    @param node_list List of nodes to update
     
    def updateNodes(self,node_list=None):
        if node_list == None:
            node_list = self.network_nodes
            
        # Run updateNode on the nodes simultaneously. 
        self.runSimultaneousCommands(node_list=node_list,node_function=OFSTestNode.OFSTestNode.updateNode)
        # Wait for reboot
        print "Waiting 180 seconds for nodes to reboot"
        time.sleep(180)
        # workaround for strange cuer1 issue where hostname changes on reboot.
        for node in node_list:
            tmp_hostname = node.runSingleCommandBacktick("hostname")
            if tmp_hostname != node.hostname:
                logging.info( "Hostname changed from %s to %s! Resetting to %s" % (node.hostname,tmp_hostname,node.hostname))
                node.runSingleCommandAsRoot("hostname %s" % node.hostname)
                
    
    ##
    # @fn installRequiredSoftware(self,node_list=None):
    #
    # This installs the required software on all the nodes
    #
    #    @param self The object pointer
    #    @param node_list List of nodes to update
                    
        
    
    def installRequiredSoftware(self,node_list=None):
        if node_list == None:
            node_list = self.network_nodes
        self.runSimultaneousCommands(node_list=node_list,node_function=OFSTestNode.OFSTestNode.installRequiredSoftware)
    
    ##
    # @fn buildOFSFromSource(
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
    #         debug=False,
    #         node_list=None):
    #
    #
    # This builds OrangeFS on the build node
    #
    #    @param self The object pointer
    #    @param resource_type What form is the source tree? (SVN,TAR,LOCAL) etc.
    #    @param resource_location Where is the source located?
    #    @param download_location Where should the source be downloaded to?
    #    @param build_node    On which node should OFS be build (default first)
    #    @param build_kmod    Build kernel module
    #    @param enable_strict    Enable strict option
    #    @param enable_fuse    Build fuse module
    #    @param enable_shared     Build shared libraries
    #    @param enable_hadoop    Build hadoop support
    #    @param ofs_prefix    Where should OFS be installed?
    #    @param db4_prefix    Where is db4 located?
    #    @param security_mode    None, Cert, Key
    #    @param ofs_patch_files Location of OrangeFS patches.
    #    @param configure_opts    Additional configure options
    #    @param make_opts        Make options
    #    @param debug    Enable debugging
    #    @param svn_options    Additional options for SVN
    #    @param node_list List of nodes to update
          

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
        debug=False,
        svn_options=None,
        node_list=None,
        ):
        
        output = []
        dir_list = ""
        
        msg = "Resource type is "+resource_type+" location is "+resource_location
        print msg
        logging.info(msg)
        
        if node_list == None:
            node_list = self.network_nodes
        
        if build_node == None:
            build_node = node_list[0]
            
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
            #print "Copying source from %s to %s" % (resource_location,build_node.hostname)
            rc = self.local_master.copyToRemoteNode(resource_location,build_node,download_location,True)
            if rc != 0:
                logging.exception("Could not copy source from %s to %s" % (resource_location,build_node.hostname))
                return rc
            #rc = build_node.runSingleCommand("ls -l "+download_location+dir_list,output)
            #print output
            
            #print "Calling build_node.copyOFSSource(%s,%s,%s)" %(resource_type,resource_location,download_location+dir_list)
        
            resource_type = "BUILDNODE"
            resource_location = download_location+dir_list
        rc = build_node.copyOFSSource(resource_type,resource_location,download_location+dir_list,svn_options)
        
        if rc != 0:
            return rc

        for patch in ofs_patch_files:
            
            patch_name = os.path.basename(patch)
            rc = self.local_master.copyToRemoteNode(patch,build_node,"%s/%s" % (build_node.ofs_source_location,patch_name),False)
            if rc != 0:
                logging.exception("Could not upload patch at %s to buildnode %s" % (patch,build_node.hostname))
                return rc


        rc = build_node.configureOFSSource(build_kmod=build_kmod,enable_strict=enable_strict,enable_shared=enable_shared,ofs_prefix=ofs_prefix,db4_prefix=db4_prefix,ofs_patch_files=ofs_patch_files,configure_opts=configure_opts,security_mode=security_mode,enable_hadoop=enable_hadoop)
        if rc != 0:
            return rc
        
        rc = build_node.makeOFSSource(make_opts)
        if rc != 0:
            return rc
        
        return rc

   
    ##    
    #    @fn installOFSBuild(self,build_node=None,install_opts="",node_list=None):
    #
    #    Install the OrangeFS build on a given node
    #    @param self The object pointer
    #    @param build_node Node on which to build OrangeFS
    #    @param install_opts Other install options
    #    @param node_list List of nodes in network
           
        
    def installOFSBuild(self,build_node=None,install_opts="",node_list=None):
        if node_list == None:
            node_list = self.network_nodes
        
        if build_node == None:
            build_node = self.network_nodes[0]
        return build_node.installOFSSource(install_opts)
    
   
    ##    
    #    @fn installOFSTests(self,build_node=None,install_opts=""):
    #
    #    Install the OrangeFS tests in the source code on a given node
    #    @param self The object pointer
    #    @param client_node Node on which to install OrangeFS tests
    #    @param configure_opts Other install options
    #    @param node_list List of nodes in network.
 

    def installOFSTests(self,client_node=None,configure_options="",node_list=None):
        if node_list == None:
            node_list = self.network_nodes
        if client_node == None:
            client_node = node_list[0]
        return client_node.installOFSTests(configure_options)

   
    ##    
    #    @fn installBenchmarks(self,build_node=None,node_list=None):
    #
    #    Install the 3rd party benchmarks on the given node.
    #    @param self The object pointer
    #    @param build_node Node on which to install benchmark tests
    #    @param node_list List of nodes in network.
 

    def installBenchmarks(self,build_node=None,node_list=None):
        if node_list == None:
            node_list = self.network_nodes
        if build_node == None:
            build_node = node_list[0]
        return build_node.installBenchmarks("http://devorange.clemson.edu/pvfs/benchmarks-20121017.tar.gz","/home/%s/benchmarks" % build_node.current_user)

   
    ##    
    #    @fn configureOFSServer(self,ofs_fs_name,master_node=None,node_list=None,pvfs2genconfig_opts="",security=None):
    #
    #    @param self The object pointer
    #    @param ofs_fs_name    Default name of OrangeFS service: version < 2.9 = "pvfs2-fs"; version >= 2.9 = "orangefs"
    #    @param master_node    Master node in the cluster.
    #    @param node_list      List of nodes in OrangeFS cluster
    #    @param pvfs2genconfig_opts = Additional options for pvfs2genconfig utility
    #    @param security        None, "Key", "Cert"

 
       
    def configureOFSServer(self,ofs_fs_name,master_node=None,node_list=None,pvfs2genconfig_opts="",security=None):
        if node_list == None:
            node_list = self.network_nodes
        if master_node == None:
            master_node = node_list[0]
        return master_node.configureOFSServer(ofs_hosts_v=node_list,ofs_fs_name=ofs_fs_name,configuration_options=pvfs2genconfig_opts,security=security)

   
    ##    
    #    @fn copyOFSToNodeList(self,destination_list=None):
    #
    #    Copy OFS from build node to rest of cluster.
    #    
    #    @param self The object pointer
    #    @param destination_list List of nodes to copy OrangeFS to. OFS should already be at destination_list[0].
        
    def copyOFSToNodeList(self,destination_list=None):
        if destination_list == None:
            destination_list = self.network_nodes;
        self.copyResourceToNodeList(node_function=OFSTestNode.OFSTestNode.copyOFSInstallationToNode,destination_list=destination_list)

       
    ##    
    #  @fn copyResourceToNodeList(self,node_function,destination_list=None):
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
    #    @param self The object pointer
    #    @param node_function    Python function/method that does copying
    #    @param destination_list    list of nodes. Assumption is source is at node[0].
        

    def copyResourceToNodeList(self,node_function,destination_list=None):

        
        if destination_list == None:
            destination_list = self.network_nodes
            
        # This assumes that the OFS installation is at the destination_list[0]
        list_length = len(destination_list)
        
        # If our list is of length 1 or less, return.
        if list_length <= 1:
            return 
        
        #copy from list[0] to list[list_length/2]
        msg = "Copying from %s to %s" % (destination_list[0].ip_address,destination_list[list_length/2].ip_address)
        print msg
        logging.info(msg)
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
    
   
    ##    
    #    @fn stopOFSServers(self,node_list=None):
    #
    #    Stops the OrangeFS servers on the nodes
    #    @param self The object pointer
    #    @param node_list List of nodes in network.
     
          
    def stopOFSServers(self,node_list=None):
        if node_list == None:
            node_list = self.network_nodes
        self.runSimultaneousCommands(node_list=node_list,node_function=OFSTestNode.OFSTestNode.stopOFSServer)
        time.sleep(20)

   
    ##    
    #    @fn startOFSServers(self,node_list=None):
    #
    #    Starts the OrangeFS servers on the nodes
    #
    #    @param self The object pointer
    #    @param node_list List of nodes in network.
     

    def startOFSServers(self,node_list=None):
        if node_list == None:
            node_list = self.network_nodes
        self.runSimultaneousCommands(node_list=node_list,node_function=OFSTestNode.OFSTestNode.startOFSServer)
        time.sleep(20)

   
    ##    
    #    @fn startOFSClientAllNodes(self,security=None,node_list=None):
    #
    #    Starts the OrangeFS servers on all created nodes
    #    @param self The object pointer
    #    @param security OFS security mode: None,"Key","Cert"
    #    @param node_list List of nodes in network.
     

    def startOFSClientAllNodes(self,security=None,node_list=None):
        if node_list == None:
            node_list = self.network_nodes
        for node in node_list:
            self.startOFSClient(node,security)
   
    ##    
    #    @fn startOFSClient(self,client_node=None,security=None):
    #
    #    Starts the OrangeFS servers on one node
    #
    #    @param self The object pointer
    #    @param client_node Node on which to run OrangeFS client
    #    @param security OFS security mode: None,"Key","Cert"
    #    @param node_list List of nodes in network.

            
    def startOFSClient(self,client_node=None,security=None,node_list=None):
        if node_list == None:
            node_list = self.network_nodes
        if client_node == None:
            client_node = node_list[0]
        client_node.installKernelModule()
        #client_node.runSingleCommand('/sbin/lsmod | grep pvfs')
        client_node.startOFSClient(security=security)


        time.sleep(10)
        #client_node.runSingleCommand('ps aux | grep pvfs')

    ##
    # @fn mountOFSFilesystem(self,mount_fuse=False,client_node=None,node_list=None):
    #    Mount OrangeFS on a given client node.
    #    @param self The object pointer
    #    @param mount_fuse Mount using fuse module?
    #    @param client_node Node on which to run OrangeFS client
    #    @param node_list List of nodes in network.


    def mountOFSFilesystem(self,mount_fuse=False,client_node=None,node_list=None):
        if node_list == None:
            node_list = self.network_nodes
        if client_node == None:
            client_node = node_list[0]
        client_node.mountOFSFilesystem(mount_fuse=mount_fuse)
        time.sleep(10)
        print "Checking mount"
        mount_res=client_node.runSingleCommandBacktick("mount | grep -i pvfs")
        print mount_res
        logging.info("Checking Mount: "+mount_res)
        #client_node.runSingleCommand("touch %s/myfile" % client_node.ofs_mount_point)
        #client_node.runSingleCommand("ls %s/myfile" % client_node.ofs_mount_point)

   
    ##    
    # @fn mountOFSFilesystemAllNodes(self,mount_fuse=False,node_list=None):
    #
    #    Mount OrangeFS on all nodes
    #    @param self The object pointer
    #    @param mount_fuse Mount using fuse module?
    #    @param node_list List of nodes in network.


    def mountOFSFilesystemAllNodes(self,mount_fuse=False,node_list=None):
        
        # TODO: make this multithreaded
        for node in self.network_nodes:
            self.mountOFSFilesystem(mount_fuse=mount_fuse,client_node=node)
   
    ##    
    #    @fn stopOFSClient(self,client_node=None,node_list=None):
    #
    #    Stop the OrangeFS client on a given node
    #    @param self The object pointer
    #    @param client_node Node on which to run OrangeFS client
    #    @param node_list List of nodes in network.


        
    def stopOFSClient(self,client_node=None,node_list=None):
        if node_list == None:
            node_list = self.network_nodes
        if client_node == None:
            client_node = node_list[0]
        client_node.stopOFSClient()
        
   
    ##    
    #    @fn stopOFSClientAllNodes(self,node_list=None):
    #
    #    Stop the OrangeFS client on all nodes in list
    #    @param self The object pointer
    #    @param node_list List of nodes in network.



    def stopOFSClientAllNodes(self,node_list=None):
        if node_list == None:
            node_list = self.network_nodes
        # TODO: make this multithreaded
        for node in node_list:
            self.stopOFSClient(node)

   
    ##    
    #    @fn unmountOFSFilesystemAllNodes(self,node_list=None):
    #
    #    Unmount the OrangeFS directory on all nodes in list    
    #    @param self The object pointer
    #    @param node_list List of nodes in network.



    
    def unmountOFSFilesystemAllNodes(self,node_list=None):
        if node_list == None:
            node_list = self.network_nodes
        # TODO: make this multithreaded
        for node in node_list:
            node.unmountOFSFilesystem()

   
    ##    
    #    @fn terminateAllCloudNodes(self,node_list=None):
    #
    #    Terminate all the cloud nodes in a list
    #    @param self The object pointer
    #    @param node_list List of nodes in network.



    def terminateAllCloudNodes(self, node_list=None):
        if node_list == None:
            node_list = self.network_nodes
        for node in node_list:
            if node.is_cloud == True:
                self.terminateCloudNode(node)

   
    ##    
    #    installTorque(self,node_list=None, head_node=None):
    #
    #    Install Torque on the virtual cluster
    #    @param self The object pointer
    #    @param node_list List of nodes in network.
    #    @param head_node Head node of Torque setup


                
    
 
    def installTorque(self, node_list=None, head_node=None):
        
        if node_list == None:
            node_list = self.network_nodes
        
        if head_node == None:
            head_node = node_list[0]
        # first install torque on the head node.
        head_node.installTorqueServer()
        
        
        #now add all the client nodes
        
        for i in range(len(node_list)):
                node_list[i].installTorqueClient(head_node)
                
                torque_node_cpu = node_list[i].runSingleCommandBacktick('grep proc /proc/cpuinfo | wc -l')
                if "ubuntu" in head_node.distro.lower() or "mint" in head_node.distro.lower() or "debian" in head_node.distro.lower():
                    head_node.runSingleCommandAsRoot("bash -c 'echo \\\"%s np=%s\\\" >> /var/spool/torque/server_priv/nodes'" % (node_list[i].hostname,torque_node_cpu))
                elif "centos" in head_node.distro.lower() or "scientific linux" in head_node.distro.lower() or "redhat" in head_node.distro.lower() or "fedora" in head_node.distro.lower():
                    head_node.runSingleCommandAsRoot("bash -c 'echo \\\"%s np=%s\\\" >> /var/lib/torque/server_priv/nodes'" % (node_list[i].hostname,torque_node_cpu))
        head_node.restartTorqueServer()    

        # wait for the server to start
        time.sleep(15)
        
        down_hostnames = self.checkTorque()
        print "Down torque nodes: "
        print down_hostnames
        retries = 0

        try:
            while len(down_hostnames) > 0 and retries < 5:
                for hostname in down_hostnames:
                    # if a node is down, try restarting Torque Mom. Not sure why this happens.
                    node = self.findNode(hostname=hostname.rstrip())
                    node.restartTorqueMom()
                
                #wait for all clients to start
                time.sleep(15)
                down_hostnames = self.checkTorque()
                print "Down torque nodes: "
                print down_hostnames
                retries = retries + 1
        # break on an AttributeErrors if node isn't found
        except AttributeError:
            pass
    
   
    ##    
    #    @fn checkTorque(self, node_list=None, head_node=None):
    #
    #    Check to see if Torque is running properly.
    #    @param self The object pointer
    #    @param node_list List of nodes in network.
    #    @param head_node Head node of Torque setup

      
            
    def checkTorque(self, node_list=None, head_node=None):
        
        if node_list == None:
            node_list = self.network_nodes
        
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
#         ''' % (len(self.network_nodes),len(self.network_nodes),self.network_nodes[0].mpich2_installation_location,self.network_nodes[0].current_user,len(self.network_nodes),len(self.network_nodes),command)
#         
#         print script
#         return script
# 
#     
#         
#     def blockPBSUntilDone(self,jobid):
#         
#         rc = self.network_nodes[0].runSingleCommand("qstat -i %s" % jobid)
# 
#         while rc == 0:
#             time.sleep(60)
#             rc = self.network_nodes[0].runSingleCommand("qstat -i %s" % jobid)
#             
#     
#     def setupMPIEnvironment(self,headnode=None):
#         
#         if headnode==None:
#             headnode = self.network_nodes[0]
#     
#         headnode.setupMPIEnvironment()

   
    ##    
    #    @fn installMpich2(self, node_list=None, head_node=None):
    #
    #    Install Mpich on the virtual cluster
    #    @param self The object pointer
    #    @param node_list List of nodes in network.
    #    @param head_node Head node of Torque setup
   
     
     
    def installMpich2(self,node_list=None,head_node=None):
        # TODO: Make this match installOpenMPI()         
        
        if node_list == None:
            node_list = self.network_nodes
        if head_node==None:
            head_node = node_list[0]
    
        head_node.installMpich2()

   
    ##    
    #   @fn generateOFSKeys(self,node_list=None,head_node=None):
    #
    #    Generate SSH keys for OrangeFS security
    #    @param self The object pointer
    #    @param node_list List of nodes in network.
    #    @param head_node Head node of ssh setup

   
    
    def generateOFSKeys(self,node_list=None,head_node=None):
        if node_list == None:
            node_list = self.network_nodes
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

   
    ##    
    #    @fn generateOFSServerKeys(self,node_list=None,security_node=None)
    #
    #    Generate server SSH keys for OrangeFS security
    #    @param self The object pointer
    #    @param node_list List of nodes in network.
    #    @param security_node Node that generates server keys
    
    
    def generateOFSServerKeys(self,node_list=None,security_node=None):
        if node_list == None:
            node_list = self.network_nodes
        if security_node==None:
            security_node = node_list[0]
        
        #for each server, create a private server key at <orangefs>/etc/orangefs-serverkey.pem
        #cd /tmp/$USER/keys
        #openssl genrsa -out orangefs-serverkey-(remote hostname).pem 2048
        #copy  orangefs-serverkey-(remote_hostname).pem <orangefs>/etc/
        output = []
       
        
        security_node.runSingleCommand("mkdir -p /tmp/%s/security" % security_node.current_user)
        security_node.changeDirectory("/tmp/%s/security" % security_node.current_user)
        
        for node in self.network_nodes:
            keyname = "orangefs-serverkey-%s.pem" % node.hostname
            rc = security_node.runSingleCommand("openssl genrsa -out %s 2048" % keyname,output)
            if rc != 0:
                logging.exception("Could not create server security key for "+node.hostname)
                logging.exception(output) 
                return rc
            rc = security_node.copyToRemoteNode(source="/tmp/%s/security/%s" % (security_node.current_user,keyname), destination_node=node, destination="%s/etc/orangefs-serverkey.pem" % node.ofs_installation_location, recursive=False)
            if rc != 0:
                logging.exception("Could not copy server security key %s for %s " % (keyname,node.hostname))
                return rc
            
            
            #rc = node.runSingleCommand("chown 
        


        return 0

   
    ##    
    #  @fn  generateOFSClientKeys(self,node_list=None,security_node=None)
    #
    #    Generate client SSH keys for OrangeFS security
    #    @param self The object pointer
    #    @param node_list List of nodes in network.
    #    @param security_node Node that generates server keys   
    
    def generateOFSClientKeys(self,node_list=None,security_node=None):
        if node_list == None:
            node_list = self.network_nodes
        if security_node==None:
            security_node = node_list[0]

        # cd /tmp/$USER/keys
        # for each client
        #   openSSl genrsa -out pvfs2-clientkey-{hostname}.pem 1024
        #   copy pvfs2-clientkey-{hostname).pem hostname:{orangefs}/etc
                
        
        output = []
       
        
        security_node.runSingleCommand("mkdir -p /tmp/%s/security" % security_node.current_user)
        security_node.changeDirectory("/tmp/%s/security" % security_node.current_user)

        for node in self.network_nodes:
            keyname = "pvfs2-clientkey-%s.pem" % node.hostname
            rc = security_node.runSingleCommand("openssl genrsa -out %s 1024" % keyname,output)
            if rc != 0:
                logging.exception("Could not create client security key for "+node.hostname)
                logging.exception(output) 
                return rc
            rc = security_node.copyToRemoteNode(source="/tmp/%s/security/%s" % (security_node.current_user,keyname), destination_node=node, destination="%s/etc/pvfs2-clientkey.pem" % node.ofs_installation_location, recursive=False)
            if rc != 0:
                logging.exception("Could not copy client security key %s for %s " % (keyname,node.hostname))
                logging.exception(output) 
                return rc


        return 0
    
   
    ##    
    #    @fn generateOFSKeystoreFile(self,node_list=None,security_node=None):
    #
    #    Generate OrangeFS keystore file for security
    #    @param self The object pointer
    #    @param node_list List of nodes in network.
    #    @param security_node Node that generates server keys

       

    
    def generateOFSKeystoreFile(self,node_list=None,security_node=None):
        if node_list == None:
            node_list = self.network_nodes
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
        for node in self.network_nodes:
            
            server_keyname = "orangefs-serverkey-%s.pem" % node.hostname
            client_keyname = "pvfs2-clientkey-%s.pem" % node.hostname
            if node.alias_list == None:
                node.alias_list = node.getAliasesFromConfigFile(node.ofs_installation_location + "/etc/orangefs.conf")
            if len(node.alias_list) == 0:
                logging.exception( "Could not get aliases")
                return -1
            for alias in node.alias_list:
                if node.hostname in alias:
                    security_node.runSingleCommand('echo "S:%s" >> orangefs-keystore' % alias)
                    security_node.runSingleCommand('openssl rsa -in %s -pubout >> orangefs-keystore' % server_keyname)
                
            security_node.runSingleCommand('echo "C:%s" >> orangefs-keystore' % node.hostname)
            security_node.runSingleCommand('openssl rsa -in %s -pubout >> orangefs-keystore' % client_keyname)
            
        for node in self.network_nodes:

            rc = security_node.copyToRemoteNode(source="/tmp/%s/security/orangefs-keystore" % security_node.current_user, destination_node=node, destination="%s/etc/" % node.ofs_installation_location, recursive=False)
            if rc != 0:
                logging.exception( "Could not copy keystore for %s " % (node.hostname))
                logging.exception( output) 
                return rc
            #now protect the files
            rc = node.runSingleCommand("chmod 400 %s/etc/*.pem" % node.ofs_installation_location)
            if rc != 0:
                logging.exception("Could not protect keys on %s " % (node.hostname))
                logging.exception( output) 
                return rc

        
        return 0


  
    ##
    #  @fn  findExistingOFSInstallation(self,node_list=None):
    #
    #    find an existing OrangeFS installation for each node in the list.
    #
    #    @param self The object pointer
    #    @param node_list List of nodes in network.
    
     
    def findExistingOFSInstallation(self,node_list=None):
        if node_list == None:
            node_list = self.network_nodes
        for node in node_list:
            rc = node.findExistingOFSInstallation()
            if rc != 0:
                return rc
        
        return 0
            
      
    ##
    #       @fn networkOFSSettings(self,
    #             ofs_installation_location,
    #             db4_prefix,
    #             ofs_extra_tests_location,
    #             pvfs2tab_file,
    #             resource_location,
    #             resource_type,
    #             ofs_config_file,
    #             ofs_fs_name,
    #             ofs_hostname_override,
    #             ofs_mount_point,
    #             node_list = None
    #             ):
    #
    #    Manually set OrangeFS settings for each node in the list. Used for retesting.
    #
    #    @param self The object pointer
    #    @param ofs_installation_location Directory where OrangeFS is installed. Should be the same on all nodes.
    #    @param db4_prefix Location of Berkeley DB4
    #    @param ofs_extra_tests_location Location of 3rd party benchmarks
    #    @param pvfs2tab_file Location of pvfs2tab file
    #    @param resource_location Location of OrangeFS source
    #    @param resource_type Form of source: TAR,SVN,LOCAL,BUILDNODE
    #    @param ofs_config_file Location of orangefs.conf file
    #    @param ofs_tcp_port TCP port on which OrangeFS servers run
    #    @param ofs_fs_name Name of OrangeFS in filesystem url
    #    @param ofs_hostname_override Change hostname to this. Needed to work around an openstack issue
    #    @param ofs_mount_point Location of OrangeFS mount_point
    #    @param node_list List of nodes in network
        
    def networkOFSSettings(self,
            ofs_installation_location,
            db4_prefix,
            ofs_extra_tests_location,
            pvfs2tab_file,
            resource_location,
            resource_type,
            ofs_config_file,
            ofs_tcp_port,
            ofs_fs_name,
            ofs_hostname_override = None,
            ofs_mount_point = None,
            node_list = None
            ):

        if node_list == None:
            node_list = self.network_nodes
            
        for i,node in enumerate(node_list):

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
            
            if ofs_tcp_port != None:
                node.ofs_tcp_port = ofs_tcp_port
            
            # Mount point. can be read from mount
            if ofs_mount_point != None:
                node.ofs_mount_point = ofs_mount_point
            
            # Hostname override. Needed to workaround an openstack problem.
            if len(ofs_hostname_override) > 0:
                try:
                    node.hostname = ofs_hostname_override[i]
                    node.runSingleCommandAsRoot("hostname "+node.hostname)
                except:
                    # if not, ignore the error
                    pass
            #node.setEnvironmentVariable("LD_LIBRARY_PATH",node.db4_lib+":"+node.ofs_installation_location+":$LD_LIBRARY_PATH")

  
    ##
    #  @fn exportNFSDirectory(self,directory,nfs_server_list=None,options=None,network=None,netmask=None,node_list=None):
    #
    #    export an NFS directory for each server in the list.
    #
    # @param self The object pointer
    # @param nfs_server_list List of NFS servers
    # @param directory_name Directory to export
    # @param options NFS options
    # @param network Network on which to export
    # @param netmask Netmask for network

        

    def exportNFSDirectory(self,directory,nfs_server_list=None,options=None,network=None,netmask=None):
        if nfs_server_list == None:
            nfs_server_list = self.network_nodes
        
        rc = 0
        for nfs_server in nfs_server_list:
            rc += nfs_server.exportNFSDirectory(directory,options,network,netmask)

        return rc
 
  
    ##
    #    @fn mountNFSDirectory(self,nfs_share,mount_point,options="",nfs_client_list=None):
    #
    #    Mount an NFS share at mount_point for each client in the client list.
    #
    # @param self The object pointer
    # @param nfs_share NFS share to mount
    # @param mount_point NFS mount_point on machine
    # @param options NFS mount options
    # @param nfs_client_list List of NFS client nodes
         
    
    def mountNFSDirectory(self,nfs_share,mount_point,options="",nfs_client_list=None):
        
        if nfs_client_list == None:
            nfs_client_list = self.network_nodes
    
        fail = 0
        
        for nfs_client in nfs_client_list:
            rc = nfs_client.mountNFSDirectory(nfs_share,mount_point,options)
            if rc != 0:
                logging.exception( "Mounting %s at %s with options %s on %s failed with RC=%d" %(nfs_share,mount_point,options,nfs_client.ip_address,rc))
                fail = fail+1
            
        
        return fail

  

    ##
    # @fn installOpenMPI(self,mpi_nfs_directory=None,build_node=None,mpi_local_directory=None,node_list=None):
    #
    # This function installs OpenMPI software at the mpi_nfs_directory
    #
    # @param self The object pointer
    # @param build_node Node used to build OpenMPI
    # @param mpi_nfs_directory Location to install OpenMPI. This should be an nfs directory accessible from all nodes
    # @param mpi_local_directory Location to build OpenMPI. This should be a local directory on the build node
    # @param node_list List of nodes in OpenMPI network.
    
    

    def installOpenMPI(self,mpi_nfs_directory=None,build_node=None,mpi_local_directory=None,node_list=None):
        if node_list == None:
            node_list = self.network_nodes
        if build_node == None:
            build_node = node_list[0]
        
        
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
        rc = self.mountNFSDirectory(nfs_share=nfs_share,mount_point=self.mpi_nfs_directory,options="bg,intr,noac,nfsvers=3")
        
        # build mpi in the build location, but install it to the nfs directory
        rc = build_node.installOpenMPI(install_location=self.mpi_nfs_directory,build_location=self.mpi_nfs_directory)
    
        
        
        
        # how many slots per node do we need?

        # Testing requires np=4
        MAX_SLOTS = 4
        number_nodes = len(self.network_nodes)
    
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
            build_node.runSingleCommand("grep -v localhost /etc/hosts | awk '{print \$2 \"\\tslots=%r\"}' > %s/openmpihosts" % (slots,self.mpi_nfs_directory))

        # update runtest to use openmpihosts file - should be done in patched openmpi
        #print 'sed -i s,"mpirun -np","mpirun --hostfile %s/openmpihosts -np",g %s/%s/ompi/mca/io/romio/test/runtests' % (self.mpi_nfs_directory,self.mpi_nfs_directory,build_node.openmpi_version)
        #build_node.runSingleCommand('sed -i s,"mpirun -np","mpirun --hostfile %s/openmpihosts -np",g %s/%s/ompi/mca/io/romio/test/runtests' % (self.mpi_nfs_directory,self.mpi_nfs_directory,build_node.openmpi_version))
        #build_node.runSingleCommand('chmod a+x %s/%s/ompi/mca/io/romio/test/runtests' % (self.mpi_nfs_directory,build_node.openmpi_version))
        # update bashrc     
        for node in self.network_nodes:
            #node.openmpi_installation_location = self.mpi_nfs_directory
            # have we already done this?
            #done = node.runSingleCommand("grep -v %s /home/%s/.bashrc" % (node.openmpi_installation_location,node.current_user))
            #done = 0
            #if done == 0:
            node.runSingleCommand("echo 'export PATH=%s/openmpi/bin:%s/bin:\$PATH' >> /home/%s/.bashrc" % (self.mpi_nfs_directory,node.ofs_installation_location,node.current_user))
            node.runSingleCommand("echo 'export LD_LIBRARY_PATH=%s/openmpi/lib:%s/lib:%s/lib:\$LD_LIBRARY_PATH' >> /home/%s/.bashrc" % (self.mpi_nfs_directory,node.ofs_installation_location,node.db4_dir,node.current_user))
            node.runSingleCommand("echo 'export PVFS2TAB_FILE=%s/etc/orangefstab' >> /home/%s/.bashrc" % (node.ofs_installation_location,node.current_user))
        
        return rc


  
    ##
    # @fn   setupHadoop(self,hadoop_nodes=None,master_node=None):
    #
    #    Hadoop should be installed with required software. This configures hadoop 
    #    for OrangeFS testing on the virtual cluster.
    #
    # @param self The object pointer
    # @param hadoop_nodes Nodes on which to setup hadoop
    # @param master_node Hadoop master node
     
    def setupHadoop(self,hadoop_nodes=None,master_node=None):
        if hadoop_nodes == None:
            hadoop_nodes = self.network_nodes
        
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
            master_node.copyToRemoteNode(source="%s/test/automated/hadoop-tests.d/conf/" % master_node.ofs_source_location,destination_node=node,destination="%s/conf/" % node.hadoop_location,recursive=True)
            
            # update mapred-site.xml
            node.runSingleCommand("sed -i s/__NODE001__/%s/ %s/conf/mapred-site.xml" % (master_node.hostname,node.hadoop_location))
            
            # update core-site.xml
            node.runSingleCommand("sed -i s,__MNT_LOCATION__,%s, %s/conf/core-site.xml" % (node.ofs_mount_point,node.hadoop_location))

            # point slave node to master
            node.runSingleCommand("echo '%s' > %s/conf/masters" % (master_node.hostname,node.hadoop_location))
            
            # notify master of new slave
            master_node.runSingleCommand("echo '%s' >> %s/conf/slaves" % (node.hostname,master_node.hadoop_location))
            
        master_node.runSingleCommand("%s/bin/start-mapred.sh" % master_node.hadoop_location)
        time.sleep(20)
        # hadoop dfs -ls is our "ping" for hadoop. 
        rc = master_node.runSingleCommand("%s/bin/hadoop dfs -ls /" % master_node.hadoop_location)
        if rc != 0:
            print "Hadoop setup failed. See logs for more information."
        else:
            print "Hadoop setup successfully"
            master_node.runSingleCommand("%s/bin/hadoop dfs -mkdir /user/%s" % (master_node.hadoop_location,master_node.current_user))
            
        return rc
        
