def functions(self,testing_network):
    pass
def heidleberg_IO(self,testing_network):
    pass
def ior_mpiio(self,testing_network):
    pass
def ior_mpiio_3(self,testing_network):
    pass
def noncontig(self,testing_network):
    pass
def romio_async(self,testing_network):
    pass
def romio_coll_test(self,testing_network):
    pass
def romio_error(self,testing_network):
    pass
def romio_excl(self,testing_network):
    pass
def romio_file_info(self,testing_network):
    pass
def romio_noncontig_coll2(self,testing_network):
    pass
def romio_psimple(self,testing_network):
    pass
def romio_simple(self,testing_network):
    pass
def romio_split_coll(self,testing_network):
    pass
def romio_status(self,testing_network):
    pass
def stadler_file_view_test(self,testing_network):
    pass
    
def romio_testsuite(testing_node,output=[]):

    #/opt/mpi/openmpi-1.6.5/ompi/mca/io/romio/romio/test
    testing_node.changeDirectory("/opt/mpi/openmpi-1.6.5/ompi/mca/io/romio/romio/test")
    
    rc = 0
    print "%s -machinefile=%s -fname=%s/romioruntests" % (testing_node.romio_runtests_pvfs2,testing_node.created_openmpihosts,testing_node.ofs_mount_point)
    rc = testing_node.runSingleCommand("%s -machinefile=%s -fname=%s/romioruntests" % (testing_node.romio_runtests_pvfs2,testing_node.created_openmpihosts,testing_node.ofs_mount_point),output)
    
    return rc

    

tests = [ romio_testsuite

 ]
