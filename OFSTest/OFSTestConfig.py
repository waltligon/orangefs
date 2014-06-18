#!/usr/bin/python

##
#
# @class OFSTestConfig
# 
# @brief This class holds the configuration for OrangeFS
#
# These variables in this class control the configuration of OFSTest. OFSTest can 
# setup an instance of OrangeFS on any real or virtual cluster with 
# passwordless ssh access for one user and passwordless sudo access for
# that user. 
#

import pprint
import logging


class OFSTestConfig(object):
    
    
    
    def __init__(self):
        
        ## @var log_file
        # name of output logfile
        # Web Interface: auto
        self.log_file = "OFSTest.log"
        
        #------------------------------
        #
        # node control/cloud variables
        #
        #------------------------------
       
        ## @var using_cloud
        # Are we using OpenStack/Cloud?
        
        self.using_cloud = False  # Web Interface: auto
        
        ## @var cloud_config
        # Location of the ec2rc.sh/openstack.sh file
        self.cloud_config = ""  # Web Interface: auto
        
        ## @var ssh_key_filepath
        # Path of ssh key used to access all nodes
        self.ssh_key_filepath = "" # Web Interface: auto
        
        ## @var ssh_key_filepaths 
        # List of differing keypaths, if applicable
        self.ssh_key_filepaths = []  # Web Interface: not used
        
        ## @var cloud_key_name
        # Internal cloud key name. Must be consistant accross nodes.
        self.cloud_key_name = "" # Web Interface: auto
        
        ## @var number_new_cloud_nodes
        # Number of new cloud nodes to be created. 
        # If == 0, then using existing nodes.
        self.number_new_cloud_nodes = 0 # Web Interface: user
        
        ## @var cloud_image
        # Image name to be launched for cloud instance.
        # Must be consistant across nodes.
        self.cloud_image = "" # Web Interface: user
        
        ## @var cloud_machine
        # cloud machine type (e.g. m1.medium)
        # Must be consistant across nodes.
        self.cloud_machine = "" # Web Interface: user
        
        ## @var cloud_delete_after_test
        # Should the nodes be deleted after testing?
        self.cloud_delete_after_test = False # Web Interface: user
        
        ## @var cloud_domain
        # Cloud domain
        self.cloud_domain=None # Web Interface: auto
        
        ## @var cloud_associate_ip
        # Associate external ip address with Cloud nodes?
        self.cloud_associate_ip=False # Web Interface: auto
        
        ## @var node_ip_addresses
        # List of node ip addresses. If a private network is used, this is the
        # Internal network.
        self.node_ip_addresses = [] # Web Interface: not used
         
        ## @var node_ext_ip_addresses
        # List of addresses accessible from the local machine.
        # if node_ip_addresses is accessible, this need not be set.
        self.node_ext_ip_addresses = [] # Web Interface: not used
        
        ## @var node_username
        # Single username to access all nodes
        self.node_username = "cloud-user" # Web Interface: not used
        
        ## @var node_usernames
        # usernames for login of individual nodes, if necessary
        self.node_usernames = [] # Web Interface: not used
        
        ## @var ofs_fs_name
        # Name of OrangeFS filesystem service in URL. No longer needed
        self.ofs_fs_name=None # Web Interface: not used
        
        #------------------------------
        #
        # OrangeFS configuration
        #
        #------------------------------
        
        ## @var ofs_resource_location
        # location of OrangeFS source
        self.ofs_resource_location = "" # Web Interface: user
        
        ## @var ofs_resource_type
        # What package is the OrangeFS source? SVN, TAR, Local Dir, Node? 
        self.ofs_resource_type = "" # Web Interface: auto
        
        ## @var pvfs2genconfig_opts
        # Additional options for pvfs2genconfig to generate Orangefs.conf file
        self.pvfs2genconfig_opts = "" # Web Interface: user
        
        #------------------------------
        #
        # build options
        #
        #------------------------------
        
        ## @var install_fuse
        # Build Fuse module
        self.install_fuse=False  # Web Interface: user
            
        ## @var install_prefix
        # Where to install OrangeFS.
        self.install_prefix = "/opt/orangefs" # Web Interface: auto
        
        ## @var db4_prefix
        # Location of DB4
        self.db4_prefix = "/opt/db4" # Web Interface: auto

        ## @var install_OFS_client
        # Install the OrangeFS client and add --with-kernel option
        self.install_OFS_client = True # Web Interface: auto

        ## @var install_shared
        # --enable-shared flag
        self.install_shared = False # Web Interface: auto
        
        ## @var enable_strict
        # --enable-strict flag
        self.enable_strict = True # Web Interface: user
        
        ## @var ofs_security_mode
        # Security Mode. Default is None. Options are "Key" and "Cert"
        self.ofs_security_mode = None # Web Interface: user
        
        ## @var ofs_build_kmod
        # Build the kernel module?
        self.ofs_build_kmod = True # Web Interface: auto
        
        ## @var ofs_compile_debug
        # Compile with -g debugging option?
        self.ofs_compile_debug = True # Web Interface: user

        ## @var ofs_patch_files
        # List of patches to patch OrangeFS source
        self.ofs_patch_files=[] # Web Interface: user
        
        ## @var svn_username
        # Username of svn user. Allows checkout instead of export
        self.svn_username = None # Web Interface: not used
        
        ## @var svn_password
        # Password of svn user. Allows checkout instead of export
        self.svn_password = None # Web Interface: not used

        
        ## @var svn_options
        # Additional options for SVN
        self.svn_options = None # Web Interface: not used

        ## @var install_hadoop
        # Enable OrangeFS hadoop support
        self.install_hadoop = False # Web Interface: auto

        ## @var configure_opts
        # Additional options for configure
        self.configure_opts = "" # Web Interface: user

        #------------------------------
        #
        # installation options
        #
        #------------------------------
        
        ## @var mount_OFS_after_setup
        # Mount the filesystem after setup
        self.mount_OFS_after_setup = True # Web Interface: auto
        
        ## @var ofs_tcp_port
        # TCP port on which to run OrangeFS
        self.ofs_tcp_port = "3396" # Web Interface: auto
        
        ## @var ofs_mount_as_fuse
        # Mount filesystem using the fuse module instead of kernel module.
        self.ofs_mount_as_fuse = False # Web Interface: user
        
        ## @var install_tests
        # Install OrangeFS tests
        self.install_tests = True # Web Interface: auto
        
        ## @var install_OFS_server
        # Install and start the OrangeFS server software
        self.install_OFS_server = True # Web Interface: auto
        
        ## @var install_MPI
        # Install MPI software, including Torque
        self.install_MPI = False # Web Interface: user
        
        ## @var install_opts
        # Additional installation options
        self.install_opts = "" # Web Interface: user
        
        
        #------------------------------
        #
        # existing installation options
        #
        #------------------------------
        
        ## @var ofs_extra_tests_location
        # location of suite of testing benchmarks
        self.ofs_extra_tests_location = None # Web Interface: not used
        
        ## @var ofs_pvfs2tab_file
        # location of PVFS2TAB_FILE. Mountpoint will be read from file
        self.ofs_pvfs2tab_file = None # Web Interface: not used
        
        ## @var ofs_source_location
        # location of orangefs on the main node
        self.ofs_source_location = None # Web Interface: not used
        
        ## @var ofs_config_file
        #Location of OrangeFS.conf file
        self.ofs_config_file = None  # Web Interface: not used
        
        ## @var delete_existing_data
        # Delete data on OrangeFS partition
        self.delete_existing_data = False # Web Interface: not used
        
        ## @var ofs_mount_point
        # OrangeFS mount point
        self.ofs_mount_point = None # Web Interface: not used
        
        ## @var ofs_hostname_override
        # Override the hostname given by hostname command. Will force the
        # hostname to be the value provided. Needed to workaround
        # a bug on some cloud setups.
        self.ofs_hostname_override = [] # Web Interface: not used
        
        ## @var start_client_on_all_nodes
        # Start the OrangeFS client on all nodes after installation?
        self.start_client_on_all_nodes = False # Web Interface: not used
        
        
        #------------------------------
        #
        # Test Flags
        #
        #------------------------------
        
        ## @var run_sysint_tests
        # Run the system integration tests?
        self.run_sysint_tests = False # Web Interface: user
        
        ## @var run_usrint_tests
        # Run the user integration library tests?
        self.run_usrint_tests = False # Web Interface: user
        
        ## @var run_vfs_tests
        # Run the kernel module vfs tests?
        self.run_vfs_tests = False # Web Interface: user
        
        ## @var run_fuse_tests
        # Run the fuse vfs tests?
        self.run_fuse_tests = False # Web Interface: user
        
        ## @var run_mpi_tests
        # Run OrangeFS ROM-IO tests.
        self.run_mpi_tests = False # Web Interface: user
        
        ## @var run_hadoop_tests
        # Run OrangeFS Hadoop tests
        self.run_hadoop_tests = False # Web Interface: user
        
        ## @var cloud_subnet
        # cloud subnet ID for primary network interface
        #self.cloud_subnet=None
        #TODO: Remove hardcoded definition. 
        self.cloud_subnet="03de6c88-231c-4c2c-9bfd-3c2d17604a82"
        
        self.cloud_type = 'EC2' # Web Interface: auto
        self.nova_password_file=None # Web Interface: auto
        
        ## @var instance_suffix
        #
        # Suffix to add to instance name.
        # usually the same as the output directory.
        self.instance_suffix = "" # Web Interface: auto
        
    
    ##
    #
    # @fn setConfig(self,kwargs={}):
    #
    # A universal method for setting the initial configuration. Overridden
    # in subclasses
    #
    # @param self The object pointer
    # @param kwargs Argument list with keywords.
    
    

    
    def setConfig(self,kwargs={}):
        pass
        
    ##
    #
    # @fn addConfig(self,str_args=[]):
    #
    #
    # This method can "override" existing values as specified in str_args. 
    # This was implemented to add support for command line override of config files.
    #
    # @param self The object pointer
    # @param str_args An array of variable=value strings.
    

    
    def addConfig(self,str_args=[]):
        
        # this method taks an array of strings and converts them into a dictionary
        d = {}

        
        for line in str_args:
            #print line
            # ignore comments
            if line.lstrip() == "" or (line.lstrip())[0] == '#':
                continue
            else:
                (key,delim, val) = line.rstrip().partition("=")
                
                # translate the boolean values
                if val.lower() == "true":
                    d[key] = True
                elif val.lower() == "false":
                    d[key] = False
                else:
                    try:
                        # do we have an int? 
                        d[key] = int(val)
                    except ValueError:
                        # guess not. Must be a string.
                        d[key] = val

        
        self.setConfigFromDict(d)
        
    ##
    #
    # @fn printDict(self):
    #
    # This prints all the setup variables. Used for debugging.
    # @param self The object pointer
    # 

    
    def printDict(self):
        logging.debug(pprint.pformat(self.__dict__))


    ##
    #
    # @fn setConfigFromDict(self,d={}):
    #
    # This method takes a formatted dictionary d and converts the values
    # into the variables of this class. Every new variable added above will
    # need an entry here.
    #
    # @param self The object pointer
    # @param d Dictionary with varible names and values.
    
    
    
    
    def setConfigFromDict(self,d={}):
        
        temp = d.get('log_file')
        if temp != None:
            self.log_file = temp
        
        temp = d.get('using_cloud')
        if temp != None:
            self.using_cloud = temp
        
        temp = d.get('cloud_config')
        if temp != None:
            self.cloud_config = temp
        
        temp = d.get('ssh_key_filepath')
        if temp != None:
            self.ssh_key_filepath = temp

        temp = d.get('cloud_key_name')
        if temp != None:
            self.cloud_key_name = temp
        
        temp = d.get('number_new_cloud_nodes')
        if temp != None:
            self.number_new_cloud_nodes = temp
        # sanity check
        if self.number_new_cloud_nodes > 0:
            self.using_cloud = True

        temp = d.get('cloud_image')
        if temp != None:
            self.cloud_image = temp

        temp = d.get('cloud_machine')
        if temp != None:
            self.cloud_machine = temp
            
        temp = d.get('cloud_delete_after_test')
        if temp != None:
            self.cloud_delete_after_test = temp
        
        temp = d.get('node_ip_addresses')
        if temp != None:
            nodelist = temp.split(" ")
            #print nodelist
            for node in nodelist:
                self.node_ip_addresses.append(node)

        temp = d.get('node_ext_ip_addresses')
        if temp != None:
            nodelist = temp.split(" ")
            #print nodelist
            for node in nodelist:
                self.node_ext_ip_addresses.append(node)

        
        temp = d.get('node_usernames')
        if temp != None:
            
            userlist = temp.split(" ")
            #print userlist
            for user in userlist:
                self.node_usernames.append(user)
        
        temp = d.get('ofs_hostname_override')
        if temp == None:
            temp = d.get('ofs_host_name_override')
        
        if temp != None:
            userlist = temp.split(" ")
            #print userlist
            for user in userlist:
                self.ofs_hostname_override.append(user)
        
        # one username for all nodes
        temp = d.get('node_username')
        if temp != None:
            for node in nodelist:
                self.node_usernames.append(temp)
        
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
        
        temp = d.get('run_hadoop_tests')
        if temp != None:
            self.run_hadoop_tests = temp
            
        temp = d.get('ofs_fs_name')
        if temp != None:
            self.ofs_fs_name = temp
        
        temp = d.get('ofs_build_kmod')
        if temp != None:
            self.ofs_build_kmod = temp
        
        # depricated. for backward compatibility
        temp = d.get('ofs_mount_fuse')
        if temp != None:
            self.run_fuse_tests = temp
        
        temp = d.get('run_fuse_tests')
        if temp != None:
            self.run_fuse_tests = temp

        temp = d.get('cloud_domain')
        if temp != None:
            self.cloud_domain = temp
        temp = d.get('cloud_associate_ip')
        if temp != None:
            self.cloud_associate_ip = temp

        temp = d.get('ofs_mount_as_fuse')
        if temp != None:
            self.run_fuse_tests = temp
            
        temp = d.get('mount_OFS_after_setup')
        if temp != None:
            self.mount_OFS_after_setup = temp

        temp = d.get('ofs_mount_as_fuse')
        if temp != None:
            self.ofs_mount_as_fuse = temp
            

        temp = d.get('install_tests')
        if temp != None:
            self.install_tests = temp


        temp = d.get('install_OFS_server')
        if temp != None:
            self.install_OFS_server = temp


        temp = d.get('install_OFS_client')
        if temp != None:
            self.install_OFS_client = temp


        temp = d.get('install_MPI')
        if temp != None:
            self.install_MPI = temp
        
        temp = d.get('install_hadoop')
        if temp != None:
            self.install_hadoop = temp
            
                # --enable-fuse
        temp = d.get('install_fuse')
        if temp != None:
            self.install_fuse=temp
    
        # --prefix=
        temp = d.get('install_prefix')
        if temp != None:
            self.install_prefix = temp
        
        # --with-db=
        # disabled 
        temp = d.get('db4_prefix')
        if temp != None:
            self.db4_prefix = temp

        # add --with-kernel option
        temp = d.get('install_ofs_client')
        if temp != None:
            self.install_OFS_client = temp
    

        # add --enable=shared
        temp = d.get('install_shared')
        if temp != None:
            self.install_shared = temp
        
        # --enable-strict
        temp = d.get('enable_strict')
        if temp != None:
            self.enable_strict = temp
        
        temp = d.get('install_opts')
        if temp != None:
            self.install_opts = temp
        
        temp = d.get('ofs_security_mode')
        if temp != None:
            self.ofs_security_mode = temp
        
        temp = d.get('ofs_patch_files')
        if temp != None:
            patchlist = temp.split(" ")
            #print patchlist
            for patch in patchlist:
                self.ofs_patch_files.append(patch)
        
        temp = d.get('ofs_extra_tests_location')
        if temp != None:
            self.ofs_extra_tests_location = temp
        
        temp = d.get('ofs_pvfs2tab_file')
        if temp != None:
            self.ofs_pvfs2tab_file = temp

        temp = d.get('ofs_source_location')
        if temp != None:
            self.ofs_source_location = temp
        
        temp = d.get('ofs_config_file')
        if temp != None:
            self.ofs_config_file = temp
        
        temp = d.get('ofs_mount_point')
        if temp != None:
            self.ofs_mount_point = temp
        
        temp = d.get('start_client_on_all_nodes')
        if temp != None:
            self.start_client_on_all_nodes = temp
        
        temp = d.get('cloud_subnet')
        if temp != None:
            self.cloud_subnet = temp

        temp = d.get('svn_password')
        if temp != None:
            self.svn_password = temp

        temp = d.get('svn_username')
        if temp != None:
            self.svn_username = temp

        temp = d.get('svn_options')
        if temp != None:
            self.svn_options = temp
        
        temp = d.get('cloud_type')
        if temp != None:
            self.cloud_type = temp
        
        temp = d.get('nova_password_file')
        if temp != None:
            self.nova_password_file = temp
        
        temp = d.get('ldap_server_uri')
        if temp != None:
            self.ldap_server_uri = temp
        
        temp = d.get('ldap_admin')
        if temp != None:
            self.ldap_admin = temp
        
        temp = d.get('ldap_admin_password')
        if temp != None:
            self.ldap_admin_password = temp

        temp = d.get('ldap_container')
        if temp != None:
            self.ldap_container = temp
            
        temp = d.get('ofs_tcp_port')
        if temp != None:
            self.ofs_tcp_port = temp
                
