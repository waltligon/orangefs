#!/bin/bash

killall -9 pvfs2-client pvfs2-client-core
umount /tmp/mnt

rmmod pvfs2


