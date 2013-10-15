#!/usr/bin/python

###############################################################################
#
# OFSTestConfig
# 
# This class holds the configuration for OrangeFS
#
################################################################################



###############################################################################################'
#
# class OFSTestNode(object)
#
# OrangeFS is a complex file system with a wide variety of functionality that
# can run on many different systems in many different configurations. This
# class holds the variables that control OrangeFS setup and testing
# 
################################################################################################

class OFSTestConfig(object):
    
    #------------------------------------------------------------------
    #
    # setup variables
    #
    #
    # These variables control the configuration of OFSTest. OFSTest can 
    # setup an instance of OrangeFS on any real or virtual cluster with 
    # passwordless ssh access for one user and passwordless sudo access for
    # that user. 
    #
    #------------------------------------------------------------------
    
    
    def __init__(self):
        
    
        # name of output logfile
        self.log_file = "OFSTest.log"
        
        #------------------------------
        #
        # node control/ec2 variables
        #
        #------------------------------
       
        # are we using OpenStack/EC2
        self.using_ec2 = False
        
        # Location of the ec2rc.sh file
        self.ec2rc_sh = ""
        
        # path of ssh key used to access all nodes
        self.ssh_key_filepath = ""
        
        # list of differing keypaths if applicable
        # TODO: Add multikey functionality.
        self.ssh_key_filepaths = []
        
        # Internal ec2 key name. Must be consistant accross nodes.
        self.ec2_key_name = ""
        
        # Number of new ec2 nodes to be created. 
        # If == 0, then using existing nodes.
        self.number_new_ec2_nodes = 0
        
        # Image name to be launched for ec2 instance.
        # Must be consistant across nodes.
        self.ec2_image = ""
        
        # ec2 machine type (e.g. m1.medium)
        # Must be consistant across nodes.
        self.ec2_machine = ""
        
        # Should the nodes be deleted after testing?
        self.ec2_delete_after_test = False
        
        # EC2 domain
        self.ec2_domain=None
        
        # Associate external ip address with EC2 nodes.
        self.ec2_associate_ip=False
        
        # list of node ip addresses. If a private network is used, this is the
        # Internal network.
        self.node_ip_addresses = []
        
        # list of addresses accessible from the local machine.
        # if node_ip_addresses is accessible, this need not be set.
        self.node_ext_ip_addresses = []
        
        # single username to access all nodes
        self.node_username = "ec2-user"
        
        # usernames for login of individual nodes, if necessary
        self.node_usernames = []
        
        # no longer needed
        self.ofs_fs_name=None
        
        #------------------------------
        #
        # OrangeFS configuration
        #
        #------------------------------
        
        # location of OrangeFS source
        self.ofs_resource_location = ""
        
        # What package is the OrangeFS source? SVN, TAR, Local Dir, Node? 
        self.ofs_resource_type = ""
        
        
        # Additional options for pvfs2genconfig to generate Orangefs.conf file
        self.pvfs2genconfig_opts = ""
        
        #------------------------------
        #
        # build options
        #
        #------------------------------
        
        
        # --enable-fuse
        self.install_fuse=False
            
        # --prefix=
        self.install_prefix = None
        
        # --with-db=
        self.db4_prefix = "/opt/db4"

        # add --with-kernel option
        self.install_OFS_client = True

        # add --enable=shared
        self.install_shared = False
        
        # --enable-strict
        self.enable_strict = True
        
        # disable security. Options are "Key" and "Cert"
        self.ofs_security_mode = None
        
        # build the kernel module?
        self.ofs_build_kmod = True
        
        # compile with -g debugging option?
        self.ofs_compile_debug = True

        # list of patches to patch OrangeFS source
        self.ofs_patch_files=[]
        
        # username of svn user. Allows checkout instead of export
        self.svn_username = None

        # Additional options for configure
        self.configure_opts = ""

        #------------------------------
        #
        # installation options
        #
        #------------------------------
        
        
        # Mount the filesystem after setup
        self.mount_OFS_after_setup = True
        
        # Mount filesystem using the fuse module instead of kernel module.
        self.ofs_mount_as_fuse = False
        
        # Install OrangeFS tests
        self.install_tests = True
        
        # Install and start the OrangeFS server software
        self.install_OFS_server = True
        
        # Install MPI software, including Torque
        self.install_MPI = False
        
        # Additional installation option
        self.install_opts = ""
        
        
        #------------------------------
        #
        # existing installation options
        #
        #------------------------------
        
        
        # location of suite of testing benchmarks
        self.ofs_extra_tests_location = None
        
        # location of PVFS2TAB_FILE. Mountpoint will be read from file
        self.ofs_pvfs2tab_file = None
        
        # location of orangefs on the main node
        self.ofs_source_location = None
        
        #Location of OrangeFS.conf file
        self.ofs_config_file = None
        
        # Delete data on OrangeFS partition
        self.delete_existing_data = False
        
        # OrangeFS mount point
        self.ofs_mount_point = None
        
        # Override the hostname given by hostname command. Will force the
        # hostname to be the value provided. Needed to workaround
        # a bug on some ec2 setups.
        self.ofs_host_name_override = []
        
        
        #------------------------------
        #
        # Test Flags
        #
        #------------------------------
        
        self.run_sysint_tests = False
        self.run_usrint_tests = False
        self.run_vfs_tests = False
        self.run_fuse_tests = False
        self.run_mpi_tests = False
        
        
    
    #------------------------------------------------------------------
    #
    # setConfig
    #
    #
    # A universal method for setting the initial configuration. Overridden
    # in subclasses
    #
    #------------------------------------------------------------------

    
    def setConfig(self,kwargs={}):
        pass
        
    #------------------------------------------------------------------
    #
    # addConfig
    #
    #
    # This method can "override" existing values as specified in str_args
    # str_args are an array of variable=value strings.
    #
    #------------------------------------------------------------------

    
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
        
    #------------------------------------------------------------------
    #
    # printDict
    #
    #
    # This prints all the setup variables. Used for debugging.
    #
    #------------------------------------------------------------------

    
    def printDict(self):
        print self.__dict__


    #------------------------------------------------------------------
    #
    # setConfigFromDict
    #
    #
    # This method takes a formatted dictionary d and converts the values
    # into the variables of this class.
    #
    #------------------------------------------------------------------
    
    
    
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
        
        temp = d.get('ofs_host_name_override')
        if temp != None:
            
            userlist = temp.split(" ")
            #print userlist
            for user in userlist:
                self.ofs_host_name_override.append(user)
        
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

        temp = d.get('ec2_domain')
        if temp != None:
            self.ec2_domain = temp
        temp = d.get('ec2_associate_ip')
        if temp != None:
            self.ec2_associate_ip = temp

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
        
        
        
        
        
        
        
        
        