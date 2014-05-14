#!/usr/bin/python
##
#
# @class OFSTestRemoteNode
#
# This class is an abstraction of all remote machines, virtual and bare metal. 
#
# Currently OFSTestRemoteNode only supports Linux machines.

import os
import subprocess
import shlex
import cmd
import time
import sys
import OFSTestNode
import logging


class OFSTestRemoteNode(OFSTestNode.OFSTestNode):
 
    ##
    # @fn __init__(self,username,ip_address,key,local_node,is_cloud=False,ext_ip_address=None):
    #
    # Initialization routine.
    #
    # @param self The object pointer
    # @param username Name of current user
    # @param ip_address IP address of node on cluster network.
    # @param key Location on local machine of SSH key used to access node.
    # @param local_node OFSTestLocalNode object that represents the local machine
    # @param is_cloud Is this an cloud/OpenStack node?
    # @param ext_ip_address IP address of node accessible from local machine if different from cluster IP.
    # 
    # @return None, but will print ssh command used to access node.
 
    def __init__(self,username,ip_address,key,local_node,is_cloud=False,ext_ip_address=None):

        print "-----------------------------------------------------------"    
        super(OFSTestRemoteNode,self).__init__()
        
        # need ip_address, user_name, hostname, and local keyfile to connect
        self.ip_address = ip_address
        
        # do we have an external ip address?
        if ext_ip_address == None:
            self.ext_ip_address = ip_address
        else:
            self.ext_ip_address = ext_ip_address
        self.current_user = username
        self.is_cloud = is_cloud
        self.sshLocalKeyFile = key
        self.localnode = local_node
        self.ssh_command = "/usr/bin/ssh -i %s %s@%s" % (self.sshLocalKeyFile,self.current_user,self.ext_ip_address)
        
        super(OFSTestRemoteNode,self).currentNodeInformation()
        self.getKeyFileFromLocal(local_node)
        
        
        msg = "SSH: "+self.ssh_command
        logging.info(msg)
        print msg
    ##
    #
    # @fn getKeyFileFromLocal(self,local_node):
    #
    # Uploads the SSH key from the localnode to the remote node and adds information to local keytable.
    #
    # @param self The object pointer
    # @param local_node OFSTestLocalNode object that represents the local machine
    
        
    def getKeyFileFromLocal(self,local_node):
        local_node.addRemoteKey(self.ext_ip_address,self.sshLocalKeyFile)
        self.uploadNodeKeyFromLocal(local_node)
        
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
        
        # read the file from the server

        alias = self.runSingleCommandBacktick('cat '+config_file_name+' | grep \"Alias \"')
        logging.debug("Alias is "+ alias)
        
        alias_lines = alias.split('\n')
        
        aliases = []
                
                
        for line in alias_lines:
            if "Alias " in line:
                # split the line into Alias, AliasName, url
                element = line.split()
                # What we want is element 1
                aliases.append(element[1].rstrip())
                
            
        logging.info( "OrangeFS Aliases: ")
        logging.info(aliases)    
        return aliases
         
        
        
    ##       
    # @fn runAllBatchCommands(self,output=[]):
    #
    # Writes stored batch commands to a file, then runs them.
    # 
    # @param self The object pointer
    # @param output Output of command

    def runAllBatchCommands(self,output=[],debug=False):
        OFSTestNode.batch_count = OFSTestNode.batch_count+1

        
        # Open file with mode 700
        batchfilename = "./runcommand%d.sh" % OFSTestNode.batch_count
        script_file = open(batchfilename,'w')
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
        
        logging.debug("----- Start generated batchfile: %s -----------------------" % batchfilename)
        script_file = open(batchfilename,'r')
        for line in script_file:
            logging.debug(line)
        script_file.close()
        logging.debug("---- End generated batchfile: %s -------------------------" % batchfilename)            


        command_line = "/usr/bin/ssh -i %s %s@%s -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no \"bash -s\" < %s" % (self.sshLocalKeyFile,self.current_user,self.ext_ip_address,batchfilename)
        
        logging.debug("Command:" + command_line)
        p = subprocess.Popen(command_line,shell=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,bufsize=-1)
        
        # clear the output list, then append stdout,stderr to list to get pass-by-reference to work
        del output[:]
        output.append(command_line)
        for i in p.communicate():
            output.append(i)
        
        logging.debug("RC: %r" % p.returncode)
        logging.debug("STDOUT: %s" % output[1] )
        logging.debug("STDERR: %s" % output[2] )
        # now clear out the batch commands list
        self.batch_commands = []    

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
        
        if remote_user == None:
            remote_user = self.current_user
        
            
        
        #start with the ssh command and open quote
        
        command_chunks = ["/usr/bin/ssh -i %s %s@%s -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -o BatchMode=yes \"" %  (self.sshLocalKeyFile,remote_user,self.ext_ip_address)]

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
            rflag = "-a"
        else:
            rflag = ""
          
        rsync_command = "rsync %s -e \\\"ssh -i %s -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no\\\" %s %s@%s:%s" % (rflag,self.getRemoteKeyFile(destination_node.ext_ip_address),source,destination_node.current_user,destination_node.ip_address,destination)
        #print rsync_command
        output = []
        rc = self.runSingleCommand(rsync_command,output)
        if rc != 0:
            logging.exception( "Could not copy to remote node")
            logging.exception( output)
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
          
        rsync_command = "rsync %s -e \\\"ssh -i %s -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no\\\"  %s@%s:%s %s" % (rflag,self.getRemoteKeyFile(source_node.ext_ip_address),source_node.current_user,source_node.ip_address,source,destination)
        
        output = []
        rc = self.runSingleCommand(rsync_command,output)
        if rc != 0:
            logging.exception( "Could not copy to remote node")
            logging.exception( output)
        return rc
      
    ##
    #
    # @fn uploadNodeKeyFromLocal(self,local_node):
    #
    # This function uploads a the key that accesses the current node from the local machine to the current node.
    # This allows the this node to distribute its own key to other nodes.
    #
    # @param self The object pointer
    # @param local_node OFSTestLocalNode object that represents the local machine  
    #
    
    def uploadNodeKeyFromLocal(self, local_node):
        
        # This function uploads a key 
        #print "uploading key %s from local to %s" % (local_node.getRemoteKeyFile(self.ext_ip_address), self.ext_ip_address)
        # copy the file from the local node to the current node
        self.sshLocalKeyFile=local_node.getRemoteKeyFile(self.ext_ip_address)
        rc = local_node.copyToRemoteNode(self.sshLocalKeyFile,self,'~/.ssh/',False)
        keybasename = os.path.basename(self.sshLocalKeyFile)
        
        if rc != 0:
            logging.exception( "Upload of key %s from local to %s failed!" % (local_node.getRemoteKeyFile(self.ext_ip_address), self.ext_ip_address))
            return rc
 
        #set the ssh key file to the uploaded location.
        self.sshNodeKeyFile = "/home/%s/.ssh/%s" % (self.current_user,keybasename)
        
        #symlink to id_rsa
        
        #self.runSingleCommand("bash -c 'cat %s >> /home/%s/.ssh/authorized_keys'" % (self.sshNodeKeyFile,self.current_user))
        
        self.runSingleCommand("ln -s %s /home/%s/.ssh/id_rsa" % (self.sshNodeKeyFile,self.current_user))
    

        return 0
    
    ##
    #
    # @fn uploadRemoteKeyFromLocal(self,local_node,remote_address):
    #
    # This function uploads the key used to access a remote node at remote_address from the local machine. 
    # It then adds this to the nodes key table.
    #
    # @param self The object pointer
    # @param local_node OFSTestLocalNode object that represents the local machine  
    # @param remote_address Address of remote node associated with the SSH key. 
    
      
    def uploadRemoteKeyFromLocal(self,local_node,remote_address):
        # This function uploads a key for a remote node and adds it to the table
        # get the remote key name
        #print "uploading key %s from local to %s" % (local_node.getRemoteKeyFile(self.ext_ip_address), self.ext_ip_address)
        remote_key = local_node.getRemoteKeyFile(remote_address)
        #copy it
        rc = local_node.copyToRemoteNode(remote_key,self,'~/.ssh/',False)
        #add it to the keytable
        if rc != 0:
            print "Upload of key %s from local to %s failed!" % (local_node.getRemoteKeyFile(self.ext_ip_address), self.ext_ip_address)
            return rc
        else:
            #set the ssh key file to the uploaded location.
            self.keytable[remote_address] = "/home/%s/.ssh/%s" % (self.current_user,os.path.basename(remote_key))
        
        
        
        return 0

    ##
    #
    # @fn allowRootSshAccess(self)
    #
    # This function copies the user's .ssh key to root's .ssh directory. Assumes passwordless sudo already enabled. 
    #
    # @param self The object pointer        
        
    def allowRootSshAccess(self):
        # This one we activate! 
        print "Allowing root SSH access with user %s's credentials" % self.current_user
        self.runSingleCommandAsBatch(command="sudo cp -r /home/%s/.ssh /root/ " % self.current_user)
        
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
        
