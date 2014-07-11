#!/bin/sh

pvfs2_tailtest=${PVFS2_MOUNTPOINT}/tail_test
local_reference=${PVFS2_DEST}/tail_ref
datagen() {
	for I in `seq 1 25`; do
		echo "line$I" 
	done
}

datagen > $pvfs2_tailtest

tail $pvfs2_tailtest
