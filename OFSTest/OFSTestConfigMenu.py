#!/usr/bin/python
#
#
# This class implements a dialog for Python based testing
#
#

#
from os.path import basename, isdir
from OFSTestConfig import *



class OFSTestConfigMenu(OFSTestConfig):
    
    # This class is simply a different wrapper around the OFSTestConfig class.
    def __init__(self):
        super(OFSTestConfigMenu,self).__init__()

        
    #======================================
    #
    # def get_yes_no_input
    #
    # This function asks the user a yes/no questions and validates input
    #
    # Return: Yes = True, No = False
    #
    #======================================
    def yes_no_prompt(self,prompt):

        while True:
            input = self.string_prompt(prompt+" [Y/N]:")
            if input.lower() == 'y':
                return True
            elif input.lower() == 'n':
                return False
            else:
                print "Invalid input. Please enter Y or N."

    def menu_selection(self,prompt,options=[],default=0):
        pass


    #======================================
    #
    # def get_file_location
    #
    # This function asks the user for a file location.
    # Can also validate existance or create new
    #
    # Return: filepath
    #
    #====================================== 

    def filepath_prompt(self,prompt,default="",validate=True):
        
        input = self.string_prompt(prompt,default)
        if validate == True:
            while True:    
                try:
                    with open(input): return input
                except IOError:
                    print "File %s Not Found." % input
                    input = self.string_prompt(prompt,default)
        else:
            return input


    #======================================
    #
    # def get_directory_location
    #
    # This function asks the user for a directory location.
    # Can also validate existance or create new
    #
    # Return: directory
    #
    #====================================== 

    def directory_prompt(self,prompt,default="",validate=True):
        
        input = self.string_prompt(prompt,default)
        if validate == True:
            exists = os.path.isdir(input)        
            while exists == False:
                print "Path %s Not Found." % input
                input = self.string_prompt(prompt,default)
                exists = os.path.isdir(input)        
        
        return input


    #======================================
    #
    # def get_ip_address
    #
    # This function asks the user for an ip address
    # Can also validate existance
    #
    # Return: ip address. Throws exception if not valid
    #
    #======================================     
    def ip_prompt(self,prompt,default="",validate=False):
        input = self.string_prompt(prompt,default)
        return input


    #======================================
    #
    # def get_host_name
    #
    # This function asks the user for a hostname
    # Can also validate existance
    #
    # Return: hostname. Throws exception if not valid
    #
    #======================================     
    def hostname_prompt(self,prompt,default="",validate=False):
        input = self.string_prompt(prompt,default)
        return input


    #======================================
    #
    # def get_string
    #
    # This function asks the user for string input
    #
    # Return: string
    #
    #======================================     
    def string_prompt(self,prompt,default=""):
        string = raw_input(prompt+"\t")
        return string.rstrip('\n')


    #======================================
    #
    # def get_int
    #
    # This function asks the user for an int
    #
    # Return: int
    #
    #======================================     
    def int_prompt(self,prompt,default=""):
        while True:
            try:
                input = int(self.string_prompt(prompt,default))
                return input
            except ValueError:
                print "%s is not a valid value. Please enter an integer value. "
                input = int(self.string_prompt(prompt,default))

    def add_remote_nodes(self,keyfile,is_ec2=False):
        done = False
        
        while not done:
            ip_address = self.ip_prompt(prompt="Please enter the ip address of the master node:",validate=True)
            self.node_ip_address.append(ip_address)
            username = self.string_prompt(prompt="Please enter login userid for this node:")
            self.node_usernames.append(username)
            print "Nodes are:"
            for i in range(len(self.node_ip_address)):
                print "%s@%s" % (self.node_usernames[i],self.node_ip_address[i])
                
            more_nodes = self.yes_no_prompt(prompt="Would you like to add another node?")
            
            while more_nodes == True:
             
                ip_address = self.ip_prompt(prompt="Please enter the ip address of the next node:",validate=True)
                self.node_ip_address.append(ip_address)
                username = self.string_prompt(prompt="Please enter login userid for this node:")
                my_node_manager.addRemoteNode(ip_address=ip_address,username=username,keyname=keyfile,is_ec2=is_ec2)
                self.node_usernames.append(username)
                print "Nodes are:"
                for i in range(len(self.node_ip_address)):
                    print "%s@%s" % (self.node_usernames[i],self.node_ip_address[i])
                
            more_nodes = self.yes_no_prompt(prompt="Would you like to add another node?")
                
            print "OrangeFS Network will contain the following nodes:"
            
            done = self.yes_no_prompt("Is this correct?",default="Y")
            if done == False:
                self.node_ip_address = []
                self.node_usernames = []


    def setConfig(self,kwargs={}):
        print "Welcome to OrangeFS Testing"
        self.using_ec2 = self.yes_no_prompt(prompt="Will you be using an EC2/OpenStack connection?")


        if self.using_ec2 == True:

            rc = -1
            
            self.ec2rc_sh = self.filepath_prompt(prompt="Please enter the location of the ec2rc.sh file:",validate=True)
            
            self.ssh_key_filepath = self.filepath_prompt(prompt="Please enter the location of the ssh key for EC2 instances:",validate=True)
            
            self.ec2_key_name = self.string_prompt(prompt="Please enter the name of the EC2 key:")
         
           # my_node_manager.addEC2Connection(ec2rc_sh,keyname,keyfile)
                
            # need to get system and machine types from EC2 here.
            
            new_instances = self.yes_no_prompt(prompt="Would you like to create new EC2 instances?")
            
            if new_instances:
            
                self.number_new_ec2_nodes = self.int_prompt(prompt="How many EC2 instances would you like?")
                self.ec2_image = self.string_prompt(prompt="Which image would you like to use?",default="cloud-ubuntu-12.04")
                self.ec2_machine = self.string_prompt(prompt="Which machine type would you like to use?",default="c1.small")
            
                #nodes  = my_node_manager.createNewEC2Nodes(number_nodes,image,machine)
            
            else:
                self.add_remote_nodes(keyfile=self.ssh_key_filepath,is_ec2=True)
                
            

                
            self.ec2_delete_after_test = self.yes_no_prompt("Would you like to delete the nodes after tests are complete?")

        else:
            self.ssh_key_filepath = self.filepath_prompt(prompt="Please enter the location of the ssh key for remote machines:",validate=True)
            self.add_remote_nodes(keyfile=self.ssh_key_filepath,is_ec2=False)    
            print "NOTICE: Before running tests, be sure test machines are updated and have all required software installed:"
            
        self.ofs_resource_location = self.string_prompt("Please provide the location of the OrangeFS source tree or tarball:")
        self.ofs_resource_type = self.string_prompt("What type of resource is this [TAR,SVN,LOCAL]?",default="LOCAL")

        self.run_sysint_tests = self.yes_no_prompt("Run sysint tests?")
        self.run_vfs_tests = self.yes_no_prompt("Run vfs tests?")

        self.configure_opts = self.string_prompt("Enter any special build options (./configure).  Enter for defaults.",default="")
        self.pvfs2genconfig_opts = self.string_prompt("Enter any special pvfs2genconfig options.  Enter for defaults.",default="")

def test_driver():
    menu = OFSTestConfigMenu()
    menu.setConfig()
    print menu.__dict__