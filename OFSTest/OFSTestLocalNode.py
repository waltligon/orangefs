#!/usr/bin/python
##
#
# @class OFSTestLocalNode 
#
#
# This class is for the local machine. 
#
# Since there is always one and only one local machine, there should always be one and only one 
# instance of OFSTestLocalNode. 
#
# The OFSTestLocalNode is the "master" of the OFSTest system. It controls access to all the other
# nodes. This is because you access remote nodes through your local machine. 
#
# This programs assumes that the OFSTestNode is a *nix machine operating a bash shell. 
# MacOSX functionality may be limited. Windows local nodes are not currently supported.
#

import OFSTestNode
import os
import subprocess
import shlex
import cmd
import time
import sys
import logging

class OFSTestLocalNode(OFSTestNode.OFSTestNode):

    

    def __init__(self):

        print "-----------------------------------------------------------"
            
        super(OFSTestLocalNode,self).__init__()
        
        ## @var is_remote
        # Local nodes are neither remote nor Cloud
        self.is_remote = False
        ## @var is_cloud
        # Is this node an cloud node? Always false, even if it's true.
        self.is_cloud = False
        ## @var ip_address
        # set to local host
        self.ip_address = "127.0.0.1"
        ## @var current_user 
        # Current logged in user.
        self.current_user = self.runSingleCommandBacktick("whoami")
        #print self.current_user
        self.currentNodeInformation()
        print "Local machine"
        print "-----------------------------------------------------------"    

    ##       
    # @fn currentNodeInformation(self):
    #
    # Gets the current node information, including external ip address.
    # 
    # @param self The object pointer
    
    def currentNodeInformation(self):
        
        super(OFSTestLocalNode,self).currentNodeInformation()
        self.ext_ip_address = self.runSingleCommandBacktick("ifconfig  | grep 'inet addr:'| grep -v '127.0.0.1' | cut -d: -f2 | awk '{ print $1}'")
        
        
        
        
        
        

    #==========================================================================
    # 
    # Utility functions
    #
    # These functions implement basic functionality to operate the node
    #
    #==========================================================================
    
    ##       
    # @fn runAllBatchCommands(self,output=[]):
    #
    # Writes stored batch commands to a file, then runs them.
    # 
    # @param self The object pointer
    # @param output Output of command
     
    def runAllBatchCommands(self,output=[],debug=False):
     
        
        # Open file with mode 700
        batchfile = "./runcommand%d.sh" % OFSTestNode.batch_count
        script_file = open(batchfile,'w')
        script_file.write("#!/bin/bash\n")
        
        for element in self.current_environment:
            script_file.write("export %s=%s\n" % (element, self.current_environment[element]))
        
        # change to current directory
        script_file.write("cd %s\n" % self.current_directory)
        #error checking: Did command run correctly?
        script_file.write("if [ $? -ne 0 ]\n")
        script_file.write("then\n")
        script_file.write("\texit 1\n")
        script_file.write("fi\n")
        
        # command
        for command in self.batch_commands:
            script_file.write(command)
            script_file.write('\n');
        
        #error checking: Did command run correctly?
        #script_file.write("if [ $? -ne 0 ]\n")
        #script_file.write("then\n")
        #script_file.write("\texit 1\n")
        #script_file.write("fi\n")
        
        script_file.close()
        
        os.chmod(batchfile,0755)

        logging.debug("----- Start generated batchfile: %s -----------------------" % batchfile)
        script_file = open(batchfile,'r')
        for line in script_file:
            logging.debug(line)
        script_file.close()
        logging.debug("---- End generated batchfile: %s -------------------------" % batchfile)           
        logging.debug("Command: "+batchfile)    
                    
        # run the command and capture stdout and stderr
        p = subprocess.Popen(batchfile,shell=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,bufsize=-1)
        
        # clear the output list, then append stdout,stderr to list to get pass-by-reference to work
        del output[:]
        output.append(command)
        for i in p.communicate():
            output.append(i)
        
        logging.debug("RC: %r" % p.returncode)
        logging.debug("STDOUT: %s" % output[1] )
        logging.debug("STDERR: %s" % output[2] )
        
        # now clear out the batch commands list
        self.batch_commands = []    
        OFSTestNode.batch_count = OFSTestNode.batch_count+1

        return p.returncode
        
    ##       
    # @fn prepareCommandLine(self,command,outfile="",append_out=False,errfile="",append_err=False,remote_user=None):
    #
    # Formats the command line to run on this specific type of node with appropriate environment.
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
        
        # This runs a single command via bash. To get all the environment variables in will require a little magic.
        outdirect = ""
        
        if outfile != "":
            if append_out == True:
                outdirect = " >> "+outfile
            else:
                outdirect = " >" + outfile
        
        errdirect = ""
        
        if errfile != "":
            if append_err == True:
                errdirect = " 2>> "+errfile
            else:
                errdirect = " 2>" + errfile
        
        if remote_user == None:
            remote_user = self.current_user
        elif remote_user == "root":
            logging.warn("I'm sorry, Dave, I'm afraid I can't do that.")
            logging.warn("Really dumb idea to run commands as root with passwordless access on localhost. Easy way to mess up machine.") 
            remote_user = self.current_user
        
        #start with the ssh command and open quote
        command_chunks = ["/bin/bash -c \""]

        # change to proper directory
        command_chunks.append("cd %s; " % self.current_directory)
        #now append each variable followed by a space
        for variable in self.current_environment:
            command_chunks.append("%s=%s; " % (variable,self.current_environment[variable]))
        #now append the command
        command_chunks.append(command)
        command_chunks.append("\"")
        command_chunks.append(outdirect)
        command_chunks.append(errdirect)
        
        
        #Command chunks has the entire command, but not the way python likes it. Join the chunks into one string
        command_line = ''.join(command_chunks)

        return command_line

    ##
    #
    # @fn copyToRemoteNode(self, source, destination_node, destination, recursive=False):
    #
    # This copies files from this node to the remote node via rsync.
    #
    # @param self The object pointer
    # @param source Source file or directory
    # @param destination_node Node to which files should be copied
    # @param destination Destination file or directory on remote node.
    # @param recursive Copy recursively?
    #
    # @return Return code of copy command.
    
    def copyToRemoteNode(self, source, destination_node, destination, recursive=False):
        # This runs the copy command remotely 
        rflag = ""
        # verify source file exists
        if recursive == True:
            rflag = "-a "
        else:
            rflag = ""
        
        rsync_command = "rsync %s -e \\\"ssh -i %s -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no\\\" %s %s@%s:%s" % (rflag,self.getRemoteKeyFile(destination_node.ext_ip_address),source,destination_node.current_user,destination_node.ext_ip_address,destination)
        
        output = []
        rc = self.runSingleCommand(rsync_command,output)
        if rc != 0:
            logging.exception("Could not copy to remote node")
        return rc
    
    ##
    #
    # @fn copyFromRemoteNode(self, source_node, source, destination, recursive=False):
    #
    # This copies files from the remote node to this node via rsync.
    #
    # @param self The object pointer
    # @param source_node Node from which files should be copied
    # @param source Source file or directory on remote node.
    # @param destination Destination file or directory
    # @param recursive Copy recursively?
    #
    # @return Return code of copy command.
    
      
    def copyFromRemoteNode(self, source_node, source, destination, recursive=False):
        # This runs the copy command remotely 
        rflag = ""
        # verify source file exists
        if recursive == True:
            rflag = "-a"
        else:
            rflag = ""
          
        rsync_command = "rsync %s -e \\\"ssh -i %s -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no\\\"  %s@%s:%s %s" % (rflag,self.getRemoteKeyFile(source_node.ext_ip_address),source_node.current_user,source_node.ext_ip_address,source,destination)
        
        output = []
        rc = self.runSingleCommand(rsync_command,output)
        if rc != 0:
            logging.exception( "Could not copy to remote node")
        return rc
    
    ##
    # @fn def getAliasesFromConfigFile(self,config_file_name):
    #
    # Reads the OrangeFS alias from the configuration file.
    #
    # @param self The object pointer
    # @param config_file_name Full path to the configuration file. (Usually orangefs.conf) 
    #
    # @return list of alias names
    def getAliasesFromConfigFile(self,config_file_name):
        

        alias = self.runSingleCommandBacktick('cat '+config_file_name+' | grep \"Alias \"')
        logging.debug("Alias is "+ alias)
        
        config_file = open(config_file_name,'r')
        
        aliases = []
        for line in config_file.readlines():
            if "Alias " in line:
                # split the line into Alias, AliasName, url
                element = line.split()
                # What we want is element 1
                aliases.append(element[1].rstrip())
            if "</Aliases>" in line:
                break
            
        return aliases 

        #============================================================================
        #
        # OFSBuilderFunctions
        #
        # These functions implement functionality to build OrangeFS
        #
        #=============================================================================

        #============================================================================
        #
        # OFSServerFunctions
        #
        # These functions implement functionality for an OrangeFS server
        #
        #=============================================================================
        
        #============================================================================
        #
        # OFSClientFunctions
        #
        # These functions implement functionality for an OrangeFS client
        #
        #=============================================================================
      
  
