#!/bin/bash
bb-setup-pvfs -b /home/sson/as-branch/pvfs2 $1 $2
bbrun -v -n $3 mpiexec -n 1 ~/as-branch/pvfs2/test/client/mpi-io/mpi-io-test -c -f pvfs2:/mnt/pvfs2/test.out -p ~/pvfs2tab -i 5