#!/usr/bin/python
##
# @class OFSTestConfigFile
# 
# @brief This class reads the configuration from a file.
#
################################################################################

from OFSTestConfig import *

class OFSTestConfigFile(OFSTestConfig):
    
    # This class reads the configuration from a file

    def __init__(self):
        super(OFSTestConfigFile,self).__init__()
        
        
    ##
    #
    # @fn setConfig(self,kwargs={}):
    #
    # This function reads the configuration into a dictionary D, which
    # is passed to the setConfigFromDict in the parent class.
    # @param self The object pointer
    # @param kwargs Dictionary with variable=value pairing. 
    
    
    def setConfig(self,kwargs={}):
        
        #read the file into a dictionary
        
        filename = kwargs.get('filename')
        print "==================================================================="
        print "Reading configuration from file "+filename
        if filename == None:
            print "Cannot find filename" 
            return
        
        d = {}
        with open(filename) as f:
            for line in f:
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
        
        
'''
config = OFSTestConfigFile()

config.getConfig(kwargs={"filename":"OFSTest.conf"})
config.printDict()
'''
