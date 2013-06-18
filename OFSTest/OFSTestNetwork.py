#!/usr/bin/python
#
# OFSTestNetwork.py
#
# Create the class with three lists of OFSTestNodes
#
# Client nodes
# Server nodes
# Build nodes
#
# We will also always have exactly one local node.
# Local node
#
# Client Nodes: These nodes run the OrangeFS client. All tests are run on all client nodes
#
# Server Nodes: These nodes run the OrangeFS server. 
#
# Build Nodes: All software is built on the build nodes and copied to other nodes.
#
# Local Node: The local machine. May or may not serve any other role.
#
#


import re
import os
import OFSTestNode
import OFSTestLocalNode 
import OFSTestRemoteNode 
import OFSEC2ConnectionManager
from boto import *
import Queue
import threading
import time

class OFSTestNetwork(object):

    def __init__(self):
    
        # Configuration for ec2 
        self.ec2_connection_manager = None
        # dictionary of instances
           
        self.created_nodes = []
        
        self.local_master = OFSTestLocalNode.OFSTestLocalNode()
        

        #self.created_nodes['127.0.0.1']=self.local_master
    
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
        
    def addRemoteNode(self,username,ip_address,key,is_ec2=False):
        #This function adds a remote node
        
        # Is this a remote machine or an existing ec2 node?
        remote_node = OFSTestRemoteNode.OFSTestRemoteNode(username=username,ip_address=ip_address,key=key,local_node=self.local_master,is_ec2=is_ec2)
        
     
        # Get everybodys keys straight.
        for existing_node in self.created_nodes:
         #   if remote_address != '127.0.0.1': #skip localhost
            # upload key for new node to old node
            existing_node.uploadRemoteKeyFromLocal(self.local_master,remote_node.ip_address)
            # upload key for old node to new node
            remote_node.uploadRemoteKeyFromLocal(self.local_master,existing_node.ip_address)
                
        # Add to the node dictionary
        self.created_nodes.append(remote_node)
        
        # Return the new node
        return remote_node
            
        
    def runSimultaneousCommands(self,node_list,node_function=OFSTestNode.OFSTestNode.runSingleCommand,args=[],kwargs={}):
        
        #passes in a thread class that does the work on a node list with arguments args
         
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
          
        
    
    
    def addEC2Connection(self,ec2_config_file,key_name,key_location):
        #This function initializes the ec2 connection
        self.ec2_connection_manager = OFSEC2ConnectionManager.OFSEC2ConnectionManager(ec2_config_file)
        self.ec2_connection_manager.setEC2Key(key_name,key_location)
        
    
    def createNewEC2Nodes(self,number_nodes,image_name,machine_type):
        
        # This function creates number nodes on the ec2 system. 
        # It returns a list of nodes
        
        new_instances = self.ec2_connection_manager.createNewEC2Instances(number_nodes,image_name,machine_type)
        new_ofs_test_nodes = []
        
        for i in new_instances:
            i.update()
            print "Instance %s at %s has state %s with code %r" % (i.id,i.ip_address,i.state,i.state_code)
            
            while i.state_code == 0:
                
                time.sleep(10)
                i.update()
                print "Instance %s at %s has state %s with code %r" % (i.id,i.ip_address,i.state,i.state_code)
            
                
            #pprint(i.__dict__)
        
            # Create the node and get the instance name
            if "ubuntu" in image_name:
                name = 'ubuntu'
            else:
                name = 'ec2-user'
                
            new_node = OFSTestRemoteNode.OFSTestRemoteNode(username=name,ip_address=i.ip_address,key=self.ec2_connection_manager.instance_key_location,local_node=self.local_master,is_ec2=True)
            new_ofs_test_nodes.append(new_node)
        
            # Add the node to the created nodes list.
            self.created_nodes.append(new_node)
        
        # upload the remote key to all the nodes
        for node in new_ofs_test_nodes:
            self.runSimultaneousCommands(node_list=new_ofs_test_nodes,node_function=OFSTestRemoteNode.OFSTestRemoteNode.uploadRemoteKeyFromLocal, args=[self.local_master,node.ip_address])

            
        # return the list of newly created nodes.
        return new_ofs_test_nodes

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
        
        
    def updateEC2Nodes(self):
        # This only updates the EC2 controlled nodes
        ec2_nodes = [node for node in self.created_nodes if node.is_ec2 == True]
        self.updateNodes(ec2_nodes)       
            
        
    def updateNodes(self,node_list=None):
        if node_list == None:
            node_list = self.created_nodes
        self.runSimultaneousCommands(node_list=node_list,node_function=OFSTestNode.OFSTestNode.updateNode)
        # Wait for reboot
        time.sleep(20)
    
    def installRequiredSoftware(self,node_list=None):
        if node_list == None:
            node_list = self.created_nodes
        self.runSimultaneousCommands(node_list=node_list,node_function=OFSTestNode.OFSTestNode.installRequiredSoftware)

    def buildOFSFromSource(self,resource_type,resource_location,build_node=None,download_location="",configure_opts="",make_opts="",build_kmod=True):
        if build_node == None:
            build_node = self.created_nodes[0]
            
        if download_location == "":
            download_location = "/tmp/%s/" % build_node.current_user
        
        if resource_type == "LOCAL":
            # copy from the local to the buildnode, then pretend it is a "buildnode" resource.
            # shameless hack.
            self.local_master.copyToRemoteNode(resource_location,build_node,download_location,True)
            resource_type == "BUILDNODE"
            resource_location = download_location
        
        build_node.copyOFSSource(resource_type,resource_location,download_location)
        #"TAR","http://www.orangefs.org/downloads/LATEST/source/orangefs-2.8.7.tar.gz",)
        build_node.configureOFSSource(configure_opts,build_kmod)
        build_node.makeOFSSource(make_opts)

    
    def installOFSBuild(self,build_node=None,install_opts=""):
        if build_node == None:
            build_node = self.created_nodes[0]
        build_node.installOFSSource(install_opts)
    
    def buildAndInstallOFSFromSource(self,resource_type,resource_location,build_node=None,download_location="",configure_opts="",make_opts="",install_opts="",build_kmod=True):
        if build_node == None:
            build_node = self.created_nodes[0]
        self.buildOFSFromSource(resource_type,resource_location,build_node,download_location,configure_opts,make_opts,build_kmod)
        self.installOFSBuild(build_node,install_opts)
        self.installOFSTests(build_node,configure_opts)

    def installOFSTests(self,client_node=None,configure_options=""):
        if client_node == None:
            client_node = self.created_nodes[0]
        client_node.installOFSTests(configure_options)

    def installBenchmarks(self,client_node=None,configure_options=""):
        if client_node == None:
            client_node = self.created_nodes[0]
        client_node.installBenchmarks("http://devorange.clemson.edu/pvfs/benchmarks-20121017.tar.gz","/tmp/%s/benchmarks" % client_node.current_user)
        
    def configureOFSServer(self,ofs_fs_name,master_node=None,node_list=None,pvfs2genconfig_opts=""):
        if node_list == None:
            node_list = self.created_nodes
        if master_node == None:
            master_node = self.created_nodes[0]
        master_node.configureOFSServer(node_list,ofs_fs_name,pvfs2genconfig_opts)
        
    def copyOFSToNodeList(self,destination_list=None):
        if destination_list == None:
            destination_list = self.created_nodes
        # This assumes that the OFS installation is at the destination_list[0]
        list_length = len(destination_list)
        
        # If our list is of length 1 or less, return.
        if list_length <= 1:
            return
        
        #copy from list[0] to list[list_length/2]
        print "Copying from %s to %s" % (destination_list[0].ip_address,destination_list[list_length/2].ip_address)
        destination_list[0].copyOFSInstallationToNode(destination_list[list_length/2])
        
        
        #Run the following commands on separate threads
        # self.copyOFSToNodeList(destination_list[:(list_length/2)-1])
        # self.copyOFSToNodeList(destination_list[list_length/2:])

        #partition the list.
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
                    
                    print "Copying %r" % list
                    self.manager.copyOFSToNodeList(list)
                    
                        
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
          
    def stopOFSServers(self,node_list=None):
        if node_list == None:
            node_list = self.created_nodes
        self.runSimultaneousCommands(node_list=node_list,node_function=OFSTestNode.OFSTestNode.stopOFSServer)
        time.sleep(20)

    def startOFSServers(self,node_list=None):
        if node_list == None:
            node_list = self.created_nodes
        self.runSimultaneousCommands(node_list=node_list,node_function=OFSTestNode.OFSTestNode.startOFSServer)
        time.sleep(20)

    def startOFSClient(self,client_node=None):
        if client_node == None:
            client_node = self.created_nodes[0]
        client_node.installKernelModule()
        #client_node.runSingleCommand('/sbin/lsmod | grep pvfs')
        client_node.startOFSClient()
        time.sleep(10)
        #client_node.runSingleCommand('ps aux | grep pvfs')
    
    def mountOFSFilesystem(self,mount_fuse=False,client_node=None,):
        if client_node == None:
            client_node = self.created_nodes[0]
        client_node.mountOFSFilesystem(mount_fuse=mount_fuse)
        time.sleep(10)
        print "Checking mount"
        mount_res=client_node.runSingleCommandBacktick("mount")
        print mount_res
        #client_node.runSingleCommand("touch %s/myfile" % client_node.ofs_mountpoint)
        #client_node.runSingleCommand("ls %s/myfile" % client_node.ofs_mountpoint)
    def stopOFSClient(self,client_node=None):
        if client_node == None:
            client_node = self.created_nodes[0]
        client_node.stopOFSClient()
        #client_node.runSingleCommand("mount")
        #client_node.runSingleCommand("touch %s/myfile" % client_node.ofs_mountpoint)
        #client_node.runSingleCommand("ls %s/myfile" % client_node.ofs_mountpoint)
    
    def terminateAllEC2Nodes(self):
        for node in self.created_nodes:
             if node.is_ec2 == True:
                self.terminateEC2Node(node)
 
    def installTorque(self):
        
        # first install torque on the head node.
        self.created_nodes[0].installTorqueServer()
        
        #print "Restarting Torque Server"
        #self.created_nodes[0].restartTorqueServer()
        #print "Restarting Torque Clients"
        
        #    node.restartTorqueMom()
        
        #now add all the client nodes
        
        for i in range(len(self.created_nodes)):
                self.created_nodes[i].installTorqueClient(self.created_nodes[0])
                
                torque_node_cpu = self.created_nodes[i].runSingleCommandBacktick('grep proc /proc/cpuinfo | wc -l')
                if "ubuntu" in self.created_nodes[0].distro.lower() or "mint" in self.created_nodes[0].distro.lower() or "debian" in self.created_nodes[0].distro.lower():
                    self.created_nodes[0].runSingleCommand("sudo bash -c 'echo \"%s np=%s\" >> /var/spool/torque/server_priv/nodes'" % (self.created_nodes[i].host_name,torque_node_cpu))
                elif "centos" in self.created_nodes[0].distro.lower() or "scientific linux" in self.created_nodes[0].distro.lower() or "redhat" in self.created_nodes[0].distro.lower() or "fedora" in self.created_nodes[0].distro.lower():
                    self.created_nodes[0].runSingleCommandAsBatch("sudo bash -c 'echo \"%s np=%s\" >> /var/lib/torque/server_priv/nodes'" % (self.created_nodes[i].host_name,torque_node_cpu))
        self.created_nodes[0].restartTorqueServer()    

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
            
    def checkTorque(self):
        pbsnodeout = self.created_nodes[0].runSingleCommandBacktick("pbsnodes -l | grep down | awk '{print \$1}'")
        print pbsnodeout
        return pbsnodeout.split("\n")
    
    def generatePAVConf(self,**kwargs):
        
        for node in self.created_nodes:
            pav_conf = node.generatePAVConf()
            self.local_master.copyToRemoteNode("/tmp/pav.conf", node, pav_conf)
    
    def makePBSScript(self):
        '''
        #!/bin/sh
        #PBS -l walltime=0:10:0
        #PBS -l nodes=8
        #PBS -j oe
        #PBS -q shared

        nprocs=4

        $CLUSTER_DIR/pav/pav_start -c $PAV_CONFIG -n \$nprocs >/dev/null

        eval \$( $CLUSTER_DIR/pav/pav_info -c $PAV_CONFIG)
        export PVFS2TAB_FILE

        PATH=${CLUSTER_DIR}/mpich2/bin:\${PATH}
        mpdboot --file=\${WORKINGDIR}/compnodes --totalnum=\$nprocs

        mpiexec -np \$nprocs $@
        mpdallexit
        $CLUSTER_DIR/pav/pav_stop -c $PAV_CONFIG >/dev/null" '''
        
    def blockPBSUntilDone(self):

        '''
        while true ; do 
            qstat -i $1 >/dev/null 2>&1 
            if [ $? -eq 0 ] ; then
                sleep 60
                continue
            else
                break
            fi	
        done
        '''
    
    def setupMPIEnvironment(self,headnode=None):
        
        if headnode==None:
            headnode = self.created_nodes[0]
    
        headnode.setupMPIEnvironment()
     
    def installMpich2(self,headnode=None):
        if headnode==None:
            headnode = self.created_nodes[0]
    
        headnode.installMpich2()
       
        
def test_driver():
    my_node_manager = OFSTestNetwork()
    my_node_manager.addEC2Connection("ec2-cred/ec2rc.sh","Buildbot","/home/jburton/buildbot.pem")

    nodes  = my_node_manager.createNewEC2Nodes(4,"cloud-ubuntu-12.04","c1.small")
    #nodes  = my_node_manager.createNewEC2Nodes(6,"cloud-sl6","c1.small")

    '''
    ec2_ip_addresses = ['10.20.102.98','10.20.102.97','10.20.102.99','10.20.102.100']

    nodes = []

    for my_address in ec2_ip_addresses:
        nodes.append(my_node_manager.addRemoteNode(ip_address=my_address,username="ubuntu",key="/home/jburton/buildbot.pem",is_ec2=True))
    for node in nodes:
        node.setEnvironmentVariable("VMSYSTEM","cloud-ubuntu-12.04")
    '''

    print ""
    print "==================================================================="
    print "Updating New Nodes"
    
    my_node_manager.updateEC2Nodes()

    print ""
    print "==================================================================="
    print "Installing Required Software"
    my_node_manager.installRequiredSoftware()

    print ""
    print "==================================================================="
    print "Installing Torque"
    my_node_manager.installTorque()

    print ""
    print "==================================================================="
    
    print "Check Torque"
    my_node_manager.checkTorque()
     
    print ""
    print "==================================================================="
    print "Downloading and Installing OrangeFS"

    my_node_manager.buildAndInstallOFSFromSource(resource_type="SVN",resource_location="http://orangefs.org/svn/orangefs/branches/stable")
    #my_node_manager.buildAndInstallOFSFromSource(resource_type="SVN",resource_location="http://orangefs.org/svn/orangefs/trunk")
    #my_node_manager.buildAndInstallOFSFromSource(resource_type="TAR",resource_location="http://www.orangefs.org/downloads/LATEST/source/orangefs-2.8.7.tar.gz")
    #for node in my_node_manager.created_nodes:
    #    node.setEnvironmentVariable("LD_LIBRARY_PATH","%s/lib:/opt/db4/lib:$LD_LIBRARY_PATH" % (node.ofs_installation_location))
    #    node.setEnvironmentVariable("LD_PRELOAD","%s/lib/libofs.so:%s/lib/libpvfs2.so" % (node.ofs_installation_location,node.ofs_installation_location))
    print ""
    print "==================================================================="
    print "Installing Benchmarks"


    my_node_manager.installBenchmarks()



    print ""
    print "==================================================================="
    print "Copy installation to all nodes"

    my_node_manager.configureOFSServer()
    my_node_manager.copyOFSToNodeList()


        
    print ""
    print "==================================================================="
    print "Start OFS Server"
    my_node_manager.startOFSServers()

    print ""
    print "==================================================================="
    print "Start Client"
    my_node_manager.startOFSClient()

    print ""
    print "==================================================================="
    print "Mount Filesystem"
    my_node_manager.mountOFSFilesystem()


    print ""
    print "==================================================================="
    print "Run Tests"
    communicate = []
    rc = nodes[0].runSingleCommand(command="ps aux | grep pvfs",output=communicate)
    print "RC = %d" % rc
    print "STDOUT = "+communicate[1]
    print "STDERR = "+communicate[2]
    nodes[0].runSingleCommand("mount")
    nodes[0].runSingleCommand("dmesg")


    import OFSSysintTest
    import OFSVFSTest
    import OFSUserintTest

    rc = 0


    def write_output(filename,function,rc):
        output = open(filename,'a+')
        if rc != 0:
            output.write("%s........................................FAIL: RC = %d\n" % (function.__name__,rc))
        else:
            output.write("%s........................................PASS.\n" % function.__name__)
        output.close()

    #filename = "OFSTestNetwork.log"    
    output = open(filename,'w+')
    output.write("Sysint Tests ==================================================\n")
    output.close()

    for callable in OFSSysintTest.__dict__.values():
        try:
            #print "Running %s" % callable.__name__
            rc = nodes[0].runOFSTest(callable)
            write_output(filename,callable,rc)
        except AttributeError:
            pass
        except TypeError:
            pass

    output = open(filename,'a+')
    nodes[0].runSingleCommandBacktick("dmesg")
    output.write("VFS Tests ==================================================\n")
    output.close()

    for callable in OFSVFSTest.__dict__.values():
        try:
            rc = nodes[0].runOFSTest(callable)
            write_output(filename,callable,rc)
        except AttributeError:
            pass
        except TypeError:
            pass
   
    #my_node_manager.stopOFSServers()
    #my_node_manager.stopOFSClient()

    #for node in my_node_manager.created_nodes:

     #   node.setEnvironmentVariable("LD_PRELOAD","%s/lib/libofs.so:%s/lib/libpvfs2.so" % (node.ofs_installation_location,node.ofs_installation_location))
        #node.setEnvironmentVariable("LD_LIBRARY_PATH","%s/lib:/opt/db4/lib:$LD_LIBRARY_PATH" % (node.ofs_installation_location))
    
    #my_node_manager.startOFSServers()
    ''' 
    output = open(filename,'a+')
    nodes[0].runSingleCommandBacktick("dmesg")
    output.write("Userint Tests ==================================================\n")
    output.close()

    for callable in OFSUserintTest.__dict__.values():
        try:
            rc = nodes[0].runOFSTest(callable)
            write_output("OFSTestNetwork.log",callable,rc)
        except AttributeError:
            pass
        except TypeError:
            pass

    '''



      
    print ""
    print "==================================================================="
    print "Stop Client"
    my_node_manager.stopOFSServers()



    print ""
    print "==================================================================="
    print "Terminating Nodes"

  #  my_node_manager.terminateAllEC2Nodes()
   
    #my_node_manager.runSimultaneousCommands(node_list=nodes,args=["sudo apt-get update && sudo apt-get -y dist-upgrade < /dev/zero && sudo reboot"])
    #my_node_manager.runSimultaneousCommands(node_list=nodes,args=["sudo reboot"])



    #print "Getting source"
    #my_node_manager.runSimultaneousCommands(node_list=nodes,args=["sudo apt-get -y install subversion"])
    #my_node_manager.runSimultaneousCommands(node_list=nodes,node_function=OFSTestNode.copyOFSSource,kwargs={"resource_type": "SVN","resource": "http://orangefs.org/svn/orangefs/trunk","dest_dir": "/tmp/ubuntu"})

    #for n in nodes:
    #    my_node_manager.terminateEC2Node(n)

    #print my_node.runSingleCommand("whoami")

#Call script with -t to test
if len(sys.argv) > 1 and sys.argv[1] == "-t":
    test_driver()