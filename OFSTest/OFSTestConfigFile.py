#!/usr/bin/python

from OFSTestConfig import *

class OFSTestConfigFile(OFSTestConfig):
    
    # This class reads the configuration from a file

    def __init__(self):
        super(OFSTestConfigFile,self).__init__()
        
        
    def setConfig(self,kwargs={}):
        
        #read the file into a dictionary
        
        filename = kwargs.get('filename')
        print "Filename is "+filename
        if filename == None:
            print "Cannot find filename" 
            return
        
        d = {}
        with open(filename) as f:
            for line in f:
                print line
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