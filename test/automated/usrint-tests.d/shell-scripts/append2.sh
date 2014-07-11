#!/bin/sh

pvfs2_testdir=${PVFS2_MOUNTPOINT}/append_dir
pvfs2_testfile=${pvfs2_testdir}/append_test2
local_reference=${PVFS2_DEST}/append_ref2

datagen() {
	for I in `seq 1 25`; do
		echo "line$I" 
	done
}

mkdir -p $pvfs2_testdir

datagen > $local_reference
datagen > $pvfs2_testfile
datagen >> $local_reference
datagen >> $pvfs2_testfile

diff -u $local_reference $pvfs2_testfile
