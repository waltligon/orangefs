#!/usr/bin/python

class OFSTestConfig(object):
    
    def __init__(self):
        
        self.log_file = ""
        self.using_ec2 = False
        self.ec2rc_sh = ""
        self.ssh_key_filepath = ""
        self.ec2_key_name = ""
        self.number_new_ec2_nodes = 0
        self.ec2_image = ""
        self.ec2_machine = ""
        self.ec2_delete_after_test = False
        self.node_ip_addresses = []
        self.node_usernames = []
        self.ofs_resource_location = ""
        self.ofs_resource_type = ""
        self.configure_opts = ""
        self.pvfs2genconfig_opts = ""
        self.run_sysint_tests = False
        self.run_usrint_tests = False
        self.run_vfs_tests = False
        self.run_mpi_tests = False
        self.ofs_fs_name="pvfs2-fs"
        self.ofs_build_kmod = True
        self.ofs_mount_fuse = False
    
    def setConfig(self,kwargs={}):
        pass
    
    def printDict(self):
        print self.__dict__
    
    def setConfigFromDict(self,d={}):
        
        temp = d.get('log_file')
        if temp != None:
            self.log_file = temp
        
        temp = d.get('using_ec2')
        if temp != None:
            self.using_ec2 = temp
        
        temp = d.get('ec2rc_sh')
        if temp != None:
            self.ec2rc_sh = temp
        
        temp = d.get('ssh_key_filepath')
        if temp != None:
            self.ssh_key_filepath = temp

        temp = d.get('ec2_key_name')
        if temp != None:
            self.ec2_key_name = temp
        
        temp = d.get('number_new_ec2_nodes')
        if temp != None:
            self.number_new_ec2_nodes = temp
        # sanity check
        if self.number_new_ec2_nodes > 0:
	    self.using_ec2 = True

        temp = d.get('ec2_image')
        if temp != None:
            self.ec2_image = temp

        temp = d.get('ec2_machine')
        if temp != None:
            self.ec2_machine = temp
            
        temp = d.get('ec2_delete_after_test')
        if temp != None:
            self.ec2_delete_after_test = temp
        
        temp = d.get('node_ip_addresses')
        if temp != None:
            nodelist = temp.split(" ")
            print nodelist
            for node in nodelist:
                self.node_ip_addresses.append(node)
        
        temp = d.get('node_usernames')
        if temp != None:
            
            userlist = temp.split(" ")
            print userlist
            for user in userlist:
                self.node_usernames.append(user)
        
        temp = d.get('ofs_resource_location')
        if temp != None:
            self.ofs_resource_location = temp
            
        temp = d.get('ofs_resource_type')
        if temp != None:
            self.ofs_resource_type = temp

        temp = d.get('configure_opts')
        if temp != None:
            self.configure_opts = temp
        
        temp = d.get('pvfs2genconfig_opts')
        if temp != None:
            self.pvfs2genconfig_opts = temp
        
        temp = d.get('run_vfs_tests')
        if temp != None:
            self.run_vfs_tests = temp
        
        temp = d.get('run_sysint_tests')
        if temp != None:
            self.run_sysint_tests = temp
        
        temp = d.get('run_mpi_tests')
        if temp != None:
            self.run_mpi_tests = temp
        
        temp = d.get('run_usrint_tests')
        if temp != None:
            self.run_usrint_tests = temp
            
        temp = d.get('ofs_fs_name')
        if temp != None:
            self.ofs_fs_name = temp
        
        temp = d.get('ofs_build_kmod')
        if temp != None:
            self.ofs_build_kmod = temp
        
        temp = d.get('ofs_mount_fuse')
        if temp != None:
            self.ofs_mount_fuse = temp