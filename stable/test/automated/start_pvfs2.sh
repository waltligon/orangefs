#/bin/bash 

#parameters are MY_PVFS2_DEST and MY_CVS_TAG

. `pwd`/testfunctions.sh

echo "Attempting to start pvfs2 on $HOSTNAME"



PVFS2_DEST=$1
CVS_TAG=$2
if [ ! $LD_LIBRARY_PATH ] 
then
	export LD_LIBRARY_PATH=${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/lib:/opt/db4/lib
else
	export LD_LIBRARY_PATH=${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/lib:/opt/db4/lib:${LD_LIBRARY_PATH}
fi

cd $PVFS2_DEST
teardown_pvfs2 && start_pvfs2