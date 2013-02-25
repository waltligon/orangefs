#!/bin/sh

#echo LD_PRELOAD=${LD_PRELOAD}
#echo PVFS2TAB_FILE=${PVFS2TAB_FILE}
#echo LD_LIBRARY_PATH=${LD_LIBRARY_PATH}

pvfs2_testfile=${PVFS2_MOUNTPOINT}/append_test
local_reference=${PVFS2_DEST}/append_ref
datagen() {
	for I in `seq 1 25`; do
		echo "line$I" 
	done
}

datagen > $local_reference
datagen > $pvfs2_testfile
datagen >> $local_reference
datagen >> $pvfs2_testfile

diff -u $local_reference $pvfs2_testfile
