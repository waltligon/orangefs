#!/bin/bash

umount /tmp/mnt
killall -TERM pvfs2-client
sleep 2
rmmod pvfs2


