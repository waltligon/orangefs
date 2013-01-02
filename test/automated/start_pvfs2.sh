#/bin/bash 

. `pwd`/testfunctions.sh

echo "Attempting to start pvfs2 on $HOSTNAME"

cd $PVFS_DEST
pwd
ls -l 
teardown_pvfs2 && start_pvfs2