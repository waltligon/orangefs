#!/bin/bash

umount /tmp/mnt
killall -TERM pvfs2-client
rmmod pvfs2


