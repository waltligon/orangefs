#/bin/bash 

#parameters are MY_PVFS2_DEST and MY_CVS_TAG

. `pwd`/testfunctions.sh

echo "Attempting to start pvfs2 on $HOSTNAME"
PVFS2_DEST=$1
CVS_TAG=$2
cd $PVFS2_DEST
teardown_pvfs2 && start_pvfs2