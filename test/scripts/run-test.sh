#!/bin/bash
export VFS_HOSTS
export RUN_MPI_TEST
export RUN_VFS_TEST
export SVNBRANCH
export WITH_DB=/opt/db4
export LD_LIBRARY_PATH=$WITH_DB/lib
export SERVER_DEBUG_PARAMS
mkdir ~/test2
cd ~/test2
echo svn export --force -q http://www.orangefs.org/svn/orangefs/${SVNBRANCH}/test/run-nightly-setup
svn export --force -q http://www.orangefs.org/svn/orangefs/${SVNBRANCH}/test/run-nightly-setup
./run-nightly-setup ${SVNBRANCH}
cd ~/test2/pvfs2/test
ls
./run-nightly ${SVNBRANCH}
echo "Run nightly completed. Exiting."
exit
exit
exit
