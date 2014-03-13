#!/usr/bin/python
#
# OFSFunctionGroup.py
#
# Abstract class for all test function groups. Should not be implemented 
# directly.
#
#
#

import OFSTestNode

class OFSFunctionGroup(object):

    #--------------------------------------------------------------------------
    #
    # __init__(testing_node)
    #
    # params:
    #
    #   testing_node = OFSTestNode on which the tests will be run
    #
    # instance variables:
    #
    #   self.testing_node = testing_node
    #   self.header = Name of header printed in output file
    #   self.prefix = Name of prefix for test name
    #   self.run_client = False
    #   self.mount_fs = Does the file system need to be mounted?
    #   self.mount_as_fuse = False
    #
    #   self.tests = Array of functions to be run.
    #
    # Should be called by subclasses
    #
    #--------------------------------------------------------------------------

    def __init__(self,testing_node):
        
        self.testing_node = testing_node
        self.header = "OFS Function Group (abstract)"
        self.prefix = "fgroup"
        self.mount_fs = False
        self.run_client = False
        self.mount_as_fuse = False
        self.tests = []
    
    #--------------------------------------------------------------------------
    # 
    # setupTestEnvironment()
    #
    # Method sets up the test environment on the node before running tests
    # based on the values of the instance variables.
    #
    #--------------------------------------------------------------------------
    def setupTestEnvironment(self):
        
        if self.run_client == True:
            self.testing_node.startOFSClient()
        else:
            self.testing_node.stopOFSClient()
        
        if self.mount_fs == True:
            self.testing_node.mountOFSFilesystem(mount_fuse=self.mount_as_fuse)
        else:
            self.testing_node.unmountOFSFilesystem()
    
