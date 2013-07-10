#!/usr/bin/python
###############################################################################################
#
# class OFSTestRemoteNode(OFSTestNode)
#
#
# This class is for the all remote machines
#
#
#
################################################################################################
import os
import subprocess
import shlex
import cmd
import time
import sys
import xml.etree.ElementTree as ET
import OFSTestNode

class OFSTestRemoteNode(OFSTestNode.OFSTestNode):
 
    def __init__(self,username,ip_address,key,local_node,is_ec2=False,ext_ip_address=None):
        super(OFSTestRemoteNode,self).__init__()
        
        # need ip_address, user_name, host_name, and local keyfile to connect
        self.ip_address = ip_address
        
        # do we have an external ip address?
        if ext_ip_address == None:
            self.ext_ip_address = ip_address
        else:
            self.ext_ip_address = ext_ip_address
        self.current_user = username
        self.is_ec2 = is_ec2
        self.sshLocalKeyFile = key
        self.localnode = local_node
        
        self.getKeyFileFromLocal(local_node)
        super(OFSTestRemoteNode,self).currentNodeInformation()
        
        #print "Host,kernel:  %s,%s" % (self.host_name,self.kernel_version)
        
        
    def getKeyFileFromLocal(self,localnode):
        localnode.addRemoteKey(self.ext_ip_address,self.sshLocalKeyFile)
        self.uploadNodeKeyFromLocal(localnode)
        

        
    def getAliasesFromConfigFile(self,config_file_name):
        
        # read the file from the server
        print "examining "+config_file_name
        alias = self.runSingleCommandBacktick("ls -l "+config_file_name)
        print alias
        alias = self.runSingleCommandBacktick('cat '+config_file_name)
        print alias
        alias = self.runSingleCommandBacktick('cat '+config_file_name+' | grep \"Alias \"')
        print "Alias is "+ alias
        
        alias_lines = alias.split('\n')
        
        aliases = []
        #!/usr/bin/python        
                
        for line in alias_lines:
            if "Alias " in line:
                # split the line into Alias, AliasName, url
                element = line.split()
                # What we want is element 1
                aliases.append(element[1].rstrip())
                
            
        print "aliases returned: "
        print aliases    
        return aliases
         
        
        
        
        
        # ok, now let's get the os version.
        # should have the architecture and version from the kernel name 
    def runAllBatchCommands(self):
        # Open file with mode 700
        script_file = open("/tmp/runcommand.sh",'w')
        script_file.write("#!/bin/bash\n")
        script_file.write("script runcommand\n")
        
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
        

        script_file.write("exit\n")
        script_file.close()

        command_line = "/usr/bin/ssh -i %s %s@%s -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no \"bash -s\" < /tmp/runcommand.sh" % (self.sshLocalKeyFile,self.current_user,self.ext_ip_address)
        #command_v = shlex.split(command_line)
        #print command_line
        # run the script and capture the return code
        rc = subprocess.call(command_line,shell=True)
    
        # now clear out the batch commands list
        self.batch_commands = []    
        
        return rc

    
            
            
        
        


    def prepareCommandLine(self,command,outfile="",append_out=False,errfile="",append_err=False):
        # Input command line 
        # Returns modified of command line

        outdirect = ""
        errdirect = ""
        
    
        if outfile != "":
            if append_out == True:
                outdirect = " >> "+outfile
            else:
                outdirect = " >" + outfile
        
        
        if errfile != "":
            if append_err == True:
                errdirect = " 2>> "+errfile
            else:
                errdirect = " 2>" + errfile
        
        #start with the ssh command and open quote
        command_chunks = ["/usr/bin/ssh -i %s %s@%s -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no \"" % (self.sshLocalKeyFile,self.current_user,self.ext_ip_address)]

        # change to proper directory
        command_chunks.append("cd %s; " % self.current_directory)
        #now append each variable followed by a space
        for variable in self.current_environment:
          command_chunks.append("export %s=%s; " % (variable,self.current_environment[variable]))
        #now append the command
        command_chunks.append(command)
                
        #now append the close quote
        command_chunks.append("\"")
        command_chunks.append(outdirect)
        command_chunks.append(errdirect)
        
        #Command chunks has the entire command, but not the way python likes it. Join the chunks into one string
        command_line = ''.join(command_chunks)
        #print command_line
        # now use shlex to get the right list for python
        #command_v = shlex.split(command_line)
        #command_v[-1] = '"%s"' % command_v[-1]
        return command_line
    
    
     
    def copyToRemoteNode(self, source, destinationNode, destination, recursive=False):
        # This runs the copy command remotely 
        rflag = ""
        # verify source file exists
        if recursive == True:
          rflag = "-a"
        else:
          rflag = ""
          
        rsync_command = "rsync %s -e \\\"ssh -i %s -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no\\\" %s %s@%s:%s" % (rflag,self.getRemoteKeyFile(destinationNode.ext_ip_address),source,destinationNode.current_user,destinationNode.ip_address,destination)
        return self.runSingleCommand(rsync_command)
      
    def copyFromRemoteNode(self, source_node, source, destination, recursive=False):
        # This runs the copy command remotely 
        rflag = ""
        # verify source file exists
        if recursive == True:
          rflag = "-a"
        else:
          rflag = ""
          
        rsync_command = "rsync %s -e \\\"ssh -i %s -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no\\\"  %s@%s:%s %s" % (rflag,self.getRemoteKeyFile(source_node.ext_ip_address),source_node.current_user,source_node.ip_address,source,destination)
        return self.runSingleCommand(rsync_command)
      
    def uploadNodeKeyFromLocal(self, local_node):
        
        # This function uploads a key 
        print "uploading key %s from local to %s" % (local_node.getRemoteKeyFile(self.ext_ip_address), self.ext_ip_address)
        # copy the file from the local node to the current node
        self.sshLocalKeyFile=local_node.getRemoteKeyFile(self.ext_ip_address)
        rc = local_node.copyToRemoteNode(self.sshLocalKeyFile,self,'~/.ofstestkeys/',False)
        
        if rc != 0:
            print "Upload of key %s from local to %s failed!" % (local_node.getRemoteKeyFile(self.ext_ip_address), self.ext_ip_address)
            return rc
        else:
            #set the ssh key file to the uploaded location.
            self.sshNodeKeyFile = "/home/%s/.ofstestkeys/%s" % (self.current_user,os.path.basename(self.sshLocalKeyFile))
            return 0
      
    def uploadRemoteKeyFromLocal(self,local_node,remote_address):
        # This function uploads a key for a remote node and adds it to the table
        # get the remote key name
        print "uploading key %s from local to %s" % (local_node.getRemoteKeyFile(self.ext_ip_address), self.ext_ip_address)
        remote_key = local_node.getRemoteKeyFile(remote_address)
        #copy it
        rc = local_node.copyToRemoteNode(remote_key,self,'~/.ofstestkeys/',False)
        #add it to the keytable
        if rc != 0:
            print "Upload of key %s from local to %s failed!" % (local_node.getRemoteKeyFile(self.ext_ip_address), self.ext_ip_address)
            return rc
        else:
            #set the ssh key file to the uploaded location.
            self.keytable[remote_address] = "/home/%s/.ofstestkeys/%s" % (self.current_user,os.path.basename(remote_key))
            return 0
        
        
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
        
