#!/bin/bash

umount /tmp/mnt
killall -TERM pvfs2-client

sleep 5
rmmod pvfs2


