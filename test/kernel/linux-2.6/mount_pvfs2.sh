#!/bin/bash

if [ "`echo $PATH | grep -c /usr/src/modtools/sbin`" = "0" ]; then
   echo "Loading up bashrc to get latest mod-tools"
   . ~/.bashrc
fi

if [ "`lsmod | grep -c pvfs2`" = "0" ]; then
   echo "Loading the pvfs2 module"
   insmod ../../../src/kernel/linux-2.6/pvfs2.ko $1
fi

CUR_DEV="pvfs2-flow"
for f in `cat /proc/devices | grep pvfs2 | awk '{print $1}'`; do
   if [ ! -c /dev/$CUR_DEV ]; then
      echo "Creating device file /dev/$CUR_DEV with major number $f"
      mknod /dev/$CUR_DEV c $f 0 -m 666
   fi
   CUR_DEV="pvfs2-req"
done

if [ ! -d /tmp/mnt ]; then
   echo "Creating /tmp/mnt"
   mkdir /tmp/mnt
fi

if [ "`mount | grep -c pvfs2`" = "0" ]; then
   echo "Mounting pvfs2 on /tmp/mnt"
   mount -t pvfs2 pvfs2 /tmp/mnt
else
   echo "Uh...I think pvfs2 is already mounted."
fi


