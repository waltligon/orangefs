#!/bin/sh

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
