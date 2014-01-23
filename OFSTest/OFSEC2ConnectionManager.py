#!/usr/bin/python
# 
# OFSEC2ConnectionManager.py
#
# This class manages the EC2 connection. It has no awareness of OFSTestNodes
# or the OFSTestNetwork.
#
#
# Members:
#
#        ec2_instance_names = {}
#        ec2_instance_list = {}
#        ec2_access_key = EC2 Access key. In ec2rc.sh file.
#        ec2_endpoint = Endpoint is the hostname. e.g. devstack.clemson.edu
#        ec2_path = Path to EC2 on the host (URL = http://host:port/path)
#        ec2_port = Port is TCP port.
#        ec2_secret_key = EC2 Secret Key. In ec2rc.sh file.
#        ec2_region = EC2 region. Received after initial connect.
#        ec2_connection = The ec2.connection.EC2Connection object.
#        ec2_image_list = List of all available images
#        ec2_key_list = List of all available keys (not used)
#        ec2_region_name = EC2 region name. Required to connect.
#		 ec2_is_secure - Is this http or https?       
#		 instance_key - Name of key (in EC2) used to access instance via SSH
#		 instance_key_location - *.pem ssh key used to access instance.
#
#
# Methods:
#
# 	__init__():				Initialization.
#
#	readEC2ConfigFile():	Reads relevant values from ec2rc.sh file. Called by 
#							__init__()
#
#	connect():				Connects to EC2
#
#	setEC2Key():			Sets key name (EC2) and key location (file) used to
#							create and access EC2 instances.
#
#	getAllImages():			Returns a list of available EC2 Images	
#
#	terminateEC2Instance():	Terminates a running EC2 instance
#
#	createNewEC2Instances(): Creates new EC2 instances and returns list of them.
#
#	associateIPAddress():	Creates an new external IP address and associates 
#							with the instance.
#
#	checkEC2Connection():	Checks to see if the EC2 connection is available. 
#							Connects if it isn't.
#
#	getAllEC2Instances():	Gets all instances from EC2 connection. Returns a
#							list of instances.
#
#	printAllInstanceStatus():	Prints the status of all instances. For 
#								debugging.
#
#	instanceIsDaysOld():	Tests whether an instance is so many days old.
#
#	deleteOldInstances():	Deletes instances over a certain number of days old.
#				
#------------------------------------------------------------------------------


import os
import time
import re
from boto import ec2
from pprint import pprint
from datetime import datetime, timedelta



class OFSEC2ConnectionManager(object):
#------------------------------------------------------------------------------
#
# __init__(self,ec2_config_file=None,region_name=None):
#
# Arguments:
#
#	ec2_config_file = Path to ec2rc.sh file.
#	region_name = Name of ec2 region to connect to.
#------------------------------------------------------------------------------

    
    def __init__(self,ec2_config_file=None,region_name=None):
        
        self.ec2_instance_names = {}
        self.ec2_instance_list = {}
        self.ec2_access_key = ""
        self.ec2_endpoint = ""
        self.ec2_path = ""
        self.ec2_port = ""
        self.ec2_secret_key = ""
        self.ec2_region = None
        self.ec2_connection = None
        self.ec2_image_list = None
        self.ec2_key_list = None
        self.ec2_is_secure = False
        self.instance_key = None
        self.instance_key_location = None
        
        # Default region name is RegionOne
        if region_name == None:
        	self.ec2_region_name = "RegionOne"
        else:
	        self.ec2_region_name = region_name
        
       
        if ec2_config_file is not None:
	        # Read the ec2rc.sh file if provided
            self.readEC2ConfigFile(ec2_config_file)
        else:
        	# Otherwise, get the configuration from the environment.
            self.getEC2ConfigFromEnvironment()
        
#------------------------------------------------------------------------------
#
#	readEC2ConfigFile(self,filename):
#
#		Reads relevant values from ec2rc.sh file.
#
#	Arguments:
#
#				filename = Path to ec2rc file
#------------------------------------------------------------------------------
        
    
    def readEC2ConfigFile(self,filename):
        
        #open ec2 file
        ec2conf_rc = open(filename,'r')
        
        for line in ec2conf_rc:
            if "export EC2_ACCESS_KEY" in line:
                # check for EC2_ACCESS_KEY
                (export,variable,self.ec2_access_key) = re.split(' |=',line.rstrip())    
                #print line_v
                #print "%s,%s,%s" %    (export,variable,self.ec2_access_key)
            elif "export EC2_SECRET_KEY" in line:
                # check for EC2_SECRET_KEY
                (export,variable,self.ec2_secret_key) = re.split(" |=",line.rstrip())    
                #print "%s,%s,%s" %    (export,variable,self.ec2_secret_key)
                
            elif "export EC2_URL" in line:
                # check for EC2_URL
                
                url_v = re.split(" |=|://|:|/",line.rstrip())
                
                #url_v contains (export,variable,secure,self.ec2_endpoint,port_string,path...)
                
                # is_secure? http = false, https = true
                if url_v[2] == "https":
                    self.ec2_is_secure = True
                else:
                    self.ec2_is_secure = False
                    
                # endpint is hostname
                self.ec2_endpoint = url_v[3]
                
                # then comes the port, convert to integer
                self.ec2_port = int(url_v[4])
                
                # finally, the path is all elements from 5 to the end
                path_v = url_v[5:]
                
                self.ec2_path = '/'.join(path_v)
                
#------------------------------------------------------------------------------
#      
#	connect(debug):			Gets region info and connects to EC2. 
#							ec2.connection.EC2Connection object
#							should be stored in self.ec2_connection.
#
#	Arguments:
#
#	debug = Debug level.
#------------------------------------------------------------------------------

    def connect(self,debug=0):
        
        print "Connecting to EC2/OpenStack region=%s endpoint=%s" % (self.ec2_region_name,self.ec2_endpoint)
        self.ec2_region = ec2.regioninfo.RegionInfo(name=self.ec2_region_name,endpoint=self.ec2_endpoint)

        
        if debug > 0:
	        print "EC2 region is %r" % self.ec2_region
        	print "ec2.connection.EC2Connection(aws_access_key_id=%s,aws_secret_access_key=%s,is_secure=self.ec2_is_secure,port=%d,debug=2,region=%s,path=%s)" % (self.ec2_access_key,self.ec2_secret_key,self.ec2_port,self.ec2_region,self.ec2_path)
        
        self.ec2_connection = ec2.connection.EC2Connection(aws_access_key_id=self.ec2_access_key,aws_secret_access_key=self.ec2_secret_key,is_secure=self.ec2_is_secure,port=self.ec2_port
        ,debug=debug,region=self.ec2_region,path=self.ec2_path)
    
    	if debug > 0:
	        print "EC2 connection is %r" % self.ec2_connection

#------------------------------------------------------------------------------
#      
#	setEC2Key(keyname,keylocation)
#
#				Sets key name (EC2) and key location (file) used to create and
#				access EC2 instances.
#
#------------------------------------------------------------------------------

    def setEC2Key(self,keyname,keylocation):
        self.instance_key = keyname
        self.instance_key_location = keylocation

#------------------------------------------------------------------------------
#	getAllImages():			Returns a list of available EC2 Images	
#------------------------------------------------------------------------------

    def getAllImages(self):
        self.checkEC2Connection()        
        self.ec2_image_list = self.ec2_connection.get_all_images()
        
        return self.ec2_image_list

#------------------------------------------------------------------------------
#
#	terminateEC2Instance(ip_address):	Terminates a running EC2 instance 
#
#	Arguments:
#		ip_address: IP address (internal) of the node.
#
#	Return:
#		1:	Instance not found for that ip address
#		0:	Instance terminated.
#
#------------------------------------------------------------------------------
        
    def terminateEC2Instance(self,ip_address):
        
        self.checkEC2Connection()
        
        node_instance = next(( i for i in self.ec2_instance_list if i.ip_address == ip_address),None)
        
        if node_instance == None:
            self.getAllEC2Instances()
            node_instance = next(( i for i in self.ec2_instance_list if i.ip_address == ip_address),None)
        if node_instance == None:
            print "Instance at %s not found." % ip_address
            return 1
            
        try: 
        	print "Releasing external IP address %s" % node_instance.ext_ip_address
        	self.ec2_connection.release_address(node_instance.ext_ip_address)
        except:
        	print "Warning: Could not release external IP Address "+ node_instance.ext_ip_address
        	
        print "Terminating node at %s" % ip_address
        
        try:
            node_instance.terminate()
        except AttributeError:
            # Terminate will throw an AttributeError when it tries to set the status of a terminated instance. Ignore it.
            pass
        return 0
#------------------------------------------------------------------------------
#
#	createNewEC2Instances(number_nodes,image_system,type): 
#							Creates new EC2 instances and returns list of them.
#
#	Arguments:
#		number_nodes 	= Number of nodes to create
#		image_system	= Image to run. (e.g. "cloud-ubuntu-12.04")
#		type			= Image "flavor" (e.g. "m1.medium")
#
#	Return:
#		A list of new instances.
#		
#------------------------------------------------------------------------------        
    def createNewEC2Instances(self,number_nodes,image_system,type):
        self.checkEC2Connection()  
        
        # This creates a new instance for the system of a given machine type
        
        # get the image ID for the operating system
        if self.ec2_image_list == None:
            self.getAllImages()
        
        # now let's find the os name in the image list
        image = next((i for i in self.ec2_image_list if i.name == image_system), None)
        
        if image == None:
            print "Image %s Not Found!" % image_system
            return None
        
        print "Creating %d new %s %s instances." % (number_nodes,type,image_system)
        orangefs_subnet="03de6c88-231c-4c2c-9bfd-3c2d17604a82"
        reservation = image.run(min_count=number_nodes, max_count=number_nodes, key_name=self.instance_key, security_groups=None, user_data=None, addressing_type=None, instance_type=type,subnet_id=orangefs_subnet) 
        
        print "Waiting 60 seconds for all instances to start."
        time.sleep(60)
        
        count = 0
        while len(reservation.instances) < number_nodes and count < 6:
            print "Waiting on instances"
            time.sleep(10)
            count = count + 1
            #pprint(reservation.__dict__)
            
        new_instances = [i for i in reservation.instances]
        
        for i in new_instances:
            print "Created new EC2 instance %s " % i.id
        
        return new_instances

#------------------------------------------------------------------------------      
#	associateIPAddress():	Creates an new external IP address and associates 
#							with the instances in the array.
#------------------------------------------------------------------------------

    def associateIPAddresses(self,instances=[],domain=None):
        external_addresses = []
        try:
			all_addresses = self.ec2_connection.get_all_addresses()
			print all_addresses
		except:
			pass
        for i in instances:
            #print i.__dict__

            address = self.ec2_connection.allocate_address(domain)
            print "Associating ext IP %s to %s with int IP %s" % (address.public_ip,i.id,i.ip_address)
            self.ec2_connection.associate_address(instance_id=i.id,public_ip=address.public_ip)
            external_addresses.append(address.public_ip)
        
            
            
        print "Waiting 30 seconds for external networking"
        time.sleep(30)
        return external_addresses
        
#------------------------------------------------------------------------------
#
#	checkEC2Connection():	Checks to see if the EC2 connection is available. 
#							Connects if it isn't.
# 
#------------------------------------------------------------------------------

    def checkEC2Connection(self):
        if self.ec2_connection == None:
            self.connect()

#------------------------------------------------------------------------------
#
#	getAllEC2Instances():	Gets all instances from EC2 connection. Returns a
#							list of instances.
#
#------------------------------------------------------------------------------
    
    def getAllEC2Instances(self):
        self.checkEC2Connection()
        
        reservation_v = self.ec2_connection.get_all_instances()
        
        self.ec2_instance_list = [i for r in reservation_v for i in r.instances]

#------------------------------------------------------------------------------
#
#	printAllInstanceStatus():	Prints the status of all instances. For 
#								debugging.
#				
#------------------------------------------------------------------------------
    
    def printAllInstanceStatus(self):
        self.getAllEC2Instances()
        for instance in self.ec2_instance_list:
            print "Instance %s at %s has status %s" % (instance.id,instance.ip_address,instance.status)

#------------------------------------------------------------------------------
#
#	instanceIsDaysOld():	Tests whether an instance is so many days old.
#				
#------------------------------------------------------------------------------
        
    def instanceIsDaysOld(self,instance,days_old=7):
        today = datetime.today()
        launch = datetime.strptime(instance.launch_time,"%Y-%m-%dT%H:%M:%S.000Z")
        # 2012-12-22T00:05:30.000Z
        week = timedelta(days=days_old)
              
        print today
        print launch
        
        if today - launch > week:
            print "Expired"
            return True
        else:
            return False
#------------------------------------------------------------------------------
# deleteOldInstances(days_old,name_filter,key_filter)
#
#	delete instances over a certain number of days old.
#------------------------------------------------------------------------------
    def deleteOldInstances(self,days_old=7,name_filter="",key_filter=""):    
        
        self.checkEC2Connection()
        
        for i in self.ec2_instance_list:
            #pprint(i.__dict__)
            
            if name_filter in i.public_dns_name and i.key_name.lower() == key_filter.lower():
                print "Instance name %s, Instance IP: %s, Instance host: %s Key %s" % (i.id,i.ip_address,i.public_dns_name,i.key_name)
                if self.instanceIsDaysOld(i,days_old):
                    try:
                        i.terminate()
                    except AttributeError:
                        # Terminate will throw an attribute error when it tries to set the status of a terminated instance. 
                        pass
        
        self.getAllEC2Instances()
        

        
 #------------------------------------------------------------------------------
 #
 #	Future functionality
 #
 #------------------------------------------------------------------------------

    def getEC2ConfigFromEnvironment(self):
    	print "This should be implemented, but isn't."
    	
    def manageExistingEC2Instance(self,ec2_node):
        pass
    
    def getEC2InstanceInformation(self,ec2_node):
        # get the EC2 Information for the node
        pass
    
    def deleteEC2Instance(self,ec2_node):
        pass
    
    def hardRebootEC2Instance(self,ec2_node):
        pass
    
    def deleteAllEC2Instances(self):
        pass  
        
#------------------------------------------------------------------------------
#
#	Test driver - Not currently in use.
#
#------------------------------------------------------------------------------
	 
    
def OFSEC2ConnectionManager_test_driver():
    
   # old_mgr = OFSEC2ConnectionManager(ec2_config_file="/home/jburton/Projects/Testing/PyTest/ec2-cred/ec2rc.sh",region_name="nova")
    my_mgr = OFSEC2ConnectionManager(ec2_config_file="/home/jburton/cuer1/ec2rc.sh",region_name="RegionOne")
    print "Connect to EC2"
    #old_mgr.connect(debug=1)
    my_mgr.connect(debug=1)
    
    print "Testing connection"
    #old_mgr.printAllInstanceStatus()
    #my_mgr.printAllInstanceStatus()
    #my_mgr.getAllImages()
    #my_mgr.deleteOldInstances(days_old=3)
    #old_mgr.setEC2Key("BuildBot","/home/jburton/buildbot.pem")
    my_mgr.setEC2Key("BuildBot2","/home/jburton/cuer1/buildbot2.pem")

    
    
    
        
    print "Creating Instances"
    
    node_list = my_mgr.createNewEC2Instances(number_nodes=1,image_system="cloud-rhel6",type="m1.small")
        
    #print my_mgr
    for node in node_list:
        my_mgr.terminateEC2Instance(node.ip_address)


#OFSEC2ConnectionManager_test_driver()
