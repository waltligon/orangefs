#!/usr/bin/python

###############################################################################
#
# OFSTestNode
# 
# This is the base class for all test nodes that run OrangeFS. Local and 
# remote; Client, Server, and Build nodes. 
#
# OFSTestNode is an abstract class. All nodes should be of a more specific 
# subclass type.
#
################################################################################

import os
import subprocess
import shlex
import cmd
import time
import sys
import xml.etree.ElementTree as ET
import traceback
#import scriptine.shell

###############################################################################################'
#
# class OFSTestNode(object)
#
#
# This class represents all machines that are part of the OFS process. This includes local, remote
# and remote-ec2 based machines.
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
#
################################################################################################

# global variable for batch counting
batch_count = 0

class OFSTestNode(object):
    
      # initialize node. We don't have much info for the base class.
    def __init__(self):
        
        #------
        #
        # identity variables
        #
        #-------
        
        self.ip_address = ""
        self.ext_ip_address = self.ip_address
        self.host_name = ""
        self.operating_system = ""
        self.package_system=""
        self.kernel_source_location = ""
        self.kernel_version=""
        self.is_remote=True
        self.is_ec2=False
        self.processor_type = "x86_64"
        
        #------
        #
        # shell variables
        #
        #-------
        
        self.current_user = ""
        self.current_directory = "~"
        self.current_environment = {}
        self.batch_commands = []
        
        #-------------------------------------------------------
        # sshKeys
        #
        #
        # The node key file is the file on the node that contains the key to access this machine.
        # The local key file is the location of the key on the local host. Local key file is ALWAYS on the
        # localhost.
        #
        # The keytable is a dictionary of locations of keys to remote machines on the current node.
        #
        #--------------------------------------------------------
          
        self.sshLocalKeyFile = ""
        self.sshNodeKeyFile = ""
        self.keytable = {}
        
        #----------------------------------------------------------
        #
        # orangefs related variables
        #
        #----------------------------------------------------------
       
        self.ofs_source_location = ""
        self.ofs_storage_location = ""
        self.kernel_source_location = ""
        self.ofs_installation_location = ""
        self.ofs_extra_tests_location = ""
        self.kernel_version = ""
        self.ofs_mountpoint = ""
        self.ofs_fs_name="orangefs"
        
        # svn branch (or ofs source directory name)
        self.ofs_branch = ""
        #default tcp port
        self.ofs_tcp_port = "3396"
        
            
    def currentNodeInformation(self):
        
        self.distro = ""
        #print "Getting current node information"
        
        # can we ssh in? We'll need the group if we can't, so let's try this first.
        self.current_group = self.runSingleCommandBacktick(command="ls -l /home/ | grep %s | awk {'print \\$4'}" % self.current_user)
        
        #print "Current group is "+self.current_group

        # direct access as root not good. Need to get the actual user in
        # Gross hackery for SuseStudio images. OpenStack injects into root, not user.
        if self.current_group.rstrip() == "":
            self.current_group = self.runSingleCommandBacktick(command="ls -l /home/ | grep %s | awk {'print \\$4'}" % self.current_user,remote_user="root")
            #print "Current group (from root) is "+self.current_group
            if self.current_group.rstrip() == "":
                print "Could not access node at "+self.ip_address+" via ssh"
                exit(-1)
            
            

            rc = self.runSingleCommand(command="cp -r /root/.ssh /home/%s/" % self.current_user,remote_user="root")
            if rc != 0:
                print "Could not copy ssh key from /root/.ssh to /home/%s/ " % self.current_user
                exit(rc)
            
            #get the user and group name of the home directory
            
            rc = self.runSingleCommand(command="chown -R %s:%s /home/%s/.ssh/" % (self.current_user,self.current_group,self.current_user),remote_user="root") 
            if rc != 0:
                print "Could not change ownership of /home/%s/.ssh to %s:%s" % (self.current_user,self.current_user,self.current_group)
                exit(rc)
            
        
        self.host_name = self.runSingleCommandBacktick("hostname -s")
        self.kernel_version = self.runSingleCommandBacktick("uname -r")
        self.processor_type = self.runSingleCommandBacktick("uname -p")
        
        
        # information for ubuntu and suse is in /etc/os-release
        #print self.runSingleCommand("find /etc/lsb-release 2> /dev/null")
        
        

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
            self.distro = runSingleCommandBacktick("head -n 1 /etc/SuSE-release").rstrip()
        elif self.runSingleCommandBacktick("uname").rstrip() == "Darwin":
            #print "Mac OS X based machine found"
            self.distro = "Mac OS X-%s" % self.runSingleCommandBacktick("sw_vers -productVersion")
        
        
        
        print "Node: %s %s %s %s" % (self.host_name,self.distro,self.kernel_version,self.processor_type)
        
        
       
      #==========================================================================
      # 
      # Utility functions
      #
      # These functions implement basic shell functionality 
      #
      #==========================================================================


      # Change the current directory on the node to run scripts.
    def changeDirectory(self, directory):
        self.current_directory = directory
      
      # set an environment variable to a value
    def setEnvironmentVariable(self,variable,value):
        self.current_environment[variable] = value
      
      # The setenv parameter should be a string 
    def setEnvironment(self, setenv):
        #for each line in setenv
        variable_list = setenv.split('\n')
        for variable in variable_list:
          #split based on the equals sign
          vname,value = variable.split('=')
          self.setEnvironmentVariable(vname,value)

      # Clear all environment variables
    def clearEnvironment(self):
          self.current_environment = {}
      
      # return current directory
    def printWorkingDirectory(self):
        return self.current_directory
     
      # Add a command to the list of commands being run.
      # This is a single line of a shell script.
    def addBatchCommand(self,command):
        self.batch_commands.append(command)
    
    def runSingleCommand(self,command,output=[],remote_user=None):
        
        # This runs a single command and returns the return code of that command
        # command, stdout, and stderr are in the output list
        
        #print command
        if remote_user==None:
            remote_user = self.current_user
        
        command_line = self.prepareCommandLine(command=command,remote_user=remote_user)
        
        #if "awk" in command_line:
        #    print command_line
        #print command_line
        p = subprocess.Popen(command_line,shell=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,bufsize=-1)
        
        # clear the output list, then append stdout,stderr to list to get pass-by-reference to work
        del output[:]
        output.append(command_line)
        for i in p.communicate():
            output.append(i)

        return p.returncode
        

      
    def runSingleCommandBacktick(self,command,output=[],remote_user=None):
        # This runs a single command and returns the stdout of that command.
        #print command
          #print command
        if remote_user==None:
            remote_user = self.current_user
      
        
        self.runSingleCommand(command=command,output=output,remote_user=remote_user)
        if len(output) >= 2:
            return output[1].rstrip('\n')
        else:
            return ""
    
    def runOFSTest(self,package,test_function,output=[],logfile="",errfile=""):
        # This method runs an OrangeFS test on the given node
        #
        # Output and errors are written to the output and errfiles
        # 
        # return is return code from the test function
        #
       
        print "Running test %s-%s" % (package,test_function.__name__)
        
        if logfile == "":
            logfile = "%s-%s.log" % (package,test_function.__name__)
        
                
        rc = test_function(self,output)

        try:
            logfile_h = open(logfile,"w+")
            logfile_h.write('COMMAND:' + output[0]+'\n')
            logfile_h.write('RC: %r\n' % rc)
            logfile_h.write('STDOUT:' + output[1]+'\n')
            logfile_h.write('STDERR:' + output[2]+'\n')
            
        except:
            
            traceback.print_exc()
            rc = -99
        
        logfile_h.close()
            
        
        return rc
        
        
    
    def prepareCommandLine(self,command,outfile="",append_out=False,errfile="",append_err=False,remote_user=None):
        # Implimented in the client. Should not be here.
        print "This should be implimented in the subclass, not in OFSTestNode."
        print "Trying naive attempt to create command list."
        return command
       
      # Run a single command as a batchfile. Some systems require this for passwordless sudo
    def runSingleCommandAsBatch(self,command,output=[]):
        self.addBatchCommand(command)
        self.runAllBatchCommands(output)
    
    def runBatchFile(self,filename,output=[]):
        #copy the old batch file to the batch commands list
        batch_file = open(filename,'r')
        self.batch_commands = batch_file.readlines()
        
        # Then run it
        self.runAllBatchCommands(output)

      # copy files from the current node to a destination node.
    def copyToRemoteNode(self,source, destinationNode, destination):
        pass
      
      # copy files from a remote node to the current node.
    def copyFromRemoteNode(self,sourceNode, source, destination):
        pass
      
    def writeToOutputFile(self,command_line,cmd_out,cmd_err):
        
        outfile = open("output.out","a+")
        outfile.write("bash$ "+command_line)
        outfile.write("\n")
        outfile.write("Output: "+cmd_out)
        outfile.write("Stderr: "+cmd_err)
        outfile.write("\n")
        outfile.write("\n")
        outfile.close()
      
    #---------------------
    #
    # ssh utility functions
    #
    #---------------------
    def getRemoteKeyFile(self,address):
        return self.keytable[address]
      
    def addRemoteKey(self,address,keylocation):
        #
        #This method adds the location of the key for machine at address to the keytable.
        #
        self.keytable[address] = keylocation
     
     
    def copyLocal(self, source, destination, recursive):
        # This runs the copy command remotely 
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
    
    def updateNode(self):
        #print "Distro is " + self.distro
        if "ubuntu" in self.distro.lower() or "mint" in self.distro.lower() or "debian" in self.distro.lower():
            self.addBatchCommand("sudo apt-get -y update")
            self.addBatchCommand("sudo apt-get -y dist-upgrade < /dev/zero")
        elif "suse" in self.distro.lower():
            self.addBatchCommand("sudo zypper --non-interactive update")
        elif "centos" in self.distro.lower() or "scientific linux" in self.distro.lower() or "red hat" in self.distro.lower() or "fedora" in self.distro.lower():
            self.addBatchCommand("sudo yum update --disableexcludes=main -y")
            # Uninstall the old kernel
            self.addBatchCommand("sudo rpm -e kernel-`uname -r`")
            #Update grub from current kernel to installed kernel
            self.addBatchCommand('sudo perl -e "s/`uname -r`/`rpm -q --queryformat \'%{VERSION}-%{RELEASE}.%{ARCH}\n\' kernel`/g" -p -i /boot/grub/grub.conf')
        
        self.runAllBatchCommands()
        print "Node "+self.host_name+" at "+self.ip_address+" updated. Rebooting."
        self.runSingleCommandAsBatch("sudo /sbin/reboot")
    
    def installTorqueServer(self):
        if "ubuntu" in self.distro.lower() or "mint" in self.distro.lower() or "debian" in self.distro.lower():
            batch_commands = '''

                #install torque
                echo "Installing TORQUE from apt-get"
                sudo apt-get install -y -q torque-server torque-scheduler torque-client torque-mom < /dev/null 
                sudo bash -c "echo %s > /etc/torque/server_name"
                sudo bash -c "echo %s > /var/spool/torque/server_name"
            ''' % (self.host_name,self.host_name)
            self.addBatchCommand(batch_commands)

        elif "suse" in self.distro.lower():
            
            
            print "TODO: Torque for "+self.distro
            return

            batch_commands = '''
            

            echo "Installing TORQUE from devorange: "
            echo "wget -r -np -nd http://devorange.clemson.edu/pvfs/${SYSTEM}/RPMS/${ARCH}/"
            wget -r -np -nd http://devorange.clemson.edu/pvfs/${SYSTEM}/RPMS/${ARCH}/
            #cd  devorange.clemson.edu/pvfs/openSUSE-12.2/RPMS/x86_64
            ls *.rpm
            sudo rpm -e libtorque2
            sudo rpm -ivh *.rpm
            cd -
            '''
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

                ''' % (self.processor_type,self.host_name,self.host_name)
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

                ''' % (self.processor_type,self.host_name,self.host_name)
                
            self.addBatchCommand(batch_commands)
        
        self.runAllBatchCommands()
     
    def installTorqueClient(self,pbsserver):
        pbsserver_name = pbsserver.host_name
        print "Installing Torque Client for "+self.distro.lower()
        if "ubuntu" in self.distro.lower() or "mint" in self.distro.lower() or "debian" in self.distro.lower():
            batch_commands = '''

                #install torque
                echo "Installing TORQUE from apt-get"
                sudo apt-get install -y -q torque-client torque-mom < /dev/null 
                sudo bash -c 'echo \$pbsserver %s > /var/spool/torque/mom_priv/config' 
                sudo bash -c 'echo \$logevent 255 >> /var/spool/torque/mom_priv/config' 
            ''' % pbsserver_name

            self.addBatchCommand(batch_commands)

        elif "suse" in self.distro.lower():
            # this needs to be fixed
            print "TODO: Torque for "+self.distro
            return

            batch_commands = '''
            

            echo "Installing TORQUE from devorange: "
            echo "wget -r -np -nd http://devorange.clemson.edu/pvfs/${SYSTEM}/RPMS/${ARCH}/"
            wget -r -np -nd http://devorange.clemson.edu/pvfs/${SYSTEM}/RPMS/${ARCH}/
            #cd  devorange.clemson.edu/pvfs/openSUSE-12.2/RPMS/x86_64
            ls *.rpm
            sudo rpm -e libtorque2
            sudo rpm -ivh *.rpm
            cd -
            sudo bash -c 'echo $pbsserver %s > /var/spool/torque/mom_priv/config'
            sudo bash -c 'echo $logevent 255 >> /var/spool/torque/mom_priv/config' 
            ''' % pbsserver_name
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
                ''' % (self.processor_type,pbsserver_name)
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
                sudo /etc/init.d/munge start
                ''' % (self.processor_type,pbsserver_name)
            self.addBatchCommand(batch_commands)   
        self.runAllBatchCommands()
            
    def restartTorqueServer(self):
        if "ubuntu" in self.distro.lower() or "mint" in self.distro.lower() or "debian" in self.distro.lower():
            self.runSingleCommandAsBatch("sudo /etc/init.d/torque-server restart")
            self.runSingleCommandAsBatch("sudo /etc/init.d/torque-scheduler restart")
        elif "centos" in self.distro.lower() or "scientific linux" in self.distro.lower() or "red hat" in self.distro.lower() or "fedora" in self.distro.lower():
            self.runSingleCommandAsBatch("sudo /etc/init.d/munge stop")
            self.runSingleCommandAsBatch("sudo /etc/init.d/munge start")            
            self.runSingleCommandAsBatch("sudo /etc/init.d/pbs_server stop")
            self.runSingleCommandAsBatch("sudo /etc/init.d/pbs_server start")
            self.runSingleCommandAsBatch("sudo /etc/init.d/pbs_sched stop")
            self.runSingleCommandAsBatch("sudo /etc/init.d/pbs_sched start")
            

        #self.runAllBatchCommands()
    
    def restartTorqueMom(self):
        if "ubuntu" in self.distro.lower() or "mint" in self.distro.lower() or "debian" in self.distro.lower():
            self.runSingleCommandAsBatch("sudo /etc/init.d/torque-mom restart")
        elif "centos" in self.distro.lower() or "scientific linux" in self.distro.lower() or "red hat" in self.distro.lower() or "fedora" in self.distro.lower():
              
            self.runSingleCommandAsBatch("sudo /etc/init.d/pbs_mom restart")


    def installRequiredSoftware(self):
        
        
        if "ubuntu" in self.distro.lower() or "mint" in self.distro.lower() or "debian" in self.distro.lower():
            batch_commands = '''
                sudo apt-get update > /dev/null
                #documentation needs to be updated. linux-headers needs to be added for ubuntu!
                sudo apt-get install -y -q gcc g++ flex bison libssl-dev linux-source perl make linux-headers-`uname -r` zip subversion automake autoconf libfuse2 fuse-utils libfuse-dev pkg-config< /dev/null
                # needed for Ubuntu 10.04
                sudo apt-get install -y -q linux-image < /dev/null
                # will fail on Ubuntu 10.04. Run separately to not break anything
                sudo apt-get install -y -q fuse < /dev/null
                #sudo apt-get install -yu avahi-autoipd  avahi-dnsconfd  avahi-utils avahi-daemon    avahi-discover  avahi-ui-utils </dev/null
                sudo apt-get clean

                #prepare source
                SOURCENAME=`find /usr/src -name "linux-source*" -type d -prune -printf %f`
                cd /usr/src/${SOURCENAME}
                sudo tar -xjf ${SOURCENAME}.tar.bz2  &> /dev/null
                cd ${SOURCENAME}/
                sudo cp /boot/config-`uname -r` .config
                sudo make oldconfig &> /dev/null
                sudo make prepare &>/dev/null
                sudo /sbin/modprobe -v fuse
                sudo chmod a+x /bin/fusermount
                sudo chmod a+r /etc/fuse.conf

            '''
            self.addBatchCommand(batch_commands)
            #self.addBatchCommand("sudo apt-get install -y -q gcc g++ flex bison libssl-dev linux-source perl make linux-headers-`uname -r` zip subversion automake autoconf torque-server torque-scheduler torque-client < /dev/null")
            #self.addBatchCommand('SOURCENAME=`find /usr/src -name "linux-source*" -type d -prune -printf %f`')
            #self.addBatchCommand('cd /usr/src/${SOURCENAME}')
            #self.addBatchCommand('sudo tar -xjf ${SOURCENAME}.tar.bz2  &> /dev/null')
            #self.addBatchCommand('cd ${SOURCENAME}/')
            #self.addBatchCommand('sudo cp /boot/config-`uname -r` .config')
            #self.addBatchCommand('sudo make oldconfig &> /dev/null')
            #self.addBatchCommand('sudo make prepare &>/dev/null')
        elif "suse" in self.distro.lower():
            batch_commands = '''
            # prereqs should be installed as part of the image. Thanx SuseStudio!
            #zypper --non-interactive install gcc gcc-c++ gcc-gfortran flex bison libopenssl-devel kernel-source kernel-syms kernel-devel perl make subversion automake autoconf zip fuse fuse-devel fuse-libs sudo nano 
            #install db4

            cd /usr/src/linux-`uname -r | sed s/-[\d].*//`
            sudo cp /boot/config-`uname -r` .config
            sudo make oldconfig &> /dev/null
            sudo make modules_prepare &>/dev/null
            sudo ln -s /lib/modules/`uname -r`/build/Module.symvers /lib/modules/`uname -r`/source
            sudo modprobe -v fuse
            sudo chmod a+x /bin/fusermount
            sudo chmod a+r /etc/fuse.conf



            '''
            self.addBatchCommand(batch_commands)
        elif "centos" in self.distro.lower() or "scientific linux" in self.distro.lower() or "red hat" in self.distro.lower() or "fedora" in self.distro.lower():
            
            batch_commands = '''
                echo "Installing prereqs via yum..."
                sudo yum -y install gcc gcc-c++ gcc-gfortran fuse flex bison openssl-devel db4-devel kernel-devel-`uname -r` kernel-headers-`uname -r` perl make subversion automake autoconf zip fuse fuse-devel fuse-libs 
                sudo /sbin/modprobe -v fuse
                sudo chmod a+x /bin/fusermount
                sudo chmod a+r /etc/fuse.conf

            '''
            self.addBatchCommand(batch_commands)

        batch_commands = '''
        
        if [ ! -d /opt/db4 ]
        then
            cd ~
            wget -q http://devorange.clemson.edu/pvfs/db-4.8.30.tar.gz
            tar zxf db-4.8.30.tar.gz &> /dev/null
            cd db-4.8.30/build_unix
            echo "Configuring Berkeley DB 4.8.30..."
            ../dist/configure --prefix=/opt/db4 &> db4conf.out
            echo "Building Berkeley DB 4.8.30..."
            make &> db4make.out
            echo "Installing Berkeley DB 4.8.30 to /opt/db4..."
            sudo make install &> db4install.out
        fi
        exit
        exit
        '''
        
        self.addBatchCommand(batch_commands)
        self.runAllBatchCommands()
        self.setEnvironmentVariable("LD_LIBRARY_PATH","/opt/db4/lib:$LD_LIBRARY_PATH")

    def copyOFSSourceFromSVN(self,svnurl,dest_dir,svnusername,svnpassword):
    
        output = []
        self.ofs_branch = os.path.basename(svnurl)
    
        svn_options = ""
        if svnusername != "" and svnpassword != "":
          svn_options = "%s --username %s --password %s" % (svn_options, svnusername,svnpassword)
        
        print "svn export %s %s" % (svnurl,svn_options)
        self.changeDirectory(dest_dir)
        rc = self.runSingleCommand("svn export %s %s" % (svnurl,svn_options),output)
        if rc != 0:
            print "Could not export from svn"
            print output
            return rc
        else:
            self.ofs_source_location = "%s/%s" % (dest_dir.rstrip('/'),self.ofs_branch)
            print "svn exported to %s" % self.ofs_source_location
        
        # svn --export directory --username --password
        return rc


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
  
    def copyOFSSourceFromDirectory(self,directory,dest_dir):
        if directory != dest_dir:
            rc = self.copyLocal(directory,dest_dir,True)
        self.ofs_source_location = dest_dir
        return rc
      
    def copyOFSSourceFromRemoteNode(self,source_node,directory,dest_dir):
        #implemented in subclass
        return 0
        
      
      
    def copyOFSSource(self,resource_type,resource,dest_dir,username="",password=""):
        # Make directory dest_dir
        rc = self.runSingleCommand("mkdir -p %s" % dest_dir)
        if rc != 0:
            print "Could not mkdir -p %s" %dest_dir
            return rc
          
        
        # ok, now what kind of resource do we have here?
        # switch on resource_type
        #
        # svn
        if resource_type == "SVN":
          rc = self.copyOFSSourceFromSVN(resource,dest_dir,username,password)
        # elseif tarball
        elif resource_type == "TAR":
          rc = self.copyOFSSourceFromRemoteTarball(resource,dest_dir)
        elif resource_type == "REMOTEDIR":
          # elseif remote_directory
          # is the resource a hostname or ipaddress?
          # if hostname, then look up the ip address
          # check the ipaddress in the key table
          # if we have it, then copy directory from the remote to the dest_dir, recursively.
          # create a new node for the remote node.
          self.copyOFSSourceFromRemoteNode(directory,dest_dir)
        elif resource_type == "LOCAL":
          #Must be "pushed" from local node to current node
          pass
        elif resouce_type == "BUILDNODE":
          # else local directory
          rc = self.copyOFSSourceFromDirectory(resource,dest_dir)
        else:
          print "Resource type %s not supported!\n" % resource_type
          return -1
        
        
        return rc
        
        
      
    def configureOFSSource(self,
        build_kmod=True,
        enable_strict=False,
        enable_fuse=False,
        enable_shared=False,
        ofs_prefix="/opt/orangefs",
        db4_prefix="/opt/db4",
        configure_opts="",
        debug=False):
    
        
        # Change directory to source location.
        self.current_directory = self.ofs_source_location
        # Run prepare.
        output = []
        rc = self.runSingleCommand("./prepare",output)
        if rc != 0:
            print self.ofs_source_location+"/prepare failed!" 
            print output
            return rc
        
        # get the kernel version if it has been updated
        self.kernel_version = self.runSingleCommandBacktick("uname -r")
        
        self.kernel_source_location = "/lib/modules/%s" % self.kernel_version
        
        #default_options = "--disable-karma --enable-shared --enable-static --enable-ucache --with-db=/opt/db4 --prefix=/tmp/orangefs --with-kernel=%s/build" % self.kernel_source_location
        
        configure_opts = configure_opts+" --prefix=%s --with-db=%s" % (ofs_prefix,db4_prefix)
        
        if build_kmod == True:
            configure_opts = "%s --with-kernel=%s/build" % (configure_opts,self.kernel_source_location)
        
        if enable_strict == True:
            configure_opts = configure_opts+" --enable-strict"

        if enable_shared == True:
            configure_opts = configure_opts+" --enable-shared"

        if enable_fuse == True:
            configure_opts = configure_opts+" --enable-fuse"
        
        
        
        rc = self.runSingleCommand("./configure %s" % configure_opts, output)
        # did configure run correctly?
        if rc == 0:
            self.ofs_installation_location = ofs_prefix
        else:
            print "Configuration of OrangeFS at %s Failed!" % self.ofs_source_location
            print output
            

        return rc
        
    def checkMount(self,output=[]):
        mount_check = self.runSingleCommand("mount | grep %s" % self.ofs_mountpoint,output)
        '''    
        if mount_check == 0:
            print "OrangeFS mount found: "+output[1]
        else:
            print "OrangeFS mount not found!"
            print output
        '''
        return mount_check

    def getAliasesFromConfigFile(self,config_file_name):
        
        pass
        
        
    
    def makeOFSSource(self,make_options=""):
        # Change directory to source location.
        self.changeDirectory(self.ofs_source_location)
        output = []
        rc = self.runSingleCommand("make "+make_options, output)
        if rc != 0:
            print "Build (make) of of OrangeFS at %s Failed!" % self.ofs_source_location
            print output
            return rc
        
        rc = self.runSingleCommand("make kmod",output)
        if rc != 0:
            print "Build (make) of of OrangeFS-kmod at %s Failed!" % self.ofs_source_location
            print output
            
        return rc

    def getKernelVersion(self):
        #if self.kernel_version != "":
        #  return self.kernel_version
        return self.runSingleCommand("uname -r")
     
      
    def installOFSSource(self,install_options=""):
        self.changeDirectory(self.ofs_source_location)
        output = []
        rc = self.runSingleCommand("make install",output)
        if rc != 0:
            print "Could not install OrangeFS from %s to %s" % (self.ofs_source_location,self.ofs_installation_location)
            print output
            return rc
        self.runSingleCommand("make kmod_install kmod_prefix=%s" % self.ofs_installation_location,output)
        if rc != 0:
            print "Could not install OrangeFS from %s to %s" % (self.ofs_source_location,self.ofs_installation_location)
            print output
        
        return rc

        
    def installOFSTests(self,configure_options=""):
        
        output = []
        if configure_options == "":
            configure_options = "--with-db=/opt/db4 --prefix=%s" % self.ofs_installation_location
        
 
        
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
   

    def generatePAVConf(self,**kwargs):
        
        pav_conf = kwargs.get("pav_conf")
        if pav_conf == None:
            pav_conf = "/home/%s/pav.conf" % self.current_user
        
        keep_conf = kwargs.get("keep_conf")    
        if keep_conf != None:
            return pav_conf
        
        file = open("/tmp/pav.conf","w+")
        
        file.write("SRCDIR_TOP=%s\n" % self.ofs_source_location)
        file.write("BUILDDIR=%s\n" % self.ofs_source_location)
        
        nodefile = self.runSingleCommandBacktick("echo REAL LIST OF NODES HERE")
        
        file.write("NODEFILE=%s\n" % nodefile)
        file.write("IONCOUNT=1\n")
        file.write("METACOUNT=1\n")
        file.write("UNIQUEMETA=0\n")
        # create this directory on all nodes
        file.write("WORKINGDIR=/tmp/pvfs-pav-working\n")
        file.write("PROTOCOL=tcp\n")
        file.write("PVFSTCCPORT=%s\n" % self.ofs_tcp_port)
        file.write("STORAGE=$WORKINGDIR/storage\n")
        file.write("SERVERLOG=$WORKINGDIR/log\n")
        file.write("MOUNTPOINT=%s\n" % self.ofs_mountpoint)
        file.write("BINDIR=$WORKINDIR/bin\n")
        file.write("RCMDPROG=ssh -i %s\n" % self.sshNodeKeyFile)
        file.write("RCPPROG=scp -i %s\n" % self.sshNodeKeyFile)
        file.write("RCMDPROG_ROOT=ssh -l root -i %s\n" % self.sshNodeKeyFile)
        file.write("PROGROOT=$SRCDIR_TOP/test/common/pav\n")
        file.write("SERVER=%s/sbin/pvfs2-server\n" % self.ofs_installation_location)
        file.write("PINGPROG=%s/bin/pvfs2-ping\n" % self.ofs_installation_location)
        file.write("GENCONFIG=%s/bin/pvfs2-genconfig\n" % self.ofs_installation_location)
        file.write("COPYBINS=1\n")
        file.write("TROVESYNC=1\n")
        file.write("MOUNT_FS=0\n")
        file.write("KERNEL_KVER=%s\n" % self.kernel_version)
        file.write("PVFS_KMOD=%s/lib/modules/%s/kernel/fs/pvfs2/pvfs2.ko\n" % (self.ofs_installation_location,self.kernel_version))
        file.write("PVFS_CLIENT=%s/sbin/pvfs2-client\n" % self.ofs_installation_location)
        file.write("COMPUTENODES_LAST=1\n")
        file.close()
        
        return pav_conf
        
        #============================================================================
        #
        # OFSServerFunctions
        #
        # These functions implement functionality for an OrangeFS server
        #
        #=============================================================================

    def copyOFSInstallationToNode(self,destinationNode):
        rc = self.copyToRemoteNode(self.ofs_installation_location+"/", destinationNode, self.ofs_installation_location, True)
        destinationNode.ofs_installation_location = self.ofs_installation_location
        destinationNode.ofs_branch =self.ofs_branch
        return rc
       
      
       
    def configureOFSServer(self,ofs_hosts_v,ofs_fs_name,configuration_options="",ofs_source_location="",ofs_storage_location=""):
        # This function runs the configuration programs and puts the result in self.ofs_installation_location/etc/orangefs.conf
        self.ofs_fs_name=ofs_fs_name
        
        self.changeDirectory(self.ofs_installation_location)
        
        if ofs_storage_location == "":
            self.ofs_storage_location = self.ofs_installation_location + "/data"
        else:
            self.ofs_storage_location = self.ofs_storage_location
           
        # ofs_hosts is a list of ofs hosts separated by white space.
        ofs_host_str = ""
        
        # Add each ofs host to the string of hosts.
        for ofs_host in ofs_hosts_v:
           ofs_host_str = ofs_host_str + ofs_host.host_name + ":3396,"
        
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
        
     
        
      
    def startOFSServer(self):
        # remove the old storage directory.
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
        
        # This is sloppy, but should work. Ideally should reimplement in python, but don't want to reinvent the wheel.
        print "Attempting to start OFSServer for host %s" % self.host_name
        
        #self.runSingleCommand("ls -l %s" % self.ofs_installation_location)
        #print "Installation location is %s" % self.ofs_installation_location
        #print "Running command \"grep 'Alias ' %s/etc/orangefs.conf | grep %s | cut -d ' ' -f 2\"" % (self.ofs_installation_location,self.host_name)
        alias_list = self.getAliasesFromConfigFile(self.ofs_installation_location + "/etc/orangefs.conf")
        #aliases = self.runSingleCommandBacktick('grep "Alias " %s/etc/orangefs.conf | grep %s | cut -d " " -f 2' % (self.ofs_installation_location,self.host_name))
        if len(alias_list) == 0:
            print "Could not find any aliases in %s/etc/orangefs.conf" % self.ofs_installation_location
            return -1
        
        for alias in alias_list:
          # create the storage space, if necessary.
            if self.host_name in alias:
                self.runSingleCommand("mkdir -p %s" % self.ofs_storage_location)
                #self.runSingleCommand("cat %s/etc/orangefs.conf")
              
                self.runSingleCommand("%s/sbin/pvfs2-server -p %s/pvfs2-server-%s.pid -f %s/etc/orangefs.conf -a %s" % ( self.ofs_installation_location,self.ofs_installation_location,self.host_name,self.ofs_installation_location,alias))
              
              
                #now start the server
                self.runSingleCommand("%s/sbin/pvfs2-server -p %s/pvfs2-server-%s.pid %s/etc/orangefs.conf -a %s" % (self.ofs_installation_location,self.ofs_installation_location,self.host_name,self.ofs_installation_location,alias))
                #self.runAllBatchCommands()
                # give the servers 15 seconds to get running
                print "Starting OrangeFS servers..."
                time.sleep(15)
    
    #   print "Checking to see if OrangeFS servers are running..."
    #   running = self.runSingleCommand("ps aux | grep pvfs2")
    #      print running
          
        #Now set up the pvfs2tab_file
        self.ofs_mountpoint = "/tmp/mount/orangefs"
        self.runSingleCommand("mkdir -p "+ self.ofs_mountpoint)
        self.runSingleCommand("mkdir -p %s/etc" % self.ofs_installation_location)
        self.runSingleCommand("echo \"tcp://%s:3396/%s %s pvfs2 defaults 0 0\" > %s/etc/orangefstab" % (self.host_name,self.ofs_fs_name,self.ofs_mountpoint,self.ofs_installation_location))
        self.setEnvironmentVariable("PVFS2TAB_FILE",self.ofs_installation_location + "/etc/orangefstab")
       
        # set the debug mask
        self.runSingleCommand("%s/bin/pvfs2-set-debugmask -m %s \"all\"" % (self.ofs_installation_location,self.ofs_mountpoint))
       
        return 0
        
    def stopOFSServer(self):
        
        # read the file from the .pid file.
        # kill the pid
        #self.runSingleCommand("for pidfile in %s/pvfs2-server*.pid ; do if [ -f $pidfile ] ; then kill -9 `cat $pidfile`; fi; done")
        self.runSingleCommand("killall -s 9 pvfs2-server")
        
        #ofs_servers = self.stopOFSServer("ps aux | grep pvfs2-server"
        
        # if it's still alive, use kill -9
        
        #============================================================================
        #
        # OFSClientFunctions
        #
        # These functions implement functionality for an OrangeFS client
        #
        #=============================================================================

    def installKernelModule(self):
        
        # Installing Kernel Module is a root task, therefore, it must be done via batch.
        # The following shell commands are implemented in Python:
        '''
        sudo /sbin/insmod ${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/lib/modules/`uname -r`/kernel/fs/pvfs2/pvfs2.ko &> pvfs2-kernel-module.log
        sudo /sbin/lsmod >> pvfs2-kernel-module.log
        '''
        self.addBatchCommand("sudo /sbin/insmod %s/lib/modules/%s/kernel/fs/pvfs2/pvfs2.ko &> pvfs2-kernel-module.log" % (self.ofs_installation_location,self.kernel_version))
        self.addBatchCommand("sudo /sbin/lsmod >> pvfs2-kernel-module.log")
        self.runAllBatchCommands()
        
     
    def startOFSClient(self):
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
        # TBD: Add security.
        keypath = ""
        self.addBatchCommand("export LD_LIBRARY_PATH=/opt/db4/lib:%s/lib" % self.ofs_installation_location)
        self.addBatchCommand("export PVFS2TAB_FILE=%s/etc/orangefstab" % self.ofs_installation_location)
        self.addBatchCommand("sudo LD_LIBRARY_PATH=/opt/db4/lib:%s/lib PVFS2TAB_FILE=%s/etc/orangefstab  %s/sbin/pvfs2-client -p %s/sbin/pvfs2-client-core -L %s/pvfs2-client-%s.log" % (self.ofs_installation_location,self.ofs_installation_location,self.ofs_installation_location,self.ofs_installation_location,self.ofs_installation_location,self.ofs_branch))
        print "Starting pvfs2-client: "
        print "sudo LD_LIBRARY_PATH=/opt/db4/lib:%s/lib PVFS2TAB_FILE=%s/etc/orangefstab  %s/sbin/pvfs2-client -p %s/sbin/pvfs2-client-core -L %s/pvfs2-client-%s.log" % (self.ofs_installation_location,self.ofs_installation_location,self.ofs_installation_location,self.ofs_installation_location,self.ofs_installation_location,self.ofs_branch)
        print ""
        self.addBatchCommand("sudo chmod 644 %s/pvfs2-client-%s.log" % (self.ofs_installation_location,self.ofs_branch))
        self.runAllBatchCommands()
        #client_log = self.runSingleCommandBacktick("cat %s/pvfs2-client-%s.log" % (self.ofs_installation_location,self.ofs_branch))
        #print "Client log output"
        #print client_log
        
      
    def mountOFSFilesystem(self,mount_fuse=False):
        # Mounting the OFS Filesystem is a root task, therefore, it must be done via batch.
        # The following shell command is implimented in Python
        '''
            echo "Mounting pvfs2 service at tcp://${HOSTNAME}:3396/pvfs2-fs at mountpoint $PVFS2_MOUNTPOINT"
        sudo mount -t pvfs2 tcp://${HOSTNAME}:3396/pvfs2-fs ${PVFS2_MOUNTPOINT}
        
        
        if [ $? -ne 0 ]
        then
            echo "Something has gone wrong. Mount failed."
        fi
        mount > allmount.log
        '''
        output = []
        rc = self.checkMount(output)
        if rc == 0:
            print "OrangeFS already mounted at %s" % output[1]
            return
        if mount_fuse == True:
            print "Mounting OrangeFS service at tcp://%s:%s/%s at mountpoint %s via fuse" % (self.host_name,self.ofs_tcp_port,self.ofs_fs_name,self.ofs_mountpoint)
            self.runSingleCommand("%s/bin/pvfs2fuse %s -o fs_spec=tcp://%s:%s/%s -o nonempty" % (self.ofs_installation_location,self.ofs_mountpoint,self.host_name,self.ofs_tcp_port,self.ofs_fs_name),output)
            #print output
            
        else:
            print "Mounting OrangeFS service at tcp://%s:%s/%s at mountpoint %s" % (self.host_name,self.ofs_tcp_port,self.ofs_fs_name,self.ofs_mountpoint)
            self.addBatchCommand("sudo mount -t pvfs2 tcp://%s:%s/%s %s" % (self.host_name,self.ofs_tcp_port,self.ofs_fs_name,self.ofs_mountpoint))
            self.runAllBatchCommands()
        
        print "Waiting 30 seconds for mount"            
        time.sleep(30)


    def unmountOFS(self):
        print "Unmounting OrangeFS mounted at " + self.ofs_mountpoint
        self.addBatchCommand("sudo umount %s" % self.ofs_mountpoint)
        self.addBatchCommand("sleep 10")

    def stopOFSClient(self):
        
        '''
            mount | grep -q $PVFS2_MOUNTPOINT && sudo umount $PVFS2_MOUNTPOINT
        ps -e | grep -q pvfs2-client && sudo killall pvfs2-client
        sleep 1
        /sbin/lsmod | grep -q pvfs2 && sudo /sbin/rmmod pvfs2
        # let teardown always succeed.  pvfs2-client might already be killed
        # and pvfs2 kernel module might not be loaded yet 
        '''
        self.unmountOFS()
        print "Stopping pvfs2-client process"
        self.addBatchCommand("sudo killall pvfs2-client")
        self.addBatchCommand("sleep 10")
        self.addBatchCommand("sudo killall -s 9 pvfs2-client")
        self.addBatchCommand("sleep 2")
        #print "Removing pvfs2 kernel module."
        #self.addBatchCommand("sudo /sbin/rmmod pvfs2")
        #self.addBatchCommand("sleep 2")
        self.runAllBatchCommands()
        
    
        
    def setupMPIEnvironment(self):
        
        home_dir = "/home/"+self.current_user
        self.setEnvironmentVariable("PAV_CONFIG","%s/pav.conf" % home_dir)

        # cluster environments need a few things available on a cluster-wide file
        # system: pav (which needs some pvfs2 programs), the mpi program, mpich2
        # (specifically mpd and tools )

        self.setEnvironmentVariable("CLUSTER_DIR","%s/nightly" % home_dir)
        self.addBatchCommand("mkdir -p ${CLUSTER_DIR}")
        self.addBatchCommand("rm -rf ${CLUSTER_DIR}/pav ${CLUSTER_DIR}/mpich2 ${CLUSTER_DIR}/pvfs2")
        self.addBatchCommand("cp -ar %s/test/common/pav ${CLUSTER_DIR}" % self.ofs_source_location)
        self.addBatchCommand("cp -ar %s/soft/mpich2 ${CLUSTER_DIR}" % self.ofs_source_location)
        self.addBatchCommand("cp -ar %s ${CLUSTER_DIR}/pvfs2" % self.ofs_installation_location)
        self.runAllBatchCommands()

    def installMpich2(self,location=None):
        if location == None:
            location = "/tmp/%s/mpich2" % self.current_user
            
        url = "http://devorange.clemson.edu/pvfs/mpich2-1.5.tar.gz"
        # just to make debugging less painful
        #[ -n "${SKIP_BUILDING_MPICH2}" ] && return 0
        #[ -d ${PVFS2_DEST} ] || mkdir ${PVFS2_DEST}
        self.runSingleCommand("mkdir -p "+location)
        tempdir = self.current_directory
        self.changeDirectory(location)
        self.runSingleCommand("rm -rf mpich2-*.tar.gz")
        #wget http://www.mcs.anl.gov/research/projects/mpich2/downloads/tarballs/1.5/mpich2-1.5.tar.gz
        rc = self.runSingleCommand("wget --quiet %s" % url)
        #wget --passive-ftp --quiet 'ftp://ftp.mcs.anl.gov/pub/mpi/misc/mpich2snap/mpich2-snap-*' -O mpich2-latest.tar.gz
        if rc != 0:
            print "Could not download mpich from %s." % url
            self.changeDirectory(tempdir)
            return rc
        self.runSingleCommand("rm -rf mpich2-snap-*")
        #tar xzf mpich2-latest.tar.gz
        self.runSingleCommand("tar xzf mpich2-1.5.tar.gz")
        self.runSingleCommand("mv mpich2-1.5 mpich2-snapshot")
        
        self.runSingleCommand("mkdir -p %s/mpich2-snapshot/build" % location)
        self.changeDirectory(location+"mpich2-snapshot/build")
        
        configure = '''
        ../configure -q --prefix=%s/soft/mpich2 \
		--enable-romio --with-file-system=ufs+nfs+testfs+pvfs2 \
		--with-pvfs2=%s \
		--enable-g=dbg --without-mpe \
		--disable-f77 --disable-fc >mpich2config-%s.log
        ''' % (self.ofs_extra_tests_location,self.ofs_installation_location,self.ofs_branch)
        
        wd = self.runSingleCommandBacktick("pwd")
        print wd
        print configure
        rc = self.runSingleCommand(configure)
        if rc != 0:
            print "Configure of MPICH failed. rc=%d" % rc
            self.changeDirectory(tempdir)
            return rc
        
        rc = self.runSingleCommand("make > mpich2make-%s.log 2> /dev/null" % self.ofs_branch)
        if rc != 0:
            print "Make of MPICH failed."
            self.changeDirectory(tempdir)
            return rc
        
        rc = self.runSingleCommand("make install > mpich2install-${CVSTAG} 2> /dev/null" % self.ofs_branch)
        if rc != 0:
            print "Install of MPICH failed."
            self.changeDirectory(tempdir)
            return rc
        
        return 0

        #============================================================================
        #
        # OFSTestFunctions
        #
        # These functions implement functionality to test the OFSSystem Move??? Yes!
        #
        #=============================================================================
      
    def setAutomatedTestEnvironment(self):
        # This function sets the environment for an automated test to run.PVFS2_MOUNTPOINT=/tmp/ubuntu/pvfs2-nightly/20130415/branches/stable/mount
        #self.setEnvironmentVariable("USERLIB_SCRIPTS","%s/userint-tests.d" % self.ofs_installation_location)
        #self.setEnvironmentVariable("SYSINT_SCRIPTS","%s/sysint-tests.d" % self.ofs_installation_location)
        #self.setEnvironmentVariable("VFS_SCRIPTS","%s/vfs-tests.d" % self.ofs_installation_location)
        #self.setEnvironmentVariable("LOGNAME",self.current_user)
        #self.setEnvironmentVariable("SVNBRANCH",self.ofs_branch)
        self.setEnvironmentVariable("PVFS2_MOUNTPOINT",self.ofs_mountpoint)
        self.setEnvironmentVariable("PVFS2_DEST",self.ofs_installation_location)
        self.setEnvironmentVariable("EXTRA_TESTS",self.ofs_extra_tests_location)
        
        
        

        pass
      

      

    def runGroupTest(self,test_group):
        pass
     
    def runSingleTest(self,test_group,test_name):
        pass

    def runAllTests(self):
        pass
      
 


    
#===================================================================================================
# Unit test script begins here
#===================================================================================================
def test_driver():
    local_machine = OFSTestLocalNode()
    local_machine.addRemoteKey('10.20.102.54',"/home/jburton/buildbot.pem")
    local_machine.addRemoteKey('10.20.102.60',"/home/jburton/buildbot.pem")
    
    '''
    local_machine.changeDirectory("/tmp")
    local_machine.setEnvironmentVariable("FOO","BAR")
    local_machine.runSingleCommand("echo $FOO")
    local_machine.addBatchCommand("echo \"This is a test of the batch command system\"")
    local_machine.addBatchCommand("echo \"Current directory is `pwd`\"")
    local_machine.addBatchCommand("echo \"Variable foo is $FOO\"")
    local_machine.runAllBatchCommands()
    

    
    #local_machine.copyOFSSource("LOCALDIR","/home/jburton/testingjdb/","/tmp/jburton/testingjdb/")
    #local_machine.configureOFSSource()
    #local_machine.makeOFSSource()
    #local_machine.installOFSSource()
    '''
    
    remote_machine = OFSTestRemoteNode('ec2-user','10.20.102.54',"/home/jburton/buildbot.pem",local_machine)
    remote_machine1 = OFSTestRemoteNode('ec2-user','10.20.102.60', "/home/jburton/buildbot.pem",local_machine)

'''
    remote_machine.setEnvironmentVariable("LD_LIBRARY_PATH","/opt/db4/lib")
    remote_machine1.setEnvironmentVariable("LD_LIBRARY_PATH","/opt/db4/lib")

   # remote_machine.uploadNodeKeyFromLocal(local_machine)
   # remote_machine1.uploadNodeKeyFromLocal(local_machine)
    remote_machine.uploadRemoteKeyFromLocal(local_machine,remote_machine1.ip_address)
    remote_machine1.uploadRemoteKeyFromLocal(local_machine,remote_machine.ip_address)
    
    remote_machine.copyOFSSource("SVN","http://orangefs.org/svn/orangefs/trunk","/tmp/ec2-user/")
    print "Configuring remote source"
    remote_machine.configureOFSSource()
    remote_machine.makeOFSSource()
    remote_machine.installOFSSource()
    
    #remote_machine1.runSingleCommandAsBatch("sudo rm /tmp/mount/orangefs/touched")
    #remote_machine1.copyOFSSource("TAR","http://www.orangefs.org/downloads/LATEST/source/orangefs-2.8.7.tar.gz","/tmp/ec2-user/")


    print ""
    print "-------------------------------------------------------------------------"
    print "Configuring remote source without shared libraries on " + remote_machine.host_name
    print ""
    remote_machine.runSingleCommand("rm -rf /tmp/ec2-user")
    remote_machine.runSingleCommand("rm -rf /tmp/orangefs")
    remote_machine1.installBenchmarks("http://devorange.clemson.edu/pvfs/benchmarks-20121017.tar.gz","/tmp/ec2-user/benchmarks")
    remote_machine.copyOFSSource("SVN","http://orangefs.org/svn/orangefs/branches/stable","/tmp/ec2-user/")
    remote_machine.configureOFSSource()
    remote_machine.makeOFSSource()
    remote_machine.installOFSSource()

    remote_machine.configureOFSServer([remote_machine])
    remote_machine.stopOFSServer()
    remote_machine.startOFSServer()
    #remote_machine.stopOFSServer()
    print ""
    print "Checking to see if pvfs2 server is running..."
    remote_machine.runSingleCommand("ps aux | grep pvfs2")
    print ""
    print "Checking to see what is in /tmp/mount/orangefs before mount..."
    remote_machine.runSingleCommand("ls -l /tmp/mount/orangefs")
    remote_machine.installKernelModule()
    remote_machine.startOFSClient()
    remote_machine.mountOFSFilesystem()
    print ""
    print "Checking to see if pvfs2 client is running..."
    remote_machine.runSingleCommand("ps aux | grep pvfs2")
    print ""
    print "Checking pvfs2 mount..."
    remote_machine.runSingleCommand("mount | (grep pvfs2 || echo \"Not Mounted\")")
    print ""
    print "Checking to see what is in /tmp/mount/orangefs after mount..."
    remote_machine.runSingleCommand("ls -l /tmp/mount/orangefs")
    print ""
    print "Checking to see if mounted FS works..."
    remote_machine.runSingleCommandAsBatch("sudo touch /tmp/mount/orangefs/touched")
    print ""
    print "Checking to see what is in /tmp/mount/orangefs after touch..."
    remote_machine.runSingleCommand("ls -l /tmp/mount/orangefs")
    print ""

    remote_machine.stopOFSClient()
    remote_machine.stopOFSServer()
    print "Checking to see if all pvfs2 services have stopped."
    remote_machine.runSingleCommand("ps aux | grep pvfs2")
    print ""

    print ""
    print "-------------------------------------------------------------------------"
    print "Configuring remote source with shared libraries on " + remote_machine1.host_name
    print ""
    remote_machine1.runSingleCommand("rm -rf /tmp/orangefs")
    remote_machine1.runSingleCommand("rm -rf /tmp/ec2-user")
    remote_machine1.installBenchmarks("http://devorange.clemson.edu/pvfs/benchmarks-20121017.tar.gz","/tmp/ec2-user/")
    remote_machine1.copyOFSSource("SVN","http://orangefs.org/svn/orangefs/branches/stable","/tmp/ec2-user/")


    remote_machine1.configureOFSSource("--enable-strict --enable-shared --enable-ucache --disable-karma --with-db=/opt/db4 --prefix=/tmp/orangefs --with-kernel=%s/build" % remote_machine1.getKernelVersion())
    #remote_machine1.configureOFSSource()
    remote_machine1.makeOFSSource()
    remote_machine1.installOFSSource()

    remote_machine1.configureOFSServer([remote_machine1])
    remote_machine1.stopOFSServer()
    remote_machine1.startOFSServer()
    #remote_machine1.stopOFSServer()
    print ""
    print "Checking to see if pvfs2 server is running..."
    remote_machine1.runSingleCommand("ps aux | grep pvfs2")
    print ""
    print "Checking to see what is in /tmp/mount/orangefs before mount..."
    remote_machine1.runSingleCommand("ls -l /tmp/mount/orangefs")
    remote_machine1.installKernelModule()
    remote_machine1.startOFSClient()
    remote_machine1.mountOFSFilesystem()
    print ""
    print "Checking to see if pvfs2 client is running..."
    remote_machine1.runSingleCommand("ps aux | grep pvfs2")
    print ""
    print "Checking pvfs2 mount..."
    remote_machine1.runSingleCommand("mount | (grep pvfs2 || echo \"Not Mounted\")")
    print ""
    print "Checking to see what is in /tmp/mount/orangefs after mount..."
    remote_machine1.runSingleCommand("ls -l /tmp/mount/orangefs")
    print ""
    print "Checking to see if mounted FS works"
    remote_machine1.runSingleCommandAsBatch("sudo touch /tmp/mount/orangefs/touched")
    print ""
    print "Checking to see what is in /tmp/mount/orangefs after touch..."
    remote_machine1.runSingleCommand("ls -l /tmp/mount/orangefs")

    remote_machine1.stopOFSClient()
    remote_machine1.stopOFSServer()
    print "Checking to see if all pvfs2 services have stopped..."
    remote_machine1.runSingleCommand("ps aux | grep pvfs2")
    print ""


    #export LD_LIBRARY_PATH=${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/lib:/opt/db4/lib
    #export PRELOAD="LD_PRELOAD=${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/lib/libofs.so:${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/lib/libpvfs2.so

    #local_machine.copyToRemoteNode("/home/jburton/buildbot.pem",remote_machine,"~/buildbot.pem",False)
    #remote_machine.copyToRemoteNode("~/buildbot.pem",remote_machine1,"~/buildbot.pem",False)

    
    remote_machine.setEnvironmentVariable("FOO","BAR")
    remote_machine.runSingleCommand("echo $FOO")
    remote_machine.runSingleCommand("hostname -s")
    remote_machine.addBatchCommand("echo \"This is a test of the batch command system\"")
    remote_machine.addBatchCommand("echo \"Current directory is `pwd`\"")
    remote_machine.addBatchCommand("echo \"Variable foo is $FOO\"")
    remote_machine.addBatchCommand("touch /tmp/touched")
    remote_machine.addBatchCommand("sudo apt-get update && sudo apt-get -y dist-upgrade")
    remote_machine.addBatchCommand("sudo yum -y upgrade")
    remote_machine.runAllBatchCommands()
    
   ''' 
    
#Call script with -t to test
#if len(sys.argv) > 1 and sys.argv[1] == "-t":
#    test_driver()
