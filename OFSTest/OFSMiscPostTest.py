#!/usr/bin/python

def checkSHM(testing_node,output=[]):
    rc = testing_node.runSingleCommand("[ \"$(ls -A /dev/shm/pvfs*)\" ]",output)
    return rc

tests = [ checkSHM ]
    
