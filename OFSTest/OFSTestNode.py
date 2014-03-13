#!/usr/bin/python

##
#
# @class OFSTestNode
# 
# @brief This is the base class for all test nodes that run OrangeFS. Local and 
# remote; Client, Server, and Build nodes. 
#
# OFSTestNode is an abstract class. All nodes should be of a more specific 
# subclass type.
#
# This class represents all machines that are part of the OFS process. This includes local, remote
# and remote-cloud based machines.
#
# This program assumes that the OFSTestNode is a *nix machine operating a bash shell. 
# MacOSX functionality may be limited. Windows local nodes are not currently supported.
#
# The methods are broken down into the following:
#
# Object functions: Gets and sets for the object data.
# Utility functions: Basic shell functionality.
# OFSTestServer functions: Configure and run OrangeFS server
# OFSTestBuilder functions: Compile OrangeFS and build rpms/dpkgs.
# OFSTestClient functions: Configure and run OrangeFS client


import os
import subprocess
import shlex
import cmd
import time
import sys
import xml.etree.ElementTree as ET
import traceback


## @var batch_count
# global variable for batch counting
batch_count = 0

class OFSTestNode(object):
    
    ## @var node_number
    # variable for number of node in the cluster.
    node_number = 0
    
    ##
    # @fn __init__(self):
    # Initialize node. We don't have much info for the base class.
    # 
    # @param self The object pointer.
    
    def __init__(self):
        
#------------------------------------------------------------------------------
#
# Class members.
#
#
#------------------------------------------------------------------------------
        
        ## @var alias_list    
        # list of OrangeFS Aliases
        self.alias_list = None
        
        ## @var ip_address
        # ip address on internal network
        self.ip_address = ""
        
        ## @var ext_ip_address
        # ip address on external network
        self.ext_ip_address = self.ip_address
        
        ## @var hostname
        # current hostname
        self.hostname = ""
        
        ## @var distro
        # operating system
        self.distro = ""
        
        ## @var package_system
        # package system (rpm/dpkg)
        self.package_system=""
        
        ## @var kernel_source_location
        # location of the linux kernel source
        self.kernel_source_location = ""
        
        ## @var kernel_version 
        # Output of (uname -r)
        self.kernel_version=""
        
        ## @var is_remote
        # is this a remote machine?
        self.is_remote=True
        
        ## @var is_cloud
        # is this an cloud/openstack instance?
        self.is_cloud=False
        
        ## @var processor_type
        # type of processor (i386/x86_64)
        self.processor_type = "x86_64"
        
        #------
        #
        # shell variables
        #
        #-------
        
        ## @var current_user
        # main user login. Usually cloud-user for cloud instances.
        self.current_user = ""
        
        ## @var current_directory
        # current working directory
        self.current_directory = "~"
        
        ## @var previous_directory
        #previous directory
        self.previous_directory = "~"
        
        ## @var current_environment
        # current environment variables
        self.current_environment = {}
        
        ## @var batch_commands
        # commands written to a batch file
        self.batch_commands = []
        
        #-------------------------------------------------------
        # sshKeys
        #--------------------------------------------------------

        ## @var sshLocalKeyFile
        # The local key file is the location of the key on the local host. Local key file is ALWAYS on the
        # localhost.
        self.sshLocalKeyFile = ""
        
        ## @var sshNodeKeyFile
        # The node key file is the file on the node that contains the key to access this machine.       
        self.sshNodeKeyFile = ""
        
        ## @var keytable
        # The keytable is a dictionary of locations of keys to remote machines on the current node.
        self.keytable = {}
        
        #----------------------------------------------------------
        #
        # orangefs related variables
        #
        #----------------------------------------------------------
       
        ## @var ofs_source_location
        # This is the location of the OrangeFS source
        self.ofs_source_location = ""
        
        ## @var ofs_storage_location
        # This is the location of the OrangeFS storage
        self.ofs_storage_location = ""

        ## @var ofs_installation_location
        # This is where OrangeFS is installed. Defaults to /opt/orangefs
        self.ofs_installation_location = ""

        ## @var ofs_extra_tests_location
        # This is the location of the third party benchmarks
        self.ofs_extra_tests_location = ""

        ## @var ofs_mount_point
        # This is the mount_point for OrangeFS
        self.ofs_mount_point = ""
        
        ## @var ofs_fs_name
        # This is the OrangeFS service name. pvfs2-fs < 2.9, orangefs >= 2.9
        self.ofs_fs_name="orangefs"
        
        ## @var ofs_branch
        # svn branch (or ofs source directory name)
        self.ofs_branch = ""
        
        ## @var ofs_conf_file
        # Location of orangefs.conf file.
        self.ofs_conf_file = None
        
        ## @var build_kmod
        # Do we need to build the kernel module?
        self.build_kmod = False
        
        ## @var ofs_tcp_port
        # default tcp port
        self.ofs_tcp_port = "3396"
        
        ## @var db4_dir
        # berkeley db4 location
        self.db4_dir = "/opt/db4"
        
        ## @var db4_lib_dir
        # berkeley db4 library location
        self.db4_lib_dir = self.db4_dir+"/lib"
        
        
        self.mpich2_installation_location = ""
        self.mpich2_source_location = ""
        self.mpich2_version = ""
        self.created_mpichhosts = None 
        
        # OpenMPI related variables
        
        ## @var openmpi_installation_location
        ## @var openmpi_source_location
        ## @var openmpi_version
        ## @var created_openmpihosts
        # Created openmpihosts file
        
        self.openmpi_installation_location = ""
        self.openmpi_source_location = ""
        self.openmpi_version = ""
        self.created_openmpihosts = None  
        
        ## @var mpi_nfs_directory
        # Where is the common mpi nfs directory?
        self.mpi_nfs_directory = ""
        
        ## @var romio_runtests_pvfs2
        # Where is the romio test script?
         
        # Sloppy - revise.
        self.romio_runtests_pvfs2 = None
        
        
        # Hadoop variables
        ## @var hadoop_version
        # Version of hadoop software
        self.hadoop_version = "hadoop-1.2.1"
        
        ## @var hadoop_location
        # Location of hadoop installation
        self.hadoop_location = "/opt/"+self.hadoop_version
        
        ## @var jdk6_location
        # Location of Oracle JDK 6
        self.jdk6_location = "/usr/java/default"
        
        
        
        # Information about node in the network.
        self.node_number = OFSTestNode.node_number
        OFSTestNode.node_number += 1

    ##
    # 
    # @fn currentNodeInformation(self):
    #
    # Logs into the node to gain information about the system
    #
    # @param self The object pointer
    

    def currentNodeInformation(self):
        
        self.distro = ""
        #print "Getting current node information"


        # can we ssh in? We'll need the group if we can't, so let's try this first.
        output = []
        self.runSingleCommand("ls -l /home/ | grep %s | awk '{print \\$4}'" % self.current_user,output)
        print output
        
        
        
        self.current_group = self.runSingleCommandBacktick(command="ls -l /home/ | grep %s | awk '{print \\$4}'" % self.current_user)

        # is this a mac? Home located under /Users
        # Wow, this is ugly. Need to stop hardcoding "/home"
        if self.current_group.rstrip() == "":
            self.current_group = self.runSingleCommandBacktick(command="ls -l /Users/ | grep %s | awk '{print \\$4}'" % self.current_user)

        print "Current group is "+self.current_group

        # Direct access as root not good. Need to get the actual user in
        # Gross hackery for SuseStudio images. OpenStack injects key into root, not user.
                    
        if self.current_group.rstrip() == "":
            self.current_group = self.runSingleCommandBacktick(command="ls -l /home/ | grep %s | awk '{print \\$4}'" % self.current_user,remote_user="root")

            print "Current group for %s (from root) is %s" % (self.current_user,self.current_group)
            if self.current_group.rstrip() == "":
                print "Could not access node at "+self.ext_ip_address+" via ssh"
                exit(-1)
            
            
            # copy the ssh key to the user's directory
            rc = self.runSingleCommand(command="cp -r /root/.ssh /home/%s/" % self.current_user,remote_user="root")
            if rc != 0:
                print "Could not copy ssh key from /root/.ssh to /home/%s/ " % self.current_user
                exit(rc)
            
            #get the user and group name of the home directory
            
            # change the owner of the .ssh directory from root to the login user
            rc = self.runSingleCommand(command="chown -R %s:%s /home/%s/.ssh/" % (self.current_user,self.current_group,self.current_user),remote_user="root") 
            if rc != 0:
                print "Could not change ownership of /home/%s/.ssh to %s:%s" % (self.current_user,self.current_user,self.current_group)
                exit(rc)
            


        # get kernel version and processor type
        self.kernel_version = self.runSingleCommandBacktick("uname -r")
        self.processor_type = self.runSingleCommandBacktick("uname -p")
        
        
        # Find the distribution. Unfortunately Linux distributions each have their own file for distribution information.
            
        # information for ubuntu and suse is in /etc/os-release

        if self.runSingleCommandBacktick('find /etc -name "os-release" 2> /dev/null').rstrip() == "/etc/os-release":
            #print "SuSE or Ubuntu based machine found"
            pretty_name = self.runSingleCommandBacktick("cat /etc/os-release | grep PRETTY_NAME")
            [var,self.distro] = pretty_name.split("=")
        # for redhat based distributions, information is in /etc/system-release
        elif self.runSingleCommandBacktick('find /etc -name "redhat-release" 2> /dev/null').rstrip() == "/etc/redhat-release":
            #print "RedHat based machine found"
            self.distro = self.runSingleCommandBacktick("cat /etc/redhat-release")
        elif self.runSingleCommandBacktick('find /etc -name "lsb-release" 2> /dev/null').rstrip() == "/etc/lsb-release":
            #print "Ubuntu based machine found"
            #print self.runSingleCommandBacktick("cat /etc/lsb-release ")
            pretty_name = self.runSingleCommandBacktick("cat /etc/lsb-release | grep DISTRIB_DESCRIPTION")
            #print "Pretty name " + pretty_name
            [var,self.distro] = pretty_name.split("=")    
        # Mac OS X 
        elif self.runSingleCommandBacktick('find /etc -name "SuSE-release" 2> /dev/null').rstrip() == "/etc/SuSE-release":
            self.distro = self.runSingleCommandBacktick("head -n 1 /etc/SuSE-release").rstrip()

            
        elif self.runSingleCommandBacktick("uname").rstrip() == "Darwin":
            #print "Mac OS X based machine found"
            self.distro = "Mac OS X-%s" % self.runSingleCommandBacktick("sw_vers -productVersion")

        # get the hostname
        self.hostname = self.runSingleCommandBacktick("hostname")

        # SuSE distros require a hostname kludge to get it to work. Otherwise all instances will be set to the same hostname
        # That's a better solution than what Openstack gives us. So why not? 
        if self.is_cloud == True:
            suse_host = "ofsnode-%d" % self.node_number
            print "Renaming %s based node to %s" % (self.distro,suse_host)
            self.runSingleCommandAsBatch("sudo hostname %s" % suse_host)
            self.runSingleCommandAsBatch("sudo bash -c 'echo %s > /etc/HOSTNAME'" % suse_host)
            self.hostname = suse_host
            
        # Torque doesn't like long hostnames. Truncate the hostname to 15 characters if necessary.
        elif len(self.hostname) > 15 and self.is_cloud == True:
            short_hostname = self.hostname[:15]
            self.runSingleCommandAsBatch("sudo bash -c 'echo %s > /etc/hostname'" % short_hostname)
            self.runSingleCommandAsBatch("sudo hostname %s" % short_hostname)
            print "Truncating hostname %s to %s" % (self.hostname,short_hostname)
            self.hostname = self.hostname[:15]
        elif self.is_cloud == False:
            print "Not an Cloud Node!"
        
        # print out node information
        print "Node: %s %s %s %s" % (self.hostname,self.distro,self.kernel_version,self.processor_type)
        
        
       
#==========================================================================
# 
# Utility functions
#
# These functions implement basic shell functionality 
#
#==========================================================================

    ##
    # @fn changeDirectory(self, directory):
    # Change the current directory on the node to run scripts.
    #
    # @param self The object pointer
    # @param directory New directory. Note: "-" will change to previous directory.
    
    def changeDirectory(self, directory):
        # cd "-" will restore previous directory
        if directory is not "-": 
            self.previous_directory = self.current_directory
            self.current_directory = directory
        else:
            self.restoreDirectory()
    ##
    # @fn restoreDirectory(self):
    # Restore directory - This restores the previous directory.
    # @param self The object pointer
    
    def restoreDirectory(self):
        temp = self.current_directory
        self.current_directory = self.previous_directory
        self.previous_directory = temp

    ##
    # @fn setEnvironmentVariable(self,variable,value):  
    # set an environment variable to a value
    # @param self The object pointer    
    # @param variable Variable name
    # @param value Value of variable
    
    def setEnvironmentVariable(self,variable,value):
        self.current_environment[variable] = value
    
    ##
    # @fn unsetEnvironmentVariable(self,variable):
    # Erase an environment variable
    # @param self The object pointer
    # @param variable Variable name
    
    def unsetEnvironmentVariable(self,variable):
        del self.current_environment[variable] 
    
    ## 
    # @fn setEnvironment(self, setenv): 
    # This function sets the environment based on the output of setenv.
    # 
    # @param self The object pointer
    # @param setenv A string that is formatted like the output of the setenv command.
     
    def setEnvironment(self, setenv):
    
        variable_list = setenv.split('\n')
        for variable in variable_list:
            #split based on the equals sign
            vname,value = variable.split('=')
            self.setEnvironmentVariable(vname,value)
          
    ##
    # @fn clearEnvironment(self):
    # Clear all environment variables
    # @param self The object pointer
    
    def clearEnvironment(self):
        self.current_environment = {}
    
    ##
    # @fn printWorkingDirectory(self):  
    # 
    # @param self The object pointer
    # @return current directory
    
    def printWorkingDirectory(self):
        return self.current_directory
    
    
    ##
    # @fn addBatchCommand(self,command):
    # Add a command to the list of batch commands to be run.
    # This is generally the single line of a shell script.
    # @param self The object pointer
    # @param command The command to add
    
    def addBatchCommand(self,command):
        self.batch_commands.append(command)
    
    ##
    # @fn runSingleCommand(self,command,output=[],remote_user=None):
    # This runs a single command and returns the return code of that command
    #
    # command, stdout, and stderr are in the output list
    # @param self The object pointer
    # @param command The command to run
    # @param output Output list
    # @param remote_user User to run as. Default is current user.
    
    def runSingleCommand(self,command,output=[],remote_user=None):
        
        
        
        #print command
        if remote_user==None:
            remote_user = self.current_user
    
        # get the correct format of the command line for the node we are running on.    
        command_line = self.prepareCommandLine(command=command,remote_user=remote_user)
        
        # run via Popen
        p = subprocess.Popen(command_line,shell=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,bufsize=-1)
        
        # clear the output list, then append stdout,stderr to list to get pass-by-reference to work
        del output[:]
        output.append(command_line)
        for i in p.communicate():
            output.append(i)

        return p.returncode
     
    ##
    # @fn runSingleCommandBacktick(self,command,output=[],remote_user=None):
    # This runs a single command and returns the stdout of that command.
    # @param self The object pointer
    # @param command The command to run
    # @param output Output list
    # @param remote_user User to run as. Default is current user.
          
    def runSingleCommandBacktick(self,command,output=[],remote_user=None):
        
        if remote_user==None:
            remote_user = self.current_user
      
        
        self.runSingleCommand(command=command,output=output,remote_user=remote_user)
        if len(output) >= 2:
            return output[1].rstrip('\n')
        else:
            return ""
    
    ##
    # @fn runOFSTest(self,package,test_function,output=[],logfile="",errfile=""):
    # This method runs an OrangeFS test on the given node
    #
    # Output and errors are written to the output and errfiles
    # 
    # return is return code from the test function
    # @param self The object pointer
    # @param package Test package name
    # @param test_function Test function to run.
    # @param output Output list
    # @param logfile File to log stdout
    # @param errfile File to log stderr
    
    
#
    def runOFSTest(self,package,test_function,output=[],logfile="",errfile=""):

       
        print "Running test %s-%s" % (package,test_function.__name__)
        
        if logfile == "":
            logfile = "%s-%s.log" % (package,test_function.__name__)
        
                
        # Run the test function
        rc = test_function(self,output)

        try:
            # write the command, return code, stdout and stderr of last program to logfile
            logfile_h = open(logfile,"w+")
            logfile_h.write('COMMAND:' + output[0]+'\n')
            logfile_h.write('RC: %r\n' % rc)
            logfile_h.write('STDOUT:' + output[1]+'\n')
            logfile_h.write('STDERR:' + output[2]+'\n')
            
        except:
            
            traceback.print_exc()
            # RC -999 is a test program error.
            rc = -999
        
        logfile_h.close()
            
        
        return rc
    
    ##
    # @fn prepareCommandLine(self,command,outfile="",append_out=False,errfile="",append_err=False,remote_user=None):   
    # This method prepares the command line for run single command. 
    # Should not be implemented here, but in subclass
    #
    # @param self The object pointer
    # @param command Shell command to be run.
    # @param outfile File to redirect stdout to.
    # @param append_out Append outfile or overwrite?
    # @param errfile File to redirect stderr to.
    # @param append_err Append errfile or overwrite?
    # @param remote_user Run command as this user
    #
    # @return String Formatted command line.
    
    
    def prepareCommandLine(self,command,outfile="",append_out=False,errfile="",append_err=False,remote_user=None):
        # Implimented in the client. Should not be here.
        print "This should be implimented in the subclass, not in OFSTestNode."
        print "Trying naive attempt to create command list."
        return command
    
    ##
    # @fn runAllBatchCommands(self,output=[]):
    # This method runs all the batch commands in the list. 
    # Should not be implemented here, but in the subclass.
    # @param self The object pointer
    # @param output Output list

       
    def runAllBatchCommands(self,output=[]):
        # implemented in child class
        pass
    
    ##
    # @fn runSingleCommandAsBatch(self,command,output=[]):
    # Run a single command as a batchfile. Some systems require this for passwordless sudo
    # @param self The object pointer
    # @param command Shell command to be run.
    # @param output Output list
    
    
    def runSingleCommandAsBatch(self,command,output=[]):
        self.addBatchCommand(command)
        self.runAllBatchCommands(output)
    
    ##
    # @fn runBatchFile(self,filename,output=[]):
    # Run a batch file through the system.
    #
    # Not sure why this is here
    # @param self The object pointer
    # @param filename Batch file name
    # @param output Output list
    
    def runBatchFile(self,filename,output=[]):
        #copy the old batch file to the batch commands list
        batch_file = open(filename,'r')
        self.batch_commands = batch_file.readlines()
        
        # Then run it
        self.runAllBatchCommands(output)
        
    ##
    # @fn copyToRemoteNode(self, source, destination_node, destination, recursive=False):
    # copy files from the current node to a destination node.
    # Should not be implemented here, but in the subclass.
    # @param self The object pointer
    # @param source Source file or directory
    # @param destination_node Node to which files should be copied
    # @param destination Destination file or directory on remote node.
    # @param recursive Copy recursively?
    #
    # @return Return code of copy command.
    
    
    def copyToRemoteNode(self, source, destination_node, destination, recursive=False):
        # implimented in subclass
        pass

    ##
    #
    # @fn copyFromRemoteNode(self, source_node, source, destination, recursive=False):
    #
    # This copies files from the remote node to this node via rsync.
    # Should not be implemented here, but in the subclass.
    #
    # @param self The object pointer
    # @param source_node Node from which files should be copied
    # @param source Source file or directory on remote node.
    # @param destination Destination file or directory
    # @param recursive Copy recursively?
    #
    # @return Return code of copy command.
    
    def copyFromRemoteNode(self,sourceNode, source, destination, recursive=False):
        # implimented in subclass
        pass
    
    ##
    # @fn writeToOutputFile(self,command_line,cmd_out,cmd_err):
    # writeToOutputFile()
    #
    # Write output (command, stdout, stderr) from runSingleCommand to a file.
    # 
    # @param self The object pointer
    # @param command_line command that was run
    # @param cmd_out stdout of command
    # @param cmd_err stderr of command
    
      
    def writeToOutputFile(self,command_line,cmd_out,cmd_err):
        
        outfile = open("output.out","a+")
        outfile.write("bash$ "+command_line)
        outfile.write("\n")
        outfile.write("Output: "+cmd_out)
        outfile.write("Stderr: "+cmd_err)
        outfile.write("\n")
        outfile.write("\n")
        outfile.close()
      
    ##
    # @fn getRemoteKeyFile(self,address):
    #
    # ssh utility functions
    # @param self The object pointer
    # @param address remote ip address
    
    def getRemoteKeyFile(self,address):
        #print "Looking for %s in keytable for %s" % (address,self.hostname)
        #print self.keytable
        return self.keytable[address]
    
    ##
    # @fn addRemoteKey(self,address,keylocation):
    #This method adds the location of the key for machine at address to the keytable.
    # @param self The object pointer
    # @param address ip address of remote machine
    # @param keylocation location of ssh key for remote machine on node.
      
    def addRemoteKey(self,address,keylocation):
        #
        #This method adds the location of the key for machine at address to the keytable.
        #
        self.keytable[address] = keylocation
    
    ##
    # @fn copyLocal(self, source, destination, recursive):
    # This runs the copy command locally 
    # @param self The object pointer
    # @param source Source directory
    # @param destination Destination directory
    # @param recursive Copy recursively?
     
    def copyLocal(self, source, destination, recursive=False):
        
        rflag = ""
        # verify source file exists
        if recursive == True:
            rflag = "-a"
        else:
            rflag = ""
          
        rsync_command = "rsync %s %s %s" % (rflag,source,destination)
        output = []
        rc = self.runSingleCommand(rsync_command, output)
        if rc != 0:
            print rsync_command+" failed!"
            print output
        return rc
      
 
    #============================================================================
    #
    # OFSBuilderFunctions
    #
    # These functions implement functionality to build OrangeFS
    #
    #=============================================================================
    
    ##
    # @fn updateNode(self):
    #
    # This function updates the software on the node via the package management system
    # @param self The object pointer
    #
    
    
    
    def updateNode(self):
        #print "Distro is " + self.distro
        if "ubuntu" in self.distro.lower() or "mint" in self.distro.lower() or "debian" in self.distro.lower():
            self.addBatchCommand("sudo DEBIAN_FRONTEND=noninteractive apt-get -y update")
            self.addBatchCommand("sudo DEBIAN_FRONTEND=noninteractive apt-get -y dist-upgrade < /dev/zero")
            self.addBatchCommand("sudo /sbin/reboot ")
        elif "suse" in self.distro.lower():
            self.addBatchCommand("sudo zypper --non-interactive update")
            self.addBatchCommand("sudo /sbin/reboot &")
        elif "centos" in self.distro.lower() or "scientific linux" in self.distro.lower() or "red hat" in self.distro.lower() or "fedora" in self.distro.lower():
            self.addBatchCommand("sudo yum update --disableexcludes=main -y")
            # Uninstall the old kernel
            self.addBatchCommand("sudo rpm -e kernel-`uname -r`")
            #Update grub from current kernel to installed kernel
            self.addBatchCommand('sudo perl -e "s/`uname -r`/`rpm -q --queryformat \'%{VERSION}-%{RELEASE}.%{ARCH}\n\' kernel`/g" -p -i /boot/grub/grub.conf')
            self.addBatchCommand("sudo /sbin/reboot ")
        
        self.runAllBatchCommands()
        print "Node "+self.hostname+" at "+self.ip_address+" updated."
        
        print "Node "+self.hostname+" at "+self.ip_address+" Rebooting."
    
    ##
    # @fn installTorqueServer(self):
    #
    # This function installs and configures the torque server from the package management system on the node.
    # @param self The object pointer
    #
    
    def installTorqueServer(self):
        
        #Each distro handles torque slightly differently.
        
        if "ubuntu" in self.distro.lower() or "mint" in self.distro.lower() or "debian" in self.distro.lower():
            batch_commands = '''

                #install torque
                echo "Installing TORQUE from apt-get"
                sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -q torque-server torque-scheduler torque-client torque-mom < /dev/null 
                sudo bash -c "echo %s > /etc/torque/server_name"
                sudo bash -c "echo %s > /var/spool/torque/server_name"
                

            ''' % (self.hostname,self.hostname)
            self.addBatchCommand(batch_commands)

        elif "suse" in self.distro.lower():
            
            # Torque should already have been installed via SuSE studio, but it needs to be setup.

            batch_commands = '''
                sudo bash -c "echo %s > /etc/torque/server_name"
                sudo bash -c "echo %s > /var/spool/torque/server_name"
            ''' % (self.hostname,self.hostname)
           
            

            self.addBatchCommand(batch_commands)
        elif "centos" in self.distro.lower() or "scientific linux" in self.distro.lower() or "red hat" in self.distro.lower() or "fedora" in self.distro.lower():
            
            if "6." in self.distro:
                batch_commands = '''
                   

                    echo "Adding epel repository"
                    wget http://dl.fedoraproject.org/pub/epel/6/%s/epel-release-6-8.noarch.rpm
                    sudo rpm -Uvh epel-release-6*.noarch.rpm
                    echo "Installing TORQUE from rpm: "
                    sudo yum -y update
                    sudo yum -y install torque-server torque-client torque-mom torque-scheduler munge
                    sudo bash -c '[ -f /etc/munge/munge.key ] || /usr/sbin/create-munge-key'
                    sudo bash -c "echo %s > /etc/torque/server_name"
                    sudo bash -c "echo %s > /var/lib/torque/server_name"

                ''' % (self.processor_type,self.hostname,self.hostname)
            elif "5." in self.distro:
                batch_commands = '''
                   

                    echo "Adding epel repository"
                    wget http://dl.fedoraproject.org/pub/epel/5/%s/epel-release-5-4.noarch.rpm
                    sudo rpm -Uvh epel-release-5*.noarch.rpm
                    echo "Installing TORQUE from rpm: "
                    sudo yum -y update
                    sudo yum -y install torque-server torque-client torque-mom torque-scheduler munge
                    sudo bash -c '[ -f /etc/munge/munge.key ] || /usr/sbin/create-munge-key'
                    sudo bash -c "echo %s > /etc/torque/server_name"
                    sudo bash -c "echo %s > /var/lib/torque/server_name"

                ''' % (self.processor_type,self.hostname,self.hostname)
            else:
                print "TODO: Torque for "+self.distro
                batch_commands = ""
            #print batch_commands
            self.addBatchCommand(batch_commands)
            
            # The following commands setup the Torque queue for OrangeFS on all systems.
            qmgr_commands = '''            
                sudo qmgr -c "set server scheduling=true"
                sudo qmgr -c "create queue orangefs_q queue_type=execution"
                sudo qmgr -c "set queue orangefs_q started=true"
                sudo qmgr -c "set queue orangefs_q enabled=true"
                sudo qmgr -c "set queue orangefs_q resources_default.nodes=1"
                sudo qmgr -c "set queue orangefs_q resources_default.walltime=3600"
                sudo qmgr -c "set server default_queue=orangefs_q"
                sudo qmgr -c "set server operators += %s@%s"
                sudo qmgr -c "set server managers += %s@%s"
                ''' % (self.current_user,self.hostname,self.current_user,self.hostname)

            self.addBatchCommand(batch_commands)
        
        self.runAllBatchCommands()
        
    ##
    # @fn installTorqueClient(self,pbsserver):
    #
    # This function installs and configures the torque client from the package management system on the node.
    # @param self The object pointer
    # @param pbsserver OFSTestNode that is the pbsserver
            
                                
        

     
    def installTorqueClient(self,pbsserver):
        pbsserver_name = pbsserver.hostname
        print "Installing Torque Client for "+self.distro.lower()
        if "ubuntu" in self.distro.lower() or "mint" in self.distro.lower() or "debian" in self.distro.lower():
            batch_commands = '''

                #install torque
                echo "Installing TORQUE from apt-get"
                sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -q torque-client torque-mom  < /dev/null 
                sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -q libtorque2 libtorque2-dev  < /dev/null 
                sudo bash -c 'echo \$pbsserver %s > /var/spool/torque/mom_priv/config' 
                sudo bash -c 'echo \$logevent 255 >> /var/spool/torque/mom_priv/config'
                sudo bash -c 'echo %s > /etc/torque/server_name' 
            ''' % (pbsserver_name,pbsserver_name)

            self.addBatchCommand(batch_commands)

        elif "suse" in self.distro.lower():

            # RPMS should have already been installed via SuSE studio
            batch_commands = '''
                sudo bash -c 'echo \$pbsserver %s > /var/spool/torque/mom_priv/config' 
                sudo bash -c 'echo \$logevent 255 >> /var/spool/torque/mom_priv/config'
                sudo bash -c 'echo %s > /etc/torque/server_name' 
           

            ''' % (pbsserver_name,pbsserver_name)
            self.addBatchCommand(batch_commands)
        elif "centos" in self.distro.lower() or "scientific linux" in self.distro.lower() or "red hat" in self.distro.lower() or "fedora" in self.distro.lower():
            
            if "6." in self.distro:
                batch_commands = '''
               
                echo "Adding epel repository"
                wget http://dl.fedoraproject.org/pub/epel/6/%s/epel-release-6-8.noarch.rpm
                sudo rpm -Uvh epel-release-6*.noarch.rpm
                echo "Installing TORQUE from rpm: "
                sudo yum -y update
                sudo yum -y install torque-client torque-mom munge
                sudo bash -c '[ -f /etc/munge/munge.key ] || /usr/sbin/create-munge-key'
                sudo bash -c 'echo \$pbsserver %s > /var/lib/torque/mom_priv/config' 
                sudo bash -c 'echo \$logevent 255 >> /var/lib/torque/mom_priv/config' 
                sudo bash -c 'echo %s > /etc/torque/server_name' 
                ''' % (self.processor_type,pbsserver_name,pbsserver_name)
            elif "5." in self.distro:
                batch_commands = '''
               
                echo "Adding epel repository"
                wget http://dl.fedoraproject.org/pub/epel/5/%s/epel-release-5-4.noarch.rpm
                sudo rpm -Uvh epel-release-5*.noarch.rpm
                echo "Installing TORQUE from rpm: "
                sudo yum -y update
                sudo yum -y install torque-client torque-mom munge
                
                sudo bash -c '[ -f /etc/munge/munge.key ] || /usr/sbin/create-munge-key'
                sudo bash -c 'echo \$pbsserver %s > /var/lib/torque/mom_priv/config' 
                sudo bash -c 'echo \$logevent 255 >> /var/lib/torque/mom_priv/config' 
                sudo bash -c 'echo %s > /etc/torque/server_name' 
                sudo /etc/init.d/munge start
                ''' % (self.processor_type,pbsserver_name,pbsserver_name)
            else:
                print "TODO: Torque for "+self.distro
                batch_commands = ""
            self.addBatchCommand(batch_commands) 
        #print batch_commands  
        self.runAllBatchCommands()
            
    ##
    # @fn restartTorqueServer(self):
    # When installed from the package manager, torque doesn't start with correct options. This restarts it.
    # @param self The object pointer
    #
  


    def restartTorqueServer(self):
        if "ubuntu" in self.distro.lower() or "mint" in self.distro.lower() or "debian" in self.distro.lower():
            batch_commands = '''
            sudo /etc/init.d/torque-server restart
            sudo /etc/init.d/torque-scheduler restart
            '''
            
        elif "centos" in self.distro.lower() or "scientific linux" in self.distro.lower() or "red hat" in self.distro.lower() or "fedora" in self.distro.lower():
            batch_commands = '''
            sudo /etc/init.d/munge stop
            sudo /etc/init.d/munge start
            sudo /etc/init.d/pbs_server stop
            sudo /etc/init.d/pbs_server start
            sudo /etc/init.d/pbs_sched stop
            sudo /etc/init.d/pbs_sched start
            '''
        elif "suse" in self.distro.lower():
            batch_commands = '''
            sudo /etc/init.d/pbs_server stop
            sudo /etc/init.d/pbs_server start
            sudo /etc/init.d/pbs_sched stop
            sudo /etc/init.d/pbs_sched start
            '''
        self.addBatchCommand(batch_commands)
        self.runAllBatchCommands()

        
    ##
    # @fn restartTorqueMom(self):
    #
    # When installed from the package manager, torque doesn't start with correct options. This restarts it.
    # @param self The object pointer
    #


    
    def restartTorqueMom(self):
        if "ubuntu" in self.distro.lower() or "mint" in self.distro.lower() or "debian" in self.distro.lower():
            self.runSingleCommandAsBatch("sudo /etc/init.d/torque-mom restart")
        elif "centos" in self.distro.lower() or "scientific linux" in self.distro.lower() or "red hat" in self.distro.lower() or "fedora" in self.distro.lower() or "suse" in self.distro.lower():
              
            self.runSingleCommandAsBatch("sudo /etc/init.d/pbs_mom restart")


    


    ##
    # @fn installRequiredSoftware(self):
    # 
    # This installs all the prerequisites for building and testing OrangeFS from the package management system
    # @param self The object pointer
    #



    def installRequiredSoftware(self):
        
        
        if "ubuntu" in self.distro.lower() or "mint" in self.distro.lower() or "debian" in self.distro.lower():
            batch_commands = '''
                sudo bash -c 'echo 0 > /selinux/enforce'
                sudo DEBIAN_FRONTEND=noninteractive apt-get update > /dev/null
                #documentation needs to be updated. linux-headers needs to be added for ubuntu!
                sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -q openssl gcc g++ gfortran flex bison libssl-dev linux-source perl make linux-headers-`uname -r` zip subversion automake autoconf  pkg-config rpm patch libuu0 libuu-dev libuuid1 uuid uuid-dev uuid-runtime < /dev/null
                sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -q libfuse2 fuse-utils libfuse-dev < /dev/null
                sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -q autofs nfs-kernel-server rpcbind nfs-common nfs-kernel-server < /dev/null
                # needed for Ubuntu 10.04
                sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -q linux-image < /dev/null
                # will fail on Ubuntu 10.04. Run separately to not break anything
                sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -q fuse < /dev/null
                #sudo DEBIAN_FRONTEND=noninteractive apt-get install -yu avahi-autoipd  avahi-dnsconfd  avahi-utils avahi-daemon    avahi-discover  avahi-ui-utils </dev/null
                sudo apt-get clean

                #prepare source
                SOURCENAME=`find /usr/src -name "linux-source*" -type d -prune -printf %f`
                cd /usr/src/${SOURCENAME}
                sudo tar -xjf ${SOURCENAME}.tar.bz2  &> /dev/null
                cd ${SOURCENAME}/
                sudo cp /boot/config-`uname -r` .config
                sudo make oldconfig &> /dev/null
                sudo make prepare &>/dev/null
                if [ ! -f /lib/modules/`uname -r`/build/include/linux/version.h ]
                then
                sudo ln -s include/generated/uapi/version.h /lib/modules/`uname -r`/build/include/linux/version.h
                fi
                sudo /sbin/modprobe -v fuse
                sudo chmod a+x /bin/fusermount
                sudo chmod a+r /etc/fuse.conf
                sudo rm -rf /opt
                sudo ln -s /mnt /opt
                sudo chmod -R a+w /mnt
                sudo service cups stop
                sudo service sendmail stop
                sudo service rpcbind restart
                sudo service nfs-kernel-server restart

                # install Sun Java6 for hadoop via webupd8
                sudo add-apt-repository ppa:webupd8team/java < /dev/null
                sudo apt-get update 
                sudo bash -c 'echo debconf shared/accepted-oracle-license-v1-1 select true | debconf-set-selections'
                sudo bash -c 'echo debconf shared/accepted-oracle-license-v1-1 seen true | debconf-set-selections'
                sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -q oracle-java6-installer < /dev/null
                

            '''
            self.addBatchCommand(batch_commands)
            
            # ubuntu installs java to a different location than RHEL and SuSE 
            self.jdk6_location = "/usr/lib/jvm/java-6-oracle"
            
            
        elif "suse" in self.distro.lower():
            batch_commands = '''
            sudo bash -c 'echo 0 > /selinux/enforce'
            sudo /sbin/SuSEfirewall2 off
            # prereqs should be installed as part of the image. Thanx SuseStudio!
            #zypper --non-interactive install gcc gcc-c++ flex bison libopenssl-devel kernel-source kernel-syms kernel-devel perl make subversion automake autoconf zip fuse fuse-devel fuse-libs sudo nano openssl
            sudo zypper --non-interactive patch libuuid1 uuid-devel
            

            cd /usr/src/linux-`uname -r | sed s/-[\d].*//`
            sudo cp /boot/config-`uname -r` .config
            sudo make oldconfig &> /dev/null
            sudo make modules_prepare &>/dev/null
            sudo make prepare &>/dev/null
            sudo ln -s /lib/modules/`uname -r`/build/Module.symvers /lib/modules/`uname -r`/source
            if [ ! -f /lib/modules/`uname -r`/build/include/linux/version.h ]
            then
            sudo ln -s include/generated/uapi/version.h /lib/modules/`uname -r`/build/include/linux/version.h
            fi
            sudo modprobe -v fuse
            sudo chmod a+x /bin/fusermount
            sudo chmod a+r /etc/fuse.conf
            #sudo mkdir -p /opt
            sudo rm -rf /opt
            sudo ln -s /mnt /opt
            sudo chmod -R a+w /opt
            cd /tmp
            #### Install Java 6 #####
            wget -q http://devorange.clemson.edu/pvfs/jdk-6u45-linux-x64-rpm.bin
            yes y | sudo bash ./jdk-6u45-linux-x64-rpm.bin
            
            # start rpcbind to work around a bug in OpenSuse
            sudo /sbin/rpcbind
            

            '''
            self.addBatchCommand(batch_commands)
            
            # RPM installs to default location
            self.jdk6_location = "/usr/java/default"
            
        elif "centos" in self.distro.lower() or "scientific linux" in self.distro.lower() or "red hat" in self.distro.lower() or "fedora" in self.distro.lower():
            
            batch_commands = '''
                sudo bash -c 'echo 0 > /selinux/enforce'
                echo "Installing prereqs via yum..."
                sudo yum -y install gcc gcc-c++ gcc-gfortran openssl fuse flex bison openssl-devel kernel-devel-`uname -r` kernel-headers-`uname -r` perl make subversion automake autoconf zip fuse fuse-devel fuse-libs wget patch bzip2 libuuid libuuid-devel uuid uuid-devel
                sudo yum -y install nfs-utils nfs-utils-lib nfs-kernel nfs-utils-clients rpcbind libtool libtool-ltdl 
                sudo /sbin/modprobe -v fuse
                sudo chmod a+x /bin/fusermount
                sudo chmod a+r /etc/fuse.conf
                #sudo mkdir -p /opt
                #link to use additional space in /mnt drive
                sudo rm -rf /opt
                sudo ln -s /mnt /opt
                sudo chmod -R a+w /mnt
                sudo chmod -R a+w /opt
                sudo service cups stop
                sudo service sendmail stop
                sudo service rpcbind start
                sudo service nfs restart
                
                # install java 6
                cd /tmp
                wget -q http://devorange.clemson.edu/pvfs/jdk-6u45-linux-x64-rpm.bin
                yes y | sudo bash ./jdk-6u45-linux-x64-rpm.bin
            
                


            '''
            self.addBatchCommand(batch_commands)
            
            # RPM installs to default location
            self.jdk6_location = "/usr/java/default"

        # db4 is built from scratch for all systems to have a consistant version.
        batch_commands = '''
        
        if [ ! -d %s ]
        then
            cd ~
            wget -q http://devorange.clemson.edu/pvfs/db-4.8.30.tar.gz
            tar zxf db-4.8.30.tar.gz &> /dev/null
            cd db-4.8.30/build_unix
            echo "Configuring Berkeley DB 4.8.30..."
            ../dist/configure --prefix=%s &> db4conf.out
            echo "Building Berkeley DB 4.8.30..."
            make &> db4make.out
            echo "Installing Berkeley DB 4.8.30 to %s..."
            make install &> db4install.out
        fi
        

        exit
        ''' % (self.db4_dir,self.db4_dir,self.db4_dir) 
        self.db4_lib_dir = self.db4_dir+"/lib"
        self.addBatchCommand(batch_commands)
        self.runAllBatchCommands()
        
        # Install Hadoop. 
        output = []
        self.changeDirectory("/opt")
        # Download from a helluva open source mirror.
        self.runSingleCommand("wget  http://www.gtlib.gatech.edu/pub/apache/hadoop/core/%s/%s.tar.gz" % (self.hadoop_version,self.hadoop_version),output )
        self.runSingleCommand("tar -zxf %s.tar.gz" % self.hadoop_version)

        # Add DB4 to the library path.
        self.setEnvironmentVariable("LD_LIBRARY_PATH","%s:$LD_LIBRARY_PATH" % self.db4_lib_dir)

    ##
    # @fn installMpich2(self,location=None):
    #
    # This function installs mpich2 software
    # TODO: Must be reimplemented to match OpenMPI installation.
    # @param self The object pointer
    # @param location Location to install mpich
    #

    
    

    def installMpich2(self,location=None):
        if location == None:
            location = "/home/%s/mpich2" % self.current_user
        
        mpich_version = "mpich-3.0.4"
            
        url = "http://devorange.clemson.edu/pvfs/%s.tar.gz" % mpich_version
        url = "wget"
        # just to make debugging less painful
        #[ -n "${SKIP_BUILDING_MPICH2}" ] && return 0
        #[ -d ${PVFS2_DEST} ] || mkdir ${PVFS2_DEST}
        self.runSingleCommand("mkdir -p "+location)
        tempdir = self.current_directory
        self.changeDirectory("/home/%s" % self.current_user)
        
        #wget http://www.mcs.anl.gov/research/projects/mpich2/downloads/tarballs/1.5/mpich2-1.5.tar.gz
        rc = self.runSingleCommand("wget --quiet %s" % url)
        #wget --passive-ftp --quiet 'ftp://ftp.mcs.anl.gov/pub/mpi/misc/mpich2snap/mpich2-snap-*' -O mpich2-latest.tar.gz
        if rc != 0:
            print "Could not download mpich from %s." % url
            self.changeDirectory(tempdir)
            return rc

        output = []
        self.runSingleCommand("tar xzf %s.tar.gz"% mpich_version)
        
        self.mpich2_source_location = "/home/%s/%s" % (self.current_user,mpich_version)
        self.changeDirectory(self.mpich2_source_location)
        #self.runSingleCommand("ls -l",output)
        #print output
        
        configure = '''
        ./configure -q --prefix=%s \
        --enable-romio --with-file-system=pvfs2 \
        --with-pvfs2=%s \
        --enable-g=dbg \
         >mpich2config.log
        ''' % (location,self.ofs_installation_location)
        
        #wd = self.runSingleCommandBacktick("pwd")
        #print wd
        #print configure
        

        print "Configuring MPICH"
        rc = self.runSingleCommand(configure,output)
        
        if rc != 0:
            print "Configure of MPICH failed. rc=%d" % rc
            print output
            self.changeDirectory(tempdir)
            return rc
        
        print "Building MPICH"
        rc = self.runSingleCommand("make > mpich2make.log")
        if rc != 0:
            print "Make of MPICH failed."
            print output
            self.changeDirectory(tempdir)
            return rc

        print "Installing MPICH"
        rc = self.runSingleCommand("make install > mpich2install.log")
        if rc != 0:
            print "Install of MPICH failed."
            print output
            self.changeDirectory(tempdir)
            return rc
        
        print "Checking MPICH install"
        rc = self.runSingleCommand("make installcheck > mpich2installcheck.log")
        if rc != 0:
            print "Install of MPICH failed."
            print output
            self.changeDirectory(tempdir)
            return rc
        
        self.mpich2_installation_location = location 
        # change this!
        self.romio_runtests_pvfs2 = self.mpich2_source_location+"ompi/mca/io/romio/romio/test/runtests"
        
        return 0
    
    ##
    # @fn installOpenMPI(self,install_location=None,build_location=None):
    #
    # This function installs OpenMPI software
    # @param self The object pointer
    # @param install_location Location to install OpenMPI
    # @param build_location Location to build OpenMPI
    #
    

    def installOpenMPI(self,install_location=None,build_location=None):
        
        
        if install_location == None:
            install_location = "/opt/mpi"
        
        if build_location == None:
            build_location = install_location
        
        self.openmpi_version = "openmpi-1.6.5"
        url_base = "http://devorange.clemson.edu/pvfs/"
        url = url_base+self.openmpi_version+"-omnibond-2.tar.gz"
        

        patch_name = "openmpi.patch"
        patch_url = url_base+patch_name
        
        self.runSingleCommand("mkdir -p "+build_location)
        tempdir = self.current_directory
        self.changeDirectory(build_location)
        
        #wget http://www.mcs.anl.gov/research/projects/mpich2/downloads/tarballs/1.5/mpich2-1.5.tar.gz
        rc = self.runSingleCommand("wget --quiet %s" % url)
        #wget --passive-ftp --quiet 'ftp://ftp.mcs.anl.gov/pub/mpi/misc/mpich2snap/mpich2-snap-*' -O mpich2-latest.tar.gz
        if rc != 0:
            print "Could not download %s from %s." % (self.openmpi_version,url)
            self.changeDirectory(tempdir)
            return rc

        output = []
        self.runSingleCommand("tar xzf %s-omnibond-2.tar.gz"% self.openmpi_version)
        
        self.openmpi_source_location = "%s/%s" % (build_location,self.openmpi_version)
        self.changeDirectory(self.openmpi_source_location)
        rc = self.runSingleCommand("wget --quiet %s" % patch_url)


        # using pre-patched version. No longer needed.
        '''
        print "Patching %s" %self.openmpi_version
        rc = self.runSingleCommand("patch -p0 < %s" % patch_name,output)
        
        
        if rc != 0:
            print "Patching %s failed. rc=%d" % (self.openmpi_version,rc)
            print output
            self.changeDirectory(tempdir)
            return rc
        
        self.runSingleCommand("sed -i s/ADIOI_PVFS2_IReadContig/NULL/ ompi/mca/io/romio/romio/adio/ad_pvfs2/ad_pvfs2.c")
        self.runSingleCommand("sed -i s/ADIOI_PVFS2_IWriteContig/NULL/ ompi/mca/io/romio/romio/adio/ad_pvfs2/ad_pvfs2.c")
        '''

        
        configure = './configure --prefix %s/openmpi --with-io-romio-flags=\'--with-pvfs2=%s --with-file-system=pvfs2+nfs\' >openmpiconfig.log' % (install_location,self.ofs_installation_location)
        

        print "Configuring %s" % self.openmpi_version
        rc = self.runSingleCommand(configure,output)
        
        if rc != 0:
            print "Configure of %s failed. rc=%d" % (self.openmpi_version,rc)
            print output
            self.changeDirectory(tempdir)
            return rc
        
        print "Making %s" % self.openmpi_version
        rc = self.runSingleCommand("make > openmpimake.log")
        if rc != 0:
            print "Make of %s failed."
            print output
            self.changeDirectory(tempdir)
            return rc

        print "Installing %s" % self.openmpi_version
        rc = self.runSingleCommand("make install > openmpiinstall.log")
        if rc != 0:
            print "Install of %s failed." % self.openmpi_version
            print output
            self.changeDirectory(tempdir)
            return rc
        
        #print "Checking MPICH install" % openmpi_version
        #rc = self.runSingleCommand("make installcheck > mpich2installcheck.log")
        #if rc != 0:
        #    print "Install of MPICH failed."
        #    print output
        #    self.changeDirectory(tempdir)
        #    return rc
        
        self.openmpi_installation_location = install_location+"/openmpi"
        
        self.romio_runtests_pvfs2 = self.openmpi_source_location+"/ompi/mca/io/romio/romio/test/runtests.pvfs2"
        self.runSingleCommand("chmod a+x "+self.romio_runtests_pvfs2)
        
        return 0
    
        
    


    ##
    # @fn copyOFSSourceFromSVN(self,svnurl,dest_dir,svnusername,svnpassword):
    #
    # This copies the source from an SVN branch
    # @param self The object pointer
    # @param svnurl Url of svn resource
    # @param dest_dir Destination directory on machine
    # @param svnusername svn username
    # @param svnpassword svn password
    # @param svnoptions additional SVN options
    


    def copyOFSSourceFromSVN(self,svnurl,dest_dir,svnusername,svnpassword,svn_options=None):
    
        output = []
        self.ofs_branch = os.path.basename(svnurl)
    
        #export svn by default. This merely copies from SVN without allowing update
        if svn_options == None:
            svn_options = ""
        svn_action = "export --force"
        
        # use the co option if we have a username and password
        if svnusername != "" and svnpassword != "":
            svn_options = "%s --username %s --password %s" % (svn_options, svnusername,svnpassword)
            svn_action = "co"
        
        print "svn %s %s %s" % (svn_action,svnurl,svn_options)
        self.changeDirectory(dest_dir)
        rc = self.runSingleCommand("svn %s %s %s" % (svn_action,svnurl,svn_options),output)
        if rc != 0:
            print "Could not export from svn"
            print output
            return rc
        else:
            self.ofs_source_location = "%s/%s" % (dest_dir.rstrip('/'),self.ofs_branch)
            print "svn exported to %s" % self.ofs_source_location
               
        return rc


    ##
    # @fn installBenchmarks(self,tarurl="http://devorange.clemson.edu/pvfs/benchmarks-20121017.tar.gz",dest_dir="",configure_options="",make_options="",install_options=""):
    #
    # This downloads and untars the thirdparty benchmarks
    # @param self The object pointer
    # @param tarurl Url of tarfile
    # @param dest_dir Destination on local machine
    # @param configure_options Options for configure
    # @param make_options Options for make
    # @param install_options Options for install
 


    def installBenchmarks(self,tarurl="http://devorange.clemson.edu/pvfs/benchmarks-20121017.tar.gz",dest_dir="",configure_options="",make_options="",install_options=""):
        if dest_dir == "":
            dest_dir = "/home/%s/" % self.current_user
        print "Installing benchmarks from "+tarurl
        tarfile = os.path.basename(tarurl)
        output = []
        
        #make sure the directory is there
        self.runSingleCommand("mkdir -p "+dest_dir)
        self.changeDirectory(dest_dir)
        self.runSingleCommand("rm " + tarfile)
        rc = self.runSingleCommand("wget " + tarurl, output)
        if rc != 0:
            print "Could not download benchmarks"
            print output
            return rc
        tarflags = ""
        taridx = 0
    
        if ".tar.gz" in tarfile:
            tarflags = "zxf"
            taridx = tarfile.index(".tar.gz")
        elif ".tgz" in tarfile:
            tarflags = "zxf"
            taridx = tarfile.index(".tgz")
        elif ".tar.bz2" in tarfile:
            tarflags = "jxf"
            taridx = tarfile.index(".tar.bz2")
        elif ".tar" in tarfile:
            tarflags = "xf"
            taridx = tarfile.index(".tar")
        else:
            print "%s Not a tarfile" % tarurl
            return 1
    
        tardir = tarfile[:taridx]
        rc = self.runSingleCommand("tar %s %s" % (tarflags, tarfile))
        #print self.runSingleCommandBacktick("ls %s" % dest_dir)
        if rc != 0:
            print "Could not untar benchmarks"
            print output
            return rc
        
        self.ofs_extra_tests_location = dest_dir+"/benchmarks" 
        #print "Extra tests location: "+self.ofs_extra_tests_location
        #print self.runSingleCommandBacktick("ls %s" % self.ofs_extra_tests_location)
        return 0
    
    
    ##
    # @fn makeFromTarFile(self,tarurl,dest_dir,configure_options="",make_options="",install_options=""):
    #
    # This is a generic function to ./configure, make, make install a tarball
    # @param self The object pointer
    # @param tarurl Url of tarfile
    # @param dest_dir Destination on local machine
    # @param configure_options Options for configure
    # @param make_options Options for make
    # @param install_options Options for install
 


    def makeFromTarFile(self,tarurl,dest_dir,configure_options="",make_options="",install_options=""):
        tarfile = os.path.basename(tarurl)
        self.changeDirectory(dest_dir)
        self.runSingleCommand("rm " + tarfile)
        self.runSingleCommand("wget " + tarurl)
        tarflags = ""
        taridx = 0
    
        if ".tar.gz" in tarfile:
            tarflags = "zxf"
            taridx = tarfile.index(".tar.gz")
        elif ".tgz" in tarfile:
            tarflags = "zxf"
            taridx = tarfile.index(".tgz")
        elif ".tar.bz2" in tarfile:
            tarflags = "jxf"
            taridx = tarfile.index(".tar.bz2")
        elif ".tar" in tarfile:
            tarflags = "xf"
            taridx = tarfile.index(".tar")
        else:
            print "%s Not a tarfile" % tarurl
            return 1
        
        tardir = tarfile[:taridx]
        self.runSingleCommand("tar %s %s" % (tarflags, tarfile))
        self.changeDirectory(tardir)
        self.runSingleCommand("./prepare")
        self.runSingleCommand("./configure "+ configure_options)
        
        self.runSingleCommand("make "+ make_options)
        self.runSingleCommand("make install "+install_options)
    
    
    ##
    # @fn copyOFSSourceFromRemoteTarball(self,tarurl,dest_dir):
    #
    # This downloads the source from a remote tarball. Several forms are 
    # supported
    # @param self The object pointer
    # @param tarurl Url of tarfile
    # @param dest_dir Destination on local machine
   
    
    
    def copyOFSSourceFromRemoteTarball(self,tarurl,dest_dir):
    
        tarfile = os.path.basename(tarurl)
        #make sure the directory is there
        self.runSingleCommand("mkdir -p "+dest_dir)
        self.changeDirectory(dest_dir)
        self.runSingleCommand("rm " + tarfile)
        output = []
        rc = self.runSingleCommand("wget " + tarurl,output)
        if rc != 0:
            print "Could not download OrangeFS"
            print output
            return rc
        tarflags = ""
        taridx = 0
        
        if ".tar.gz" in tarfile:
            tarflags = "zxf"
            taridx = tarfile.index(".tar.gz")
        elif ".tgz" in tarfile:
            tarflags = "zxf"
            taridx = tarfile.index(".tgz")
        elif ".tar.bz2" in tarfile:
            tarflags = "jxf"
            taridx = tarfile.index(".tar.bz2")
        elif ".tar" in tarfile:
            tarflags = "xf"
            taridx = tarfile.index(".tar")
        else:
            print "%s Not a tarfile" % tarurl
            return 1
        
        rc = self.runSingleCommand("tar %s %s" % (tarflags, tarfile),output)
        if rc != 0:
            print "Could not untar OrangeFS"
            print output
            return rc
        
        #remove the extension from the tarfile for the directory. That is the assumption
        self.ofs_branch = tarfile[:taridx]
        
        self.ofs_source_location = "%s/%s" % (dest_dir.rstrip('/'),self.ofs_branch)
        # Change directory /tmp/user/
        # source_location = /tmp/user/source
        return rc
  
    ##
    # @fn copyOFSSourceFromDirectory(self,directory,dest_dir):
    #
    # This copies the source from a local directory
    # @param self The object pointer
    # @param directory Directory that contains OFS source.
    # @param dest_dir Destination on local machine
   


    def copyOFSSourceFromDirectory(self,directory,dest_dir):
        rc = 0
        if directory != dest_dir:
            rc = self.copyLocal(directory,dest_dir,True)
        self.ofs_source_location = dest_dir
        dest_list = os.path.basename(dest_dir)
        self.ofs_branch = dest_list[-1]
        return rc
    
    ##
    # @fn copyOFSSourceFromRemoteNode(self,source_node,directory,dest_dir):
    #
    # This copies the source from a remote directory
    #
    # @param self The object pointer
    # @param source_node OFSTestNode that has the source
    # @param directory Directory that contains OFS source.
    # @param dest_dir Destination on local machine

      
    def copyOFSSourceFromRemoteNode(self,source_node,directory,dest_dir):
        #implemented in subclass
        return 0
  
    ##
    # @fn copyOFSSource(self,resource_type,resource,dest_dir,username="",password=""):
    #
    # This copies the source from wherever it is. Uses helper functions to get it from
    # the right place.
    # @param self The object pointer
    # @param resource_type Type of resource. Possible values are "SVN,TAR,LOCAL,BUILDNODE"
    # @param resource Resource location (url or directory)
    # @param dest_dir Destination on local machine
    # @param username Username needed to access resource
    # @param password Password needed to access resource     
    # @param options Options for accessing resource (e.g. svn options)
      
      
    def copyOFSSource(self,resource_type,resource,dest_dir,username="",password="",options=None):
        
        # Make directory dest_dir
        rc = self.runSingleCommand("mkdir -p %s" % dest_dir)
        if rc != 0:
            print "Could not mkdir -p %s" %dest_dir
            return rc
          
        
        # ok, now what kind of resource do we have here?
        # switch on resource_type
        #
        
        #print "Copy "+ resource_type+ " "+ resource+ " "+dest_dir
        
        if resource_type == "SVN":
            rc = self.copyOFSSourceFromSVN(resource,dest_dir,username,password,options)
        elif resource_type == "TAR":
            rc = self.copyOFSSourceFromRemoteTarball(resource,dest_dir)
        #elif resource_type == "REMOTEDIR":
        #    Remote node support not yet implimented.
        #  self.copyOFSSourceFromRemoteNode(directory,dest_dir)
        elif resource_type == "LOCAL":
            # Must be "pushed" from local node to current node instead of
            # "pulled" by the current node.
            #
            # Get around this by copying to the buildnode, then resetting type.
            # to "BUILDNODE"
            pass
        elif resource_type == "BUILDNODE":
            # Local directory on the current node. 
            rc = self.copyOFSSourceFromDirectory(resource,dest_dir)
        else:
            print "Resource type %s not supported!\n" % resource_type
            return -1
        
        
        return rc
        
    ##
    # @fn configureOFSSource(self,
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
    #         debug=False):
    #
    #
    # This prepares the OrangeFS source and runs the configure command.
    #
    # @param self The object pointer
    # @param build_kmod Build the kernel module
    # @param enable_strict Use --enable-strict option
    # @param enable_fuse Enable fuse support
    # @param enable_shared Build shared libraries
    # @param enable_hadoop Enable hadoop support
    # @param ofs_prefix Where to install OrangeFS
    # @param db4_prefix Location of Berkeley DB4 
    # @param security_mode OFS Security Mode: None,"Key","Cert"
    # @param ofs_patch_files List of patch files for OrangeFS
    # @param configure_opts Additional configure options
    # @param debug Debug mode?
      
    def configureOFSSource(self,
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
        debug=False):
    
        # Save build_kmod for later.
        self.build_kmod = build_kmod
        
        # Change directory to source location.
        self.changeDirectory(self.ofs_source_location)
        
        output = []

        # Installs patches to OrangeFS. Assumes patches are p1.
        print ofs_patch_files
        for patch in ofs_patch_files:
            
            print "Patching: patch -c -p1 < %s" % patch
            rc = self.runSingleCommand("patch -c -p1 < %s" % patch)
            if rc != 0:
                print "Patch Failed!"
       
        # Run prepare. 
        rc = self.runSingleCommand("./prepare",output)
        if rc != 0:
            print self.ofs_source_location+"/prepare failed!" 
            print output
            return rc
        
        #sanity check for OFS installation prefix
        rc = self.runSingleCommand("mkdir -p "+ofs_prefix)
        if rc != 0:
            print "Could not create directory "+ofs_prefix
            ofs_prefix = "/home/%s/orangefs" % self.current_user
            print "Using default %s" % ofs_prefix
        else:
            self.runSingleCommand("rmdir "+ofs_prefix)
            
        
        # get the kernel version if it has been updated
        self.kernel_version = self.runSingleCommandBacktick("uname -r")
        
        self.kernel_source_location = "/lib/modules/%s" % self.kernel_version
        
        # Will always need prefix and db4 location.
        configure_opts = configure_opts+" --prefix=%s --with-db=%s" % (ofs_prefix,db4_prefix)
        
        # Add various options to the configure
         
        if build_kmod == True:
            
            if "suse" in self.distro.lower():
                # SuSE puts kernel source in a different location.
                configure_opts = "%s --with-kernel=%s/source" % (configure_opts,self.kernel_source_location)
            else:
                configure_opts = "%s --with-kernel=%s/build" % (configure_opts,self.kernel_source_location)
        
        if enable_strict == True:
            # should check gcc version, but am too lazy for that. Will work on gcc > 4.4
            # gcc_ver = self.runSingleCommandBacktick("gcc -v 2>&1 | grep gcc | awk {'print \$3'}")
            
            # won't work for rhel 5 based distros, gcc is too old.
            if ("centos" in self.distro.lower() or "scientific linux" in self.distro.lower() or "red hat" in self.distro.lower()) and " 5." in self.distro:
                pass
            else:
                configure_opts = configure_opts+" --enable-strict"

        if enable_hadoop == True:
            configure_opts =  configure_opts + " --with-jdk=%s --enable-hadoop --with-hadoop=%s --enable-jni " % (self.jdk6_location,self.hadoop_location)
            enable_shared = True


        if enable_shared == True:
            configure_opts = configure_opts+" --enable-shared"

        if enable_fuse == True:
            configure_opts = configure_opts+" --enable-fuse"
        

        
        if security_mode == None:
            # no security mode, ignore
            # must come first to prevent exception
            pass
        elif security_mode.lower() == "key":
            configure_opts = configure_opts+" --enable-security-key"
        elif security_mode.lower() == "cert":
            configure_opts = configure_opts+" --enable-security-cert"
        
        rc = self.runSingleCommand("./configure %s" % configure_opts, output)
        
        # did configure run correctly?
        if rc == 0:
            # set the OrangeFS installation location to the prefix.
            self.ofs_installation_location = ofs_prefix
        else:
            print "Configuration of OrangeFS at %s Failed!" % self.ofs_source_location
            print output
            

        return rc
    
    ##
    # @fn checkMount(self,mount_point=None,output=[]):
    #
    # This looks to see if a given mount_point is mounted.
    #
    # @param self The object pointer
    # @param mount_point Mount point to check
    # @param output Output list
    #
    # @return Is 0 - mounted
    # @return Is not 0 - not mounted
    #


        
    def checkMount(self,mount_point=None,output=[]):
        if mount_point == None:
            mount_point = self.ofs_mount_point
        mount_check = self.runSingleCommand("mount | grep %s" % mount_point,output)
        '''    
        if mount_check == 0:
            print "OrangeFS mount found: "+output[1]
        else:
            print "OrangeFS mount not found!"
            print output
        '''
        return mount_check
    
    ##
    # @fn def getAliasesFromConfigFile(self,config_file_name):
    #
    # Reads the OrangeFS alias from the configuration file. 
    # Implimented in child classes.
    #
    #
    # @param self The object pointer
    # @param config_file_name Full path to the configuration file. (Usually orangefs.conf)
    #
    # @return list of alias names 
        
    def getAliasesFromConfigFile(self,config_file_name):
        pass
        
        
    ##
    # @fn makeOFSSource(self,make_options=""):
    # This makes the OrangeFS source
    #
    # @param self The object pointer
    # @param make_options Addtional make options
    
    
    
    def makeOFSSource(self,make_options=""):
        # Change directory to source location.
        self.changeDirectory(self.ofs_source_location)
        output = []
        # Clean up first.
        rc = self.runSingleCommand("make clean")
        # Make
        rc = self.runSingleCommand("make "+make_options, output)
        if rc != 0:
            print "Build (make) of of OrangeFS at %s Failed!" % self.ofs_source_location
            print output
            return rc
        # Make the kernel module
        if self.build_kmod == True:
            rc = self.runSingleCommand("make kmod",output)
            if rc != 0:
                print "Build (make) of of OrangeFS-kmod at %s Failed!" % self.ofs_source_location
                print output
            
        return rc
    
    ##
    # @fn getKernelVersion(self):
    #
    # wrapper for uname -r
    # @param self The object pointer




    def getKernelVersion(self):
        #if self.kernel_version != "":
        #  return self.kernel_version
        return self.runSingleCommand("uname -r")

    ##
    # @fn installOFSSource(self,install_options="",install_as_root=False):
    #
    # This looks to see if a given mount_point is mounted
    # @param self The object pointer
    # @param install_options Addtional install options
    # @param install_as_root Install OFS as root?

      
    def installOFSSource(self,install_options="",install_as_root=False):
        self.changeDirectory(self.ofs_source_location)
        output = []
        if install_as_root == True:
            rc = self.runSingleCommandAsBatch("sudo make install",output)
        else:
            rc = self.runSingleCommand("make install",output)
        
        if rc != 0:
            
            print "Could not install OrangeFS from %s to %s" % (self.ofs_source_location,self.ofs_installation_location)
            print output
            return rc
        if self.build_kmod == True:
            self.runSingleCommand("make kmod_install kmod_prefix=%s" % self.ofs_installation_location,output)
            if rc != 0:
                print "Could not install OrangeFS from %s to %s" % (self.ofs_source_location,self.ofs_installation_location)
                print output
        
        return rc

    ##
    # @fn installOFSTests(self,configure_options=""):
    #
    # This installs the OrangeFS test programs in the OFS source tree
    # @param self The object pointer
    # @param configure_options Addtional configure options
    

    def installOFSTests(self,configure_options=""):
        
        output = []
        
        if configure_options == "":
            configure_options = "--with-db=%s --prefix=%s" % (self.db4_dir,self.ofs_installation_location)
        
 
        
        self.changeDirectory("%s/test" % self.ofs_source_location)
        rc = self.runSingleCommand("./configure %s"% configure_options)
        if rc != 0:
            print "Could not configure OrangeFS tests"
            print output
            return rc
        
        rc = self.runSingleCommand("make all")
        if rc != 0:
            print "Could not build (make) OrangeFS tests"
            print output
            return rc
   
        rc = self.runSingleCommand("make install")
        if rc != 0:
            print "Could not install OrangeFS tests"
            print output
        return rc
    
    ##
    # @fn exportNFSDirectory(self,directory_name,options=None,network=None,netmask=None):
    #
    # this exports directory directory_name via NFS.
    # @param self The object pointer
    # @param directory_name Directory to export
    # @param options NFS options
    # @param network Network on which to export
    # @param netmask Netmask for network

    def exportNFSDirectory(self,directory_name,options=None,network=None,netmask=None):
        if options == None:
            options = "rw,sync,no_root_squash,no_subtree_check"
        if network == None:
            network = self.ip_address
        if netmask == None:
            netmask = 24
        
        
        self.runSingleCommand("mkdir -p %s" % directory_name)
        if "suse" in self.distro.lower():
            commands = '''
            sudo bash -c 'echo "%s %s/%r(%s)" >> /etc/exports'
            sudo /sbin/rpcbind 
            sleep 3
            sudo /etc/init.d/nfs restart
            sudo /etc/init.d/nfsserver restart
            sudo exportfs -a
            ''' % (directory_name,self.ip_address,netmask,options)
        else:
            commands = '''
            sudo bash -c 'echo "%s %s/%r(%s)" >> /etc/exports'
            #sudo service cups stop
            #sudo service sendmail stop
            sudo service rpcbind restart
            sudo service nfs restart
            sudo service nfs-kernel-server restart
            sudo exportfs -a
            ''' % (directory_name,self.ip_address,netmask,options)
        
        
        
        self.runSingleCommandAsBatch(commands)
        time.sleep(30)
        
        return "%s:%s" % (self.ip_address,directory_name)
    
    ##
    # @fn mountNFSDirectory(self,nfs_share,mount_point,options=""):
    #
    # this mounts nfs_share at mount_point with options.
    # @param self The object pointer
    # @param nfs_share NFS share to mount
    # @param mount_point NFS mount_point on machine
    # @param options NFS mount options

        
    def mountNFSDirectory(self,nfs_share,mount_point,options=""):
        self.changeDirectory("/home/%s" % self.current_user)
        self.runSingleCommand("mkdir -p %s" % mount_point)
        commands = 'sudo mount -t nfs -o %s %s %s' % (options,nfs_share,mount_point)
        print commands
        self.runSingleCommandAsBatch(commands)
        output = []
        rc = self.runSingleCommand("mount | grep %s" % nfs_share,output)
        count = 0
        while rc != 0 and count < 10 :
            time.sleep(15)
            self.runSingleCommandAsBatch(commands)
            rc = self.runSingleCommand("mount | grep %s" % nfs_share,output)
            print output
            count = count + 1
        return 0
    
    ##
    # @fn clearSHM(self):
    #   
    #  This clears out all SHM objects for OrangeFS.
    # @param self The object pointer

    def clearSHM(self):
        self.runSingleCommandAsBatch("sudo rm /dev/shm/pvfs*")
   
   

        #============================================================================
        #
        # OFSServerFunctions
        #
        # These functions implement functionality for an OrangeFS server
        #
        #=============================================================================

    ##
    # @fn copyOFSInstallationToNode(self,destination_node):
    #
    # This copies an entire OrangeFS installation from the current node to destination_node.
    # Also sets the ofs_installation_location and ofs_branch on the destination
    # @param self The object pointer
    # @param destination_node OFSTestNode to which the installation is copied.



    def copyOFSInstallationToNode(self,destination_node):
        rc = self.copyToRemoteNode(self.ofs_installation_location+"/", destination_node, self.ofs_installation_location, True)
        destination_node.ofs_installation_location = self.ofs_installation_location
        destination_node.ofs_branch =self.ofs_branch
        # TODO: Copy ofs_conf_file, don't just link
        #rc = self.copyToRemoteNode(self.ofs_conf_file+"/", destination_node, self.ofs_conf_file, True)
        destination_node.ofs_conf_file =self.ofs_conf_file
        destination_node.ofs_fs_name = destination_node.runSingleCommandBacktick("grep Name %s | awk '{print \\$2}'" % destination_node.ofs_conf_file)
        return rc
       

    ##
    # @fn configureOFSServer(self,ofs_hosts_v,ofs_fs_name,configuration_options="",ofs_source_location="",ofs_storage_location="",ofs_conf_file=None,security=None):
    #
    # This function runs the configuration programs and puts the result in self.ofs_installation_location/etc/orangefs.conf 
    # @param self The object pointer
    # @param ofs_hosts_v List of OFS hosts
    # @param ofs_fs_name OrangeFS filesystem name in url
    # @param configuration_options Additional configuration options
    # @param ofs_source_location Location of OrangeFS source
    # @param ofs_storage_location Location of OrangeFS storage
    # @param ofs_conf_file Configuration file name. Default is [OFS location]/etc/orangefs.conf
    # @param security OFS security level None,"Key","Cert"

      
    
       
    def configureOFSServer(self,ofs_hosts_v,ofs_fs_name,configuration_options="",ofs_source_location="",ofs_storage_location="",ofs_conf_file=None,security=None):
        
            
        self.ofs_fs_name=ofs_fs_name
        
        self.changeDirectory(self.ofs_installation_location)
        
        if ofs_storage_location == "":
            self.ofs_storage_location  = self.ofs_installation_location + "/data"
        else:
            self.ofs_storage_location = self.ofs_storage_location
           
        # ofs_hosts is a list of ofs hosts separated by white space.
        ofs_host_str = ""
        
        # Add each ofs host to the string of hosts.
        for ofs_host in ofs_hosts_v:
            ofs_host_str = ofs_host_str + ofs_host.hostname + ":3396,"
        
        #strip the trailing comma
        ofs_host_str = ofs_host_str.rstrip(',')

        #implement the following command
        '''
        INSTALL-pvfs2-${CVS_TAG}/bin/pvfs2-genconfig fs.conf \
            --protocol tcp \
            --iospec="${MY_VFS_HOSTS}:3396" \
            --metaspec="${MY_VFS_HOSTS}:3396"  \
            --storage ${PVFS2_DEST}/STORAGE-pvfs2-${CVS_TAG} \
            $sec_args \
            --logfile=${PVFS2_DEST}/pvfs2-server-${CVS_TAG}.log --quiet
        ''' 
      
        security_args = ""
        if security == None:
            pass
        elif security.lower() == "key":
            print "Configuring key based security"
            security_args = "--securitykey --serverkey=%s/etc/orangefs-serverkey.pem --keystore=%s/etc/orangefs-keystore" % (self.ofs_installation_location,self.ofs_installation_location)
        elif security.lower() == "cert":
            print "Certificate based security not yet supported by OFSTest."
            pass
            
        self.runSingleCommand("mkdir -p %s/etc" % self.ofs_installation_location)
        if configuration_options == "":
            genconfig_str="%s/bin/pvfs2-genconfig %s/etc/orangefs.conf --protocol tcp --iospec=\"%s\" --metaspec=\"%s\" --storage=%s %s --logfile=%s/pvfs2-server-%s.log --quiet" % (self.ofs_installation_location,self.ofs_installation_location,ofs_host_str,ofs_host_str,self.ofs_storage_location,security_args,self.ofs_installation_location,self.ofs_branch)
        else:
            genconfig_str="%s/bin/pvfs2-genconfig %s/etc/orangefs.conf %s --quiet" % (self.ofs_installation_location,self.ofs_installation_location,configuration_options)
        
        print "Generating orangefs.conf "+ genconfig_str
        # run genconfig
        output = []
        rc = self.runSingleCommand(genconfig_str,output)
        if rc != 0:
            print "Could not generate orangefs.conf file."
            print output
            return rc
        
        # do we need to copy the file to a new location?
        if ofs_conf_file == None:
            self.ofs_conf_file = self.ofs_installation_location+"/etc/orangefs.conf"
        else:
            rc = self.copyLocal(self.ofs_installation_location+"/etc/orangefs.conf",ofs_conf_file,False)
            if rc != 0:
                print "Could not copy orangefs.conf file to %s. Using %s/etc/orangefs.conf" % (ofs_conf_file,self.ofs_installation_location)
                self.ofs_conf_file = self.ofs_installation_location+"/etc/orangefs.conf"
            else:
                self.ofs_conf_file = ofs_conf_file
        
        # Now set the fs name
        self.ofs_fs_name = self.runSingleCommandBacktick("grep Name %s | awk '{print \\$2}'" % self.ofs_conf_file)
        
        return rc
    ##
    # @fn startOFSServer(self,run_as_root=False):
    #
    # This function starts the orangefs server
    # @param self The object pointer
    # @param run_as_root Run as root user

        
      
    def startOFSServer(self,run_as_root=False):
        
        output = []
        self.changeDirectory(self.ofs_installation_location)
        #print self.runSingleCommand("pwd")
        # initialize the storage
        
        '''
        Change the following shell command to python
        
        for alias in `grep 'Alias ' fs.conf | grep ${HOSTNAME} | cut -d ' ' -f 2`; do
            ${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/sbin/pvfs2-server \
                -p `pwd`/pvfs2-server-${alias}.pid \
                -f fs.conf -a $alias
            ${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/sbin/pvfs2-server \
                -p `pwd`/pvfs2-server-${alias}.pid  \
                fs.conf $server_conf -a $alias
        '''
        
        
        print "Attempting to start OFSServer for host %s" % self.hostname
        

        # need to get the alias list from orangefs.conf file
        if self.alias_list == None:
            self.alias_list = self.getAliasesFromConfigFile(self.ofs_conf_file)
        
        if len(self.alias_list) == 0:
            print "Could not find any aliases in %s/etc/orangefs.conf" % self.ofs_installation_location
            return -1

        # for all the aliases in the file
        for alias in self.alias_list:
            # if the alias is for THIS host
            if self.hostname in alias:
                
                # create storage space for the server
                rc = self.runSingleCommand("%s/sbin/pvfs2-server -p %s/pvfs2-server-%s.pid -f %s/etc/orangefs.conf -a %s" % ( self.ofs_installation_location,self.ofs_installation_location,self.hostname,self.ofs_installation_location,alias),output)
                if rc != 0:
                    # If storage space is already there, creating it will fail. Try deleting and recreating.
                    rc = self.runSingleCommand("%s/sbin/pvfs2-server -p %s/pvfs2-server-%s.pid -r %s/etc/orangefs.conf -a %s" % ( self.ofs_installation_location,self.ofs_installation_location,self.hostname,self.ofs_installation_location,alias),output)
                    rc = self.runSingleCommand("%s/sbin/pvfs2-server -p %s/pvfs2-server-%s.pid -f %s/etc/orangefs.conf -a %s" % ( self.ofs_installation_location,self.ofs_installation_location,self.hostname,self.ofs_installation_location,alias),output)
                    if rc != 0:
                        print "Could not create OrangeFS storage space"
                        print output
                        return rc
              
                
                # Are we running this as root? 
                prefix = "" 
                if run_as_root == True:
                    prefix = "sudo LD_LIBRARY_PATH=%s:%s/lib" % (self.db4_lib_dir,self.ofs_installation_location)
                    
                    
                server_start = "%s %s/sbin/pvfs2-server -p %s/pvfs2-server-%s.pid %s/etc/orangefs.conf -a %s" % (prefix,self.ofs_installation_location,self.ofs_installation_location,self.hostname,self.ofs_installation_location,alias)
                print server_start
                rc = self.runSingleCommandAsBatch(server_start,output)
                
                # give the servers 15 seconds to get running
                print "Starting OrangeFS servers..."
                time.sleep(15)

        #Now set up the pvfs2tab_file
        self.ofs_mount_point = "/tmp/mount/orangefs"
        self.runSingleCommand("mkdir -p "+ self.ofs_mount_point)
        self.runSingleCommand("mkdir -p %s/etc" % self.ofs_installation_location)
        self.runSingleCommand("echo \"tcp://%s:3396/%s %s pvfs2 defaults 0 0\" > %s/etc/orangefstab" % (self.hostname,self.ofs_fs_name,self.ofs_mount_point,self.ofs_installation_location))
        self.runSingleCommandAsBatch("sudo ln -s %s/etc/orangefstab /etc/pvfs2tab" % self.ofs_installation_location)
        self.setEnvironmentVariable("PVFS2TAB_FILE",self.ofs_installation_location + "/etc/orangefstab")
       
        # set the debug mask
        self.runSingleCommand("%s/bin/pvfs2-set-debugmask -m %s \"all\"" % (self.ofs_installation_location,self.ofs_mount_point))
       
        return 0
    
    ##
    # @fn stopOFSServer(self):
    #
    # This function stops the OrangeFS servers.
    # @param self The object pointer
    #
    #-------------------------------
        
    def stopOFSServer(self):
        # Kill'em all and let root sort 'em out.        
        # TODO: Install killall on SuSE based systems. 
        self.runSingleCommand("killall -s 9 pvfs2-server")
        
        
    #============================================================================ 
    #
    # OFSClientFunctions
    #
    # These functions implement functionality for an OrangeFS client
    #
    #=============================================================================
    
    ##
    # @fn installKernelModule(self):
    #
    # This function inserts the kernel module into the kernel
    # @param self The object pointer
    #


    def installKernelModule(self):
        
        # Installing Kernel Module is a root task, therefore, it must be done via batch.
        # The following shell commands are implemented in Python:
        '''
        sudo /sbin/insmod ${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/lib/modules/`uname -r`/kernel/fs/pvfs2/pvfs2.ko &> pvfs2-kernel-module.log
        sudo /sbin/lsmod >> pvfs2-kernel-module.log
        '''
        # first check to see if the kernel module is already installed.
        rc = self.runSingleCommand('/sbin/lsmod | grep pvfs2')
        if rc == 0:
            return 0
        self.addBatchCommand("sudo /sbin/insmod %s/lib/modules/%s/kernel/fs/pvfs2/pvfs2.ko &> pvfs2-kernel-module.log" % (self.ofs_installation_location,self.kernel_version))
        self.addBatchCommand("sudo /sbin/lsmod >> pvfs2-kernel-module.log")
        self.runAllBatchCommands()
        return 0
        
     
    ##
    # @fn startOFSClient(self,security=None):
    #
    # This function starts the orangefs client
    # @param self The object pointer
    # @param security OFS security level None,"Key","Cert"

    def startOFSClient(self,security=None):
        # Starting the OFS Client is a root task, therefore, it must be done via batch.
        # The following shell command is implimented in Python
        '''
            keypath=""
        if [ $ENABLE_SECURITY ] ; then
            keypath="--keypath ${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/etc/clientkey.pem"
        fi
        sudo ${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/sbin/pvfs2-client \
            -p ${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/sbin/pvfs2-client-core \
            -L ${PVFS2_DEST}/pvfs2-client-${CVS_TAG}.log \
            $keypath
        sudo chmod 644 ${PVFS2_DEST}/pvfs2-client-${CVS_TAG}.logfile
        '''
        
        # if the client is already running, return.
        rc = self.runSingleCommand("/bin/ps -f --no-heading -u root | grep pvfs2-client")
        if rc == 0:
            return 0
        
        # Clear the shared memory objects
        self.clearSHM()
        
        # TODO: Add cert-based security.
        keypath = ""
        if security==None:
            pass
        elif security.lower() == "key":
            keypath = "--keypath=%s/etc/pvfs2-clientkey.pem" % self.ofs_installation_location
        elif security.lower() == "cert":
            pass

        
        print "Starting pvfs2-client: "
        print "sudo LD_LIBRARY_PATH=%s:%s/lib PVFS2TAB_FILE=%s/etc/orangefstab  %s/sbin/pvfs2-client -p %s/sbin/pvfs2-client-core -L %s/pvfs2-client-%s.log %s" % (self.db4_lib_dir,self.ofs_installation_location,self.ofs_installation_location,self.ofs_installation_location,self.ofs_installation_location,self.ofs_installation_location,self.ofs_branch,keypath)
        print ""
        
        # start the client 
        self.addBatchCommand("sudo LD_LIBRARY_PATH=%s:%s/lib PVFS2TAB_FILE=%s/etc/orangefstab  %s/sbin/pvfs2-client -p %s/sbin/pvfs2-client-core -L %s/pvfs2-client-%s.log %s" % (self.db4_lib_dir,self.ofs_installation_location,self.ofs_installation_location,self.ofs_installation_location,self.ofs_installation_location,self.ofs_installation_location,self.ofs_branch,keypath))
        # change the protection on the logfile to 644
        self.addBatchCommand("sudo chmod 644 %s/pvfs2-client-%s.log" % (self.ofs_installation_location,self.ofs_branch))
        self.runAllBatchCommands()

        return 0
        
    ##
    # @fn mountOFSFilesystem(self,mount_fuse=False,mount_point=None):
    #
    # This function mounts OrangeFS via kernel module or fuse
    # @param self The object pointer
    # @param mount_fuse Mount with fuse module?
    # @param mount_point OFS Mountpoint. Default is /tmp/mount/orangefs

      
    def mountOFSFilesystem(self,mount_fuse=False,mount_point=None):
        # Mounting the OFS Filesystem is a root task, therefore, it must be done via batch.
        # The following shell command is implimented in Python
        '''
            echo "Mounting pvfs2 service at tcp://${HOSTNAME}:3396/pvfs2-fs at mount_point $PVFS2_MOUNTPOINT"
        sudo mount -t pvfs2 tcp://${HOSTNAME}:3396/pvfs2-fs ${PVFS2_MOUNTPOINT}
        
        
        if [ $? -ne 0 ]
        then
            echo "Something has gone wrong. Mount failed."
        fi
        mount > allmount.log
        '''
        output = []
        
        # is the filesystem already mounted?
        rc = self.checkMount(output)
        if rc == 0:
            print "OrangeFS already mounted at %s" % output[1]
            return
        
        # where is this to be mounted?
        if mount_point != None:
            self.ofs_mount_point = mount_point
        elif self.ofs_mount_point == "":
            self.ofs_mount_point = "/tmp/mount/orangefs"

        # create the mount_point directory    
        self.runSingleCommand("mkdir -p %s" % self.ofs_mount_point)
        
        # mount with fuse
        if mount_fuse == True:
            print "Mounting OrangeFS service at tcp://%s:%s/%s at mount_point %s via fuse" % (self.hostname,self.ofs_tcp_port,self.ofs_fs_name,self.ofs_mount_point)
            self.runSingleCommand("%s/bin/pvfs2fuse %s -o fs_spec=tcp://%s:%s/%s -o nonempty" % (self.ofs_installation_location,self.ofs_mount_point,self.hostname,self.ofs_tcp_port,self.ofs_fs_name),output)
            #print output
            
        #mount with kmod
        else:
            print "Mounting OrangeFS service at tcp://%s:%s/%s at mount_point %s" % (self.hostname,self.ofs_tcp_port,self.ofs_fs_name,self.ofs_mount_point)
            self.addBatchCommand("sudo mount -t pvfs2 tcp://%s:%s/%s %s" % (self.hostname,self.ofs_tcp_port,self.ofs_fs_name,self.ofs_mount_point))
            self.runAllBatchCommands()
        
        print "Waiting 30 seconds for mount"            
        time.sleep(30)

    ##
    # @fn unmountOFSFilesystem(self):
    #
    # This function unmounts OrangeFS. Works for both kmod and fuse
    # @param self The object pointer
    #

    
    def unmountOFSFilesystem(self):
        print "Unmounting OrangeFS mounted at " + self.ofs_mount_point
        self.runSingleCommandAsBatch("sudo umount -f -l %s" % self.ofs_mount_point)
        time.sleep(15)

    ##
    # @fn stopOFSClient(self):
    #
    # This function stops the orangefs client and unmounts the filesystem
    # @param self The object pointer
    #
    

    def stopOFSClient(self):
        
        # Unmount the filesystem.
        self.unmountOFSFilesystem()
        print "Stopping pvfs2-client process"
        self.addBatchCommand("sudo killall pvfs2-client")
        self.addBatchCommand("sleep 10")
        self.addBatchCommand("sudo killall -s 9 pvfs2-client")
        self.addBatchCommand("sleep 2")
        self.runAllBatchCommands()
        
    
 
    ##
    # @fn findExistingOFSInstallation(self):
    #
    # This function finds an existing OrangeFS installation on the node
    # @param self The object pointer
    #

        

    def findExistingOFSInstallation(self):
        # to find OrangeFS server, first finr the pvfs2-server file
        #ps -ef | grep -v grep| grep pvfs2-server | awk {'print $8'}
        output = []
        pvfs2_server = self.runSingleCommandBacktick("ps -f --no-heading -C pvfs2-server | awk '{print \\$8}'")
        
        # We have <OFS installation>/sbin/pvfs2_server. Get what we want.
        (self.ofs_installation_location,sbin) = os.path.split(os.path.dirname(pvfs2_server))
        
        # to find OrangeFS conf file
        #ps -ef | grep -v grep| grep pvfs2-server | awk {'print $11'}
        self.ofs_conf_file = self.runSingleCommandBacktick("ps -f --no-heading -C pvfs2-server | awk '{print \\$11}'")
        
        # to find url
        
        rc = self.runSingleCommandBacktick("ps -f --no-heading -C pvfs2-server | awk '{print \\$13}'",output)
        #print output
        alias = output[1].rstrip()
        
        rc = self.runSingleCommandBacktick("grep %s %s | grep tcp: | awk '{print \\$3}'" % (alias,self.ofs_conf_file),output )
        #print output
        url_base = output[1].rstrip()
        
        self.ofs_fs_name = self.runSingleCommandBacktick("grep Name %s | awk '{print \\$2}'" % self.ofs_conf_file)
        
        # to find mount point
        # should be better than this.


        rc = self.runSingleCommand("mount | grep pvfs2 | awk '{ print \\$2}'",output)
        if rc != 0:
            print "OrangeFS mount point not detected. Trying /tmp/mount/orangefs."
            self.ofs_mount_point = "/tmp/mount/orangefs"
        else: 
            self.ofs_mount_point = output[1].rstrip()
        
        # to find PVFS2TAB_FILE
        print "Looking for PVFS2TAB_FILE"
        rc = self.runSingleCommand("grep -l -r '%s/%s\s%s' %s 2> /dev/null" %(url_base,self.ofs_fs_name,self.ofs_mount_point,self.ofs_installation_location),output)
        if rc != 0:
            rc = self.runSingleCommand("grep -l -r '%s/%s\s%s' /etc 2> /dev/null" % (url_base,self.ofs_fs_name,self.ofs_mount_point),output)
        
        
        
        if rc == 0:
            #print output
            self.setEnvironmentVariable("PVFS2TAB_FILE",output[1].rstrip())
        
        # to find source
        # find the directory
        #find / -name pvfs2-config.h.in -print 2> /dev/null
        # grep directory/configure 
        # grep -r 'prefix = /home/cloud-user/orangefs' /home/cloud-user/stable/Makefile
        return 0
        
        


        
    
# #===================================================================================================
# # Unit test script begins here
# #===================================================================================================
# def test_driver():
#     local_machine = OFSTestLocalNode()
#     local_machine.addRemoteKey('10.20.102.54',"/home/jburton/buildbot.pem")
#     local_machine.addRemoteKey('10.20.102.60',"/home/jburton/buildbot.pem")
#     
#     '''
#     local_machine.changeDirectory("/tmp")
#     local_machine.setEnvironmentVariable("FOO","BAR")
#     local_machine.runSingleCommand("echo $FOO")
#     local_machine.addBatchCommand("echo \"This is a test of the batch command system\"")
#     local_machine.addBatchCommand("echo \"Current directory is `pwd`\"")
#     local_machine.addBatchCommand("echo \"Variable foo is $FOO\"")
#     local_machine.runAllBatchCommands()
#     
# 
#     
#     #local_machine.copyOFSSource("LOCALDIR","/home/jburton/testingjdb/","/tmp/jburton/testingjdb/")
#     #local_machine.configureOFSSource()
#     #local_machine.makeOFSSource()
#     #local_machine.installOFSSource()
#     '''
#     
#     remote_machine = OFSTestRemoteNode('cloud-user','10.20.102.54',"/home/jburton/buildbot.pem",local_machine)
#     remote_machine1 = OFSTestRemoteNode('cloud-user','10.20.102.60', "/home/jburton/buildbot.pem",local_machine)
# 
# '''
#     remote_machine.setEnvironmentVariable("LD_LIBRARY_PATH","/opt/db4/lib")
#     remote_machine1.setEnvironmentVariable("LD_LIBRARY_PATH","/opt/db4/lib")
# 
#    # remote_machine.uploadNodeKeyFromLocal(local_machine)
#    # remote_machine1.uploadNodeKeyFromLocal(local_machine)
#     remote_machine.uploadRemoteKeyFromLocal(local_machine,remote_machine1.ip_address)
#     remote_machine1.uploadRemoteKeyFromLocal(local_machine,remote_machine.ip_address)
#     
#     remote_machine.copyOFSSource("SVN","http://orangefs.org/svn/orangefs/trunk","/tmp/cloud-user/")
#     print "Configuring remote source"
#     remote_machine.configureOFSSource()
#     remote_machine.makeOFSSource()
#     remote_machine.installOFSSource()
#     
#     #remote_machine1.runSingleCommandAsBatch("sudo rm /tmp/mount/orangefs/touched")
#     #remote_machine1.copyOFSSource("TAR","http://www.orangefs.org/downloads/LATEST/source/orangefs-2.8.7.tar.gz","/tmp/cloud-user/")
# 
# 
#     print ""
#     print "-------------------------------------------------------------------------"
#     print "Configuring remote source without shared libraries on " + remote_machine.hostname
#     print ""
#     remote_machine.runSingleCommand("rm -rf /tmp/cloud-user")
#     remote_machine.runSingleCommand("rm -rf /tmp/orangefs")
#     remote_machine1.installBenchmarks("http://devorange.clemson.edu/pvfs/benchmarks-20121017.tar.gz","/tmp/cloud-user/benchmarks")
#     remote_machine.copyOFSSource("SVN","http://orangefs.org/svn/orangefs/branches/stable","/tmp/cloud-user/")
#     remote_machine.configureOFSSource()
#     remote_machine.makeOFSSource()
#     remote_machine.installOFSSource()
# 
#     remote_machine.configureOFSServer([remote_machine])
#     remote_machine.stopOFSServer()
#     remote_machine.startOFSServer()
#     #remote_machine.stopOFSServer()
#     print ""
#     print "Checking to see if pvfs2 server is running..."
#     remote_machine.runSingleCommand("ps aux | grep pvfs2")
#     print ""
#     print "Checking to see what is in /tmp/mount/orangefs before mount..."
#     remote_machine.runSingleCommand("ls -l /tmp/mount/orangefs")
#     remote_machine.installKernelModule()
#     remote_machine.startOFSClient()
#     remote_machine.mountOFSFilesystem()
#     print ""
#     print "Checking to see if pvfs2 client is running..."
#     remote_machine.runSingleCommand("ps aux | grep pvfs2")
#     print ""
#     print "Checking pvfs2 mount..."
#     remote_machine.runSingleCommand("mount | (grep pvfs2 || echo \"Not Mounted\")")
#     print ""
#     print "Checking to see what is in /tmp/mount/orangefs after mount..."
#     remote_machine.runSingleCommand("ls -l /tmp/mount/orangefs")
#     print ""
#     print "Checking to see if mounted FS works..."
#     remote_machine.runSingleCommandAsBatch("sudo touch /tmp/mount/orangefs/touched")
#     print ""
#     print "Checking to see what is in /tmp/mount/orangefs after touch..."
#     remote_machine.runSingleCommand("ls -l /tmp/mount/orangefs")
#     print ""
# 
#     remote_machine.stopOFSClient()
#     remote_machine.stopOFSServer()
#     print "Checking to see if all pvfs2 services have stopped."
#     remote_machine.runSingleCommand("ps aux | grep pvfs2")
#     print ""
# 
#     print ""
#     print "-------------------------------------------------------------------------"
#     print "Configuring remote source with shared libraries on " + remote_machine1.hostname
#     print ""
#     remote_machine1.runSingleCommand("rm -rf /tmp/orangefs")
#     remote_machine1.runSingleCommand("rm -rf /tmp/cloud-user")
#     remote_machine1.installBenchmarks("http://devorange.clemson.edu/pvfs/benchmarks-20121017.tar.gz","/tmp/cloud-user/")
#     remote_machine1.copyOFSSource("SVN","http://orangefs.org/svn/orangefs/branches/stable","/tmp/cloud-user/")
# 
# 
#     remote_machine1.configureOFSSource("--enable-strict --enable-shared --enable-ucache --disable-karma --with-db=/opt/db4 --prefix=/tmp/orangefs --with-kernel=%s/build" % remote_machine1.getKernelVersion())
#     #remote_machine1.configureOFSSource()
#     remote_machine1.makeOFSSource()
#     remote_machine1.installOFSSource()
# 
#     remote_machine1.configureOFSServer([remote_machine1])
#     remote_machine1.stopOFSServer()
#     remote_machine1.startOFSServer()
#     #remote_machine1.stopOFSServer()
#     print ""
#     print "Checking to see if pvfs2 server is running..."
#     remote_machine1.runSingleCommand("ps aux | grep pvfs2")
#     print ""
#     print "Checking to see what is in /tmp/mount/orangefs before mount..."
#     remote_machine1.runSingleCommand("ls -l /tmp/mount/orangefs")
#     remote_machine1.installKernelModule()
#     remote_machine1.startOFSClient()
#     remote_machine1.mountOFSFilesystem()
#     print ""
#     print "Checking to see if pvfs2 client is running..."
#     remote_machine1.runSingleCommand("ps aux | grep pvfs2")
#     print ""
#     print "Checking pvfs2 mount..."
#     remote_machine1.runSingleCommand("mount | (grep pvfs2 || echo \"Not Mounted\")")
#     print ""
#     print "Checking to see what is in /tmp/mount/orangefs after mount..."
#     remote_machine1.runSingleCommand("ls -l /tmp/mount/orangefs")
#     print ""
#     print "Checking to see if mounted FS works"
#     remote_machine1.runSingleCommandAsBatch("sudo touch /tmp/mount/orangefs/touched")
#     print ""
#     print "Checking to see what is in /tmp/mount/orangefs after touch..."
#     remote_machine1.runSingleCommand("ls -l /tmp/mount/orangefs")
# 
#     remote_machine1.stopOFSClient()
#     remote_machine1.stopOFSServer()
#     print "Checking to see if all pvfs2 services have stopped..."
#     remote_machine1.runSingleCommand("ps aux | grep pvfs2")
#     print ""
# 
# 
#     #export LD_LIBRARY_PATH=${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/lib:/opt/db4/lib
#     #export PRELOAD="LD_PRELOAD=${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/lib/libofs.so:${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/lib/libpvfs2.so
# 
#     #local_machine.copyToRemoteNode("/home/jburton/buildbot.pem",remote_machine,"~/buildbot.pem",False)
#     #remote_machine.copyToRemoteNode("~/buildbot.pem",remote_machine1,"~/buildbot.pem",False)
# 
#     
#     remote_machine.setEnvironmentVariable("FOO","BAR")
#     remote_machine.runSingleCommand("echo $FOO")
#     remote_machine.runSingleCommand("hostname -s")
#     remote_machine.addBatchCommand("echo \"This is a test of the batch command system\"")
#     remote_machine.addBatchCommand("echo \"Current directory is `pwd`\"")
#     remote_machine.addBatchCommand("echo \"Variable foo is $FOO\"")
#     remote_machine.addBatchCommand("touch /tmp/touched")
#     remote_machine.addBatchCommand("sudo apt-get update && sudo apt-get -y dist-upgrade")
#     remote_machine.addBatchCommand("sudo yum -y upgrade")
#     remote_machine.runAllBatchCommands()
#     
#    ''' 
#     
# #Call script with -t to test
# #if len(sys.argv) > 1 and sys.argv[1] == "-t":
# #    test_driver()
