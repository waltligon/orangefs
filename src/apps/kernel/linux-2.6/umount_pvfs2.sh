#!/bin/bash

umount /tmp/mnt
killall -TERM pvfs2-client
sleep 1
rmmod pvfs2


