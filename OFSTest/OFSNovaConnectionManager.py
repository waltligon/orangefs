#!/usr/bin/python


#import os
from datetime import datetime, timedelta
import glanceclient
import json
import keystoneclient
import keystoneclient.exceptions
import keystoneclient.v2_0
import logging
import os
from pprint import pprint
import random
import re
import string
import string
import sys
import time
import uuid

import OFSCloudConnectionManager
import OFSTestRemoteNode
import neutronclient.neutron.client as neutronclient
import novaclient.v1_1 as novaclient


## 
# @class OFSNovaConnectionManager
#
# @brief This class manages the cloud connection using the OpenStack Nova API. It has no awareness of OFSTestNodes or the OFSTestNetwork.
#
class OFSNovaConnectionManager(OFSCloudConnectionManager.OFSCloudConnectionManager):
  
    ##
    #
    # @fn __init__(self,cloud_config_file=None,region_name=None):
    #
    # Initialization
    #
    # @param self The object pointer
    # @param cloud_config_file Path to Cloudrc.sh file.
    # @param region_name Name of Cloud region to connect to.
    #

    
    def __init__(self,cloud_config_file=None,region_name=None,password_file=None):
        
        super(OFSNovaConnectionManager,self).__init__()
        ##
        # @var self.cloud_is_secure
        # @brief Is this http or https?
        self.cloud_instance_key = None
               
        # @var String self.cloud_instance_key
        # @brief Name of key (in Cloud) used to access instance via SSH
        self.cloud_instance_key_location = None
        
        self.nova_auth_url = "";
        self.nova_default_role_name = "";
        self.nova_username = "";
        self.nova_tenant_name = "";
        self.nova_passwdfile = "";
        self.nova_password = "";
        self.nova_network_id = "";
        self.keystoneapi = None;
        self.novaapi = None;
        self.glance_endpoint = None;
        self.glance_endpoint = None;
        self.glanceapi = None;
        
        
        # hard code this in for now.
        #self.nova_network_name = 'flat'
        #self.nova_network_id = '42f48524-45d3-41fa-aee4-4e5ecf762f79'
        self.nova_network_name = 'OrangeFS'
        self.nova_network_id = 'efdc9f8f-2b90-421b-b041-aedc1aadce16' 
     
    
        if cloud_config_file is not None:
            # Read the openrc.sh file if provided
            self.readCloudConfigFile(cloud_config_file)
            self.readCloudPasswordFile(password_file)
        else:
            # Otherwise, get the configuration from the environment.
            self.getCloudConfigFromEnvironment()
        

                
    ##
    #
    # @fn readCloudConfigFile(self,filename):
    #
    # Reads relevant values from openrc.sh file.
    #
    # @param self The object pointer
    #
    # @param filename Path to openrc.sh file
    #
            
    
    def readCloudConfigFile(self,filename):
        
        #open Cloud file
        msg = "Reading config file %s" % filename

        conf_rc = open(os.path.expandvars(filename),'r')
        
        for line in conf_rc:
            if "export OS_AUTH_URL" in line:
                # check for AUTH_URL
                
                (export,variable,self.nova_auth_url) = re.split(" |=",line.rstrip())
                

            elif "export OS_TENANT_ID" in line:

                (export,variable,self.nova_tenant_id) = re.split(" |=",line.rstrip())
            
            elif "export OS_TENANT_NAME" in line:

                (export,variable,self.nova_tenant_name) = re.split(" |=",line.rstrip())
                self.nova_tenant_name=self.nova_tenant_name.rstrip("\"").lstrip("\"")

            elif "export OS_USERNAME" in line:

                (export,variable,self.nova_username) = re.split(" |=",line.rstrip())

    ##
    #
    # @fn readCloudPasswordFile(self,filename):
    #
    # Reads relevant values from password file. This file contains the password for nova that is used for the OS_PASSWORD value
    #
    # @param self The object pointer
    #
    # @param filename Path to file
    #
    
    def readCloudPasswordFile(self,filename):
        self.nova_password = string.rstrip(open(os.path.expandvars(filename)).readlines()[0])
        
        
               
    ##
    #      
    # @fn connect(self,debug): 
    # Gets region info and connects to Cloud. Cloud.connection.CloudConnection object should be stored in self.cloud_connection.
    # @param self The object pointer
    # @param debug  Debug level.
    #

    def connect(self,debug=0):
        
        logging.debug("AUTH_URL=%s NOVA_USERNAME=%s NOVA_PASSWORD=%s NOVA_TENANT_NAME=%s " % (self.nova_auth_url, self.nova_username, self.nova_password, self.nova_tenant_name))
        self.keystoneapi = keystoneclient.v2_0.Client(username=self.nova_username, password=self.nova_password, tenant_name=self.nova_tenant_name, auth_url=self.nova_auth_url)
        self.novaapi = novaclient.Client(self.nova_username, self.nova_password, self.nova_tenant_name, self.nova_auth_url, no_cache=True)
        self.glance_endpoint = self.keystoneapi.service_catalog.get_endpoints("image")["image"][0]["publicURL"]
        self.glance_endpoint = self.glance_endpoint.replace("/v1","")
        self.glanceapi = glanceclient.Client('1',endpoint=self.glance_endpoint,token=self.keystoneapi.auth_token)

   
    ##
    # @fn terminateCloudInstance(self,ip_address)
    # Terminates a running Cloud instance 
    #
    # @param self The object pointer
    # @param	ip_address IP address (internal) of the node.
    #
    # @return 1	Instance not found for that ip address
    # @return 0	Instance terminated.
    #
    #
        
    def terminateCloudInstance(self,ip_address):
        
        self.checkCloudConnection()
        
        
        server_list = []
        #server_list = [s for s in self.novaapi.servers.list() if s.addresses[self.nova_network_name][0]['addr'] == ip_address]
        
        for s in self.novaapi.servers.list():
            try:
                if s.addresses[self.nova_network_name][0]['addr'] == ip_address:
                    server_list.append(s)
            except:
                pass
        
        
        
        print "Attempting to delete server at %s" % ip_address
        if len(server_list) > 0:
            print "Deleting server"
            server_list[0].delete()
            return 0
        else:
            print "Could not find server"
            return 1
            
            
            
    ##
    #
    # @fn createNewCloudInstances(self,number_nodes,image_name,flavor_name): 
    # Creates new EC2 instances and returns list of them.
    #
    # @param self The object pointer
    # @param number_nodes  Number of nodes to create
    # @param image_name Image to run. (e.g. "cloud-ubuntu-12.04")
    # @param flavor_name Image "flavor" (e.g. "m1.medium")
    # @param subnet_id Id of subnet instance should run on 
    #
    # @return    A list of new instances.
    #        
        
    def createNewCloudInstances(self,number_nodes,image_name,flavor_name,subnet_id=None,instance_suffix=""):
        self.checkCloudConnection()  
        
        # This creates a new instance for the system of a given machine type
        
        # get the image ID for the operating system
        if self.cloud_image_list == None:
            self.getAllCloudImages()
        
        # now let's find the os name in the image list
        image = next((i for i in self.cloud_image_list if i.name == image_name), None)
        
        if image == None:
            logging.exception( "Image %s Not Found!" % image_name)
            return None
        
        flavor = next((f for f in self.novaapi.flavors.list() if f.name == flavor_name),None)
        if flavor == None:
            logging.exception("Flavor %s not found!" % flavor_name)
            return None
        
        msg = "Creating %d new %s %s instances." % (number_nodes,image_name,flavor_name)
        print msg
        logging.info(msg)
        
        #print image.__dict__
        new_instances = []

        for index in range(0,number_nodes):
            instance = self.novaapi.servers.create("ofsnode-%03d%s"%(index+1,instance_suffix), image.id, flavor.id, key_name=self.cloud_instance_key, nics = [ { "net-id" : self.nova_network_id } ])
            msg = "Created new Cloud instance %s " % instance.name
            logging.info(msg)
            print msg
            new_instances.append(instance)
        
        return new_instances
    
    

    ##      
    # @fn associateIPAddresses(self,instances[],domain=None):	
    # Creates an new external IP address and associates	with the instances in the array.
    # @param self The object pointer
    # @param instances List of instances to associate
    # @param domain Domain on which to allocate addresses
    # @return A list of the external addresses
    #

    def associateIPAddresses(self,instances=[],domain=None):
        
        external_addresses = []
        floating_ip = None
        
        # reallocate free floating ips
        floating_ip_list = [ip for ip in self.novaapi.floating_ips.list() if ip.fixed_ip == None]
        
        # if we don't have enough, create the ones we need.
        if len(floating_ip_list) < len(instances):
            for i in range(len(instances)-len(floating_ip_list)):
                new_floating_ip = self.novaapi.floating_ips.create();
                floating_ip_list.append(new_floating_ip)
            
        for idx,instance in enumerate(instances):
            msg = "Associating external ip %s with instance %s" % (floating_ip_list[idx].ip,instance.name)
            print msg
            logging.info(msg) 
            self.novaapi.servers.add_floating_ip(instance,floating_ip_list[idx])
            external_addresses.append(floating_ip_list[idx].ip)

        #external_addresses = [s.addresses[self.nova_network_name][0]['addr'] for s in instances]
        msg = "Waiting 60 seconds for external ip address association."
        print msg
        logging.info(msg) 
        time.sleep(60)

        
        return external_addresses
        
    ##
    #
    # @fn checkCloudConnection(self):	
    # Checks to see if the Cloud connection is available.	Connects if it isn't.
    # @param self The object pointer
    #

    def checkCloudConnection(self):
        if self.novaapi == None:
            self.connect()

    ##
    #
    # @fn getAllCloudInstances(self):	
    # Gets all instances from Cloud connection. Returns a	list of instances.
    # @param self The object pointer
    #
    
    def getAllCloudInstances(self):
        self.checkCloudConnection()
        self.cloud_instances = [s for s in self.novaapi.servers.list()]
        return self.cloud_instances


        ##
    #
    # @fn getAllCloudImages(self):    
    # Gets all images from Cloud connection. Returns a    list of images.
    # @param self The object pointer
    #
    
    def getAllCloudImages(self):
        self.checkCloudConnection()

        self.cloud_image_list = self.glanceapi.images.list();
        return self.cloud_image_list

    ##
    #
    # @fn	printAllInstanceStatus(self):	
    # Prints the status of all instances. For debugging.
    # @param self The object pointer
    #
    
    def printAllInstanceStatus(self):
        self.getAllCloudInstances()
        for instance in self.cloud_instance_list:
            print "Instance %s at %s has status %s" % (instance.id,instance.ip_address,instance.status)

    ##
    #
    # @fn	instanceIsDaysOld(self,instance,days_old):	
    # Tests whether an instance is so many days old.
    # @param self The object pointer
    # @param instance Instance to check
    # @param days_old Age to check
    # 
    # @return True if instance is at least days_old days old
    # @return Fals if instance is not days_old
    #
        
    def instanceIsDaysOld(self,instance,days_old=7):
        today = datetime.today()
        launch = datetime.strptime(instance.launch_time,"%Y-%m-%dT%H:%M:%S.000Z")
        # 2012-12-22T00:05:30.000Z
        week = timedelta(days=days_old)
              
        print today
        print launch
        
        if today - launch >= week:
            print "Expired"
            return True
        else:
            return False
    
    def refreshCloudInstanceList(self,instances=[]):
        new_instances = []
        for instance in instances:
            instance = self.novaapi.servers.get(instance)
            new_instances.append(instance)
        return new_instances
            
        

    ##
    # deleteOldInstances(days_old,name_filter,key_filter)
    #
    # Delete instances over a certain number of days old.
    # @param self The object pointer
    # @param days_old Age to check
    # @param name_filter Filter to only delete instances matching name
    # @param key_filter Filter to only delete instance associated with this Cloud key
    #
    #
    def deleteOldInstances(self,days_old=7,name_filter="",key_filter=""):    
        
        self.checkCloudConnection()
        
        for i in self.cloud_instance_list:
            #pprint(i.__dict__)
            
            if name_filter in i.public_dns_name and i.key_name.lower() == key_filter.lower():
                print "Instance name %s, Instance IP: %s, Instance host: %s Key %s" % (i.id,i.ip_address,i.public_dns_name,i.key_name)
                if self.instanceIsDaysOld(i,days_old):
                    try:
                        i.terminate()
                    except AttributeError:
                        # Terminate will throw an attribute error when it tries to set the status of a terminated instance. 
                        pass
        
        self.getAllCloudInstances()
        
    #
    #
    #	Future functionality
    # 
    #

    def getCloudConfigFromEnvironment(self):
        print "This should be implemented, but isn't."
        
    def manageExistingCloudInstance(self,Cloud_node):
        pass
    
    def getCloudInstanceInformation(self,Cloud_node):
        # get the Cloud Information for the node
        pass
    
    def deleteCloudInstance(self,Cloud_node):
        pass
    
    def hardRebootCloudInstance(self,Cloud_node):
        pass
    
    def deleteAllCloudInstances(self):
        pass  
        
   ##
    # @fn createNewCloudNodes(number_nodes,image_name,machine_type,associateip=False,domain=None):
    #
    # Creates new cloud nodes and adds them to network_nodes list.
    #
    #
    #    @param self The object pointer  
    #    @param number_nodes  number of nodes to be created
    #    @param image_name  Name of Cloud image to launch
    #    @param flavor_name  Cloud "flavor" of virtual node
    #    @param associateip  Associate to external ip?
    #    @param domain Domain to associate with external ip
    #     @param cloud_subnet cloud subnet id for primary network interface.
    #
    #    @returns list of new nodes.


    
    def createNewCloudNodes(self,number_nodes,image_name,flavor_name,local_master,associateip=False,domain=None,cloud_subnet=None, instance_suffix=""):
        
        # This function creates number nodes on the cloud system. 
        # It returns a list of nodes
        
        new_instances = self.createNewCloudInstances(number_nodes,image_name,flavor_name,cloud_subnet,instance_suffix)
        # new instances should have a 60 second delay to make sure everything is running.

        ip_addresses = []
        new_ofs_test_nodes = []
        
        for idx,instance in enumerate(new_instances):
            instance = self.novaapi.servers.get(instance)
            print "Instance %s has status %s" % (instance.id,instance.status)
            
            while not instance.status == "ACTIVE":
                
                time.sleep(10)
                instance = self.novaapi.servers.get(instance)
                print "Instance %s has status %s" % (instance.id,instance.status)
            
        
        print "Waiting 120 seconds for networking"            
        time.sleep(180)
        # refresh all the instances in the list.
        new_instances = self.refreshCloudInstanceList(new_instances)

        # now that the instances are up, check the external ip
        if associateip == True:
            # if we need to associate an external ip address, do so
            ip_addresses = self.associateIPAddresses(new_instances,domain)
        else:
            
            for idx,instance in enumerate(new_instances):
                
                #pprint(instance.__dict__)
                msg = "Instance %s using current IP %s" % (instance.id,instance.addresses[self.nova_network_name][0]['addr'])
                print msg
                logging.info(msg)
                #(i.__dict__)
                ip_addresses.append(instance.addresses[self.nova_network_name][0]['addr'])
        

        print "===========================================================" 
        print "Adding new nodes to OFS cluster"
 
        for idx,instance in enumerate(new_instances):
            # Create the node and get the instance name
            if "ubuntu" in image_name:
                name = 'ubuntu'
            elif "fedora" in image_name:
                # fedora 18 = cloud-user, fedora 19 = fedora
                
                # fedora 18 = cloud-user, fedora 19 = fedora
                name = 'fedora'
            elif "centos7" in image_name:
                name = 'centos'
            elif "rhel7" in image_name:
                name = 'cloud-user'
            else:
                name = 'ec2-user'
            
            new_node = OFSTestRemoteNode.OFSTestRemoteNode(username=name,ip_address=instance.addresses[self.nova_network_name][0]['addr'],key=self.cloud_instance_key_location,local_node=local_master,is_cloud=True,ext_ip_address=ip_addresses[idx])

            new_ofs_test_nodes.append(new_node)

        # return the list of newly created nodes.
        logging.debug("New Node IP Addresses: ")
        logging.debug(ip_addresses)
        return new_ofs_test_nodes



#     
# def OFSCloudConnectionManager_test_driver():
#     
#     # old_mgr = OFSCloudConnectionManager(cloud_config_file="/home/jburton/Projects/Testing/PyTest/Cloud-cred/Cloudrc.sh",region_name="nova")
#     my_mgr = OFSCloudConnectionManager(cloud_config_file="/home/jburton/cuer1/Cloudrc.sh",region_name="RegionOne")
#     print "Connect to Cloud"
#     #old_mgr.connect(debug=1)
#     my_mgr.connect(debug=1)
#     
#     print "Testing connection"
#     #old_mgr.printAllInstanceStatus()
#     #my_mgr.printAllInstanceStatus()
#     #my_mgr.getAllCloudImages()
#     #my_mgr.deleteOldInstances(days_old=3)
#     #old_mgr.setCloudKey("BuildBot","/home/jburton/buildbot.pem")
#     my_mgr.setCloudKey("BuildBot2","/home/jburton/cuer1/buildbot2.pem")
# 
#     
#     
#     
#         
#     print "Creating Instances"
#     
#     node_list = my_mgr.createNewCloudInstances(number_nodes=1,image_system="cloud-rhel6",type="m1.small")
#         
#     #print my_mgr
#     for node in node_list:
#         my_mgr.terminateCloudInstance(node.ip_address)
# 
# 
# #OFSCloudConnectionManager_test_driver()
