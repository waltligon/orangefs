#!/bin/bash

umount /tmp/mnt
killall -TERM pvfs2-client
sleep 2

if test "x`fuser /dev/pvfs2-req`" = "x"; then
    rmmod pvfs2
else
    echo "It seems that the pvfs2 device file is still open!"
    fuser /dev/pvfs2-req
fi


