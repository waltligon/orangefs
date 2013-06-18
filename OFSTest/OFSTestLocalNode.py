###############################################################################################'
#
# class OFSTestLocalNode(OFSTestNode)
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
#
#
################################################################################################

import OFSTestNode
import os
import subprocess
import shlex
import cmd
import time
import sys
import xml.etree.ElementTree as ET

class OFSTestLocalNode(OFSTestNode.OFSTestNode):


    def __init__(self):
        super(OFSTestLocalNode,self).__init__()
        
        # Local nodes are neither remote nor EC2
        self.is_remote = False
        self.is_ec2 = False
        self.ip_address = "127.0.0.1"
        self.currentNodeInformation()
      
    def currentNodeInformation(self):
        super(OFSTestLocalNode,self).currentNodeInformation()
        self.user_name = self.runSingleCommandBacktick("whoami")
        
        
        
        
        

      #==========================================================================
      # 
      # Utility functions
      #
      # These functions implement basic functionality to operate the node
      #
      #==========================================================================
     
    def runAllBatchCommands(self):
     
        # Open file with mode 700
        script_file = open("/tmp/runcommand.sh",'w')
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
        
        os.chmod("/tmp/runcommand.sh",0755)
        
        # run the command and capture stdout and stderr
        
        rc = subprocess.call("/tmp/runcommand.sh",shell=True)
        
       
        # clear the batch commands list
        self.batch_commands = []
        
        return rc
      
      
      
    def prepareCommandLine(self,command,outfile="",append_out=False,errfile="",append_err=False):
        
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
        
        #start with the ssh command and open quote
        command_chunks = ["/bin/bash -c '"]

        # change to proper directory
        command_chunks.append("cd %s; " % self.current_directory)
        #now append each variable followed by a space
        for variable in self.current_environment:
          command_chunks.append("%s=%s; " % (variable,self.current_environment[variable]))
        #now append the command
        command_chunks.append(command)
        command_chunks.append("'")
        command_chunks.append(outdirect)
        command_chunks.append(errdirect)
        
        
        #Command chunks has the entire command, but not the way python likes it. Join the chunks into one string
        command_line = ''.join(command_chunks)
        #print command_line
        # now use shlex to get the right list for python
        #return shlex.split(command_line)
        return command_line
        
    
    def copyToRemoteNode(self, source, destinationNode, destination, recursive=False):
        # This runs the copy command remotely 
        rflag = ""
        # verify source file exists
        if recursive == True:
          rflag = "-a "
        else:
          rflag = ""
          
        rsync_command = "rsync %s-e \"ssh -i %s -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no\" %s %s@%s:%s" % (rflag,self.getRemoteKeyFile(destinationNode.ip_address),source,destinationNode.current_user,destinationNode.ip_address,destination)
        return self.runSingleCommand(rsync_command)
      
    def copyFromRemoteNode(self, source_node, source, destination, recursive=False):
        # This runs the copy command remotely 
        rflag = ""
        # verify source file exists
        if recursive == True:
          rflag = "-a"
        else:
          rflag = ""
          
        rsync_command = "rsync %s -e \"ssh -i %s -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no\"  %s@%s:%s %s" % (rflag,self.getRemoteKeyFile(source_node.ip_address),source_node.current_user,source_node.ip_address,source,destination)
        return self.runSingleCommand(rsync_command)  
    
    def getAliasesFromConfigFile(self,config_file_name):
        
        print "examining "+config_file_name
        alias = self.runSingleCommandBacktick("ls -l "+config_file_name)
        print alias
        alias = self.runSingleCommandBacktick('cat '+config_file_name)
        print alias
        alias = self.runSingleCommandBacktick('cat '+config_file_name+' | grep \"Alias \"')
        print "Alias is "+ alias
        
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
      
  