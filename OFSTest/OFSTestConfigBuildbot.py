#!/usr/bin/python

from OFSTestConfig import *

class OFSTestConfigBuildbot(OFSTestConfig):
    
    # This class provides the Buildbot interface to OFSTestConfig
    def __init__(self):
        super(OFSTestConfigBuildbot,self).__init__()

    def setConfig(self,kwargs={}):

        self.setConfigFromDict(kwargs)
'''    
    
        temp = None
        
        temp = kwargs.get('log_file')
        if temp != None:
            self.log_file = temp
        
        temp = kwargs.get('using_ec2')
        if temp != None:
            self.using_ec2 = temp
        
        temp = kwargs.get('ec2rc_sh')
        if temp != None:
            self.ec2rc_sh = temp
        
        temp = kwargs.get('ssh_key_filepath')
        if temp != None:
            self.ssh_key_filepath = temp

        temp = kwargs.get('ec2_key_name')
        if temp != None:
            self.ec2_key_name = temp
        
        temp = kwargs.get('number_new_ec2_nodes')
        if temp != None:
            self.number_new_ec2_nodes = temp

        temp = kwargs.get('ec2_image')
        if temp != None:
            self.ec2_image = temp

        temp = kwargs.get('ec2_machine')
        if temp != None:
            self.ec2_machine = temp
            
        temp = kwargs.get('ec2_delete_after_test')
        if temp != None:
            self.ec2_delete_after_test = temp
        
        temp = kwargs.get('node_ip_addresses')
        if temp != None:
            self.ec2_node_ip_addresses = temp
        
        temp = kwargs.get('node_usernames')
        if temp != None:
            self.ec2_node_usernames = temp
        
        temp = kwargs.get('ofs_resource_location')
        if temp != None:
            self.ofs_resource_location = temp
            
        temp = kwargs.get('ofs_resource_type')
        if temp != None:
            self.ofs_resource_type = temp

        temp = kwargs.get('configure_opts')
        if temp != None:
            self.configure_opts = temp
        
        temp = kwargs.get('pvfs2genconfig_opts')
        if temp != None:
            self.pvfs2genconfig_opts = temp
        
        
'''